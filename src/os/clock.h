// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace matrixos
{

/// Wall-clock time, already broken down in the configured zone.
struct LocalTime
{
    int year = 1970;
    int month = 1; ///< 1 to 12
    int day = 1;   ///< 1 to 31
    int hour = 0;  ///< 0 to 23
    int minute = 0;
    int second = 0;
    int weekday = 4; ///< 0 is Sunday, as in `struct tm`
};

/// Where an app gets the time, since it may not read a clock itself (FR-26,
/// ADR-0015).
///
/// `synchronized()` is the important half. The Pi has no real-time clock (C-9),
/// so between power-on and the first NTP answer the system clock holds a stale
/// value from `fake-hwclock` — a time that looks entirely plausible and is
/// wrong. An app must show that it does not know rather than display it.
class TimeProvider
{
public:
    virtual ~TimeProvider() = default;

    virtual bool synchronized() const = 0;

    /// Meaningless while `synchronized()` is false.
    virtual LocalTime now() const = 0;

    /// A tzdata name such as `Europe/Vienna`. An unknown name means UTC, which
    /// is what glibc does and what the curated list in settings.h avoids.
    virtual void setTimeZone(std::string_view zone) = 0;
};

/// The system clock, with `systemd-timesyncd` as the authority on whether it can
/// be believed.
class SystemTimeProvider : public TimeProvider
{
public:
    /// The file `systemd-timesyncd` creates once it has accepted a server's
    /// time. It lives on a tmpfs, so it is gone again after a reboot — which is
    /// exactly right.
    static constexpr const char *kSyncFlag = "/run/systemd/timesync/synchronized";

    /// The directory whose absence means this machine is not running timesyncd,
    /// and the plausibility fallback applies instead.
    static constexpr const char *kSyncDirectory = "/run/systemd/timesync";

    /// A system clock before this year cannot have been set by NTP. Only used
    /// where timesyncd is absent, which on the device it never is.
    static constexpr int kPlausibleYear = 2025;

    bool synchronized() const override;
    LocalTime now() const override;
    void setTimeZone(std::string_view zone) override;

private:
    /// Checking a file sixty times a second to learn a fact that changes twice
    /// in a device's lifetime would be waste of the sort C-3 warns about.
    static constexpr std::chrono::seconds kCheckInterval{2};

    mutable std::chrono::steady_clock::time_point checked_{};
    mutable bool synchronized_ = false;
    std::string zone_;
};

} // namespace matrixos
