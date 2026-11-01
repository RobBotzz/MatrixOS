// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/spotify.h"

#include "os/cover.h"
#include "os/log.h"
#include "os/pkce.h"
#include "os/state.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <regex>

namespace matrixos
{
namespace
{

constexpr std::string_view kSection = "spotify";
constexpr std::string_view kClientIdKey = "client_id";
constexpr std::string_view kRefreshKey = "refresh_token";

constexpr const char *kAuthorize = "https://accounts.spotify.com/authorize";
constexpr const char *kToken = "https://accounts.spotify.com/api/token";
constexpr const char *kApi = "https://api.spotify.com/v1";

/// Refreshed this long before Spotify says it expires.
constexpr std::chrono::seconds kExpiryMargin{60};

const std::string kFormHeader = "Content-Type: application/x-www-form-urlencoded";

} // namespace

std::optional<Track> parsePlayback(std::string_view json)
{
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("item") ||
        !root["item"].is_object())
    {
        return std::nullopt;
    }

    try
    {
        const auto &item = root["item"];
        Track track;
        track.title = item.value("name", "");
        track.duration_ms = item.value("duration_ms", 0);
        track.progress_ms = root.value("progress_ms", 0);
        track.playing = root.value("is_playing", false);

        std::vector<std::string> artists;
        if (item.contains("artists") && item["artists"].is_array())
        {
            for (const auto &artist : item["artists"])
            {
                artists.push_back(artist.value("name", ""));
            }
        }
        for (std::size_t i = 0; i < artists.size(); ++i)
        {
            track.artist += (i == 0 ? "" : ", ") + artists[i];
        }

        if (root.contains("device") && root["device"].is_object() &&
            root["device"]["volume_percent"].is_number())
        {
            track.volume = root["device"]["volume_percent"].get<int>();
        }

        // The smallest image is enough for 32x32; Spotify lists them largest first.
        if (item.contains("album") && item["album"].contains("images") &&
            item["album"]["images"].is_array() && !item["album"]["images"].empty())
        {
            track.cover_url = item["album"]["images"].back().value("url", "");
        }
        return track;
    }
    catch (const nlohmann::json::exception &)
    {
        return std::nullopt;
    }
}

std::optional<Tokens> parseTokens(std::string_view json)
{
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root["access_token"].is_string())
    {
        return std::nullopt;
    }
    Tokens tokens;
    tokens.access = root["access_token"].get<std::string>();
    tokens.refresh = root.value("refresh_token", "");
    tokens.expires_in = root.value("expires_in", 3600);
    return tokens;
}

std::string encodeState(std::string_view nonce, std::string_view origin)
{
    return std::string(nonce) + "." + base64Url(origin);
}

std::optional<std::pair<std::string, std::string>> decodeState(std::string_view state)
{
    const auto dot = state.rfind('.');
    if (dot == std::string_view::npos || dot == 0)
    {
        return std::nullopt;
    }
    std::optional<std::string> origin = base64UrlDecode(state.substr(dot + 1));
    if (!origin)
    {
        return std::nullopt;
    }
    return std::make_pair(std::string(state.substr(0, dot)), std::move(*origin));
}

bool allowedOrigin(std::string_view origin)
{
    static const std::regex kLocal(
        R"(^http://(matrixos-[0-9a-f]{4}\.local|localhost|127\.0\.0\.1|10\.\d{1,3}\.\d{1,3}\.\d{1,3}|192\.168\.\d{1,3}\.\d{1,3}|172\.(1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3})(:\d{1,5})?$)");
    return std::regex_match(origin.begin(), origin.end(), kLocal);
}

bool validClientId(std::string_view client_id)
{
    return client_id.size() == 32 && std::all_of(client_id.begin(), client_id.end(),
                                                 [](char c) { return std::isxdigit(c) != 0; });
}

std::string_view toString(SpotifyLink link)
{
    switch (link)
    {
    case SpotifyLink::NotConnected:
        return "not-connected";
    case SpotifyLink::Connecting:
        return "connecting";
    case SpotifyLink::Connected:
        return "connected";
    case SpotifyLink::Failed:
        return "failed";
    }
    return "unknown";
}

SpotifyService::SpotifyService(StateStore &store, Call call, Get get)
    : SpotifyService(store, std::move(call), std::move(get), Timing{})
{
}

SpotifyService::SpotifyService(StateStore &store, Call call, Get get, Timing timing)
    : saved_(store.section(kSection)), call_(std::move(call)), get_(std::move(get)), timing_(timing)
{
    client_id_ = saved_.getString(kClientIdKey, "");
    refresh_ = saved_.getString(kRefreshKey, "");
    if (!client_id_.empty() && !refresh_.empty())
    {
        link_ = SpotifyLink::Connected;
    }
}

SpotifyService::~SpotifyService()
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

void SpotifyService::begin()
{
    worker_ = std::thread([this] { run(); });
}

SpotifySnapshot SpotifyService::snapshot() const
{
    std::lock_guard lock(mutex_);
    return {link_, track_, cover_, reachable_, revision_};
}

