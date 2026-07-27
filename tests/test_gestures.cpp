// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "hal/gestures.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

using namespace matrixos;
using namespace std::chrono_literals;

namespace
{

/// A synthetic clock. No waiting, no flakiness — the whole point of the recognizer
/// taking time as an argument.
class FakeClock
{
public:
    InputTime now() const { return base_ + elapsed_; }
    void advance(std::chrono::milliseconds by) { elapsed_ += by; }

private:
    InputTime base_ = InputTime{} + 1h; // far enough in that debounce maths is sane
    std::chrono::milliseconds elapsed_{0};
};

std::vector<InputType> typesOf(const std::vector<InputEvent> &events)
{
    std::vector<InputType> types;
    for (const InputEvent &event : events)
    {
        types.push_back(event.type);
    }
    return types;
}

} // namespace

TEST_CASE("a short press produces one Press, on release")
{
    GestureRecognizer recognizer;
    FakeClock clock;

    CHECK(recognizer.onButtonChange(true, clock.now()).empty()); // nothing yet
    clock.advance(80ms);
    CHECK(typesOf(recognizer.onButtonChange(false, clock.now())) == std::vector{InputType::Press});
}

TEST_CASE("a hold fires LongPress while the button is still down")
{
    GestureRecognizer recognizer;
    FakeClock clock;

    recognizer.onButtonChange(true, clock.now());

    clock.advance(599ms);
    CHECK(recognizer.tick(clock.now()).empty()); // one millisecond short

    clock.advance(1ms);
    CHECK(typesOf(recognizer.tick(clock.now())) == std::vector{InputType::LongPress});
    CHECK(recognizer.isDown()); // fired without waiting for the release (FR-10)
}

TEST_CASE("LongPress fires only once however long the hold lasts")
{
    GestureRecognizer recognizer;
    FakeClock clock;

    recognizer.onButtonChange(true, clock.now());
    clock.advance(600ms);
    REQUIRE(typesOf(recognizer.tick(clock.now())) == std::vector{InputType::LongPress});

    for (int i = 0; i < 5; ++i)
    {
        clock.advance(200ms);
        CHECK(recognizer.tick(clock.now()).empty());
    }
}

TEST_CASE("the release after a LongPress produces no Press")
{
    GestureRecognizer recognizer;
    FakeClock clock;

    recognizer.onButtonChange(true, clock.now());
    clock.advance(700ms);
    REQUIRE(typesOf(recognizer.tick(clock.now())) == std::vector{InputType::LongPress});

    CHECK(recognizer.onButtonChange(false, clock.now()).empty());
}

TEST_CASE("switch bounce collapses into a single press")
{
    GestureRecognizer recognizer;
    FakeClock clock;

    recognizer.onButtonChange(true, clock.now());

    // A real switch settles within a few milliseconds. Eight of them stay inside
    // the ten-millisecond window — chatter lasting longer than the window would get
    // through, which is inherent to any debounce and not something to pretend away.
    for (int i = 0; i < 2; ++i)
    {
        clock.advance(2ms);
        CHECK(recognizer.onButtonChange(false, clock.now()).empty());
        clock.advance(2ms);
        CHECK(recognizer.onButtonChange(true, clock.now()).empty());
    }

    clock.advance(100ms);
    CHECK(typesOf(recognizer.onButtonChange(false, clock.now())) == std::vector{InputType::Press});
}

TEST_CASE("repeating a level that has not changed is ignored")
{
    GestureRecognizer recognizer;
    FakeClock clock;

    CHECK(recognizer.onButtonChange(false, clock.now()).empty()); // already up
    recognizer.onButtonChange(true, clock.now());
    clock.advance(50ms);
    CHECK(recognizer.onButtonChange(true, clock.now()).empty()); // already down
}

TEST_CASE("ticking with no button activity produces nothing")
{
    GestureRecognizer recognizer;
    FakeClock clock;

    for (int i = 0; i < 10; ++i)
    {
        clock.advance(100ms);
        CHECK(recognizer.tick(clock.now()).empty());
    }
}

TEST_CASE("by default a press is instant and no DoublePress is ever produced")
{
    // The default trades double-press away to keep a single press immediate; see
    // the note on Timing::double_press_window.
    GestureRecognizer recognizer;
    FakeClock clock;

    recognizer.onButtonChange(true, clock.now());
    clock.advance(30ms);
    CHECK(typesOf(recognizer.onButtonChange(false, clock.now())) == std::vector{InputType::Press});

    clock.advance(50ms);
    CHECK(recognizer.onButtonChange(true, clock.now()).empty());
    clock.advance(30ms);
    CHECK(typesOf(recognizer.onButtonChange(false, clock.now())) ==
          std::vector{InputType::Press}); // a second Press, not a DoublePress
}

TEST_CASE("with a window configured, two quick presses become one DoublePress")
{
    GestureRecognizer recognizer(GestureRecognizer::Timing{
        .long_press = 600ms, .debounce = 10ms, .double_press_window = 300ms});
    FakeClock clock;

    recognizer.onButtonChange(true, clock.now());
    clock.advance(30ms);
    CHECK(recognizer.onButtonChange(false, clock.now()).empty()); // Press is withheld

    clock.advance(50ms);
    CHECK(typesOf(recognizer.onButtonChange(true, clock.now())) ==
          std::vector{InputType::DoublePress});

    clock.advance(30ms);
    CHECK(recognizer.onButtonChange(false, clock.now()).empty()); // release stays silent
}

TEST_CASE("with a window configured, a lone press arrives once the window expires")
{
    GestureRecognizer recognizer(GestureRecognizer::Timing{
        .long_press = 600ms, .debounce = 10ms, .double_press_window = 300ms});
    FakeClock clock;

    recognizer.onButtonChange(true, clock.now());
    clock.advance(30ms);
    REQUIRE(recognizer.onButtonChange(false, clock.now()).empty());

    clock.advance(299ms);
    CHECK(recognizer.tick(clock.now()).empty());

    clock.advance(1ms);
    CHECK(typesOf(recognizer.tick(clock.now())) == std::vector{InputType::Press});
}
