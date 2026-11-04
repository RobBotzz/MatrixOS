// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/life/life.h"

#include "apps/life/patterns.h"
#include "gfx/font.h"
#include "gfx/surface.h"

#include <algorithm>
#include <string>

namespace matrixos
{
namespace
{

/// Seconds per generation, slowest to fastest.
constexpr std::array<float, LifeApp::kSpeeds> kStepSeconds = {0.4F, 0.2F, 0.1F, 0.05F, 0.025F};

constexpr float kDensity = 0.3F;
constexpr int kMinPopulation = 12;
constexpr std::uint8_t kGlowStart = 6;

/// Newborn, then old: a cell cools as it survives.
struct Palette
{
    Color young;
    Color old;
    Color glow;
};

constexpr std::array<Palette, LifeApp::kPalettes> kColourSets = {{
    {{120, 255, 230}, {30, 70, 200}, {40, 10, 60}},
    {{255, 230, 120}, {200, 40, 20}, {50, 10, 0}},
    {{200, 255, 120}, {20, 120, 40}, {0, 40, 10}},
}};

constexpr int kAgeToOld = 24;

constexpr int kMenuEntries = 1 + static_cast<int>(kLifePatterns);
constexpr int kMenuRows = 3;
constexpr int kRowHeight = 8;
constexpr Color kTitle{255, 255, 255};
constexpr Color kChosen{255, 215, 0};
constexpr Color kOther{110, 120, 140};

std::string_view menuName(int entry)
{
    return entry == 0 ? "RANDOM" : lifePatternName(static_cast<std::size_t>(entry - 1));
}

Color mix(Color a, Color b, int step, int steps)
{
    const auto channel = [step, steps](std::uint8_t from, std::uint8_t to)
    { return static_cast<std::uint8_t>(from + (to - from) * step / steps); };
    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b)};
}

int index(int x, int y)
{
    return y * LifeApp::kWidth + x;
}

} // namespace

LifeApp::LifeApp(std::uint32_t seed)
    : random_(seed), age_(kWidth * kHeight, 0), glow_(kWidth * kHeight, 0)
{
}

void LifeApp::onEnter()
{
    screen_ = Screen::Menu;
}

void LifeApp::start()
{
    if (selected_ == 0)
    {
        seed();
    }
    else
    {
        setCells(lifePatternCells(static_cast<std::size_t>(selected_ - 1)));
    }
    since_step_ = 0.0F;
    screen_ = Screen::Running;
}

void LifeApp::select(int delta)
{
    selected_ = ((selected_ + delta) % kMenuEntries + kMenuEntries) % kMenuEntries;
}

void LifeApp::seed()
{
    std::bernoulli_distribution live(kDensity);
    for (std::size_t i = 0; i < age_.size(); ++i)
    {
        age_[i] = live(random_) ? 1 : 0;
        glow_[i] = 0;
    }
    generation_ = 0;
    settled_for_ = 0.0F;
    history_ = {};
}

void LifeApp::setCells(const std::vector<std::pair<int, int>> &cells)
{
    std::fill(age_.begin(), age_.end(), 0);
    std::fill(glow_.begin(), glow_.end(), 0);
    for (const auto &[x, y] : cells)
    {
        age_[static_cast<std::size_t>(index(x, y))] = 1;
    }
    generation_ = 0;
    settled_for_ = 0.0F;
    history_ = {};
}

bool LifeApp::alive(int x, int y) const
{
    const int wx = (x % kWidth + kWidth) % kWidth;
    const int wy = (y % kHeight + kHeight) % kHeight;
    return age_[static_cast<std::size_t>(index(wx, wy))] > 0;
}

int LifeApp::population() const
{
    return static_cast<int>(std::count_if(age_.begin(), age_.end(), [](auto a) { return a > 0; }));
}

int LifeApp::neighbours(int x, int y) const
{
    int count = 0;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            if ((dx != 0 || dy != 0) && alive(x + dx, y + dy))
            {
                ++count;
            }
        }
    }
    return count;
}

