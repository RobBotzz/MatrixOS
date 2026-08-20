// Copyright (C) 2026 RobBotzz

//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace matrixos
{

/// One network as the radio saw it.
struct WifiNetwork
{
    std::string ssid;
    int signal = 0;      ///< 0 to 100, as NetworkManager reports it
    bool secured = true; ///< false means an open network, which needs no key
};

/// Driving the radio: scan, join, fall back to our own access point (ADR-0013).
///
/// The interface exists for the same reason the display and input HALs do — the
/// development machine has no radio we are allowed to touch, so there are two
/// implementations by design (NFR-17). Every method here **blocks for seconds**
/// and none of them may be called from the render thread; `Provisioning` owns a
/// worker thread and is the only caller.
class WifiControl
{
public:
    virtual ~WifiControl() = default;

    /// False when there is nothing to drive — no NetworkManager, no radio, or a
    /// host build. The device then runs without provisioning instead of showing
    /// a setup screen it could never complete.
    virtual bool available() const = 0;

    /// Strongest first, duplicates removed. An empty list means the scan failed
    /// or found nothing; the two are deliberately not distinguished, because the
    /// user's next move is the same either way.
    virtual std::vector<WifiNetwork> scan() = 0;

    /// Brings up the setup access point (C-8: this necessarily leaves client
    /// mode, because there is one radio).
    virtual bool startAccessPoint() = 0;

    /// Joins `ssid`. An empty `psk` means an open network.
    virtual bool connect(std::string_view ssid, std::string_view psk) = 0;

    /// Forgets every stored client network (FR-42). The access-point profile
    /// stays, because it is the way back.
    virtual bool forget() = 0;

    /// The network we are joined to as a client, empty if none. This is the one
    /// question that decides whether setup is needed at all.
    virtual std::string connectedSsid() = 0;

    /// The name of a stored client network, empty if none is configured.
    ///
    /// Distinct from `connectedSsid()` on purpose, and the distinction matters at
    /// boot: a configured network that has not associated *yet* must not be
    /// mistaken for an unconfigured device. Getting that wrong takes the radio
    /// away from a connection that was seconds from succeeding (C-8).
    virtual std::string savedNetwork() = 0;
};

/// What a build without a radio uses: it reports that there is nothing to do.
///
/// Not a test double — this is the second real implementation, and it is what
/// runs on the development machine so that a provisioning tool can never
/// reconfigure the developer's own network.
class NoWifi : public WifiControl
{
public:
    bool available() const override { return false; }
    std::vector<WifiNetwork> scan() override { return {}; }
    bool startAccessPoint() override { return false; }
    bool connect(std::string_view, std::string_view) override { return false; }
    bool forget() override { return false; }
    std::string connectedSsid() override { return {}; }
    std::string savedNetwork() override { return {}; }
};

} // namespace matrixos
