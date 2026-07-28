// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/snake/snake.h"

#include "gfx/surface.h"
#include "os/state.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

using namespace matrixos;
using matrixos::test::TempDir;

namespace
{

constexpr std::uint32_t kSeed = 12345;

void send(SnakeApp &app, InputType type, int delta = 0)
{
    app.onInput(InputEvent{type, delta});
}

void stepOnce(SnakeApp &app)
{
    app.update(Duration{app.stepInterval()});
}

void steps(SnakeApp &app, int count)
{
    for (int i = 0; i < count; ++i)
    {
        stepOnce(app);
    }
}

void start(SnakeApp &app)
{
    app.onEnter();
    send(app, InputType::Press);
}

/// Turns at most a quarter towards the food and takes one step.
void chaseFood(SnakeApp &app)
{
    const SnakeApp::Cell head = app.head();
    const SnakeApp::Cell food = app.food();

    SnakeApp::Direction want = app.direction();
    if (food.x != head.x)
    {
        want = food.x > head.x ? SnakeApp::Direction::Right : SnakeApp::Direction::Left;
    }
    else if (food.y != head.y)
    {
        want = food.y > head.y ? SnakeApp::Direction::Down : SnakeApp::Direction::Up;
    }

    const int turn = (static_cast<int>(want) - static_cast<int>(app.direction()) + 4) % 4;
    if (turn == 1 || turn == 2)
    {
        send(app, InputType::Rotate, +1);
    }
    else if (turn == 3)
    {
        send(app, InputType::Rotate, -1);
    }

    stepOnce(app);
}

} // namespace

TEST_CASE("snake starts idle and a press begins a game")
{
    StateStore store = StateStore::inMemory();
    SnakeApp app(store, kSeed);
    app.onEnter();

    CHECK(app.state() == SnakeApp::State::Idle);

    send(app, InputType::Press);
    CHECK(app.state() == SnakeApp::State::Running);
    CHECK(app.length() == SnakeApp::kStartLength);
    CHECK(app.score() == 0);
}

TEST_CASE("the snake advances exactly one cell per step")
{
    StateStore store = StateStore::inMemory();
    SnakeApp app(store, kSeed);
    start(app);

    const SnakeApp::Cell before = app.head();
    stepOnce(app);

    CHECK(app.head().x == before.x + 1);
    CHECK(app.head().y == before.y);
    CHECK(app.length() == SnakeApp::kStartLength);
}

TEST_CASE("rotating turns relative to the current heading")
{
    StateStore store = StateStore::inMemory();
    SnakeApp app(store, kSeed);
    start(app);
    REQUIRE(app.direction() == SnakeApp::Direction::Right);

    send(app, InputType::Rotate, +1);
    stepOnce(app);
    CHECK(app.direction() == SnakeApp::Direction::Down);

    send(app, InputType::Rotate, -1);
    stepOnce(app);
    CHECK(app.direction() == SnakeApp::Direction::Right);

    send(app, InputType::Rotate, -1);
    stepOnce(app);
    CHECK(app.direction() == SnakeApp::Direction::Up);
}

TEST_CASE("two detents between steps are two turns, and neither is lost")
{
    StateStore store = StateStore::inMemory();
    SnakeApp app(store, kSeed);
    start(app);

    send(app, InputType::Rotate, +1);
    send(app, InputType::Rotate, +1);

    stepOnce(app);
    CHECK(app.direction() == SnakeApp::Direction::Down);

    stepOnce(app);
    CHECK(app.direction() == SnakeApp::Direction::Left);
    CHECK(app.state() == SnakeApp::State::Running);
}

TEST_CASE("a single detent can never reverse into the neck")
{
    StateStore store = StateStore::inMemory();

    for (const int delta : {+1, -1})
    {
        SnakeApp app(store, kSeed);
        start(app);

        send(app, InputType::Rotate, delta);
        stepOnce(app);
        CHECK(app.state() == SnakeApp::State::Running);
    }
}

TEST_CASE("running into a wall ends the game")
{
    StateStore store = StateStore::inMemory();
    SnakeApp app(store, kSeed);
    start(app);
    steps(app, SnakeApp::kColumns);

    CHECK(app.state() == SnakeApp::State::Dead);
}

TEST_CASE("eating food grows the snake and scores")
{
    StateStore store = StateStore::inMemory();
    SnakeApp app(store, kSeed);
    start(app);

    const SnakeApp::Cell first_food = app.food();

    int guard = 0;
    while (app.score() == 0 && guard++ < 200)
    {
        chaseFood(app);
    }

    REQUIRE(app.state() == SnakeApp::State::Running);
    CHECK(app.score() == 1);
    CHECK(app.length() == SnakeApp::kStartLength + 1);

    const SnakeApp::Cell next_food = app.food();
    CHECK_FALSE((next_food.x == first_food.x && next_food.y == first_food.y));
}

TEST_CASE("the snake gets faster as it grows")
{
    StateStore store = StateStore::inMemory();
    SnakeApp app(store, kSeed);
    start(app);

    const float at_start = app.stepInterval();

    int guard = 0;
    while (app.score() < 3 && guard++ < 600)
    {
        chaseFood(app);
    }

    REQUIRE(app.score() == 3);
    CHECK(app.stepInterval() < at_start);
}

