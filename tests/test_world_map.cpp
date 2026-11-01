// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/worldmap/world_map_app.h"

#include "fake_clock.h"
#include "gfx/surface.h"
#include "gfx/world_map_data.h"
#include "os/sun.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace matrixos;
using Catch::Approx;
using matrixos::test::FakeClock;

namespace
{

constexpr std::int64_t kMarchEquinox = 1774017960;     // 2026-03-20 14:46 UTC
constexpr std::int64_t kJuneSolstice = 1782030240;     // 2026-06-21 08:24 UTC
constexpr std::int64_t kDecemberSolstice = 1797886200; // 2026-12-21 20:50 UTC
constexpr std::int64_t kEquinoxNoon = 1774008000;      // 2026-03-20 12:00 UTC

int column(double longitude)
{
    return static_cast<int>((longitude + 180.0) / 5.625);
}

int row(double latitude)
{
    return static_cast<int>((90.0 - latitude) / 5.625);
}

bool land(int x, int y)
{
    const int bit = y * world::kMapWidth + x;
    return (world::kLandMask[static_cast<std::size_t>(bit / 8)] >> (7 - bit % 8) & 1) != 0;
}

int brightness(Color colour)
{
    return colour.r + colour.g + colour.b;
}

} // namespace

TEST_CASE("the sun stands over the equator at an equinox and the tropics at a solstice")
{
    CHECK(subsolarPoint(kMarchEquinox).latitude == Approx(0.0).margin(0.5));
    CHECK(subsolarPoint(kJuneSolstice).latitude == Approx(23.44).margin(0.5));
    CHECK(subsolarPoint(kDecemberSolstice).latitude == Approx(-23.44).margin(0.5));
}

TEST_CASE("at noon UTC the sun is near the Greenwich meridian")
{
    // The equation of time keeps it within a few degrees, never more than four.
    CHECK(subsolarPoint(kEquinoxNoon).longitude == Approx(0.0).margin(4.5));
}

TEST_CASE("the sun is overhead at the subsolar point and below the opposite one")
{
    const SunPosition sun = subsolarPoint(kJuneSolstice);
    CHECK(sunElevation(sun, sun.latitude, sun.longitude) == Approx(90.0).margin(0.01));
    CHECK(sunElevation(sun, -sun.latitude, sun.longitude + 180.0) == Approx(-90.0).margin(0.01));
    CHECK(sunElevation(sun, 85.0, sun.longitude + 180.0) > 0.0); // midnight sun
}

TEST_CASE("the mask has the Sahara and an empty Pacific")
{
    CHECK(land(column(10.0), row(23.0)));
    CHECK_FALSE(land(column(-150.0), row(0.0)));
    CHECK(land(column(0.0), row(-85.0))); // Antarctica
}

TEST_CASE("Europe lies in daylight while Australia sleeps, mid-afternoon in July")
{
    FakeClock clock; // 2026-07-29 14:05 UTC
    WorldMapApp app(clock);
    app.update(Duration{0.016F});
    Surface frame(64, 32);
    app.render(frame);

    const Color europe = frame.pixel(column(15.0), row(48.0));
    const Color australia = frame.pixel(column(135.0), row(-25.0));
    CHECK(brightness(europe) > 3 * brightness(australia));
}

TEST_CASE("rotating moves the time, press brings it back")
{
    FakeClock clock;
    WorldMapApp app(clock);
    app.onInput({InputType::Rotate, 3});
    CHECK(app.offsetHours() == 3);
    app.onInput({InputType::Rotate, 1000});
    CHECK(app.offsetHours() == WorldMapApp::kMaxOffsetHours);

    // Twelve hours on, Australia is the bright one.
    app.onInput({InputType::Press, 0});
    app.onInput({InputType::Rotate, 12});
    app.update(Duration{0.016F});
    Surface frame(64, 32);
    app.render(frame);
    CHECK(brightness(frame.pixel(column(135.0), row(-25.0))) >
          3 * brightness(frame.pixel(column(15.0), row(48.0))));

    app.onInput({InputType::Press, 0});
    CHECK(app.offsetHours() == 0);
}

TEST_CASE("without the time the map says so instead of guessing")
{
    FakeClock clock;
    clock.synced = false;
    WorldMapApp app(clock);
    app.update(Duration{0.016F});
    Surface frame(64, 32);
    app.render(frame);

    bool orange = false;
    for (int y = 0; y < 10 && !orange; ++y)
    {
        for (int x = 0; x < 64 && !orange; ++x)
        {
            orange = frame.pixel(x, y).r == 255 && frame.pixel(x, y).g == 150;
        }
    }
    CHECK(orange);
}
