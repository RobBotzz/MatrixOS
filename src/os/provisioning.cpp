// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/provisioning.h"

#include "os/log.h"
#include "os/state.h"

#include <utility>

namespace matrixos
{
namespace
{

constexpr std::string_view kSection = "wifi";
constexpr std::string_view kSsidKey = "ssid";

} // namespace

std::string_view toString(SetupState state)
{
    switch (state)
    {
    case SetupState::Unmanaged:
        return "unmanaged";
    case SetupState::Waiting:
        return "waiting";
    case SetupState::AccessPoint:
        return "setup";
    case SetupState::Connecting:
        return "connecting";
    case SetupState::Connected:
        return "connected";
    case SetupState::Failed:
        return "failed";
    }
    return "unknown";
}

Provisioning::Provisioning(WifiControl &wifi, StateStore &store)
    : Provisioning(wifi, store, Timing{})
{
}

Provisioning::Provisioning(WifiControl &wifi, StateStore &store, Timing timing)
    : wifi_(wifi), saved_(store.section(kSection)), timing_(timing)
{
}

Provisioning::~Provisioning()
{
    stopping_ = true;
    wake_.notify_all();
    waitForIdle();
}

SetupStatus Provisioning::status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

std::vector<WifiNetwork> Provisioning::networks() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return networks_;
}

bool Provisioning::needsSetup() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.state == SetupState::AccessPoint || status_.state == SetupState::Connecting ||
           status_.state == SetupState::Failed;
}

void Provisioning::set(SetupState state, std::string ssid, std::string message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = state;
    status_.ssid = std::move(ssid);
    status_.message = std::move(message);
}

bool Provisioning::nap(std::chrono::milliseconds duration)
{
    std::unique_lock<std::mutex> lock(mutex_);
    wake_.wait_for(lock, duration, [this] { return stopping_.load(); });
    return !stopping_;
}

void Provisioning::post(std::function<void()> job)
{
    if (worker_.joinable())
    {
        worker_.join(); // the previous job has finished; this returns at once
    }

    worker_ = std::thread(
        [this, job = std::move(job)]
        {
            job();
            busy_ = false;
        });
}

void Provisioning::waitForIdle()
{
    if (worker_.joinable())
    {
        worker_.join();
    }
}

void Provisioning::begin()
{
    if (busy_.exchange(true))
    {
        return;
    }
    post([this] { runBoot(); });
}

void Provisioning::runBoot()
{
    // Asking the radio whether it exists means spawning nmcli, which costs
    // seconds. It belongs on the worker for the same reason everything else
    // here does: nothing may sit between power-on and the first picture
    // (NFR-8).
    if (!wifi_.available())
    {
        // No radio to drive. The device runs normally and simply never shows a
        // setup screen — a host build must not ask the user to join an access
        // point that does not exist.
        set(SetupState::Unmanaged, {});
        logInfo("provisioning: no WiFi under our control, setup disabled");
        return;
    }

    const std::string joined = wifi_.connectedSsid();
    if (!joined.empty())
    {
        set(SetupState::Connected, joined);
        saved_.setString(kSsidKey, joined);
        saved_.save();
        logInfo("provisioning: already on '{}'", joined);
        return;
    }

    // Two sources, and asking only the first one was a real defect: on the first
    // boot after an upgrade `wifi.conf` does not exist yet, while NetworkManager
    // has had the network configured for months. Trusting our own file alone
    // declared such a device unconfigured and opened the access point — taking
    // the radio away from a connection that was seconds from succeeding.
    std::string expected = saved_.getString(kSsidKey, "");
    if (expected.empty())
    {
        expected = wifi_.savedNetwork();
    }

    if (!expected.empty())
    {
        // A known network may simply need a moment: the service starts after
        // NetworkManager has *started*, which is long before it has associated.
        set(SetupState::Waiting, expected);
        logInfo("provisioning: waiting for '{}'", expected);

        const auto deadline = std::chrono::steady_clock::now() + timing_.join_grace;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (!nap(timing_.poll_interval))
            {
                return; // shutting down
            }
            const std::string current = wifi_.connectedSsid();
            if (!current.empty())
            {
                set(SetupState::Connected, current);
                logInfo("provisioning: joined '{}'", current);
                return;
            }
        }

        openAccessPoint(SetupState::AccessPoint, "network not found");
        return;
    }

    openAccessPoint(SetupState::AccessPoint, {});
}

void Provisioning::openAccessPoint(SetupState state, std::string message)
{
    if (!wifi_.startAccessPoint())
    {
        // Nothing else can be done from here, and the panel must say so rather
        // than show a network name nobody can join.
        set(SetupState::Failed, {}, "access point unavailable");
        return;
    }

    std::vector<WifiNetwork> found = wifi_.scan();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        networks_ = std::move(found);
    }

    set(state, {}, std::move(message));
}

bool Provisioning::requestScan()
{
    if (!wifi_.available() || busy_.exchange(true))
    {
        return false;
    }

    post(
        [this]
        {
            std::vector<WifiNetwork> found = wifi_.scan();
            std::lock_guard<std::mutex> lock(mutex_);
            networks_ = std::move(found);
        });
    return true;
}

bool Provisioning::requestConnect(std::string ssid, std::string psk)
{
    if (!wifi_.available() || ssid.empty() || busy_.exchange(true))
    {
        return false;
    }

    set(SetupState::Connecting, ssid);
    post([this, ssid = std::move(ssid), psk = std::move(psk)] { runConnect(ssid, psk); });
    return true;
}

void Provisioning::runConnect(const std::string &ssid, const std::string &psk)
{
    if (wifi_.connect(ssid, psk))
    {
        set(SetupState::Connected, ssid);
        saved_.setString(kSsidKey, ssid);
        saved_.save();
        logInfo("provisioning: joined '{}'", ssid);
        return;
    }

    // FR-34: back to the access point, and say what happened. Without the first
    // half the device is unreachable; without the second it looks broken.
    logWarn("provisioning: could not join '{}'", ssid);
    openAccessPoint(SetupState::Failed, "wrong password or out of range");
}

bool Provisioning::requestReset()
{
    if (!wifi_.available() || busy_.exchange(true))
    {
        return false;
    }

    post(
        [this]
        {
            wifi_.forget();
            saved_.setString(kSsidKey, "");
            saved_.save();
            logInfo("provisioning: reset to factory state");
            openAccessPoint(SetupState::AccessPoint, {});
        });
    return true;
}

} // namespace matrixos
