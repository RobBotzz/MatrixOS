// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/life/life.h"

#include "gfx/surface.h"

#include <catch2/catch_test_macros.hpp>

using namespace matrixos;

TEST_CASE("a block stays as it is")
{
    LifeApp life(1);
    life.setCells({{10, 10}, {11, 10}, {10, 11}, {11, 11}});
    life.step();
    CHECK(life.population() == 4);
    CHECK(life.alive(10, 10));
    CHECK(life.alive(11, 11));
}

TEST_CASE("a blinker turns from a row into a column and back")
{
    LifeApp life(1);
    life.setCells({{9, 10}, {10, 10}, {11, 10}});
    life.step();
    CHECK(life.alive(10, 9));
    CHECK(life.alive(10, 11));
    CHECK_FALSE(life.alive(9, 10));
    life.step();
    CHECK(life.alive(9, 10));
    CHECK(life.alive(11, 10));
}

TEST_CASE("a glider travels and wraps around the edges")
{
    LifeApp life(1);
    // Moves one cell right and one down every four generations.
    life.setCells({{62, 29}, {63, 30}, {61, 31}, {62, 31}, {63, 31}});
    for (int i = 0; i < 8; ++i)
    {
        life.step();
    }
    // Two cells right and two down, wrapped: across both edges at once.
    CHECK(life.population() == 5);
    CHECK(life.alive(0, 31));
    CHECK(life.alive(1, 0));
    CHECK(life.alive(63, 1));
    CHECK(life.alive(0, 1));
    CHECK(life.alive(1, 1));
}

TEST_CASE("rotate sets the speed, press reseeds, a long press changes the palette")
{
    LifeApp life(7);
    const int speed = life.speed();
    life.onInput({InputType::Rotate, 1});
    CHECK(life.speed() == speed + 1);
    life.onInput({InputType::Rotate, 10});
    CHECK(life.speed() == LifeApp::kSpeeds - 1);
    life.onInput({InputType::Rotate, -10});
    CHECK(life.speed() == 0);

    life.setCells({{1, 1}});
    life.onInput({InputType::Press, 0});
    CHECK(life.population() > 100);

    life.onInput({InputType::LongPress, 0});
    CHECK(life.palette() == 1);
}

TEST_CASE("a settled field is replaced after a few seconds")
{
    LifeApp life(3);
    life.setCells({{10, 10}, {11, 10}, {10, 11}, {11, 11}});
    life.update(Duration{LifeApp::kSettledSeconds + 1.0F});
    CHECK(life.population() > 100);
}

TEST_CASE("a lively field is drawn")
{
    LifeApp life(5);
    Surface frame(64, 32);
    life.render(frame);
    int lit = 0;
    for (int y = 0; y < 32; ++y)
    {
        for (int x = 0; x < 64; ++x)
        {
            lit += frame.pixel(x, y) == Color::black() ? 0 : 1;
        }
    }
    CHECK(lit == life.population());
}
