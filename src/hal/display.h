// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

namespace matrixos
{

class Surface;

/// A destination for finished frames.
///
/// Two implementations exist by design (ADR-0002): the LED panel on the Pi and a
/// terminal simulator on the development machine. Only main.cpp knows which one
/// is in use.
class Display
{
public:
    virtual ~Display() = default;

    virtual int width() const = 0;
    virtual int height() const = 0;

    /// Hand over a finished frame. Returns once the frame has been committed,
    /// so a partially drawn frame is never visible (FR-3).
    virtual void present(const Surface &frame) = 0;

    /// Blank the output. Called on shutdown so the panel goes dark (FR-4).
    virtual void clear() = 0;
};

} // namespace matrixos
