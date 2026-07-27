// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/shell.h"

#include "hal/display.h"
#include "hal/input.h"
#include "os/launcher.h"

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

bool Shell::launcherActive() const
{
    return active_ != nullptr && active_ == launcher_.get();
}

void Shell::run(const std::function<bool()> &shouldStop)
{
    using Clock = std::chrono::steady_clock;

    if (apps_.empty())
    {
        logError("no apps registered, nothing to run");
        return;
    }

    buildLauncher();

    // Boots into the first app rather than into the launcher, so the panel shows
    // something alive immediately. FR-19 — restore whatever ran last — will
    // replace this fixed choice once state can be persisted.
    activateApp(0);

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
        applyPending();

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

void Shell::buildLauncher()
{
    std::vector<std::string_view> names;
    names.reserve(apps_.size());
    for (const auto &app : apps_)
    {
        names.push_back(app->name());
    }

    launcher_ = std::make_unique<Launcher>(std::move(names),
                                           [this](std::size_t index)
                                           {
                                               pending_ = Target::App;
                                               pending_index_ = index;
                                           });
}

void Shell::applyPending()
{
    const Target target = pending_;
    pending_ = Target::None;

    switch (target)
    {
    case Target::App:
        activateApp(pending_index_);
        break;
    case Target::Launcher:
        activateLauncher();
        break;
    case Target::None:
        break;
    }
}

void Shell::activateApp(std::size_t index)
{
    if (index >= apps_.size())
    {
        logError("no app at index {}", index);
        return;
    }

    last_app_ = index;
    setActive(apps_[index].get());
}

void Shell::activateLauncher()
{
    setActive(launcher_.get());
}

void Shell::setActive(App *app)
{
    deactivate();

    active_ = app;
    if (active_ == nullptr)
    {
        return;
    }

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

void Shell::requestHome()
{
    if (launcherActive())
    {
        if (!last_app_.has_value())
        {
            logInfo("home pressed, but no app has run yet");
            return;
        }
        pending_ = Target::App;
        pending_index_ = *last_app_;
        return;
    }

    pending_ = Target::Launcher;
}

void Shell::dispatch(const InputEvent &event)
{
    if (event.type == InputType::Home)
    {
        requestHome();
        return;
    }

    if (active_ != nullptr)
    {
        guarded("onInput", [this, &event] { active_->onInput(event); });
    }
}

void Shell::reportFailure(const char *stage, const char *what)
{
    const bool was_launcher = launcherActive();
    logError("app '{}' failed in {}: {} — dropping it", activeName(), stage, what);

    // Dropped without onExit: an app that just threw cannot be trusted to clean
    // up after itself.
    active_ = nullptr;

    if (!was_launcher)
    {
        // FR-17 in full: the user lands in the launcher, not at a black screen.
        pending_ = Target::Launcher;
    }
}

} // namespace matrixos
