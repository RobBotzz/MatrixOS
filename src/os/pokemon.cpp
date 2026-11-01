// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/pokemon.h"

#include "os/cover.h"
#include "os/log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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

std::optional<std::string> PokemonService::name(int id)
{
    {
        std::lock_guard lock(mutex_);
        if (const auto found = names_.find(id); found != names_.end())
        {
            return found->second;
        }
    }
    const std::optional<std::string> body = fetch_(speciesUrl(id));
    std::optional<std::string> german = body ? germanName(*body) : std::nullopt;
    if (!german)
    {
        return std::nullopt;
    }
    std::string folded = panelName(*german);
    std::lock_guard lock(mutex_);
    names_[id] = folded;
    return folded;
}

bool PokemonService::prepare()
{
    std::uniform_int_distribution<int> pick(1, kCount);
    std::vector<int> ids;
    while (ids.size() < 4)
    {
        const int id = pick(random_);
        if (std::find(ids.begin(), ids.end(), id) == ids.end())
        {
            ids.push_back(id);
        }
    }

    PokemonRound round;
    round.id = ids[0];
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        std::optional<std::string> found = name(ids[i]);
        if (!found)
        {
            std::lock_guard lock(mutex_);
            offline_ = true;
            return false;
        }
        round.options[i] = std::move(*found);
    }

    const std::optional<std::string> png = fetch_(spriteUrl(round.id));
    std::optional<std::vector<std::uint8_t>> sprite =
        png ? decodeSprite(*png, kSprite) : std::nullopt;
    if (!sprite)
    {
        std::lock_guard lock(mutex_);
        offline_ = !png.has_value();
        return false;
    }
    round.sprite = std::move(*sprite);

    std::shuffle(round.options.begin() + 1, round.options.end(), random_);
    round.answer = std::uniform_int_distribution<int>(0, 3)(random_);
    std::swap(round.options[0], round.options[static_cast<std::size_t>(round.answer)]);

    std::lock_guard lock(mutex_);
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
