// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/scores/score_screens.h"

#include "gfx/font.h"
#include "gfx/surface.h"

#include <algorithm>

namespace matrixos
{
namespace
{

constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ ";

constexpr Color kYellow{255, 215, 0};
constexpr Color kText{255, 255, 255};
constexpr Color kWrong{255, 60, 60};
constexpr Color kDim{110, 120, 140};

constexpr int kPanelWidth = 64;
constexpr int kRowHeight = 8;
constexpr std::size_t kRows = 3;
constexpr float kBlinkSeconds = 0.4F;

} // namespace

ScoreScreens::ScoreScreens(StateSection &saved) : board_(saved) {}

void ScoreScreens::rewind()
{
    scroll_ = 0;
    highlight_.reset();
}

void ScoreScreens::scroll(int delta)
{
    const auto entries = board_.entries().size();
    const int last = entries > kRows ? static_cast<int>(entries - kRows) : 0;
    scroll_ = static_cast<std::size_t>(std::clamp(static_cast<int>(scroll_) + delta, 0, last));
}

void ScoreScreens::beginEntry(int score)
{
    score_ = score;
    entry_ = board_.lastName();
    entry_.resize(Scoreboard::kNameLength, ' ');
    cursor_ = 0;
}

bool ScoreScreens::onEntryInput(const InputEvent &event)
{
    const bool press = event.type == InputType::Press;
    if (event.type == InputType::Rotate)
    {
        turnLetter(event.delta);
    }
    else if (event.type == InputType::LongPress ||
             (press && cursor_ + 1 == Scoreboard::kNameLength))
    {
        finishEntry();
        return true;
    }
    else if (press)
    {
        ++cursor_;
    }
    return false;
}

void ScoreScreens::addUnasked(int score)
{
    const std::string name = board_.lastName();
    board_.add(name.empty() ? "???" : name, score);
}

void ScoreScreens::turnLetter(int delta)
{
    const auto size = static_cast<int>(kAlphabet.size());
    const auto at = kAlphabet.find(entry_[cursor_]);
    const int index = at == std::string_view::npos ? 0 : static_cast<int>(at);
    entry_[cursor_] = kAlphabet[static_cast<std::size_t>(((index + delta) % size + size) % size)];
}

void ScoreScreens::finishEntry()
{
    std::string name = entry_;
    name.erase(name.find_last_not_of(' ') + 1);
    const std::size_t place = board_.add(name.empty() ? "???" : name, score_);
    highlight_ = place;
    scroll_ = place < kRows ? 0 : place - (kRows - 1);
}

void ScoreScreens::renderBoard(Surface &surface) const
{
    drawTextCentered(surface, 1, "HIGHSCORE", kYellow);

    const auto &entries = board_.entries();
    if (entries.empty())
    {
        drawTextCentered(surface, 12, "NO SCORES", kDim);
        drawTextCentered(surface, 22, "PRESS", kText);
        return;
    }

    for (std::size_t row = 0; row < kRows && scroll_ + row < entries.size(); ++row)
    {
        const std::size_t place = scroll_ + row;
        const int y = kRowHeight * static_cast<int>(row + 1) + 1;
        const Color colour = highlight_ == place ? kYellow : kText;
        const std::string rank = std::to_string(place + 1);
        const std::string score = std::to_string(entries[place].score);

        drawText(surface, rightAlignedTextX(9, rank), y, rank, kDim);
        drawText(surface, 13, y, entries[place].name, colour);
        drawText(surface, rightAlignedTextX(kPanelWidth - 2, score), y, score, colour);
    }
}

void ScoreScreens::renderEntry(Surface &surface, float seconds, std::string_view title) const
{
    drawTextCentered(surface, 1, title, kWrong);
    drawTextCentered(surface, 10, "SCORE " + std::to_string(score_), kYellow);

    const int left = centredTextX(kPanelWidth, entry_);
    drawText(surface, left, 21, entry_, kText);

    if (static_cast<int>(seconds / kBlinkSeconds) % 2 == 0)
    {
        const int x = left + static_cast<int>(cursor_) * kGlyphAdvance;
        for (int dx = 0; dx < kGlyphWidth; ++dx)
        {
            surface.setPixel(x + dx, 29, kYellow);
        }
    }
}

} // namespace matrixos
