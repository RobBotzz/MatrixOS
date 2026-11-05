// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/spotify/spotify_app.h"

#include "gfx/font.h"
#include "gfx/surface.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace matrixos
{
namespace
{

constexpr Color kGreen{30, 215, 96};
constexpr Color kText{255, 255, 255};
constexpr Color kLabel{130, 145, 175};
constexpr Color kTrack{40, 44, 58};
constexpr Color kNoCover{24, 26, 34};

constexpr int kCoverSize = SpotifyService::kCoverSize;
constexpr int kTextLeft = kCoverSize + 2;
constexpr int kTextWidth = 64 - kTextLeft;

constexpr float kReadInterval = 0.25F;
constexpr float kScrollSpeed = 12.0F; ///< pixels per second
constexpr float kScrollPause = 1.5F;  ///< seconds at each end
constexpr int kVolumeStep = 2;
constexpr float kVolumeSettle = 0.4F;
constexpr float kVolumeShown = 1.5F;

constexpr int kListLeft = 1;
constexpr int kListWidth = 62;

struct Mode
{
    std::string_view top;
    std::string_view bottom;
    SpotifyList list;
};

constexpr std::array<Mode, 4> kModes{{
    {"MY", "PLAYLISTS", SpotifyList::Playlists},
    {"RECENTLY", "PLAYED", SpotifyList::Recent},
    {"TOP", "SONGS", SpotifyList::TopTracks},
    {"TOP", "ARTISTS", SpotifyList::TopArtists},
}};

std::string panelText(const std::string &text)
{
    std::string out;
    for (const char c : text)
    {
        if (static_cast<unsigned char>(c) < 0x80)
        {
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

/// A line that fits stays put; one that does not slides left and back, pausing
/// at both ends.
int scrollOffset(int width, int room, float time)
{
    const int overflow = width - room;
    if (overflow <= 0)
    {
        return 0;
    }
    const float travel = static_cast<float>(overflow) / kScrollSpeed;
    const float cycle = 2.0F * (travel + kScrollPause);
    float t = time - cycle * static_cast<float>(static_cast<int>(time / cycle));
    if (t < kScrollPause)
    {
        return 0;
    }
    t -= kScrollPause;
    if (t < travel)
    {
        return static_cast<int>(t * kScrollSpeed);
    }
    t -= travel;
    if (t < kScrollPause)
    {
        return overflow;
    }
    t -= kScrollPause;
    return overflow - static_cast<int>(t * kScrollSpeed);
}

void drawClipped(Surface &surface, int left, int room, int y, const std::string &text, Color color,
                 float time)
{
    Surface line(textWidth(text) + 1, kGlyphHeight);
    drawText(line, 0, 0, text, color);
    const int offset = scrollOffset(textWidth(text), room, time);
    for (int x = 0; x < room; ++x)
    {
        for (int row = 0; row < kGlyphHeight; ++row)
        {
            const int source = x + offset;
            if (source >= 0 && source < line.width())
            {
                const Color pixel = line.pixel(source, row);
                if (!(pixel == Color::black()))
                {
                    surface.setPixel(left + x, y + row, pixel);
                }
            }
        }
    }
}

void fill(Surface &surface, int x, int y, int width, int height, Color color)
{
    for (int dy = 0; dy < height; ++dy)
    {
        for (int dx = 0; dx < width; ++dx)
        {
            surface.setPixel(x + dx, y + dy, color);
        }
    }
}

} // namespace

SpotifyApp::SpotifyApp(SpotifyService &spotify) : spotify_(spotify) {}

void SpotifyApp::onEnter()
{
    spotify_.setActive(true);
    snapshot_ = spotify_.snapshot();
    revision_seen_ = snapshot_.revision;
    since_revision_ = 0.0F;
    pending_volume_ = -1;
    browsing_paused_ = false;
    in_list_ = false;
    spotify_.clearStart();
}

void SpotifyApp::onExit()
{
    spotify_.setActive(false);
}

bool SpotifyApp::browsing() const
{
    if (snapshot_.link != SpotifyLink::Connected)
    {
        return false;
    }
    return snapshot_.track ? browsing_paused_ : snapshot_.reachable;
}

void SpotifyApp::onBrowseInput(const InputEvent &event)
{
    if (snapshot_.start != SpotifyStart::Idle)
    {
        const bool answer = event.type == InputType::Press || event.type == InputType::Rotate;
        if (answer && snapshot_.start != SpotifyStart::Starting)
        {
            spotify_.clearStart();
            since_read_ = kReadInterval;
        }
        return;
    }

    const int count = in_list_ && snapshot_.list ? static_cast<int>(snapshot_.list->size())
                                                 : static_cast<int>(kModes.size());
    switch (event.type)
    {
    case InputType::Rotate:
        if (count > 0)
        {
            int &selected = in_list_ ? item_ : mode_;
            selected = ((selected + event.delta) % count + count) % count;
            scroll_ = 0.0F;
        }
        break;
    case InputType::Press:
        if (!in_list_)
        {
            in_list_ = true;
            item_ = 0;
            scroll_ = 0.0F;
            spotify_.loadList(kModes[static_cast<std::size_t>(mode_)].list);
            since_read_ = kReadInterval;
        }
        else if (snapshot_.list_state == SpotifyListState::Ready && count > 0)
        {
            spotify_.play(static_cast<std::size_t>(item_));
            since_read_ = kReadInterval;
        }
        break;
    case InputType::LongPress:
        if (in_list_)
        {
            in_list_ = false;
        }
        else
        {
            browsing_paused_ = false;
        }
        break;
    default:
        break;
    }
}

void SpotifyApp::onInput(const InputEvent &event)
{
    if (browsing())
    {
        onBrowseInput(event);
        return;
    }
    if (snapshot_.link != SpotifyLink::Connected || !snapshot_.track)
    {
        return;
    }

    switch (event.type)
    {
    case InputType::Press:
        spotify_.togglePlay();
        break;
    case InputType::DoublePress:
        spotify_.next();
        break;
    case InputType::LongPress:
        if (snapshot_.track->playing)
        {
            spotify_.previous();
        }
        else
        {
            browsing_paused_ = true;
            in_list_ = false;
        }
        break;
    case InputType::Rotate:
    {
        const int base = pending_volume_ >= 0
                             ? pending_volume_
                             : (snapshot_.track->volume >= 0 ? snapshot_.track->volume : 50);
        pending_volume_ = std::clamp(base + event.delta * kVolumeStep, 0, 100);
        since_turn_ = 0.0F;
        volume_shown_ = kVolumeShown;
        break;
    }
    default:
        break;
    }
}

void SpotifyApp::update(Duration dt)
{
    const float seconds = dt.count();
    since_revision_ += seconds;
    scroll_ += seconds;
    volume_shown_ = std::max(0.0F, volume_shown_ - seconds);

    if (pending_volume_ >= 0)
    {
        since_turn_ += seconds;
        if (since_turn_ >= kVolumeSettle)
        {
            spotify_.setVolume(pending_volume_);
            pending_volume_ = -1;
            since_read_ = kReadInterval; // the old level must not flash up
        }
    }

    since_read_ += seconds;
    if (since_read_ >= kReadInterval)
    {
        since_read_ = 0.0F;
        const std::string title = snapshot_.track ? snapshot_.track->title : std::string();
        snapshot_ = spotify_.snapshot();
        if (snapshot_.revision != revision_seen_)
        {
            revision_seen_ = snapshot_.revision;
            since_revision_ = 0.0F;
        }
        if (!snapshot_.track || snapshot_.track->title != title)
        {
            scroll_ = 0.0F;
        }
        if (snapshot_.track && snapshot_.track->playing)
        {
            browsing_paused_ = false;
            in_list_ = false;
        }
    }
}

void SpotifyApp::render(Surface &surface)
{
    switch (snapshot_.link)
    {
    case SpotifyLink::NotConnected:
        renderMessage(surface, "SPOTIFY", "NOT LINKED");
        return;
    case SpotifyLink::Connecting:
        renderMessage(surface, "SPOTIFY", "LINKING");
        return;
    case SpotifyLink::Failed:
        renderMessage(surface, "SIGN IN", "AGAIN");
        return;
    case SpotifyLink::Connected:
        break;
    }

    if (browsing())
    {
        renderBrowse(surface);
        return;
    }
    if (!snapshot_.track)
    {
        renderMessage(surface, "SPOTIFY", "OFFLINE");
        return;
    }
    renderTrack(surface, *snapshot_.track);
}

void SpotifyApp::renderBrowse(Surface &surface) const
{
    switch (snapshot_.start)
    {
    case SpotifyStart::Starting:
        renderMessage(surface, "STARTING", "");
        return;
    case SpotifyStart::NoDevice:
        renderMessage(surface, "OPEN", "SPOTIFY");
        return;
    case SpotifyStart::Failed:
        renderMessage(surface, "COULD NOT", "START");
        return;
    case SpotifyStart::Idle:
        break;
    }

    if (in_list_)
    {
        renderList(surface);
        return;
    }

    const Mode &mode = kModes[static_cast<std::size_t>(mode_)];
    renderMessage(surface, mode.top, mode.bottom);
    const int left = (surface.width() - static_cast<int>(kModes.size()) * 4 + 2) / 2;
    for (int i = 0; i < static_cast<int>(kModes.size()); ++i)
    {
        fill(surface, left + i * 4, 28, 2, 2, i == mode_ ? kGreen : kTrack);
    }
}

void SpotifyApp::renderList(Surface &surface) const
{
    switch (snapshot_.list_state)
    {
    case SpotifyListState::Idle:
    case SpotifyListState::Loading:
        renderMessage(surface, "LOADING", "");
        return;
    case SpotifyListState::Failed:
        renderMessage(surface, "COULD NOT", "LOAD");
        return;
    case SpotifyListState::SignIn:
        renderMessage(surface, "SIGN IN", "AGAIN");
        return;
    case SpotifyListState::Ready:
        break;
    }

    if (!snapshot_.list || snapshot_.list->empty())
    {
        renderMessage(surface, "NOTHING", "HERE");
        return;
    }

    const int count = static_cast<int>(snapshot_.list->size());
    const SpotifyItem &item =
        (*snapshot_.list)[static_cast<std::size_t>(std::min(item_, count - 1))];
    const std::string title = panelText(item.title);
    drawClipped(surface, kListLeft, kListWidth, 3, title.empty() ? "?" : title, kText, scroll_);
    drawClipped(surface, kListLeft, kListWidth, 13, panelText(item.subtitle), kLabel, scroll_);
    drawTextCentered(surface, 24, std::to_string(item_ + 1) + "/" + std::to_string(count), kGreen);
}

void SpotifyApp::renderTrack(Surface &surface, const Track &track) const
{
    if (snapshot_.cover &&
        snapshot_.cover->size() == static_cast<std::size_t>(kCoverSize * kCoverSize * 3))
    {
        const auto &rgb = *snapshot_.cover;
        std::size_t at = 0;
        for (int y = 0; y < kCoverSize; ++y)
        {
            for (int x = 0; x < kCoverSize; ++x, at += 3)
            {
                surface.setPixel(x, y, {rgb[at], rgb[at + 1], rgb[at + 2]});
            }
        }
    }
    else
    {
        fill(surface, 0, 0, kCoverSize, kCoverSize, kNoCover);
    }

    drawClipped(surface, kTextLeft, kTextWidth, 2, panelText(track.title), kText, scroll_);

    const int volume = pending_volume_ >= 0 ? pending_volume_ : track.volume;
    if (volume_shown_ > 0.0F && volume >= 0)
    {
        drawText(surface, kTextLeft, 12, std::to_string(volume) + "%", kGreen);
    }
    else
    {
        drawClipped(surface, kTextLeft, kTextWidth, 12, panelText(track.artist), kLabel, scroll_);
    }

    // Paused: two bars. Playing: a triangle.
    if (track.playing)
    {
        for (int row = 0; row < 5; ++row)
        {
            fill(surface, kTextLeft + 1, 21 + row, 1 + std::min(row, 4 - row), 1, kGreen);
        }
    }
    else
    {
        fill(surface, kTextLeft + 1, 21, 1, 5, kText);
        fill(surface, kTextLeft + 3, 21, 1, 5, kText);
    }

    const int width = kTextWidth - 1;
    fill(surface, kTextLeft, 29, width, 2, kTrack);
    if (track.duration_ms > 0)
    {
        const float elapsed = track.playing ? since_revision_ * 1000.0F : 0.0F;
        const float progress = std::min(1.0F, (static_cast<float>(track.progress_ms) + elapsed) /
                                                  static_cast<float>(track.duration_ms));
        fill(surface, kTextLeft, 29, static_cast<int>(progress * static_cast<float>(width)), 2,
             kGreen);
    }
}

void SpotifyApp::renderMessage(Surface &surface, std::string_view top,
                               std::string_view bottom) const
{
    drawTextCentered(surface, 7, top, kGreen);
    drawTextCentered(surface, 18, bottom, kLabel);
}

} // namespace matrixos
