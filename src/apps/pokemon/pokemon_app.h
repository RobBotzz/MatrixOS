// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/app.h"
#include "os/pokemon.h"

#include <optional>
#include <vector>

namespace matrixos
{

/// Who's that Pokémon, as the show did it: a black silhouette on turning rays
/// and a big yellow question mark, then the reveal in colour with the name. Not
/// a game — no score, no answers to pick. Press reveals or moves on, rotate
/// skips; both also happen by themselves.
class PokemonApp : public App
{
public:
    static constexpr float kGuessSeconds = 10.0F;
    static constexpr float kRevealSeconds = 8.0F;

    explicit PokemonApp(PokemonService &pokemon);

    std::string_view name() const override { return "Pokemon"; }

    void onEnter() override;
    void onExit() override;
    void onInput(const InputEvent &event) override;
    void update(Duration dt) override;
    void render(Surface &surface) override;

    const std::optional<PokemonRound> &round() const { return round_; }
    bool revealed() const { return revealed_; }

private:
    void next();
    void reveal();

    PokemonService &pokemon_;
    std::vector<float> angles_; ///< per pixel, around the silhouette
    std::optional<PokemonRound> round_;
    bool revealed_ = false;
    float phase_ = 0.0F; ///< seconds in the current guess or reveal
    float spin_ = 0.0F;  ///< seconds, for the turning rays
    float since_poll_ = 0.0F;
};

} // namespace matrixos
