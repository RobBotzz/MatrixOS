// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/pokemon.h"

#include "os/cover.h"
#include "os/log.h"

#include <nlohmann/json.hpp>

#include <cctype>

namespace matrixos
{

std::optional<std::string> germanName(std::string_view species_json)
{
    const nlohmann::json root = nlohmann::json::parse(species_json, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root["names"].is_array())
    {
        return std::nullopt;
    }
    for (const auto &entry : root["names"])
    {
        if (entry.is_object() && entry["language"].is_object() &&
            entry["language"].value("name", "") == "de" && entry["name"].is_string())
        {
            return entry["name"].get<std::string>();
        }
    }
    return std::nullopt;
}

std::string panelName(std::string_view utf8)
{
    static const std::array<std::pair<std::string_view, std::string_view>, 9> kFolds = {{
        {"\xC3\xA4", "AE"},
        {"\xC3\xB6", "OE"},
        {"\xC3\xBC", "UE"},
        {"\xC3\x84", "AE"},
        {"\xC3\x96", "OE"},
        {"\xC3\x9C", "UE"},
        {"\xC3\x9F", "SS"},
        {"\xE2\x99\x80", " W"}, // ♀
        {"\xE2\x99\x82", " M"}, // ♂
    }};

    std::string out;
    while (!utf8.empty())
    {
        bool folded = false;
        for (const auto &[from, to] : kFolds)
        {
            if (utf8.substr(0, from.size()) == from)
            {
                out += to;
                utf8.remove_prefix(from.size());
                folded = true;
                break;
            }
        }
        if (folded)
        {
            continue;
        }
        const auto c = static_cast<unsigned char>(utf8.front());
        if (c < 0x80)
        {
            out += static_cast<char>(std::toupper(c));
        }
        utf8.remove_prefix(1);
    }
    return out;
}

std::string PokemonService::speciesUrl(int id)
{
    return "https://pokeapi.co/api/v2/pokemon-species/" + std::to_string(id);
}

std::string PokemonService::spriteUrl(int id)
{
    return "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/" +
           std::to_string(id) + ".png";
}

PokemonService::PokemonService(Fetch fetch, std::uint32_t seed)
    : PokemonService(std::move(fetch), seed, Timing{})
{
}

PokemonService::PokemonService(Fetch fetch, std::uint32_t seed, Timing timing)
    : fetch_(std::move(fetch)), timing_(timing), random_(seed)
{
}

PokemonService::~PokemonService()
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable())
    {
        worker_.join();
    }
}

void PokemonService::begin()
{
    worker_ = std::thread([this] { run(); });
}

void PokemonService::setActive(bool active)
{
    {
        std::lock_guard lock(mutex_);
        active_ = active;
    }
    wake_.notify_all();
}

std::optional<PokemonRound> PokemonService::take()
{
    std::optional<PokemonRound> round;
    {
        std::lock_guard lock(mutex_);
        if (!ready_.empty())
        {
            round = std::move(ready_.front());
            ready_.pop_front();
        }
    }
    wake_.notify_all();
    return round;
}

bool PokemonService::offline() const
{
    std::lock_guard lock(mutex_);
    return offline_;
}

bool PokemonService::prepare()
{
    PokemonRound round;
    round.id = std::uniform_int_distribution<int>(1, kCount)(random_);

    const std::optional<std::string> species = fetch_(speciesUrl(round.id));
    const std::optional<std::string> german = species ? germanName(*species) : std::nullopt;
    const std::optional<std::string> png = german ? fetch_(spriteUrl(round.id)) : std::nullopt;
    std::optional<std::vector<std::uint8_t>> sprite =
        png ? decodeSprite(*png, kSprite) : std::nullopt;

    std::lock_guard lock(mutex_);
    if (!sprite)
    {
        offline_ = !species.has_value() || (german && !png.has_value());
        return false;
    }
    round.name = panelName(*german);
    round.sprite = std::move(*sprite);
    offline_ = false;
    ready_.push_back(std::move(round));
    return true;
}

void PokemonService::run()
{
    while (!stopping_)
    {
        bool wanted = false;
        {
            std::lock_guard lock(mutex_);
            wanted = active_ && ready_.size() < kReady;
        }

        const bool ok = wanted && prepare();

        std::unique_lock lock(mutex_);
        if (wanted && !ok)
        {
            wake_.wait_for(lock, timing_.retry, [this] { return stopping_.load(); });
        }
        else
        {
            wake_.wait(lock,
                       [this] { return stopping_.load() || (active_ && ready_.size() < kReady); });
        }
    }
}

} // namespace matrixos
