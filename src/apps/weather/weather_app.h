// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/app.h"
#include "os/weather.h"

namespace matrixos
{

/// The forecast for the configured place (v0.5). Rotating changes the window.
class WeatherApp : public App
{
public:
    enum class Window
    {
        Now,
        Hours,
        Days,
    };

    explicit WeatherApp(const WeatherService &weather);

    std::string_view name() const override { return "Weather"; }

    void onEnter() override;
    void onInput(const InputEvent &event) override;
    void update(Duration dt) override;
    void render(Surface &surface) override;

    Window window() const { return window_; }

private:
    void renderNow(Surface &surface, const Forecast &forecast) const;
    void renderHours(Surface &surface, const Forecast &forecast) const;
    void renderDays(Surface &surface, const Forecast &forecast) const;
    void renderUnknown(Surface &surface) const;

    const WeatherService &weather_;
    WeatherSnapshot snapshot_;
    Window window_ = Window::Now;
    float since_read_ = 0.0F;
};

} // namespace matrixos
