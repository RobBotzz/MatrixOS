// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/app.h"
#include "os/pokemon.h"

#include <optional>

namespace matrixos
{

class StateSection;
class StateStore;

/// Who's that Pokémon: a silhouette and four names. Rotate picks a name, press
/// answers and reveals, press again for the next one. Keeps the best streak.
class PokemonApp : public App
{
public:
    PokemonApp(PokemonService &pokemon, StateStore &store);

    std::string_view name() const override { return "Pokemon"; }
    bool interruptible() const override { return false; }

    void onEnter() override;
    void onExit() override;
    void onInput(const InputEvent &event) override;
    void update(Duration dt) override;
    void render(Surface &surface) override;

    bool hasRound() const { return round_.has_value(); }
    const std::optional<PokemonRound> &round() const { return round_; }
    int selected() const { return selected_; }
    bool revealed() const { return revealed_; }
    int streak() const { return streak_; }
    int best() const { return best_; }

private:
    void next();

    PokemonService &pokemon_;
    StateSection &saved_;
    std::optional<PokemonRound> round_;
    int selected_ = 0;
    bool revealed_ = false;
    int streak_ = 0;
    int best_ = 0;
    float since_poll_ = 0.0F;
};

} // namespace matrixos
