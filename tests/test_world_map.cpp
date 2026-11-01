// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/worldmap/world_map_app.h"

#include "fake_clock.h"
#include "gfx/equal_earth.h"
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

struct Pixel
{
    int x;
    int y;
};

Pixel at(double latitude, double longitude)
{
    const PanelPoint point = equalEarthForward({latitude, longitude});
    return {static_cast<int>(point.x), static_cast<int>(point.y)};
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

TEST_CASE("Equal Earth fills the panel's width and comes back where it started")
{
    CHECK(equalEarthForward({0.0, -180.0}).x == Approx(0.0).margin(1e-9));
    CHECK(equalEarthForward({0.0, 180.0}).x == Approx(64.0).margin(1e-9));
    CHECK(equalEarthForward({0.0, 0.0}).y == Approx(16.0).margin(1e-9));
    CHECK(equalEarthForward({90.0, 0.0}).y > 0.0); // the poles stay on the panel

    for (const GeoPoint place :
         {GeoPoint{48.2, 16.4}, GeoPoint{-33.9, 151.2}, GeoPoint{64.1, -21.9}})
    {
        const auto back = equalEarthInverse(equalEarthForward(place));
        REQUIRE(back);
        CHECK(back->latitude == Approx(place.latitude).margin(1e-6));
        CHECK(back->longitude == Approx(place.longitude).margin(1e-6));
    }
}

TEST_CASE("the poles are lines, not the whole width: the corners are off the map")
{
    CHECK_FALSE(equalEarthInverse({0.5, 31.5}));
    CHECK_FALSE(equalEarthInverse({63.5, 0.5}));
    CHECK(equalEarthInverse({32.0, 31.0}));
}

TEST_CASE("the mask has the Sahara and an empty Pacific")
{
    CHECK(land(at(23.0, 10.0).x, at(23.0, 10.0).y));
    CHECK_FALSE(land(at(0.0, -150.0).x, at(0.0, -150.0).y));
    CHECK(land(at(-80.0, 0.0).x, at(-80.0, 0.0).y)); // Antarctica
}

TEST_CASE("Europe lies in daylight while Australia sleeps, mid-afternoon in July")
{
    FakeClock clock; // 2026-07-29 14:05 UTC
    WorldMapApp app(clock);
    app.update(Duration{0.016F});
    Surface frame(64, 32);
    app.render(frame);

    const Color europe = frame.pixel(at(48.0, 15.0).x, at(48.0, 15.0).y);
    const Color australia = frame.pixel(at(-25.0, 135.0).x, at(-25.0, 135.0).y);
    CHECK(brightness(europe) > 3 * brightness(australia));
}

TEST_CASE("rotating moves the time, press brings it back")
{
    FakeClock clock;
    WorldMapApp app(clock);
    app.onInput({InputType::Rotate, 3});
    CHECK(app.offsetHours() == 3);
    app.onInput({InputType::Rotate, 21});
    CHECK(app.offsetHours() == 0); // a full day on is now again
    app.onInput({InputType::Rotate, 25});
    CHECK(app.offsetHours() == 1);
    app.onInput({InputType::Rotate, -25});
    CHECK(app.offsetHours() == 0);
    app.onInput({InputType::Rotate, -24});
    CHECK(app.offsetHours() == 0);
    app.onInput({InputType::Rotate, -23});
    CHECK(app.offsetHours() == -23);

    // Twelve hours on, Australia is the bright one.
    app.onInput({InputType::Press, 0});
    app.onInput({InputType::Rotate, 12});
    app.update(Duration{0.016F});
    Surface frame(64, 32);
    app.render(frame);
    CHECK(brightness(frame.pixel(at(-25.0, 135.0).x, at(-25.0, 135.0).y)) >
          3 * brightness(frame.pixel(at(48.0, 15.0).x, at(48.0, 15.0).y)));

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
