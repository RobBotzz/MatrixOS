// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace matrixos
{

/// Known structures, laid out to run forever on the wrapping 64x32 field. A plain
/// Gosper gun would not: its gliders come back around and destroy it. So each
/// layout is checked to repeat, with its period in `lifePatternPeriod`.
inline constexpr std::size_t kLifePatterns = 5;

std::string_view lifePatternName(std::size_t pattern);

/// The generations after which the field looks the same again, once running.
int lifePatternPeriod(std::size_t pattern);

/// The living cells at the start, as (x, y) on the panel.
std::vector<std::pair<int, int>> lifePatternCells(std::size_t pattern);

} // namespace matrixos
