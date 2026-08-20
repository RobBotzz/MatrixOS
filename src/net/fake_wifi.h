// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "net/wifi.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>

namespace matrixos
{

/// A radio made of variables.
///
/// It has two users, which is what earns it a place in `net/` rather than in the
/// tests: the suite drives the whole provisioning flow with it, and `--fake-wifi`
/// puts the same flow into the terminal simulator, so the setup screens and the
/// portal can be built and looked at without a device.
class FakeWifi : public WifiControl
{
public:
    /// What a scan finds. Set before use.
    std::vector<WifiNetwork> networks = {
        {"Kitchen", 82, true}, {"Nachbar 2.4G", 54, true}, {"Cafe Gast", 31, false}};

    /// The only password that works. Empty means every attempt succeeds, which
    /// is the default because the interesting failure is exercised explicitly.
    std::string password;

    bool radio = true;

    /// False models the provisioning mistake that will happen: nmcli answers,
    /// but the `matrixos-setup` profile is missing, so the access point cannot
    /// come up.
    bool access_point_profile = true;

    bool available() const override { return radio; }

    std::vector<WifiNetwork> scan() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++scans;
        return networks;
    }

    bool startAccessPoint() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++access_points;
        if (!radio || !access_point_profile)
        {
            return false;
        }
        connected_.clear();
        return true;
    }

    /// Makes the next join wait until `release()` — the only way to look at the
    /// Connecting state without racing the worker thread through it.
    void holdConnect()
    {
        std::lock_guard<std::mutex> lock(gate_);
        held_ = true;
    }

    void release()
    {
        {
            std::lock_guard<std::mutex> lock(gate_);
            held_ = false;
        }
        opened_.notify_all();
    }

    bool connect(std::string_view ssid, std::string_view psk) override
    {
        {
            std::unique_lock<std::mutex> lock(gate_);
            opened_.wait(lock, [this] { return !held_; });
        }

        std::lock_guard<std::mutex> lock(mutex_);
        ++attempts;

        const bool known = std::any_of(networks.begin(), networks.end(),
                                       [&](const WifiNetwork &n) { return n.ssid == ssid; });
        if (!known || (!password.empty() && psk != password))
        {
            return false;
        }

        saved_ = std::string(ssid);
        connected_ = std::string(ssid);
        return true;
    }

    bool forget() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++forgets;
        connected_.clear();
        saved_.clear();
        return true;
    }

    std::string connectedSsid() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return connected_;
    }

    std::string savedNetwork() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return saved_;
    }

    /// Models a device that NetworkManager has a profile for but has not
    /// associated with yet — the state every real device is in for the first
    /// seconds after a boot.
    void pretendConfigured(std::string ssid) { saved_ = std::move(ssid); }

    /// Pretends the device came up already joined, the way NetworkManager does
    /// with a stored profile.
    void pretendJoined(std::string ssid)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        saved_ = ssid;
        connected_ = std::move(ssid);
    }

    int scans = 0;
    int access_points = 0;
    int attempts = 0;
    int forgets = 0;

private:
    mutable std::mutex mutex_;
    std::string connected_;
    std::string saved_;

    std::mutex gate_;
    std::condition_variable opened_;
    bool held_ = false;
};

} // namespace matrixos
