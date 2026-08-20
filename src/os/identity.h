// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <string>
#include <string_view>

namespace matrixos
{

/// Who this unit is (FR-32).
///
/// Every device is flashed from the same image, so identity cannot be baked in.
/// It is derived from the CPU serial, which is unique per board and needs no
/// state, no provisioning step and no first-boot file to survive a reset.
struct Identity
{
    std::string suffix;       ///< four lowercase hex characters, e.g. "a3f1"
    std::string hostname;     ///< matrixos-a3f1
    std::string access_point; ///< MatrixOS-a3f1
};

Identity deviceIdentity();

/// The suffix `provision.sh` and MatrixOS must agree on: the last four
/// characters of the `Serial` line in /proc/cpuinfo, lowercased. Empty when
/// there is no such line, which is every machine that is not a Pi.
///
/// Split out from the file reading so it can be tested against real cpuinfo text
/// rather than against whatever the build machine happens to be.
std::string serialSuffix(std::string_view cpuinfo);

} // namespace matrixos
