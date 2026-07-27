// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

namespace matrixos
{

/// Turns the two phase-shifted signals of a rotary encoder into detents.
/// Hardware-free, so direction and bounce handling are unit-testable.
class QuadratureDecoder
{
public:
    /// Most mechanical encoders complete four transitions per detent.
    explicit QuadratureDecoder(int stepsPerDetent = 4);

    /// Returns +1 or -1 once a detent is complete, 0 while it is in progress.
    /// Bounce cancels itself out, and steps that skip a position are discarded as
    /// glitches — a real encoder cannot change both lines at once.
    int update(bool a, bool b);

    bool midDetent() const { return accumulator_ != 0; }

private:
    int steps_per_detent_;
    int state_ = 0;
    int accumulator_ = 0;
    bool started_ = false;
};

} // namespace matrixos
