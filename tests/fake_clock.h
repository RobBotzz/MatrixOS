// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/clock.h"

#include <string>

namespace matrixos::test
{

/// A clock that is told what time it is.
///
/// This is the whole reason `TimeProvider` exists (ADR-0015): a test that waited
/// for midnight to check the date rollover would not be a test.
class FakeClock : public TimeProvider
{
public:
    LocalTime time{2026, 7, 29, 14, 5, 30, 3}; // Wednesday
    bool synced = true;
    std::string zone;

    bool synchronized() const override { return synced; }
    LocalTime now() const override { return time; }
    void setTimeZone(std::string_view value) override { zone = value; }
};

} // namespace matrixos::test
