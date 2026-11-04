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
#include <utility>
#include <vector>

namespace matrixos
{
namespace
{

constexpr std::string_view kSection = "snake";
// The score of a run still going. A pulled plug must not cost a run that made the
// board, so it is written as it grows and picked up at the next start.
constexpr std::string_view kBankedKey = "banked";

constexpr int kCell = 3;
constexpr int kBorderX = 2;
constexpr int kBorderY = 1;
constexpr int kFieldWidth = SnakeApp::kColumns * kCell + 2 * kBorderX;
constexpr int kFieldHeight = SnakeApp::kRows * kCell + 2 * kBorderY;
static_assert(kFieldWidth == 64);
static_assert(kFieldHeight == 32);

constexpr Color kBorderColor{26, 26, 44};
constexpr Color kBody{0x22, 0xC5, 0x5E};
constexpr Color kHead{0xA0, 0xFF, 0xC0};
constexpr Color kFood{0xFF, 0x43, 0x26};
constexpr Color kGold{255, 215, 0};
constexpr Color kWall{95, 105, 135};
constexpr Color kLabel{140, 140, 160};
constexpr Color kScore{255, 255, 255};
constexpr Color kRecord{255, 190, 0};

constexpr float kGoldWarnSeconds = 1.5F; // blinks for its last moments
constexpr float kGoldBlinkPeriod = 0.2F;

constexpr int kWallClearance = 5; // cells from the head, so a wall never lands in its face

constexpr float kCrashSeconds = 0.3F;   // everything holds still, the head turns red
constexpr float kCrumbleSeconds = 1.0F; // head to tail
constexpr float kLoosenSeconds = 0.25F; // the pixels of one cell let go one by one
constexpr float kFallSeconds = 0.6F;
constexpr float kGravity = 60.0F; // pixels per second squared
static_assert(kCrashSeconds + kCrumbleSeconds + kLoosenSeconds + kFallSeconds <=
              SnakeApp::kDyingSeconds);

constexpr int kMaxStepsPerFrame = 4; // caps catch-up after a stall

Color dimmed(Color color, float amount)
{
    const auto channel = [amount](std::uint8_t value)
    { return static_cast<std::uint8_t>(static_cast<float>(value) * amount); };
    return {channel(color.r), channel(color.g), channel(color.b)};
}

/// 0 to 1, always the same for one pixel.
float scatter(int x, int y)
{
    auto hash =
        static_cast<std::uint32_t>(x) * 73856093U ^ static_cast<std::uint32_t>(y) * 19349663U;
    hash ^= hash >> 13;
    hash *= 2654435761U;
    hash ^= hash >> 16;
    return static_cast<float>(hash & 0xFFFFU) / 65535.0F;
}

void drawCentered(Surface &surface, int y, std::string_view text, Color color, int scale = 1)
{
    drawTextCentered(surface, y, text, color, scale);
}

void drawFrame(Surface &surface)
{
    for (int x = 0; x < surface.width(); ++x)
    {
        for (int i = 0; i < kBorderY; ++i)
        {
            surface.setPixel(x, i, kBorderColor);
            surface.setPixel(x, surface.height() - 1 - i, kBorderColor);
        }
    }
    for (int y = 0; y < surface.height(); ++y)
    {
        for (int i = 0; i < kBorderX; ++i)
        {
            surface.setPixel(i, y, kBorderColor);
            surface.setPixel(surface.width() - 1 - i, y, kBorderColor);
        }
    }
}

void drawCell(Surface &surface, SnakeApp::Cell cell, Color color)
{
    for (int dy = 0; dy < kCell; ++dy)
    {
        for (int dx = 0; dx < kCell; ++dx)
        {
            surface.setPixel(kBorderX + cell.x * kCell + dx, kBorderY + cell.y * kCell + dy, color);
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

bool same(SnakeApp::Cell a, SnakeApp::Cell b)
{
    return a.x == b.x && a.y == b.y;
}

} // namespace

SnakeApp::SnakeApp(StateStore &store, std::uint32_t seed)
    : saved_(store.section(kSection)), scores_(saved_), random_(seed)
{
    const int banked = saved_.getInt(kBankedKey, 0);
    if (scores_.board().qualifies(banked))
    {
        scores_.addUnasked(banked);
    }
    clearBankedScore();
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
    if (state_ == State::EnterName)
    {
        if (scores_.onEntryInput(event))
        {
            clearBankedScore();
            state_ = State::Idle;
        }
        return;
    }

    switch (event.type)
    {
    case InputType::Rotate:
        if (state_ == State::Idle)
        {
            scores_.scroll(event.delta);
        }
        else if (state_ == State::Running)
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
            startGame();
            break;
        case State::Running:
            state_ = State::Paused;
            break;
        case State::Paused:
            state_ = State::Running;
            break;
        case State::Dead:
            scores_.rewind();
            state_ = State::Idle;
            break;
        case State::Dying:
        case State::EnterName:
            break;
        }
        break;

    case InputType::LongPress:
        if (state_ == State::Dying)
        {
            break;
        }
        if ((state_ != State::Running && state_ != State::Paused) || !askName())
        {
            scores_.rewind();
            state_ = State::Idle;
        }
        break;

    default:
        break;
    }
}

void SnakeApp::update(Duration dt)
{
    blink_ += dt.count();

    if (state_ == State::Dying)
    {
        dying_ += dt.count();
        if (dying_ >= kDyingSeconds && !askName())
        {
            state_ = State::Dead;
        }
        return;
    }

    if (state_ != State::Running)
    {
        return;
    }

    gold_left_ -= dt.count();
    if (gold_ && gold_left_ <= 0.0F)
    {
        gold_.reset();
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
        std::min(kMaxSpeed, kStartSpeed + static_cast<float>(eaten_) * kSpeedPerFood);
    return 1.0F / speed;
}

void SnakeApp::startGame()
{
    scores_.rewind();
    snake_.clear();
    turns_.clear();
    walls_.clear();
    gold_.reset();

    const int y = kRows / 2;
    for (int i = 0; i < kStartLength; ++i)
    {
        snake_.push_back({kColumns / 2 - i, y});
    }

    direction_ = Direction::Right;
    state_ = State::Running;
    step_timer_ = 0.0F;
    dying_ = 0.0F;
    score_ = 0;
    eaten_ = 0;
    placeFood();

    logInfo("snake started");
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
    if (!inside(next) || isWall(next))
    {
        die();
        return;
    }

    const bool golden = gold_ && same(next, *gold_);
    const bool grows = golden || same(next, food_);
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
        eat(golden);
    }
}

void SnakeApp::eat(bool golden)
{
    score_ += golden ? kGoldPoints : 1;
    ++eaten_;

    if (golden)
    {
        gold_.reset();
    }
    else
    {
        placeFood();
        maybePlaceGold();
    }

    if (eaten_ % kFoodPerWall == 0)
    {
        addWall();
    }
    bankScore();
}

void SnakeApp::die()
{
    state_ = State::Dying;
    dying_ = 0.0F;
    logInfo("snake over, score {}", score());
}

bool SnakeApp::askName()
{
    if (!scores_.board().qualifies(score_))
    {
        return false;
    }
    scores_.beginEntry(score_);
    state_ = State::EnterName;
    return true;
}

bool SnakeApp::occupied(Cell cell) const
{
    return std::any_of(snake_.begin(), snake_.end(),
                       [cell](const Cell &part) { return same(part, cell); });
}

bool SnakeApp::isWall(Cell cell) const
{
    return std::any_of(walls_.begin(), walls_.end(),
                       [cell](const Cell &part) { return same(part, cell); });
}

std::vector<SnakeApp::Cell> SnakeApp::freeCells() const
{
    std::vector<Cell> free;
    free.reserve(static_cast<std::size_t>(kColumns * kRows));

    for (int y = 0; y < kRows; ++y)
    {
        for (int x = 0; x < kColumns; ++x)
        {
            const Cell cell{x, y};
            if (!occupied(cell) && !isWall(cell) && !same(cell, food_) &&
                !(gold_ && same(cell, *gold_)))
            {
                free.push_back(cell);
            }
        }
    }
    return free;
}

void SnakeApp::placeFood()
{
    const std::vector<Cell> free = freeCells();
    if (free.empty())
    {
        die();
        return;
    }

    std::uniform_int_distribution<std::size_t> pick(0, free.size() - 1);
    food_ = free[pick(random_)];
}

void SnakeApp::maybePlaceGold()
{
    if (gold_ || std::uniform_int_distribution<int>(1, kGoldOneIn)(random_) != 1)
    {
        return;
    }

    const std::vector<Cell> free = freeCells();
    if (!free.empty())
    {
        std::uniform_int_distribution<std::size_t> pick(0, free.size() - 1);
        gold_ = free[pick(random_)];
        gold_left_ = kGoldSeconds;
    }
}

// Walls keep off the outer ring and never touch each other, not even at a corner.
// So every free cell stays reachable, and there are no pockets to get caught in.
bool SnakeApp::wallFits(Cell cell) const
{
    if (cell.x < 1 || cell.x > kColumns - 2 || cell.y < 1 || cell.y > kRows - 2)
    {
        return false;
    }
    if (occupied(cell) || same(cell, food_) || (gold_ && same(cell, *gold_)))
    {
        return false;
    }
    const Cell at = head();
    if (std::abs(cell.x - at.x) + std::abs(cell.y - at.y) < kWallClearance)
    {
        return false;
    }
    return std::none_of(
        walls_.begin(), walls_.end(), [cell](const Cell &wall)
        { return std::abs(wall.x - cell.x) <= 1 && std::abs(wall.y - cell.y) <= 1; });
}

void SnakeApp::addWall()
{
    std::vector<std::pair<Cell, Cell>> spots;
    for (int y = 0; y < kRows; ++y)
    {
        for (int x = 0; x < kColumns; ++x)
        {
            const Cell cell{x, y};
            for (const Cell other : {Cell{x + 1, y}, Cell{x, y + 1}})
            {
                if (wallFits(cell) && wallFits(other))
                {
                    spots.emplace_back(cell, other);
                }
            }
        }
    }

    if (!spots.empty()) // a full field simply stops growing walls
    {
        std::uniform_int_distribution<std::size_t> pick(0, spots.size() - 1);
        const auto &[first, second] = spots[pick(random_)];
        walls_.push_back(first);
        walls_.push_back(second);
    }
}

void SnakeApp::bankScore()
{
    if (scores_.board().qualifies(score_))
    {
        saved_.setInt(kBankedKey, score_);
        saved_.save();
    }
}

void SnakeApp::clearBankedScore()
{
    if (saved_.getInt(kBankedKey, 0) != 0)
    {
        saved_.setInt(kBankedKey, 0);
        saved_.save();
    }
}

void SnakeApp::renderCrumble(Surface &surface) const
{
    const auto length = static_cast<float>(snake_.size());
    for (std::size_t i = 0; i < snake_.size(); ++i)
    {
        const Color color = i == 0 ? kFood : kBody;
        const float start = kCrashSeconds + kCrumbleSeconds * static_cast<float>(i) / length;

        for (int dy = 0; dy < kCell; ++dy)
        {
            for (int dx = 0; dx < kCell; ++dx)
            {
                const int x = kBorderX + snake_[i].x * kCell + dx;
                const int y = kBorderY + snake_[i].y * kCell + dy;
                const float age = dying_ - start - scatter(x, y) * kLoosenSeconds;

                if (age < 0.0F)
                {
                    surface.setPixel(x, y, color);
                }
                else if (age < kFallSeconds)
                {
                    const int fallen = static_cast<int>(kGravity * age * age / 2.0F);
                    surface.setPixel(x, y + fallen, dimmed(color, 1.0F - age / kFallSeconds));
                }
            }
        }
    }
}

void SnakeApp::render(Surface &surface)
{
    switch (state_)
    {
    case State::Idle:
        scores_.renderBoard(surface);
        return;

    case State::EnterName:
        scores_.renderEntry(surface, blink_, "GAME OVER");
        return;

    case State::Dead:
    {
        const auto &entries = scores_.board().entries();
        const int best = entries.empty() ? 0 : entries.front().score;
        drawFrame(surface);
        drawCentered(surface, 3, "GAME OVER", kLabel);
        drawCentered(surface, 12, std::to_string(score()), kScore, 2);
        drawCentered(surface, 25, "HI " + std::to_string(best), kLabel);
        return;
    }

    case State::Running:
    case State::Paused:
    case State::Dying:
        break;
    }

    drawFrame(surface);

    for (const Cell &wall : walls_)
    {
        drawCell(surface, wall, kWall);
    }

    drawCell(surface, food_, kFood);

    if (gold_ && (gold_left_ > kGoldWarnSeconds ||
                  std::fmod(blink_, kGoldBlinkPeriod) < kGoldBlinkPeriod / 2.0F))
    {
        drawCell(surface, *gold_, kGold);
    }

    if (state_ == State::Dying)
    {
        renderCrumble(surface);
        return;
    }

    for (std::size_t i = 0; i < snake_.size(); ++i)
    {
        drawCell(surface, snake_[i], i == 0 ? kHead : kBody);
    }

    if (state_ == State::Paused)
    {
        for (int y = 11; y < 21; ++y)
        {
            for (int x = kBorderX; x < surface.width() - kBorderX; ++x)
            {
                surface.setPixel(x, y, Color::black());
            }
        }
        drawCentered(surface, 13, "PAUSE", kRecord);
    }
}

} // namespace matrixos
