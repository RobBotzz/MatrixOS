// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/life/patterns.h"

#include <array>
#include <initializer_list>
#include <span>

namespace matrixos
{
namespace
{

using Shape = std::span<const std::string_view>;

constexpr std::array<std::string_view, 9> kGun = {
    "........................O...........", "......................O.O...........",
    "............OO......OO............OO", "...........O...O....OO............OO",
    "OO........O.....O...OO..............", "OO........O...O.OO....O.O...........",
    "..........O.....O.......O...........", "...........O...O....................",
    "............OO......................",
};
constexpr std::array<std::string_view, 4> kEater = {"OO..", "O.O.", "..O.", "..OO"};
constexpr std::array<std::string_view, 7> kQueenBee = {
    ".........O............", ".......O.O............", "......O.O.............",
    "OO...O..O...........OO", "OO....O.O...........OO", ".......O.O............",
    ".........O............",
};
constexpr std::array<std::string_view, 4> kLightShip = {".O..O", "O....", "O...O", "OOOO."};
constexpr std::array<std::string_view, 5> kMediumShip = {"...O..", ".O...O", "O.....", "O....O",
                                                         "OOOOO."};
constexpr std::array<std::string_view, 5> kHeavyShip = {"...OO..", ".O....O", "O......", "O.....O",
                                                        "OOOOOO."};
constexpr std::array<std::string_view, 13> kPulsar = {
    "..OOO...OOO..", ".............", "O....O.O....O", "O....O.O....O", "O....O.O....O",
    "..OOO...OOO..", ".............", "..OOO...OOO..", "O....O.O....O", "O....O.O....O",
    "O....O.O....O", ".............", "..OOO...OOO..",
};
constexpr std::array<std::string_view, 9> kGalaxy = {
    "OOOOOO.OO", "OOOOOO.OO", ".......OO", "OO.....OO", "OO.....OO",
    "OO.....OO", "OO.......", "OO.OOOOOO", "OO.OOOOOO",
};
constexpr std::array<std::string_view, 3> kPentadecathlon = {"..O....O..", "OO.OOOO.OO",
                                                             "..O....O.."};

/// One shape on the field; mirroring both ways turns it by half a circle.
struct Stamp
{
    Shape shape;
    int x = 0;
    int y = 0;
    bool mirror_x = false;
    bool mirror_y = false;
};

struct Layout
{
    std::string_view name;
    int period = 0;
    std::vector<Stamp> stamps;
};

const std::array<Layout, kLifePatterns> &layouts()
{
    // An eater swallows the gliders before they can wrap around.
    static const std::array<Layout, kLifePatterns> kLayouts = {{
        {"GLIDER GUN", 30, {{kGun, 9, 0}, {kEater, 51, 28}}},
        // These two offsets make the streams destroy each other head on.
        {"GUN DUEL", 30, {{kGun, 3, 0}, {kGun, 26, 22, true, true}}},
        {"FLEET",
         128,
         {{kLightShip, 10, 2},
          {kMediumShip, 40, 9},
          {kHeavyShip, 30, 17, true},
          {kLightShip, 5, 25, true},
          {kMediumShip, 40, 24, true}}},
        {"GARDEN",
         120,
         {{kPulsar, 4, 10}, {kPulsar, 47, 10}, {kGalaxy, 28, 3}, {kPentadecathlon, 27, 23}}},
        {"QUEEN BEES",
         30,
         {{kQueenBee, 4, 1},
          {kQueenBee, 36, 1, true},
          {kQueenBee, 20, 12},
          {kQueenBee, 4, 23},
          {kQueenBee, 36, 23, true}}},
    }};
    return kLayouts;
}

} // namespace

std::string_view lifePatternName(std::size_t pattern)
{
    return layouts()[pattern].name;
}

int lifePatternPeriod(std::size_t pattern)
{
    return layouts()[pattern].period;
}

std::vector<std::pair<int, int>> lifePatternCells(std::size_t pattern)
{
    std::vector<std::pair<int, int>> cells;
    for (const Stamp &stamp : layouts()[pattern].stamps)
    {
        const auto height = static_cast<int>(stamp.shape.size());
        for (int row = 0; row < height; ++row)
        {
            const std::string_view line = stamp.shape[static_cast<std::size_t>(row)];
            const auto width = static_cast<int>(line.size());
            for (int column = 0; column < width; ++column)
            {
                if (line[static_cast<std::size_t>(column)] == 'O')
                {
                    cells.emplace_back(stamp.x + (stamp.mirror_x ? width - 1 - column : column),
                                       stamp.y + (stamp.mirror_y ? height - 1 - row : row));
                }
            }
        }
    }
    return cells;
}

} // namespace matrixos
