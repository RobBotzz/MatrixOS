// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/provisioning.h"

#include "net/fake_wifi.h"
#include "net/nmcli_wifi.h"
#include "os/state.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace matrixos;
using matrixos::test::TempDir;

namespace
{

/// No grace period and no polling delay: the boot job runs to its conclusion the
/// moment it is started, so every test below is deterministic and none of them
/// waits on a clock.
constexpr Provisioning::Timing kInstant{std::chrono::milliseconds(0), std::chrono::milliseconds(0)};

} // namespace

TEST_CASE("with no radio to drive, setup is off and the device just runs")
{
    FakeWifi wifi;
    wifi.radio = false;
    StateStore store = StateStore::inMemory();

    Provisioning provisioning(wifi, store, kInstant);
    provisioning.begin();
    provisioning.waitForIdle();

    CHECK(provisioning.status().state == SetupState::Unmanaged);
    CHECK_FALSE(provisioning.needsSetup());
    CHECK(wifi.access_points == 0);
}

TEST_CASE("a fresh device opens its access point and scans")
{
    FakeWifi wifi;
    StateStore store = StateStore::inMemory();

    Provisioning provisioning(wifi, store, kInstant);
    provisioning.begin();
    provisioning.waitForIdle();

    const SetupStatus status = provisioning.status();
    CHECK(status.state == SetupState::AccessPoint);
    CHECK(provisioning.needsSetup());
    CHECK(wifi.access_points == 1);

    // The portal needs a list the moment it is opened, so the scan happens with
    // the access point rather than on the first request.
    CHECK(provisioning.networks().size() == wifi.networks.size());
}

TEST_CASE("a device that is already on a network needs no setup")
{
    FakeWifi wifi;
    wifi.pretendJoined("Kitchen");
    StateStore store = StateStore::inMemory();

    Provisioning provisioning(wifi, store, kInstant);
    provisioning.begin();
    provisioning.waitForIdle();

    CHECK(provisioning.status().state == SetupState::Connected);
    CHECK(provisioning.status().ssid == "Kitchen");
    CHECK_FALSE(provisioning.needsSetup());
    CHECK(wifi.access_points == 0);
}

TEST_CASE("a remembered network that is out of range falls back to the access point")
{
    FakeWifi wifi;
    StateStore store = StateStore::inMemory();
    store.section("wifi").setString("ssid", "Kitchen");

    Provisioning provisioning(wifi, store, kInstant);
    provisioning.begin();
    provisioning.waitForIdle();

    CHECK(provisioning.status().state == SetupState::AccessPoint);
    CHECK(provisioning.status().message == "network not found");
    CHECK(wifi.access_points == 1);
}

TEST_CASE("the whole setup flow: pick a network, enter the password, connected")
{
    FakeWifi wifi;
    wifi.password = "letmein";
    StateStore store = StateStore::inMemory();

    Provisioning provisioning(wifi, store, kInstant);
    provisioning.begin();
    provisioning.waitForIdle();
    REQUIRE(provisioning.status().state == SetupState::AccessPoint);

    wifi.holdConnect();
    REQUIRE(provisioning.requestConnect("Kitchen", "letmein"));

    // The request returns at once — the browser is waiting on the other end.
    CHECK(provisioning.status().state == SetupState::Connecting);
    CHECK(provisioning.status().ssid == "Kitchen");

    wifi.release();
    provisioning.waitForIdle();

    CHECK(provisioning.status().state == SetupState::Connected);
    CHECK_FALSE(provisioning.needsSetup());
    CHECK(store.section("wifi").getString("ssid", "") == "Kitchen");
}

