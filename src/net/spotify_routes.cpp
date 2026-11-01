// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "net/spotify_routes.h"

namespace matrixos
{

std::string spotifyJson(const SpotifyService &spotify)
{
    const SpotifySnapshot snapshot = spotify.snapshot();
    std::string json = "{";
    json += "\"link\":\"" + std::string(toString(snapshot.link)) + "\",";
    json += "\"clientId\":\"" + jsonEscape(spotify.clientId()) + "\",";
    json += "\"redirectUri\":\"" + std::string(SpotifyService::kRedirectUri) + "\",";
    json += "\"track\":";
    if (snapshot.track)
    {
        json += "\"" + jsonEscape(snapshot.track->title) + " — " +
                jsonEscape(snapshot.track->artist) + "\"";
    }
    else
    {
        json += "null";
    }
    json += "}";
    return json;
}

HttpResponse spotifyLink(SpotifyService &spotify, const HttpRequest &request)
{
    const std::string url = spotify.startLink(request.param("clientId"), request.param("origin"));
    if (url.empty())
    {
        return HttpResponse::text(400, "a 32-character Client ID and a local origin are required");
    }
    return HttpResponse::json("{\"authorizeUrl\":\"" + jsonEscape(url) + "\"}");
}

HttpResponse spotifyCallback(SpotifyService &spotify, const HttpRequest &request)
{
    if (!request.param("error").empty())
    {
        return HttpResponse::redirect("/?spotify=denied");
    }
    const bool ok = spotify.finishLink(request.param("code"), request.param("state"));
    return HttpResponse::redirect(ok ? "/?spotify=linking" : "/?spotify=expired");
}

void installSpotifyRoutes(HttpServer &server, SpotifyService &spotify)
{
    server.route("GET", "/api/spotify", [&spotify](const HttpRequest &)
                 { return HttpResponse::json(spotifyJson(spotify)); });

    server.route("POST", "/api/spotify/link",
                 [&spotify](const HttpRequest &request) { return spotifyLink(spotify, request); });

    server.route("GET", "/spotify/callback", [&spotify](const HttpRequest &request)
                 { return spotifyCallback(spotify, request); });

    server.route("POST", "/api/spotify/code",
                 [&spotify](const HttpRequest &request)
                 {
                     return spotify.finishLink(request.param("code"), std::nullopt)
                                ? HttpResponse::noContent()
                                : HttpResponse::text(409, "start linking again");
                 });

    server.route("POST", "/api/spotify/unlink",
                 [&spotify](const HttpRequest &)
                 {
                     spotify.unlink();
                     return HttpResponse::noContent();
                 });
}

} // namespace matrixos
