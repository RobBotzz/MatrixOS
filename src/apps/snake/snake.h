// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/app.h"

#include <cstdint>
#include <deque>
#include <random>

namespace matrixos
{

class StateStore;
class StateSection;

/// Snake on a 31x15 grid of 2x2 pixel cells, inside a one-pixel frame.
class SnakeApp : public App
{
public:
    enum class State
    {
        Idle,
        Running,
        Paused,
        Dead,
    };

    enum class Direction
    {
        Up,
        Right,
        Down,
        Left,
    };

    struct Cell
    {
        int x = 0;
        int y = 0;
    };

    static constexpr int kColumns = 31;
    static constexpr int kRows = 15;
    static constexpr int kStartLength = 3;

    static constexpr float kStartSpeed = 5.0F; // cells per second
    static constexpr float kMaxSpeed = 14.0F;
    static constexpr float kSpeedPerFood = 0.35F;

    static constexpr std::size_t kMaxQueuedTurns = 3;

    SnakeApp(StateStore &store, std::uint32_t seed);

    std::string_view name() const override { return "Snake"; }

    void onEnter() override;
    void onExit() override;
    void onInput(const InputEvent &event) override;
    void update(Duration dt) override;
    void render(Surface &surface) override;

    State state() const { return state_; }
    Direction direction() const { return direction_; }
    int score() const { return score_; }
    int highScore() const { return high_score_; }
    Cell head() const { return snake_.front(); }
    Cell food() const { return food_; }
    std::size_t length() const { return snake_.size(); }

    /// Seconds between two grid steps at the current length.
    float stepInterval() const;

private:
    void startGame();
    void step();
    void die();
    void turn(int detents);
    void placeFood();
    bool occupied(Cell cell) const;
    void recordScore();

    StateSection &scores_;

    State state_ = State::Idle;
    Direction direction_ = Direction::Right;

    std::deque<Cell> snake_;
    std::deque<int> turns_; // pending detents, one applied per step
    Cell food_{};

    float step_timer_ = 0.0F;
    float blink_ = 0.0F;

    // Counted, not derived from the body: a step that kills pops the tail before
    // the collision check, so the deque is briefly one cell short of the truth.
    int score_ = 0;
    int high_score_ = 0;
    bool beat_high_score_ = false;

    std::mt19937 random_;
};

} // namespace matrixos
