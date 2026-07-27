// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/app.h"

namespace matrixos
{

/// Focus/break cycle timer. Runs off the frame delta on purpose, not off
/// wall-clock time: that avoids C-9 and keeps it testable.
class PomodoroApp : public App
{
public:
    enum class Mode
    {
        Focus,
        Break,
    };

    enum class State
    {
        Setting,
        Running,
        Paused,
        Alarm,
    };

    static constexpr int kMinMinutes = 1;
    static constexpr int kMaxMinutes = 60;
    static constexpr int kDefaultFocusMinutes = 25;
    static constexpr int kDefaultBreakMinutes = 5;

    std::string_view name() const override { return "Pomodoro"; }

    void onInput(const InputEvent &event) override;
    void update(Duration dt) override;
    void render(Surface &surface) override;

    Mode mode() const { return mode_; }
    State state() const { return state_; }
    int focusMinutes() const { return focus_minutes_; }
    int breakMinutes() const { return break_minutes_; }
    float remainingSeconds() const { return remaining_; }

private:
    void onPress();
    void startTimer(Mode mode);
    void reset();
    int currentMinutes() const;

    Mode mode_ = Mode::Focus;
    State state_ = State::Setting;

    int focus_minutes_ = kDefaultFocusMinutes;
    int break_minutes_ = kDefaultBreakMinutes;

    float remaining_ = 0.0F;
    float blink_ = 0.0F;
    float alarm_elapsed_ = 0.0F;
};

} // namespace matrixos
