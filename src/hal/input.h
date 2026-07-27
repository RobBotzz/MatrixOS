// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <vector>

namespace matrixos
{

/// The complete input vocabulary of the device (FR-8).
///
/// `Home` comes from the dedicated home button and is the only event the shell
/// consumes itself; everything else belongs to the active app. There is no
/// `VeryLongPress` — the home button made it unnecessary, which is why
/// `LongPress` can fire while the button is still held (ADR-0009).
enum class InputType
{
    Rotate,
    Press,
    DoublePress,
    LongPress,
    Home,
};

struct InputEvent
{
    InputType type;

    /// `Rotate` only: signed detents, normally +1 or -1.
    int delta = 0;
};

constexpr const char *inputTypeName(InputType type)
{
    switch (type)
    {
    case InputType::Rotate:
        return "Rotate";
    case InputType::Press:
        return "Press";
    case InputType::DoublePress:
        return "DoublePress";
    case InputType::LongPress:
        return "LongPress";
    case InputType::Home:
        return "Home";
    }
    return "?";
}

/// A source of input events.
///
/// Two implementations exist by design: the encoder plus home button on the Pi,
/// and a keyboard in the simulator (FR-11). Apps see only events, never GPIO.
class Input
{
public:
    virtual ~Input() = default;

    /// Non-blocking: returns everything that happened since the last call.
    virtual std::vector<InputEvent> poll() = 0;
};

} // namespace matrixos
