// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <string_view>

namespace matrixos::settings
{

/// Shared between the settings app (writer) and the shell (reader).
constexpr std::string_view kSection = "settings";
constexpr std::string_view kBrightness = "brightness";
constexpr std::string_view kStartup = "startup";

/// `startup` value meaning "whatever ran last" (FR-19) rather than a fixed app
/// (FR-25).
constexpr std::string_view kStartupLast = "last";

constexpr int kMinBrightness = 10;
constexpr int kMaxBrightness = 100;
constexpr int kBrightnessStep = 5;
constexpr int kDefaultBrightness = 60;

} // namespace matrixos::settings
