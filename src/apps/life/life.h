// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/app.h"

#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace matrixos
{

/// Conway's Game of Life on the whole panel, wrapping at the edges. It opens on a
/// menu: random fields or one of the known structures. Running, rotate sets the
/// speed, press returns to the menu, a long press changes the palette. A random
/// field that has settled, only blinks or is nearly empty is replaced by itself;
/// the structures run forever.
class LifeApp : public App
{
public:
    enum class Screen
    {
        Menu,
        Running,
    };

    static constexpr int kWidth = 64;
    static constexpr int kHeight = 32;
    static constexpr int kSpeeds = 5;
    static constexpr int kPalettes = 3;

    /// How long a settled or blinking field stays before it is replaced.
    static constexpr float kSettledSeconds = 4.0F;
    static constexpr int kMaxGenerations = 2000;

    explicit LifeApp(std::uint32_t seed);

    std::string_view name() const override { return "Life"; }

    void onEnter() override;
    void onInput(const InputEvent &event) override;
    void update(Duration dt) override;
    void render(Surface &surface) override;

    /// Clears the field and sets living cells; for the tests.
    void setCells(const std::vector<std::pair<int, int>> &cells);

    /// One generation, as `update` would run it.
    void step();

    bool alive(int x, int y) const;
    int population() const;
    Screen screen() const { return screen_; }
    int selected() const { return selected_; } ///< 0 is random, then the structures
    std::int64_t generation() const { return generation_; }
    int speed() const { return speed_; }
    int palette() const { return palette_; }

private:
    void start();
    void seed();
    void select(int delta);
    void renderMenu(Surface &surface) const;
    int neighbours(int x, int y) const;
    std::uint64_t fingerprint() const;

    Screen screen_ = Screen::Menu;
    int selected_ = 0;

    std::mt19937 random_;
    std::vector<std::uint16_t> age_; ///< 0 means dead
    std::vector<std::uint8_t> glow_; ///< afterglow of a cell that just died
    std::array<std::uint64_t, 2> history_{};
    std::int64_t generation_ = 0;
    int speed_ = 2;
    int palette_ = 0;
    float since_step_ = 0.0F;
    float settled_for_ = 0.0F;
};

} // namespace matrixos
