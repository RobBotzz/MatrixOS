// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace matrixos
{

/// A picture as the sending page produces it (ADR-0018): already 64x32, already
/// RGB, so the unit needs no image decoder.
///
/// On the wire: "MXP1", width (u8), height (u8), frame count (u16 LE), then per
/// frame a delay in milliseconds (u16 LE) followed by width*height*3 bytes RGB.
struct Picture
{
    static constexpr int kWidth = 64;
    static constexpr int kHeight = 32;
    static constexpr std::size_t kFrameBytes = kWidth * kHeight * 3;
    static constexpr std::size_t kMaxFrames = 100;
    static constexpr std::size_t kMaxBytes = 8 + kMaxFrames * (2 + kFrameBytes);

    /// Shorter delays are raised to this, so a hostile file cannot ask for
    /// frames faster than the panel draws them.
    static constexpr int kMinDelayMs = 20;

    struct Frame
    {
        int delay_ms = 0; ///< 0 for a still picture
        std::vector<std::uint8_t> rgb;
    };

    std::vector<Frame> frames;
};

/// Anything that is not exactly a valid picture is `nullopt` — the bytes come
/// from whoever has the unit's link.
std::optional<Picture> parsePicture(std::string_view bytes);

std::string serializePicture(const Picture &picture);

} // namespace matrixos
