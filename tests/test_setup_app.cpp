// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/setup/setup_app.h"

#include "gfx/font.h"
#include "gfx/surface.h"
#include "net/fake_wifi.h"
#include "os/state.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace matrixos;

namespace
{

constexpr Provisioning::Timing kInstant{std::chrono::milliseconds(0), std::chrono::milliseconds(0)};
constexpr Duration kFrame{1.0F / 60.0F};

Identity testIdentity()
{
    return Identity{"a3f1", "matrixos-a3f1", "MatrixOS-a3f1"};
}

Surface draw(SetupApp &app)
{
    app.update(kFrame);
    Surface frame(64, 32);
    app.render(frame);
    return frame;
}

bool showsAt(const Surface &frame, int y, std::string_view text, int scale = 1)
{
    Surface expected(frame.width(), frame.height());
    drawTextCentered(expected, y, text, Color{255, 255, 255}, scale);

    for (int row = 0; row < frame.height(); ++row)
    {
        for (int column = 0; column < frame.width(); ++column)
        {
            const bool wanted = !(expected.pixel(column, row) == Color::black());
            if (wanted && frame.pixel(column, row) == Color::black())
            {
                return false;
            }
        }
    }
    return true;
}

struct Fixture
{
    FakeWifi wifi;
    StateStore store = StateStore::inMemory();
    Provisioning provisioning{wifi, store, kInstant};
    SetupApp app{provisioning, testIdentity()};

    void boot()
    {
        provisioning.begin();
        provisioning.waitForIdle();
        app.onEnter();
    }
};

} // namespace

TEST_CASE("the setup screen answers what to join, and which unit this is")
{
    Fixture fixture;
    fixture.boot();

    const Surface frame = draw(fixture.app);

    CHECK(fixture.app.shown() == SetupState::AccessPoint);
    CHECK(showsAt(frame, 2, "JOIN WIFI"));
    CHECK(showsAt(frame, 11, "MatrixOS"));

    // The suffix is the only part that differs between units, so it is the part
    // that gets the double-height type (Q-9).
    CHECK(showsAt(frame, 19, "a3f1", 2));
}

TEST_CASE("the full access point name would not fit, which is why it is split")
{
    // The arithmetic behind the layout above, asserted so a font change cannot
    // silently push the name off the panel.
    CHECK(textWidth("MatrixOS-a3f1") > 64);
    CHECK(textWidth("MatrixOS") <= 64);
    CHECK(textWidth("a3f1", 2) <= 64);
}

TEST_CASE("a failed attempt says so on the panel, not only in the log")
{
    Fixture fixture;
    fixture.wifi.password = "letmein";
    fixture.boot();

    REQUIRE(fixture.provisioning.requestConnect("Kitchen", "wrong"));
    fixture.provisioning.waitForIdle();

    const Surface frame = draw(fixture.app);

    CHECK(fixture.app.shown() == SetupState::Failed);
    CHECK(showsAt(frame, 2, "TRY AGAIN"));
    CHECK(showsAt(frame, 19, "a3f1", 2)); // and the name to rejoin is still there
}

TEST_CASE("while connecting, the network being joined is on screen")
{
    Fixture fixture;
    fixture.boot();
    fixture.wifi.holdConnect();

    REQUIRE(fixture.provisioning.requestConnect("Kitchen", "letmein"));
    const Surface frame = draw(fixture.app);

    CHECK(fixture.app.shown() == SetupState::Connecting);
    CHECK(showsAt(frame, 5, "CONNECTING"));
    CHECK(showsAt(frame, 16, "Kitchen"));

    fixture.wifi.release();
    fixture.provisioning.waitForIdle();
}

TEST_CASE("success is stated, so nobody wonders whether it worked")
{
    Fixture fixture;
    fixture.boot();

    REQUIRE(fixture.provisioning.requestConnect("Kitchen", ""));
    fixture.provisioning.waitForIdle();

    const Surface frame = draw(fixture.app);

    CHECK(fixture.app.shown() == SetupState::Connected);
    CHECK(showsAt(frame, 3, "CONNECTED"));
    CHECK(showsAt(frame, 13, "Kitchen"));
}

TEST_CASE("a connected device still says which unit it is")
{
    // The name is what somebody needs to reach the device — matrixos-<suffix> —
    // and before this it was only on screen while the device was in setup mode,
    // which is precisely when nobody is looking for it.
    Fixture fixture;
    fixture.boot();

    REQUIRE(fixture.provisioning.requestConnect("Kitchen", ""));
    fixture.provisioning.waitForIdle();

    CHECK(showsAt(draw(fixture.app), 23, "a3f1"));
}

TEST_CASE("a network name longer than the panel is cut, not wrapped off the edge")
{
    Fixture fixture;
    fixture.wifi.networks = {{"Ridiculously Long Network Name", 60, false}};
    fixture.boot();

    REQUIRE(fixture.provisioning.requestConnect("Ridiculously Long Network Name", ""));
    fixture.provisioning.waitForIdle();

    const Surface frame = draw(fixture.app);
    CHECK(showsAt(frame, 13, "Ridiculous"));
}

TEST_CASE("the walking dots move, which is what tells a stalled device from a busy one")
{
    Fixture fixture;
    fixture.boot();
    fixture.wifi.holdConnect();
    REQUIRE(fixture.provisioning.requestConnect("Kitchen", "letmein"));

    Surface first(64, 32);
    fixture.app.update(kFrame);
    fixture.app.render(first);

    // Past the dot period, so a different dot is lit.
    fixture.app.update(Duration{0.4F});
    Surface later(64, 32);
    fixture.app.render(later);

    bool differs = false;
    for (int y = 0; y < 32 && !differs; ++y)
    {
        for (int x = 0; x < 64 && !differs; ++x)
        {
            differs = !(first.pixel(x, y) == later.pixel(x, y));
        }
    }
    CHECK(differs);

    fixture.wifi.release();
    fixture.provisioning.waitForIdle();
}

TEST_CASE("without a radio the screen says so rather than inviting a join")
{
    Fixture fixture;
    fixture.wifi.radio = false;
    fixture.boot();

    const Surface frame = draw(fixture.app);

    CHECK(fixture.app.shown() == SetupState::Unmanaged);
    CHECK_FALSE(showsAt(frame, 2, "JOIN WIFI"));
}
