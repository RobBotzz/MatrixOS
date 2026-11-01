// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "gfx/equal_earth.h"

#include <cmath>

namespace matrixos
{
namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kA1 = 1.340264;
constexpr double kA2 = -0.081106;
constexpr double kA3 = 0.000893;
constexpr double kA4 = 0.003796;
const double kM = std::sqrt(3.0) / 2.0;

constexpr double kPanelWidth = 64.0;
constexpr double kPanelHeight = 32.0;

/// The projection is 2 * 2.70663 wide and 2 * 1.31738 high in its own units; the
/// width fills the panel, the height then leaves a fraction of a pixel spare.
const double kHalfWidth = kPi / (std::sqrt(3.0) / 2.0 * kA1);
const double kScale = kPanelWidth / (2.0 * kHalfWidth);

double polynomial(double t)
{
    const double t2 = t * t;
    const double t6 = t2 * t2 * t2;
    return kA1 + kA2 * t2 + t6 * (kA3 + kA4 * t2);
}

double derivative(double t)
{
    const double t2 = t * t;
    const double t6 = t2 * t2 * t2;
    return kA1 + 3 * kA2 * t2 + t6 * (7 * kA3 + 9 * kA4 * t2);
}

} // namespace

PanelPoint equalEarthForward(const GeoPoint &point)
{
    const double phi = point.latitude * kPi / 180.0;
    const double lambda = point.longitude * kPi / 180.0;
    const double theta = std::asin(kM * std::sin(phi));
    const double x = lambda * std::cos(theta) / (kM * derivative(theta));
    const double y = theta * polynomial(theta);
    return {kPanelWidth / 2.0 + x * kScale, kPanelHeight / 2.0 - y * kScale};
}

std::optional<GeoPoint> equalEarthInverse(const PanelPoint &point)
{
    const double x = (point.x - kPanelWidth / 2.0) / kScale;
    const double y = (kPanelHeight / 2.0 - point.y) / kScale;

    double theta = y;
    for (int i = 0; i < 8; ++i)
    {
        theta -= (theta * polynomial(theta) - y) / derivative(theta);
    }
    if (std::fabs(theta) > kPi / 3.0)
    {
        return std::nullopt;
    }

    const double lambda = kM * x * derivative(theta) / std::cos(theta);
    if (std::fabs(lambda) > kPi)
    {
        return std::nullopt;
    }
    const double phi = std::asin(std::sin(theta) / kM);
    return GeoPoint{phi * 180.0 / kPi, lambda * 180.0 / kPi};
}

} // namespace matrixos
