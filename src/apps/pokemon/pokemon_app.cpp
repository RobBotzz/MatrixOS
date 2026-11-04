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
#include <cmath>

namespace matrixos
{
namespace
{

constexpr std::string_view kSection = "pokemon";
constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ ";

constexpr Color kCore{255, 255, 255};
constexpr Color kPale{170, 240, 240};
constexpr Color kTeal{40, 190, 205};
constexpr Color kRed{215, 20, 55};
constexpr Color kRedLight{255, 85, 80};
constexpr Color kOrange{255, 130, 50};
constexpr Color kSilhouette{20, 85, 145};
constexpr Color kYellow{255, 215, 0};
constexpr Color kOutline{20, 50, 130};
constexpr Color kBand{0, 0, 0};
constexpr Color kText{255, 255, 255};
constexpr Color kRight{40, 230, 90};
constexpr Color kWrong{255, 60, 60};
constexpr Color kDim{110, 120, 140};

constexpr int kPanelWidth = 64;
constexpr int kPanelHeight = 32;
constexpr int kSprite = PokemonService::kSprite;
constexpr int kSpriteLeft = 4;
constexpr int kSpriteTop = 0;
constexpr int kBandTop = 25;
constexpr int kQuestionLeft = 41;
constexpr int kQuestionTop = 3;
constexpr int kQuestionScale = 3;
constexpr int kDotsX = 60;
constexpr int kDotsTop = 12;

constexpr std::size_t kRays = 48;
constexpr int kReachMin = 18;
constexpr int kReachSpread = 11;
constexpr float kRaysPerSecond = 1.5F;
constexpr double kPullRight = 6.0;  ///< the burst sits left, the red owns the right
constexpr double kBurstShift = 4.0; ///< offsets the pull, so the silhouette sits in the middle

constexpr float kFlashSeconds = 0.15F;
constexpr float kPollInterval = 0.25F;
constexpr float kBlinkSeconds = 0.4F;

constexpr int kRowHeight = 8;
constexpr std::size_t kRows = 3;

Color mix(Color a, Color b, float amount)
{
    const auto channel = [amount](std::uint8_t from, std::uint8_t to)
    { return static_cast<std::uint8_t>(static_cast<float>(from) + (to - from) * amount); };
    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b)};
}

void drawOutlined(Surface &surface, int x, int y, std::string_view text, int scale = 1)
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

PokemonApp::PokemonApp(PokemonService &pokemon, StateStore &store)
    : pokemon_(pokemon), scoreboard_(store.section(kSection))
{
    // Double, not float: atan2f carries a symbol version newer than the device's
    // glibc, and the binary would not load at all.
    constexpr double kTurn = 6.283185307179586;
    const double cx = kSpriteLeft + kSprite / 2.0 + kBurstShift;
    const double cy = kSpriteTop + kSprite / 2.0;
    for (int y = 0; y < kPanelHeight; ++y)
    {
        for (int x = 0; x < kPanelWidth; ++x)
        {
            const double dx = x + 0.5 - cx;
            const double dy = y + 0.5 - cy;
            const double radius = std::sqrt(dx * dx + dy * dy);
            turn_.push_back(static_cast<float>(std::atan2(dy, dx) / kTurn + 0.5));
            distance_.push_back(static_cast<float>(radius + dx / radius * kPullRight));
        }
    }

    std::uint32_t state = 2463534242U;
    for (std::size_t ray = 0; ray < kRays; ++ray)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        reach_.push_back(static_cast<std::uint8_t>(kReachMin + state % kReachSpread));
    }
}

void PokemonApp::onEnter()
{
    pokemon_.setActive(true);
}

void PokemonApp::onExit()
{
    pokemon_.setActive(false);
}

void PokemonApp::start()
{
    score_ = 0;
    highlight_.reset();
    nextRound();
}

void PokemonApp::nextRound()
{
    round_ = pokemon_.take();
    selected_ = 0;
    screen_ = Screen::Guess;
}

void PokemonApp::answer()
{
    right_ = selected_ == round_->answer;
    if (right_)
    {
        ++score_;
    }
    since_answer_ = 0.0F;
    screen_ = Screen::Result;
}

void PokemonApp::endRun()
{
    round_.reset();
    if (!scoreboard_.qualifies(score_))
    {
        scroll_ = 0;
        screen_ = Screen::Scores;
        return;
    }
    entry_ = scoreboard_.lastName();
    entry_.resize(Scoreboard::kNameLength, ' ');
    cursor_ = 0;
    screen_ = Screen::EnterName;
}

void PokemonApp::finishEntry()
{
    std::string name = entry_;
    name.erase(name.find_last_not_of(' ') + 1);
    const std::size_t place = scoreboard_.add(name.empty() ? "???" : name, score_);
    highlight_ = place;
    scroll_ = place < kRows ? 0 : place - (kRows - 1);
    screen_ = Screen::Scores;
}

void PokemonApp::turnLetter(int delta)
{
    const auto size = static_cast<int>(kAlphabet.size());
    const auto at = kAlphabet.find(entry_[cursor_]);
    const int index = at == std::string_view::npos ? 0 : static_cast<int>(at);
    entry_[cursor_] = kAlphabet[static_cast<std::size_t>(((index + delta) % size + size) % size)];
}

