// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace matrixos
{

/// Album art for the panel (ADR-0019): a JPEG, centre-cropped to a square and
/// averaged down to `size` x `size` RGB. Anything that does not decode is
/// `nullopt`.
std::optional<std::vector<std::uint8_t>> decodeCover(std::string_view jpeg, int size);

} // namespace matrixos
