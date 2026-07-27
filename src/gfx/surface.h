// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "gfx/color.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace matrixos
{

/// A plain RGB24 pixel buffer that owns its memory and knows nothing about
/// hardware. Apps draw into a Surface; a Display decides what happens to it.
///
/// Because this is ordinary memory, a rendered frame can be compared
/// byte-for-byte in a test (NFR-12) — see ADR-0002.
class Surface
{
public:
    static constexpr int kChannels = 3;

    /// Negative dimensions are clamped to zero, which yields an empty surface.
    Surface(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }

    void clear(Color color = Color::black());

    /// Coordinates outside the surface are ignored, so callers need no clipping.
    void setPixel(int x, int y, Color color);

    /// Reads outside the surface return black.
    Color pixel(int x, int y) const;

    /// RGB24, row-major, three bytes per pixel.
    std::span<const std::uint8_t> bytes() const { return pixels_; }

private:
    bool contains(int x, int y) const;
    std::size_t offset(int x, int y) const;

    int width_;
    int height_;
    std::vector<std::uint8_t> pixels_;
};

} // namespace matrixos