TEST_CASE("a wrong password reports the failure and brings the access point back")
{
    FakeWifi wifi;
    wifi.password = "letmein";
    StateStore store = StateStore::inMemory();

    Provisioning provisioning(wifi, store, kInstant);
    provisioning.begin();
    provisioning.waitForIdle();

    REQUIRE(provisioning.requestConnect("Kitchen", "wrong"));
    provisioning.waitForIdle();

    const SetupStatus status = provisioning.status();
    CHECK(status.state == SetupState::Failed);
    CHECK_FALSE(status.message.empty());
    CHECK(provisioning.needsSetup());

    // FR-34: reachable again without a power cycle.
    CHECK(wifi.access_points == 2);
}

TEST_CASE("a second attempt after a failure succeeds")
{
    FakeWifi wifi;
    wifi.password = "letmein";
    StateStore store = StateStore::inMemory();

    Provisioning provisioning(wifi, store, kInstant);
    provisioning.begin();
    provisioning.waitForIdle();

    REQUIRE(provisioning.requestConnect("Kitchen", "wrong"));
    provisioning.waitForIdle();
    REQUIRE(provisioning.status().state == SetupState::Failed);

    REQUIRE(provisioning.requestConnect("Kitchen", "letmein"));
    provisioning.waitForIdle();

    CHECK(provisioning.status().state == SetupState::Connected);
}

TEST_CASE("the failed password is never written anywhere")
{
    TempDir dir;
    FakeWifi wifi;
    wifi.password = "letmein";

    {
        StateStore store(dir.path());
        Provisioning provisioning(wifi, store, kInstant);
        provisioning.begin();
        provisioning.waitForIdle();

        REQUIRE(provisioning.requestConnect("Kitchen", "letmein"));
        provisioning.waitForIdle();
    }

    // NetworkManager owns the credential; the store keeps only the name, which
    // is what the panel and the configuration page display (FR-24).
    StateStore reopened(dir.path());
    CHECK(reopened.section("wifi").getString("ssid", "") == "Kitchen");
    CHECK(reopened.section("wifi").getString("psk", "absent") == "absent");
}

TEST_CASE("an empty SSID is refused rather than attempted")
{
    FakeWifi wifi;
    StateStore store = StateStore::inMemory();

    Provisioning provisioning(wifi, store, kInstant);
    provisioning.begin();
    provisioning.waitForIdle();

    CHECK_FALSE(provisioning.requestConnect("", "secret"));
    CHECK(wifi.attempts == 0);
}

TEST_CASE("an open network needs no password")
{
    FakeWifi wifi;
    StateStore store = StateStore::inMemory();

    Provisioning provisioning(wifi, store, kInstant);
    provisioning.begin();
    provisioning.waitForIdle();

    REQUIRE(provisioning.requestConnect("Cafe Gast", ""));
    provisioning.waitForIdle();

    CHECK(provisioning.status().state == SetupState::Connected);
}

TEST_CASE("a factory reset forgets the network and returns to setup")
{
    FakeWifi wifi;
    StateStore store = StateStore::inMemory();

    Provisioning provisioning(wifi, store, kInstant);
    provisioning.begin();
    provisioning.waitForIdle();
    REQUIRE(provisioning.requestConnect("Kitchen", ""));
    provisioning.waitForIdle();
    REQUIRE(provisioning.status().state == SetupState::Connected);

    REQUIRE(provisioning.requestReset());
    provisioning.waitForIdle();

    CHECK(wifi.forgets == 1);
    CHECK(provisioning.status().state == SetupState::AccessPoint);
    CHECK(provisioning.needsSetup());
    CHECK(store.section("wifi").getString("ssid", "x").empty());
}

TEST_CASE("an access point that will not come up is a visible failure, not a silent one")
{
    FakeWifi wifi;
    wifi.password = "letmein";
    StateStore store = StateStore::inMemory();

    // The radio answers — bringing the profile up is what fails. That is the
    // provisioning mistake that will be made, and it must be visible.
    Provisioning provisioning(wifi, store, kInstant);
    provisioning.begin();
    provisioning.waitForIdle();
    REQUIRE(provisioning.status().state == SetupState::AccessPoint);

    wifi.access_point_profile = false;
    REQUIRE(provisioning.requestConnect("Kitchen", "no"));
    provisioning.waitForIdle();

    CHECK(provisioning.status().state == SetupState::Failed);
    CHECK(provisioning.status().message == "access point unavailable");
}

