// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/morse/morse.h"

#include "gfx/surface.h"
#include "os/state.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <set>
#include <string>

using namespace matrixos;
using matrixos::test::TempDir;

namespace
{

constexpr std::uint32_t kSeed = 4242;
constexpr float kStep = 1.0F / 240.0F;

void send(MorseApp &app, InputType type, int delta = 0)
{
    app.onInput(InputEvent{type, delta});
}

void advance(MorseApp &app, float seconds)
{
    for (float spent = 0.0F; spent < seconds; spent += kStep)
    {
        app.update(Duration{kStep});
    }
}

/// Stops the moment the letter is up, so `remaining()` is still the full limit
/// and a score can be asserted exactly.
void runUntilKeying(MorseApp &app)
{
    for (int i = 0; i < 2000 && app.phase() != MorseApp::Phase::Keying; ++i)
    {
        app.update(Duration{kStep});
    }
}

void keyCode(MorseApp &app, std::string_view code)
{
    for (const char symbol : code)
    {
        send(app, symbol == '-' ? InputType::LongPress : InputType::Press);
    }
}

/// Keys whatever letter is on screen, in whichever mode.
void keyCurrent(MorseApp &app)
{
    keyCode(app, MorseApp::patternFor(app.currentLetter()));
}

/// The wrong symbol for the next position of the current letter.
InputType wrongSymbol(const MorseApp &app)
{
    const std::string_view code = MorseApp::patternFor(app.currentLetter());
    return code[app.entered().size()] == '.' ? InputType::LongPress : InputType::Press;
}

MorseApp startedStudy(StateStore &store)
{
    MorseApp app(store, kSeed);
    app.onEnter();
    send(app, InputType::Press); // STUDY is the first menu entry
    return app;
}

MorseApp startedQuiz(StateStore &store, std::uint32_t seed = kSeed)
{
    MorseApp app(store, seed);
    app.onEnter();
    send(app, InputType::Rotate, 1); // Study -> Quiz
    send(app, InputType::Press);
    return app;
}

MorseApp startedGuess(StateStore &store, std::uint32_t seed = kSeed)
{
    MorseApp app(store, seed);
    app.onEnter();
    send(app, InputType::Rotate, 2); // Study -> Quiz -> Read
    send(app, InputType::Press);
    return app;
}

MorseApp startedCode(StateStore &store, std::uint32_t seed = kSeed)
{
    MorseApp app(store, seed);
    app.onEnter();
    send(app, InputType::Rotate, 3); // ... -> Game
    send(app, InputType::Press);
    return app;
}

/// Turns the cursor onto `index` and confirms.
void pickChoice(MorseApp &app, int index)
{
    while (app.choice() != index)
    {
        send(app, InputType::Rotate, 1);
    }
    send(app, InputType::Press);
}

void answerCorrectly(MorseApp &app)
{
    runUntilKeying(app);
    keyCurrent(app);
}

/// Opens the pause menu and walks to `action`.
void choosePause(MorseApp &app, MorseApp::PauseAction action)
{
    send(app, InputType::Rotate, 1);
    REQUIRE(app.paused());

    for (int i = 0; i < app.pauseCount(); ++i)
    {
        if (app.pauseAction(app.pauseIndex()) == action)
        {
            break;
        }
        send(app, InputType::Rotate, 1);
    }
    REQUIRE(app.pauseAction(app.pauseIndex()) == action);
    send(app, InputType::Press);
}

struct Margins
{
    int left = -1;
    int right = -1;
};

/// Ink margins inside rows [top, bottom], which is how "centred" is checked
/// without depending on which columns of a glyph happen to carry ink.
Margins marginsIn(const Surface &surface, int top, int bottom)
{
    int first = surface.width();
    int last = -1;

    for (int y = top; y <= bottom; ++y)
    {
        for (int x = 0; x < surface.width(); ++x)
        {
            if (!(surface.pixel(x, y) == Color::black()))
            {
                first = std::min(first, x);
                last = std::max(last, x);
            }
        }
    }

    if (last < 0)
    {
        return {};
    }
    return {first, surface.width() - 1 - last};
}

bool anythingDrawn(const Surface &surface)
{
    for (int y = 0; y < surface.height(); ++y)
    {
        for (int x = 0; x < surface.width(); ++x)
        {
            if (!(surface.pixel(x, y) == Color::black()))
            {
                return true;
            }
        }
    }
    return false;
}

} // namespace

