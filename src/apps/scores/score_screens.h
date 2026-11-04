// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "apps/scores/scoreboard.h"
#include "hal/input.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace matrixos
{

class StateSection;
class Surface;

/// What the games with a scoreboard share: the board itself, three rows at a
/// time, and the name entry after a good run.
class ScoreScreens
{
public:
    explicit ScoreScreens(StateSection &saved);

    const Scoreboard &board() const { return board_; }
    const std::string &entry() const { return entry_; }
    std::size_t cursor() const { return cursor_; }

    /// Back to the top of the board, nothing highlighted.
    void rewind();
    void scroll(int delta);

    void beginEntry(int score);

    /// Rotate changes the letter, press moves on, and the last press or a long
    /// press finishes. True once the entry is on the board.
    bool onEntryInput(const InputEvent &event);

    /// Puts a score on the board under the last name, without asking.
    void addUnasked(int score);

    void renderBoard(Surface &surface) const;
    void renderEntry(Surface &surface, float seconds, std::string_view title) const;

private:
    void turnLetter(int delta);
    void finishEntry();

    Scoreboard board_;
    std::size_t scroll_ = 0;
    std::optional<std::size_t> highlight_;

    std::string entry_;
    std::size_t cursor_ = 0;
    int score_ = 0;
};

} // namespace matrixos
