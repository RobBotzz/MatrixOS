// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/plasma/plasma.h"

#include "gfx/surface.h"
#include "os/log.h"

#include <algorithm>
#include <cmath>

namespace matrixos
{
namespace
{

constexpr float kTwoThirdsPi = 2.0943951F; // 2 * pi / 3
constexpr float kFourThirdsPi = 4.1887902F;

std::uint8_t channel(float phase)
{
    const float unit = std::sin(phase) * 0.5F + 0.5F;
    return static_cast<std::uint8_t>(std::clamp(unit * 255.0F, 0.0F, 255.0F));
}

/// Maps a field value to a colour. The variant shifts the palette, so switching
/// variants changes both the pattern and its colours.
Color palette(float value, int variant)
{
    const float phase = value + static_cast<float>(variant) * 1.7F;
    return Color{channel(phase), channel(phase + kTwoThirdsPi), channel(phase + kFourThirdsPi)};
}

/// The scalar field itself. Kept in one place so a variant is a formula, not a
/// separate code path.
float field(int variant, float x, float y, float t)
{
    switch (variant)
    {
    case 1:
    {
        // Ripples spreading from a slowly circling centre.
        const float cx = 32.0F + 18.0F * std::sin(t * 0.4F);
        const float cy = 16.0F + 9.0F * std::cos(t * 0.3F);
        const float dx = x - cx;
        const float dy = y - cy;
        return std::sin(std::sqrt(dx * dx + dy * dy) * 0.45F - t * 2.0F) * 3.0F;
    }
    case 2:
    {
        // Stripes rotating around the centre of the panel.
        const float angle = t * 0.5F;
        return std::sin(((x - 32.0F) * std::cos(angle) + (y - 16.0F) * std::sin(angle)) * 0.28F) *
               3.0F;
    }
    case 3:
    {
        // Interference of two sources drifting in opposite directions.
        const float a = std::sin((x + t * 6.0F) * 0.22F) + std::sin((y - t * 4.0F) * 0.3F);
        const float b = std::sin((x - y + t * 5.0F) * 0.17F);
        return (a + b) * 1.6F;
    }
    default:
        // Classic plasma: three sines that never quite line up.
        return std::sin(x * 0.22F + t) + std::sin(y * 0.31F - t * 0.7F) +
               std::sin((x + y) * 0.15F + t * 1.3F);
    }
}

} // namespace

void PlasmaApp::onInput(const InputEvent &event)
{
    switch (event.type)
    {
    case InputType::Rotate:
    {
        // Wraps in both directions; C++ modulo of a negative value is negative.
        const int count = kVariantCount;
        variant_ = ((variant_ + event.delta) % count + count) % count;
        logInfo("plasma variant {}", variant_);
        break;
    }
    case InputType::Press:
        paused_ = !paused_;
        logInfo("plasma {}", paused_ ? "paused" : "running");
        break;
    default:
        break;
    }
}

void PlasmaApp::update(Duration dt)
{
    if (!paused_)
    {
        seconds_ += dt.count();
    }
}

void PlasmaApp::render(Surface &surface)
{
    for (int y = 0; y < surface.height(); ++y)
    {
        for (int x = 0; x < surface.width(); ++x)
        {
            const float value =
                field(variant_, static_cast<float>(x), static_cast<float>(y), seconds_);
            surface.setPixel(x, y, palette(value, variant_));
        }
    }
}

} // namespace matrixos