TEST_CASE("the code table is complete and unambiguous", "[morse]")
{
    REQUIRE(MorseApp::order().size() == 36);

    std::set<std::string> codes;
    for (const char symbol : MorseApp::order())
    {
        const std::string_view code = MorseApp::patternFor(symbol);
        REQUIRE_FALSE(code.empty());
        REQUIRE(code.size() <= static_cast<std::size_t>(MorseApp::kMaxSymbols));
        REQUIRE(code.find_first_not_of(".-") == std::string_view::npos);
        REQUIRE(codes.insert(std::string(code)).second); // no two letters share a code
    }
}

TEST_CASE("known letters have the codes they should", "[morse]")
{
    CHECK(MorseApp::patternFor('E') == ".");
    CHECK(MorseApp::patternFor('T') == "-");
    CHECK(MorseApp::patternFor('A') == ".-");
    CHECK(MorseApp::patternFor('S') == "...");
    CHECK(MorseApp::patternFor('O') == "---");
    CHECK(MorseApp::patternFor('Q') == "--.-");
    CHECK(MorseApp::patternFor('0') == "-----");
    CHECK(MorseApp::patternFor('7') == "--...");
}

TEST_CASE("lookup ignores case and rejects anything else", "[morse]")
{
    CHECK(MorseApp::patternFor('a') == ".-");
    CHECK(MorseApp::patternFor('z') == "--..");
    CHECK(MorseApp::patternFor('?').empty());
    CHECK(MorseApp::patternFor(' ').empty());
    CHECK(MorseApp::patternFor('\0').empty());
}

TEST_CASE("the order starts with the shortest codes", "[morse]")
{
    // What makes the first run of the code game playable, so it is worth asserting.
    const std::string_view order = MorseApp::order();
    CHECK(order.substr(0, 6) == "ETIANM");

    std::size_t previous = 0;
    for (const char symbol : order)
    {
        const std::size_t length = MorseApp::patternFor(symbol).size();
        CHECK(length >= previous); // never gets shorter
        previous = length;
    }
}

TEST_CASE("the menu picks a mode", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app(store, kSeed);
    app.onEnter();

    REQUIRE(app.screen() == MorseApp::Screen::Menu);
    REQUIRE(app.selected() == MorseApp::Screen::Study);

    send(app, InputType::Rotate, 1);
    CHECK(app.selected() == MorseApp::Screen::Quiz);

    send(app, InputType::Rotate, 1);
    CHECK(app.selected() == MorseApp::Screen::Guess);

    send(app, InputType::Rotate, 1);
    CHECK(app.selected() == MorseApp::Screen::Code);

    send(app, InputType::Rotate, 1); // wraps
    CHECK(app.selected() == MorseApp::Screen::Study);

    send(app, InputType::Rotate, -1);
    CHECK(app.selected() == MorseApp::Screen::Code);

    send(app, InputType::Press);
    CHECK(app.screen() == MorseApp::Screen::Code);
}

TEST_CASE("the study never advances on its own", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedStudy(store);

    REQUIRE(app.studySymbol() == 'E');
    REQUIRE(app.phase() == MorseApp::Phase::Keying);

    // Long enough for many turns of the demonstration loop.
    advance(app, 30.0F);

    CHECK(app.studySymbol() == 'E');
    CHECK(app.phase() == MorseApp::Phase::Keying);
}

TEST_CASE("the study advances only when the letter is keyed", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedStudy(store);

    REQUIRE(app.studySymbol() == 'E'); // "."

    send(app, InputType::Press);
    CHECK(app.phase() == MorseApp::Phase::Correct);

    advance(app, 1.0F);
    CHECK(app.studySymbol() == 'T');
    CHECK(app.phase() == MorseApp::Phase::Keying);
    CHECK(app.entered().empty());

    send(app, InputType::LongPress); // "-"
    advance(app, 1.0F);
    CHECK(app.studySymbol() == 'I');
}

