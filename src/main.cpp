// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.
//
// Composition root: the only place that knows which display backends exist.

#include "gfx/surface.h"
#include "hal/display.h"
#include "hal/sim/terminal_display.h"

#ifdef MATRIXOS_HAS_MATRIX
#include "hal/matrix/matrix_display.h"
#endif

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>

namespace
{

// One 64x32 panel (C-7).
constexpr int kPanelWidth = 64;
constexpr int kPanelHeight = 32;

volatile std::sig_atomic_t g_interrupted = 0;

void onSignal(int)
{
    g_interrupted = 1;
}

bool hasFlag(int argc, char *argv[], const char *flag)
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], flag) == 0)
        {
            return true;
        }
    }
    return false;
}

/// A frame that makes geometry, orientation and channel order obvious at a glance.
void drawTestPattern(matrixos::Surface &surface)
{
    using matrixos::Color;

    surface.clear();
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

    // Three gradients: a swapped channel order shows up as the wrong colour.
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

} // namespace

int main(int argc, char *argv[])
{
    [[maybe_unused]] const bool force_simulator = hasFlag(argc, argv, "--simulate");

    std::unique_ptr<matrixos::Display> display;

#ifdef MATRIXOS_HAS_MATRIX
    if (!force_simulator)
    {
        // Also consumes the library's own --led-* flags from argc/argv.
        display = matrixos::MatrixDisplay::createFromFlags(&argc, &argv);
    }
#endif

    if (!display)
    {
        display = std::make_unique<matrixos::TerminalDisplay>(kPanelWidth, kPanelHeight);
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    matrixos::Surface frame(display->width(), display->height());
    drawTestPattern(frame);
    display->present(frame);

    // There is no shell loop yet: hold the pattern until asked to stop.
    while (g_interrupted == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    display->clear(); // leave the panel dark (FR-4)
    return 0;
}
