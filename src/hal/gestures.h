// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "hal/input.h"

#include <chrono>
#include <optional>
#include <vector>

namespace matrixos
{

/// steady_clock is CLOCK_MONOTONIC on Linux, so kernel edge timestamps can be
/// handed in unconverted.
using InputClock = std::chrono::steady_clock;
using InputTime = InputClock::time_point;

/// Turns a button's raw transitions into gestures. Takes the time as an argument
/// instead of reading a clock, which is what makes press timing testable.
class GestureRecognizer
{
public:
    struct Timing
    {
        /// Fires while the button is still down; the release then stays silent
        /// (FR-10).
        std::chrono::milliseconds long_press{600};

        /// Transitions closer together than this are switch bounce.
        std::chrono::milliseconds debounce{10};

        /// Zero disables `DoublePress`. Any other value delays every `Press` by
        /// this long, since a single press can only be confirmed once the window
        /// has passed — see Q-4 in requirements.md.
        std::chrono::milliseconds double_press_window{0};
    };

    /// Two overloads because GCC cannot form `Timing{}` as a default argument
    /// before the enclosing class is complete.
    GestureRecognizer();
    explicit GestureRecognizer(Timing timing);

    std::vector<InputEvent> onButtonChange(bool pressed, InputTime when);

    /// Must be called every frame: a hold fires without any further edge, and a
    /// withheld press has to be confirmed.
    std::vector<InputEvent> tick(InputTime now);

    bool isDown() const { return down_; }

private:
    /// `Consumed`: the press already produced its event, so the release that
    /// follows must stay silent.
    enum class State
    {
        Idle,
        Down,
        Consumed,
        PendingPress,
    };

    Timing timing_;
    State state_ = State::Idle;
    bool down_ = false;
    std::optional<InputTime> last_change_;
    InputTime pressed_at_{};
    InputTime released_at_{};
};

} // namespace matrixos
