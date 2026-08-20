// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/clock.h"

#include "os/log.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>

namespace matrixos
{

bool SystemTimeProvider::synchronized() const
{
    const auto now = std::chrono::steady_clock::now();
    if (checked_ != std::chrono::steady_clock::time_point{} && now - checked_ < kCheckInterval)
    {
        return synchronized_;
    }
    checked_ = now;

    std::error_code ignored;
    if (std::filesystem::exists(kSyncDirectory, ignored))
    {
        const bool synced = std::filesystem::exists(kSyncFlag, ignored);
        if (synced && !synchronized_)
        {
            logInfo("clock: time synchronised");
        }
        synchronized_ = synced;
        return synchronized_;
    }

    // No timesyncd — a development machine. Believe a clock that is at least
    // plausible, so the simulator is usable anywhere. On the device this branch
    // is never taken, which is deliberate: here the wrong answer costs a
    // developer a puzzled minute, there it costs a clock that lies (ADR-0015).
    const std::time_t stamp = std::time(nullptr);
    std::tm broken{};
    ::localtime_r(&stamp, &broken);
    synchronized_ = broken.tm_year + 1900 >= kPlausibleYear;
    return synchronized_;
}

LocalTime SystemTimeProvider::now() const
{
    const std::time_t stamp = std::time(nullptr);
    std::tm broken{};
    ::localtime_r(&stamp, &broken);

    LocalTime local;
    local.year = broken.tm_year + 1900;
    local.month = broken.tm_mon + 1;
    local.day = broken.tm_mday;
    local.hour = broken.tm_hour;
    local.minute = broken.tm_min;
    local.second = broken.tm_sec;
    local.weekday = broken.tm_wday;
    return local;
}

void SystemTimeProvider::setTimeZone(std::string_view zone)
{
    if (zone.empty() || zone == zone_)
    {
        return;
    }

    zone_ = zone;

    // Process-global, and safe only because time is formatted on the render
    // thread and nowhere else (ADR-0015). The HTTP thread never calls localtime.
    ::setenv("TZ", zone_.c_str(), 1);
    ::tzset();

    logInfo("clock: time zone {}", zone_);
}

} // namespace matrixos
