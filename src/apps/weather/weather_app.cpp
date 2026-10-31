// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/weather/weather_app.h"

#include "gfx/font.h"
#include "gfx/surface.h"

#include <array>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>

namespace matrixos
{
namespace
{

constexpr Color kText{255, 255, 255};
constexpr Color kLabel{130, 145, 175};
constexpr Color kLow{90, 140, 230};
constexpr Color kUnknown{70, 78, 96};
constexpr Color kStale{255, 150, 0};

constexpr Color kSun{255, 200, 0};
constexpr Color kCloud{200, 205, 215};
constexpr Color kDrop{60, 140, 255};
constexpr Color kFlake{220, 240, 255};
constexpr Color kMoon{230, 225, 180};

constexpr float kReadInterval = 0.5F;
constexpr int kColumns = 3;
constexpr int kHourStep = 3;
constexpr int kMaxPlaceChars = 10;

constexpr std::array<const char *, 7> kWeekdays = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

using Icon = std::array<const char *, 8>;

// Y sun, W cloud, B drop, S flake, M moon.
constexpr Icon kClear = {"...Y....", ".Y...Y..", "..YYY...", "Y.YYY.Y.",
                         "..YYY...", ".Y...Y..", "...Y....", "........"};
constexpr Icon kNight = {"..MMM...", ".MM.....", "MM......", "MM......",
                         "MM......", ".MM.....", "..MMM...", "........"};
constexpr Icon kPartly = {".Y.Y....", "..YY....", "YYY.WW..", ".Y.WWWW.",
                          "..WWWWWW", ".WWWWWWW", "..WWWWW.", "........"};
constexpr Icon kCloudy = {"........", "........", "...WW...", "..WWWW..",
                          ".WWWWWW.", "WWWWWWWW", ".WWWWWW.", "........"};
constexpr Icon kFog = {"........", "WWWWWW..", "........", ".WWWWWWW",
                       "........", "WWWWWW..", "........", ".WWWWWW."};
constexpr Icon kRain = {"..WWW...", ".WWWWW..", "WWWWWWW.", ".WWWWW..",
                        "........", ".B..B...", "...B..B.", ".B..B..."};
constexpr Icon kSnow = {"..WWW...", ".WWWWW..", "WWWWWWW.", ".WWWWW..",
                        "........", ".S...S..", "...S...S", ".S...S.."};
constexpr Icon kThunder = {"..WWW...", ".WWWWW..", "WWWWWWW.", ".WWYWW..",
                           "...Y....", "..YY....", "...Y....", "..Y....."};

const Icon &iconFor(Sky sky, bool daytime)
{
    switch (sky)
    {
    case Sky::Clear:
        return daytime ? kClear : kNight;
    case Sky::PartlyCloudy:
        return kPartly;
    case Sky::Cloudy:
        return kCloudy;
    case Sky::Fog:
        return kFog;
    case Sky::Rain:
        return kRain;
    case Sky::Snow:
        return kSnow;
    case Sky::Thunder:
        return kThunder;
    }
    return kCloudy;
}

Color paletteColor(char key)
{
    switch (key)
    {
    case 'Y':
        return kSun;
    case 'B':
        return kDrop;
    case 'S':
        return kFlake;
    case 'M':
        return kMoon;
    default:
        return kCloud;
    }
}

void drawIcon(Surface &surface, int x, int y, const Icon &icon, int scale,
              std::optional<Color> tint = std::nullopt)
{
    for (int row = 0; row < 8; ++row)
    {
        for (int column = 0; column < 8; ++column)
        {
            const char key = icon[row][column];
            if (key == '.')
            {
                continue;
            }
            const Color color = tint.value_or(paletteColor(key));
            for (int dy = 0; dy < scale; ++dy)
            {
                for (int dx = 0; dx < scale; ++dx)
                {
                    surface.setPixel(x + column * scale + dx, y + row * scale + dy, color);
                }
            }
        }
    }
}

std::string degrees(float temperature)
{
    const long rounded = std::lround(temperature);
    return std::to_string(rounded == 0 ? 0 : rounded);
}

/// A temperature with a small ring for the degree sign, which the font lacks.
void drawTemperature(Surface &surface, int x, int y, const std::string &text, Color color,
                     int scale)
{
    const int end = drawText(surface, x, y, text, color, scale);
    const int ring_x = end;
    for (int dy = 0; dy < 3; ++dy)
    {
        for (int dx = 0; dx < 3; ++dx)
        {
            if (dx == 1 && dy == 1)
            {
                continue;
            }
            for (int sy = 0; sy < scale; ++sy)
            {
                for (int sx = 0; sx < scale; ++sx)
                {
                    surface.setPixel(ring_x + dx * scale + sx, y + dy * scale + sy, color);
                }
            }
        }
    }
}

int temperatureWidth(const std::string &text, int scale)
{
    return textWidth(text, scale) + 4 * scale;
}

void drawCenteredTemperature(Surface &surface, int centre, int y, const std::string &text,
                             Color color)
{
    drawTemperature(surface, centre - temperatureWidth(text, 1) / 2, y, text, color, 1);
}

/// The font is ASCII; a place name from the geocoder may not be.
std::string panelLabel(const std::string &name)
{
    std::string label;
    for (const char c : name)
    {
        if (static_cast<unsigned char>(c) < 0x80 && label.size() < kMaxPlaceChars)
        {
            label += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return label;
}

int columnCentre(int width, int column)
{
    return width * (2 * column + 1) / (2 * kColumns);
}

} // namespace

WeatherApp::WeatherApp(const WeatherService &weather) : weather_(weather) {}

void WeatherApp::onEnter()
{
    snapshot_ = weather_.snapshot();
    since_read_ = 0.0F;
}

void WeatherApp::onInput(const InputEvent &event)
{
    if (event.type != InputType::Rotate)
    {
        return;
    }

    constexpr int kWindows = 3;
    const int next = (static_cast<int>(window_) + event.delta) % kWindows;
    window_ = static_cast<Window>((next + kWindows) % kWindows);
}

void WeatherApp::update(Duration dt)
{
    since_read_ += dt.count();
    if (since_read_ >= kReadInterval)
    {
        snapshot_ = weather_.snapshot();
        since_read_ = 0.0F;
    }
}

void WeatherApp::render(Surface &surface)
{
    if (!snapshot_.forecast)
    {
        renderUnknown(surface);
        return;
    }

    switch (window_)
    {
    case Window::Now:
        renderNow(surface, *snapshot_.forecast);
        break;
    case Window::Hours:
        renderHours(surface, *snapshot_.forecast);
        break;
    case Window::Days:
        renderDays(surface, *snapshot_.forecast);
        break;
    }

    if (snapshot_.stale)
    {
        surface.setPixel(surface.width() - 2, 0, kStale);
        surface.setPixel(surface.width() - 1, 0, kStale);
        surface.setPixel(surface.width() - 2, 1, kStale);
        surface.setPixel(surface.width() - 1, 1, kStale);
    }
}

void WeatherApp::renderNow(Surface &surface, const Forecast &forecast) const
{
    drawIcon(surface, 2, 3, iconFor(forecast.sky, forecast.daytime), 2);
    drawTemperature(surface, 22, 4, degrees(forecast.temperature), snapshot_.stale ? kLabel : kText,
                    2);
    drawTextCentered(surface, 24, panelLabel(snapshot_.place.name), kLabel);
}

void WeatherApp::renderHours(Surface &surface, const Forecast &forecast) const
{
    for (int column = 0; column < kColumns; ++column)
    {
        const std::size_t index = static_cast<std::size_t>((column + 1) * kHourStep);
        if (index >= forecast.hours.size())
        {
            break;
        }
        const Forecast::Hour &hour = forecast.hours[index];
        const int centre = columnCentre(surface.width(), column);
        const std::string label = (hour.hour < 10 ? "0" : "") + std::to_string(hour.hour);

        drawText(surface, centre - textWidth(label) / 2, 0, label, kLabel);
        drawIcon(surface, centre - 4, 10, iconFor(hour.sky, true), 1);
        drawCenteredTemperature(surface, centre, 22, degrees(hour.temperature), kText);
    }
}

void WeatherApp::renderDays(Surface &surface, const Forecast &forecast) const
{
    for (int column = 0; column < kColumns; ++column)
    {
        const std::size_t index = static_cast<std::size_t>(column);
        if (index >= forecast.days.size())
        {
            break;
        }
        const Forecast::Day &day = forecast.days[index];
        const int centre = columnCentre(surface.width(), column);
        const char *label = kWeekdays[static_cast<std::size_t>(day.weekday) % kWeekdays.size()];

        drawText(surface, centre - textWidth(label) / 2, 0, label, kLabel);
        drawIcon(surface, centre - 4, 8, iconFor(day.sky, true), 1);
        drawCenteredTemperature(surface, centre, 17, degrees(day.high), kText);
        drawCenteredTemperature(surface, centre, 25, degrees(day.low), kLow);
    }
}

void WeatherApp::renderUnknown(Surface &surface) const
{
    drawIcon(surface, 2, 3, kCloudy, 2, kUnknown);
    drawText(surface, 22, 4, "--", kUnknown, 2);
    drawTextCentered(surface, 24, "NO DATA", kStale);
}

} // namespace matrixos
