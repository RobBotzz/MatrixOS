// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/scores/scoreboard.h"

#include "os/state.h"

#include <algorithm>

namespace matrixos
{
namespace
{

constexpr std::string_view kLastName = "last_name";

std::string nameKey(std::size_t place)
{
    return "name" + std::to_string(place);
}

std::string scoreKey(std::size_t place)
{
    return "score" + std::to_string(place);
}

} // namespace

Scoreboard::Scoreboard(StateSection &saved) : saved_(saved)
{
    for (std::size_t place = 0; place < kSize; ++place)
    {
        const int score = saved_.getInt(scoreKey(place), 0);
        if (score <= 0)
        {
            break;
        }
        entries_.push_back({saved_.getString(nameKey(place), "???"), score});
    }
}

bool Scoreboard::qualifies(int score) const
{
    return score > 0 && (entries_.size() < kSize || score > entries_.back().score);
}

std::size_t Scoreboard::add(std::string_view name, int score)
{
    const auto place = std::find_if(entries_.begin(), entries_.end(),
                                    [score](const ScoreEntry &e) { return e.score < score; });
    const auto index = static_cast<std::size_t>(place - entries_.begin());
    entries_.insert(place, {std::string(name), score});
    if (entries_.size() > kSize)
    {
        entries_.pop_back();
    }
    saved_.setString(kLastName, name);
    save();
    return index;
}

std::string Scoreboard::lastName() const
{
    return saved_.getString(kLastName, "");
}

void Scoreboard::save()
{
    for (std::size_t place = 0; place < entries_.size(); ++place)
    {
        saved_.setString(nameKey(place), entries_[place].name);
        saved_.setInt(scoreKey(place), entries_[place].score);
    }
    saved_.save();
}

} // namespace matrixos
