// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "gfx/surface.h"
#include "os/app.h"

#include <cstdint>
#include <optional>

namespace matrixos
{

class TimeProvider;

/// The world with day and night on it, the sun where it stands overhead. Rotate
/// moves the time an hour per detent, press returns to now.
class WorldMapApp : public App
{
public:
    static constexpr int kMaxOffsetHours = 72;

    explicit WorldMapApp(const TimeProvider &time);

    std::string_view name() const override { return "World"; }

    void onInput(const InputEvent &event) override;
    void update(Duration dt) override;
    void render(Surface &surface) override;

    int offsetHours() const { return offset_hours_; }

private:
    void shade(std::int64_t unix_seconds);

    const TimeProvider &time_;
    int offset_hours_ = 0;
    bool known_ = false;
    std::optional<std::int64_t> shaded_for_;
    Surface map_;
    int sun_x_ = -1;
    int sun_y_ = -1;
};

} // namespace matrixos
