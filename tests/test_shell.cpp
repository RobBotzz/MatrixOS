// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/setup/setup_app.h"
#include "fake_clock.h"
#include "gfx/surface.h"
#include "hal/display.h"
#include "hal/input.h"
#include "net/fake_wifi.h"
#include "os/app.h"
#include "os/provisioning.h"
#include "os/settings.h"
#include "os/shell.h"
#include "os/state.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace matrixos;
using matrixos::test::TempDir;

namespace
{

constexpr Provisioning::Timing kInstantSetup{std::chrono::milliseconds(0),
                                             std::chrono::milliseconds(0)};

const Identity kTestIdentity{"a3f1", "matrixos-a3f1", "MatrixOS-a3f1"};

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

    void setBrightness(int percent) override { brightness = percent; }

    int presents = 0;
    int clears = 0;
    int brightness = -1;
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
    explicit SpyApp(Calls &calls, std::string_view name = "Spy") : calls_(calls), name_(name) {}

    std::string_view name() const override { return name_; }
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
    std::string_view name_;
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
Shell makeShell(Display &display, Input &input, StateStore &store)
{
    return Shell(display, input, store, Duration::zero());
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

/// Same again for the active app's name, which `run()` also clears on its way
/// out.
std::function<bool()> stopAfterNaming(const Shell &shell, unsigned long frames,
                                      std::vector<std::string> &namePerFrame)
{
    return [&shell, frames, &namePerFrame]
    {
        namePerFrame.emplace_back(shell.activeName());
        return shell.frameCount() >= frames;
    };
}

} // namespace

TEST_CASE("the shell activates the first app and ticks it once per frame")
{
    RecordingDisplay display;
    ScriptedInput input;
    Calls calls;

    StateStore store = StateStore::inMemory();
    Shell shell = makeShell(display, input, store);
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

    StateStore store = StateStore::inMemory();
    Shell shell = makeShell(display, input, store);
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

    StateStore store = StateStore::inMemory();
    Shell shell = makeShell(display, input, store);
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

    StateStore store = StateStore::inMemory();
    Shell shell = makeShell(display, input, store);
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

    StateStore store = StateStore::inMemory();
    Shell shell = makeShell(display, input, store);
    shell.add(std::make_unique<SpyApp>(calls));
    shell.run(stopAfter(shell, 2));

    CHECK(calls.received.empty());
}

TEST_CASE("an app that throws is dropped and the user lands in the launcher")
{
    RecordingDisplay display;
    ScriptedInput input;

    StateStore store = StateStore::inMemory();
    Shell shell = makeShell(display, input, store);
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

    StateStore store = StateStore::inMemory();
    Shell shell = makeShell(display, input, store);
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

    StateStore store = StateStore::inMemory();
    Shell shell = makeShell(display, input, store);
    shell.add(std::make_unique<SpyApp>(first, "First"));
    shell.add(std::make_unique<SpyApp>(second, "Second"));
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
    StateStore store = StateStore::inMemory();
    Shell shell = makeShell(display, input, store);
    input.queue({{InputType::Home, 0}});
    shell.run(stopAfter(shell, 2));

    CHECK(shell.frameCount() == 0);
}

TEST_CASE("with no apps registered the shell does nothing at all")
{
    RecordingDisplay display;
    ScriptedInput input;

    StateStore store = StateStore::inMemory();
    Shell shell = makeShell(display, input, store);
    shell.run(stopAfter(shell, 3));

    CHECK(shell.frameCount() == 0);
    CHECK(display.presents == 0);
}

TEST_CASE("the device comes back to the app that was running (FR-19)")
{
    TempDir dir;

    {
        RecordingDisplay display;
        ScriptedInput input;
        Calls first;
        Calls second;

        input.queue({{InputType::Home, 0}});    // to the launcher
        input.queue({{InputType::Rotate, +1}}); // move to the second entry
        input.queue({{InputType::Press, 0}});   // start it

        StateStore store(dir.path());
        Shell shell = makeShell(display, input, store);
        shell.add(std::make_unique<SpyApp>(first, "First"));
        shell.add(std::make_unique<SpyApp>(second, "Second"));
        shell.run(stopAfter(shell, 5));

        REQUIRE(second.entered == 1);
    }

    RecordingDisplay display;
    ScriptedInput input;
    Calls first;
    Calls second;

    StateStore store(dir.path());
    Shell shell = makeShell(display, input, store);
    shell.add(std::make_unique<SpyApp>(first, "First"));
    shell.add(std::make_unique<SpyApp>(second, "Second"));
    shell.run(stopAfter(shell, 2));

    CHECK(second.entered == 1);
    CHECK(first.entered == 0);
}

TEST_CASE("the launcher opens on the restored app rather than at the top of the list")
{
    TempDir dir;
    {
        StateStore setup(dir.path());
        StateSection &shell_state = setup.section("shell");
        shell_state.setString("last_app", "Second");
        REQUIRE(shell_state.save());
    }

    RecordingDisplay display;
    ScriptedInput input;
    Calls first;
    Calls second;

    input.queue({{InputType::Home, 0}});  // frame 1: to the launcher
    input.queue({});                      // frame 2: the launcher is on screen
    input.queue({{InputType::Press, 0}}); // frame 3: start whatever is selected

    StateStore store(dir.path());
    Shell shell = makeShell(display, input, store);
    shell.add(std::make_unique<SpyApp>(first, "First"));
    shell.add(std::make_unique<SpyApp>(second, "Second"));
    shell.run(stopAfter(shell, 5));

    CHECK(second.entered == 2); // pressed without turning: same app again
    CHECK(first.entered == 0);
}

TEST_CASE("an app named in the store that no longer exists falls back to the first")
{
    TempDir dir;
    {
        StateStore setup(dir.path());
        StateSection &shell_state = setup.section("shell");
        shell_state.setString("last_app", "Tetris");
        REQUIRE(shell_state.save());
    }

    RecordingDisplay display;
    ScriptedInput input;
    Calls first;
    Calls second;

    StateStore store(dir.path());
    Shell shell = makeShell(display, input, store);
    shell.add(std::make_unique<SpyApp>(first, "First"));
    shell.add(std::make_unique<SpyApp>(second, "Second"));
    shell.run(stopAfter(shell, 2));

    CHECK(first.entered == 1);
    CHECK(second.entered == 0);
}

TEST_CASE("a fixed startup app wins over the one that ran last (FR-25)")
{
    RecordingDisplay display;
    ScriptedInput input;
    Calls first;
    Calls second;

    StateStore store = StateStore::inMemory();
    store.section("shell").setString("last_app", "Second");
    store.section(settings::kSection).setString(settings::kStartup, "First");

    Shell shell = makeShell(display, input, store);
    shell.add(std::make_unique<SpyApp>(first, "First"));
    shell.add(std::make_unique<SpyApp>(second, "Second"));
    shell.run(stopAfter(shell, 2));

    CHECK(first.entered == 1);
    CHECK(second.entered == 0);
}

TEST_CASE("the stored brightness reaches the display")
{
    RecordingDisplay display;
    ScriptedInput input;
    Calls calls;

    StateStore store = StateStore::inMemory();
    store.section(settings::kSection).setInt(settings::kBrightness, 25);

    Shell shell = makeShell(display, input, store);
    shell.add(std::make_unique<SpyApp>(calls));
    shell.run(stopAfter(shell, 2));

    CHECK(display.brightness == 25);
}

TEST_CASE("a brightness outside the allowed range is clamped, not obeyed")
{
    RecordingDisplay display;
    ScriptedInput input;
    Calls calls;

    StateStore store = StateStore::inMemory();
    store.section(settings::kSection).setInt(settings::kBrightness, 0);

    Shell shell = makeShell(display, input, store);
    shell.add(std::make_unique<SpyApp>(calls));
    shell.run(stopAfter(shell, 2));

    CHECK(display.brightness == settings::kMinBrightness);
}

// ---------------------------------------------------------------------------
// v0.4: the shell shows the setup app while the device needs the user, and
// applies the time zone the way it applies brightness.
// ---------------------------------------------------------------------------

TEST_CASE("an unconfigured device lands on the setup app rather than on the startup one")
{
    RecordingDisplay display;
    ScriptedInput input;
    StateStore store = StateStore::inMemory();

    FakeWifi wifi;
    Provisioning provisioning(wifi, store, kInstantSetup);
    provisioning.begin();
    provisioning.waitForIdle();
    REQUIRE(provisioning.needsSetup());

    Calls first;
    Shell shell = makeShell(display, input, store);
    shell.add(std::make_unique<SpyApp>(first, "First"));
    const std::size_t setup = shell.add(std::make_unique<SetupApp>(provisioning, kTestIdentity));
    shell.superviseSetup(provisioning, setup);

    std::vector<std::string> active;
    shell.run(stopAfterNaming(shell, 3, active));

    CHECK(active.back() == "Setup");
}

TEST_CASE("when setup finishes, the device moves on by itself")
{
    RecordingDisplay display;
    ScriptedInput input;
    StateStore store = StateStore::inMemory();

    FakeWifi wifi;
    Provisioning provisioning(wifi, store, kInstantSetup);
    provisioning.begin();
    provisioning.waitForIdle();

    Calls first;
    Shell shell = makeShell(display, input, store);
    shell.add(std::make_unique<SpyApp>(first, "First"));
    const std::size_t setup = shell.add(std::make_unique<SetupApp>(provisioning, kTestIdentity));
    shell.superviseSetup(provisioning, setup);
    shell.setSetupSuccessHold(Duration::zero()); // the hold has its own case below

    // One session, with the world changing inside it. Two run() calls would not
    // do: run() activates the startup app again every time, so a state change
    // between two of them is not the same thing as one during a session.
    std::vector<std::string> active;
    shell.run(
        [&]
        {
            active.emplace_back(shell.activeName());
            if (shell.frameCount() == 3)
            {
                provisioning.requestConnect("Kitchen", "");
                provisioning.waitForIdle();
            }
            return shell.frameCount() >= 30;
        });

    // Frame 0 is recorded before the shell has looked at provisioning at all, so
    // the startup app is what it sees first; the take-over lands one frame later.
    REQUIRE(active.size() > 3);
    CHECK(active[1] == "Setup");
    CHECK(active.back() == "First");
}

TEST_CASE("the connected screen stays up long enough to be read")
{
    // Setup stops being needed the instant the join succeeds. Without the hold
    // the success screen would be replaced in the frame it first appeared, and
    // the panel would never confirm anything to the person standing in front of
    // it — which is the whole reason the setup app exists (FR-35).
    RecordingDisplay display;
    ScriptedInput input;
    StateStore store = StateStore::inMemory();

    FakeWifi wifi;
    Provisioning provisioning(wifi, store, kInstantSetup);
    provisioning.begin();
    provisioning.waitForIdle();

    Calls first;
    Shell shell = makeShell(display, input, store);
    shell.add(std::make_unique<SpyApp>(first, "First"));
    const std::size_t setup = shell.add(std::make_unique<SetupApp>(provisioning, kTestIdentity));
    shell.superviseSetup(provisioning, setup);
    shell.setSetupSuccessHold(Duration{60.0F}); // longer than this test can run

    std::vector<std::string> active;
    shell.run(
        [&]
        {
            active.emplace_back(shell.activeName());
            if (shell.frameCount() == 3)
            {
                provisioning.requestConnect("Kitchen", "");
                provisioning.waitForIdle();
            }
            return shell.frameCount() >= 30;
        });

    REQUIRE_FALSE(provisioning.needsSetup());
    CHECK(active.back() == "Setup");
}

TEST_CASE("the setup app is never remembered as the last app")
{
    // Otherwise a device that was set up once would boot into a setup screen it
    // no longer needs, and FR-19 would fight FR-35.
    RecordingDisplay display;
    ScriptedInput input;
    StateStore store = StateStore::inMemory();

    FakeWifi wifi;
    Provisioning provisioning(wifi, store, kInstantSetup);
    provisioning.begin();
    provisioning.waitForIdle();

    Calls first;
    Shell shell = makeShell(display, input, store);
    shell.add(std::make_unique<SpyApp>(first, "First"));
    const std::size_t setup = shell.add(std::make_unique<SetupApp>(provisioning, kTestIdentity));
    shell.superviseSetup(provisioning, setup);

    std::vector<std::string> active;
    shell.run(stopAfterNaming(shell, 3, active));
    REQUIRE(active.back() == "Setup");

    CHECK(store.section("shell").getString("last_app", "") == "First");
}

TEST_CASE("a device that is already online never sees the setup app")
{
    RecordingDisplay display;
    ScriptedInput input;
    StateStore store = StateStore::inMemory();

    FakeWifi wifi;
    wifi.pretendJoined("Kitchen");
    Provisioning provisioning(wifi, store, kInstantSetup);
    provisioning.begin();
    provisioning.waitForIdle();

    Calls first;
    Shell shell = makeShell(display, input, store);
    shell.add(std::make_unique<SpyApp>(first, "First"));
    const std::size_t setup = shell.add(std::make_unique<SetupApp>(provisioning, kTestIdentity));
    shell.superviseSetup(provisioning, setup);

    std::vector<std::string> active;
    shell.run(stopAfterNaming(shell, 3, active));

    CHECK(active.back() == "First");
}

TEST_CASE("without superviseSetup the setup app is just another launcher entry")
{
    RecordingDisplay display;
    ScriptedInput input;
    StateStore store = StateStore::inMemory();

    FakeWifi wifi;
    Provisioning provisioning(wifi, store, kInstantSetup);
    provisioning.begin();
    provisioning.waitForIdle();

    Calls first;
    Shell shell = makeShell(display, input, store);
    shell.add(std::make_unique<SpyApp>(first, "First"));
    shell.add(std::make_unique<SetupApp>(provisioning, kTestIdentity));

    std::vector<std::string> active;
    shell.run(stopAfterNaming(shell, 3, active));

    CHECK(active.back() == "First");
}

TEST_CASE("the time zone reaches the provider, and only when it changes")
{
    RecordingDisplay display;
    ScriptedInput input;
    StateStore store = StateStore::inMemory();
    test::FakeClock clock;

    Calls calls;
    Shell shell = makeShell(display, input, store);
    shell.add(std::make_unique<SpyApp>(calls));
    shell.useTimeProvider(clock);

    shell.run(stopAfter(shell, 3));
    CHECK(clock.zone == settings::kDefaultTimeZone);

    clock.zone.clear(); // a second write would put it back
    shell.run(stopAfter(shell, 6));
    CHECK(clock.zone.empty());

    store.section(settings::kSection).setString(settings::kTimeZone, "Asia/Tokyo");
    shell.run(stopAfter(shell, 9));
    CHECK(clock.zone == "Asia/Tokyo");
}