TEST_CASE("a wrong symbol in the study costs nothing but another go", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedStudy(store);

    send(app, wrongSymbol(app));

    CHECK(app.phase() == MorseApp::Phase::Wrong);
    CHECK(app.studySymbol() == 'E');
    CHECK(app.lives() == MorseApp::kLives); // untouched: the study has no lives
    CHECK(app.score() == 0);

    advance(app, 1.0F);
    CHECK(app.phase() == MorseApp::Phase::Keying);
    CHECK(app.studySymbol() == 'E');
    CHECK(app.entered().empty());
}

TEST_CASE("the study shows how far into the letter the operator is", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedStudy(store);

    keyCurrent(app); // E
    advance(app, 1.0F);
    keyCurrent(app); // T
    advance(app, 1.0F);
    REQUIRE(app.studySymbol() == 'I'); // ".."

    send(app, InputType::Press);
    CHECK(app.entered() == ".");
    CHECK(app.phase() == MorseApp::Phase::Keying);

    send(app, InputType::Press);
    CHECK(app.phase() == MorseApp::Phase::Correct);
}

TEST_CASE("turning opens the pause menu, and resume gives the letter back", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedStudy(store);

    send(app, InputType::Rotate, 1);

    CHECK(app.paused());
    CHECK(app.pauseCount() == 2); // resume, menu — nothing else, in any mode
    CHECK(app.pauseIndex() == 0);
    CHECK(app.pauseAction(0) == MorseApp::PauseAction::Resume);
    CHECK(app.pauseAction(1) == MorseApp::PauseAction::Menu);

    // Keying is inert while paused, so a stray hold cannot be sent into the run.
    send(app, InputType::LongPress);
    CHECK(app.entered().empty());

    send(app, InputType::Press); // RESUME
    CHECK_FALSE(app.paused());
    CHECK(app.screen() == MorseApp::Screen::Study);
    CHECK(app.studySymbol() == 'E');
}

TEST_CASE("the pause menu leaves the study where it stood", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedStudy(store);

    keyCurrent(app); // E
    advance(app, 1.0F);
    REQUIRE(app.studySymbol() == 'T');

    choosePause(app, MorseApp::PauseAction::Menu);
    CHECK_FALSE(app.paused());
    CHECK(app.screen() == MorseApp::Screen::Menu);

    // The study picks up where it was left.
    send(app, InputType::Press);
    CHECK(app.studySymbol() == 'T');
    CHECK(app.phase() == MorseApp::Phase::Keying);
}

TEST_CASE("the quiz asks from memory and keeps its own counsel", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedQuiz(store);

    REQUIRE(app.screen() == MorseApp::Screen::Quiz);
    REQUIRE(app.phase() == MorseApp::Phase::Keying);
    CHECK_FALSE(app.revealed());
    CHECK(app.entered().empty());
    CHECK(app.poolSize() == MorseApp::kStartPool);
}

TEST_CASE("the quiz has no clock", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedQuiz(store);

    const char letter = app.currentLetter();
    advance(app, 30.0F);

    CHECK(app.currentLetter() == letter);
    CHECK(app.phase() == MorseApp::Phase::Keying);
    CHECK(app.lives() == MorseApp::kLives);
    CHECK(app.score() == 0);
}

TEST_CASE("a miss in the quiz hands the answer over and the letter is still finished", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedQuiz(store);

    const char missed = app.currentLetter();
    send(app, wrongSymbol(app));

    CHECK(app.phase() == MorseApp::Phase::Wrong);
    CHECK(app.revealed()); // this is the point of the mode
    CHECK(app.lives() == MorseApp::kLives);
    CHECK(app.score() == 0);

    advance(app, 1.0F);
    CHECK(app.phase() == MorseApp::Phase::Keying);
    CHECK(app.currentLetter() == missed); // the same letter, now with its code up
    CHECK(app.revealed());
    CHECK(app.entered().empty());

    keyCurrent(app);
    CHECK(app.phase() == MorseApp::Phase::Correct);

    advance(app, 1.0F);
    CHECK(app.phase() == MorseApp::Phase::Keying);
    CHECK(app.currentLetter() != missed);
    CHECK_FALSE(app.revealed()); // the next one is asked for again
}

