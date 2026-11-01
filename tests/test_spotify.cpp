// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/spotify/spotify_app.h"
#include "gfx/surface.h"
#include "net/http_get.h"
#include "net/spotify_routes.h"
#include "os/cover.h"
#include "os/pkce.h"
#include "os/spotify.h"
#include "os/state.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <functional>
#include <map>

using namespace matrixos;
using Catch::Matchers::ContainsSubstring;
using matrixos::test::TempDir;

namespace
{

constexpr const char *kClientId = "0123456789abcdef0123456789abcdef";
constexpr const char *kOrigin = "http://matrixos-f74b.local";

constexpr const char *kPlaying = R"({
 "is_playing": true, "progress_ms": 30000,
 "device": {"volume_percent": 40},
 "item": {"name": "Song", "duration_ms": 200000,
  "artists": [{"name": "One"}, {"name": "Two"}],
  "album": {"images": [{"url": "https://i.scdn.co/image/big", "width": 640},
                       {"url": "https://i.scdn.co/image/small", "width": 64}]}}})";

constexpr const char *kTokens =
    R"({"access_token":"AAA","token_type":"Bearer","expires_in":3600,"refresh_token":"RRR"})";

std::string hex(const std::array<std::uint8_t, 32> &digest)
{
    std::string out;
    for (const auto byte : digest)
    {
        static constexpr char kDigits[] = "0123456789abcdef";
        out += kDigits[byte >> 4];
        out += kDigits[byte & 0xf];
    }
    return out;
}

/// Answers by "METHOD url-prefix"; records every call.
struct FakeSpotify
{
    std::vector<HttpCall> calls;
    std::function<std::optional<HttpResult>(const HttpCall &)> respond;

    SpotifyService::Call call()
    {
        return [this](const HttpCall &request)
        {
            calls.push_back(request);
            return respond(request);
        };
    }

    static SpotifyService::Get noCover()
    {
        return [](const std::string &) { return std::nullopt; };
    }

