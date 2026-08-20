// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/morse/morse.h"

#include "gfx/font.h"
#include "gfx/surface.h"
#include "hal/gestures.h"
#include "os/log.h"
#include "os/state.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace matrixos
{
namespace
{

constexpr std::string_view kSection = "morse";
constexpr std::string_view kHighScoreKey = "highscore";
// The stored key keeps its old spelling on purpose: renaming it to match the
// GUESS label would orphan the record already on every provisioned device.
constexpr std::string_view kGuessScoreKey = "readscore";

struct Entry
{
    char symbol;
    std::string_view code;
};

/// Shortest code first. That ordering is doing real work: it is the sequence the
/// study walks and the sequence the code game unlocks, so a first run is E and T
/// rather than Q and 7.
constexpr std::array<Entry, 36> kTable = {{
    {'E', "."},     {'T', "-"},     {'I', ".."},    {'A', ".-"},    {'N', "-."},    {'M', "--"},
    {'S', "..."},   {'U', "..-"},   {'R', ".-."},   {'W', ".--"},   {'D', "-.."},   {'K', "-.-"},
    {'G', "--."},   {'O', "---"},   {'H', "...."},  {'V', "...-"},  {'F', "..-."},  {'L', ".-.."},
    {'P', ".--."},  {'J', ".---"},  {'B', "-..."},  {'X', "-..-"},  {'C', "-.-."},  {'Y', "-.--"},
    {'Z', "--.."},  {'Q', "--.-"},  {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
    {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."}, {'0', "-----"},
}};

constexpr std::string_view kOrder = "ETIANMSURWDKGOHVFLPJBXCYZQ1234567890";

constexpr bool orderMatchesTable()
{
    if (kOrder.size() != kTable.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < kTable.size(); ++i)
    {
        if (kTable[i].symbol != kOrder[i])
        {
            return false;
        }
    }
    return true;
}

constexpr std::size_t longestCode()
{
    std::size_t longest = 0;
    for (const Entry &entry : kTable)
    {
        longest = std::max(longest, entry.code.size());
    }
    return longest;
}

// Both guard the layout below: the widest code has to fit across the panel, and
// the keying buffer is sized from kMaxSymbols.
static_assert(orderMatchesTable());
static_assert(longestCode() == static_cast<std::size_t>(MorseApp::kMaxSymbols));

// Borrowed from the other apps rather than invented, so the trainer reads as
// part of the same set: the red is Pomodoro's, the green and the gold Snake's.
constexpr Color kStudyAccent{0x22, 0xC8, 0xD8};
constexpr Color kQuizAccent{0xA8, 0x7C, 0xFF};
constexpr Color kGuessAccent{0xFF, 0x5E, 0x9A};
constexpr Color kCodeAccent{0xFF, 0xB0, 0x20};
constexpr Color kGood{0x22, 0xC5, 0x5E};
constexpr Color kBad{0xFF, 0x43, 0x26};
constexpr Color kRecord{255, 190, 0};
constexpr Color kLabel{140, 140, 160};
constexpr Color kWhite{255, 255, 255};

constexpr int kBarTop = 0;
constexpr int kBarHeight = 2;

constexpr int kMenuNameTop = 6;      // scale 2, rows 6 to 19
constexpr int kMenuSubtitleTop = 23; // rows 23 to 29
constexpr int kMenuDotRow = 31;
constexpr int kDotGap = 4;

constexpr int kPauseStep = 9;
constexpr int kMarkerWidth = 3;
constexpr int kMarkerGap = 3;
constexpr int kMarkerHeight = 3;

constexpr int kStudyCharTop = 6;  // scale 2, rows 6 to 19
constexpr int kStudyCodeTop = 24; // rows 24 to 27

constexpr int kStatusTop = 3;       // rows 3 to 9
constexpr int kCodeCharTop = 11;    // scale 2, rows 11 to 24
constexpr int kCodeSymbolsTop = 26; // rows 26 to 29
constexpr int kGuideRow = 31;
constexpr int kLifeLeft = 2;
constexpr int kLifeWidth = 4;
constexpr int kLifeHeight = 3;
constexpr int kLifeGap = 2;

// The count-in gets its own rows: sharing kCodeCharTop put the digit's last row
// on the label's first.
constexpr int kReadyCountTop = 9; // scale 2, rows 9 to 22
constexpr int kReadyLabelTop = 25;

// Snake's end screen to within one row: its score sits a row lower, which leaves
// three pixels of air above it and one below. Ours is centred in the gap. Snake
// has the same off-centre score and was deliberately left alone.
constexpr int kOverTitleTop = 3;
constexpr int kOverScoreTop = 11;
constexpr int kOverFooterTop = 25;

// Four scale-2 letters across the panel. The underline marks the whole ten-pixel
// cell; the letter is centred inside it, which is not the same as a fixed inset —
// T, I, 1 and 0 light only three of their glyph's columns.
constexpr int kChoiceWidth = 10;
constexpr int kChoiceGap = 5;
constexpr int kGuessCodeTop = 10; // rows 10 to 13
constexpr int kChoiceTop = 16;    // scale 2, rows 16 to 29
constexpr int kUnderlineTop = 30; // rows 30 to 31
constexpr int kUnderlineHeight = 2;
static_assert(MorseApp::kChoices * kChoiceWidth + (MorseApp::kChoices - 1) * kChoiceGap <= 64);
static_assert(MorseApp::kStartPool > MorseApp::kChoices); // always enough distractors

constexpr int kDotWidth = 4;
constexpr int kDashWidth = 9;
constexpr int kCodeHeight = 4;
constexpr int kCodeGap = 3;

constexpr int kPanelWidth = 64;
static_assert(MorseApp::kMaxSymbols * kDashWidth + (MorseApp::kMaxSymbols - 1) * kCodeGap <
              kPanelWidth);

constexpr float kBlinkPeriod = 0.7F;
constexpr float kReadyStep = 0.7F;
constexpr float kReadySeconds = 3.0F * kReadyStep;
constexpr float kCorrectSeconds = 0.45F;

// The code game dwells on a miss because that flash is the only time it shows the
// answer. A drill keeps the answer afterwards, so it only has to say "no".
constexpr float kCodeWrongSeconds = 1.1F;
constexpr float kDrillWrongSeconds = 0.6F;

/// What a dash costs to produce, mechanically. Read from the recognizer instead
/// of retyped, so changing the threshold cannot silently unbalance the code game.
constexpr float kDashSeconds =
    static_cast<float>(GestureRecognizer::Timing{}.long_press.count()) / 1000.0F;

constexpr int kLetterPoints = 10;
constexpr int kSpeedPoints = 10;
constexpr int kMaxStreakBonus = 10;

constexpr Color scaled(Color color, int percent)
{
    return Color{static_cast<std::uint8_t>(color.r * percent / 100),
                 static_cast<std::uint8_t>(color.g * percent / 100),
                 static_cast<std::uint8_t>(color.b * percent / 100)};
}

Color mix(Color from, Color to, float t)
{
    const float k = std::clamp(t, 0.0F, 1.0F);
    const auto lerp = [k](std::uint8_t a, std::uint8_t b)
    {
        return static_cast<std::uint8_t>(static_cast<float>(a) +
                                         (static_cast<float>(b) - static_cast<float>(a)) * k);
    };
    return {lerp(from.r, to.r), lerp(from.g, to.g), lerp(from.b, to.b)};
}

void fillRect(Surface &surface, int x, int y, int width, int height, Color color)
{
    for (int dy = 0; dy < height; ++dy)
    {
        for (int dx = 0; dx < width; ++dx)
        {
            surface.setPixel(x + dx, y + dy, color);
        }
    }
}

/// Rounds up, which puts the spare pixel of an odd remainder on the left.
///
/// It is not a detail: at scale 1 a string is always `6n - 1` pixels wide, so
/// every label on a 64-wide panel has an odd remainder, and rounding down left
/// the entire interface sitting one pixel to the left of centre.
int centred(const Surface &surface, int content_width)
{
    return (surface.width() - content_width + 1) / 2;
}

void drawCentered(Surface &surface, int y, std::string_view text, Color color, int scale = 1)
{
    drawTextCentered(surface, y, text, color, scale);
}

void drawProgressBar(Surface &surface, int y, float fraction, Color color)
{
    fillRect(surface, 0, y, surface.width(), kBarHeight, scaled(color, 15));
    fillRect(
        surface, 0, y,
        static_cast<int>(std::clamp(fraction, 0.0F, 1.0F) * static_cast<float>(surface.width())),
        kBarHeight, color);
}

int symbolWidth(char symbol)
{
    return symbol == '-' ? kDashWidth : kDotWidth;
}

int codeWidth(std::string_view code)
{
    int width = 0;
    for (std::size_t i = 0; i < code.size(); ++i)
    {
        width += symbolWidth(code[i]);
        if (i + 1 < code.size())
        {
            width += kCodeGap;
        }
    }
    return width;
}

/// Symbols before `active` are drawn in `done`, the one at `active` in `live`,
/// everything after it in `pending`. Passing the code's length therefore draws
/// the whole thing as done, and -1 draws it all as pending.
void drawCode(Surface &surface, int y, std::string_view code, int active, Color done, Color live,
              Color pending)
{
    int x = centred(surface, codeWidth(code));

    for (std::size_t i = 0; i < code.size(); ++i)
    {
        const int index = static_cast<int>(i);
        const Color color = index == active ? live : (index < active ? done : pending);
        fillRect(surface, x, y, symbolWidth(code[i]), kCodeHeight, color);
        x += symbolWidth(code[i]) + kCodeGap;
    }
}

/// Pagination, in the shape the settings app already uses.
void drawDots(Surface &surface, int y, int count, int current, Color color)
{
    const int total = count * kDotGap - (kDotGap - 1);
    int x = centred(surface, total);

    for (int i = 0; i < count; ++i)
    {
        surface.setPixel(x, y, i == current ? color : scaled(color, 30));
        x += kDotGap;
    }
}

/// Seconds one repetition of `code` takes, without the pause that follows it.
float codeSeconds(std::string_view code)
{
    float total = 0.0F;
    for (std::size_t i = 0; i < code.size(); ++i)
    {
        total += (code[i] == '-' ? 3.0F : 1.0F) * MorseApp::kUnit;
        if (i + 1 < code.size())
        {
            total += MorseApp::kUnit;
        }
    }
    return total;
}

struct Playhead
{
    int symbol = -1; ///< -1 once the code has finished: the pause before the repeat
    bool on = false;
};

Playhead playheadAt(std::string_view code, float elapsed)
{
    float t = 0.0F;

    for (std::size_t i = 0; i < code.size(); ++i)
    {
        t += (code[i] == '-' ? 3.0F : 1.0F) * MorseApp::kUnit;
        if (elapsed < t)
        {
            return {static_cast<int>(i), true};
        }

        if (i + 1 == code.size())
        {
            break;
        }

        t += MorseApp::kUnit;
        if (elapsed < t)
        {
            return {static_cast<int>(i), false};
        }
    }

    return {-1, false};
}

Color accentFor(MorseApp::Screen screen)
{
    switch (screen)
    {
    case MorseApp::Screen::Study:
        return kStudyAccent;
    case MorseApp::Screen::Quiz:
        return kQuizAccent;
    case MorseApp::Screen::Guess:
        return kGuessAccent;
    case MorseApp::Screen::Code:
        return kCodeAccent;
    case MorseApp::Screen::Menu:
        break;
    }
    return kWhite;
}

std::string_view modeName(MorseApp::Screen screen)
{
    switch (screen)
    {
    case MorseApp::Screen::Study:
        return "STUDY";
    case MorseApp::Screen::Quiz:
        return "QUIZ";
    case MorseApp::Screen::Guess:
        return "GUESS";
    case MorseApp::Screen::Code:
        return "CODE";
    case MorseApp::Screen::Menu:
        break;
    }
    return "?";
}

std::string_view pauseLabel(MorseApp::PauseAction action)
{
    switch (action)
    {
    case MorseApp::PauseAction::Resume:
        return "RESUME";
    case MorseApp::PauseAction::Menu:
        return "MENU";
    }
    return "?";
}

} // namespace

MorseApp::MorseApp(StateStore &store, std::uint32_t seed)
    : scores_(store.section(kSection)), random_(seed)
{
}

std::string_view MorseApp::patternFor(char symbol)
{
    const char upper =
        static_cast<char>(symbol >= 'a' && symbol <= 'z' ? symbol - ('a' - 'A') : symbol);

    for (const Entry &entry : kTable)
    {
        if (entry.symbol == upper)
        {
            return entry.code;
        }
    }
    return {};
}

std::string_view MorseApp::order()
{
    return kOrder;
}

char MorseApp::studySymbol() const
{
    return kOrder[static_cast<std::size_t>(study_)];
}

char MorseApp::currentLetter() const
{
    return screen_ == Screen::Study ? studySymbol() : target_;
}

/// STUDY and QUIZ are the same drill with the answer revealed at different
/// moments. The two scored modes are not drills: something is at stake.
bool MorseApp::isDrill() const
{
    return screen_ == Screen::Study || screen_ == Screen::Quiz;
}

std::string_view MorseApp::entered() const
{
    return {entered_.data(), static_cast<std::size_t>(entered_length_)};
}

int MorseApp::poolSize() const
{
    const int grown = kStartPool + correct_ / kCorrectPerNewLetter;
    return std::min(grown, static_cast<int>(kOrder.size()));
}

int MorseApp::pauseCount() const
{
    return kPauseEntries;
}

MorseApp::PauseAction MorseApp::pauseAction(int index) const
{
    return index == 0 ? PauseAction::Resume : PauseAction::Menu;
}

void MorseApp::onEnter()
{
    high_score_ = std::max(0, scores_.getInt(kHighScoreKey, 0));
    guess_high_ = std::max(0, scores_.getInt(kGuessScoreKey, 0));
}

void MorseApp::onExit()
{
    // A run cannot survive the app going off screen: its clock would keep the
    // score of a letter nobody was looking at.
    if ((screen_ == Screen::Code || screen_ == Screen::Guess) && phase_ != Phase::Over)
    {
        endRun();
    }
    paused_ = false;
}

void MorseApp::onInput(const InputEvent &event)
{
    if (paused_)
    {
        if (event.type == InputType::Rotate)
        {
            const int count = pauseCount();
            pause_index_ = ((pause_index_ + event.delta) % count + count) % count;
        }
        else if (event.type == InputType::Press)
        {
            applyPause();
        }
        return;
    }

    switch (screen_)
    {
    case Screen::Menu:
        if (event.type == InputType::Rotate)
        {
            const int count = static_cast<int>(kMenu.size());
            menu_ = ((menu_ + event.delta) % count + count) % count;
        }
        else if (event.type == InputType::Press)
        {
            switch (selected())
            {
            case Screen::Study:
                startStudy();
                break;
            case Screen::Quiz:
                startQuiz();
                break;
            case Screen::Guess:
                startGuess();
                break;
            default:
                startCode();
                break;
            }
        }
        break;

    case Screen::Study:
    case Screen::Quiz:
        // Every mode is keyed, so all of them spend Press and LongPress on the
        // key and the way out has to be Rotate.
        if (event.type == InputType::Rotate)
        {
            pause();
        }
        else if (phase_ == Phase::Keying)
        {
            if (event.type == InputType::Press)
            {
                key('.');
            }
            else if (event.type == InputType::LongPress)
            {
                key('-');
            }
        }
        break;

    case Screen::Guess:
        if (phase_ == Phase::Over)
        {
            if (event.type == InputType::Press)
            {
                startGuess();
            }
            else if (event.type == InputType::LongPress)
            {
                openMenu();
            }
            break;
        }

        // Nothing here keys, so the hold is free — and a mode driven by turning
        // wants its way out on the button, not on another turn.
        if (event.type == InputType::LongPress)
        {
            endRun();
            openMenu();
            break;
        }

        if (phase_ == Phase::Keying)
        {
            if (event.type == InputType::Rotate)
            {
                choice_ = ((choice_ + event.delta) % kChoices + kChoices) % kChoices;
            }
            else if (event.type == InputType::Press)
            {
                answerChoice();
            }
        }
        break;

    case Screen::Code:
        if (phase_ == Phase::Over)
        {
            if (event.type == InputType::Press)
            {
                startCode();
            }
            else if (event.type == InputType::LongPress)
            {
                openMenu();
            }
            break;
        }

        if (event.type == InputType::Rotate)
        {
            pause();
        }
        else if (phase_ == Phase::Keying)
        {
            if (event.type == InputType::Press)
            {
                key('.');
            }
            else if (event.type == InputType::LongPress)
            {
                key('-');
            }
        }
        break;
    }
}

void MorseApp::update(Duration dt)
{
    const float seconds = dt.count();
    blink_ += seconds;

    // A paused game must not lose its letter to the clock.
    if (paused_)
    {
        return;
    }

    switch (screen_)
    {
    case Screen::Menu:
        break;

    case Screen::Study:
    case Screen::Quiz:
        switch (phase_)
        {
        case Phase::Keying:
        {
            // The demonstration loops for as long as the letter is unanswered,
            // which is what makes the repetition purposeful rather than a
            // slideshow the operator waits out. It only runs once the answer is
            // on screen — in the quiz its rhythm would be the answer.
            if (!revealed_)
            {
                break;
            }

            const float span = codeSeconds(patternFor(currentLetter())) + kStudyPause;
            study_elapsed_ += seconds;
            if (study_elapsed_ >= span)
            {
                study_elapsed_ -= span;
            }
            break;
        }

        case Phase::Correct:
            phase_timer_ -= seconds;
            if (phase_timer_ <= 0.0F)
            {
                if (screen_ == Screen::Study)
                {
                    showStudy(study_ + 1);
                }
                else
                {
                    nextLetter();
                }
            }
            break;

        case Phase::Wrong:
            phase_timer_ -= seconds;
            if (phase_timer_ <= 0.0F)
            {
                // Same letter again, and in the quiz it is now on screen. Neither
                // drill costs anything but another go.
                entered_length_ = 0;
                study_elapsed_ = 0.0F;
                phase_ = Phase::Keying;
            }
            break;

        case Phase::Ready:
        case Phase::Over:
            break;
        }
        break;

    case Screen::Guess:
    case Screen::Code:
        switch (phase_)
        {
        case Phase::Ready:
            phase_timer_ -= seconds;
            if (phase_timer_ <= 0.0F)
            {
                nextLetter();
            }
            break;

        case Phase::Keying:
            remaining_ -= seconds;

            // One dash of grace before the clock is believed, but only where a
            // dash can be in flight. A hold in progress is invisible to an app —
            // the recognizer says nothing until the threshold — so failing at a
            // bare zero would cut the operator off mid-dash. Read never holds.
            if (remaining_ <= (screen_ == Screen::Code ? -kDashSeconds : 0.0F))
            {
                remaining_ = 0.0F;
                miss();
            }
            break;

        case Phase::Correct:
        case Phase::Wrong:
            phase_timer_ -= seconds;
            if (phase_timer_ <= 0.0F)
            {
                if (lives_ > 0)
                {
                    nextLetter();
                }
                else
                {
                    endRun();
                }
            }
            break;

        case Phase::Over:
            break;
        }
        break;
    }
}

void MorseApp::openMenu()
{
    screen_ = Screen::Menu;
    paused_ = false;
}

void MorseApp::pause()
{
    paused_ = true;

    // Resume is preselected, so a knock against the encoder costs one press
    // rather than a run.
    pause_index_ = 0;
}

void MorseApp::applyPause()
{
    const PauseAction action = pauseAction(pause_index_);
    paused_ = false;

    switch (action)
    {
    case PauseAction::Resume:
        break;

    case PauseAction::Menu:
        if (screen_ == Screen::Code && phase_ != Phase::Over)
        {
            endRun();
        }
        openMenu();
        break;
    }
}

void MorseApp::startStudy()
{
    screen_ = Screen::Study;
    showStudy(study_);
}

void MorseApp::showStudy(int index)
{
    const int count = static_cast<int>(kOrder.size());
    study_ = ((index % count) + count) % count;
    study_elapsed_ = 0.0F;
    entered_length_ = 0;

    // The study never withholds the answer; that is what makes it the study.
    revealed_ = true;
    phase_ = Phase::Keying;
}

void MorseApp::startQuiz()
{
    screen_ = Screen::Quiz;

    // Its own pool, starting where the code game's does: a quiz that opens on Q would
    // teach nothing but discouragement.
    correct_ = 0;
    target_ = 0;
    limit_ = kStartLimit;
    nextLetter();
}

void MorseApp::startCode()
{
    screen_ = Screen::Code;
    phase_ = Phase::Ready;
    phase_timer_ = kReadySeconds;

    score_ = 0;
    lives_ = kLives;
    correct_ = 0;
    streak_ = 0;
    best_streak_ = 0;
    limit_ = kStartLimit;
    min_limit_ = kMinLimit;
    limit_step_ = kLimitStep;

    // Full rather than empty, so the bar does not read as "out of time" while
    // the count-in runs.
    remaining_ = limit_;
    entered_length_ = 0;
    target_ = 0;
    beat_high_score_ = false;

    logInfo("morse code started, best {}", high_score_);
}

void MorseApp::startGuess()
{
    screen_ = Screen::Guess;
    phase_ = Phase::Ready;
    phase_timer_ = kReadySeconds;

    score_ = 0;
    lives_ = kLives;
    correct_ = 0;
    streak_ = 0;
    best_streak_ = 0;

    limit_ = kGuessStartLimit;
    min_limit_ = kGuessMinLimit;
    limit_step_ = kGuessLimitStep;
    remaining_ = limit_;

    target_ = 0;
    entered_length_ = 0;
    beat_high_score_ = false;

    logInfo("morse guess started, best {}", guess_high_);
}

/// Fills the four answers around `target_` and records where it landed.
///
/// Distractors are drawn from letters whose code is the *same length* wherever
/// there are enough of them. Picking at random would let most questions be
/// settled by counting symbols rather than reading them, which is not the skill
/// being trained.
void MorseApp::dealChoices()
{
    const std::string_view pool = kOrder.substr(0, static_cast<std::size_t>(poolSize()));
    const std::size_t want = patternFor(target_).size();

    std::array<char, 36> same{};
    std::array<char, 36> other{};
    std::size_t same_count = 0;
    std::size_t other_count = 0;

    for (const char candidate : pool)
    {
        if (candidate == target_)
        {
            continue;
        }
        if (patternFor(candidate).size() == want)
        {
            same[same_count++] = candidate;
        }
        else
        {
            other[other_count++] = candidate;
        }
    }

    std::shuffle(same.begin(), same.begin() + static_cast<std::ptrdiff_t>(same_count), random_);
    std::shuffle(other.begin(), other.begin() + static_cast<std::ptrdiff_t>(other_count), random_);

    choices_[0] = target_;
    std::size_t filled = 1;
    for (std::size_t i = 0; i < same_count && filled < choices_.size(); ++i)
    {
        choices_[filled++] = same[i];
    }
    for (std::size_t i = 0; i < other_count && filled < choices_.size(); ++i)
    {
        choices_[filled++] = other[i];
    }

    std::shuffle(choices_.begin(), choices_.end(), random_);

    answer_ = 0;
    for (std::size_t i = 0; i < choices_.size(); ++i)
    {
        if (choices_[i] == target_)
        {
            answer_ = static_cast<int>(i);
        }
    }
    choice_ = 0;
}

void MorseApp::answerChoice()
{
    if (choice_ == answer_)
    {
        hit();
    }
    else
    {
        miss();
    }
}

void MorseApp::nextLetter()
{
    const std::string_view pool = kOrder.substr(0, static_cast<std::size_t>(poolSize()));

    char pick = pool.front();
    if (pool.size() > 1)
    {
        std::uniform_int_distribution<std::size_t> choose(0, pool.size() - 1);
        do
        {
            pick = pool[choose(random_)];
        } while (pick == target_); // never the same letter twice running
    }

    target_ = pick;
    entered_length_ = 0;
    study_elapsed_ = 0.0F;
    remaining_ = limit_;

    if (screen_ == Screen::Guess)
    {
        dealChoices();
    }

    // A fresh letter is always asked for from memory; a miss is what gives it
    // away, and only until the next one.
    revealed_ = false;
    phase_ = Phase::Keying;
}

void MorseApp::key(char symbol)
{
    const std::string_view code = patternFor(currentLetter());
    const auto index = static_cast<std::size_t>(entered_length_);

    // A dash does not exist until the button has been held for the whole
    // threshold, so that time went on mechanics, not on recall. The code game hands
    // it straight back: `0` is five dashes, three seconds of pure holding,
    // against a limit that falls to two and a half. Without this it is not hard,
    // it is impossible.
    if (symbol == '-' && screen_ == Screen::Code)
    {
        remaining_ = std::min(limit_, remaining_ + kDashSeconds);
    }

    if (entered_length_ >= kMaxSymbols)
    {
        miss(); // unreachable while the table's longest code is kMaxSymbols
        return;
    }

    entered_[index] = symbol;
    ++entered_length_;

    // Checked symbol by symbol rather than on completion: a wrong dot is wrong
    // the moment it is sent, and waiting would spend the operator's clock on an
    // answer that cannot come right.
    if (index >= code.size() || code[index] != symbol)
    {
        miss();
        return;
    }

    if (static_cast<std::size_t>(entered_length_) == code.size())
    {
        hit();
    }
}

void MorseApp::hit()
{
    phase_ = Phase::Correct;
    phase_timer_ = kCorrectSeconds;

    if (isDrill())
    {
        // The quiz widens its pool too, but only on letters recalled unaided:
        // finishing one after it was handed to you is practice, not progress.
        if (screen_ == Screen::Quiz && !revealed_)
        {
            ++correct_;
        }
        return; // update() walks to the next letter
    }

    ++correct_;
    ++streak_;
    best_streak_ = std::max(best_streak_, streak_);

    // Speed is most of the score: the letter is worth ten, the clock up to ten
    // more, and a streak one apiece up to another ten.
    const float left = limit_ > 0.0F ? std::clamp(remaining_ / limit_, 0.0F, 1.0F) : 0.0F;
    score_ += kLetterPoints + static_cast<int>(std::lround(left * kSpeedPoints)) +
              std::min(streak_, kMaxStreakBonus);

    limit_ = std::max(min_limit_, limit_ - limit_step_);

    recordScore();
}

void MorseApp::miss()
{
    phase_ = Phase::Wrong;

    if (isDrill())
    {
        phase_timer_ = kDrillWrongSeconds;

        // The quiz spends the miss on teaching: the answer comes up and stays up
        // until the letter has been keyed, so a blank is never a dead end.
        revealed_ = true;
        return;
    }

    phase_timer_ = kCodeWrongSeconds;
    streak_ = 0;
    lives_ = std::max(0, lives_ - 1);
}

void MorseApp::endRun()
{
    phase_ = Phase::Over;
    recordScore();
    logInfo("morse run over, score {}, best streak {}", score_, best_streak_);
}

void MorseApp::recordScore()
{
    const bool reading = screen_ == Screen::Guess;
    int &best = reading ? guess_high_ : high_score_;

    if (score_ <= best)
    {
        return;
    }

    best = score_;
    beat_high_score_ = true;

    // Written as it is beaten, not at the end of the run: the same rule Snake
    // follows, and for the same reason (a pulled plug must not cost the record).
    scores_.setInt(reading ? kGuessScoreKey : kHighScoreKey, best);
    scores_.save();
}

void MorseApp::render(Surface &surface)
{
    if (paused_)
    {
        renderPause(surface);
        return;
    }

    switch (screen_)
    {
    case Screen::Menu:
        renderMenu(surface);
        return;
    case Screen::Study:
    case Screen::Quiz:
        renderDrill(surface);
        return;
    case Screen::Guess:
        if (phase_ == Phase::Over)
        {
            renderOver(surface);
        }
        else
        {
            renderGuess(surface);
        }
        return;
    case Screen::Code:
        if (phase_ == Phase::Over)
        {
            renderOver(surface);
        }
        else
        {
            renderCode(surface);
        }
        return;
    }
}

void MorseApp::renderMenu(Surface &surface) const
{
    const Screen mode = selected();
    const Color accent = accentFor(mode);

    fillRect(surface, 0, kBarTop, surface.width(), kBarHeight, accent);
    drawCentered(surface, kMenuNameTop, modeName(mode), kWhite, 2);

    // Each subtitle names the thing that mode does not do, since that is what
    // separates it from its neighbour in the list.
    std::string subtitle;
    switch (mode)
    {
    case Screen::Study:
        subtitle = "NO CLOCK";
        break;
    case Screen::Quiz:
        subtitle = "NO CLOCK";
        break;
    case Screen::Guess:
        subtitle = "HI " + std::to_string(guess_high_);
        break;
    default:
        subtitle = "HI " + std::to_string(high_score_);
        break;
    }
    drawCentered(surface, kMenuSubtitleTop, subtitle, kLabel);

    drawDots(surface, kMenuDotRow, static_cast<int>(kMenu.size()), menu_, accent);
}

void MorseApp::renderPause(Surface &surface) const
{
    const Color accent = accentFor(screen_);
    fillRect(surface, 0, kBarTop, surface.width(), kBarHeight, scaled(accent, 60));

    const int count = pauseCount();
    const int top = (surface.height() - ((count - 1) * kPauseStep + kGlyphHeight)) / 2 + 1;

    for (int i = 0; i < count; ++i)
    {
        const std::string_view label = pauseLabel(pauseAction(i));
        const int y = top + i * kPauseStep;
        const bool active = i == pause_index_;

        drawCentered(surface, y, label, active ? kWhite : scaled(kWhite, 28));

        if (active)
        {
            const int left = centredTextX(surface.width(), label) - kMarkerGap - kMarkerWidth;
            fillRect(surface, left, y + 2, kMarkerWidth, kMarkerHeight, accent);
        }
    }
}

void MorseApp::renderDrill(Surface &surface) const
{
    const bool study = screen_ == Screen::Study;
    const Color accent = accentFor(screen_);
    const char symbol = currentLetter();
    const std::string_view code = patternFor(symbol);

    // The study walks the alphabet, so it can show how far in it is. The quiz
    // draws at random, so its progress is how much of the alphabet it draws from.
    drawProgressBar(surface, kBarTop,
                    static_cast<float>(study ? study_ + 1 : poolSize()) /
                        static_cast<float>(kOrder.size()),
                    accent);

    // The character carries the demonstration — lit while the key would be down,
    // dim between symbols. That rhythm is what the code row cannot show, and it
    // is also why it stays off until the answer is public.
    const bool sounding =
        revealed_ && phase_ == Phase::Keying && playheadAt(code, study_elapsed_).on;

    const Color letter = phase_ == Phase::Correct ? kGood
                         : phase_ == Phase::Wrong ? kBad
                         : sounding               ? kWhite
                         : revealed_              ? scaled(kWhite, 25)
                                                  : kWhite;

    const char text[2] = {symbol, '\0'};
    drawCentered(surface, kStudyCharTop, text, letter, 2);

    if (phase_ == Phase::Correct || phase_ == Phase::Wrong)
    {
        const Color shade = phase_ == Phase::Correct ? kGood : kBad;
        drawCode(surface, kStudyCodeTop, code, static_cast<int>(code.size()), shade, shade, shade);
        return;
    }

    if (!revealed_)
    {
        // Asked for from memory: only what has been sent, and a rail so the row
        // does not read as broken while it is still empty.
        fillRect(surface, kLifeLeft, kGuideRow, surface.width() - 2 * kLifeLeft, 1,
                 scaled(kWhite, 12));
        drawCode(surface, kStudyCodeTop, entered(), entered_length_, kWhite, kWhite, kWhite);
        return;
    }

    // The answer on screen doubles as the operator's own progress: what has been
    // sent is white, the symbol due next is the accent, the rest waits in the
    // dark.
    drawCode(surface, kStudyCodeTop, code, entered_length_, kWhite, accent, scaled(accent, 22));
}

void MorseApp::renderCode(Surface &surface) const
{
    const float left = limit_ > 0.0F ? std::clamp(remaining_ / limit_, 0.0F, 1.0F) : 0.0F;

    fillRect(surface, 0, kBarTop, surface.width(), kBarHeight, scaled(kCodeAccent, 15));
    fillRect(surface, 0, kBarTop, static_cast<int>(left * static_cast<float>(surface.width())),
             kBarHeight, mix(kBad, kCodeAccent, left));

    for (int i = 0; i < kLives; ++i)
    {
        fillRect(surface, kLifeLeft + i * (kLifeWidth + kLifeGap), kStatusTop + 2, kLifeWidth,
                 kLifeHeight, i < lives_ ? kBad : scaled(kBad, 20));
    }

    // Right-aligned on its ink, so the score keeps the margin from its edge that
    // the lives keep from theirs.
    const std::string score = std::to_string(score_);
    drawText(surface, rightAlignedTextX(surface.width() - 1 - kLifeLeft, score), kStatusTop, score,
             kWhite);

    if (phase_ == Phase::Ready)
    {
        const int count = std::max(1, static_cast<int>(std::ceil(phase_timer_ / kReadyStep)));
        drawCentered(surface, kReadyCountTop, std::to_string(count), scaled(kCodeAccent, 85), 2);
        drawCentered(surface, kReadyLabelTop, "READY", kLabel);
        return;
    }

    const Color letter =
        phase_ == Phase::Correct ? kGood : (phase_ == Phase::Wrong ? kBad : kWhite);
    const char text[2] = {target_, '\0'};
    drawCentered(surface, kCodeCharTop, text, letter, 2);

    if (phase_ == Phase::Correct || phase_ == Phase::Wrong)
    {
        // A miss is the moment the letter is worth teaching, so the answer goes
        // up rather than just a cross.
        const std::string_view code = patternFor(target_);
        const Color shade = phase_ == Phase::Correct ? kGood : kBad;
        drawCode(surface, kCodeSymbolsTop, code, static_cast<int>(code.size()), shade, shade,
                 shade);
        return;
    }

    // Keying: only what has been sent. Nothing on screen hints at the length of
    // the answer, which is the part being tested.
    fillRect(surface, kLifeLeft, kGuideRow, surface.width() - 2 * kLifeLeft, 1, scaled(kWhite, 12));
    drawCode(surface, kCodeSymbolsTop, entered(), entered_length_, kWhite, kWhite, kWhite);
}

void MorseApp::renderGuess(Surface &surface) const
{
    const float left = limit_ > 0.0F ? std::clamp(remaining_ / limit_, 0.0F, 1.0F) : 0.0F;

    fillRect(surface, 0, kBarTop, surface.width(), kBarHeight, scaled(kGuessAccent, 15));
    fillRect(surface, 0, kBarTop, static_cast<int>(left * static_cast<float>(surface.width())),
             kBarHeight, mix(kBad, kGuessAccent, left));

    for (int i = 0; i < kLives; ++i)
    {
        fillRect(surface, kLifeLeft + i * (kLifeWidth + kLifeGap), kStatusTop + 2, kLifeWidth,
                 kLifeHeight, i < lives_ ? kBad : scaled(kBad, 20));
    }

    // Right-aligned on its ink, so the score keeps the margin from its edge that
    // the lives keep from theirs.
    const std::string score = std::to_string(score_);
    drawText(surface, rightAlignedTextX(surface.width() - 1 - kLifeLeft, score), kStatusTop, score,
             kWhite);

    if (phase_ == Phase::Ready)
    {
        const int count = std::max(1, static_cast<int>(std::ceil(phase_timer_ / kReadyStep)));
        drawCentered(surface, kReadyCountTop, std::to_string(count), scaled(kGuessAccent, 85), 2);
        drawCentered(surface, kReadyLabelTop, "READY", kLabel);
        return;
    }

    // The question: the code, whole and plainly lit. Reading it is the exercise.
    const std::string_view code = patternFor(target_);
    drawCode(surface, kGuessCodeTop, code, static_cast<int>(code.size()), kGuessAccent,
             kGuessAccent, kGuessAccent);

    const int row = centred(surface, kChoices * kChoiceWidth + (kChoices - 1) * kChoiceGap);

    for (int i = 0; i < kChoices; ++i)
    {
        const int x = row + i * (kChoiceWidth + kChoiceGap);
        const bool picked = i == choice_;
        const bool right = i == answer_;

        // While answering, only the cursor is lit. Afterwards the right letter
        // goes green whatever was chosen — a miss is the moment it is worth
        // showing — and a wrong pick goes red beside it.
        Color tint = scaled(kWhite, 30);
        if (phase_ == Phase::Keying)
        {
            tint = picked ? kWhite : scaled(kWhite, 30);
        }
        else if (right)
        {
            tint = kGood;
        }
        else if (picked && phase_ == Phase::Wrong)
        {
            tint = kBad;
        }

        // Centred within its own cell rather than nudged in by a constant: the
        // narrow glyphs would otherwise sit against the right edge of the box the
        // underline draws.
        const char text[2] = {choices_[static_cast<std::size_t>(i)], '\0'};
        drawText(surface, x + centredTextX(kChoiceWidth, text, 2), kChoiceTop, text, tint, 2);

        const bool marked = phase_ == Phase::Keying ? picked : (right || picked);
        if (marked)
        {
            const Color bar = phase_ == Phase::Keying ? kGuessAccent : (right ? kGood : kBad);
            fillRect(surface, x, kUnderlineTop, kChoiceWidth, kUnderlineHeight, bar);
        }
    }
}

void MorseApp::renderOver(Surface &surface) const
{
    fillRect(surface, 0, kBarTop, surface.width(), kBarHeight,
             beat_high_score_ ? kRecord : scaled(kCodeAccent, 45));

    const bool on = !beat_high_score_ || std::fmod(blink_, kBlinkPeriod) < kBlinkPeriod / 2.0F;
    if (on)
    {
        drawCentered(surface, kOverTitleTop, beat_high_score_ ? "NEW BEST" : "GAME OVER",
                     beat_high_score_ ? kRecord : kLabel);
    }

    drawCentered(surface, kOverScoreTop, std::to_string(score_), kWhite, 2);
    drawCentered(surface, kOverFooterTop,
                 "HI " + std::to_string(screen_ == Screen::Guess ? guess_high_ : high_score_),
                 kLabel);
}

} // namespace matrixos
