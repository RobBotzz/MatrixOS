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

constexpr Color kFocusAccent{0xFF, 0x43, 0x26};
constexpr Color kFocusDigits{255, 255, 255};
constexpr Color kFocusPulpDim{56, 15, 8};
constexpr Color kLeafColor{40, 180, 70};

constexpr Color kBreakAccent{0x22, 0xE6, 0xA4};
constexpr Color kBreakDigits{170, 255, 230};

constexpr int kDigitScale = 2;
constexpr int kLabelTop = 0;
constexpr int kDigitsTop = 9;

constexpr int kTomatoCenterX = 8;
constexpr int kTomatoCenterY = 18;
constexpr int kTomatoRadiusX = 8;
constexpr int kTomatoRadiusY = 9;
constexpr int kLeafTop = 5;

constexpr int kFocusColumnLeft = 20;
constexpr int kFocusDigitAdvance = 11;
constexpr int kFocusBarTop = 26;

constexpr int kBreakLabelLeft = 2;
constexpr int kBreakBarTop = 28;

constexpr int kBarHeight = 2;

constexpr float kPauseBlinkPeriod = 1.0F;
constexpr float kAlarmBlinkPeriod = 0.4F;
constexpr float kAlarmFlashSeconds = 30.0F;

constexpr int kMaxDisplayMinutes = 99;

std::array<char, 8> formatTime(float seconds, bool withColon)
{
    // Clamped here so the buffer size is provably enough, not enough by accident.
    const int total =
        std::clamp(static_cast<int>(std::ceil(seconds)), 0, kMaxDisplayMinutes * 60 + 59);

    std::array<char, 8> text{};
    std::snprintf(text.data(), text.size(), withColon ? "%02d:%02d" : "%02d%02d", total / 60,
                  total % 60);
    return text;
}

/// Draws each glyph individually so the spacing can be tightened; the four digits
/// would not otherwise fit beside the tomato.
void drawSpacedText(Surface &surface, int x, int y, std::string_view text, Color color, int scale,
                    int advance)
{
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        drawText(surface, x + static_cast<int>(i) * advance, y, text.substr(i, 1), color, scale);
    }
}

void drawBar(Surface &surface, int x, int y, int width, float fraction, Color color)
{
    const int filled =
        static_cast<int>(std::clamp(fraction, 0.0F, 1.0F) * static_cast<float>(width));
    for (int offset = 0; offset < filled; ++offset)
    {
        for (int row = 0; row < kBarHeight; ++row)
        {
            surface.setPixel(x + offset, y + row, color);
        }
    }
}

/// Empties from the top as the fraction falls: pulp below the level, husk above.
void drawTomato(Surface &surface, float fraction, Color pulp, Color husk)
{
    const int top = kTomatoCenterY - kTomatoRadiusY;
    const int height = kTomatoRadiusY * 2 + 1;
    const float level = static_cast<float>(top) +
                        (1.0F - std::clamp(fraction, 0.0F, 1.0F)) * static_cast<float>(height);

    for (int y = top; y <= kTomatoCenterY + kTomatoRadiusY; ++y)
    {
        for (int x = kTomatoCenterX - kTomatoRadiusX; x <= kTomatoCenterX + kTomatoRadiusX; ++x)
        {
            const float dx =
                static_cast<float>(x - kTomatoCenterX) / static_cast<float>(kTomatoRadiusX);
            const float dy =
                static_cast<float>(y - kTomatoCenterY) / static_cast<float>(kTomatoRadiusY);
            if (dx * dx + dy * dy > 1.0F)
            {
                continue;
            }
            surface.setPixel(x, y, static_cast<float>(y) >= level ? pulp : husk);
        }
    }

    surface.setPixel(kTomatoCenterX, kLeafTop + 2, kLeafColor);
    surface.setPixel(kTomatoCenterX, kLeafTop + 3, kLeafColor);
    for (int i = 1; i <= 3; ++i)
    {
        surface.setPixel(kTomatoCenterX - i, kLeafTop + 3 - (i / 3), kLeafColor);
        surface.setPixel(kTomatoCenterX + i, kLeafTop + 3 - (i / 3), kLeafColor);
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
    const float fraction = total > 0.0F ? remaining_ / total : 0.0F;

    if (focus)
    {
        drawTomato(surface, state_ == State::Setting ? 1.0F : fraction, kFocusAccent,
                   kFocusPulpDim);
        drawText(surface, kFocusColumnLeft, kLabelTop, "FOCUS", accent);

        if (!blink_off)
        {
            const std::array<char, 8> text = formatTime(shown, false);
            drawSpacedText(surface, kFocusColumnLeft, kDigitsTop, std::string_view{text.data()},
                           digits, kDigitScale, kFocusDigitAdvance);
        }

        if (state_ != State::Setting)
        {
            drawBar(surface, kFocusColumnLeft, kFocusBarTop, surface.width() - kFocusColumnLeft,
                    fraction, accent);
        }
        return;
    }

    drawText(surface, kBreakLabelLeft, kLabelTop, "BREAK", accent);

    if (!blink_off)
    {
        const std::array<char, 8> text = formatTime(shown, true);
        const std::string_view view{text.data()};
        const int x = (surface.width() - textWidth(view, kDigitScale)) / 2;
        drawText(surface, x, kDigitsTop, view, digits, kDigitScale);
    }

    if (state_ != State::Setting)
    {
        drawBar(surface, 0, kBreakBarTop, surface.width(), fraction, accent);
    }
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