    bool called(const std::string &method, const std::string &path) const
    {
        for (const auto &c : calls)
        {
            if (c.method == method && c.url.find(path) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }
};

std::optional<HttpResult> ok(std::string body)
{
    return HttpResult{200, std::move(body)};
}

/// A service already linked, with the token endpoint answering `kTokens`.
std::unique_ptr<SpotifyService>
linked(StateStore &store, FakeSpotify &fake,
       std::function<std::optional<HttpResult>(const HttpCall &)> player)
{
    fake.respond = [player](const HttpCall &request) -> std::optional<HttpResult>
    {
        if (request.url.find("accounts.spotify.com/api/token") != std::string::npos)
        {
            return ok(kTokens);
        }
        return player(request);
    };
    auto spotify = std::make_unique<SpotifyService>(store, fake.call(), FakeSpotify::noCover());
    const std::string url = spotify->startLink(kClientId, kOrigin);
    const std::string state = url.substr(url.find("state=") + 6);
    REQUIRE(spotify->finishLink("CODE", urlDecode(state)));
    spotify->step();
    REQUIRE(spotify->snapshot().link == SpotifyLink::Connected);
    fake.calls.clear();
    return spotify;
}

} // namespace

TEST_CASE("SHA-256 matches the FIPS test vectors")
{
    CHECK(hex(sha256("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hex(sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(hex(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("the PKCE challenge matches RFC 7636, appendix B")
{
    CHECK(codeChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") ==
          "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
    CHECK(randomToken().size() == 64);
    CHECK(randomToken() != randomToken());
}

TEST_CASE("base64url survives a round trip and refuses foreign characters")
{
    const std::string text = "http://matrixos-f74b.local:8080";
    CHECK(base64UrlDecode(base64Url(text)) == text);
    CHECK_FALSE(base64UrlDecode("ab+/"));
}

TEST_CASE("the origin travels through state")
{
    const auto decoded = decodeState(encodeState("nonce", kOrigin));
    REQUIRE(decoded);
    CHECK(decoded->first == "nonce");
    CHECK(decoded->second == kOrigin);
    CHECK_FALSE(decodeState("no-dot-here"));
}

TEST_CASE("only local origins are accepted")
{
    CHECK(allowedOrigin("http://matrixos-f74b.local"));
    CHECK(allowedOrigin("http://192.168.178.30"));
    CHECK(allowedOrigin("http://10.42.0.1:8080"));
    CHECK(allowedOrigin("http://172.16.0.5"));
    CHECK(allowedOrigin("http://localhost:8099"));

    CHECK_FALSE(allowedOrigin("https://matrixos-f74b.local"));
    CHECK_FALSE(allowedOrigin("http://evil.example"));
    CHECK_FALSE(allowedOrigin("http://matrixos-f74b.local.evil.example"));
    CHECK_FALSE(allowedOrigin("http://172.32.0.1"));
    CHECK_FALSE(allowedOrigin("http://192.168.1.1/path"));
}

TEST_CASE("a Client ID is 32 hex characters")
{
    CHECK(validClientId(kClientId));
    CHECK_FALSE(validClientId("0123"));
    CHECK_FALSE(validClientId("0123456789abcdef0123456789abcdeg"));
}

TEST_CASE("the player state parses into a track")
{
    const std::optional<Track> track = parsePlayback(kPlaying);
    REQUIRE(track);
    CHECK(track->title == "Song");
    CHECK(track->artist == "One, Two");
    CHECK(track->playing);
    CHECK(track->progress_ms == 30000);
    CHECK(track->volume == 40);
    CHECK(track->cover_url == "https://i.scdn.co/image/small");
    CHECK_FALSE(parsePlayback(R"({"is_playing":false})"));
}

TEST_CASE("curl's configuration quotes everything and carries the secrets")
{
    HttpCall call;
    call.method = "POST";
    call.url = "https://example.com/a?b=\"c\"";
    call.headers = {"Authorization: Bearer secret"};
    call.body = "x=1";

    const std::string config = curlConfig(call);
    CHECK_THAT(config, ContainsSubstring("url = \"https://example.com/a?b=\\\"c\\\"\""));
    CHECK_THAT(config, ContainsSubstring("header = \"Authorization: Bearer secret\""));
    CHECK_THAT(config, ContainsSubstring("data-binary = \"x=1\""));
}

TEST_CASE("linking runs PKCE and keeps the refresh token")
{
    TempDir dir;
    FakeSpotify fake;
    fake.respond = [](const HttpCall &) { return ok(kTokens); };
    {
        StateStore store(dir.path());
        SpotifyService spotify(store, fake.call(), FakeSpotify::noCover());

        CHECK(spotify.startLink("not-a-client-id", kOrigin).empty());
        CHECK(spotify.startLink(kClientId, "http://evil.example").empty());

        const std::string url = spotify.startLink(kClientId, kOrigin);
        CHECK_THAT(url, ContainsSubstring("client_id=" + std::string(kClientId)));
        CHECK_THAT(url, ContainsSubstring("code_challenge_method=S256"));
        CHECK_THAT(url, ContainsSubstring("redirect_uri=https%3A%2F%2Frobbotzz.github.io"));

        CHECK_FALSE(spotify.finishLink("CODE", std::string_view("wrong.state")));
        const std::string state = urlDecode(url.substr(url.find("state=") + 6));
        REQUIRE(spotify.finishLink("CODE", state));
        CHECK(spotify.snapshot().link == SpotifyLink::Connecting);

        spotify.step();
        CHECK(spotify.snapshot().link == SpotifyLink::Connected);
        REQUIRE(fake.calls.size() == 1);
        CHECK_THAT(fake.calls[0].body, ContainsSubstring("code_verifier="));
        CHECK_THAT(fake.calls[0].body, ContainsSubstring("grant_type=authorization_code"));
    }

    StateStore store(dir.path());
    SpotifyService again(store, fake.call(), FakeSpotify::noCover());
    CHECK(again.snapshot().link == SpotifyLink::Connected);
    CHECK(again.clientId() == kClientId);
}

TEST_CASE("Spotify is asked nothing while the app is off screen")
{
    StateStore store = StateStore::inMemory();
    FakeSpotify fake;
    auto spotify = linked(store, fake, [](const HttpCall &) { return ok(kPlaying); });

    spotify->step();
    CHECK(fake.calls.empty());

    spotify->setActive(true);
    spotify->step();
    CHECK(fake.called("GET", "/v1/me/player"));
    REQUIRE(spotify->snapshot().track);
    CHECK(spotify->snapshot().track->title == "Song");

    spotify->setActive(false);
    fake.calls.clear();
    spotify->step();
    CHECK(fake.calls.empty());
}

TEST_CASE("an expired access token is refreshed and the call repeated")
{
    StateStore store = StateStore::inMemory();
    FakeSpotify fake;
    int players = 0;
    auto spotify = linked(store, fake,
                          [&players](const HttpCall &) -> std::optional<HttpResult>
                          {
                              ++players;
                              return players == 1 ? HttpResult{401, ""} : *ok(kPlaying);
                          });

    spotify->setActive(true);
    spotify->step();
    CHECK(players == 2);
    CHECK(fake.called("POST", "accounts.spotify.com/api/token"));
    CHECK(spotify->snapshot().track);
}

TEST_CASE("nothing playing and a revoked link are both told apart")
{
    StateStore store = StateStore::inMemory();
    FakeSpotify fake;
    auto spotify = linked(store, fake, [](const HttpCall &) { return HttpResult{204, ""}; });
    spotify->setActive(true);
    spotify->step();
    CHECK(spotify->snapshot().link == SpotifyLink::Connected);
    CHECK_FALSE(spotify->snapshot().track);

    fake.respond = [](const HttpCall &request) -> std::optional<HttpResult>
    {
        if (request.url.find("api/token") != std::string::npos)
        {
            return HttpResult{400, R"({"error":"invalid_grant"})"};
        }
        return HttpResult{401, ""};
    };
    spotify->setActive(true);
    spotify->step();
    CHECK(spotify->snapshot().link == SpotifyLink::Failed);
}

TEST_CASE("press, double-press, long press and the knob become player commands")
{
    StateStore store = StateStore::inMemory();
    FakeSpotify fake;
    auto spotify = linked(store, fake,
                          [](const HttpCall &request) -> std::optional<HttpResult>
                          {
                              if (request.method == "GET")
                              {
                                  return ok(kPlaying);
                              }
                              return HttpResult{204, ""};
                          });
    SpotifyApp app(*spotify);
    app.onEnter();
    spotify->step();
    app.update(Duration{1.0F});

    app.onInput({InputType::Press, 0});
    app.onInput({InputType::DoublePress, 0});
    app.onInput({InputType::LongPress, 0});
    spotify->step();
    CHECK(fake.called("PUT", "/me/player/pause"));
    CHECK(fake.called("POST", "/me/player/next"));
    CHECK(fake.called("POST", "/me/player/previous"));

    app.onInput({InputType::Rotate, 2});
    CHECK(app.pendingVolume() == 50);
    app.update(Duration{0.1F});
    spotify->step();
    CHECK_FALSE(fake.called("PUT", "volume_percent"));
    app.update(Duration{0.5F});
    spotify->step();
    CHECK(fake.called("PUT", "/me/player/volume?volume_percent=50"));
    app.onExit();
}

TEST_CASE("a factory reset forgets the link and the Client ID")
{
    TempDir dir;
    StateStore store(dir.path());
    FakeSpotify fake;
    auto spotify = linked(store, fake, [](const HttpCall &) { return ok(kPlaying); });
    spotify->reset();
    CHECK(spotify->snapshot().link == SpotifyLink::NotConnected);
    CHECK(spotify->clientId().empty());

    StateStore again(dir.path());
    SpotifyService reloaded(again, fake.call(), FakeSpotify::noCover());
    CHECK(reloaded.snapshot().link == SpotifyLink::NotConnected);
}

TEST_CASE("the configuration page's routes start and finish linking")
{
    StateStore store = StateStore::inMemory();
    FakeSpotify fake;
    fake.respond = [](const HttpCall &) { return ok(kTokens); };
    SpotifyService spotify(store, fake.call(), FakeSpotify::noCover());

    HttpRequest bad;
    bad.params = {{"clientId", "x"}, {"origin", kOrigin}};
    CHECK(spotifyLink(spotify, bad).status == 400);

    HttpRequest good;
    good.params = {{"clientId", kClientId}, {"origin", kOrigin}};
    const HttpResponse started = spotifyLink(spotify, good);
    REQUIRE(started.status == 200);
    CHECK_THAT(started.body, ContainsSubstring("accounts.spotify.com/authorize"));

    HttpRequest denied;
    denied.params = {{"error", "access_denied"}};
    CHECK_THAT(spotifyCallback(spotify, denied).headers[0].second, ContainsSubstring("denied"));

    CHECK_THAT(spotifyJson(spotify), ContainsSubstring("\"link\":\"not-connected\""));
    CHECK_THAT(spotifyJson(spotify), ContainsSubstring(SpotifyService::kRedirectUri));
}

TEST_CASE("the app says what it cannot show")
{
    StateStore store = StateStore::inMemory();
    FakeSpotify fake;
    fake.respond = [](const HttpCall &) { return std::nullopt; };
    SpotifyService spotify(store, fake.call(), FakeSpotify::noCover());
    SpotifyApp app(spotify);
    app.onEnter();

    Surface frame(64, 32);
    app.render(frame);
    bool lit = false;
    for (int y = 0; y < 32 && !lit; ++y)
    {
        for (int x = 0; x < 64 && !lit; ++x)
        {
            lit = !(frame.pixel(x, y) == Color::black());
        }
    }
    CHECK(lit);
    app.onExit();
}

TEST_CASE("album art decodes and shrinks to 32x32, and junk does not")
{
    // A 64x64 solid red JPEG, made with ffmpeg.
    const std::optional<std::string> jpeg = base64UrlDecode(
        "_9j_4AAQSkZJRgABAgAAAQABAAD__gAPTGF2YzYxLjMuMTAwAP_bAEMACAoKCwoLDQ0NDQ0NEA8QEBAQ"
        "EBAQEBAQEBISEhUVFRISEhAQEhIUFBUVFxcXFRUVFRcXGRkZHh4cHCMjJCsrM__EAE0AAQEAAAAAAAAA"
        "AAAAAAAAAAAGAQEBAQAAAAAAAAAAAAAAAAAABgcQAQAAAAAAAAAAAAAAAAAAAAARAQAAAAAAAAAAAAAA"
        "AAAAAAD_wAARCABAAEADASIAAhEAAxEA_9oADAMBAAIRAxEAPwCLASjfwAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAH_2Q");
    REQUIRE(jpeg);

    const auto cover = decodeCover(*jpeg, 32);
    REQUIRE(cover);
    REQUIRE(cover->size() == 32 * 32 * 3);
    CHECK((*cover)[0] > 200);
    CHECK((*cover)[1] < 60);
    CHECK((*cover)[2] < 60);

    CHECK_FALSE(decodeCover("not a jpeg", 32));
    CHECK_FALSE(decodeCover(*jpeg, 128));
}
