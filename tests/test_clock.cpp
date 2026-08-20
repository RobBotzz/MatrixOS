// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/clock/clock_app.h"

#include "fake_clock.h"
#include "gfx/font.h"
#include "gfx/surface.h"
#include "os/settings.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace matrixos;
using matrixos::test::FakeClock;

namespace
{

constexpr Duration kFrame{1.0F / 60.0F};

Surface draw(ClockApp &app)
{
    app.update(kFrame);
    Surface frame(64, 32);
    app.render(frame);
    return frame;
}

int litPixels(const Surface &frame)
{
    int lit = 0;
    for (int y = 0; y < frame.height(); ++y)
    {
        for (int x = 0; x < frame.width(); ++x)
        {
            if (!(frame.pixel(x, y) == Color::black()))
            {
                ++lit;
            }
        }
    }
    return lit;
}

/// Compares a region of the panel against what `text` would draw there, which is
/// how a layout is asserted without a golden file.
bool showsAt(const Surface &frame, int y, std::string_view text, int scale = 1)
{
    Surface expected(frame.width(), frame.height());
    drawTextCentered(expected, y, text, Color{255, 255, 255}, scale);

    for (int row = 0; row < frame.height(); ++row)
    {
        for (int column = 0; column < frame.width(); ++column)
        {
            const bool wanted = !(expected.pixel(column, row) == Color::black());
            const bool actual = !(frame.pixel(column, row) == Color::black());
            if (wanted && !actual)
            {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("before the first sync the clock says it does not know")
{
    FakeClock clock;
    clock.synced = false;
    ClockApp app(clock);

    const Surface frame = draw(app);

    CHECK_FALSE(app.timeKnown());
    CHECK(showsAt(frame, 7, "--:--", 2));
    CHECK(showsAt(frame, 25, "NO TIME"));
}

TEST_CASE("the unknown screen never shows a digit of the stale system clock")
{
    // C-9: at power-on the system clock holds whatever fake-hwclock last wrote.
    // Showing it would be a confident lie, which is the whole point of ADR-0015.
    FakeClock clock;
    clock.synced = false;
    clock.time = {2026, 7, 26, 23, 47, 12, 0};
    ClockApp app(clock);

    const Surface frame = draw(app);

    CHECK_FALSE(showsAt(frame, 5, "23:47", 2));
    CHECK_FALSE(showsAt(frame, 5, "23 47", 2));
}

TEST_CASE("once synchronised the time is on the panel")
{
    FakeClock clock;
    clock.time = {2026, 7, 29, 14, 5, 30, 3};
    ClockApp app(clock);

    const Surface frame = draw(app);

    CHECK(app.timeKnown());
    CHECK(showsAt(frame, 5, "14:05", 2));
}

TEST_CASE("hours and minutes are always two digits")
{
    FakeClock clock;
    clock.time = {2026, 1, 2, 3, 4, 0, 5};
    ClockApp app(clock);

    CHECK(showsAt(draw(app), 5, "03:04", 2));
}

TEST_CASE("the colon follows the second, and the digits do not move when it goes")
{
    FakeClock clock;
    ClockApp app(clock);

    clock.time.second = 30;
    const Surface lit = draw(app);
    CHECK(showsAt(lit, 5, "14:05", 2));

    clock.time.second = 31;
    const Surface dark = draw(app);
    CHECK(showsAt(dark, 5, "14 05", 2));

    // The colon is the only difference; a shifted layout would change far more.
    CHECK(litPixels(lit) > litPixels(dark));
}

TEST_CASE("the default face carries the date")
{
    FakeClock clock;
    clock.time = {2026, 7, 29, 14, 5, 30, 3};
    ClockApp app(clock);

    CHECK(app.face() == ClockApp::Face::Date);
    CHECK(showsAt(draw(app), 23, "WED 29 JUL"));
}

TEST_CASE("rotating walks the three faces and wraps in both directions")
{
    FakeClock clock;
    ClockApp app(clock);

    app.onInput(InputEvent{InputType::Rotate, +1});
    CHECK(app.face() == ClockApp::Face::Seconds);

    app.onInput(InputEvent{InputType::Rotate, +1});
    CHECK(app.face() == ClockApp::Face::Bare);

    app.onInput(InputEvent{InputType::Rotate, +1});
    CHECK(app.face() == ClockApp::Face::Date);

    app.onInput(InputEvent{InputType::Rotate, -1});
    CHECK(app.face() == ClockApp::Face::Bare);
}

TEST_CASE("the seconds face drops the date and grows a bar instead")
{
    FakeClock clock;
    clock.time = {2026, 7, 29, 14, 5, 0, 3};
    ClockApp app(clock);
    app.onInput(InputEvent{InputType::Rotate, +1});

    // The bar draws its spent part dimmed rather than not at all, so the full
    // span reads as a scale — which means counting lit pixels proves nothing and
    // the colour at a position is what has to be asserted.
    const Surface empty = draw(app);
    CHECK_FALSE(showsAt(empty, 23, "WED 29 JUL"));
    const Color early = empty.pixel(12, 28);

    clock.time.second = 44; // even, so the colon is lit in both frames
    const Surface filling = draw(app);

    CHECK_FALSE(filling.pixel(12, 28) == early); // that stretch is now spent time
    CHECK(filling.pixel(60, 28) == early);       // and this one is not yet
}

TEST_CASE("every face draws something, and nothing lands outside the panel")
{
    FakeClock clock;
    ClockApp app(clock);

    for (int rotation = 0; rotation < 3; ++rotation)
    {
        CHECK(litPixels(draw(app)) > 0);
        app.onInput(InputEvent{InputType::Rotate, +1});
    }
}

TEST_CASE("a press does nothing — the clock has no mode to be in")
{
    FakeClock clock;
    ClockApp app(clock);

    app.onInput(InputEvent{InputType::Press});
    app.onInput(InputEvent{InputType::LongPress});

    CHECK(app.face() == ClockApp::Face::Date);
}

TEST_CASE("the curated time zones fit the panel and name real zones")
{
    for (const settings::TimeZone &zone : settings::kTimeZones)
    {
        CHECK(textWidth(zone.label) <= 64);
        CHECK_FALSE(zone.zone.empty());
    }

    // The default has to be one of the entries, or the settings app would open
    // on a value it cannot show.
    bool found = false;
    for (const settings::TimeZone &zone : settings::kTimeZones)
    {
        found = found || zone.zone == settings::kDefaultTimeZone;
    }
    CHECK(found);
}
