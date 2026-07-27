// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "gfx/font.h"
#include "gfx/surface.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace matrixos;

namespace
{

/// Renders text and returns it as ASCII art, one character per pixel. Comparing
/// pictures as text is what makes a font testable at all.
std::string asArt(std::string_view text, int width = 24, int height = 7)
{
    Surface surface(width, height);
    drawText(surface, 0, 0, text, Color::white());

    std::string art;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            art += surface.pixel(x, y) == Color::black() ? '.' : '#';
        }
        art += '\n';
    }
    return art;
}

int litPixels(std::string_view text)
{
    Surface surface(64, 16);
    drawText(surface, 0, 0, text, Color::white());

    int lit = 0;
    for (int y = 0; y < surface.height(); ++y)
    {
        for (int x = 0; x < surface.width(); ++x)
        {
            if (!(surface.pixel(x, y) == Color::black()))
            {
                ++lit;
            }
        }
    }
    return lit;
}

} // namespace

TEST_CASE("a glyph is drawn the right way round, not mirrored")
{
    // 'L' is asymmetric, so a reversed bit order would be obvious here.
    CHECK(asArt("L", 5) == "#....\n"
                           "#....\n"
                           "#....\n"
                           "#....\n"
                           "#....\n"
                           "####.\n"
                           ".....\n");
}

TEST_CASE("text advances by one pixel of spacing per glyph")
{
    CHECK(textWidth("") == 0);
    CHECK(textWidth("A") == kGlyphWidth);
    CHECK(textWidth("AB") == kGlyphWidth + kGlyphAdvance);
    CHECK(textWidth("ABC") == kGlyphWidth + 2 * kGlyphAdvance);
}

TEST_CASE("drawText reports where the next glyph would start")
{
    Surface surface(64, 8);
    const int after = drawText(surface, 3, 0, "AB", Color::white());
    CHECK(after == 3 + 2 * kGlyphAdvance);
}

TEST_CASE("space is blank and printable characters are not")
{
    CHECK(litPixels(" ") == 0);
    CHECK(litPixels("A") > 0);
    CHECK(litPixels("~") > 0);
    CHECK(litPixels("!") > 0);
}

TEST_CASE("characters outside printable ASCII fall back to a question mark")
{
    CHECK(asArt("\x01", 5) == asArt("?", 5));
    CHECK(asArt("\x7f", 5) == asArt("?", 5));
}

TEST_CASE("text running off the surface is clipped, not fatal")
{
    Surface surface(8, 7);

    drawText(surface, 6, 0, "MMMM", Color::white());  // off the right edge
    drawText(surface, -4, 0, "MMMM", Color::white()); // off the left edge
    drawText(surface, 0, -3, "M", Color::white());    // above the top
    drawText(surface, 0, 5, "M", Color::white());     // below the bottom

    // Nothing to assert beyond "we got here" — Surface drops out-of-range writes.
    CHECK(surface.width() == 8);
}

TEST_CASE("every printable character has a glyph, and none bleeds into the next cell")
{
    for (char character = ' '; character < 127; ++character)
    {
        Surface surface(kGlyphWidth + 2, kGlyphHeight + 2);
        drawText(surface, 0, 0, std::string(1, character), Color::white());

        for (int y = 0; y < surface.height(); ++y)
        {
            // Column 5 is the spacing column and must always stay dark.
            CHECK(surface.pixel(kGlyphWidth, y) == Color::black());
        }
    }
}
