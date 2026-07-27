// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "gfx/surface.h"
#include "os/app.h"
#include "os/log.h"

#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace matrixos
{

class Display;
class Input;

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

    Shell(Display &display, Input &input, Duration targetFrameTime = kDefaultFrameTime);

    /// Apps are registered once at startup and owned by the shell — compiled in,
    /// never loaded dynamically (NG4). Registration order is launcher order.
    void add(std::unique_ptr<App> app);

    /// Ticks the active app until `shouldStop` returns true, then leaves the
    /// display dark (FR-4). Injecting the stop condition is what lets a test run
    /// an exact number of frames.
    void run(const std::function<bool()> &shouldStop);

    unsigned long frameCount() const { return frames_; }

    /// Name of the active app, or an empty view if none is.
    std::string_view activeName() const;

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

    void buildLauncher();
    void applyPending();
    void activateApp(std::size_t index);
    void activateLauncher();
    void setActive(App *app);
    void deactivate();
    void requestHome();
    void dispatch(const InputEvent &event);
    void reportFailure(const char *stage, const char *what);

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
    Duration target_frame_time_;

    std::vector<std::unique_ptr<App>> apps_;
    std::unique_ptr<App> launcher_;
    App *active_ = nullptr;

    Target pending_ = Target::None;
    std::size_t pending_index_ = 0;
    std::optional<std::size_t> last_app_;

    Surface frame_;
    unsigned long frames_ = 0;
};

} // namespace matrixos
