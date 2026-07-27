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

} // namespace

int drawText(Surface &surface, int x, int y, std::string_view text, Color color)
{
    int cursor = x;

    for (const char character : text)
    {
        const std::uint8_t *glyph = glyphFor(character);

        for (int row = 0; row < kGlyphHeight; ++row)
        {
            for (int column = 0; column < kGlyphWidth; ++column)
            {
                // Bit 0 is the leftmost pixel; see font5x7_data.h.
                if ((glyph[row] & (1U << column)) != 0)
                {
                    surface.setPixel(cursor + column, y + row, color);
                }
            }
        }

        cursor += kGlyphAdvance;
    }

    return cursor;
}

int textWidth(std::string_view text)
{
    if (text.empty())
    {
        return 0;
    }
    return static_cast<int>(text.size()) * kGlyphAdvance - (kGlyphAdvance - kGlyphWidth);
}

} // namespace matrixos
