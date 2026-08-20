// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "net/wifi.h"

#include <string>

namespace matrixos
{

/// WiFi through NetworkManager, driven by `nmcli` as a child process
/// (ADR-0013).
///
/// Every call spawns `nmcli` with an explicit argument vector — never a shell —
/// so an SSID containing a space, a quote or a semicolon is a string and not a
/// command. Terse output (`-t -f`) is the parsing contract; a line that does not
/// parse is skipped rather than treated as an error, which is the same rule the
/// state store applies to a malformed value.
class NmcliWifi : public WifiControl
{
public:
    /// The access-point profile `provision.sh` creates. Bringing it up is how
    /// the device gets back to a state the user can reach.
    static constexpr const char *kAccessPointProfile = "matrixos-setup";
    static constexpr const char *kInterface = "wlan0";

    bool available() const override;
    std::vector<WifiNetwork> scan() override;
    bool startAccessPoint() override;
    bool connect(std::string_view ssid, std::string_view psk) override;
    bool forget() override;
    std::string connectedSsid() override;
    std::string savedNetwork() override;

private:
    /// Both questions are the same `nmcli connection show`, with and without
    /// `--active`. The setup access point is never the answer to either.
    std::string firstClientProfile(bool activeOnly);

    /// Rescans until `ssid` is visible, or gives up. Joining a network that
    /// nmcli has not seen produces a profile without a key-management setting,
    /// which fails in a way that reads exactly like a wrong password.
    bool awaitNetwork(std::string_view ssid);

    bool profileExists(std::string_view name);

    mutable int available_ = -1; ///< -1 unknown, 0 no, 1 yes — probed once
};

/// Splits one line of `nmcli -t` output, honouring its backslash escaping. An
/// SSID may legitimately contain a colon, which is why this is not a plain
/// split. Exposed for the tests.
std::vector<std::string> splitTerseLine(std::string_view line);

} // namespace matrixos
