// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/clock/clock_app.h"

#include "gfx/font.h"
#include "gfx/surface.h"
#include "os/clock.h"

#include <array>
#include <string>

namespace matrixos
{
namespace
{

constexpr Color kDigits{255, 255, 255};
constexpr Color kDate{130, 145, 175};
constexpr Color kAccent{255, 190, 0};
constexpr Color kUnknown{70, 78, 96};
constexpr Color kBarSpent{28, 32, 44};

constexpr int kTimeTop = 5;
constexpr int kBareTop = 9;
constexpr int kDateTop = 23;
constexpr int kBarTop = 28;
constexpr int kBarHeight = 2;
constexpr int kUnknownTop = 7;
constexpr int kUnknownLabelTop = 25;
constexpr int kTimeScale = 2;

constexpr std::array<const char *, 7> kWeekdays = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
constexpr std::array<const char *, 12> kMonths = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                                  "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

std::string twoDigits(int value)
{
    std::string text = std::to_string(value);
    return text.size() < 2 ? "0" + text : text;
}

void drawCentered(Surface &surface, int y, std::string_view text, Color color, int scale = 1)
{
    drawTextCentered(surface, y, text, color, scale);
}

} // namespace

ClockApp::ClockApp(const TimeProvider &time) : time_(time) {}

void ClockApp::onInput(const InputEvent &event)
{
    if (event.type != InputType::Rotate)
    {
        return;
    }

    constexpr int kFaces = 3;
    const int next = (static_cast<int>(face_) + event.delta) % kFaces;
    face_ = static_cast<Face>((next + kFaces) % kFaces);
}

void ClockApp::update(Duration dt)
{
    blink_ += dt.count();
    known_ = time_.synchronized();
}

void ClockApp::render(Surface &surface)
{
    if (!known_)
    {
        // Placeholders in the shape of the real thing, so the panel reads as
        // "not yet" rather than as "broken".
        drawCentered(surface, kUnknownTop, "--:--", kUnknown, kTimeScale);
        drawCentered(surface, kUnknownLabelTop, "NO TIME", kAccent);
        return;
    }

    const LocalTime now = time_.now();

    // The colon carries the second, driven by the time itself rather than by a
    // timer of its own, so it can never drift against the digits. Replaced by a
    // space rather than dropped: the font gives both the same advance, so the
    // digits do not move.
    const bool colon = now.second % 2 == 0;
    const std::string time = twoDigits(now.hour) + (colon ? ":" : " ") + twoDigits(now.minute);

    drawCentered(surface, face_ == Face::Bare ? kBareTop : kTimeTop, time, kDigits, kTimeScale);

    if (face_ == Face::Date)
    {
        const std::string date = std::string(kWeekdays[now.weekday % 7]) + " " +
                                 std::to_string(now.day) + " " + kMonths[(now.month - 1) % 12];
        drawCentered(surface, kDateTop, date, kDate);
    }
    else if (face_ == Face::Seconds)
    {
        // The minute as a bar: a glance says how far through it is, without
        // reading two more digits.
        const int width = surface.width() - 4;
        const int filled = now.second * width / 60;
        for (int x = 0; x < width; ++x)
        {
            for (int row = 0; row < kBarHeight; ++row)
            {
                surface.setPixel(2 + x, kBarTop + row, x < filled ? kAccent : kBarSpent);
            }
        }
    }
}

} // namespace matrixos
