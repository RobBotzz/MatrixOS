// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/pokemon/pokemon_app.h"

#include "gfx/font.h"
#include "gfx/surface.h"

#include <cmath>

namespace matrixos
{
namespace
{

constexpr Color kRayRed{200, 30, 40};
constexpr Color kRayBlue{30, 70, 200};
constexpr Color kSilhouette{0, 0, 0};
constexpr Color kYellow{255, 215, 0};
constexpr Color kOutline{20, 30, 110};
constexpr Color kFlash{255, 255, 255};
constexpr Color kDim{70, 78, 96};

constexpr int kSprite = PokemonService::kSprite;
constexpr int kSpriteLeft = 4;
constexpr int kSpriteTop = 1;
constexpr int kRays = 16;
constexpr float kSpinRadiansPerSecond = 0.25F;
constexpr float kFlashSeconds = 0.15F;
constexpr float kPollInterval = 0.25F;
constexpr int kQuestionScale = 3;
constexpr int kBandTop = 25;

constexpr float kPi = 3.14159265F;

/// Text with a one-pixel outline, which is how the show's lettering reads on
/// a busy background.
void drawOutlined(Surface &surface, int x, int y, std::string_view text, int scale)
{
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (dx != 0 || dy != 0)
            {
                drawText(surface, x + dx, y + dy, text, kOutline, scale);
            }
        }
    }
    drawText(surface, x, y, text, kYellow, scale);
}

} // namespace

PokemonApp::PokemonApp(PokemonService &pokemon) : pokemon_(pokemon) {}

void PokemonApp::onEnter()
{
    pokemon_.setActive(true);
    if (!round_)
    {
        next();
    }
}

void PokemonApp::onExit()
{
    pokemon_.setActive(false);
}

void PokemonApp::next()
{
    std::optional<PokemonRound> round = pokemon_.take();
    if (round || !round_)
    {
        round_ = std::move(round);
        revealed_ = false;
    }
    phase_ = 0.0F;
}

void PokemonApp::reveal()
{
    revealed_ = true;
    phase_ = 0.0F;
}

void PokemonApp::onInput(const InputEvent &event)
{
    if (event.type == InputType::Press && round_ && !revealed_)
    {
        reveal();
    }
    else if (event.type == InputType::Press || event.type == InputType::Rotate)
    {
        next();
    }
}

void PokemonApp::update(Duration dt)
{
    const float seconds = dt.count();
    spin_ += seconds;

    if (!round_)
    {
        since_poll_ += seconds;
        if (since_poll_ >= kPollInterval)
        {
            since_poll_ = 0.0F;
            next();
        }
        return;
    }

    phase_ += seconds;
    if (!revealed_ && phase_ >= kGuessSeconds)
    {
        reveal();
    }
    else if (revealed_ && phase_ >= kRevealSeconds)
    {
        next();
    }
}

void PokemonApp::render(Surface &surface)
{
    if (!round_)
    {
        drawTextCentered(surface, 7, "POKEMON", kYellow);
        drawTextCentered(surface, 18, pokemon_.offline() ? "OFFLINE" : "LOADING", kDim);
        return;
    }

    // Rays turning around the middle of the silhouette.
    const float cx = kSpriteLeft + kSprite / 2.0F;
    const float cy = kSpriteTop + kSprite / 2.0F;
    const bool flash = revealed_ && phase_ < kFlashSeconds;
    for (int y = 0; y < surface.height(); ++y)
    {
        for (int x = 0; x < surface.width(); ++x)
        {
            const float angle =
                std::atan2(y + 0.5F - cy, x + 0.5F - cx) + spin_ * kSpinRadiansPerSecond;
            const int ray = static_cast<int>(std::floor(angle / (2.0F * kPi) * kRays));
            surface.setPixel(x, y, flash ? kFlash : ((ray % 2 + 2) % 2 == 0 ? kRayRed : kRayBlue));
        }
    }

    const auto &sprite = round_->sprite;
    for (int y = 0; y < kSprite; ++y)
    {
        for (int x = 0; x < kSprite; ++x)
        {
            const auto at = static_cast<std::size_t>((y * kSprite + x) * 4);
            if (sprite[at + 3] == 0)
            {
                continue;
            }
            surface.setPixel(kSpriteLeft + x, kSpriteTop + y,
                             revealed_ ? Color{sprite[at], sprite[at + 1], sprite[at + 2]}
                                       : kSilhouette);
        }
    }

    if (!revealed_)
    {
        const int width = textWidth("?", kQuestionScale);
        drawOutlined(surface, 46 - width / 2, 5, "?", kQuestionScale);
        return;
    }

    for (int y = kBandTop; y < surface.height(); ++y)
    {
        for (int x = 0; x < surface.width(); ++x)
        {
            surface.setPixel(x, y, kSilhouette);
        }
    }
    drawText(surface, centredTextX(surface.width(), round_->name), kBandTop, round_->name, kYellow);
}

} // namespace matrixos
