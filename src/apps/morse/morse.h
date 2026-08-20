// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/app.h"

#include <array>
#include <cstdint>
#include <random>
#include <string_view>

namespace matrixos
{

class StateStore;
class StateSection;

/// Morse trainer in four modes, shown in the menu as STUDY, QUIZ, GUESS and CODE.
///
/// STUDY keeps the answer on screen, QUIZ asks for it from memory but hands it
/// over the moment you slip, CODE hides it and runs a clock, and GUESS inverts
/// the exercise: a code is shown and four letters are offered.
///
/// GUESS is also the only mode that never touches the key, which is why it is
/// the only one where `LongPress` is free to mean "back".
///
/// All three are keyed by the operator. They differ only in when the answer is
/// visible and what a mistake costs — which is why `revealed_` rather than three
/// separate screens does most of the work.
///
/// The dot/dash split is not invented here — it falls out of the gesture
/// recognizer. A tap under the long-press threshold arrives as `Press` on
/// release and is a dot; holding produces `LongPress` at 600 ms while the button
/// is still down, which is a dash, and the release that follows stays silent
/// (FR-10, ADR-0009). One hold is therefore exactly one dash.
///
/// That leaves `Rotate` as the only input the key does not already own, so it
/// carries the way out: turning opens a pause menu. `Home` cannot serve, because
/// the shell consumes it before an app ever sees it.
class MorseApp : public App
{
public:
    enum class Screen
    {
        Menu,
        Study, ///< the answer is on screen from the start
        Quiz,  ///< the answer is earned, or given away by a miss
        Guess, ///< the other direction: a code, four letters, pick one
        Code,  ///< the answer stays hidden, and the clock is running
    };

    enum class Phase
    {
        Ready,   ///< game only: counting in
        Keying,  ///< the operator is answering: sending it, or picking it
        Correct, ///< brief flash before moving on
        Wrong,   ///< brief flash; the drills retry, the code game charges a life
        Over,    ///< game only
    };

    enum class PauseAction
    {
        Resume,
        Menu,
    };

    /// The pause menu is the same everywhere it appears.
    static constexpr int kPauseEntries = 2;

    /// The longest code in the table. Digits need five, letters at most four.
    static constexpr int kMaxSymbols = 5;

    static constexpr int kLives = 3;

    /// Answers offered per question in Read.
    static constexpr int kChoices = 4;

    /// One Morse unit. A dot is one, a dash three, the gap between symbols one.
    /// Slow on purpose: this is read, not heard.
    static constexpr float kUnit = 0.18F;
    static constexpr float kStudyPause = 6.0F * kUnit;

    static constexpr float kStartLimit = 6.0F; // seconds allowed per letter
    static constexpr float kMinLimit = 2.5F;
    static constexpr float kLimitStep = 0.2F;

    // Recognising one of four is quicker work than recalling a code from nothing,
    // so Read runs on a tighter clock of its own.
    static constexpr float kGuessStartLimit = 5.0F;
    static constexpr float kGuessMinLimit = 2.0F;
    static constexpr float kGuessLimitStep = 0.15F;

    /// The pool starts at the shortest codes and widens as letters are landed,
    /// so the first run is `E` and `T` rather than `Q` and `7`.
    static constexpr int kStartPool = 6;
    static constexpr int kCorrectPerNewLetter = 3;

    MorseApp(StateStore &store, std::uint32_t seed);

    std::string_view name() const override { return "Morse"; }

    void onEnter() override;
    void onExit() override;
    void onInput(const InputEvent &event) override;
    void update(Duration dt) override;
    void render(Surface &surface) override;

    /// The code for `symbol`, or empty for anything not in the table. Case is
    /// ignored.
    static std::string_view patternFor(char symbol);

    /// Every symbol the trainer knows, shortest code first.
    static std::string_view order();

    Screen screen() const { return screen_; }
    Screen selected() const { return kMenu[static_cast<std::size_t>(menu_)]; }

    /// Orthogonal to `screen()`: the pause menu overlays whichever mode is live.
    bool paused() const { return paused_; }
    int pauseIndex() const { return pause_index_; }
    int pauseCount() const;
    PauseAction pauseAction(int index) const;

    /// The letter being keyed, whichever mode is running.
    char currentLetter() const;

    /// True in the study and the quiz: keyed, but with nothing at stake.
    bool isDrill() const;
    std::string_view entered() const;

    char studySymbol() const;
    int studyIndex() const { return study_; }

    /// Whether the answer is currently on screen. Always true in the study;
    /// in the quiz it turns on when a letter is missed and off with the next one.
    bool revealed() const { return revealed_; }

    Phase phase() const { return phase_; }
    char target() const { return target_; }
    int score() const { return score_; }

    /// Two records, because sending and reading are two skills.
    int highScore() const { return high_score_; }
    int guessHighScore() const { return guess_high_; }

    /// Read only: the letters on offer, which of them is under the cursor, and
    /// which one is right.
    const std::array<char, kChoices> &choices() const { return choices_; }
    int choice() const { return choice_; }
    int answerIndex() const { return answer_; }
    int lives() const { return lives_; }
    int streak() const { return streak_; }
    float remaining() const { return remaining_; }
    float limit() const { return limit_; }

    /// How many symbols the letter pool currently holds. QUIZ, GUESS and CODE
    /// all draw from it; it widens as letters are landed.
    int poolSize() const;

private:
    // Untimed practice first, then the two timed games, easier of the two first.
    static constexpr std::array<Screen, 4> kMenu = {Screen::Study, Screen::Quiz, Screen::Guess,
                                                    Screen::Code};

    void renderMenu(Surface &surface) const;
    void renderDrill(Surface &surface) const;
    void renderCode(Surface &surface) const;
    void renderGuess(Surface &surface) const;
    void renderOver(Surface &surface) const;
    void renderPause(Surface &surface) const;

    void openMenu();
    void pause();
    void applyPause();

    void startStudy();
    void showStudy(int index);
    void startQuiz();

    void startCode();
    void startGuess();
    void nextLetter();
    void dealChoices();
    void answerChoice();

    void key(char symbol);
    void hit();
    void miss();

    void endRun();
    void recordScore();

    StateSection &scores_;
    std::mt19937 random_;

    Screen screen_ = Screen::Menu;
    int menu_ = 0;
    float blink_ = 0.0F;

    bool paused_ = false;
    int pause_index_ = 0;

    bool revealed_ = false;

    /// Kept across visits, so returning to the study resumes where it stopped.
    int study_ = 0;
    float study_elapsed_ = 0.0F;

    Phase phase_ = Phase::Over;
    char target_ = 0;
    std::array<char, kMaxSymbols> entered_{};
    int entered_length_ = 0;

    std::array<char, kChoices> choices_{};
    int choice_ = 0;
    int answer_ = 0;

    float remaining_ = 0.0F;
    float limit_ = kStartLimit;

    // Set when a game starts, so the two of them can tighten at their own rates.
    float min_limit_ = kMinLimit;
    float limit_step_ = kLimitStep;

    float phase_timer_ = 0.0F;

    int score_ = 0;
    int lives_ = kLives;
    int correct_ = 0;
    int streak_ = 0;
    int best_streak_ = 0;
    int high_score_ = 0;
    int guess_high_ = 0;
    bool beat_high_score_ = false;
};

} // namespace matrixos
