// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "net/wifi.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace matrixos
{

class StateStore;
class StateSection;

/// What the device is doing about its network, and therefore what the panel
/// shows (FR-35).
enum class SetupState
{
    Unmanaged,   ///< no radio to drive: a host build, or nmcli missing
    Waiting,     ///< giving a stored network its chance to come up at boot
    AccessPoint, ///< our own network is open and waiting for the user
    Connecting,  ///< joining
    Connected,   ///< joined
    Failed,      ///< the last attempt failed; the access point is back up
};

/// A copy of the state, taken under the lock. The app renders this and never
/// touches the provisioning object's internals.
struct SetupStatus
{
    SetupState state = SetupState::Unmanaged;
    std::string ssid;    ///< the network being joined, or the one joined
    std::string message; ///< why the last attempt failed, if it did
};

/// The setup state machine (ADR-0013), and the only thing that talks to the
/// radio.
///
/// Two threads look at this object and neither of them may block: the render
/// thread reads `status()` once a frame, and the HTTP thread posts requests. The
/// work itself — scanning, joining, bringing up the access point, all of which
/// take seconds — runs on a worker thread owned here.
class Provisioning
{
public:
    /// How long a stored network gets to come up on its own before the device
    /// decides it is out of range and opens its access point. NetworkManager
    /// needs about ten seconds for a normal association.
    struct Timing
    {
        std::chrono::milliseconds join_grace{30000};
        std::chrono::milliseconds poll_interval{2000};
    };

    /// Two overloads for the same reason `GestureRecognizer` has two: GCC cannot
    /// form `Timing{}` as a default argument before the enclosing class is
    /// complete.
    Provisioning(WifiControl &wifi, StateStore &store);
    Provisioning(WifiControl &wifi, StateStore &store, Timing timing);
    ~Provisioning();

    Provisioning(const Provisioning &) = delete;
    Provisioning &operator=(const Provisioning &) = delete;

    /// Works out where we stand and starts the boot job. Returns immediately.
    void begin();

    SetupStatus status() const;
    std::vector<WifiNetwork> networks() const;

    /// True while the user has something to do. The shell shows the setup app
    /// exactly while this holds (FR-35).
    bool needsSetup() const;

    /// All three are posted to the worker and return at once, because the
    /// browser is waiting on the other end of the request. False means a job was
    /// already running.
    bool requestScan();
    bool requestConnect(std::string ssid, std::string psk);
    bool requestReset(); ///< FR-42

    /// Waits for the worker to finish. Shutdown uses it; so do the tests, which
    /// is what keeps them free of sleeps and of flakiness.
    void waitForIdle();

private:
    void post(std::function<void()> job);
    void set(SetupState state, std::string ssid, std::string message = {});

    /// Interruptible sleep — returns false if the object is shutting down.
    bool nap(std::chrono::milliseconds duration);

    void runBoot();
    void runConnect(const std::string &ssid, const std::string &psk);
    void openAccessPoint(SetupState state, std::string message);

    WifiControl &wifi_;
    StateSection &saved_;
    Timing timing_;

    mutable std::mutex mutex_;
    SetupStatus status_;
    std::vector<WifiNetwork> networks_;

    std::condition_variable wake_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> busy_{false};
    std::thread worker_;
};

/// The state as a lowercase word, for the panel and for the JSON status.
std::string_view toString(SetupState state);

} // namespace matrixos
