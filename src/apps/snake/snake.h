// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "apps/scores/score_screens.h"
#include "os/app.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <random>
#include <vector>

namespace matrixos
{

class StateStore;
class StateSection;

/// Snake on a 20x10 grid of 3x3 pixel cells, inside a frame one pixel thick at
/// the top and bottom and two at the sides. The scoreboard comes first. Gold food
/// shows up now and then for a few seconds, every tenth food raises a wall, and a
/// crash crumbles the snake before the score is settled.
class SnakeApp : public App
{
public:
    enum class State
    {
        Idle,
        Running,
        Paused,
        Dying,
        Dead,
        EnterName,
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

    static constexpr int kColumns = 20;
    static constexpr int kRows = 10;
    static constexpr int kStartLength = 3;

    static constexpr float kStartSpeed = 3.0F; // cells per second
    static constexpr float kMaxSpeed = 8.0F;
    static constexpr float kSpeedPerFood = 0.1F;

    static constexpr int kGoldPoints = 5;
    static constexpr int kGoldOneIn = 5; // chance per ordinary food
    static constexpr float kGoldSeconds = 5.0F;

    static constexpr int kFoodPerWall = 10;

    static constexpr float kDyingSeconds = 2.15F;

    static constexpr std::size_t kMaxQueuedTurns = 3;

    SnakeApp(StateStore &store, std::uint32_t seed);

    std::string_view name() const override { return "Snake"; }
    bool interruptible() const override { return false; }

    void onExit() override;
    void onInput(const InputEvent &event) override;
    void update(Duration dt) override;
    void render(Surface &surface) override;

    State state() const { return state_; }
    Direction direction() const { return direction_; }
    int score() const { return score_; }
    int eaten() const { return eaten_; }
    const Scoreboard &scoreboard() const { return scores_.board(); }
    const std::string &entry() const { return scores_.entry(); }
    Cell head() const { return snake_.front(); }
    Cell food() const { return food_; }
    const std::optional<Cell> &gold() const { return gold_; }
    const std::vector<Cell> &walls() const { return walls_; }
    std::size_t length() const { return snake_.size(); }

    /// Seconds between two grid steps at the current length, whatever the food
    /// was worth.
    float stepInterval() const;

private:
    void startGame();
    void step();
    void die();
    bool askName();
    void turn(int detents);
    void eat(bool golden);
    void placeFood();
    void maybePlaceGold();
    void addWall();
    bool wallFits(Cell cell) const;
    std::vector<Cell> freeCells() const;
    bool occupied(Cell cell) const;
    bool isWall(Cell cell) const;
    void bankScore();
    void clearBankedScore();
    void renderCrumble(Surface &surface) const;

    StateSection &saved_;
    ScoreScreens scores_;

    State state_ = State::Idle;
    Direction direction_ = Direction::Right;

    std::deque<Cell> snake_;
    std::deque<int> turns_; // pending detents, one applied per step
    Cell food_{};
    std::optional<Cell> gold_;
    std::vector<Cell> walls_;

    float step_timer_ = 0.0F;
    float blink_ = 0.0F;
    float gold_left_ = 0.0F;
    float dying_ = 0.0F;

    // Counted, not derived from the body: a step that kills pops the tail before
    // the collision check, so the deque is briefly one cell short of the truth.
    int score_ = 0;
    int eaten_ = 0;

    std::mt19937 random_;
};

} // namespace matrixos
