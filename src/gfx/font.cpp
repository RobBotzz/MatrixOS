// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "gfx/font.h"

#include "gfx/font5x7_data.h"
#include "gfx/surface.h"

namespace matrixos
{
namespace
{

const std::uint8_t *glyphFor(char character)
{
    if (character < font5x7::kFirstChar || character > font5x7::kLastChar)
    {
        character = '?';
    }
    return font5x7::kGlyphs[character - font5x7::kFirstChar];
}

struct InkBounds
{
    int first = 0;
    int last = -1; ///< `first > last` means nothing is lit
};

/// Every column the glyph lights, in one byte. Bit 0 is the leftmost pixel, as
/// in `drawText`.
std::uint8_t glyphMask(char character)
{
    const std::uint8_t *glyph = glyphFor(character);
    std::uint8_t mask = 0;
    for (int row = 0; row < kGlyphHeight; ++row)
    {
        mask |= glyph[row];
    }
    return mask;
}

/// Leftmost and rightmost lit column, or -1 for a blank glyph.
int firstColumn(std::uint8_t mask)
{
    for (int column = 0; column < kGlyphWidth; ++column)
    {
        if ((mask & (1U << column)) != 0)
        {
            return column;
        }
    }
    return -1;
}

int lastColumn(std::uint8_t mask)
{
    for (int column = kGlyphWidth - 1; column >= 0; --column)
    {
        if ((mask & (1U << column)) != 0)
        {
            return column;
        }
    }
    return -1;
}

/// What `drawText` trims off the left of a glyph. The source font indents its
/// narrow glyphs — T, Y, I, 1 and 0 light columns 1 to 3 rather than 0 to 3 —
/// which inside a six-pixel advance leaves one pixel of space before them and
/// two after, so they read as leaning right against their neighbours.
int bearing(char character)
{
    const int column = firstColumn(glyphMask(character));
    return column > 0 ? column : 0;
}

/// Ink span of `text` in unscaled pixels, in the laid-out line rather than in
/// one glyph. Blank glyphs at either end are skipped, so a stray space does not
/// drag a line off centre.
InkBounds inkBounds(std::string_view text)
{
    InkBounds bounds;

    int cursor = 0;
    bool started = false;

    for (const char character : text)
    {
        const std::uint8_t mask = glyphMask(character);
        if (mask != 0)
        {
            if (!started)
            {
                bounds.first = cursor;
                started = true;
            }
            // Trimmed, so the ink begins at the cell edge.
            bounds.last = cursor + lastColumn(mask) - firstColumn(mask);
        }
        cursor += kGlyphAdvance;
    }

    return bounds;
}

} // namespace

int drawText(Surface &surface, int x, int y, std::string_view text, Color color, int scale)
{
    if (scale < 1)
    {
        scale = 1;
    }

    int cursor = x;

    for (const char character : text)
    {
        const std::uint8_t *glyph = glyphFor(character);

        // Each glyph is drawn from the left edge of its cell, whatever indent the
        // source font gave it. The advance stays fixed, so this is still a
        // monospaced line — the narrow glyphs simply stop leaning right.
        const int trim = bearing(character);

        for (int row = 0; row < kGlyphHeight; ++row)
        {
            for (int column = 0; column < kGlyphWidth; ++column)
            {
                // Bit 0 is the leftmost pixel; see font5x7_data.h.
                if ((glyph[row] & (1U << column)) == 0)
                {
                    continue;
                }

                for (int dy = 0; dy < scale; ++dy)
                {
                    for (int dx = 0; dx < scale; ++dx)
                    {
                        surface.setPixel(cursor + (column - trim) * scale + dx,
                                         y + row * scale + dy, color);
                    }
                }
            }
        }

        cursor += kGlyphAdvance * scale;
    }

    return cursor;
}

int textWidth(std::string_view text, int scale)
{
    if (text.empty())
    {
        return 0;
    }
    if (scale < 1)
    {
        scale = 1;
    }
    return scale * (static_cast<int>(text.size()) * kGlyphAdvance - (kGlyphAdvance - kGlyphWidth));
}

int centredTextX(int width, std::string_view text, int scale)
{
    if (scale < 1)
    {
        scale = 1;
    }

    const InkBounds bounds = inkBounds(text);
    if (bounds.first > bounds.last)
    {
        return width / 2; // nothing is lit
    }

    const int ink = (bounds.last - bounds.first + 1) * scale;

    // Rounded up, so an ink width that cannot be halved leaves its spare pixel on
    // the left rather than the right.
    return (width - ink + 1) / 2 - bounds.first * scale;
}

int rightAlignedTextX(int right_edge, std::string_view text, int scale)
{
    if (scale < 1)
    {
        scale = 1;
    }

    const InkBounds bounds = inkBounds(text);
    if (bounds.first > bounds.last)
    {
        return right_edge;
    }

    return right_edge - (bounds.last + 1) * scale + 1;
}

int drawTextCentered(Surface &surface, int y, std::string_view text, Color color, int scale)
{
    return drawText(surface, centredTextX(surface.width(), text, scale), y, text, color, scale);
}

} // namespace matrixos
