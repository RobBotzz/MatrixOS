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
#include <string>

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

/// Plays the crash animation to its end.
void crumble(SnakeApp &app)
{
    REQUIRE(app.state() == SnakeApp::State::Dying);
    app.update(Duration{SnakeApp::kDyingSeconds});
}

/// Turns at most a quarter towards `food` and takes one step.
void chase(SnakeApp &app, SnakeApp::Cell food)
{
    const SnakeApp::Cell head = app.head();

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

void chaseFood(SnakeApp &app)
{
    chase(app, app.food());
}

/// Ends a running game against the nearest wall.
void crash(SnakeApp &app)
{
    for (int i = 0; i < 4 * SnakeApp::kColumns && app.state() == SnakeApp::State::Running; ++i)
    {
        stepOnce(app);
    }
}

/// Plays until `done` says so, trying seeds until the greedy chase survives that long.
template <typename Done> void playUntil(SnakeApp &app, Done done)
{
    for (int guard = 0; guard < 2000 && !done(); ++guard)
    {
        if (app.state() != SnakeApp::State::Running)
        {
            return;
        }
        chaseFood(app);
    }
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
    crash(app);

    crumble(app);
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

TEST_CASE("a run that makes the board asks for a name and survives a restart")
{
    TempDir dir;
    int earned = 0;

    {
        StateStore store(dir.path());
        SnakeApp app(store, kSeed);
        start(app);

        playUntil(app, [&app] { return app.score() >= 2; });
        earned = app.score();
        REQUIRE(earned >= 2);

        crash(app);
        crumble(app);
        REQUIRE(app.state() == SnakeApp::State::EnterName);

        send(app, InputType::Rotate, 1);
        send(app, InputType::LongPress);
        REQUIRE(app.state() == SnakeApp::State::Idle);
    }

    StateStore reopened(dir.path());
    SnakeApp later(reopened, kSeed);
    REQUIRE(later.scoreboard().entries().size() == 1);
    CHECK(later.scoreboard().entries()[0].name == "A");
    CHECK(later.scoreboard().entries()[0].score == earned);
}

TEST_CASE("a run cut off by a pulled plug still reaches the board, once")
{
    TempDir dir;

    {
        StateStore store(dir.path());
        SnakeApp app(store, kSeed);
        start(app);
        playUntil(app, [&app] { return app.score() >= 1; });
        REQUIRE(app.state() == SnakeApp::State::Running);

        StateStore observer(dir.path()); // acceptance criterion 3, nobody called save()
        CHECK(observer.section("snake").getInt("banked", 0) == app.score());
    }

    {
        StateStore reopened(dir.path());
        SnakeApp later(reopened, kSeed);
        REQUIRE(later.scoreboard().entries().size() == 1);
        CHECK(later.scoreboard().entries()[0].name == "???");
    }

    StateStore again(dir.path());
    SnakeApp once_more(again, kSeed);
    CHECK(once_more.scoreboard().entries().size() == 1);
}

TEST_CASE("a score too low for the board ends on the score, and a press shows the board")
{
    StateStore store = StateStore::inMemory();
    SnakeApp app(store, kSeed);
    start(app);
    crash(app);
    crumble(app);

    REQUIRE(app.state() == SnakeApp::State::Dead);
    CHECK(app.scoreboard().entries().empty());

    send(app, InputType::Press);
    CHECK(app.state() == SnakeApp::State::Idle);

    send(app, InputType::Press);
    CHECK(app.state() == SnakeApp::State::Running);
}

TEST_CASE("a hold during a good run settles the score instead of dropping it")
{
    StateStore store = StateStore::inMemory();
    SnakeApp app(store, kSeed);
    start(app);
    playUntil(app, [&app] { return app.score() >= 1; });

    send(app, InputType::LongPress);
    CHECK(app.state() == SnakeApp::State::EnterName);
}

TEST_CASE("presses cannot cut the crash animation short")
{
    StateStore store = StateStore::inMemory();
    SnakeApp app(store, kSeed);
    start(app);
    crash(app);
    REQUIRE(app.state() == SnakeApp::State::Dying);

    send(app, InputType::Press);
    send(app, InputType::LongPress);
    CHECK(app.state() == SnakeApp::State::Dying);
}

TEST_CASE("the crash crumbles the snake away")
{
    StateStore store = StateStore::inMemory();
    SnakeApp app(store, kSeed);
    start(app);
    crash(app);

    const auto body_pixels = [&app]
    {
        Surface frame(64, 32);
        app.render(frame);
        int count = 0;
        for (int y = 0; y < 32; ++y)
        {
            for (int x = 0; x < 64; ++x)
            {
                count += frame.pixel(x, y) == Color{0x22, 0xC5, 0x5E} ? 1 : 0;
            }
        }
        return count;
    };

    CHECK(body_pixels() > 0);
    app.update(Duration{SnakeApp::kDyingSeconds - 0.01F});
    REQUIRE(app.state() == SnakeApp::State::Dying);
    CHECK(body_pixels() == 0);
}

TEST_CASE("gold food is worth more but speeds the snake up like any other")
{
    bool eaten_gold = false;
    for (std::uint32_t seed = 1; seed < 100 && !eaten_gold; ++seed)
    {
        StateStore store = StateStore::inMemory();
        SnakeApp app(store, seed);
        start(app);
        playUntil(app, [&app] { return app.gold().has_value(); });
        if (!app.gold())
        {
            continue;
        }

        const int score = app.score();
        const int eaten = app.eaten();
        const SnakeApp::Cell gold = *app.gold();
        while (app.state() == SnakeApp::State::Running && app.gold() && app.eaten() == eaten)
        {
            chase(app, gold);
        }

        if (app.state() == SnakeApp::State::Running && !app.gold() && app.eaten() == eaten + 1)
        {
            eaten_gold = true;
            CHECK(app.score() == score + SnakeApp::kGoldPoints);
            CHECK(app.length() == SnakeApp::kStartLength + static_cast<std::size_t>(eaten) + 1);
        }
    }
    CHECK(eaten_gold);
}

TEST_CASE("gold food goes away after its time, which stands still in a pause")
{
    bool found = false;
    for (std::uint32_t seed = 1; seed < 100 && !found; ++seed)
    {
        StateStore store = StateStore::inMemory();
        SnakeApp app(store, seed);
        start(app);
        playUntil(app, [&app] { return app.gold().has_value(); });
        if (!app.gold())
        {
            continue;
        }
        found = true;

        send(app, InputType::Press);
        app.update(Duration{SnakeApp::kGoldSeconds * 2.0F});
        CHECK(app.gold().has_value());

        send(app, InputType::Press);
        app.update(Duration{SnakeApp::kGoldSeconds});
        CHECK_FALSE(app.gold().has_value());
    }
    CHECK(found);
}

TEST_CASE("walls grow with the food, keep off the rim and each other, and kill")
{
    bool hit = false;
    for (std::uint32_t seed = 1; seed < 500 && !hit; ++seed)
    {
        StateStore store = StateStore::inMemory();
        SnakeApp app(store, seed);
        start(app);
        CHECK(app.walls().empty());

        playUntil(app, [&app] { return app.walls().size() >= 4; });
        if (app.walls().size() < 4)
        {
            continue;
        }

        const auto walls = app.walls();
        CHECK(app.eaten() >= 2 * SnakeApp::kFoodPerWall);
        for (const SnakeApp::Cell &wall : walls)
        {
            CHECK(wall.x >= 1);
            CHECK(wall.x <= SnakeApp::kColumns - 2);
            CHECK(wall.y >= 1);
            CHECK(wall.y <= SnakeApp::kRows - 2);
        }
        CHECK((std::abs(walls[0].x - walls[2].x) > 1 || std::abs(walls[0].y - walls[2].y) > 1));

        for (int guard = 0; guard < 200 && app.state() == SnakeApp::State::Running; ++guard)
        {
            chase(app, walls[0]);
        }
        const SnakeApp::Cell head = app.head();
        hit = std::abs(head.x - walls[0].x) + std::abs(head.y - walls[0].y) == 1 ||
              std::abs(head.x - walls[1].x) + std::abs(head.y - walls[1].y) == 1;
        if (hit)
        {
            CHECK(app.state() == SnakeApp::State::Dying);
        }
    }
    CHECK(hit);
}

TEST_CASE("the field is framed and the cells are three pixels square")
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
        CHECK_FALSE(frame.pixel(1, y) == Color::black());
        CHECK_FALSE(frame.pixel(62, y) == Color::black());
        CHECK_FALSE(frame.pixel(63, y) == Color::black());
    }

    const SnakeApp::Cell head = app.head();
    const int left = 2 + head.x * 3;
    const int top = 1 + head.y * 3;
    const Color drawn = frame.pixel(left, top);
    CHECK_FALSE(drawn == Color::black());
    for (int y = 0; y < 3; ++y)
    {
        for (int x = 0; x < 3; ++x)
        {
            CHECK(frame.pixel(left + x, top + y) == drawn);
        }
    }
}

TEST_CASE("the board comes first")
{
    StateStore store = StateStore::inMemory();
    Scoreboard(store.section("snake")).add("ROBIN", 7);

    SnakeApp app(store, kSeed);
    app.onEnter();

    Surface frame(64, 32);
    app.render(frame);

    REQUIRE(app.state() == SnakeApp::State::Idle);

    bool first_row = false;
    for (int x = 0; x < 64; ++x)
    {
        first_row = first_row || !(frame.pixel(x, 11) == Color::black());
    }
    CHECK(first_row);
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

    crumble(app);
    CHECK(app.score() == earned);
    CHECK(app.state() == SnakeApp::State::EnterName);
}
