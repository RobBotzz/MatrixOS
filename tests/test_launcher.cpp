// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "gfx/font.h"
#include "gfx/surface.h"
#include "os/launcher.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

using namespace matrixos;

namespace
{

constexpr Color kSelectionBar{255, 190, 0};

void send(Launcher &launcher, InputType type, int delta = 0)
{
    launcher.onInput(InputEvent{type, delta});
}

/// Which visible row carries the selection marker, if any. This is how a test can
/// tell that the selection is on screen without knowing the scroll arithmetic.
std::optional<int> selectionRow(const Surface &surface)
{
    for (int y = 0; y < surface.height(); ++y)
    {
        if (surface.pixel(0, y) == kSelectionBar)
        {
            return y / (kGlyphHeight + 1);
        }
    }
    return std::nullopt;
}

Surface renderOf(Launcher &launcher, int width = 64, int height = 32)
{
    Surface surface(width, height);
    launcher.render(surface);
    return surface;
}

std::vector<std::string_view> names(std::size_t count)
{
    static const std::vector<std::string_view> pool = {"One", "Two",   "Three", "Four", "Five",
                                                       "Six", "Seven", "Eight", "Nine", "Ten"};
    return {pool.begin(), pool.begin() + static_cast<std::ptrdiff_t>(count)};
}

} // namespace

TEST_CASE("rotating moves the selection and wraps in both directions")
{
    Launcher launcher(names(3), nullptr);
    REQUIRE(launcher.selected() == 0);

    send(launcher, InputType::Rotate, +1);
    CHECK(launcher.selected() == 1);

    send(launcher, InputType::Rotate, -1);
    send(launcher, InputType::Rotate, -1);
    CHECK(launcher.selected() == 2); // wrapped backwards past zero

    send(launcher, InputType::Rotate, +1);
    CHECK(launcher.selected() == 0);
}

TEST_CASE("a coalesced rotation larger than the list still lands in range")
{
    Launcher launcher(names(3), nullptr);

    send(launcher, InputType::Rotate, -10);
    CHECK(launcher.selected() < 3);

    send(launcher, InputType::Rotate, +25);
    CHECK(launcher.selected() < 3);
}

TEST_CASE("pressing reports the selected index")
{
    std::optional<std::size_t> started;
    Launcher launcher(names(4), [&started](std::size_t index) { started = index; });

    send(launcher, InputType::Rotate, +2);
    send(launcher, InputType::Press);

    REQUIRE(started.has_value());
    CHECK(*started == 2);
}

TEST_CASE("gestures the launcher does not use are ignored")
{
    std::optional<std::size_t> started;
    Launcher launcher(names(3), [&started](std::size_t index) { started = index; });

    send(launcher, InputType::DoublePress);
    send(launcher, InputType::LongPress);

    CHECK_FALSE(started.has_value());
    CHECK(launcher.selected() == 0);
}

TEST_CASE("an empty launcher says so and swallows input")
{
    Launcher launcher({}, nullptr);

    send(launcher, InputType::Rotate, +1);
    send(launcher, InputType::Press);

    CHECK(launcher.selected() == 0);
    CHECK_FALSE(selectionRow(renderOf(launcher)).has_value());
}

TEST_CASE("pressing without a handler does not crash")
{
    Launcher launcher(names(2), nullptr);
    send(launcher, InputType::Press);
    CHECK(launcher.selected() == 0);
}

TEST_CASE("the selected entry is drawn brighter than the others")
{
    Launcher launcher(names(2), nullptr);
    const Surface surface = renderOf(launcher);

    int first_row_lit = 0;
    int second_row_lit = 0;
    for (int x = kGlyphWidth; x < surface.width(); ++x)
    {
        for (int y = 0; y < kGlyphHeight; ++y)
        {
            if (!(surface.pixel(x, y) == Color::black()))
            {
                ++first_row_lit;
            }
            if (!(surface.pixel(x, y + kGlyphHeight + 1) == Color::black()))
            {
                ++second_row_lit;
            }
        }
    }

    CHECK(first_row_lit > 0);
    CHECK(second_row_lit > 0);
    CHECK(surface.pixel(0, 0) == kSelectionBar);
    CHECK_FALSE(surface.pixel(0, kGlyphHeight + 1) == kSelectionBar);
}

TEST_CASE("the selection stays on screen however long the list is")
{
    Launcher launcher(names(10), nullptr);

    // A 32 pixel panel shows four rows, so a ten-entry list has to scroll.
    for (std::size_t step = 0; step < 10; ++step)
    {
        REQUIRE(launcher.selected() == step);
        const Surface surface = renderOf(launcher);
        INFO("selection " << step << " must be visible");
        CHECK(selectionRow(surface).has_value());
        send(launcher, InputType::Rotate, +1);
    }
}
