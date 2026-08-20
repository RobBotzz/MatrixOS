// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <array>
#include <string_view>

namespace matrixos::settings
{

/// Shared between the settings app (writer) and the shell (reader).
constexpr std::string_view kSection = "settings";
constexpr std::string_view kBrightness = "brightness";
constexpr std::string_view kStartup = "startup";
constexpr std::string_view kTimeZone = "timezone";

/// `startup` value meaning "whatever ran last" (FR-19) rather than a fixed app
/// (FR-25).
constexpr std::string_view kStartupLast = "last";

constexpr int kMinBrightness = 10;
constexpr int kMaxBrightness = 100;
constexpr int kBrightnessStep = 5;
constexpr int kDefaultBrightness = 60;

/// One time zone as the panel shows it and as the system understands it
/// (ADR-0015).
struct TimeZone
{
    std::string_view label; ///< at most 9 characters, or it does not fit on 64 px
    std::string_view zone;  ///< a tzdata name
};

/// A short curated list rather than all of tzdata: stepping an encoder through
/// six hundred entries is not a user interface. An arbitrary zone belongs on the
/// configuration page, once somebody needs one.
constexpr std::array<TimeZone, 12> kTimeZones = {{
    {"Vienna", "Europe/Vienna"},
    {"Berlin", "Europe/Berlin"},
    {"Zurich", "Europe/Zurich"},
    {"London", "Europe/London"},
    {"Lisbon", "Europe/Lisbon"},
    {"Athens", "Europe/Athens"},
    {"UTC", "UTC"},
    {"New York", "America/New_York"},
    {"Chicago", "America/Chicago"},
    {"Denver", "America/Denver"},
    {"LA", "America/Los_Angeles"},
    {"Tokyo", "Asia/Tokyo"},
}};

/// Where the units are built and handed out (ADR-0007). A wrong default is one
/// setting away from right; no default at all would mean UTC on a device
/// standing in a living room.
constexpr std::string_view kDefaultTimeZone = "Europe/Vienna";

} // namespace matrixos::settings
