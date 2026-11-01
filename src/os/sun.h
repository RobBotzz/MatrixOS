// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <cstdint>

namespace matrixos
{

/// Where the sun stands overhead, in degrees.
struct SunPosition
{
    double latitude = 0.0;
    double longitude = 0.0;
};

/// NOAA's low-precision solar position: declination and equation of time from a
/// Fourier series in the day of the year. Good to a fraction of a degree, which
/// is far below one 5.6-degree pixel of the world map.
SunPosition subsolarPoint(std::int64_t unix_seconds);

/// The sun's height above the horizon, in degrees, seen from a place.
double sunElevation(const SunPosition &sun, double latitude, double longitude);

} // namespace matrixos
