// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "net/nmcli_wifi.h"

#include "os/log.h"

#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>

extern char **environ;

namespace matrixos
{
namespace
{

constexpr int kProbeTimeoutMs = 3000;
constexpr int kScanTimeoutMs = 25000;
constexpr int kConnectTimeoutMs = 60000;
constexpr int kProfileTimeoutMs = 40000;

/// How often to rescan while waiting for a network to appear after the access
/// point has been taken down. Each scan already blocks for seconds, so this is a
/// patience setting, not a polling interval.
constexpr int kScanAttempts = 3;

constexpr const char *kWifiType = "802-11-wireless";

/// Runs a command with an explicit argument vector — no shell, so nothing in an
/// SSID or a password can be interpreted (ADR-0013).
///
/// Returns the exit status, or -1 if the child could not be started or had to be
/// killed on the timeout. The child is always reaped: a device that runs for
/// months must not accumulate a zombie per scan.
int runCommand(const std::vector<std::string> &args, std::string *output, int timeout_ms)
{
    int fds[2] = {-1, -1};
    if (::pipe(fds) != 0)
    {
        return -1;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addclose(&actions, fds[1]);

    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (const std::string &arg : args)
    {
        argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int spawned = ::posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(fds[1]);

    if (spawned != 0)
    {
        ::close(fds[0]);
        logWarn("nmcli: cannot run '{}': {}", args[0], std::strerror(spawned));
        return -1;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    bool timed_out = false;

    for (;;)
    {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();
        if (remaining <= 0)
        {
            timed_out = true;
            break;
        }

        pollfd fd{fds[0], POLLIN, 0};
        const int ready = ::poll(&fd, 1, static_cast<int>(remaining));
        if (ready == 0)
        {
            timed_out = true;
            break;
        }
        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }

        char chunk[4096];
        const ssize_t count = ::read(fds[0], chunk, sizeof(chunk));
        if (count <= 0)
        {
            break; // the child closed stdout, which it does when it exits
        }
        if (output != nullptr)
        {
            output->append(chunk, static_cast<std::size_t>(count));
        }
    }

    ::close(fds[0]);

    if (timed_out)
    {
        logWarn("nmcli: '{}' timed out after {} ms", args[1], timeout_ms);
        ::kill(pid, SIGKILL);
    }

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR)
    {
    }

    if (timed_out)
    {
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::vector<std::string> splitLines(std::string_view text)
{
    std::vector<std::string> lines;
    while (!text.empty())
    {
        const auto end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        if (!line.empty())
        {
            lines.emplace_back(line);
        }
        if (end == std::string_view::npos)
        {
            break;
        }
        text.remove_prefix(end + 1);
    }
    return lines;
}

} // namespace

std::vector<std::string> splitTerseLine(std::string_view line)
{
    std::vector<std::string> fields;
    std::string current;

    for (std::size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] == '\\' && i + 1 < line.size())
        {
            current += line[++i]; // an escaped colon belongs to the value
        }
        else if (line[i] == ':')
        {
            fields.push_back(std::move(current));
            current.clear();
        }
        else
        {
            current += line[i];
        }
    }

    fields.push_back(std::move(current));
    return fields;
}

bool NmcliWifi::available() const
{
    if (available_ < 0)
    {
        available_ = runCommand({"nmcli", "--version"}, nullptr, kProbeTimeoutMs) == 0 ? 1 : 0;
        if (available_ == 0)
        {
            logWarn("wifi: nmcli is unavailable, provisioning is disabled");
        }
    }
    return available_ == 1;
}

std::vector<WifiNetwork> NmcliWifi::scan()
{
    std::string output;
    if (runCommand({"nmcli", "-t", "-f", "SSID,SIGNAL,SECURITY", "device", "wifi", "list",
                    "--rescan", "yes"},
                   &output, kScanTimeoutMs) != 0)
    {
        return {};
    }

    // Strongest wins: the same network is reported once per band and per access
    // point, and the user is picking a name, not a radio.
    std::map<std::string, WifiNetwork> strongest;

    for (const std::string &line : splitLines(output))
    {
        const std::vector<std::string> fields = splitTerseLine(line);
        if (fields.size() < 3 || fields[0].empty())
        {
            continue; // hidden network, or a line we do not understand
        }

        WifiNetwork network;
        network.ssid = fields[0];
        network.signal = std::atoi(fields[1].c_str());
        network.secured = !fields[2].empty();

        const auto existing = strongest.find(network.ssid);
        if (existing == strongest.end() || existing->second.signal < network.signal)
        {
            strongest[network.ssid] = network;
        }
    }

    std::vector<WifiNetwork> networks;
    networks.reserve(strongest.size());
    for (auto &[ssid, network] : strongest)
    {
        networks.push_back(network);
    }

    std::sort(networks.begin(), networks.end(),
              [](const WifiNetwork &a, const WifiNetwork &b) { return a.signal > b.signal; });

    logInfo("wifi: {} networks in range", networks.size());
    return networks;
}

bool NmcliWifi::startAccessPoint()
{
    const int status =
        runCommand({"nmcli", "connection", "up", kAccessPointProfile}, nullptr, kProfileTimeoutMs);

    if (status != 0)
    {
        logError("wifi: cannot start the setup access point (profile '{}' missing?)",
                 kAccessPointProfile);
        return false;
    }

    logInfo("wifi: setup access point is up");
    return true;
}

bool NmcliWifi::awaitNetwork(std::string_view ssid)
{
    for (int attempt = 0; attempt < kScanAttempts; ++attempt)
    {
        for (const WifiNetwork &network : scan())
        {
            if (network.ssid == ssid)
            {
                return true;
            }
        }
        logInfo("wifi: '{}' not in range yet, scanning again", ssid);
    }

    return false;
}

bool NmcliWifi::profileExists(std::string_view name)
{
    std::string output;
    if (runCommand({"nmcli", "-t", "-f", "NAME", "connection", "show"}, &output,
                   kProfileTimeoutMs) != 0)
    {
        return false;
    }

    for (const std::string &line : splitLines(output))
    {
        const std::vector<std::string> fields = splitTerseLine(line);
        if (!fields.empty() && fields[0] == name)
        {
            return true;
        }
    }

    return false;
}

bool NmcliWifi::connect(std::string_view ssid, std::string_view psk)
{
    // Asked before anything changes, because it decides whether the cleanup at
    // the bottom may delete a profile: one somebody else made is not ours.
    const bool profile_existed = profileExists(ssid);

    // One radio, so the access point has to go before the client can associate
    // (C-8). Failure here is expected when it was not up in the first place.
    runCommand({"nmcli", "connection", "down", kAccessPointProfile}, nullptr, kProfileTimeoutMs);

    // Leaving access-point mode leaves the scan cache empty — nothing is scanned
    // while the radio *is* an access point. `nmcli device wifi connect` takes the
    // security type from that cache, so connecting straight away builds a profile
    // that has a password and no key-mgmt, and fails with
    //
    //     Error: 802-11-wireless-security.key-mgmt: property is missing.
    //
    // On the panel that is indistinguishable from a wrong password. Seeing the
    // network once before asking to join it is the whole fix.
    if (!awaitNetwork(ssid))
    {
        logWarn("wifi: '{}' is not in range", ssid);
        return false;
    }

    std::vector<std::string> args = {"nmcli",           "device", "wifi",    "connect",
                                     std::string(ssid), "ifname", kInterface};
    if (!psk.empty())
    {
        args.insert(args.begin() + 5, {"password", std::string(psk)});
    }

    if (runCommand(args, nullptr, kConnectTimeoutMs) == 0)
    {
        logInfo("wifi: joined '{}'", ssid);
        return true;
    }

    // A failed attempt can leave a profile behind that NetworkManager keeps
    // retrying, which would fight the access point we are about to bring back —
    // but only one we could have created ourselves.
    if (!profile_existed)
    {
        runCommand({"nmcli", "connection", "delete", std::string(ssid)}, nullptr,
                   kProfileTimeoutMs);
    }

    logWarn("wifi: could not join '{}'", ssid);
    return false;
}

bool NmcliWifi::forget()
{
    std::string output;
    if (runCommand({"nmcli", "-t", "-f", "NAME,TYPE", "connection", "show"}, &output,
                   kProfileTimeoutMs) != 0)
    {
        return false;
    }

    bool all_removed = true;

    for (const std::string &line : splitLines(output))
    {
        const std::vector<std::string> fields = splitTerseLine(line);
        if (fields.size() < 2 || fields[1] != kWifiType || fields[0] == kAccessPointProfile)
        {
            continue; // the setup profile stays: it is the way back
        }

        if (runCommand({"nmcli", "connection", "delete", fields[0]}, nullptr, kProfileTimeoutMs) !=
            0)
        {
            logWarn("wifi: could not delete profile '{}'", fields[0]);
            all_removed = false;
        }
    }

    return all_removed;
}

std::string NmcliWifi::connectedSsid()
{
    return firstClientProfile(true);
}

std::string NmcliWifi::savedNetwork()
{
    return firstClientProfile(false);
}

std::string NmcliWifi::firstClientProfile(bool activeOnly)
{
    std::vector<std::string> args = {"nmcli", "-t", "-f", "NAME,TYPE", "connection", "show"};
    if (activeOnly)
    {
        args.emplace_back("--active");
    }

    std::string output;
    if (runCommand(args, &output, kProfileTimeoutMs) != 0)
    {
        return {};
    }

    for (const std::string &line : splitLines(output))
    {
        const std::vector<std::string> fields = splitTerseLine(line);
        if (fields.size() >= 2 && fields[1] == kWifiType && fields[0] != kAccessPointProfile)
        {
            return fields[0];
        }
    }

    return {};
}

} // namespace matrixos
