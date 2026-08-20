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
/// Each glyph is drawn from the left edge of its own cell: the source font
/// indents its narrow glyphs (`T`, `Y`, `I`, `1`, `0` light columns 1 to 3, not
/// 0 to 3), which inside a six-pixel advance made them lean right against their
/// neighbours. The advance is unchanged, so a line is still monospaced. `scale` draws each font
/// pixel as a square block, so scale 2 gives 10x14 glyphs — five of those fit across the panel.
///
/// Needs no clipping from the caller: pixels outside the surface are dropped.
/// Characters outside printable ASCII are drawn as '?' rather than skipped.
int drawText(Surface &surface, int x, int y, std::string_view text, Color color, int scale = 1);

/// Pixel width `text` would occupy, without a trailing gap. Empty text is zero.
///
/// This is the advance box, not the ink: almost every glyph leaves its last
/// column empty, so the value is up to one column per scale step wider than what
/// is actually lit. Use `centredTextX` to place text, not this.
int textWidth(std::string_view text, int scale = 1);

/// The x at which `text` sits horizontally centred within `width` pixels.
///
/// Centres on the pixels the glyphs actually set. Centring on `textWidth`
/// instead leaves the text a pixel left of true centre — two pixels more space
/// on the right than on the left, which is plainly visible on a 64-pixel panel.
/// Where the ink cannot be split evenly the spare pixel goes on the left.
int centredTextX(int width, std::string_view text, int scale = 1);

/// `drawText` at that x, which is what apps almost always want.
int drawTextCentered(Surface &surface, int y, std::string_view text, Color color, int scale = 1);

/// The x that puts the last lit pixel of `text` on column `right_edge`. Aligning
/// on `textWidth` instead leaves a stray column of margin, for the same reason.
int rightAlignedTextX(int right_edge, std::string_view text, int scale = 1);

} // namespace matrixos