TEST_CASE("the quiz cannot be failed out of", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedQuiz(store);

    for (int i = 0; i < 10; ++i)
    {
        send(app, wrongSymbol(app));
        advance(app, 1.0F);

        CHECK(app.phase() == MorseApp::Phase::Keying);
        CHECK(app.lives() == MorseApp::kLives);
    }
}

TEST_CASE("the quiz pool widens only on unaided recall", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedQuiz(store);

    REQUIRE(app.poolSize() == MorseApp::kStartPool);

    // Handed the answer, then finishing the letter: practice, not progress.
    for (int i = 0; i < MorseApp::kCorrectPerNewLetter; ++i)
    {
        send(app, wrongSymbol(app));
        advance(app, 1.0F);
        REQUIRE(app.revealed());
        keyCurrent(app);
        advance(app, 1.0F);
    }
    CHECK(app.poolSize() == MorseApp::kStartPool);

    for (int i = 0; i < MorseApp::kCorrectPerNewLetter; ++i)
    {
        REQUIRE_FALSE(app.revealed());
        keyCurrent(app);
        advance(app, 1.0F);
    }
    CHECK(app.poolSize() == MorseApp::kStartPool + 1);
}

TEST_CASE("the quiz can be left, and offers nothing but that", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedQuiz(store);

    send(app, InputType::Rotate, 1);
    REQUIRE(app.paused());
    CHECK(app.pauseCount() == 2);
    CHECK(app.pauseAction(0) == MorseApp::PauseAction::Resume);
    CHECK(app.pauseAction(1) == MorseApp::PauseAction::Menu);

    send(app, InputType::Press); // RESUME
    CHECK_FALSE(app.paused());
    CHECK(app.screen() == MorseApp::Screen::Quiz);

    choosePause(app, MorseApp::PauseAction::Menu);
    CHECK(app.screen() == MorseApp::Screen::Menu);
}

TEST_CASE("nothing in the quiz touches the code game's record", "[morse]")
{
    TempDir dir;
    StateStore store(dir.path());
    MorseApp app = startedQuiz(store);

    for (int i = 0; i < 5; ++i)
    {
        keyCurrent(app);
        advance(app, 1.0F);
    }

    CHECK(app.score() == 0);
    CHECK(app.highScore() == 0);

    StateStore other(dir.path());
    CHECK(other.section("morse").getInt("highscore", -1) == -1); // never written
}

TEST_CASE("the code game has no skip and its pause stops the clock", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedCode(store);
    runUntilKeying(app);

    const float before = app.remaining();
    send(app, InputType::Rotate, 1);

    REQUIRE(app.paused());
    CHECK(app.pauseCount() == 2);
    CHECK(app.pauseAction(1) == MorseApp::PauseAction::Menu);

    advance(app, 30.0F); // far past the limit

    CHECK(app.remaining() == before);
    CHECK(app.lives() == MorseApp::kLives);
    CHECK(app.phase() == MorseApp::Phase::Keying);

    send(app, InputType::Press); // RESUME
    CHECK_FALSE(app.paused());
    keyCurrent(app);
    CHECK(app.phase() == MorseApp::Phase::Correct);
}

TEST_CASE("leaving the code game through the pause menu banks the score", "[morse]")
{
    TempDir dir;
    StateStore store(dir.path());
    MorseApp app = startedCode(store);

    answerCorrectly(app);
    const int scored = app.score();
    REQUIRE(scored > 0);

    runUntilKeying(app);
    choosePause(app, MorseApp::PauseAction::Menu);

    CHECK(app.screen() == MorseApp::Screen::Menu);
    CHECK(app.phase() == MorseApp::Phase::Over);

    StateStore reopened(dir.path());
    CHECK(reopened.section("morse").getInt("highscore", 0) == scored);
}

