// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/pomodoro/pomodoro.h"

#include "gfx/font.h"
#include "gfx/surface.h"
#include "os/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace matrixos
{
namespace
{

constexpr Color scaled(Color color, int percent)
{
    return Color{static_cast<std::uint8_t>(color.r * percent / 100),
                 static_cast<std::uint8_t>(color.g * percent / 100),
                 static_cast<std::uint8_t>(color.b * percent / 100)};
}

constexpr Color kFocusAccent{0xFF, 0x43, 0x26};
constexpr Color kFocusDigits{255, 255, 255};
constexpr Color kLeafColor{40, 180, 70};

constexpr Color kBreakAccent{0x2A, 0xE0, 0x70};
constexpr Color kBreakDigits{180, 255, 200};

constexpr int kSpentPercent = 14;
constexpr int kDrainedPercent = 45;
constexpr int kSteamPercent = 65;

constexpr int kDigitScale = 2;
constexpr int kColonWidth = 2;
constexpr int kBarHeight = 2;

constexpr int kTomatoCenterX = 10;
constexpr int kTomatoCenterY = 15;
constexpr int kTomatoRadiusX = 8;
constexpr int kTomatoRadiusY = 9;
constexpr int kLeafTop = 2;

// One layout for both modes; only the icon, the word and the colours differ.
constexpr int kLabelTop = 4;
constexpr int kDigitsTop = 13;
constexpr int kColumnLeft = 20;
constexpr int kDigitAdvance = 10;
constexpr int kPairOffset = 24;
constexpr int kColonOffset = 20;
constexpr int kBarTop = 28;
constexpr int kBarMargin = 2;

constexpr int kCupWidth = 17;
constexpr int kCupHeight = 21;
constexpr int kSteamRows = 4;

/// Coffee cup, bit 0 leftmost. Drawn to fill the same box as the tomato, so the two
/// screens weigh the same:
///   ....#......#.....
///   .....#....#......
///   ....#......#.....
///   .....#....#......
///   .................
///   ##############...
///   #............#...
///   #............#...
///   #............####
///   #............#..#
///   #............#..#
///   #............#..#
///   #............#..#
///   #............####
///   #............#...
///   #............#...
///   #............#...
///   #............#...
///   #............#...
///   .#..........#....
///   ..##########.....
constexpr std::array<std::uint32_t, kCupHeight> kCupRows = {
    0x00810, 0x00420, 0x00810, 0x00420, 0x00000, 0x03FFF, 0x02001,
    0x02001, 0x1E001, 0x12001, 0x12001, 0x12001, 0x12001, 0x1E001,
    0x02001, 0x02001, 0x02001, 0x02001, 0x02001, 0x01002, 0x00FFC,
};

// The cup fills the tomato's box exactly; these guards fail the build if the tomato
// is ever resized without redrawing it.
constexpr int kTomatoTop = kLeafTop + 2;
constexpr int kTomatoBottom = kTomatoCenterY + kTomatoRadiusY;
constexpr int kCupLeft = kTomatoCenterX - kTomatoRadiusX;
constexpr int kCupTop = kTomatoTop;
static_assert(kCupWidth == 2 * kTomatoRadiusX + 1);
static_assert(kCupHeight == kTomatoBottom - kTomatoTop + 1);

constexpr float kPauseBlinkPeriod = 1.0F;
constexpr float kAlarmBlinkPeriod = 0.4F;
constexpr float kAlarmFlashSeconds = 30.0F;

constexpr int kMaxDisplayMinutes = 99;

std::array<char, 8> formatDigits(float seconds)
{
    // Clamped here so the buffer size is provably enough, not enough by accident.
    const int total =
        std::clamp(static_cast<int>(std::ceil(seconds)), 0, kMaxDisplayMinutes * 60 + 59);

    std::array<char, 8> text{};
    std::snprintf(text.data(), text.size(), "%02d%02d", total / 60, total % 60);
    return text;
}

/// Places the four digits and the colon individually. Next to the tomato only 44
/// pixels remain, which the font's own advance and colon glyph do not fit into.
void drawTime(Surface &surface, int x, int y, float seconds, Color digits, Color colon,
              bool showColon)
{
    const std::array<char, 8> text = formatDigits(seconds);
    const std::string_view view{text.data()};

    const std::array<int, 4> offsets = {0, kDigitAdvance, kPairOffset, kPairOffset + kDigitAdvance};
    for (std::size_t i = 0; i < offsets.size(); ++i)
    {
        drawText(surface, x + offsets[i], y, view.substr(i, 1), digits, kDigitScale);
    }

    if (!showColon)
    {
        return;
    }

    for (const int dot : {4, 9})
    {
        for (int dy = 0; dy < kDigitScale; ++dy)
        {
            for (int dx = 0; dx < kColonWidth; ++dx)
            {
                surface.setPixel(x + kColonOffset + dx, y + dot + dy, colon);
            }
        }
    }
}

/// The spent part stays visible in a very dim shade, so the whole span reads as a
/// scale rather than as a bar that vanishes.
void drawBar(Surface &surface, int x, int y, int width, float fraction, Color color)
{
    const int filled =
        static_cast<int>(std::clamp(fraction, 0.0F, 1.0F) * static_cast<float>(width));
    const Color spent = scaled(color, kSpentPercent);

    for (int offset = 0; offset < width; ++offset)
    {
        for (int row = 0; row < kBarHeight; ++row)
        {
            surface.setPixel(x + offset, y + row, offset < filled ? color : spent);
        }
    }
}

/// Empties from the top as the fraction falls. The level spans the leaves too, so
/// they drain with the fruit instead of staying bright.
void drawTomato(Surface &surface, float fraction)
{
    const int drain_top = kLeafTop;
    const int drain_bottom = kTomatoCenterY + kTomatoRadiusY;
    const float level =
        static_cast<float>(drain_top) + (1.0F - std::clamp(fraction, 0.0F, 1.0F)) *
                                            static_cast<float>(drain_bottom - drain_top + 1);

    const auto shade = [level](int y, Color bright)
    { return static_cast<float>(y) >= level ? bright : scaled(bright, kDrainedPercent); };

    const int body_top = kTomatoCenterY - kTomatoRadiusY;
    for (int y = body_top; y <= drain_bottom; ++y)
    {
        for (int x = kTomatoCenterX - kTomatoRadiusX; x <= kTomatoCenterX + kTomatoRadiusX; ++x)
        {
            const float dx =
                static_cast<float>(x - kTomatoCenterX) / static_cast<float>(kTomatoRadiusX);
            const float dy =
                static_cast<float>(y - kTomatoCenterY) / static_cast<float>(kTomatoRadiusY);
            if (dx * dx + dy * dy <= 1.0F)
            {
                surface.setPixel(x, y, shade(y, kFocusAccent));
            }
        }
    }

    // The ellipse leaves a single pixel on its top row, which reads as a stray dot.
    surface.setPixel(kTomatoCenterX - 1, body_top, shade(body_top, kFocusAccent));
    surface.setPixel(kTomatoCenterX + 1, body_top, shade(body_top, kFocusAccent));

    surface.setPixel(kTomatoCenterX, kLeafTop + 2, shade(kLeafTop + 2, kLeafColor));
    surface.setPixel(kTomatoCenterX, kLeafTop + 3, shade(kLeafTop + 3, kLeafColor));
    for (int i = 1; i <= 3; ++i)
    {
        const int y = kLeafTop + 3 - (i / 3);
        surface.setPixel(kTomatoCenterX - i, y, shade(y, kLeafColor));
        surface.setPixel(kTomatoCenterX + i, y, shade(y, kLeafColor));
    }
}

void drawCup(Surface &surface, int x, int y, Color color)
{
    const Color steam = scaled(color, kSteamPercent);

    for (int row = 0; row < kCupHeight; ++row)
    {
        const Color shade = row < kSteamRows ? steam : color;
        for (int column = 0; column < kCupWidth; ++column)
        {
            if ((kCupRows[static_cast<std::size_t>(row)] & (1U << column)) != 0)
            {
                surface.setPixel(x + column, y + row, shade);
            }
        }
    }
}

} // namespace

void PomodoroApp::onInput(const InputEvent &event)
{
    switch (event.type)
    {
    case InputType::Rotate:
        if (state_ == State::Setting)
        {
            int &minutes = mode_ == Mode::Focus ? focus_minutes_ : break_minutes_;
            minutes = std::clamp(minutes + event.delta, kMinMinutes, kMaxMinutes);
        }
        break;

    case InputType::Press:
        onPress();
        break;

    case InputType::LongPress:
        reset();
        logInfo("pomodoro reset");
        break;

    default:
        break;
    }
}

void PomodoroApp::onPress()
{
    switch (state_)
    {
    case State::Setting:
        if (mode_ == Mode::Focus)
        {
            mode_ = Mode::Break;
            logInfo("pomodoro set break {} min", break_minutes_);
        }
        else
        {
            startTimer(Mode::Focus);
        }
        break;

    case State::Running:
        state_ = State::Paused;
        logInfo("pomodoro paused");
        break;

    case State::Paused:
        state_ = State::Running;
        logInfo("pomodoro resumed");
        break;

    case State::Alarm:
        startTimer(mode_ == Mode::Focus ? Mode::Break : Mode::Focus);
        break;
    }
}

void PomodoroApp::update(Duration dt)
{
    blink_ += dt.count();

    switch (state_)
    {
    case State::Running:
        remaining_ -= dt.count();
        if (remaining_ <= 0.0F)
        {
            remaining_ = 0.0F;
            state_ = State::Alarm;
            alarm_elapsed_ = 0.0F;
            logInfo("pomodoro {} finished", mode_ == Mode::Focus ? "focus" : "break");
        }
        break;

    case State::Alarm:
        alarm_elapsed_ += dt.count();
        break;

    case State::Setting:
    case State::Paused:
        break;
    }
}

void PomodoroApp::render(Surface &surface)
{
    const bool focus = mode_ == Mode::Focus;
    const Color accent = focus ? kFocusAccent : kBreakAccent;
    const Color digits = focus ? kFocusDigits : kBreakDigits;

    const bool alarming = state_ == State::Alarm && alarm_elapsed_ < kAlarmFlashSeconds;
    if (alarming && std::fmod(blink_, kAlarmBlinkPeriod) < kAlarmBlinkPeriod / 2.0F)
    {
        surface.clear(accent);
    }

    const bool blink_off =
        state_ == State::Paused && std::fmod(blink_, kPauseBlinkPeriod) < kPauseBlinkPeriod / 2.0F;

    const float total = static_cast<float>(currentMinutes()) * 60.0F;
    const float shown = state_ == State::Setting ? total : remaining_;
    const float progress =
        state_ == State::Setting ? 1.0F : (total > 0.0F ? remaining_ / total : 0.0F);

    if (focus)
    {
        drawTomato(surface, progress);
    }
    else
    {
        drawCup(surface, kCupLeft, kCupTop, accent);
    }

    drawText(surface, kColumnLeft, kLabelTop, focus ? "FOCUS" : "BREAK", accent);

    // Only a running countdown changes its digits, so only there does the colon mark
    // the second: on for the first half of each one, off for the second.
    const bool colon_on = state_ != State::Running || std::fmod(shown, 1.0F) >= 0.5F;

    if (!blink_off)
    {
        drawTime(surface, kColumnLeft, kDigitsTop, shown, digits, accent, colon_on);
    }

    drawBar(surface, kBarMargin, kBarTop, surface.width() - 2 * kBarMargin, progress, accent);
}

void PomodoroApp::startTimer(Mode mode)
{
    mode_ = mode;
    state_ = State::Running;
    remaining_ = static_cast<float>(currentMinutes()) * 60.0F;
    alarm_elapsed_ = 0.0F;
    logInfo("pomodoro {} {} min", mode == Mode::Focus ? "focus" : "break", currentMinutes());
}

void PomodoroApp::reset()
{
    mode_ = Mode::Focus;
    state_ = State::Setting;
    remaining_ = 0.0F;
    alarm_elapsed_ = 0.0F;
}

int PomodoroApp::currentMinutes() const
{
    return mode_ == Mode::Focus ? focus_minutes_ : break_minutes_;
}

} // namespace matrixos
