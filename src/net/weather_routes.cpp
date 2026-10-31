// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "net/weather_routes.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace matrixos
{
namespace
{

constexpr std::size_t kMaxNameLength = 64;

std::optional<double> number(std::string_view text, double limit)
{
    const std::string copy(text);
    char *end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (copy.empty() || *end != '\0' || !std::isfinite(value) || std::fabs(value) > limit)
    {
        return std::nullopt;
    }
    return value;
}

std::string fixed(double value, int decimals)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    return text;
}

} // namespace

std::string weatherJson(const WeatherSnapshot &snapshot)
{
    std::string json = "{";
    json += "\"name\":\"" + jsonEscape(snapshot.place.name) + "\",";
    json += "\"latitude\":" + fixed(snapshot.place.latitude, 4) + ",";
    json += "\"longitude\":" + fixed(snapshot.place.longitude, 4) + ",";
    json += "\"temperature\":" +
            (snapshot.forecast ? fixed(snapshot.forecast->temperature, 1) : std::string("null")) +
            ",";
    json += std::string("\"stale\":") + (snapshot.stale ? "true" : "false");
    json += "}";
    return json;
}

HttpResponse setWeatherPlace(WeatherService &weather, const HttpRequest &request)
{
    const std::string_view name = request.param("name");
    const std::optional<double> latitude = number(request.param("latitude"), 90.0);
    const std::optional<double> longitude = number(request.param("longitude"), 180.0);

    if (name.empty() || name.size() > kMaxNameLength || !latitude || !longitude)
    {
        return HttpResponse::text(400, "name, latitude and longitude are required");
    }

    weather.setPlace({std::string(name), *latitude, *longitude});
    return HttpResponse::noContent();
}

void installWeatherRoutes(HttpServer &server, WeatherService &weather)
{
    server.route("GET", "/api/weather", [&weather](const HttpRequest &)
                 { return HttpResponse::json(weatherJson(weather.snapshot())); });

    server.route("POST", "/api/weather", [&weather](const HttpRequest &request)
                 { return setWeatherPlace(weather, request); });
}

} // namespace matrixos
