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
#include "os/state.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace matrixos;
using matrixos::test::TempDir;

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

TEST_CASE("a round has four different names, one of them the right one")
{
    PokemonService pokemon(pokedex(), 42);
    for (int i = 0; i < 10; ++i)
    {
        REQUIRE(pokemon.prepare());
        const std::optional<PokemonRound> round = pokemon.take();
        REQUIRE(round);
        CHECK(round->id >= 1);
        CHECK(round->id <= PokemonService::kCount);
        CHECK(std::set<std::string>(round->options.begin(), round->options.end()).size() == 4);
        CHECK(round->options[static_cast<std::size_t>(round->answer)] ==
              "NAME" + std::to_string(round->id));
        CHECK(round->sprite.size() == PokemonService::kSprite * PokemonService::kSprite * 4);
    }
}

TEST_CASE("the board sorts, keeps ten, and refuses what would not make it")
{
    StateStore store = StateStore::inMemory();
    Scoreboard board(store.section("pokemon"));
    CHECK_FALSE(board.qualifies(0));
    CHECK(board.qualifies(1));

    CHECK(board.add("ANNA", 5) == 0);
    CHECK(board.add("BEN", 9) == 0);
    CHECK(board.add("CLARA", 5) == 2); // the older 5 stays ahead
    CHECK(board.entries()[1].name == "ANNA");

    for (int i = 0; i < 10; ++i)
    {
        board.add("X", 20 + i);
    }
    CHECK(board.entries().size() == Scoreboard::kSize);
    CHECK(board.entries().front().score == 29);
    CHECK_FALSE(board.qualifies(20));
    CHECK(board.qualifies(21));
}

TEST_CASE("the board and the last name survive a restart")
{
    TempDir dir;
    {
        StateStore store(dir.path());
        Scoreboard board(store.section("pokemon"));
        board.add("ROBIN", 12);
        board.add("MAX", 3);
    }
    StateStore store(dir.path());
    Scoreboard board(store.section("pokemon"));
    REQUIRE(board.entries().size() == 2);
    CHECK(board.entries()[0].name == "ROBIN");
    CHECK(board.entries()[0].score == 12);
    CHECK(board.lastName() == "MAX");
}

namespace
{

struct Game
{
    StateStore store = StateStore::inMemory();
    PokemonService pokemon{pokedex(), 3};
    PokemonApp app{pokemon, store};

    explicit Game(int rounds)
    {
        for (int i = 0; i < rounds; ++i)
        {
            REQUIRE(pokemon.prepare());
        }
        app.onEnter();
    }

    ~Game() { app.onExit(); }

    void press() { app.onInput({InputType::Press, 0}); }

    void answer(bool right)
    {
        REQUIRE(app.round());
        const int target = right ? app.round()->answer : (app.round()->answer + 1) % 4;
        app.onInput({InputType::Rotate, target - app.selected()});
        press();
    }
};

} // namespace

TEST_CASE("the scoreboard comes first, and a press starts the run")
{
    Game game(1);
    CHECK(game.app.screen() == PokemonApp::Screen::Scores);
    CHECK_FALSE(game.app.interruptible());
    game.press();
    CHECK(game.app.screen() == PokemonApp::Screen::Guess);
    REQUIRE(game.app.round());
}

TEST_CASE("nothing is revealed until the player answers")
{
    Game game(1);
    game.press();
    game.app.update(Duration{3600.0F});
    CHECK(game.app.screen() == PokemonApp::Screen::Guess);

    game.app.onInput({InputType::Rotate, 1});
    CHECK(game.app.selected() == 1);
    game.app.onInput({InputType::Rotate, -2});
    CHECK(game.app.selected() == 3);
}

TEST_CASE("right answers count up and go on")
{
    Game game(3);
    game.press();
    game.answer(true);
    CHECK(game.app.screen() == PokemonApp::Screen::Result);
    CHECK(game.app.score() == 1);
    game.press();
    CHECK(game.app.screen() == PokemonApp::Screen::Guess);
    game.answer(true);
    CHECK(game.app.score() == 2);
}

TEST_CASE("a wrong answer with a score asks for a name, then shows the board")
{
    Game game(3);
    game.press();
    game.answer(true);
    game.press();
    game.answer(false);
    CHECK(game.app.screen() == PokemonApp::Screen::Result);
    CHECK(game.app.score() == 1);
    game.press();
    REQUIRE(game.app.screen() == PokemonApp::Screen::EnterName);

    game.app.onInput({InputType::Rotate, 1}); // space wraps to A
    CHECK(game.app.entry()[0] == 'A');
    game.app.onInput({InputType::Rotate, 1});
    CHECK(game.app.entry()[0] == 'B');
    game.press();
    CHECK(game.app.cursor() == 1);
    game.app.onInput({InputType::Rotate, -1}); // one before the space is Z
    CHECK(game.app.entry()[1] == 'Z');
    game.app.onInput({InputType::LongPress, 0});

    CHECK(game.app.screen() == PokemonApp::Screen::Scores);
    REQUIRE(game.app.scoreboard().entries().size() == 1);
    CHECK(game.app.scoreboard().entries()[0].name == "BZ");
    CHECK(game.app.scoreboard().entries()[0].score == 1);
}

TEST_CASE("the last letter's press finishes the entry too")
{
    Game game(2);
    game.press();
    game.answer(true);
    game.press();
    game.answer(false);
    game.press();
    for (std::size_t i = 0; i < Scoreboard::kNameLength; ++i)
    {
        game.press();
    }
    CHECK(game.app.screen() == PokemonApp::Screen::Scores);
    CHECK(game.app.scoreboard().entries()[0].name == "???");
}

TEST_CASE("a first wrong answer has nothing to enter and goes to the board")
{
    Game game(1);
    game.press();
    game.answer(false);
    game.press();
    CHECK(game.app.screen() == PokemonApp::Screen::Scores);
    CHECK(game.app.scoreboard().entries().empty());
}

TEST_CASE("without a network the run waits and says so")
{
    StateStore store = StateStore::inMemory();
    PokemonService pokemon(pokedex(false), 1);
    CHECK_FALSE(pokemon.prepare());
    CHECK(pokemon.offline());

    PokemonApp app(pokemon, store);
    app.onEnter();
    app.onInput({InputType::Press, 0});
    CHECK(app.screen() == PokemonApp::Screen::Guess);
    CHECK_FALSE(app.round());
    app.onInput({InputType::Press, 0}); // nothing to answer yet
    CHECK(app.screen() == PokemonApp::Screen::Guess);
    Surface frame(64, 32);
    app.render(frame);
    app.onExit();
}

TEST_CASE("the guess has the burst, the question mark and a name to pick")
{
    Game game(1);
    game.press();
    Surface frame(64, 32);
    game.app.render(frame);

    const auto count = [&frame](Color colour)
    {
        int found = 0;
        for (int y = 0; y < 32; ++y)
        {
            for (int x = 0; x < 64; ++x)
            {
                found += frame.pixel(x, y) == colour ? 1 : 0;
            }
        }
        return found;
    };
    CHECK(count({255, 215, 0}) > 20);  // the question mark
    CHECK(count({20, 85, 145}) > 20);  // the silhouette
    CHECK(count({215, 20, 55}) > 100); // the red side
    CHECK(frame.pixel(1, 12).b > 150); // the bright side
    CHECK(frame.pixel(0, 30) == Color::black());
}