std::string SpotifyService::clientId() const
{
    std::lock_guard lock(mutex_);
    return client_id_;
}

std::string SpotifyService::startLink(std::string_view client_id, std::string_view origin)
{
    if (!validClientId(client_id) || !allowedOrigin(origin))
    {
        return {};
    }

    PendingLink pending;
    pending.client_id = std::string(client_id);
    pending.verifier = randomToken();
    pending.nonce = randomToken().substr(0, 24);
    pending.started = std::chrono::steady_clock::now();

    const std::string url = std::string(kAuthorize) + "?" +
                            formEncode({{"client_id", pending.client_id},
                                        {"response_type", "code"},
                                        {"redirect_uri", kRedirectUri},
                                        {"scope", kScopes},
                                        {"code_challenge_method", "S256"},
                                        {"code_challenge", codeChallenge(pending.verifier)},
                                        {"state", encodeState(pending.nonce, origin)}});

    std::lock_guard lock(mutex_);
    pending_ = std::move(pending);
    return url;
}

bool SpotifyService::finishLink(std::string_view code, std::optional<std::string_view> state)
{
    PendingLink pending;
    {
        std::lock_guard lock(mutex_);
        if (!pending_ ||
            std::chrono::steady_clock::now() - pending_->started > timing_.pending_link)
        {
            return false;
        }
        if (state)
        {
            const auto decoded = decodeState(*state);
            if (!decoded || decoded->first != pending_->nonce)
            {
                return false;
            }
        }
        if (code.empty())
        {
            return false;
        }
        pending = std::move(*pending_);
        pending_.reset();
        link_ = SpotifyLink::Connecting;
    }

    post([this, code = std::string(code), pending]
         { exchange(code, pending.verifier, pending.client_id); });
    return true;
}

void SpotifyService::unlink()
{
    std::lock_guard lock(mutex_);
    refresh_.clear();
    access_.clear();
    track_.reset();
    cover_.reset();
    cover_url_.clear();
    link_ = SpotifyLink::NotConnected;
    saved_.setString(kRefreshKey, "");
    saved_.save();
    logInfo("spotify: unlinked");
}

void SpotifyService::reset()
{
    unlink();
    std::lock_guard lock(mutex_);
    client_id_.clear();
    pending_.reset();
    saved_.setString(kClientIdKey, "");
    saved_.save();
}

void SpotifyService::setActive(bool active)
{
    {
        std::lock_guard lock(mutex_);
        active_ = active;
        next_poll_ = std::chrono::steady_clock::now();
    }
    wake_.notify_all();
}

void SpotifyService::togglePlay()
{
    bool playing = false;
    {
        std::lock_guard lock(mutex_);
        if (track_)
        {
            playing = track_->playing;
            track_->playing = !playing;
            ++revision_;
        }
    }
    post([this, playing] { command("PUT", playing ? "/me/player/pause" : "/me/player/play"); });
}

void SpotifyService::next()
{
    post([this] { command("POST", "/me/player/next"); });
}

void SpotifyService::previous()
{
    post([this] { command("POST", "/me/player/previous"); });
}

void SpotifyService::setVolume(int percent)
{
    const int volume = std::clamp(percent, 0, 100);
    {
        std::lock_guard lock(mutex_);
        if (track_)
        {
            track_->volume = volume;
            ++revision_;
        }
    }
    post([this, volume]
         { command("PUT", "/me/player/volume?volume_percent=" + std::to_string(volume)); });
}

void SpotifyService::post(std::function<void()> job)
{
    {
        std::lock_guard lock(mutex_);
        jobs_.push_back(std::move(job));
    }
    wake_.notify_all();
}