std::uint64_t LifeApp::fingerprint() const
{
    // FNV-1a over the living cells: enough to see a field that repeats itself.
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto a : age_)
    {
        hash = (hash ^ (a > 0 ? 1U : 0U)) * 1099511628211ULL;
    }
    return hash;
}

void LifeApp::step()
{
    std::vector<std::uint16_t> next(age_.size(), 0);
    for (int y = 0; y < kHeight; ++y)
    {
        for (int x = 0; x < kWidth; ++x)
        {
            const auto i = static_cast<std::size_t>(index(x, y));
            const int n = neighbours(x, y);
            if (age_[i] > 0 && (n == 2 || n == 3))
            {
                next[i] = static_cast<std::uint16_t>(std::min<int>(age_[i] + 1, 0xffff));
            }
            else if (age_[i] == 0 && n == 3)
            {
                next[i] = 1;
            }

            if (age_[i] > 0 && next[i] == 0)
            {
                glow_[i] = kGlowStart;
            }
            else if (glow_[i] > 0)
            {
                --glow_[i];
            }
        }
    }
    age_ = std::move(next);
    ++generation_;
}

void LifeApp::onInput(const InputEvent &event)
{
    switch (event.type)
    {
    case InputType::Rotate:
        if (screen_ == Screen::Menu)
        {
            select(event.delta);
        }
        else
        {
            speed_ = std::clamp(speed_ + event.delta, 0, kSpeeds - 1);
        }
        break;
    case InputType::Press:
        if (screen_ == Screen::Menu)
        {
            start();
        }
        else
        {
            screen_ = Screen::Menu;
        }
        break;
    case InputType::LongPress:
        palette_ = (palette_ + 1) % kPalettes;
        break;
    default:
        break;
    }
}

void LifeApp::update(Duration dt)
{
    if (screen_ == Screen::Menu)
    {
        return;
    }

    since_step_ += dt.count();
    const float interval = kStepSeconds[static_cast<std::size_t>(speed_)];
    while (since_step_ >= interval)
    {
        since_step_ -= interval;
        step();
        if (selected_ != 0)
        {
            continue;
        }

        // A still life repeats the last fingerprint, a blinker the one before.
        const std::uint64_t now = fingerprint();
        const bool repeating = now == history_[0] || now == history_[1];
        history_ = {now, history_[0]};

        settled_for_ = repeating || population() < kMinPopulation ? settled_for_ + interval : 0.0F;
        if (settled_for_ >= kSettledSeconds || generation_ >= kMaxGenerations)
        {
            seed();
        }
    }
}

void LifeApp::renderMenu(Surface &surface) const
{
    drawTextCentered(surface, 1, "LIFE", kTitle);

    const int first = std::clamp(selected_ - 1, 0, kMenuEntries - kMenuRows);
    for (int row = 0; row < kMenuRows; ++row)
    {
        const int entry = first + row;
        drawTextCentered(surface, kRowHeight * (row + 1) + 1, menuName(entry),
                         entry == selected_ ? kChosen : kOther);
    }
}

void LifeApp::render(Surface &surface)
{
    if (screen_ == Screen::Menu)
    {
        renderMenu(surface);
        return;
    }

    const Palette &colours = kColourSets[static_cast<std::size_t>(palette_)];
    for (int y = 0; y < kHeight; ++y)
    {
        for (int x = 0; x < kWidth; ++x)
        {
            const auto i = static_cast<std::size_t>(index(x, y));
            if (age_[i] > 0)
            {
                const int step = std::min<int>(age_[i] - 1, kAgeToOld);
                surface.setPixel(x, y, mix(colours.young, colours.old, step, kAgeToOld));
            }
            else if (glow_[i] > 0)
            {
                surface.setPixel(x, y, mix(Color::black(), colours.glow, glow_[i], kGlowStart));
            }
        }
    }
}

} // namespace matrixos