void PokemonApp::onInput(const InputEvent &event)
{
    const bool press = event.type == InputType::Press;
    const bool rotate = event.type == InputType::Rotate;

    switch (screen_)
    {
    case Screen::Scores:
        if (press)
        {
            start();
        }
        else if (rotate)
        {
            const auto entries = scoreboard_.entries().size();
            const int last = entries > kRows ? static_cast<int>(entries - kRows) : 0;
            scroll_ = static_cast<std::size_t>(
                std::clamp(static_cast<int>(scroll_) + event.delta, 0, last));
        }
        break;

    case Screen::Guess:
        if (round_ && rotate)
        {
            selected_ = ((selected_ + event.delta) % 4 + 4) % 4;
        }
        else if (round_ && press)
        {
            answer();
        }
        break;

    case Screen::Result:
        if (press && right_)
        {
            nextRound();
        }
        else if (press)
        {
            endRun();
        }
        break;

    case Screen::EnterName:
        if (rotate)
        {
            turnLetter(event.delta);
        }
        else if (event.type == InputType::LongPress ||
                 (press && cursor_ + 1 == Scoreboard::kNameLength))
        {
            finishEntry();
        }
        else if (press)
        {
            ++cursor_;
        }
        break;
    }
}

void PokemonApp::update(Duration dt)
{
    const float seconds = dt.count();
    time_ += seconds;
    since_answer_ += seconds;

    if (screen_ == Screen::Guess && !round_)
    {
        since_poll_ += seconds;
        if (since_poll_ >= kPollInterval)
        {
            since_poll_ = 0.0F;
            round_ = pokemon_.take();
        }
    }
}

void PokemonApp::renderBurst(Surface &surface) const
{
    const auto drift = static_cast<std::size_t>(time_ * kRaysPerSecond);
    for (int y = 0; y < kPanelHeight; ++y)
    {
        for (int x = 0; x < kPanelWidth; ++x)
        {
            const auto i = static_cast<std::size_t>(y * kPanelWidth + x);
            const auto ray =
                (static_cast<std::size_t>(turn_[i] * static_cast<float>(kRays)) + drift) % kRays;
            const float reach = reach_[ray];
            const float distance = distance_[i];

            Color colour = kRed;
            if (distance < reach)
            {
                const float depth = distance / reach;
                colour = depth < 0.55F ? mix(kCore, kPale, depth / 0.55F)
                                       : mix(kPale, kTeal, (depth - 0.55F) / 0.45F);
            }
            else if (reach_[(ray + 7) % kRays] > kReachMin + kReachSpread / 2)
            {
                colour = y > kPanelHeight / 2 ? kOrange : kRedLight;
            }
            surface.setPixel(x, y, colour);
        }
    }
}

void PokemonApp::renderRound(Surface &surface) const
{
    const bool revealed = screen_ == Screen::Result;
    if (revealed && since_answer_ < kFlashSeconds)
    {
        for (int y = 0; y < kPanelHeight; ++y)
        {
            for (int x = 0; x < kPanelWidth; ++x)
            {
                surface.setPixel(x, y, kCore);
            }
        }
    }
    else
    {
        renderBurst(surface);
    }

    const auto &sprite = round_->sprite;
    for (int y = 0; y < kSprite; ++y)
    {
        for (int x = 0; x < kSprite; ++x)
        {
            const auto at = static_cast<std::size_t>((y * kSprite + x) * 4);
            if (sprite[at + 3] != 0)
            {
                surface.setPixel(kSpriteLeft + x, kSpriteTop + y,
                                 revealed ? Color{sprite[at], sprite[at + 1], sprite[at + 2]}
                                          : kSilhouette);
            }
        }
    }

    drawOutlined(surface, kQuestionLeft, kQuestionTop, "?", kQuestionScale);

    const std::string score = std::to_string(score_);
    drawOutlined(surface, rightAlignedTextX(kPanelWidth - 3, score), 2, score);

    for (int y = kBandTop; y < kPanelHeight; ++y)
    {
        for (int x = 0; x < kPanelWidth; ++x)
        {
            surface.setPixel(x, y, kBand);
        }
    }

    if (revealed)
    {
        drawTextCentered(surface, kBandTop,
                         round_->options[static_cast<std::size_t>(round_->answer)],
                         right_ ? kRight : kWrong);
        return;
    }

    drawTextCentered(surface, kBandTop, round_->options[static_cast<std::size_t>(selected_)],
                     kText);
    for (int i = 0; i < 4; ++i)
    {
        const Color dot = i == selected_ ? kYellow : kOutline;
        for (int dy = 0; dy < 2; ++dy)
        {
            surface.setPixel(kDotsX, kDotsTop + i * 3 + dy, dot);
            surface.setPixel(kDotsX + 1, kDotsTop + i * 3 + dy, dot);
        }
    }
}

void PokemonApp::renderScores(Surface &surface) const
{
    drawTextCentered(surface, 1, "HIGHSCORE", kYellow);

    const auto &entries = scoreboard_.entries();
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

void PokemonApp::renderEntry(Surface &surface) const
{
    drawTextCentered(surface, 1, "GAME OVER", kWrong);
    drawTextCentered(surface, 10, "SCORE " + std::to_string(score_), kYellow);

    const int left = centredTextX(kPanelWidth, entry_);
    drawText(surface, left, 21, entry_, kText);

    if (static_cast<int>(time_ / kBlinkSeconds) % 2 == 0)
    {
        const int x = left + static_cast<int>(cursor_) * kGlyphAdvance;
        for (int dx = 0; dx < kGlyphWidth; ++dx)
        {
            surface.setPixel(x + dx, 29, kYellow);
        }
    }
}

void PokemonApp::render(Surface &surface)
{
    switch (screen_)
    {
    case Screen::Scores:
        renderScores(surface);
        return;
    case Screen::EnterName:
        renderEntry(surface);
        return;
    case Screen::Guess:
    case Screen::Result:
        break;
    }

    if (!round_)
    {
        drawTextCentered(surface, 7, "POKEMON", kYellow);
        drawTextCentered(surface, 18, pokemon_.offline() ? "OFFLINE" : "LOADING", kDim);
        return;
    }
    renderRound(surface);
}

} // namespace matrixos
