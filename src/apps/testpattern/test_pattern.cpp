// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/testpattern/test_pattern.h"

#include "gfx/surface.h"

#include <cstdint>

namespace matrixos
{

void TestPatternApp::render(Surface &surface)
{
    if (surface.width() < 8 || surface.height() < 8)
    {
        return;
    }

    const int last_x = surface.width() - 1;
    const int last_y = surface.height() - 1;

    // One-pixel border: wrong geometry shows up as a broken or doubled frame.
    for (int x = 0; x <= last_x; ++x)
    {
        surface.setPixel(x, 0, Color::white());
        surface.setPixel(x, last_y, Color::white());
    }
    for (int y = 0; y <= last_y; ++y)
    {
        surface.setPixel(0, y, Color::white());
        surface.setPixel(last_x, y, Color::white());
    }

    // Three gradients: a swapped channel order shows up as the wrong colour, and
    // the gradient itself shows whether the dark steps survive.
    const int band_height = (surface.height() - 4) / 3;
    const int span = surface.width() - 5;
    for (int band = 0; band < 3; ++band)
    {
        for (int row = 0; row < band_height; ++row)
        {
            for (int x = 2; x < last_x - 1; ++x)
            {
                const auto level = static_cast<std::uint8_t>(255 * (x - 2) / span);
                const Color color = (band == 0)   ? Color{level, 0, 0}
                                    : (band == 1) ? Color{0, level, 0}
                                                  : Color{0, 0, level};
                surface.setPixel(x, 2 + band * band_height + row, color);
            }
        }
    }

    // Marker in the top-left corner: distinguishes a mirrored or rotated panel.
    for (int x = 1; x <= 4; ++x)
    {
        surface.setPixel(x, 1, Color{255, 255, 0});
    }
}

} // namespace matrixos