TEST_CASE("a press pauses and resumes, and a hold returns to the start screen")
{
    StateStore store = StateStore::inMemory();
    SnakeApp app(store, kSeed);
    start(app);

    send(app, InputType::Press);
    REQUIRE(app.state() == SnakeApp::State::Paused);

    const SnakeApp::Cell parked = app.head();
    steps(app, 5);
    CHECK(app.head().x == parked.x);

    send(app, InputType::Press);
    CHECK(app.state() == SnakeApp::State::Running);

    send(app, InputType::LongPress);
    CHECK(app.state() == SnakeApp::State::Idle);
}

TEST_CASE("leaving mid-game pauses instead of playing on unattended")
{
    StateStore store = StateStore::inMemory();
    SnakeApp app(store, kSeed);
    start(app);

    app.onExit();
    CHECK(app.state() == SnakeApp::State::Paused);
}

TEST_CASE("the high score is written the moment it is beaten and survives a restart")
{
    TempDir dir;

    {
        StateStore store(dir.path());
        SnakeApp app(store, kSeed);
        start(app);

        int guard = 0;
        while (app.score() < 2 && guard++ < 400)
        {
            chaseFood(app);
        }
        REQUIRE(app.score() == 2);

        for (int i = 0; i < 4 * SnakeApp::kColumns && app.state() == SnakeApp::State::Running; ++i)
        {
            stepOnce(app);
        }
        REQUIRE(app.state() == SnakeApp::State::Dead);
        CHECK(app.highScore() == 2);

        StateStore observer(dir.path()); // nobody called save()
        CHECK(observer.section("snake").getInt("highscore", 0) == 2);
    }

    StateStore reopened(dir.path());
    SnakeApp later(reopened, kSeed);
    later.onEnter();
    CHECK(later.highScore() == 2);
}

TEST_CASE("the record is banked while the game is still running")
{
    TempDir dir;
    StateStore store(dir.path());
    SnakeApp app(store, kSeed);
    start(app);

    int guard = 0;
    while (app.score() == 0 && guard++ < 200)
    {
        chaseFood(app);
    }
    REQUIRE(app.state() == SnakeApp::State::Running);

    StateStore observer(dir.path()); // acceptance criterion 3
    CHECK(observer.section("snake").getInt("highscore", 0) == 1);
}

TEST_CASE("a worse game leaves the high score alone")
{
    TempDir dir;
    StateStore store(dir.path());
    store.section("snake").setInt("highscore", 99);

    SnakeApp app(store, kSeed);
    start(app);
    steps(app, SnakeApp::kColumns);

    REQUIRE(app.state() == SnakeApp::State::Dead);
    CHECK(app.highScore() == 99);
    CHECK(store.section("snake").getInt("highscore", 0) == 99);
}

TEST_CASE("the field is framed and the cells are two pixels square")
{
    StateStore store = StateStore::inMemory();
    SnakeApp app(store, kSeed);
    start(app);

    Surface frame(64, 32);
    app.render(frame);

    for (int x = 0; x < 64; ++x)
    {
        CHECK_FALSE(frame.pixel(x, 0) == Color::black());
        CHECK_FALSE(frame.pixel(x, 31) == Color::black());
    }
    for (int y = 0; y < 32; ++y)
    {
        CHECK_FALSE(frame.pixel(0, y) == Color::black());
        CHECK_FALSE(frame.pixel(63, y) == Color::black());
    }

    const SnakeApp::Cell head = app.head();
    const Color drawn = frame.pixel(1 + head.x * 2, 1 + head.y * 2);
    CHECK(frame.pixel(1 + head.x * 2 + 1, 1 + head.y * 2) == drawn);
    CHECK(frame.pixel(1 + head.x * 2, 1 + head.y * 2 + 1) == drawn);
    CHECK(frame.pixel(1 + head.x * 2 + 1, 1 + head.y * 2 + 1) == drawn);
}

TEST_CASE("the start screen shows the high score")
{
    TempDir dir;
    StateStore store(dir.path());
    store.section("snake").setInt("highscore", 7);

    SnakeApp app(store, kSeed);
    app.onEnter();

    Surface frame(64, 32);
    app.render(frame);

    REQUIRE(app.state() == SnakeApp::State::Idle);

    bool anything_below_the_title = false;
    for (int y = 20; y < 31 && !anything_below_the_title; ++y)
    {
        for (int x = 1; x < 63; ++x)
        {
            if (!(frame.pixel(x, y) == Color::black()))
            {
                anything_below_the_title = true;
                break;
            }
        }
    }
    CHECK(anything_below_the_title);
}

TEST_CASE("dying against your own body keeps the score you earned")
{
    StateStore store = StateStore::inMemory();
    SnakeApp app(store, kSeed);
    start(app);

    for (int guard = 0; guard < 4000 && app.length() < 6; ++guard)
    {
        chaseFood(app);
    }
    REQUIRE(app.state() == SnakeApp::State::Running);
    REQUIRE(app.length() >= 6);

    const int earned = app.score();

    // A tight circle: at this length the head must run into the body.
    for (int guard = 0; guard < 8 && app.state() == SnakeApp::State::Running; ++guard)
    {
        send(app, InputType::Rotate, +1);
        stepOnce(app);
    }

    REQUIRE(app.state() == SnakeApp::State::Dead);
    CHECK(app.score() == earned);
    CHECK(app.highScore() == app.score());
}
