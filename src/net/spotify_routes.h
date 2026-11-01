// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "net/http_server.h"
#include "os/spotify.h"

#include <string>

namespace matrixos
{

/// The configuration page's side of linking Spotify (ADR-0019):
/// `GET /api/spotify`, `POST /api/spotify/link`, `POST /api/spotify/code`,
/// `POST /api/spotify/unlink`, and `GET /spotify/callback`, where the static
/// callback page sends the browser back.
void installSpotifyRoutes(HttpServer &server, SpotifyService &spotify);

std::string spotifyJson(const SpotifyService &spotify);

/// The handlers without a socket, for the tests.
HttpResponse spotifyLink(SpotifyService &spotify, const HttpRequest &request);
HttpResponse spotifyCallback(SpotifyService &spotify, const HttpRequest &request);

} // namespace matrixos
