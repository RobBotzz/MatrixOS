// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/sun.h"

#include <chrono>
#include <cmath>

namespace matrixos
{
namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegree = kPi / 180.0;
constexpr std::int64_t kDay = 86400;

} // namespace

SunPosition subsolarPoint(std::int64_t unix_seconds)
{
    const std::int64_t days =
        unix_seconds >= 0 ? unix_seconds / kDay : (unix_seconds - kDay + 1) / kDay;
    const double hours = static_cast<double>(unix_seconds - days * kDay) / 3600.0;

    const std::chrono::sys_days day{std::chrono::days{days}};
    const std::chrono::year_month_day date{day};
    const auto new_year = std::chrono::sys_days{date.year() / std::chrono::January / 1};
    const int day_of_year = static_cast<int>((day - new_year).count()) + 1;
    const double year_length = date.year().is_leap() ? 366.0 : 365.0;

    const double g = 2.0 * kPi / year_length * (day_of_year - 1 + (hours - 12.0) / 24.0);

    const double equation_of_time =
        229.18 * (0.000075 + 0.001868 * std::cos(g) - 0.032077 * std::sin(g) -
                  0.014615 * std::cos(2 * g) - 0.040849 * std::sin(2 * g));
    const double declination = 0.006918 - 0.399912 * std::cos(g) + 0.070257 * std::sin(g) -
                               0.006758 * std::cos(2 * g) + 0.000907 * std::sin(2 * g) -
                               0.002697 * std::cos(3 * g) + 0.00148 * std::sin(3 * g);

    double longitude = -15.0 * (hours - 12.0 + equation_of_time / 60.0);
    longitude = std::fmod(longitude + 540.0, 360.0) - 180.0;
    return {declination / kDegree, longitude};
}

double sunElevation(const SunPosition &sun, double latitude, double longitude)
{
    const double cosine = std::sin(latitude * kDegree) * std::sin(sun.latitude * kDegree) +
                          std::cos(latitude * kDegree) * std::cos(sun.latitude * kDegree) *
                              std::cos((longitude - sun.longitude) * kDegree);
    return std::asin(std::fmax(-1.0, std::fmin(1.0, cosine))) / kDegree;
}

} // namespace matrixos
