// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "gfx/font.h"
#include "gfx/surface.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <utility>

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

namespace
{

/// Ink margins of whatever the surface holds.
std::pair<int, int> inkMargins(const Surface &surface)
{
    int first = surface.width();
    int last = -1;

    for (int y = 0; y < surface.height(); ++y)
    {
        for (int x = 0; x < surface.width(); ++x)
        {
            if (!(surface.pixel(x, y) == Color::black()))
            {
                first = std::min(first, x);
                last = std::max(last, x);
            }
        }
    }
    return {first, surface.width() - 1 - last};
}

} // namespace

TEST_CASE("centred text is centred on its ink, not on its advance box")
{
    // textWidth bills a full cell per glyph, but almost no glyph fills its last
    // column. Centring on it leaves two more pixels on the right than the left,
    // which is what this guards against.
    for (const char *text : {"READY", "HI 0", "GAME OVER", "SNAKE", "E", "PAUSE", "NO CLOCK"})
    {
        for (const int scale : {1, 2})
        {
            if (textWidth(text, scale) > 64)
            {
                continue;
            }

            Surface surface(64, 32);
            surface.clear();
            drawTextCentered(surface, 8, text, Color::white(), scale);

            const auto [left, right] = inkMargins(surface);
            INFO("text=" << text << " scale=" << scale);
            REQUIRE(left >= 0);

            // Exact where the ink can be halved, and never off by more than the
            // one pixel an odd width forces — which goes to the left.
            CHECK(left - right >= 0);
            CHECK(left - right <= 1);
        }
    }
}

TEST_CASE("centring survives text that is blank or has blank ends")
{
    Surface surface(64, 32);

    CHECK(centredTextX(64, "") == 32);
    CHECK(centredTextX(64, "   ") == 32);

    // A leading space must not drag the visible part off centre.
    surface.clear();
    drawTextCentered(surface, 8, " E", Color::white());
    const auto padded = inkMargins(surface);

    surface.clear();
    drawTextCentered(surface, 8, "E", Color::white());
    const auto bare = inkMargins(surface);

    CHECK(padded == bare);
}

TEST_CASE("every glyph is drawn from the left edge of its own cell")
{
    // The source font indents its narrow glyphs by a column. Left in place, that
    // put three pixels of space before a T or a 1 and two after it, so the letter
    // read as leaning right against its neighbours.
    const std::string text = "STUDY QUIZ 10";

    Surface surface(static_cast<int>(text.size()) * kGlyphAdvance + 4, 12);
    surface.clear();
    drawText(surface, 0, 2, text, Color::white());

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == ' ')
        {
            continue;
        }

        const int cell = static_cast<int>(i) * kGlyphAdvance;
        bool lit = false;
        for (int y = 0; y < surface.height(); ++y)
        {
            if (!(surface.pixel(cell, y) == Color::black()))
            {
                lit = true;
            }
        }

        INFO("glyph '" << text[i] << "' at cell " << cell);
        CHECK(lit);
    }
}
