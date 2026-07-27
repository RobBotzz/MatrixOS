// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "hal/gestures.h"

namespace matrixos
{
namespace
{

bool wantsDoublePress(const GestureRecognizer::Timing &timing)
{
    return timing.double_press_window > std::chrono::milliseconds::zero();
}

} // namespace

GestureRecognizer::GestureRecognizer() : GestureRecognizer(Timing{}) {}

GestureRecognizer::GestureRecognizer(Timing timing) : timing_(timing) {}

std::vector<InputEvent> GestureRecognizer::onButtonChange(bool pressed, InputTime when)
{
    std::vector<InputEvent> events;

    if (pressed == down_)
    {
        return events;
    }

    // Measured from the last accepted change, so a burst of bounce collapses.
    if (last_change_.has_value() && when - *last_change_ < timing_.debounce)
    {
        return events;
    }

    down_ = pressed;
    last_change_ = when;

    if (pressed)
    {
        if (state_ == State::PendingPress && wantsDoublePress(timing_))
        {
            // Replaces the withheld Press rather than adding to it.
            events.push_back({InputType::DoublePress, 0});
            state_ = State::Consumed;
            return events;
        }

        pressed_at_ = when;
        state_ = State::Down;
        return events;
    }

    switch (state_)
    {
    case State::Down:
        if (wantsDoublePress(timing_))
        {
            released_at_ = when;
            state_ = State::PendingPress;
        }
        else
        {
            events.push_back({InputType::Press, 0});
            state_ = State::Idle;
        }
        break;

    case State::Consumed:
        state_ = State::Idle;
        break;

    case State::Idle:
    case State::PendingPress:
        // Unreachable: a second release is caught by the check above.
        state_ = State::Idle;
        break;
    }

    return events;
}

std::vector<InputEvent> GestureRecognizer::tick(InputTime now)
{
    std::vector<InputEvent> events;

    if (state_ == State::Down && now - pressed_at_ >= timing_.long_press)
    {
        events.push_back({InputType::LongPress, 0});
        state_ = State::Consumed;
    }
    else if (state_ == State::PendingPress && now - released_at_ >= timing_.double_press_window)
    {
        events.push_back({InputType::Press, 0});
        state_ = State::Idle;
    }

    return events;
}

} // namespace matrixos
