// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/worldmap/world_map_app.h"

#include "gfx/font.h"
#include "gfx/world_map_data.h"
#include "os/clock.h"
#include "os/sun.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace matrixos
{
namespace
{

constexpr Color kLandDay{70, 170, 60};
constexpr Color kSeaDay{20, 70, 170};
constexpr Color kLandNight{14, 26, 14};
constexpr Color kSeaNight{3, 6, 20};
constexpr Color kSun{255, 220, 0};
constexpr Color kLabel{255, 255, 255};
constexpr Color kNoTime{255, 150, 0};

/// Full daylight from this elevation up, full night from the lower one down;
/// in between, twilight fades. -12 degrees is the end of nautical twilight.
constexpr double kDayFrom = 2.0;
constexpr double kNightFrom = -12.0;

constexpr double kPixelDegrees = 360.0 / world::kMapWidth;

/// The shading is redone when the time on the map has moved this far.
constexpr std::int64_t kReshadeSeconds = 60;

bool land(int x, int y)
{
    const int bit = y * world::kMapWidth + x;
    return (world::kLandMask[static_cast<std::size_t>(bit / 8)] >> (7 - bit % 8) & 1) != 0;
}

Color mix(Color night, Color day, double light)
{
    const auto channel = [light](std::uint8_t a, std::uint8_t b)
    { return static_cast<std::uint8_t>(std::lround(a + (b - a) * light)); };
    return {channel(night.r, day.r), channel(night.g, day.g), channel(night.b, day.b)};
}

void drawLabel(Surface &surface, int x, int y, const std::string &text, Color color)
{
    const int width = textWidth(text);
    for (int dy = -1; dy <= kGlyphHeight; ++dy)
    {
        for (int dx = -1; dx <= width; ++dx)
        {
            surface.setPixel(x + dx, y + dy, Color::black());
        }
    }
    drawText(surface, x, y, text, color);
}

} // namespace

WorldMapApp::WorldMapApp(const TimeProvider &time)
    : time_(time), map_(world::kMapWidth, world::kMapHeight)
{
}

void WorldMapApp::onInput(const InputEvent &event)
{
    if (event.type == InputType::Rotate)
    {
        offset_hours_ = std::clamp(offset_hours_ + event.delta, -kMaxOffsetHours, kMaxOffsetHours);
    }
    else if (event.type == InputType::Press)
    {
        offset_hours_ = 0;
    }
}

void WorldMapApp::update(Duration)
{
    known_ = time_.synchronized();
    if (!known_)
    {
        if (shaded_for_)
        {
            shaded_for_.reset();
        }
        return;
    }

    const std::int64_t when = time_.unixTime() + static_cast<std::int64_t>(offset_hours_) * 3600;
    if (!shaded_for_ || std::llabs(when - *shaded_for_) >= kReshadeSeconds)
    {
        shade(when);
    }
}

void WorldMapApp::shade(std::int64_t unix_seconds)
{
    const SunPosition sun = subsolarPoint(unix_seconds);
    for (int y = 0; y < world::kMapHeight; ++y)
    {
        const double latitude = 90.0 - (y + 0.5) * kPixelDegrees;
        for (int x = 0; x < world::kMapWidth; ++x)
        {
            const double longitude = -180.0 + (x + 0.5) * kPixelDegrees;
            const double elevation = sunElevation(sun, latitude, longitude);
            const double light =
                std::clamp((elevation - kNightFrom) / (kDayFrom - kNightFrom), 0.0, 1.0);
            map_.setPixel(x, y,
                          land(x, y) ? mix(kLandNight, kLandDay, light)
                                     : mix(kSeaNight, kSeaDay, light));
        }
    }
    sun_x_ = std::clamp(static_cast<int>((sun.longitude + 180.0) / kPixelDegrees), 0,
                        world::kMapWidth - 1);
    sun_y_ = std::clamp(static_cast<int>((90.0 - sun.latitude) / kPixelDegrees), 0,
                        world::kMapHeight - 1);
    shaded_for_ = unix_seconds;
}

void WorldMapApp::render(Surface &surface)
{
    if (!known_)
    {
        for (int y = 0; y < world::kMapHeight; ++y)
        {
            for (int x = 0; x < world::kMapWidth; ++x)
            {
                surface.setPixel(x, y, land(x, y) ? kLandNight : kSeaNight);
            }
        }
        drawLabel(surface, 2, 1, "NO TIME", kNoTime);
        return;
    }

    for (int y = 0; y < world::kMapHeight; ++y)
    {
        for (int x = 0; x < world::kMapWidth; ++x)
        {
            surface.setPixel(x, y, map_.pixel(x, y));
        }
    }
    surface.setPixel(sun_x_, sun_y_, kSun);

    if (offset_hours_ != 0)
    {
        const std::string label =
            (offset_hours_ > 0 ? "+" : "") + std::to_string(offset_hours_) + "H";
        drawLabel(surface, 2, 1, label, kLabel);
    }
}

} // namespace matrixos
