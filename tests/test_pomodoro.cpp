// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/pomodoro/pomodoro.h"
#include "gfx/surface.h"

#include <catch2/catch_test_macros.hpp>

using namespace matrixos;
using Mode = PomodoroApp::Mode;
using State = PomodoroApp::State;

namespace
{

void send(PomodoroApp &app, InputType type, int delta = 0)
{
    app.onInput(InputEvent{type, delta});
}

void advance(PomodoroApp &app, float seconds, float step = 1.0F / 60.0F)
{
    for (float elapsed = 0.0F; elapsed < seconds; elapsed += step)
    {
        app.update(Duration{step});
    }
}

Surface frameOf(PomodoroApp &app)
{
    Surface surface(64, 32);
    app.render(surface);
    return surface;
}

int litInRow(const Surface &surface, int y, int fromX = 0)
{
    int lit = 0;
    for (int x = fromX; x < surface.width(); ++x)
    {
        if (!(surface.pixel(x, y) == Color::black()))
        {
            ++lit;
        }
    }
    return lit;
}

int litInRect(const Surface &surface, int x0, int y0, int x1, int y1)
{
    int lit = 0;
    for (int y = y0; y < y1; ++y)
    {
        for (int x = x0; x < x1; ++x)
        {
            if (!(surface.pixel(x, y) == Color::black()))
            {
                ++lit;
            }
        }
    }
    return lit;
}

/// Both durations set to one minute, timer started.
void startWithOneMinuteEach(PomodoroApp &app)
{
    send(app, InputType::Rotate, -100); // focus to the minimum
    send(app, InputType::Press);        // on to the break duration
    send(app, InputType::Rotate, -100); // break to the minimum
    send(app, InputType::Press);        // start
}

} // namespace

TEST_CASE("a fresh timer waits on the focus duration")
{
    PomodoroApp app;

    CHECK(app.mode() == Mode::Focus);
    CHECK(app.state() == State::Setting);
    CHECK(app.focusMinutes() == PomodoroApp::kDefaultFocusMinutes);
    CHECK(app.breakMinutes() == PomodoroApp::kDefaultBreakMinutes);
}

TEST_CASE("setting walks focus then break, then starts")
{
    PomodoroApp app;

    send(app, InputType::Rotate, +5);
    CHECK(app.focusMinutes() == PomodoroApp::kDefaultFocusMinutes + 5);
    CHECK(app.breakMinutes() == PomodoroApp::kDefaultBreakMinutes);

    send(app, InputType::Press);
    REQUIRE(app.mode() == Mode::Break);
    REQUIRE(app.state() == State::Setting);

    send(app, InputType::Rotate, +3);
    CHECK(app.breakMinutes() == PomodoroApp::kDefaultBreakMinutes + 3);
    CHECK(app.focusMinutes() == PomodoroApp::kDefaultFocusMinutes + 5);

    send(app, InputType::Press);
    CHECK(app.mode() == Mode::Focus);
    CHECK(app.state() == State::Running);
    CHECK(app.remainingSeconds() == static_cast<float>(app.focusMinutes()) * 60.0F);
}

TEST_CASE("both durations clamp at both ends")
{
    PomodoroApp app;

    send(app, InputType::Rotate, -1000);
    CHECK(app.focusMinutes() == PomodoroApp::kMinMinutes);
    send(app, InputType::Rotate, +1000);
    CHECK(app.focusMinutes() == PomodoroApp::kMaxMinutes);

    send(app, InputType::Press);
    send(app, InputType::Rotate, +1000);
    CHECK(app.breakMinutes() == PomodoroApp::kMaxMinutes);
    send(app, InputType::Rotate, -1000);
    CHECK(app.breakMinutes() == PomodoroApp::kMinMinutes);
}

TEST_CASE("durations cannot be changed once a timer runs")
{
    PomodoroApp app;
    startWithOneMinuteEach(app);
    REQUIRE(app.state() == State::Running);

    send(app, InputType::Rotate, +5);
    CHECK(app.focusMinutes() == PomodoroApp::kMinMinutes);
}

TEST_CASE("time only runs down while running")
{
    PomodoroApp app;
    startWithOneMinuteEach(app);

    advance(app, 5.0F);
    const float after_running = app.remainingSeconds();
    CHECK(after_running < 60.0F);

    send(app, InputType::Press);
    REQUIRE(app.state() == State::Paused);
    advance(app, 20.0F);
    CHECK(app.remainingSeconds() == after_running);

    send(app, InputType::Press);
    REQUIRE(app.state() == State::Running);
    advance(app, 2.0F);
    CHECK(app.remainingSeconds() < after_running);
}

TEST_CASE("the focus timer ends in an alarm and does not go negative")
{
    PomodoroApp app;
    startWithOneMinuteEach(app);

    advance(app, 61.0F);

    CHECK(app.mode() == Mode::Focus);
    CHECK(app.state() == State::Alarm);
    CHECK(app.remainingSeconds() == 0.0F);

    advance(app, 10.0F);
    CHECK(app.remainingSeconds() == 0.0F);
}

