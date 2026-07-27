// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "gfx/surface.h"
#include "hal/display.h"
#include "hal/input.h"
#include "os/app.h"
#include "os/shell.h"

#include <catch2/catch_test_macros.hpp>

#include <deque>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace matrixos;

namespace
{

/// Records what the shell sends it. No hardware, no terminal — which is the whole
/// point of the Display interface (ADR-0002).
class RecordingDisplay : public Display
{
public:
    int width() const override { return 8; }
    int height() const override { return 4; }

    void present(const Surface &frame) override
    {
        ++presents;
        last_frame_was_black = true;
        for (const auto value : frame.bytes())
        {
            if (value != 0)
            {
                last_frame_was_black = false;
                break;
            }
        }
    }

    void clear() override { ++clears; }

    int presents = 0;
    int clears = 0;
    bool last_frame_was_black = true;
};

/// Hands out one prepared batch of events per poll.
class ScriptedInput : public Input
{
public:
    void queue(std::vector<InputEvent> batch) { batches_.push_back(std::move(batch)); }

    std::vector<InputEvent> poll() override
    {
        if (batches_.empty())
        {
            return {};
        }
        auto batch = batches_.front();
        batches_.pop_front();
        return batch;
    }

private:
    std::deque<std::vector<InputEvent>> batches_;
};

struct Calls
{
    int entered = 0;
    int exited = 0;
    int updates = 0;
    int renders = 0;
    std::vector<InputEvent> received;
    float total_dt = 0.0F;
};

/// Writes down every call instead of drawing anything.
class SpyApp : public App
{
public:
    explicit SpyApp(Calls &calls) : calls_(calls) {}

    std::string_view name() const override { return "Spy"; }
    void onEnter() override { ++calls_.entered; }
    void onExit() override { ++calls_.exited; }
    void onInput(const InputEvent &event) override { calls_.received.push_back(event); }

    void update(Duration dt) override
    {
        ++calls_.updates;
        calls_.total_dt += dt.count();
    }

    void render(Surface &surface) override
    {
        ++calls_.renders;
        surface.setPixel(0, 0, Color::white());
    }

private:
    Calls &calls_;
};

class ThrowingApp : public App
{
public:
    std::string_view name() const override { return "Throwing"; }
    void update(Duration) override { throw std::runtime_error("deliberate failure"); }
    void render(Surface &) override { ++renders; }

    int renders = 0;
};

/// Runs flat out, so tests do not wait on frame pacing.
Shell makeShell(Display &display, Input &input)
{
    return Shell(display, input, Duration::zero());
}

/// Stops the shell after exactly `frames` frames.
std::function<bool()> stopAfter(const Shell &shell, unsigned long frames)
{
    return [&shell, frames] { return shell.frameCount() >= frames; };
}

/// Same, but records whether the launcher was on screen at the start of each
/// frame. Inspecting the shell after `run()` returns would show nothing, because
/// it deactivates everything on the way out — so the stop condition doubles as
/// the observation point.
std::function<bool()> stopAfterTracking(const Shell &shell, unsigned long frames,
                                        std::vector<bool> &launcherPerFrame)
{
    return [&shell, frames, &launcherPerFrame]
    {
        launcherPerFrame.push_back(shell.launcherActive());
        return shell.frameCount() >= frames;
    };
}

} // namespace

TEST_CASE("the shell activates the first app and ticks it once per frame")
{
    RecordingDisplay display;
    ScriptedInput input;
    Calls calls;

    Shell shell = makeShell(display, input);
    shell.add(std::make_unique<SpyApp>(calls));
    shell.run(stopAfter(shell, 3));

    CHECK(calls.entered == 1);
    CHECK(calls.updates == 3);
    CHECK(calls.renders == 3);
    CHECK(display.presents == 3);
    CHECK(shell.frameCount() == 3);
}

TEST_CASE("stopping exits the app and leaves the display dark")
{
    RecordingDisplay display;
    ScriptedInput input;
    Calls calls;

    Shell shell = makeShell(display, input);
    shell.add(std::make_unique<SpyApp>(calls));
    shell.run(stopAfter(shell, 1));

    CHECK(calls.exited == 1);
    CHECK(display.clears == 1);
    CHECK(shell.activeName().empty());
}

TEST_CASE("elapsed time is passed on and is never negative")
{
    RecordingDisplay display;
    ScriptedInput input;
    Calls calls;

    Shell shell = makeShell(display, input);
    shell.add(std::make_unique<SpyApp>(calls));
    shell.run(stopAfter(shell, 5));

    CHECK(calls.total_dt >= 0.0F);
}

