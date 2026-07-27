// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "hal/display.h"

#include <string>

namespace matrixos
{

/// Renders frames into a terminal using ANSI true-colour half-block characters:
/// one character cell carries two vertically stacked pixels, so a 64x32 frame
/// occupies 64 columns and 16 rows.
///
/// This is a development tool. Colour fidelity, brightness and perceived flicker
/// cannot be judged here — those stay hardware-only checks (ADR-0002).
class TerminalDisplay : public Display
{
public:
    TerminalDisplay(int width, int height);
    ~TerminalDisplay() override;

    int width() const override { return width_; }
    int height() const override { return height_; }

    void present(const Surface &frame) override;
    void clear() override;

private:
    void showCursor();

    int width_;
    int height_;
    std::string out_; // reused across frames to avoid reallocating every present
    bool cursor_hidden_ = false;
};

} // namespace matrixos