TEST_CASE("a correct letter scores and speeds the code game up", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedCode(store);

    REQUIRE(app.phase() == MorseApp::Phase::Ready);
    REQUIRE(app.score() == 0);

    runUntilKeying(app);
    REQUIRE(app.remaining() == MorseApp::kStartLimit);

    keyCurrent(app);

    // Ten for the letter, ten for a clock that has not moved, one for the streak.
    CHECK(app.phase() == MorseApp::Phase::Correct);
    CHECK(app.score() == 21);
    CHECK(app.streak() == 1);
    CHECK(app.lives() == MorseApp::kLives);
    CHECK(app.limit() < MorseApp::kStartLimit);
}

TEST_CASE("a wrong symbol is caught as it is sent", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedCode(store);
    runUntilKeying(app);

    send(app, wrongSymbol(app));

    CHECK(app.phase() == MorseApp::Phase::Wrong);
    CHECK(app.lives() == MorseApp::kLives - 1);
    CHECK(app.score() == 0);
    CHECK(app.streak() == 0);
}

TEST_CASE("running out of time costs a life", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedCode(store);
    runUntilKeying(app);

    // Past the limit and past the dash-length grace that covers a hold in flight.
    advance(app, MorseApp::kStartLimit + 1.0F);

    CHECK(app.lives() == MorseApp::kLives - 1);
    CHECK(app.phase() != MorseApp::Phase::Keying);
}

TEST_CASE("a streak survives until it is broken", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedCode(store);

    for (int i = 0; i < 3; ++i)
    {
        answerCorrectly(app);
    }
    REQUIRE(app.streak() == 3);

    runUntilKeying(app);
    send(app, wrongSymbol(app));
    CHECK(app.streak() == 0);
    CHECK(app.lives() == MorseApp::kLives - 1);
}

TEST_CASE("the pool widens as letters are landed", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedCode(store);

    REQUIRE(app.poolSize() == MorseApp::kStartPool);

    for (int i = 0; i < MorseApp::kCorrectPerNewLetter; ++i)
    {
        answerCorrectly(app);
    }

    CHECK(app.poolSize() == MorseApp::kStartPool + 1);
}

TEST_CASE("only letters from the pool are asked for", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedCode(store);

    for (int i = 0; i < 6; ++i)
    {
        runUntilKeying(app);
        const std::string_view pool =
            MorseApp::order().substr(0, static_cast<std::size_t>(app.poolSize()));
        CHECK(pool.find(app.target()) != std::string_view::npos);
        keyCurrent(app);
    }
}

TEST_CASE("three misses end the run", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedCode(store);

    for (int i = 0; i < MorseApp::kLives; ++i)
    {
        runUntilKeying(app);
        send(app, wrongSymbol(app));
    }

    REQUIRE(app.lives() == 0);

    advance(app, 2.0F); // let the last flash finish
    CHECK(app.phase() == MorseApp::Phase::Over);

    send(app, InputType::Press);
    CHECK(app.phase() == MorseApp::Phase::Ready);
    CHECK(app.score() == 0);
    CHECK(app.lives() == MorseApp::kLives);

    send(app, InputType::LongPress); // swallowed while counting in
    CHECK(app.screen() == MorseApp::Screen::Code);
}

TEST_CASE("the run ends with the encoder's own gesture on the over screen", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedCode(store);

    for (int i = 0; i < MorseApp::kLives; ++i)
    {
        runUntilKeying(app);
        send(app, wrongSymbol(app));
    }
    advance(app, 2.0F);
    REQUIRE(app.phase() == MorseApp::Phase::Over);

    send(app, InputType::LongPress);
    CHECK(app.screen() == MorseApp::Screen::Menu);
}

TEST_CASE("leaving the app ends a run in progress", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedCode(store);
    runUntilKeying(app);

    send(app, InputType::Rotate, 1); // paused when it goes off screen
    app.onExit();

    CHECK(app.phase() == MorseApp::Phase::Over);
    CHECK_FALSE(app.paused());
}

TEST_CASE("the high score outlives the process", "[morse]")
{
    TempDir dir;
    int scored = 0;

    {
        StateStore store(dir.path());
        MorseApp app = startedCode(store);
        answerCorrectly(app);
        scored = app.score();
        REQUIRE(scored > 0);
        REQUIRE(app.highScore() == scored);
    }

    StateStore reopened(dir.path());
    MorseApp app(reopened, kSeed);
    app.onEnter();
    CHECK(app.highScore() == scored);
}