TEST_CASE("acknowledging the focus alarm starts the break")
{
    PomodoroApp app;
    startWithOneMinuteEach(app);
    advance(app, 61.0F);
    REQUIRE(app.state() == State::Alarm);

    send(app, InputType::Press);

    CHECK(app.mode() == Mode::Break);
    CHECK(app.state() == State::Running);
    CHECK(app.remainingSeconds() == static_cast<float>(app.breakMinutes()) * 60.0F);
}

TEST_CASE("acknowledging the break alarm starts the next focus, so the cycle continues")
{
    PomodoroApp app;
    startWithOneMinuteEach(app);
    advance(app, 61.0F);
    send(app, InputType::Press); // into the break
    advance(app, 61.0F);
    REQUIRE(app.mode() == Mode::Break);
    REQUIRE(app.state() == State::Alarm);

    send(app, InputType::Press);

    CHECK(app.mode() == Mode::Focus);
    CHECK(app.state() == State::Running);
    CHECK(app.remainingSeconds() == static_cast<float>(app.focusMinutes()) * 60.0F);
}

TEST_CASE("a long press returns to setting from every state and keeps the durations")
{
    PomodoroApp app;
    send(app, InputType::Rotate, -20); // focus 5
    send(app, InputType::Press);
    send(app, InputType::Rotate, +5); // break 10
    const int focus = app.focusMinutes();
    const int rest = app.breakMinutes();
    send(app, InputType::Press);

    for (int step = 0; step < 3; ++step)
    {
        if (step == 1)
        {
            send(app, InputType::Press); // paused
        }
        if (step == 2)
        {
            advance(app, static_cast<float>(focus) * 60.0F + 1.0F); // alarm
        }

        send(app, InputType::LongPress);
        CHECK(app.mode() == Mode::Focus);
        CHECK(app.state() == State::Setting);
        CHECK(app.focusMinutes() == focus);
        CHECK(app.breakMinutes() == rest);

        send(app, InputType::Press); // back through setting ...
        send(app, InputType::Press); // ... and running again
        REQUIRE(app.state() == State::Running);
    }
}

TEST_CASE("focus shows the tomato, break gives the width to the digits instead")
{
    // The break digits are centred and 58 px wide, so they reach well into the left
    // half. Only the outermost sliver belongs to the tomato alone.
    PomodoroApp app;
    CHECK(litInRect(frameOf(app), 0, 13, 3, 24) > 0);

    send(app, InputType::Press);
    REQUIRE(app.mode() == Mode::Break);
    CHECK(litInRect(frameOf(app), 0, 13, 3, 24) == 0);
}

TEST_CASE("the tomato empties as the focus timer runs down")
{
    PomodoroApp app;
    startWithOneMinuteEach(app);

    const int full = litInRect(frameOf(app), 0, 9, 17, 28);
    advance(app, 55.0F);
    const int nearly_done = litInRect(frameOf(app), 0, 9, 17, 28);

    // The husk stays visible, so the pixel count holds; what changes is how many of
    // them are bright pulp.
    CHECK(full == nearly_done);

    const Surface late = frameOf(app);
    int bright = 0;
    for (int y = 9; y < 28; ++y)
    {
        for (int x = 0; x < 17; ++x)
        {
            if (late.pixel(x, y).r > 200)
            {
                ++bright;
            }
        }
    }
    CHECK(bright > 0);
    CHECK(bright < full);
}

TEST_CASE("the bar appears only once a timer is running")
{
    PomodoroApp app;
    CHECK(litInRow(frameOf(app), 26, 20) == 0);

    startWithOneMinuteEach(app);
    CHECK(litInRow(frameOf(app), 26, 20) > 0);
}

TEST_CASE("the break bar spans the whole width and shrinks")
{
    PomodoroApp app;
    startWithOneMinuteEach(app);
    advance(app, 61.0F);
    send(app, InputType::Press);
    REQUIRE(app.mode() == Mode::Break);

    const int at_start = litInRow(frameOf(app), 28);
    CHECK(at_start > 32);

    advance(app, 40.0F);
    CHECK(litInRow(frameOf(app), 28) < at_start);
}

TEST_CASE("the alarm stops flashing eventually instead of forever")
{
    PomodoroApp app;
    startWithOneMinuteEach(app);
    advance(app, 61.0F);
    REQUIRE(app.state() == State::Alarm);

    advance(app, 120.0F);
    CHECK(app.state() == State::Alarm);
    CHECK(litInRect(frameOf(app), 0, 0, 64, 32) > 0);
}

TEST_CASE("gestures the timer does not use are ignored")
{
    PomodoroApp app;

    send(app, InputType::DoublePress);
    send(app, InputType::Home);

    CHECK(app.state() == State::Setting);
    CHECK(app.mode() == Mode::Focus);
    CHECK(app.focusMinutes() == PomodoroApp::kDefaultFocusMinutes);
}
