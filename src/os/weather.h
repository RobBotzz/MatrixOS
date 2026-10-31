// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace matrixos
{

class StateSection;
class StateStore;

struct Place
{
    std::string name;
    double latitude = 0.0;
    double longitude = 0.0;
};

/// WMO weather codes, folded into what a 16-pixel icon can tell apart.
enum class Sky
{
    Clear,
    PartlyCloudy,
    Cloudy,
    Fog,
    Rain,
    Snow,
    Thunder,
};

Sky skyFromCode(int wmo_code);

struct Forecast
{
    struct Hour
    {
        int hour = 0;
        float temperature = 0.0F;
        Sky sky = Sky::Clear;
        bool daytime = true;
    };

    struct Day
    {
        int weekday = 0; ///< 0 is Sunday
        float low = 0.0F;
        float high = 0.0F;
        Sky sky = Sky::Clear;
    };

    float temperature = 0.0F;
    Sky sky = Sky::Clear;
    bool daytime = true;
    std::vector<Hour> hours; ///< from the current hour on
    std::vector<Day> days;   ///< from today on
};

/// An Open-Meteo forecast response. Anything malformed is `nullopt`, never a
/// half-filled forecast.
std::optional<Forecast> parseForecast(std::string_view json);

std::string forecastUrl(const Place &place);

struct WeatherSnapshot
{
    Place place;
    std::optional<Forecast> forecast;
    bool stale = false; ///< FR-28
};

/// Keeps a forecast for the configured place fresh, off the render thread
/// (FR-27, ADR-0017). Apps read `snapshot()`, which never waits on the network.
class WeatherService
{
public:
    /// Returns the body, or `nullopt` on any failure. Blocks; only the worker
    /// calls it.
    using Fetch = std::function<std::optional<std::string>(const std::string &url)>;

    struct Timing
    {
        std::chrono::milliseconds refresh{std::chrono::minutes(15)};
        std::chrono::milliseconds retry{std::chrono::seconds(30)};
        std::chrono::milliseconds stale_after{std::chrono::hours(1)};
    };

    static Place defaultPlace() { return {"Vienna", 48.2082, 16.3738}; }

    WeatherService(StateStore &store, Fetch fetch);
    WeatherService(StateStore &store, Fetch fetch, Timing timing);
    ~WeatherService();

    WeatherService(const WeatherService &) = delete;
    WeatherService &operator=(const WeatherService &) = delete;

    /// Starts the worker. Without it nothing is fetched except by `refresh()`.
    void begin();

    WeatherSnapshot snapshot() const;

    /// Persists the place and fetches for it at once.
    void setPlace(Place place);

    /// One fetch, synchronously. The worker's loop body; public for the tests.
    bool refresh();

private:
    void run();

    StateSection &saved_;
    Fetch fetch_;
    Timing timing_;

    mutable std::mutex mutex_;
    Place place_;
    std::optional<Forecast> forecast_;
    std::chrono::steady_clock::time_point fetched_at_{};
    bool failing_ = false; ///< only `refresh()` touches it, so a failure logs once

    std::condition_variable wake_;
    bool place_changed_ = false;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};

} // namespace matrixos
