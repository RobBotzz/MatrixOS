// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/http_types.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace matrixos
{

class StateSection;
class StateStore;

struct Track
{
    std::string title;
    std::string artist;
    int progress_ms = 0;
    int duration_ms = 0;
    bool playing = false;
    int volume = -1; ///< -1 when the device does not report one
    std::string cover_url;
};

/// `GET /v1/me/player` with a 200 answer. Anything without a track is `nullopt`.
std::optional<Track> parsePlayback(std::string_view json);

struct Tokens
{
    std::string access;
    std::string refresh; ///< empty when the answer did not rotate it
    int expires_in = 0;
};

std::optional<Tokens> parseTokens(std::string_view json);

/// Where the browser came from, carried through Spotify inside `state` so the
/// callback page can send the code back (ADR-0019).
std::string encodeState(std::string_view nonce, std::string_view origin);
std::optional<std::pair<std::string, std::string>> decodeState(std::string_view state);

/// A `.local` name, a private IPv4 address or the loopback, over plain HTTP —
/// the same rule as the callback page.
bool allowedOrigin(std::string_view origin);

bool validClientId(std::string_view client_id);

enum class SpotifyLink
{
    NotConnected,
    Connecting,
    Connected,
    Failed, ///< the refresh token stopped working; sign in again
};

std::string_view toString(SpotifyLink link);

struct SpotifySnapshot
{
    SpotifyLink link = SpotifyLink::NotConnected;
    std::optional<Track> track;
    std::shared_ptr<const std::vector<std::uint8_t>> cover; ///< 32x32 RGB, may be null
    bool reachable = true;
    std::uint64_t revision = 0; ///< counts up with every answer from Spotify
};

/// The owner's Spotify, through the owner's own Spotify app (ADR-0019). The
/// configuration page links it; the app reads `snapshot()` and asks for
/// commands, neither of which waits on the network (FR-27).
class SpotifyService
{
public:
    using Call = std::function<std::optional<HttpResult>(const HttpCall &call)>;
    using Get = std::function<std::optional<std::string>(const std::string &url)>;

    static constexpr const char *kRedirectUri = "https://robbotzz.github.io/MatrixOS/callback/";
    static constexpr const char *kScopes = "user-read-playback-state user-modify-playback-state";
    static constexpr int kCoverSize = 32;

    struct Timing
    {
        std::chrono::milliseconds poll{std::chrono::seconds(5)};
        std::chrono::milliseconds after_command{std::chrono::milliseconds(400)};
        std::chrono::milliseconds rate_limited{std::chrono::seconds(30)};
        std::chrono::milliseconds pending_link{std::chrono::minutes(10)};
    };

    SpotifyService(StateStore &store, Call call, Get get);
    SpotifyService(StateStore &store, Call call, Get get, Timing timing);
    ~SpotifyService();

    SpotifyService(const SpotifyService &) = delete;
    SpotifyService &operator=(const SpotifyService &) = delete;

    void begin();

    SpotifySnapshot snapshot() const;
    std::string clientId() const;

    /// Starts linking; returns Spotify's authorize URL, or empty if the input
    /// is not acceptable.
    std::string startLink(std::string_view client_id, std::string_view origin);

    /// The code back from Spotify. `state` must match the pending link; the
    /// pasted fallback passes none and relies on the pending link alone.
    bool finishLink(std::string_view code, std::optional<std::string_view> state);

    void unlink();
    void reset(); ///< FR-42: forgets the Client ID as well

    /// The app on screen or not. Spotify is asked nothing while it is not.
    void setActive(bool active);

    void togglePlay();
    void next();
    void previous();
    void setVolume(int percent);

    /// Runs queued work and, if due, one poll. The worker's loop body; public
    /// for the tests.
    void step();

private:
    void post(std::function<void()> job);
    void run();
    bool poll();
    void exchange(const std::string &code, const std::string &verifier,
                  const std::string &client_id);
    bool refreshToken();
    std::optional<HttpResult> api(std::string method, const std::string &path, bool send_body);
    void command(std::string method, std::string path);
    void updateCover(const std::string &url);

    StateSection &saved_;
    Call call_;
    Get get_;
    Timing timing_;

    mutable std::mutex mutex_;
    std::string client_id_;
    std::string refresh_;
    std::string access_;
    std::chrono::steady_clock::time_point access_expiry_{};
    SpotifyLink link_ = SpotifyLink::NotConnected;
    std::optional<Track> track_;
    std::string cover_url_;
    std::shared_ptr<const std::vector<std::uint8_t>> cover_;
    bool reachable_ = true;
    std::uint64_t revision_ = 0;

    struct PendingLink
    {
        std::string client_id;
        std::string verifier;
        std::string nonce;
        std::chrono::steady_clock::time_point started{};
    };
    std::optional<PendingLink> pending_;

    bool active_ = false;
    std::chrono::steady_clock::time_point next_poll_{};
    std::deque<std::function<void()>> jobs_;

    std::condition_variable wake_;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};

} // namespace matrixos
