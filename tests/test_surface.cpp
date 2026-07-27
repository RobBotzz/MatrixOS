// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "gfx/surface.h"

#include <catch2/catch_test_macros.hpp>

using matrixos::Color;
using matrixos::Surface;

TEST_CASE("a new surface has the requested size and is black")
{
    Surface surface(64, 32);

    CHECK(surface.width() == 64);
    CHECK(surface.height() == 32);
    CHECK(surface.bytes().size() == 64u * 32u * 3u);
    CHECK(surface.pixel(0, 0) == Color::black());
    CHECK(surface.pixel(63, 31) == Color::black());
}

TEST_CASE("a pixel reads back what was written")
{
    Surface surface(8, 4);

    surface.setPixel(3, 2, Color{10, 20, 30});

    CHECK(surface.pixel(3, 2) == Color{10, 20, 30});
    CHECK(surface.pixel(2, 2) == Color::black());
}

TEST_CASE("coordinates outside the surface are ignored, not fatal")
{
    Surface surface(4, 4);

    surface.setPixel(-1, 0, Color::white());
    surface.setPixel(4, 0, Color::white());
    surface.setPixel(0, -1, Color::white());
    surface.setPixel(0, 4, Color::white());

    CHECK(surface.pixel(-1, 0) == Color::black());
    CHECK(surface.pixel(99, 99) == Color::black());

    // No write leaked into the buffer.
    for (const auto value : surface.bytes())
    {
        CHECK(value == 0);
    }
}

TEST_CASE("clear fills every pixel")
{
    Surface surface(3, 2);

    surface.clear(Color{1, 2, 3});

    for (int y = 0; y < surface.height(); ++y)
    {
        for (int x = 0; x < surface.width(); ++x)
        {
            CHECK(surface.pixel(x, y) == Color{1, 2, 3});
        }
    }
}

TEST_CASE("clear back to black empties the buffer")
{
    Surface surface(3, 2);
    surface.clear(Color::white());

    surface.clear();

    for (const auto value : surface.bytes())
    {
        CHECK(value == 0);
    }
}

TEST_CASE("bytes are RGB24 in row-major order")
{
    Surface surface(2, 2);

    surface.setPixel(1, 0, Color{1, 2, 3});
    surface.setPixel(0, 1, Color{4, 5, 6});

    const auto bytes = surface.bytes();
    CHECK(bytes[3] == 1);
    CHECK(bytes[4] == 2);
    CHECK(bytes[5] == 3);
    CHECK(bytes[6] == 4);
    CHECK(bytes[7] == 5);
    CHECK(bytes[8] == 6);
}

TEST_CASE("a degenerate size yields an empty surface rather than a crash")
{
    Surface surface(0, -5);

    CHECK(surface.width() == 0);
    CHECK(surface.height() == 0);
    CHECK(surface.bytes().empty());
    CHECK(surface.pixel(0, 0) == Color::black());
}
