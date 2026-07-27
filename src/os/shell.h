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
#include <vector>

namespace matrixos
{

class Display;
class Input;

/// The shell around one active app: it owns the loop, the app lifecycle and the
/// back buffer (ADR-0003).
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
    /// never loaded dynamically (NG4).
    void add(std::unique_ptr<App> app);

    /// Ticks the active app until `shouldStop` returns true, then leaves the
    /// display dark (FR-4). Injecting the stop condition is what lets a test run
    /// an exact number of frames.
    void run(const std::function<bool()> &shouldStop);

    unsigned long frameCount() const { return frames_; }

    /// Name of the active app, or an empty view if none is (a dropped app leaves
    /// none until the launcher exists).
    std::string_view activeName() const;

private:
    void activate(std::size_t index);
    void deactivate();
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
    App *active_ = nullptr;
    Surface frame_;
    unsigned long frames_ = 0;
};

} // namespace matrixos
