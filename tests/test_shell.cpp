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

TEST_CASE("an app that throws is dropped, the shell keeps running")
{
    RecordingDisplay display;
    ScriptedInput input;

    Shell shell = makeShell(display, input);
    auto app = std::make_unique<ThrowingApp>();
    const ThrowingApp *observer = app.get();
    shell.add(std::move(app));

    shell.run(stopAfter(shell, 4));

    // Survived all four frames instead of taking the process down (FR-17) ...
    CHECK(shell.frameCount() == 4);
    CHECK(display.presents == 4);
    // ... the app is gone after its first failure ...
    CHECK(shell.activeName().empty());
    // ... render was never reached, because update threw first ...
    CHECK(observer->renders == 0);
    // ... and the screen is black until a launcher can take over.
    CHECK(display.last_frame_was_black);
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