TEST_CASE("input events reach the active app")
{
    RecordingDisplay display;
    ScriptedInput input;
    Calls calls;

    input.queue({{InputType::Rotate, +1}, {InputType::Press, 0}});

    Shell shell = makeShell(display, input);
    shell.add(std::make_unique<SpyApp>(calls));
    shell.run(stopAfter(shell, 2));

    REQUIRE(calls.received.size() == 2);
    CHECK(calls.received[0].type == InputType::Rotate);
    CHECK(calls.received[0].delta == +1);
    CHECK(calls.received[1].type == InputType::Press);
}

TEST_CASE("Home is consumed by the shell and never reaches an app")
{
    RecordingDisplay display;
    ScriptedInput input;
    Calls calls;

    input.queue({{InputType::Home, 0}});

    Shell shell = makeShell(display, input);
    shell.add(std::make_unique<SpyApp>(calls));
    shell.run(stopAfter(shell, 2));

    CHECK(calls.received.empty());
}

TEST_CASE("an app that throws is dropped and the user lands in the launcher")
{
    RecordingDisplay display;
    ScriptedInput input;

    Shell shell = makeShell(display, input);
    auto app = std::make_unique<ThrowingApp>();
    const ThrowingApp *observer = app.get();
    shell.add(std::move(app));

    std::vector<bool> launcher_on;
    shell.run(stopAfterTracking(shell, 4, launcher_on));

    // Survived all four frames instead of taking the process down (FR-17) ...
    CHECK(shell.frameCount() == 4);
    CHECK(display.presents == 4);
    // ... render was never reached, because update threw first ...
    CHECK(observer->renders == 0);
    // ... and the launcher took over instead of leaving a black screen.
    REQUIRE_FALSE(launcher_on.empty());
    CHECK(launcher_on.back());
    CHECK_FALSE(display.last_frame_was_black);
}

TEST_CASE("Home switches to the launcher and back to the app it came from")
{
    RecordingDisplay display;
    ScriptedInput input;
    Calls calls;

    input.queue({{InputType::Home, 0}}); // frame 1: leave the app
    input.queue({});                     // frame 2: launcher on screen
    input.queue({{InputType::Home, 0}}); // frame 3: come back

    Shell shell = makeShell(display, input);
    shell.add(std::make_unique<SpyApp>(calls));

    std::vector<bool> launcher_on;
    shell.run(stopAfterTracking(shell, 4, launcher_on));

    CHECK(calls.entered == 2); // once at startup, once on returning
    CHECK(calls.exited == 2);  // once on leaving, once at shutdown
    CHECK(calls.updates == 2); // only the frames where it was actually active

    REQUIRE(launcher_on.size() >= 4);
    CHECK_FALSE(launcher_on[0]);     // the app is on screen at the start
    CHECK(launcher_on[1]);           // the first Home moved to the launcher
    CHECK_FALSE(launcher_on.back()); // the second Home came back
}

TEST_CASE("the launcher starts the app the user selected")
{
    RecordingDisplay display;
    ScriptedInput input;
    Calls first;
    Calls second;

    input.queue({{InputType::Home, 0}});    // to the launcher
    input.queue({{InputType::Rotate, +1}}); // select the second entry
    input.queue({{InputType::Press, 0}});   // start it

    Shell shell = makeShell(display, input);
    shell.add(std::make_unique<SpyApp>(first));
    shell.add(std::make_unique<SpyApp>(second));
    shell.run(stopAfter(shell, 5));

    CHECK(first.entered == 1);  // started at boot, never returned to
    CHECK(second.entered == 1); // started from the launcher
    CHECK(second.updates >= 1);
    CHECK_FALSE(shell.launcherActive());
}

TEST_CASE("Home does nothing harmful before any app has run")
{
    RecordingDisplay display;
    ScriptedInput input;

    // No apps at all: run() bails out, so Home can never even be dispatched.
    Shell shell = makeShell(display, input);
    input.queue({{InputType::Home, 0}});
    shell.run(stopAfter(shell, 2));

    CHECK(shell.frameCount() == 0);
}

TEST_CASE("with no apps registered the shell does nothing at all")
{
    RecordingDisplay display;
    ScriptedInput input;

    Shell shell = makeShell(display, input);
    shell.run(stopAfter(shell, 3));

    CHECK(shell.frameCount() == 0);
    CHECK(display.presents == 0);
}
