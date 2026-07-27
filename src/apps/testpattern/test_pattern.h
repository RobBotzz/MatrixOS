// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/app.h"

namespace matrixos
{

/// A diagnostic frame: border, corner marker and three colour gradients.
///
/// Wrong panel geometry shows up as a broken border, a mirrored or rotated panel
/// as a misplaced marker, and a swapped channel order as the wrong colour — all
/// in one look. With several devices to build, every panel needs checking once
/// (ADR-0007), which is why this is a normal app and reachable from the launcher
/// rather than only a startup flag.
class TestPatternApp : public App
{
public:
    std::string_view name() const override { return "Test Card"; }

    void render(Surface &surface) override;
};

} // namespace matrixos
