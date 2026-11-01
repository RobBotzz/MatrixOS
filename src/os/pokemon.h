// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace matrixos
{

/// One question: a sprite and four names, one of them right.
struct PokemonRound
{
    int id = 0;
    std::array<std::string, 4> options; ///< already folded for the panel's font
    int answer = 0;                     ///< index into `options`
    std::vector<std::uint8_t> sprite;   ///< RGBA, kSprite x kSprite
};

/// The German name from a PokéAPI `pokemon-species` answer.
std::optional<std::string> germanName(std::string_view species_json);

/// Upper case and ASCII, which is what the font has: umlauts become AE/OE/UE,
/// the gender signs of Nidoran become W and M.
std::string panelName(std::string_view utf8);

/// Prepares quiz rounds from PokéAPI while the quiz is on the panel, so a new
/// round never waits on the network. Nothing Pokémon is shipped with MatrixOS;
/// every unit fetches it itself (see the roadmap's backlog).
class PokemonService
{
public:
    using Fetch = std::function<std::optional<std::string>(const std::string &url)>;

    static constexpr int kCount = 151; ///< the first generation
    static constexpr int kSprite = 24;
    static constexpr std::size_t kReady = 3;

    struct Timing
    {
        std::chrono::milliseconds retry{std::chrono::seconds(15)};
    };

    static std::string speciesUrl(int id);
    static std::string spriteUrl(int id);

    PokemonService(Fetch fetch, std::uint32_t seed);
    PokemonService(Fetch fetch, std::uint32_t seed, Timing timing);
    ~PokemonService();

    PokemonService(const PokemonService &) = delete;
    PokemonService &operator=(const PokemonService &) = delete;

    void begin();

    /// Rounds are prepared only while the quiz is on the panel.
    void setActive(bool active);

    std::optional<PokemonRound> take();
    bool offline() const;

    /// Prepares one round synchronously. The worker's loop body; public for the
    /// tests.
    bool prepare();

private:
    void run();
    std::optional<std::string> name(int id);

    Fetch fetch_;
    Timing timing_;
    std::mt19937 random_; ///< worker only, or the tests

    mutable std::mutex mutex_;
    std::map<int, std::string> names_;
    std::deque<PokemonRound> ready_;
    bool active_ = false;
    bool offline_ = false;

    std::condition_variable wake_;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};

} // namespace matrixos
