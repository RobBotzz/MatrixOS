// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "hal/sim/terminal_display.h"

#include "gfx/surface.h"

#include <cstdio>

namespace matrixos
{
namespace
{

constexpr const char *kHideCursor = "\x1b[?25l";
constexpr const char *kShowCursor = "\x1b[?25h";
constexpr const char *kCursorHome = "\x1b[H";
constexpr const char *kResetAttributes = "\x1b[0m";
constexpr const char *kClearScreen = "\x1b[2J";

// U+2580 UPPER HALF BLOCK, spelled out as bytes so the result does not depend on
// the compiler's execution character set.
constexpr const char *kUpperHalfBlock = "\xe2\x96\x80";

/// Foreground paints the upper pixel of the cell, background the lower one.
void appendColorPair(std::string &out, Color upper, Color lower)
{
    char escape[48];
    const int written =
        std::snprintf(escape, sizeof(escape), "\x1b[38;2;%u;%u;%um\x1b[48;2;%u;%u;%um", upper.r,
                      upper.g, upper.b, lower.r, lower.g, lower.b);
    if (written > 0)
    {
        out.append(escape, static_cast<std::size_t>(written));
    }
}

} // namespace

TerminalDisplay::TerminalDisplay(int width, int height)
    : width_(width > 0 ? width : 0), height_(height > 0 ? height : 0)
{
}

TerminalDisplay::~TerminalDisplay()
{
    // Leaving a terminal without a cursor would be rude.
    showCursor();
}

void TerminalDisplay::present(const Surface &frame)
{
    if (!cursor_hidden_)
    {
        std::fputs(kHideCursor, stdout);
        cursor_hidden_ = true;
    }

    // Redrawing from the home position instead of clearing avoids flicker.
    out_.clear();
    out_ += kCursorHome;

    for (int y = 0; y < height_; y += 2)
    {
        for (int x = 0; x < width_; ++x)
        {
            // For an odd height the missing lower row reads as black.
            appendColorPair(out_, frame.pixel(x, y), frame.pixel(x, y + 1));
            out_ += kUpperHalfBlock;
        }
        out_ += kResetAttributes;
        out_ += '\n';
    }

    std::fwrite(out_.data(), 1, out_.size(), stdout);
    std::fflush(stdout);
}

void TerminalDisplay::clear()
{
    std::fputs(kResetAttributes, stdout);
    std::fputs(kClearScreen, stdout);
    std::fputs(kCursorHome, stdout);
    showCursor();
}

void TerminalDisplay::showCursor()
{
    if (cursor_hidden_)
    {
        std::fputs(kShowCursor, stdout);
        cursor_hidden_ = false;
    }
    std::fflush(stdout);
}

} // namespace matrixos
