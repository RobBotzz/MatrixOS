// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "hal/quadrature.h"

namespace matrixos
{
namespace
{

/// Indexed by (previous << 2 | current), where a state is A<<1 | B. One direction
/// walks the ring 00 → 01 → 11 → 10 → 00, the other walks it backwards; zero
/// entries are steps that skip a position.
constexpr int kTransitions[16] = {
    0,  +1, -1, 0,  // from 00
    -1, 0,  0,  +1, // from 01
    +1, 0,  0,  -1, // from 10
    0,  -1, +1, 0,  // from 11
};

} // namespace

QuadratureDecoder::QuadratureDecoder(int stepsPerDetent)
    : steps_per_detent_(stepsPerDetent > 0 ? stepsPerDetent : 1)
{
}

int QuadratureDecoder::update(bool a, bool b)
{
    const int current = (a ? 2 : 0) | (b ? 1 : 0);

    if (!started_)
    {
        // Only establishes where the encoder is resting.
        state_ = current;
        started_ = true;
        return 0;
    }

    if (current == state_)
    {
        return 0;
    }

    const int step = kTransitions[(state_ << 2) | current];
    state_ = current;

    if (step == 0)
    {
        return 0;
    }

    accumulator_ += step;

    if (accumulator_ >= steps_per_detent_)
    {
        accumulator_ = 0;
        return +1;
    }
    if (accumulator_ <= -steps_per_detent_)
    {
        accumulator_ = 0;
        return -1;
    }

    return 0;
}

} // namespace matrixos
