// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "apps/scores/score_screens.h"
#include "os/app.h"
#include "os/pokemon.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace matrixos
{

class StateStore;

/// Who's that Pokémon, in the look of the show. The scoreboard comes first; a
/// press starts a run. Rotate picks one of four names, press answers. A wrong
/// answer ends the run, and a score good enough for the board asks for a name.
class PokemonApp : public App
{
public:
    enum class Screen
    {
        Scores,
        Guess,
        Result,
        EnterName,
    };

    PokemonApp(PokemonService &pokemon, StateStore &store);

    std::string_view name() const override { return "Pokemon"; }
    bool interruptible() const override { return false; }

    void onEnter() override;
    void onExit() override;
    void onInput(const InputEvent &event) override;
    void update(Duration dt) override;
    void render(Surface &surface) override;

    Screen screen() const { return screen_; }
    const std::optional<PokemonRound> &round() const { return round_; }
    int selected() const { return selected_; }
    int score() const { return score_; }
    const std::string &entry() const { return scores_.entry(); }
    std::size_t cursor() const { return scores_.cursor(); }
    const Scoreboard &scoreboard() const { return scores_.board(); }

private:
    void start();
    void nextRound();
    void answer();
    void endRun();

    void renderBurst(Surface &surface) const;
    void renderRound(Surface &surface) const;

    PokemonService &pokemon_;
    ScoreScreens scores_;
    Screen screen_ = Screen::Scores;

    std::optional<PokemonRound> round_;
    int selected_ = 0;
    int score_ = 0;
    bool right_ = false;

    float time_ = 0.0F;
    float since_answer_ = 0.0F;
    float since_poll_ = 0.0F;

    std::vector<float> turn_;     ///< per pixel: where around the burst, 0 to 1
    std::vector<float> distance_; ///< per pixel: from the burst, leaning right
    std::vector<std::uint8_t> reach_;
};

} // namespace matrixos
