// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "net/http_server.h"
#include "os/weather.h"

#include <string>

namespace matrixos
{

/// `GET /api/weather` and `POST /api/weather` (name, latitude, longitude) for
/// the configuration page. The city search itself runs in the browser.
void installWeatherRoutes(HttpServer &server, WeatherService &weather);

std::string weatherJson(const WeatherSnapshot &snapshot);

/// The POST handler without a socket, for the tests.
HttpResponse setWeatherPlace(WeatherService &weather, const HttpRequest &request);

} // namespace matrixos
