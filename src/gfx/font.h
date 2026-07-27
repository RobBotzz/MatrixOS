// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "gfx/color.h"

#include <string_view>

namespace matrixos
{

class Surface;

/// One fixed 5x7 bitmap font, embedded in the binary.
///
/// Not a font engine and not a text layout system: on a 64x32 panel there is room
/// for roughly ten characters, so one legible size is all there is to choose from.
/// The glyphs come from the public-domain 5x7 BDF font shipped with
/// rpi-rgb-led-matrix, converted by tools/bdf_to_header.py.
constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kGlyphAdvance = 6; // glyph plus one pixel of spacing

/// Draws `text` with its top-left corner at (x, y) and returns the x coordinate
/// just past the last glyph.
///
/// Needs no clipping from the caller: pixels outside the surface are dropped, so
/// text may safely run off any edge. Characters outside printable ASCII are drawn
/// as '?' rather than silently skipped, so a mistake is visible.
int drawText(Surface &surface, int x, int y, std::string_view text, Color color);

/// Pixel width `text` would occupy, without a trailing gap. Empty text is zero.
int textWidth(std::string_view text);

} // namespace matrixos