TEST_CASE("state names are stable, because the panel and the JSON share them")
{
    CHECK(toString(SetupState::Unmanaged) == "unmanaged");
    CHECK(toString(SetupState::AccessPoint) == "setup");
    CHECK(toString(SetupState::Connecting) == "connecting");
    CHECK(toString(SetupState::Connected) == "connected");
    CHECK(toString(SetupState::Failed) == "failed");
}

TEST_CASE("nmcli terse output splits on colons the value did not put there")
{
    const auto plain = splitTerseLine("Kitchen:82:WPA2");
    REQUIRE(plain.size() == 3);
    CHECK(plain[0] == "Kitchen");
    CHECK(plain[1] == "82");
    CHECK(plain[2] == "WPA2");

    // An SSID may contain a colon, and nmcli escapes it. Splitting naively would
    // turn one network into two fields and lose it.
    const auto escaped = splitTerseLine("Bob\\:s Net:47:WPA2");
    REQUIRE(escaped.size() == 3);
    CHECK(escaped[0] == "Bob:s Net");
    CHECK(escaped[1] == "47");

    const auto backslash = splitTerseLine("Back\\\\slash:12:");
    REQUIRE(backslash.size() == 3);
    CHECK(backslash[0] == "Back\\slash");
    CHECK(backslash[2].empty()); // an open network reports no security
}

TEST_CASE("a network NetworkManager knows about is waited for, even on the first run")
{
    // The defect this exists for: on the first boot after an upgrade wifi.conf
    // does not exist yet, while the device has been on the same network for
    // months. Trusting our own file alone opened the access point and took the
    // radio away from a connection that was seconds from succeeding.
    FakeWifi wifi;
    wifi.pretendConfigured("Kitchen"); // configured, not yet associated
    StateStore store = StateStore::inMemory();
    REQUIRE(store.section("wifi").getString("ssid", "").empty());

    Provisioning provisioning(wifi, store, kInstant);
    provisioning.begin();
    provisioning.waitForIdle();

    // The grace period is zero here, so it expires at once and the access point
    // is the correct outcome. What must not happen is skipping the wait — and
    // the message is what tells the two apart: "network not found" can only come
    // from the path that waited.
    CHECK(provisioning.status().state == SetupState::AccessPoint);
    CHECK(provisioning.status().message == "network not found");
}

TEST_CASE("a network that associates during the grace period is never interrupted")
{
    FakeWifi wifi;
    wifi.pretendConfigured("Kitchen");
    StateStore store = StateStore::inMemory();

    // One poll of headroom, so the association can land inside the window.
    constexpr Provisioning::Timing kBrief{std::chrono::milliseconds(2000),
                                          std::chrono::milliseconds(1)};
    Provisioning provisioning(wifi, store, kBrief);
    provisioning.begin();

    wifi.pretendJoined("Kitchen"); // NetworkManager gets there on its own
    provisioning.waitForIdle();

    CHECK(provisioning.status().state == SetupState::Connected);
    CHECK_FALSE(provisioning.needsSetup());
    CHECK(wifi.access_points == 0); // the radio was never taken away
}

TEST_CASE("a device with no network configured anywhere opens the access point at once")
{
    FakeWifi wifi;
    StateStore store = StateStore::inMemory();

    // A genuinely fresh unit: nothing to wait for, so waiting would only delay
    // the screen that tells the user what to do.
    Provisioning provisioning(wifi, store, kInstant);
    provisioning.begin();
    provisioning.waitForIdle();

    CHECK(provisioning.status().state == SetupState::AccessPoint);
    CHECK(provisioning.status().message.empty());
}