TEST_CASE("a beaten record is written before the run ends", "[morse]")
{
    TempDir dir;
    StateStore store(dir.path());
    MorseApp app = startedCode(store);

    answerCorrectly(app);
    const int after_one = app.score();

    // Still mid-run: the record has to be on disk already, the same rule Snake
    // follows.
    StateStore other(dir.path());
    CHECK(other.section("morse").getInt("highscore", 0) == after_one);
}

TEST_CASE("read offers four distinct letters, one of them right", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedGuess(store);
    runUntilKeying(app);

    for (int question = 0; question < 12; ++question)
    {
        const std::array<char, MorseApp::kChoices> &choices = app.choices();

        std::set<char> unique(choices.begin(), choices.end());
        CHECK(unique.size() == choices.size()); // no letter offered twice

        REQUIRE(app.answerIndex() >= 0);
        REQUIRE(app.answerIndex() < MorseApp::kChoices);
        CHECK(choices[static_cast<std::size_t>(app.answerIndex())] == app.target());

        const std::string_view pool =
            MorseApp::order().substr(0, static_cast<std::size_t>(app.poolSize()));
        for (const char option : choices)
        {
            CHECK(pool.find(option) != std::string_view::npos);
        }

        pickChoice(app, app.answerIndex());
        runUntilKeying(app);
    }
}

TEST_CASE("read prefers distractors of the same code length", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedGuess(store);

    // Widen the pool so every length has more than three letters to draw on.
    for (int i = 0; i < 100; ++i)
    {
        runUntilKeying(app);
        pickChoice(app, app.answerIndex());
    }
    REQUIRE(app.poolSize() == static_cast<int>(MorseApp::order().size()));

    for (int question = 0; question < 20; ++question)
    {
        runUntilKeying(app);
        const std::size_t want = MorseApp::patternFor(app.target()).size();
        for (const char option : app.choices())
        {
            // Otherwise the answer could be found by counting symbols instead of
            // reading them.
            CHECK(MorseApp::patternFor(option).size() == want);
        }
        pickChoice(app, app.answerIndex());
    }
}

TEST_CASE("every choice is centred inside the box its underline draws", "[morse]")
{
    StateStore store = StateStore::inMemory();
    Surface surface(64, 32);
    MorseApp app = startedGuess(store);

    // T, I, 1 and 0 light only three of their glyph's five columns, so a fixed
    // inset leaves them against the right edge of the cell. This is the case
    // worth proving, hence the count.
    int narrow_seen = 0;

    for (int question = 0; question < 8; ++question)
    {
        runUntilKeying(app);

        for (int cell = 0; cell < MorseApp::kChoices; ++cell)
        {
            while (app.choice() != cell)
            {
                send(app, InputType::Rotate, 1);
            }

            surface.clear();
            app.render(surface);

            // The underline spans exactly one cell, which locates it.
            int box_left = surface.width();
            int box_right = -1;
            for (int x = 0; x < surface.width(); ++x)
            {
                if (!(surface.pixel(x, 31) == Color::black()))
                {
                    box_left = std::min(box_left, x);
                    box_right = std::max(box_right, x);
                }
            }
            REQUIRE(box_right >= box_left);

            int ink_left = surface.width();
            int ink_right = -1;
            for (int y = 16; y <= 29; ++y)
            {
                for (int x = box_left; x <= box_right; ++x)
                {
                    if (!(surface.pixel(x, y) == Color::black()))
                    {
                        ink_left = std::min(ink_left, x);
                        ink_right = std::max(ink_right, x);
                    }
                }
            }
            REQUIRE(ink_right >= ink_left);

            const char letter = app.choices()[static_cast<std::size_t>(cell)];
            if (ink_right - ink_left + 1 < 8) // narrower than a full glyph at scale 2
            {
                ++narrow_seen;
            }

            INFO("letter=" << letter);
            CHECK(ink_left - box_left == box_right - ink_right);
        }

        pickChoice(app, app.answerIndex());
    }

    CHECK(narrow_seen > 0);
}

