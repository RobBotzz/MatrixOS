// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/weather.h"

#include "os/log.h"
#include "os/state.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace matrixos
{
namespace
{

constexpr std::string_view kSection = "weather";
constexpr std::string_view kName = "name";
constexpr std::string_view kLatitude = "latitude";
constexpr std::string_view kLongitude = "longitude";

constexpr int kForecastHours = 12;
constexpr int kForecastDays = 4;

std::string coordinate(double value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.4f", value);
    return text;
}

double parseCoordinate(const std::string &text, double fallback)
{
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end != text.c_str() ? value : fallback;
}

/// "2026-10-31T14:00" -> 14
int hourOf(const std::string &iso)
{
    return iso.size() >= 13 ? std::atoi(iso.c_str() + 11) : 0;
}

/// "2026-10-31" -> 6 (Saturday)
int weekdayOf(const std::string &date)
{
    if (date.size() < 10)
    {
        return 0;
    }
    const std::chrono::year_month_day ymd{
        std::chrono::year{std::atoi(date.c_str())},
        std::chrono::month{static_cast<unsigned>(std::atoi(date.c_str() + 5))},
        std::chrono::day{static_cast<unsigned>(std::atoi(date.c_str() + 8))}};
    return static_cast<int>(std::chrono::weekday{std::chrono::sys_days{ymd}}.c_encoding());
}

} // namespace

Sky skyFromCode(int code)
{
    if (code <= 1)
    {
        return Sky::Clear;
    }
    if (code == 2)
    {
        return Sky::PartlyCloudy;
    }
    if (code == 3)
    {
        return Sky::Cloudy;
    }
    if (code == 45 || code == 48)
    {
        return Sky::Fog;
    }
    if ((code >= 71 && code <= 77) || code == 85 || code == 86)
    {
        return Sky::Snow;
    }
    if (code >= 95)
    {
        return Sky::Thunder;
    }
    return Sky::Rain;
}

std::optional<Forecast> parseForecast(std::string_view json)
{
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded())
    {
        return std::nullopt;
    }

    try
    {
        Forecast forecast;

        const auto &current = root.at("current");
        forecast.temperature = current.at("temperature_2m").get<float>();
        forecast.sky = skyFromCode(current.at("weather_code").get<int>());
        forecast.daytime = current.value("is_day", 1) != 0;

        const auto &hourly = root.at("hourly");
        const auto &hour_times = hourly.at("time");
        for (std::size_t i = 0; i < hour_times.size(); ++i)
        {
            forecast.hours.push_back({hourOf(hour_times.at(i).get<std::string>()),
                                      hourly.at("temperature_2m").at(i).get<float>(),
                                      skyFromCode(hourly.at("weather_code").at(i).get<int>())});
        }

        const auto &daily = root.at("daily");
        const auto &day_times = daily.at("time");
        for (std::size_t i = 0; i < day_times.size(); ++i)
        {
            forecast.days.push_back({weekdayOf(day_times.at(i).get<std::string>()),
                                     daily.at("temperature_2m_min").at(i).get<float>(),
                                     daily.at("temperature_2m_max").at(i).get<float>(),
                                     skyFromCode(daily.at("weather_code").at(i).get<int>())});
        }

        return forecast;
    }
    catch (const nlohmann::json::exception &)
    {
        return std::nullopt;
    }
}

std::string forecastUrl(const Place &place)
{
    return "https://api.open-meteo.com/v1/forecast?latitude=" + coordinate(place.latitude) +
           "&longitude=" + coordinate(place.longitude) +
           "&current=temperature_2m,weather_code,is_day"
           "&hourly=temperature_2m,weather_code"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min"
           "&timezone=auto&forecast_hours=" +
           std::to_string(kForecastHours) + "&forecast_days=" + std::to_string(kForecastDays);
}

WeatherService::WeatherService(StateStore &store, Fetch fetch)
    : WeatherService(store, std::move(fetch), Timing{})
{
}

WeatherService::WeatherService(StateStore &store, Fetch fetch, Timing timing)
    : saved_(store.section(kSection)), fetch_(std::move(fetch)), timing_(timing)
{
    const Place fallback = defaultPlace();
    place_.name = saved_.getString(kName, fallback.name);
    place_.latitude = parseCoordinate(saved_.getString(kLatitude, ""), fallback.latitude);
    place_.longitude = parseCoordinate(saved_.getString(kLongitude, ""), fallback.longitude);
}

WeatherService::~WeatherService()
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

void WeatherService::begin()
{
    worker_ = std::thread([this] { run(); });
}

WeatherSnapshot WeatherService::snapshot() const
{
    std::lock_guard lock(mutex_);
    const bool stale = forecast_.has_value() &&
                       std::chrono::steady_clock::now() - fetched_at_ > timing_.stale_after;
    return {place_, forecast_, stale};
}

void WeatherService::setPlace(Place place)
{
    logInfo("weather: place set to {}", place.name);
    {
        std::lock_guard lock(mutex_);
        place_ = std::move(place);
        forecast_.reset();
        place_changed_ = true;

        saved_.setString(kName, place_.name);
        saved_.setString(kLatitude, coordinate(place_.latitude));
        saved_.setString(kLongitude, coordinate(place_.longitude));
        saved_.save();
    }
    wake_.notify_all();
}

bool WeatherService::refresh()
{
    Place place;
    {
        std::lock_guard lock(mutex_);
        place = place_;
        place_changed_ = false;
    }

    const std::optional<std::string> body = fetch_(forecastUrl(place));
    std::optional<Forecast> forecast = body ? parseForecast(*body) : std::nullopt;
    if (!forecast)
    {
        logWarn("weather: no forecast for {}", place.name);
        return false;
    }

    std::lock_guard lock(mutex_);
    if (place_changed_)
    {
        return false; // fetched for a place that is no longer configured
    }
    forecast_ = std::move(forecast);
    fetched_at_ = std::chrono::steady_clock::now();
    return true;
}

void WeatherService::run()
{
    while (!stopping_)
    {
        const bool ok = refresh();

        std::unique_lock lock(mutex_);
        wake_.wait_for(lock, ok ? timing_.refresh : timing_.retry,
                       [this] { return stopping_.load() || place_changed_; });
    }
}

} // namespace matrixos
