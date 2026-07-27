// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/app.h"

namespace matrixos
{

/// Animated colour field — the first real app (FR-30).
///
/// Rotating switches between four variants, pressing freezes and unfreezes the
/// animation. Both are there to make the whole input path visible at a glance:
/// if rotating changes the picture, encoder, HAL, shell and app all work.
///
/// Needs nothing from outside — no network, no files, no clock of its own.
class PlasmaApp : public App
{
public:
    static constexpr int kVariantCount = 4;

    std::string_view name() const override { return "Plasma"; }

    void onInput(const InputEvent &event) override;
    void update(Duration dt) override;
    void render(Surface &surface) override;

    int variant() const { return variant_; }
    bool paused() const { return paused_; }

private:
    int variant_ = 0;
    bool paused_ = false;
    float seconds_ = 0.0F;
};

} // namespace matrixos
