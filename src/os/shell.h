// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "gfx/surface.h"
#include "os/app.h"
#include "os/launcher.h"
#include "os/log.h"

#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace matrixos
{

class Display;
class Input;
class Provisioning;
class StateStore;
class TimeProvider;

/// The shell around one active app: it owns the loop, the app lifecycle, the
/// launcher and the back buffer (ADR-0003).
///
/// One process, one thread for our code, apps as objects. "Cooperative" means
/// nothing preempts an app — it returns control by returning from `update()` or
/// `render()`.
class Shell
{
public:
    /// 60 FPS (NFR-1). Zero runs flat out, which is what tests want.
    static constexpr Duration kDefaultFrameTime{1.0F / 60.0F};

    /// How long the setup app keeps the panel after setup has ended.
    ///
    /// Setup stops being needed the instant the join succeeds, so without this
    /// the "connected" screen would live for a single frame — correct, and
    /// useless. Four seconds is long enough to read and short enough that nobody
    /// waits for it.
    static constexpr Duration kSetupSuccessHold{4.0F};

    Shell(Display &display, Input &input, StateStore &store,
          Duration targetFrameTime = kDefaultFrameTime);

    /// Apps are registered once at startup and owned by the shell — compiled in,
    /// never loaded dynamically (NG4). Registration order is launcher order.
    /// Returns the index, which is what `superviseSetup` wants.
    std::size_t add(std::unique_ptr<App> app);

    /// Applies the time zone setting, the way brightness is applied (FR-25).
    /// Without this the clock runs in whatever zone the system was left in.
    void useTimeProvider(TimeProvider &time);

    /// Shows the setup app whenever the device has something for the user to do,
    /// and leaves it again when it does not (FR-35). Without this call the app is
    /// merely one more entry in the launcher.
    void superviseSetup(Provisioning &provisioning, std::size_t setupApp);

    /// Overrides `kSetupSuccessHold`. Only tests have a reason to.
    void setSetupSuccessHold(Duration hold) { setup_hold_ = hold; }

    /// Ticks the active app until `shouldStop` returns true, then leaves the
    /// display dark (FR-4). Injecting the stop condition is what lets a test run
    /// an exact number of frames.
    void run(const std::function<bool()> &shouldStop);

    unsigned long frameCount() const { return frames_; }

    /// Name of the active app, or an empty view if none is.
    std::string_view activeName() const;

    std::vector<std::string_view> appNames() const;

    bool launcherActive() const;

private:
    /// What the next frame should switch to. Switching is deferred by one frame
    /// on purpose: the request usually comes from inside the launcher's own
    /// onInput, and activating there would exit the app that is still running.
    enum class Target
    {
        None,
        Launcher,
        App,
    };

    void installLauncher();
    void applyPending();
    void activateApp(std::size_t index);
    void activateLauncher();
    void setActive(App *app);
    void deactivate();
    void requestHome();
    void dispatch(const InputEvent &event);
    void reportFailure(const char *stage, const char *what);

    /// Resolves the `startup` setting to an index (FR-19, FR-25); falls back to
    /// the first registered app.
    std::size_t startupApp();

    void applyBrightness();
    void applyTimeZone();
    void applySetupState(Duration dt);

    /// Turns an exception escaping app code into a dropped app instead of a dead
    /// device (FR-17). Catches crashes, not hangs — a blocking app still freezes
    /// everything, which is why apps may not block.
    template <typename Work> bool guarded(const char *stage, Work &&work)
    {
        try
        {
            work();
            return true;
        }
        catch (const std::exception &error)
        {
            reportFailure(stage, error.what());
            return false;
        }
        catch (...)
        {
            reportFailure(stage, "unknown exception");
            return false;
        }
    }

    Display &display_;
    Input &input_;
    StateStore &store_;
    Duration target_frame_time_;

    std::vector<std::unique_ptr<App>> apps_;
    std::unique_ptr<Launcher> launcher_;
    App *active_ = nullptr;

    Target pending_ = Target::None;
    std::size_t pending_index_ = 0;
    std::optional<std::size_t> last_app_;

    TimeProvider *time_ = nullptr;
    std::string time_zone_;

    Provisioning *provisioning_ = nullptr;
    std::optional<std::size_t> setup_index_;
    bool setup_shown_ = false;

    Duration setup_hold_ = kSetupSuccessHold;

    /// Counting down to handing the panel back after a successful setup. Absent
    /// when nothing is pending.
    std::optional<Duration> leaving_setup_;

    Surface frame_;
    unsigned long frames_ = 0;
    int brightness_ = -1; // no valid level yet, so the first frame applies one
};

} // namespace matrixos
