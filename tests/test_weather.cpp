// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/weather/weather_app.h"
#include "gfx/surface.h"
#include "net/weather_routes.h"
#include "os/state.h"
#include "os/weather.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

using namespace matrixos;
using Catch::Matchers::ContainsSubstring;

namespace
{

// Trimmed from a real Open-Meteo answer for Vienna, 2026-10-31.
constexpr const char *kResponse = R"({
 "current":{"time":"2026-10-31T13:15","temperature_2m":11.2,"weather_code":3,"is_day":1},
 "hourly":{"time":["2026-10-31T13:00","2026-10-31T14:00","2026-10-31T15:00","2026-10-31T16:00",
   "2026-10-31T17:00","2026-10-31T18:00","2026-10-31T19:00","2026-10-31T20:00",
   "2026-10-31T21:00","2026-10-31T22:00","2026-10-31T23:00","2026-11-01T00:00"],
  "temperature_2m":[11.1,11.3,11.4,11.0,10.7,10.2,9.8,9.4,9.0,8.6,8.2,7.9],
  "weather_code":[3,3,3,61,3,3,3,3,80,3,3,61],"is_day":[1,1,1,1,0,0,0,0,0,0,0,0]},
 "daily":{"time":["2026-10-31","2026-11-01","2026-11-02"],
  "weather_code":[80,61,61],"temperature_2m_max":[11.4,11.1,8.6],
  "temperature_2m_min":[7.9,6.5,6.1]}})";

constexpr Duration kFrame{1.0F / 60.0F};

WeatherService::Fetch answering(std::optional<std::string> body)
{
    return [body](const std::string &) { return body; };
}

/// Records the worker's fetches so a test can wait for it to reach one.
class FetchLog
{
public:
    WeatherService::Fetch answering(std::optional<std::string> body)
    {
        return [this, body](const std::string &url)
        {
            std::lock_guard lock(mutex_);
            urls_.push_back(url);
            fetched_.notify_all();
            return body;
        };
    }

    bool waitFor(std::size_t count)
    {
        std::unique_lock lock(mutex_);
        return fetched_.wait_for(lock, std::chrono::seconds(2),
                                 [&] { return urls_.size() >= count; });
    }

    std::vector<std::string> urls() const
    {
        std::lock_guard lock(mutex_);
        return urls_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable fetched_;
    std::vector<std::string> urls_;
};

WeatherService::Timing timingOf(std::chrono::milliseconds refresh, std::chrono::milliseconds retry)
{
    WeatherService::Timing timing;
    timing.refresh = refresh;
    timing.retry = retry;
    return timing;
}

int litPixels(const Surface &frame)
{
    int lit = 0;
    for (int y = 0; y < frame.height(); ++y)
    {
        for (int x = 0; x < frame.width(); ++x)
        {
            lit += frame.pixel(x, y) == Color::black() ? 0 : 1;
        }
    }
    return lit;
}

Surface draw(WeatherApp &app)
{
    app.onEnter();
    Surface frame(64, 32);
    app.render(frame);
    return frame;
}

} // namespace

TEST_CASE("an Open-Meteo forecast parses into now, hours and days")
{
    const std::optional<Forecast> forecast = parseForecast(kResponse);
    REQUIRE(forecast);

    CHECK(forecast->temperature == 11.2F);
    CHECK(forecast->sky == Sky::Cloudy);
    CHECK(forecast->daytime);

    REQUIRE(forecast->hours.size() == 12);
    CHECK(forecast->hours[0].hour == 13);
    CHECK(forecast->hours[3].sky == Sky::Rain);
    CHECK(forecast->hours[0].daytime);
    CHECK_FALSE(forecast->hours[6].daytime);
    CHECK(forecast->hours[11].hour == 0);

    REQUIRE(forecast->days.size() == 3);
    CHECK(forecast->days[0].weekday == 6); // Saturday
    CHECK(forecast->days[1].weekday == 0);
    CHECK(forecast->days[2].low == 6.1F);
    CHECK(forecast->days[2].high == 8.6F);
}

TEST_CASE("a malformed or incomplete forecast is rejected, not half-read")
{
    CHECK_FALSE(parseForecast(""));
    CHECK_FALSE(parseForecast("not json"));
    CHECK_FALSE(parseForecast(R"({"current":{"temperature_2m":3}})"));
}

