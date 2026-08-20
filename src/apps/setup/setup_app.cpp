// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/setup/setup_app.h"

#include "gfx/font.h"
#include "gfx/surface.h"

#include <cmath>
#include <string>
#include <utility>

namespace matrixos
{
namespace
{

constexpr Color kHeading{255, 190, 0};
constexpr Color kText{255, 255, 255};
constexpr Color kMuted{130, 145, 175};
constexpr Color kGood{42, 224, 112};
constexpr Color kBad{255, 84, 104};

constexpr int kHeadingTop = 2;
constexpr int kNameTop = 11;
constexpr int kSuffixTop = 19; // scale 2, so it occupies rows 19 to 32
constexpr int kSuffixScale = 2;

constexpr int kStateTop = 5;
constexpr int kDetailTop = 16;

// The connected screen carries a third line, so it sits a little higher.
constexpr int kConnectedTop = 3;
constexpr int kConnectedSsidTop = 13;
constexpr int kConnectedIdTop = 23;
constexpr int kDotsTop = 26;
constexpr int kDotSpacing = 6;
constexpr int kDotCount = 3;

constexpr float kDotPeriod = 0.35F;

void drawCentered(Surface &surface, int y, std::string_view text, Color color, int scale = 1)
{
    drawTextCentered(surface, y, text, color, scale);
}

/// Cuts a name to what the panel can show. Someone else chose this string and it
/// can be any length; 64 pixels is 10 characters at scale 1.
std::string fit(std::string_view text)
{
    constexpr std::size_t kMaxCharacters = 10;
    return std::string(text.substr(0, kMaxCharacters));
}

void drawWalkingDots(Surface &surface, int y, float phase)
{
    const int lit = static_cast<int>(phase / kDotPeriod) % kDotCount;
    const int left = (surface.width() - (kDotCount - 1) * kDotSpacing) / 2;

    for (int dot = 0; dot < kDotCount; ++dot)
    {
        const Color color = dot == lit ? kHeading : kMuted;
        const int x = left + dot * kDotSpacing;
        surface.setPixel(x, y, color);
        surface.setPixel(x + 1, y, color);
        surface.setPixel(x, y + 1, color);
        surface.setPixel(x + 1, y + 1, color);
    }
}

} // namespace

SetupApp::SetupApp(const Provisioning &provisioning, Identity identity)
    : provisioning_(provisioning), identity_(std::move(identity))
{
}

void SetupApp::onEnter()
{
    status_ = provisioning_.status();
    animation_ = 0.0F;
}

void SetupApp::update(Duration dt)
{
    animation_ = std::fmod(animation_ + dt.count(), kDotPeriod * kDotCount);
    status_ = provisioning_.status();
}

void SetupApp::render(Surface &surface)
{
    switch (status_.state)
    {
    case SetupState::AccessPoint:
    case SetupState::Failed:
    {
        // The three questions in the order they are asked: what do I do, what is
        // it called, which one is mine. Only the last part differs between
        // units, so it gets the double-height type (Q-9).
        const bool failed = status_.state == SetupState::Failed;
        drawCentered(surface, kHeadingTop, failed ? "TRY AGAIN" : "JOIN WIFI",
                     failed ? kBad : kHeading);
        drawCentered(surface, kNameTop, "MatrixOS", kMuted);
        drawCentered(surface, kSuffixTop, identity_.suffix, kText, kSuffixScale);
        break;
    }

    case SetupState::Connecting:
        drawCentered(surface, kStateTop, "CONNECTING", kHeading);
        drawCentered(surface, kDetailTop, fit(status_.ssid), kText);
        drawWalkingDots(surface, kDotsTop, animation_);
        break;

    case SetupState::Waiting:
        drawCentered(surface, kStateTop, "SEARCHING", kHeading);
        drawCentered(surface, kDetailTop, fit(status_.ssid), kMuted);
        drawWalkingDots(surface, kDotsTop, animation_);
        break;

    case SetupState::Connected:
        drawCentered(surface, kConnectedTop, "CONNECTED", kGood);
        drawCentered(surface, kConnectedSsidTop, fit(status_.ssid), kText);

        // Which unit this is, and therefore how to reach it: the device answers
        // to matrixos-<suffix>. Without this line the name is only ever visible
        // in setup mode, which is exactly when nobody needs it — and invisible
        // once online, which is exactly when somebody does.
        drawCentered(surface, kConnectedIdTop, identity_.suffix, kMuted);
        break;

    case SetupState::Unmanaged:
        // Reachable only from the launcher — the shell never forces this screen.
        drawCentered(surface, kStateTop, "WIFI", kMuted);
        drawCentered(surface, kDetailTop, "NOT SET UP", kMuted);
        break;
    }
}

} // namespace matrixos
