// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "hal/input.h"

#include <chrono>
#include <string_view>

namespace matrixos
{

class Surface;

/// Elapsed time in seconds. Apps get measured time rather than a fixed step, so
/// behaviour does not depend on the frame rate actually achieved (NFR-2).
using Duration = std::chrono::duration<float>;

/// One thing the device can show.
///
/// An app is a plain C++ object, not a process — there is one process and, for
/// our own code, one thread (ADR-0003). The shell calls these methods in order
/// on the active app and on nobody else.
///
/// The one hard rule: `update()` and `render()` must return within the frame
/// budget. No sleeping, no blocking I/O, no waiting on the network. An app that
/// blocks freezes the whole device, and the exception boundary catches crashes,
/// not hangs. Anything from outside arrives through an object passed in at
/// construction (FR-26).
class App
{
public:
    virtual ~App() = default;

    virtual std::string_view name() const = 0;

    /// Becoming the active app.
    virtual void onEnter() {}

    /// Leaving. Release anything that should not outlive being on screen.
    virtual void onExit() {}

    /// One input event. Never called while inactive.
    virtual void onInput(const InputEvent &event) { (void) event; }

    /// Advance state by `dt`. Called once per frame while active.
    virtual void update(Duration dt) { (void) dt; }

    /// Draw the current state. The surface is already cleared.
    virtual void render(Surface &surface) = 0;
};

} // namespace matrixos