TEST_CASE("WMO codes fold into what an icon can show")
{
    CHECK(skyFromCode(0) == Sky::Clear);
    CHECK(skyFromCode(2) == Sky::PartlyCloudy);
    CHECK(skyFromCode(45) == Sky::Fog);
    CHECK(skyFromCode(55) == Sky::Rain);
    CHECK(skyFromCode(73) == Sky::Snow);
    CHECK(skyFromCode(96) == Sky::Thunder);
}

TEST_CASE("the forecast URL asks for the configured place")
{
    const std::string url = forecastUrl({"Graz", 47.0673, 15.4420});
    CHECK_THAT(url, ContainsSubstring("https://api.open-meteo.com/"));
    CHECK_THAT(url, ContainsSubstring("latitude=47.0673"));
    CHECK_THAT(url, ContainsSubstring("longitude=15.4420"));
}

TEST_CASE("a fresh unit forecasts for Vienna")
{
    StateStore store = StateStore::inMemory();
    WeatherService weather(store, answering(std::nullopt));
    CHECK(weather.snapshot().place.name == "Vienna");
    CHECK_FALSE(weather.snapshot().forecast);
}

TEST_CASE("a failed refresh keeps the last forecast (FR-28)")
{
    StateStore store = StateStore::inMemory();
    std::optional<std::string> body = kResponse;
    WeatherService weather(store, [&body](const std::string &) { return body; });

    REQUIRE(weather.refresh());
    body.reset();
    CHECK_FALSE(weather.refresh());

    const WeatherSnapshot snapshot = weather.snapshot();
    REQUIRE(snapshot.forecast);
    CHECK(snapshot.forecast->temperature == 11.2F);
    CHECK_FALSE(snapshot.stale);
}

TEST_CASE("a forecast older than the limit is marked stale (FR-28)")
{
    StateStore store = StateStore::inMemory();
    WeatherService::Timing timing;
    timing.stale_after = std::chrono::milliseconds(0);
    WeatherService weather(store, answering(kResponse), timing);

    REQUIRE(weather.refresh());
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(weather.snapshot().stale);
}

TEST_CASE("the place survives a restart and drops the old forecast")
{
    test::TempDir dir;
    {
        StateStore store(dir.path());
        WeatherService weather(store, answering(kResponse));
        REQUIRE(weather.refresh());
        weather.setPlace({"Graz", 47.0673, 15.4420});
        CHECK_FALSE(weather.snapshot().forecast);
    }

    StateStore store(dir.path());
    WeatherService weather(store, answering(std::nullopt));
    const Place place = weather.snapshot().place;
    CHECK(place.name == "Graz");
    CHECK(place.latitude == 47.0673);
    CHECK(place.longitude == 15.4420);
}

TEST_CASE("a hanging request never blocks a reader (FR-27)")
{
    StateStore store = StateStore::inMemory();
    std::mutex mutex;
    std::condition_variable released;
    bool fetching = false;
    bool release = false;

    WeatherService weather(store,
                           [&](const std::string &) -> std::optional<std::string>
                           {
                               std::unique_lock lock(mutex);
                               fetching = true;
                               released.notify_all();
                               released.wait(lock, [&] { return release; });
                               return std::nullopt;
                           });
    weather.begin();

    {
        std::unique_lock lock(mutex);
        released.wait(lock, [&] { return fetching; });
    }

    const auto start = std::chrono::steady_clock::now();
    const WeatherSnapshot snapshot = weather.snapshot();
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(50));
    CHECK_FALSE(snapshot.forecast);

    {
        std::lock_guard lock(mutex);
        release = true;
    }
    released.notify_all();
}

TEST_CASE("after a failure the worker retries on the short interval")
{
    FetchLog log;
    StateStore store = StateStore::inMemory();
    WeatherService weather(store, log.answering(std::nullopt),
                           timingOf(std::chrono::hours(1), std::chrono::milliseconds(5)));
    weather.begin();

    CHECK(log.waitFor(3));
}

