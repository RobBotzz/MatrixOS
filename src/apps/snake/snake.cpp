// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/snake/snake.h"

#include "gfx/font.h"
#include "gfx/surface.h"
#include "os/log.h"
#include "os/state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace matrixos
{
namespace
{

constexpr std::string_view kSection = "snake";
constexpr std::string_view kHighScoreKey = "highscore";

constexpr int kCell = 2;
constexpr int kBorder = 1;
constexpr int kFieldWidth = SnakeApp::kColumns * kCell + 2 * kBorder;
constexpr int kFieldHeight = SnakeApp::kRows * kCell + 2 * kBorder;
static_assert(kFieldWidth == 64);
static_assert(kFieldHeight == 32);

constexpr Color kBorderColor{26, 26, 44};
constexpr Color kBody{0x22, 0xC5, 0x5E};
constexpr Color kHead{0xA0, 0xFF, 0xC0};
constexpr Color kFood{0xFF, 0x43, 0x26};
constexpr Color kTitle{0x22, 0xC5, 0x5E};
constexpr Color kLabel{140, 140, 160};
constexpr Color kScore{255, 255, 255};
constexpr Color kRecord{255, 190, 0};

constexpr float kBlinkPeriod = 0.7F;

constexpr int kMaxStepsPerFrame = 4; // caps catch-up after a stall

void drawCentered(Surface &surface, int y, std::string_view text, Color color, int scale = 1)
{
    drawText(surface, (surface.width() - textWidth(text, scale)) / 2, y, text, color, scale);
}

void drawFrame(Surface &surface)
{
    for (int x = 0; x < surface.width(); ++x)
    {
        surface.setPixel(x, 0, kBorderColor);
        surface.setPixel(x, surface.height() - 1, kBorderColor);
    }
    for (int y = 0; y < surface.height(); ++y)
    {
        surface.setPixel(0, y, kBorderColor);
        surface.setPixel(surface.width() - 1, y, kBorderColor);
    }
}

void drawCell(Surface &surface, SnakeApp::Cell cell, Color color)
{
    for (int dy = 0; dy < kCell; ++dy)
    {
        for (int dx = 0; dx < kCell; ++dx)
        {
            surface.setPixel(kBorder + cell.x * kCell + dx, kBorder + cell.y * kCell + dy, color);
        }
    }
}

SnakeApp::Cell moved(SnakeApp::Cell cell, SnakeApp::Direction direction)
{
    switch (direction)
    {
    case SnakeApp::Direction::Up:
        return {cell.x, cell.y - 1};
    case SnakeApp::Direction::Right:
        return {cell.x + 1, cell.y};
    case SnakeApp::Direction::Down:
        return {cell.x, cell.y + 1};
    case SnakeApp::Direction::Left:
        return {cell.x - 1, cell.y};
    }
    return cell;
}

bool inside(SnakeApp::Cell cell)
{
    return cell.x >= 0 && cell.x < SnakeApp::kColumns && cell.y >= 0 && cell.y < SnakeApp::kRows;
}

} // namespace

SnakeApp::SnakeApp(StateStore &store, std::uint32_t seed)
    : scores_(store.section(kSection)), random_(seed)
{
}

void SnakeApp::onEnter()
{
    high_score_ = std::max(0, scores_.getInt(kHighScoreKey, 0));
}

void SnakeApp::onExit()
{
    if (state_ == State::Running)
    {
        state_ = State::Paused;
    }
}

void SnakeApp::onInput(const InputEvent &event)
{
    switch (event.type)
    {
    case InputType::Rotate:
        if (state_ == State::Running)
        {
            // Queued, not applied immediately: one turn per step (FR-9).
            const int step = event.delta > 0 ? 1 : -1;
            for (int i = 0; i < std::abs(event.delta); ++i)
            {
                if (turns_.size() >= kMaxQueuedTurns)
                {
                    break;
                }
                turns_.push_back(step);
            }
        }
        break;

    case InputType::Press:
        switch (state_)
        {
        case State::Idle:
        case State::Dead:
            startGame();
            break;
        case State::Running:
            state_ = State::Paused;
            break;
        case State::Paused:
            state_ = State::Running;
            break;
        }
        break;

    case InputType::LongPress:
        state_ = State::Idle;
        break;

    default:
        break;
    }
}

void SnakeApp::update(Duration dt)
{
    blink_ += dt.count();

    if (state_ != State::Running)
    {
        return;
    }

    step_timer_ += dt.count();

    for (int taken = 0; taken < kMaxStepsPerFrame && state_ == State::Running; ++taken)
    {
        const float interval = stepInterval();
        if (step_timer_ < interval)
        {
            return;
        }
        step_timer_ -= interval;
        step();
    }

    step_timer_ = 0.0F;
}

float SnakeApp::stepInterval() const
{
    const float speed =
        std::min(kMaxSpeed, kStartSpeed + static_cast<float>(score()) * kSpeedPerFood);
    return 1.0F / speed;
}

void SnakeApp::startGame()
{
    snake_.clear();
    turns_.clear();

    const int y = kRows / 2;
    for (int i = 0; i < kStartLength; ++i)
    {
        snake_.push_back({kColumns / 2 - i, y});
    }

    direction_ = Direction::Right;
    state_ = State::Running;
    step_timer_ = 0.0F;
    score_ = 0;
    beat_high_score_ = false;
    placeFood();

    logInfo("snake started, high score {}", high_score_);
}

void SnakeApp::turn(int detents)
{
    // Direction is a clockwise ring: one detent is one quarter turn.
    const int ring = (static_cast<int>(direction_) + detents) % 4;
    direction_ = static_cast<Direction>((ring + 4) % 4);
}

void SnakeApp::step()
{
    if (!turns_.empty())
    {
        turn(turns_.front());
        turns_.pop_front();
    }

    const Cell next = moved(snake_.front(), direction_);
    if (!inside(next))
    {
        die();
        return;
    }

    const bool grows = next.x == food_.x && next.y == food_.y;
    if (!grows)
    {
        snake_.pop_back(); // vacated before the collision check below
    }

    if (occupied(next))
    {
        die();
        return;
    }

    snake_.push_front(next);

    if (grows)
    {
        ++score_;
        placeFood();
        recordScore();
    }
}

void SnakeApp::die()
{
    state_ = State::Dead;
    logInfo("snake over, score {}", score());
    recordScore(); // usually a no-op; catches the board-is-full case
}

bool SnakeApp::occupied(Cell cell) const
{
    return std::any_of(snake_.begin(), snake_.end(),
                       [cell](const Cell &part) { return part.x == cell.x && part.y == cell.y; });
}

void SnakeApp::placeFood()
{
    std::vector<Cell> free;
    free.reserve(static_cast<std::size_t>(kColumns * kRows));

    for (int y = 0; y < kRows; ++y)
    {
        for (int x = 0; x < kColumns; ++x)
        {
            const Cell cell{x, y};
            if (!occupied(cell))
            {
                free.push_back(cell);
            }
        }
    }

    if (free.empty())
    {
        die();
        return;
    }

    std::uniform_int_distribution<std::size_t> pick(0, free.size() - 1);
    food_ = free[pick(random_)];
}

void SnakeApp::recordScore()
{
    if (score() <= high_score_)
    {
        return;
    }

    high_score_ = score();
    beat_high_score_ = true;

    // Written on the beat, not on game end (acceptance criterion 3).
    scores_.setInt(kHighScoreKey, high_score_);
    scores_.save();
}

void SnakeApp::render(Surface &surface)
{
    drawFrame(surface);

    switch (state_)
    {
    case State::Idle:
        drawCentered(surface, 5, "SNAKE", kTitle, 2);
        drawCentered(surface, 22, "HI " + std::to_string(high_score_), kLabel);
        return;

    case State::Dead:
    {
        const bool record_on =
            !beat_high_score_ || std::fmod(blink_, kBlinkPeriod) < kBlinkPeriod / 2.0F;
        if (record_on)
        {
            drawCentered(surface, 3, beat_high_score_ ? "NEW BEST" : "GAME OVER",
                         beat_high_score_ ? kRecord : kLabel);
        }
        drawCentered(surface, 12, std::to_string(score()), kScore, 2);
        drawCentered(surface, 25, "HI " + std::to_string(high_score_), kLabel);
        return;
    }

    case State::Running:
    case State::Paused:
        break;
    }

    drawCell(surface, food_, kFood);

    for (std::size_t i = 0; i < snake_.size(); ++i)
    {
        drawCell(surface, snake_[i], i == 0 ? kHead : kBody);
    }

    if (state_ == State::Paused)
    {
        for (int y = 11; y < 21; ++y)
        {
            for (int x = 1; x < surface.width() - 1; ++x)
            {
                surface.setPixel(x, y, Color::black());
            }
        }
        drawCentered(surface, 13, "PAUSE", kRecord);
    }
}

} // namespace matrixos
