// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/settings/settings_app.h"

#include "gfx/font.h"
#include "gfx/surface.h"
#include "os/log.h"
#include "os/settings.h"
#include "os/state.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace matrixos
{
namespace
{

constexpr std::string_view kLastAppLabel = "Last app";

constexpr Color kActive{255, 255, 255};
constexpr Color kIdle{80, 80, 100};
constexpr Color kAccent{255, 190, 0};
constexpr Color kBarSpent{40, 40, 55};

constexpr int kLabelTop = 3;
constexpr int kBrightnessTop = 11; // scale 2, so it occupies rows 11 to 24
constexpr int kBarTop = 27;
constexpr int kBarHeight = 3;
constexpr int kBarMargin = 4;
constexpr int kStartupTop = 16;
constexpr int kCursorTop = 25;
constexpr int kDotRow = 31;
constexpr int kDotGap = 4;

constexpr float kBlinkPeriod = 0.8F;

void drawCentered(Surface &surface, int y, std::string_view text, Color color, int scale = 1)
{
    drawText(surface, (surface.width() - textWidth(text, scale)) / 2, y, text, color, scale);
}

void drawPageDots(Surface &surface, std::size_t pages, std::size_t current)
{
    const int total = static_cast<int>(pages) * kDotGap - (kDotGap - 1);
    int x = (surface.width() - total) / 2;

    for (std::size_t page = 0; page < pages; ++page)
    {
        surface.setPixel(x, kDotRow, page == current ? kActive : kIdle);
        x += kDotGap;
    }
}

void drawBar(Surface &surface, int y, int percent, Color color)
{
    const int width = surface.width() - 2 * kBarMargin;
    const int filled = std::clamp(percent, 0, 100) * width / 100;

    for (int offset = 0; offset < width; ++offset)
    {
        for (int row = 0; row < kBarHeight; ++row)
        {
            surface.setPixel(kBarMargin + offset, y + row, offset < filled ? color : kBarSpent);
        }
    }
}

} // namespace

SettingsApp::SettingsApp(StateStore &store, std::vector<std::string_view> apps)
    : settings_(store.section(settings::kSection)), apps_(std::move(apps))
{
}

void SettingsApp::onEnter()
{
    brightness_ = std::clamp(settings_.getInt(settings::kBrightness, settings::kDefaultBrightness),
                             settings::kMinBrightness, settings::kMaxBrightness);

    const std::string stored = settings_.getString(settings::kStartup, settings::kStartupLast);

    startup_index_ = 0;
    for (std::size_t i = 0; i < apps_.size(); ++i)
    {
        if (apps_[i] == stored)
        {
            startup_index_ = i + 1;
            break;
        }
    }

    page_ = Page::Brightness;
    editing_ = false;
}

void SettingsApp::onExit()
{
    editing_ = false;
    settings_.save(); // one write per visit, not per detent (ADR-0008)
}

std::string_view SettingsApp::startup() const
{
    return startup_index_ == 0 ? settings::kStartupLast : apps_[startup_index_ - 1];
}

void SettingsApp::onInput(const InputEvent &event)
{
    switch (event.type)
    {
    case InputType::Rotate:
        if (editing_)
        {
            changeValue(event.delta);
        }
        else
        {
            page_ = page_ == Page::Brightness ? Page::Startup : Page::Brightness;
        }
        break;

    case InputType::Press:
        editing_ = !editing_;
        if (!editing_)
        {
            logInfo("settings: brightness {}, startup '{}'", brightness_, startup());
        }
        break;

    case InputType::LongPress:
        brightness_ = settings::kDefaultBrightness;
        startup_index_ = 0;
        editing_ = false;
        writeThrough();
        logInfo("settings reset to defaults");
        break;

    default:
        break;
    }
}

void SettingsApp::changeValue(int detents)
{
    if (page_ == Page::Brightness)
    {
        brightness_ = std::clamp(brightness_ + detents * settings::kBrightnessStep,
                                 settings::kMinBrightness, settings::kMaxBrightness);
    }
    else
    {
        const int count = static_cast<int>(apps_.size()) + 1;
        const int next = (static_cast<int>(startup_index_) + detents) % count;
        startup_index_ = static_cast<std::size_t>((next + count) % count);
    }

    writeThrough();
}

void SettingsApp::writeThrough()
{
    // Marks the section dirty only; save() happens on exit, not per detent.
    settings_.setInt(settings::kBrightness, brightness_);
    settings_.setString(settings::kStartup, startup());
}

void SettingsApp::update(Duration dt)
{
    blink_ += dt.count();
}

void SettingsApp::render(Surface &surface)
{
    const bool page_is_brightness = page_ == Page::Brightness;

    drawCentered(surface, kLabelTop, page_is_brightness ? "BRIGHTNESS" : "START WITH",
                 editing_ ? kIdle : kActive);

    const Color value_color = editing_ ? kAccent : kActive;

    if (page_is_brightness)
    {
        drawCentered(surface, kBrightnessTop, std::to_string(brightness_), value_color, 2);
        drawBar(surface, kBarTop, brightness_, value_color);
    }
    else
    {
        const std::string_view label = startup_index_ == 0 ? kLastAppLabel : startup();
        drawCentered(surface, kStartupTop, label, value_color);

        if (editing_ && std::fmod(blink_, kBlinkPeriod) < kBlinkPeriod / 2.0F)
        {
            const int width = textWidth(label) + 4;
            const int left = (surface.width() - width) / 2;
            for (int x = left; x < left + width; ++x)
            {
                surface.setPixel(x, kCursorTop, kAccent);
            }
        }
    }

    drawPageDots(surface, 2, page_is_brightness ? 0 : 1);
}

} // namespace matrixos
