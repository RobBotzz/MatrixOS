// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "gfx/surface.h"

#include <algorithm>

namespace matrixos
{

Surface::Surface(int width, int height)
    : width_(width > 0 ? width : 0), height_(height > 0 ? height : 0)
{
    pixels_.assign(static_cast<std::size_t>(width_) * height_ * kChannels, 0);
}

void Surface::clear(Color color)
{
    if (color == Color::black())
    {
        std::fill(pixels_.begin(), pixels_.end(), 0);
        return;
    }

    for (std::size_t i = 0; i < pixels_.size(); i += kChannels)
    {
        pixels_[i + 0] = color.r;
        pixels_[i + 1] = color.g;
        pixels_[i + 2] = color.b;
    }
}

void Surface::setPixel(int x, int y, Color color)
{
    if (!contains(x, y))
    {
        return;
    }

    const std::size_t i = offset(x, y);
    pixels_[i + 0] = color.r;
    pixels_[i + 1] = color.g;
    pixels_[i + 2] = color.b;
}

Color Surface::pixel(int x, int y) const
{
    if (!contains(x, y))
    {
        return Color::black();
    }

    const std::size_t i = offset(x, y);
    return Color{pixels_[i + 0], pixels_[i + 1], pixels_[i + 2]};
}

bool Surface::contains(int x, int y) const
{
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}

std::size_t Surface::offset(int x, int y) const
{
    return (static_cast<std::size_t>(y) * width_ + x) * kChannels;
}

} // namespace matrixos