TEST_CASE("after a success the worker waits for the refresh interval")
{
    FetchLog log;
    StateStore store = StateStore::inMemory();
    WeatherService weather(store, log.answering(kResponse),
                           timingOf(std::chrono::hours(1), std::chrono::milliseconds(5)));
    weather.begin();

    REQUIRE(log.waitFor(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(log.urls().size() == 1);
}

TEST_CASE("a new place is fetched at once, not at the next refresh")
{
    FetchLog log;
    StateStore store = StateStore::inMemory();
    WeatherService weather(store, log.answering(kResponse),
                           timingOf(std::chrono::hours(1), std::chrono::hours(1)));
    weather.begin();

    REQUIRE(log.waitFor(1));
    weather.setPlace({"Graz", 47.0673, 15.4420});
    REQUIRE(log.waitFor(2));
    CHECK_THAT(log.urls()[1], ContainsSubstring("latitude=47.0673"));
}

TEST_CASE("a forecast for a place changed mid-fetch is dropped")
{
    StateStore store = StateStore::inMemory();
    WeatherService *service = nullptr;
    WeatherService weather(store,
                           [&service](const std::string &) -> std::optional<std::string>
                           {
                               service->setPlace({"Graz", 47.0673, 15.4420});
                               return kResponse;
                           });
    service = &weather;

    CHECK_FALSE(weather.refresh());
    CHECK(weather.snapshot().place.name == "Graz");
    CHECK_FALSE(weather.snapshot().forecast);
}

TEST_CASE("the configuration page sets the place, and bad input is refused")
{
    StateStore store = StateStore::inMemory();
    WeatherService weather(store, answering(std::nullopt));

    HttpRequest good;
    good.params = {{"name", "Graz"}, {"latitude", "47.0673"}, {"longitude", "15.442"}};
    CHECK(setWeatherPlace(weather, good).status == 204);
    CHECK(weather.snapshot().place.name == "Graz");

    HttpRequest bad;
    bad.params = {{"name", "Nowhere"}, {"latitude", "123"}, {"longitude", "15"}};
    CHECK(setWeatherPlace(weather, bad).status == 400);
    CHECK(weather.snapshot().place.name == "Graz");

    HttpRequest control;
    control.params = {{"name", "Graz\nlatitude=0"}, {"latitude", "47"}, {"longitude", "15"}};
    CHECK(setWeatherPlace(weather, control).status == 400);
    CHECK(weather.snapshot().place.name == "Graz");

    CHECK_THAT(weatherJson(weather.snapshot()), ContainsSubstring("\"name\":\"Graz\""));
    CHECK_THAT(weatherJson(weather.snapshot()), ContainsSubstring("\"temperature\":null"));
}

TEST_CASE("rotating steps through now, hours and days and wraps")
{
    StateStore store = StateStore::inMemory();
    WeatherService weather(store, answering(kResponse));
    WeatherApp app(weather);

    CHECK(app.window() == WeatherApp::Window::Now);
    app.onInput({InputType::Rotate, 1});
    CHECK(app.window() == WeatherApp::Window::Hours);
    app.onInput({InputType::Rotate, 1});
    CHECK(app.window() == WeatherApp::Window::Days);
    app.onInput({InputType::Rotate, 1});
    CHECK(app.window() == WeatherApp::Window::Now);
    app.onInput({InputType::Rotate, -1});
    CHECK(app.window() == WeatherApp::Window::Days);
}

TEST_CASE("every window draws something, with and without a forecast")
{
    StateStore store = StateStore::inMemory();
    WeatherService weather(store, answering(kResponse));
    WeatherApp app(weather);

    CHECK(litPixels(draw(app)) > 0); // never fetched: the placeholder

    REQUIRE(weather.refresh());
    for (int window = 0; window < 3; ++window)
    {
        app.update(kFrame);
        CHECK(litPixels(draw(app)) > 0);
        app.onInput({InputType::Rotate, 1});
    }
}

TEST_CASE("a stale forecast carries a marker in the corner (FR-28)")
{
    StateStore store = StateStore::inMemory();
    WeatherService::Timing timing;
    timing.stale_after = std::chrono::milliseconds(0);
    WeatherService weather(store, answering(kResponse), timing);
    WeatherApp app(weather);

    REQUIRE(weather.refresh());
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const Surface frame = draw(app);
    CHECK_FALSE(frame.pixel(63, 0) == Color::black());
}

TEST_CASE("the windows take turns every 30 seconds, and rotating restarts the turn")
{
    StateStore store = StateStore::inMemory();
    WeatherService weather(store, answering(kResponse));
    WeatherApp app(weather);
    app.onEnter();

    app.update(Duration{29.0F});
    CHECK(app.window() == WeatherApp::Window::Now);
    app.update(Duration{1.0F});
    CHECK(app.window() == WeatherApp::Window::Hours);

    app.update(Duration{20.0F});
    app.onInput({InputType::Rotate, 1});
    CHECK(app.window() == WeatherApp::Window::Days);
    app.update(Duration{20.0F});
    CHECK(app.window() == WeatherApp::Window::Days);
    app.update(Duration{10.0F});
    CHECK(app.window() == WeatherApp::Window::Now);
}