TEST_CASE("turning moves the cursor and a press commits it", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedGuess(store);
    runUntilKeying(app);

    REQUIRE(app.choice() == 0);

    send(app, InputType::Rotate, 1);
    CHECK(app.choice() == 1);

    send(app, InputType::Rotate, -1);
    CHECK(app.choice() == 0);

    send(app, InputType::Rotate, -1); // wraps
    CHECK(app.choice() == MorseApp::kChoices - 1);

    pickChoice(app, app.answerIndex());
    CHECK(app.phase() == MorseApp::Phase::Correct);
    CHECK(app.score() > 0);
    CHECK(app.lives() == MorseApp::kLives);
}

TEST_CASE("a wrong pick costs a life", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedGuess(store);
    runUntilKeying(app);

    pickChoice(app, (app.answerIndex() + 1) % MorseApp::kChoices);

    CHECK(app.phase() == MorseApp::Phase::Wrong);
    CHECK(app.lives() == MorseApp::kLives - 1);
    CHECK(app.score() == 0);
}

TEST_CASE("read runs a clock and ends after three misses", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedGuess(store);
    runUntilKeying(app);

    // No dash can be in flight here, so the timeout needs no grace.
    advance(app, MorseApp::kGuessStartLimit + 0.1F);
    CHECK(app.lives() == MorseApp::kLives - 1);

    for (int i = 0; i < MorseApp::kLives - 1; ++i)
    {
        runUntilKeying(app);
        pickChoice(app, (app.answerIndex() + 1) % MorseApp::kChoices);
    }

    advance(app, 2.0F);
    CHECK(app.phase() == MorseApp::Phase::Over);
}

TEST_CASE("a hold leaves read, because nothing here keys", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedGuess(store);
    runUntilKeying(app);

    CHECK_FALSE(app.paused()); // no pause menu: the hold is the way out

    send(app, InputType::LongPress);

    CHECK(app.screen() == MorseApp::Screen::Menu);
    CHECK(app.phase() == MorseApp::Phase::Over); // the run was banked, not abandoned
}

TEST_CASE("the two games keep separate records", "[morse]")
{
    TempDir dir;

    int read_score = 0;
    {
        StateStore store(dir.path());
        MorseApp app = startedGuess(store);
        runUntilKeying(app);
        pickChoice(app, app.answerIndex());
        read_score = app.score();
        REQUIRE(read_score > 0);

        CHECK(app.guessHighScore() == read_score);
        CHECK(app.highScore() == 0); // sending is a different skill
    }

    StateStore reopened(dir.path());
    CHECK(reopened.section("morse").getInt("readscore", 0) == read_score);
    CHECK(reopened.section("morse").getInt("highscore", -1) == -1);

    MorseApp app(reopened, kSeed);
    app.onEnter();
    CHECK(app.guessHighScore() == read_score);
    CHECK(app.highScore() == 0);
}

TEST_CASE("content sits centred, with any odd pixel on the left", "[morse]")
{
    StateStore store = StateStore::inMemory();
    Surface surface(64, 32);
    MorseApp app = startedStudy(store);

    // Walk to I ("..") — eleven pixels of code on a 64-wide panel, so the
    // remainder is odd and the rounding is actually visible.
    keyCurrent(app);
    advance(app, 1.0F);
    keyCurrent(app);
    advance(app, 1.0F);
    REQUIRE(app.studySymbol() == 'I');

    surface.clear();
    app.render(surface);

    const Margins code = marginsIn(surface, 24, 27);
    REQUIRE(code.left >= 0);
    CHECK(code.left - code.right == 1); // one pixel further right than centre
}

TEST_CASE("a dash hands its hold back to the clock", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedCode(store);
    runUntilKeying(app);

    for (int guard = 0; guard < 100 && MorseApp::patternFor(app.currentLetter()).find('-') ==
                                           std::string_view::npos;
         ++guard)
    {
        keyCurrent(app);
        runUntilKeying(app);
    }

    const std::string_view code = MorseApp::patternFor(app.currentLetter());
    REQUIRE(code.find('-') != std::string_view::npos);

    advance(app, 1.0F);
    const float before = app.remaining();

    std::size_t i = 0;
    while (code[i] != '-')
    {
        send(app, InputType::Press);
        ++i;
    }
    send(app, InputType::LongPress);

    CHECK(app.remaining() > before);
}

