// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/app.h"

namespace matrixos
{

class TimeProvider;

/// The wall clock, and the default app of a fresh unit (ADR-0015).
///
/// It reads no clock of its own (FR-26): the provider is handed in, which is
/// what makes "the time is unknown", "midnight rolls over" and "the zone
/// changed" ordinary unit tests instead of things one waits for.
///
/// The first screen many owners ever see is this app saying it does not know the
/// time yet, because a device without a network cannot know it (C-9). That
/// screen is therefore designed, not an error state.
class ClockApp : public App
{
public:
    enum class Face
    {
        Date,    ///< time, and the day below it
        Seconds, ///< time, and the minute as a bar
        Bare,    ///< time alone, filling the panel
    };

    explicit ClockApp(const TimeProvider &time);

    std::string_view name() const override { return "Clock"; }

    void onInput(const InputEvent &event) override;
    void update(Duration dt) override;
    void render(Surface &surface) override;

    Face face() const { return face_; }

    /// What the panel is entitled to show. False means the device has never had
    /// a network since it was switched on.
    bool timeKnown() const { return known_; }

private:
    const TimeProvider &time_;
    Face face_ = Face::Date;
    bool known_ = false;
    float blink_ = 0.0F;
};

} // namespace matrixos
