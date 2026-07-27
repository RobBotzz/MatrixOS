// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/shell.h"

#include "hal/display.h"
#include "hal/input.h"

#include <chrono>
#include <thread>
#include <utility>

namespace matrixos
{

Shell::Shell(Display &display, Input &input, Duration targetFrameTime)
    : display_(display), input_(input), target_frame_time_(targetFrameTime),
      frame_(display.width(), display.height())
{
}

void Shell::add(std::unique_ptr<App> app)
{
    if (app == nullptr)
    {
        return;
    }
    logInfo("registered app '{}'", app->name());
    apps_.push_back(std::move(app));
}

std::string_view Shell::activeName() const
{
    return active_ != nullptr ? active_->name() : std::string_view{};
}

void Shell::run(const std::function<bool()> &shouldStop)
{
    using Clock = std::chrono::steady_clock;

    if (apps_.empty())
    {
        logError("no apps registered, nothing to run");
        return;
    }

    activate(0);

    auto previous = Clock::now();
    while (!shouldStop())
    {
        const auto frame_start = Clock::now();
        const Duration dt = frame_start - previous;
        previous = frame_start;

        for (const InputEvent &event : input_.poll())
        {
            dispatch(event);
        }

        // Apps get a clean surface, so none of them has to remember to clear.
        frame_.clear();
        if (active_ != nullptr && guarded("update", [this, dt] { active_->update(dt); }))
        {
            guarded("render", [this] { active_->render(frame_); });
        }

        display_.present(frame_);
        ++frames_;

        if (target_frame_time_ > Duration::zero())
        {
            const Duration elapsed = Clock::now() - frame_start;
            if (elapsed < target_frame_time_)
            {
                std::this_thread::sleep_for(target_frame_time_ - elapsed);
            }
        }
    }

    deactivate();
    display_.clear();
}

void Shell::activate(std::size_t index)
{
    deactivate();

    active_ = apps_[index].get();
    logInfo("activating '{}'", active_->name());
    guarded("onEnter", [this] { active_->onEnter(); });
}

void Shell::deactivate()
{
    if (active_ == nullptr)
    {
        return;
    }

    // Cleared before the call so a throwing onExit cannot land us back in here.
    App *const leaving = active_;
    active_ = nullptr;
    guarded("onExit", [leaving] { leaving->onExit(); });
}

void Shell::dispatch(const InputEvent &event)
{
    if (event.type == InputType::Home)
    {
        // Reserved for the shell and never forwarded to an app (FR-16). The
        // launcher takes this slot; until it exists there is nowhere to go.
        // Logged at info level on purpose: a silent no-op is indistinguishable
        // from a key that never arrived.
        logInfo("home pressed — no launcher yet, nothing to switch to");
        return;
    }

    if (active_ != nullptr)
    {
        guarded("onInput", [this, &event] { active_->onInput(event); });
    }
}

void Shell::reportFailure(const char *stage, const char *what)
{
    logError("app '{}' failed in {}: {} — dropping it", activeName(), stage, what);

    // Dropped without onExit: an app that just threw cannot be trusted to clean
    // up after itself. The screen goes dark until the launcher can take over.
    active_ = nullptr;
}

} // namespace matrixos
