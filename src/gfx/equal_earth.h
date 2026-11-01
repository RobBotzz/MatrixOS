// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <optional>

namespace matrixos
{

/// The Equal Earth projection (Šavrič, Patterson, Jenny, 2018), fitted to the
/// 64x32 panel: areas keep their true proportions, so Antarctica is a flat band
/// and not the whole bottom row. tools/landmask_to_header.py uses the same
/// formulas; the two must stay in step.
struct PanelPoint
{
    double x = 0.0; ///< pixels, 0 at the left edge
    double y = 0.0; ///< pixels, 0 at the top edge
};

struct GeoPoint
{
    double latitude = 0.0;  ///< degrees
    double longitude = 0.0; ///< degrees
};

PanelPoint equalEarthForward(const GeoPoint &point);

/// `nullopt` for a point outside the map's rounded outline.
std::optional<GeoPoint> equalEarthInverse(const PanelPoint &point);

} // namespace matrixos
