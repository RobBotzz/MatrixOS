// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/app.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace matrixos
{

class StateStore;
class StateSection;

/// Panel brightness and which app the device starts with (FR-25) — no time
/// zone, that stays with the clock until v0.4. Modal, like the Pomodoro:
/// rotating browses settings, a press edits the one on screen. Never touches
/// the HAL — brightness is a number written into the store, and the shell
/// applies it.
class SettingsApp : public App
{
public:
    enum class Page
    {
        Brightness,
        Startup,
    };

    /// `apps` are the startup choices; this app is deliberately not among them.
    SettingsApp(StateStore &store, std::vector<std::string_view> apps);

    std::string_view name() const override { return "Settings"; }

    void onEnter() override;
    void onExit() override;
    void onInput(const InputEvent &event) override;
    void update(Duration dt) override;
    void render(Surface &surface) override;

    Page page() const { return page_; }
    bool editing() const { return editing_; }
    int brightness() const { return brightness_; }

    std::string_view startup() const;

private:
    void changeValue(int detents);
    void writeThrough();

    StateSection &settings_;

    /// Index 0 is "Last app"; index i+1 is apps_[i].
    std::vector<std::string_view> apps_;
    std::size_t startup_index_ = 0;

    Page page_ = Page::Brightness;
    bool editing_ = false;
    int brightness_ = 0;
    float blink_ = 0.0F;
};

} // namespace matrixos
