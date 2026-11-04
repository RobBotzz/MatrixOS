// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace matrixos
{

class StateSection;

struct ScoreEntry
{
    std::string name;
    int score = 0;
};

/// The best streaks, highest first, kept in the state store.
class Scoreboard
{
public:
    static constexpr std::size_t kSize = 10;
    static constexpr std::size_t kNameLength = 5;

    explicit Scoreboard(StateSection &saved);

    const std::vector<ScoreEntry> &entries() const { return entries_; }

    /// Zero never qualifies; a tie with the last place of a full list does not.
    bool qualifies(int score) const;

    /// Returns the entry's place, counted from 0. Among equal scores the older
    /// entry stays ahead.
    std::size_t add(std::string_view name, int score);

    /// The name entered last, to start the next entry from.
    std::string lastName() const;

private:
    void save();

    StateSection &saved_;
    std::vector<ScoreEntry> entries_;
};

} // namespace matrixos
