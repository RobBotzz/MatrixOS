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
constexpr int kValueTop = 16;
constexpr int kCursorTop = 25;
constexpr int kDotRow = 31;
constexpr int kDotGap = 4;

constexpr float kBlinkPeriod = 0.8F;

void drawCentered(Surface &surface, int y, std::string_view text, Color color, int scale = 1)
{
    drawTextCentered(surface, y, text, color, scale);
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

/// Wraps in both directions, which is the only sensible behaviour for a list on
/// an endless knob.
std::size_t step(std::size_t index, int detents, std::size_t count)
{
    const int total = static_cast<int>(count);
    const int next = (static_cast<int>(index) + detents) % total;
    return static_cast<std::size_t>((next + total) % total);
}

std::size_t defaultZoneIndex()
{
    for (std::size_t i = 0; i < settings::kTimeZones.size(); ++i)
    {
        if (settings::kTimeZones[i].zone == settings::kDefaultTimeZone)
        {
            return i;
        }
    }
    return 0;
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

    const std::string zone = settings_.getString(settings::kTimeZone, settings::kDefaultTimeZone);

    zone_index_ = 0;
    for (std::size_t i = 0; i < settings::kTimeZones.size(); ++i)
    {
        if (settings::kTimeZones[i].zone == zone)
        {
            zone_index_ = i;
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

std::string_view SettingsApp::timeZone() const
{
    return settings::kTimeZones[zone_index_].zone;
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
            const int next = (static_cast<int>(page_) + event.delta) % static_cast<int>(kPageCount);
            page_ = static_cast<Page>((next + static_cast<int>(kPageCount)) %
                                      static_cast<int>(kPageCount));
        }
        break;

    case InputType::Press:
        editing_ = !editing_;
        if (!editing_)
        {
            logInfo("settings: brightness {}, startup '{}', zone '{}'", brightness_, startup(),
                    timeZone());
        }
        break;

    case InputType::LongPress:
        brightness_ = settings::kDefaultBrightness;
        startup_index_ = 0;
        zone_index_ = defaultZoneIndex();
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
    switch (page_)
    {
    case Page::Brightness:
        brightness_ = std::clamp(brightness_ + detents * settings::kBrightnessStep,
                                 settings::kMinBrightness, settings::kMaxBrightness);
        break;

    case Page::Startup:
        startup_index_ = step(startup_index_, detents, apps_.size() + 1);
        break;

    case Page::TimeZone:
        zone_index_ = step(zone_index_, detents, settings::kTimeZones.size());
        break;
    }

    writeThrough();
}

void SettingsApp::writeThrough()
{
    // Marks the section dirty only; save() happens on exit, not per detent.
    settings_.setInt(settings::kBrightness, brightness_);
    settings_.setString(settings::kStartup, startup());
    settings_.setString(settings::kTimeZone, timeZone());
}

void SettingsApp::update(Duration dt)
{
    blink_ += dt.count();
}

void SettingsApp::render(Surface &surface)
{
    static constexpr std::string_view kTitles[kPageCount] = {"BRIGHTNESS", "START WITH",
                                                             "TIME ZONE"};

    drawCentered(surface, kLabelTop, kTitles[static_cast<std::size_t>(page_)],
                 editing_ ? kIdle : kActive);

    const Color value_color = editing_ ? kAccent : kActive;

    if (page_ == Page::Brightness)
    {
        drawCentered(surface, kBrightnessTop, std::to_string(brightness_), value_color, 2);
        drawBar(surface, kBarTop, brightness_, value_color);
    }
    else
    {
        const std::string_view label = page_ == Page::Startup
                                           ? (startup_index_ == 0 ? kLastAppLabel : startup())
                                           : settings::kTimeZones[zone_index_].label;
        drawCentered(surface, kValueTop, label, value_color);

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

    drawPageDots(surface, kPageCount, static_cast<std::size_t>(page_));
}

} // namespace matrixos
