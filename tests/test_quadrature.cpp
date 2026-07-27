// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "hal/quadrature.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

using namespace matrixos;

namespace
{

/// One detent clockwise is the Gray-code ring 00 → 01 → 11 → 10 → 00.
constexpr std::array<std::pair<bool, bool>, 4> kClockwise = {{
    {false, true},  // 01
    {true, true},   // 11
    {true, false},  // 10
    {false, false}, // 00
}};

/// Counter-clockwise walks the same ring the other way: 00 → 10 → 11 → 01 → 00.
///
/// Spelled out rather than derived by reversing the array above, which is the
/// mistake that made this test fail the first time: a reversed list starts at the
/// resting position instead of leaving it, so only three of the four transitions
/// actually happen and the detent never completes.
constexpr std::array<std::pair<bool, bool>, 4> kCounterClockwise = {{
    {true, false},  // 10
    {true, true},   // 11
    {false, true},  // 01
    {false, false}, // 00
}};

/// Feeds a sequence of line states and returns the detents that came out.
int totalDetents(QuadratureDecoder &decoder, const std::vector<std::pair<bool, bool>> &states)
{
    int total = 0;
    for (const auto &[a, b] : states)
    {
        total += decoder.update(a, b);
    }
    return total;
}

std::vector<std::pair<bool, bool>> turns(int detents)
{
    const auto &ring = detents > 0 ? kClockwise : kCounterClockwise;
    const int count = detents > 0 ? detents : -detents;

    std::vector<std::pair<bool, bool>> states;
    for (int i = 0; i < count; ++i)
    {
        states.insert(states.end(), ring.begin(), ring.end());
    }
    return states;
}

} // namespace

TEST_CASE("a full cycle clockwise is one detent forwards")
{
    QuadratureDecoder decoder;
    decoder.update(false, false); // resting position

    CHECK(totalDetents(decoder, turns(1)) == +1);
}

TEST_CASE("a full cycle the other way is one detent backwards")
{
    QuadratureDecoder decoder;
    decoder.update(false, false);

    CHECK(totalDetents(decoder, turns(-1)) == -1);
}

TEST_CASE("ten detents produce exactly ten, in the right direction")
{
    // This is acceptance criterion 4 of v0.1, as logic rather than as feel.
    QuadratureDecoder decoder;
    decoder.update(false, false);

    CHECK(totalDetents(decoder, turns(10)) == +10);
    CHECK(totalDetents(decoder, turns(10)) == +10);
    CHECK(totalDetents(decoder, turns(-20)) == -20);
}

TEST_CASE("a partial turn yields nothing until the detent completes")
{
    QuadratureDecoder decoder;
    decoder.update(false, false);

    CHECK(decoder.update(false, true) == 0); // 00 -> 01
    CHECK(decoder.update(true, true) == 0);  // 01 -> 11
    CHECK(decoder.midDetent());
    CHECK(decoder.update(true, false) == 0);  // 11 -> 10
    CHECK(decoder.update(false, false) == 1); // 10 -> 00, detent complete
    CHECK_FALSE(decoder.midDetent());
}

TEST_CASE("bounce cancels itself out instead of counting as movement")
{
    QuadratureDecoder decoder;
    decoder.update(false, false);

    // The A line chatters: forwards and back, several times.
    for (int i = 0; i < 5; ++i)
    {
        CHECK(decoder.update(false, true) == 0);
        CHECK(decoder.update(false, false) == 0);
    }
    CHECK_FALSE(decoder.midDetent());

    // A real turn afterwards still registers correctly.
    CHECK(totalDetents(decoder, turns(1)) == +1);
}

TEST_CASE("reversing halfway through a detent does not produce a step")
{
    QuadratureDecoder decoder;
    decoder.update(false, false);

    decoder.update(false, true); // half a detent forwards
    decoder.update(true, true);
    decoder.update(false, true); // and back again
    decoder.update(false, false);

    CHECK_FALSE(decoder.midDetent());
}

TEST_CASE("an impossible transition is discarded as a glitch")
{
    QuadratureDecoder decoder;
    decoder.update(false, false);

    // Both lines changing at once cannot happen on a real encoder.
    CHECK(decoder.update(true, true) == 0);
    CHECK_FALSE(decoder.midDetent());
}

TEST_CASE("repeating the same state changes nothing")
{
    QuadratureDecoder decoder;
    decoder.update(false, false);

    for (int i = 0; i < 10; ++i)
    {
        CHECK(decoder.update(false, false) == 0);
    }
    CHECK_FALSE(decoder.midDetent());
}

TEST_CASE("an encoder with two cycles per detent can be configured")
{
    QuadratureDecoder decoder(8);
    decoder.update(false, false);

    CHECK(totalDetents(decoder, turns(1)) == 0); // one cycle is only half a detent
    CHECK(totalDetents(decoder, turns(1)) == 1); // the second completes it
}

TEST_CASE("the first reading only establishes the resting position")
{
    QuadratureDecoder decoder;

    // Starting from 11 rather than 00 must not be read as movement.
    CHECK(decoder.update(true, true) == 0);
    CHECK_FALSE(decoder.midDetent());
}
