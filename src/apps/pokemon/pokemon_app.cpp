// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/pokemon/pokemon_app.h"

#include "gfx/font.h"
#include "gfx/surface.h"
#include "os/state.h"

#include <algorithm>
#include <string>

namespace matrixos
{
namespace
{

constexpr std::string_view kSection = "pokemon";
constexpr std::string_view kBestKey = "best";

constexpr Color kBackdropTop{255, 210, 40};
constexpr Color kBackdropBottom{40, 120, 255};
constexpr Color kSilhouette{0, 0, 0};
constexpr Color kName{255, 255, 255};
constexpr Color kRight{40, 230, 90};
constexpr Color kWrong{255, 60, 60};
constexpr Color kDim{70, 78, 96};
constexpr Color kTitle{255, 210, 40};

constexpr int kSprite = PokemonService::kSprite;
constexpr int kSpriteLeft = (64 - kSprite) / 2;
constexpr int kBackdropMargin = 2;
constexpr int kNameTop = 25;
constexpr float kPollInterval = 0.25F;

Color blend(Color a, Color b, int step, int steps)
{
    const auto channel = [step, steps](std::uint8_t from, std::uint8_t to)
    { return static_cast<std::uint8_t>(from + (to - from) * step / steps); };
    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b)};
}

} // namespace

PokemonApp::PokemonApp(PokemonService &pokemon, StateStore &store)
    : pokemon_(pokemon), saved_(store.section(kSection))
{
    best_ = saved_.getInt(kBestKey, 0);
}

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
    round_ = pokemon_.take();
    selected_ = 0;
    revealed_ = false;
}

void PokemonApp::onInput(const InputEvent &event)
{
    if (!round_)
    {
        return;
    }

    if (event.type == InputType::Rotate && !revealed_)
    {
        selected_ = ((selected_ + event.delta) % 4 + 4) % 4;
    }
    else if (event.type == InputType::Press)
    {
        if (revealed_)
        {
            next();
            return;
        }
        revealed_ = true;
        if (selected_ == round_->answer)
        {
            ++streak_;
            if (streak_ > best_)
            {
                best_ = streak_;
                saved_.setInt(kBestKey, best_);
                saved_.save();
            }
        }
        else
        {
            streak_ = 0;
        }
    }
}

void PokemonApp::update(Duration dt)
{
    if (round_)
    {
        return;
    }
    since_poll_ += dt.count();
    if (since_poll_ >= kPollInterval)
    {
        since_poll_ = 0.0F;
        next();
    }
}

void PokemonApp::render(Surface &surface)
{
    if (!round_)
    {
        drawTextCentered(surface, 7, "POKEMON", kTitle);
        drawTextCentered(surface, 18, pokemon_.offline() ? "OFFLINE" : "LOADING", kDim);
        return;
    }

    const int left = kSpriteLeft - kBackdropMargin;
    const int width = kSprite + 2 * kBackdropMargin;
    for (int y = 0; y < kSprite; ++y)
    {
        const Color row = blend(kBackdropTop, kBackdropBottom, y, kSprite - 1);
        for (int x = 0; x < width; ++x)
        {
            surface.setPixel(left + x, y, row);
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
            surface.setPixel(kSpriteLeft + x, y,
                             revealed_ ? Color{sprite[at], sprite[at + 1], sprite[at + 2]}
                                       : kSilhouette);
        }
    }

    drawText(surface, 1, 1, std::to_string(streak_), kName);

    if (revealed_)
    {
        const bool right = selected_ == round_->answer;
        drawTextCentered(surface, kNameTop,
                         round_->options[static_cast<std::size_t>(round_->answer)],
                         right ? kRight : kWrong);
        return;
    }

    drawTextCentered(surface, kNameTop, round_->options[static_cast<std::size_t>(selected_)],
                     kName);
    for (int i = 0; i < 4; ++i)
    {
        surface.setPixel(56 + i * 2, 1, i == selected_ ? kName : kDim);
    }
}

} // namespace matrixos