void SpotifyService::step()
{
    for (;;)
    {
        std::function<void()> job;
        {
            std::lock_guard lock(mutex_);
            if (jobs_.empty())
            {
                break;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        job();
    }

    bool due = false;
    {
        std::lock_guard lock(mutex_);
        due = active_ && link_ == SpotifyLink::Connected &&
              std::chrono::steady_clock::now() >= next_poll_;
        if (due)
        {
            next_poll_ = std::chrono::steady_clock::now() + timing_.poll;
        }
    }
    if (due)
    {
        poll();
    }
}

void SpotifyService::run()
{
    while (!stopping_)
    {
        step();

        std::unique_lock lock(mutex_);
        const auto until = active_ ? next_poll_ : std::chrono::steady_clock::now() + timing_.poll;
        wake_.wait_until(lock, until, [this] { return stopping_.load() || !jobs_.empty(); });
    }
}

void SpotifyService::exchange(const std::string &code, const std::string &verifier,
                              const std::string &client_id)
{
    HttpCall request;
    request.method = "POST";
    request.url = kToken;
    request.headers = {kFormHeader};
    request.body = formEncode({{"grant_type", "authorization_code"},
                               {"code", code},
                               {"redirect_uri", kRedirectUri},
                               {"client_id", client_id},
                               {"code_verifier", verifier}});

    const std::optional<HttpResult> result = call_(request);
    const std::optional<Tokens> tokens =
        result && result->status == 200 ? parseTokens(result->body) : std::nullopt;

    std::lock_guard lock(mutex_);
    if (!tokens || tokens->refresh.empty())
    {
        link_ = SpotifyLink::Failed;
        logWarn("spotify: linking failed (status {})", result ? result->status : 0);
        return;
    }

    client_id_ = client_id;
    refresh_ = tokens->refresh;
    access_ = tokens->access;
    access_expiry_ = std::chrono::steady_clock::now() + std::chrono::seconds(tokens->expires_in);
    link_ = SpotifyLink::Connected;
    next_poll_ = std::chrono::steady_clock::now();
    saved_.setString(kClientIdKey, client_id_);
    saved_.setString(kRefreshKey, refresh_);
    saved_.save();
    logInfo("spotify: linked");
}

bool SpotifyService::refreshToken()
{
    std::string refresh;
    std::string client_id;
    {
        std::lock_guard lock(mutex_);
        refresh = refresh_;
        client_id = client_id_;
    }
    if (refresh.empty())
    {
        return false;
    }

    HttpCall request;
    request.method = "POST";
    request.url = kToken;
    request.headers = {kFormHeader};
    request.body = formEncode(
        {{"grant_type", "refresh_token"}, {"refresh_token", refresh}, {"client_id", client_id}});

    const std::optional<HttpResult> result = call_(request);
    std::lock_guard lock(mutex_);
    if (!result)
    {
        reachable_ = false;
        return false;
    }
    const std::optional<Tokens> tokens =
        result->status == 200 ? parseTokens(result->body) : std::nullopt;
    if (!tokens)
    {
        if (result->status == 400 || result->status == 401)
        {
            link_ = SpotifyLink::Failed;
            refresh_.clear();
            saved_.setString(kRefreshKey, "");
            saved_.save();
            logWarn("spotify: the link was revoked, sign in again");
        }
        return false;
    }

    access_ = tokens->access;
    access_expiry_ = std::chrono::steady_clock::now() + std::chrono::seconds(tokens->expires_in);
    if (!tokens->refresh.empty() && tokens->refresh != refresh_)
    {
        refresh_ = tokens->refresh;
        saved_.setString(kRefreshKey, refresh_);
        saved_.save();
    }
    return true;
}

std::optional<HttpResult> SpotifyService::api(std::string method, const std::string &path,
                                              bool send_body)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        std::string access;
        {
            std::lock_guard lock(mutex_);
            if (access_.empty() ||
                std::chrono::steady_clock::now() + kExpiryMargin >= access_expiry_)
            {
                access.clear();
            }
            else
            {
                access = access_;
            }
        }
        if (access.empty())
        {
            if (!refreshToken())
            {
                return std::nullopt;
            }
            std::lock_guard lock(mutex_);
            access = access_;
        }

        HttpCall request;
        request.method = method;
        request.url = std::string(kApi) + path;
        request.headers = {"Authorization: Bearer " + access};
        request.send_body = send_body;

        std::optional<HttpResult> result = call_(request);
        if (result && result->status == 401 && attempt == 0)
        {
            std::lock_guard lock(mutex_);
            access_.clear();
            continue;
        }
        return result;
    }
    return std::nullopt;
}

bool SpotifyService::poll()
{
    const std::optional<HttpResult> result = api("GET", "/me/player", false);

    std::string cover_url;
    {
        std::lock_guard lock(mutex_);
        reachable_ = result.has_value();
        if (!result)
        {
            return false;
        }
        if (result->status == 429)
        {
            next_poll_ = std::chrono::steady_clock::now() + timing_.rate_limited;
            return false;
        }
        if (result->status == 204)
        {
            track_.reset();
        }
        else if (result->status == 200)
        {
            track_ = parsePlayback(result->body);
        }
        else
        {
            return false;
        }
        ++revision_;
        if (track_ && track_->cover_url != cover_url_)
        {
            cover_url = track_->cover_url;
        }
    }

    if (!cover_url.empty())
    {
        updateCover(cover_url);
    }
    return true;
}

void SpotifyService::updateCover(const std::string &url)
{
    if (url.rfind("https://", 0) != 0)
    {
        return;
    }
    const std::optional<std::string> bytes = get_(url);
    std::optional<std::vector<std::uint8_t>> cover =
        bytes ? decodeCover(*bytes, kCoverSize) : std::nullopt;

    std::lock_guard lock(mutex_);
    cover_url_ = url;
    cover_ = cover ? std::make_shared<const std::vector<std::uint8_t>>(std::move(*cover)) : nullptr;
}

void SpotifyService::command(std::string method, std::string path)
{
    const std::optional<HttpResult> result = api(std::move(method), path, true);
    if (result && result->status >= 300)
    {
        logWarn("spotify: {} answered {}", path, result->status);
    }
    std::lock_guard lock(mutex_);
    next_poll_ = std::chrono::steady_clock::now() + timing_.after_command;
}

} // namespace matrixos
