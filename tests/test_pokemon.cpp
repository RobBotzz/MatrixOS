// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/pokemon/pokemon_app.h"

#include "gfx/surface.h"
#include "os/cover.h"
#include "os/pkce.h"
#include "os/pokemon.h"

#include <catch2/catch_test_macros.hpp>

using namespace matrixos;

namespace
{

/// 16x16, transparent, with an opaque red bar 8 wide and 4 high in the middle.
std::string sprite()
{
    return *base64UrlDecode("iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8_"
                            "9hAAAAGUlEQVR4nGNgGAWDEPwHInx4JBgwCgYAAACJnz_BlOTeUgAAAABJRU5ErkJggg");
}

std::string species(const std::string &german)
{
    return R"({"names":[{"language":{"name":"en"},"name":"English"},{"language":{"name":"de"},"name":")" +
           german + R"("}]})";
}

/// Every species answers with its number in the name; sprites always work.
PokemonService::Fetch pokedex(bool online = true)
{
    return [online](const std::string &url) -> std::optional<std::string>
    {
        if (!online)
        {
            return std::nullopt;
        }
        if (url.find(".png") != std::string::npos)
        {
            return sprite();
        }
        return species("Name" + url.substr(url.rfind('/') + 1));
    };
}

} // namespace

TEST_CASE("the German name is read from a species answer")
{
    CHECK(germanName(species("Glumanda")) == "Glumanda");
    CHECK_FALSE(germanName(R"({"names":[]})"));
    CHECK_FALSE(germanName("not json"));
}

TEST_CASE("names are folded into what the font can draw")
{
    CHECK(panelName("Glumanda") == "GLUMANDA");
    CHECK(panelName("Nidoran\xE2\x99\x80") == "NIDORAN W");
    CHECK(panelName("Nidoran\xE2\x99\x82") == "NIDORAN M");
    CHECK(panelName("\xC3\x84rger") == "AERGER");
}

TEST_CASE("a sprite is cropped to its figure and fitted with its proportions")
{
    const auto rgba = decodeSprite(sprite(), 24);
    REQUIRE(rgba);
    REQUIRE(rgba->size() == 24 * 24 * 4);
    const auto alpha = [&](int x, int y)
    { return (*rgba)[static_cast<std::size_t>((y * 24 + x) * 4 + 3)]; };
    CHECK(alpha(12, 12) == 255); // the bar, now filling the width
    CHECK(alpha(0, 12) == 255);
    CHECK(alpha(12, 0) == 0); // half as high as wide: empty above and below
    CHECK(alpha(12, 23) == 0);
    CHECK((*rgba)[static_cast<std::size_t>((12 * 24 + 12) * 4)] == 255); // red
    CHECK_FALSE(decodeSprite("not a png", 24));
}

TEST_CASE("a round has the German name and the sprite of one Pokémon")
{
    PokemonService pokemon(pokedex(), 42);
    for (int i = 0; i < 10; ++i)
    {
        REQUIRE(pokemon.prepare());
        const std::optional<PokemonRound> round = pokemon.take();
        REQUIRE(round);
        CHECK(round->id >= 1);
        CHECK(round->id <= PokemonService::kCount);
        CHECK(round->name == "NAME" + std::to_string(round->id));
        CHECK(round->sprite.size() == PokemonService::kSprite * PokemonService::kSprite * 4);
    }
}

TEST_CASE("without a network there is no round, and the app says it is offline")
{
    PokemonService pokemon(pokedex(false), 1);
    CHECK_FALSE(pokemon.prepare());
    CHECK(pokemon.offline());

    PokemonApp app(pokemon);
    app.onEnter();
    CHECK_FALSE(app.round());
    Surface frame(64, 32);
    app.render(frame);
    app.onExit();
}

TEST_CASE("press reveals, press again moves on; rotate skips")
{
    PokemonService pokemon(pokedex(), 7);
    for (int i = 0; i < 3; ++i)
    {
        REQUIRE(pokemon.prepare());
    }
    PokemonApp app(pokemon);
    app.onEnter();
    REQUIRE(app.round());
    CHECK(app.interruptible());
    const std::string first_name = app.round()->name;

    app.onInput({InputType::Press, 0});
    CHECK(app.revealed());
    CHECK(app.round()->name == first_name);

    app.onInput({InputType::Press, 0});
    CHECK_FALSE(app.revealed());

    app.onInput({InputType::Rotate, 1});
    CHECK_FALSE(app.revealed());
    app.onExit();
}

TEST_CASE("the reveal and the next Pokémon also come by themselves")
{
    PokemonService pokemon(pokedex(), 9);
    REQUIRE(pokemon.prepare());
    REQUIRE(pokemon.prepare());
    PokemonApp app(pokemon);
    app.onEnter();

    app.update(Duration{PokemonApp::kGuessSeconds - 0.5F});
    CHECK_FALSE(app.revealed());
    app.update(Duration{1.0F});
    CHECK(app.revealed());
    app.update(Duration{PokemonApp::kRevealSeconds + 0.1F});
    CHECK_FALSE(app.revealed());
    app.onExit();
}

TEST_CASE("the guess shows a yellow question mark, the reveal the name in a dark band")
{
    PokemonService pokemon(pokedex(), 11);
    REQUIRE(pokemon.prepare());
    PokemonApp app(pokemon);
    app.onEnter();

    const auto yellow = [](const Surface &frame)
    {
        int count = 0;
        for (int y = 0; y < 32; ++y)
        {
            for (int x = 0; x < 64; ++x)
            {
                const Color c = frame.pixel(x, y);
                count += c.r == 255 && c.g == 215 && c.b == 0 ? 1 : 0;
            }
        }
        return count;
    };

    Surface guess(64, 32);
    app.render(guess);
    CHECK(yellow(guess) > 20);
    CHECK(guess.pixel(12, 12) == Color::black()); // the silhouette

    app.onInput({InputType::Press, 0});
    app.update(Duration{1.0F});
    Surface reveal(64, 32);
    app.render(reveal);
    CHECK(reveal.pixel(0, 31) == Color::black()); // the band
    CHECK(yellow(reveal) > 0);                    // the name
    app.onExit();
}
