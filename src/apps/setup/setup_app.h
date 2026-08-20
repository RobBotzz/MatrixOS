// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/app.h"
#include "os/identity.h"
#include "os/provisioning.h"

namespace matrixos
{

/// What the user is supposed to do next, on the panel (FR-35).
///
/// Setup is an ordinary app rather than a mode in the loop (ADR-0007): the shell
/// activates it while the device has something for the user to do, and it draws
/// into the same `Surface` as everything else. That is the whole advantage of
/// having a display — comparable devices do this behind a blinking LED.
///
/// It shows text and no QR code. A WiFi-join payload needs a version-2 symbol,
/// which with its quiet zone is 33x33 pixels on a 32-pixel panel; the reasoning
/// is with Q-9 in requirements.md.
class SetupApp : public App
{
public:
    SetupApp(const Provisioning &provisioning, Identity identity);

    std::string_view name() const override { return "Setup"; }

    void onEnter() override;
    void update(Duration dt) override;
    void render(Surface &surface) override;

    SetupState shown() const { return status_.state; }

private:
    const Provisioning &provisioning_;
    Identity identity_;
    SetupStatus status_;
    float animation_ = 0.0F;
};

} // namespace matrixos
