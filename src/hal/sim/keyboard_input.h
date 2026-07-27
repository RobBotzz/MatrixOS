// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "hal/input.h"

#include <termios.h>

namespace matrixos
{

/// Keyboard stand-in for the encoder and the home button (FR-11), so apps are
/// usable on the development machine.
///
/// | key                  | event                        |
/// |----------------------|------------------------------|
/// | right / up arrow     | `Rotate(+1)`                 |
/// | left / down arrow    | `Rotate(-1)`                 |
/// | space, enter         | `Press`                      |
/// | `h`                  | `Home`                       |
/// | `d`                  | `DoublePress`                |
/// | `l`                  | `LongPress`                  |
///
/// `d` and `l` keep their own keys rather than coming from press timing: a raw-mode
/// terminal reports no key releases, and autorepeat starts too late to tell a hold
/// from a tap without delaying every press.
///
/// If stdin is not a terminal — as a systemd service, for instance — this
/// degrades to producing no events instead of failing.
class KeyboardInput : public Input
{
public:
    KeyboardInput();
    ~KeyboardInput() override;

    std::vector<InputEvent> poll() override;

private:
    termios original_{};
    bool raw_mode_ = false;
};

} // namespace matrixos