TEST_CASE("the worst code in the table is keyable at the tightest limit", "[morse]")
{
    StateStore store = StateStore::inMemory();
    MorseApp app = startedCode(store);

    // Drive the pool to the whole table and the limit to its floor.
    for (int i = 0; i < 100; ++i)
    {
        answerCorrectly(app);
    }
    REQUIRE(app.poolSize() == static_cast<int>(MorseApp::order().size()));
    REQUIRE(app.limit() == MorseApp::kMinLimit);

    runUntilKeying(app);
    for (int guard = 0; guard < 500 && app.currentLetter() != '0'; ++guard)
    {
        keyCurrent(app);
        runUntilKeying(app);
    }
    REQUIRE(app.currentLetter() == '0'); // "-----", the five-dash worst case
    REQUIRE(app.phase() == MorseApp::Phase::Keying);

    // Keyed the way a hand does it: hold out the threshold, release, repeat.
    // Three seconds of holding against a two-and-a-half second limit — this is
    // the case that was not hard but impossible.
    for (int i = 0; i < MorseApp::kMaxSymbols; ++i)
    {
        advance(app, 0.6F);
        send(app, InputType::LongPress);
        advance(app, 0.15F);
    }

    CHECK(app.phase() == MorseApp::Phase::Correct);
    CHECK(app.lives() == MorseApp::kLives);
}

TEST_CASE("every screen renders something", "[morse]")
{
    StateStore store = StateStore::inMemory();
    Surface surface(64, 32);

    const auto draws = [&surface](MorseApp &app)
    {
        surface.clear();
        app.render(surface);
        return anythingDrawn(surface);
    };

    MorseApp app(store, kSeed);
    app.onEnter();
    CHECK(draws(app)); // menu

    send(app, InputType::Press);
    advance(app, 0.05F);
    CHECK(draws(app)); // study, keying

    send(app, InputType::Rotate, 1);
    CHECK(draws(app)); // pause, three entries
    send(app, InputType::Press);

    send(app, wrongSymbol(app));
    CHECK(draws(app)); // study, wrong
    advance(app, 1.0F);
    keyCurrent(app);
    CHECK(draws(app)); // study, correct
    advance(app, 1.0F);

    choosePause(app, MorseApp::PauseAction::Menu);
    send(app, InputType::Rotate, 1);
    send(app, InputType::Press);
    CHECK(draws(app)); // quiz, asked from memory
    send(app, wrongSymbol(app));
    advance(app, 1.0F);
    CHECK(draws(app)); // quiz, answer revealed

    choosePause(app, MorseApp::PauseAction::Menu);
    send(app, InputType::Rotate, 1);
    send(app, InputType::Press);
    runUntilKeying(app);
    CHECK(draws(app)); // read, a code and four letters
    pickChoice(app, (app.answerIndex() + 1) % MorseApp::kChoices);
    REQUIRE(app.phase() == MorseApp::Phase::Wrong);
    CHECK(draws(app)); // read, the right letter shown beside the wrong pick
    send(app, InputType::LongPress);

    send(app, InputType::Rotate, 1);
    send(app, InputType::Press);
    CHECK(draws(app)); // game, counting in

    send(app, InputType::Rotate, 1);
    CHECK(draws(app)); // pause, two entries
    send(app, InputType::Press);

    runUntilKeying(app);
    send(app, InputType::Press);
    CHECK(draws(app)); // game, keying with one symbol sent or already judged

    for (int i = 0; i < MorseApp::kLives + 1; ++i)
    {
        runUntilKeying(app);
        if (app.phase() == MorseApp::Phase::Keying)
        {
            send(app, wrongSymbol(app));
        }
    }
    advance(app, 2.0F);
    REQUIRE(app.phase() == MorseApp::Phase::Over);
    CHECK(draws(app)); // game over
}
