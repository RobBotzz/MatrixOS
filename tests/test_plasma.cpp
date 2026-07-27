// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/plasma/plasma.h"
#include "gfx/surface.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace matrixos;

namespace
{

using Frame = std::vector<std::uint8_t>;

Frame renderFrame(PlasmaApp &app)
{
    Surface surface(64, 32);
    app.render(surface);
    const auto bytes = surface.bytes();
    return Frame(bytes.begin(), bytes.end());
}

Frame advanceAndRender(PlasmaApp &app, float seconds)
{
    app.update(Duration{seconds});
    return renderFrame(app);
}

bool isBlack(const Frame &frame)
{
    for (const auto value : frame)
    {
        if (value != 0)
        {
            return false;
        }
    }
    return true;
}

void send(PlasmaApp &app, InputType type, int delta = 0)
{
    app.onInput(InputEvent{type, delta});
}

} // namespace

TEST_CASE("plasma draws something")
{
    PlasmaApp app;
    CHECK_FALSE(isBlack(advanceAndRender(app, 0.5F)));
}

TEST_CASE("plasma is deterministic, which is what makes snapshots possible")
{
    PlasmaApp first;
    PlasmaApp second;

    const Frame a = advanceAndRender(first, 0.25F);
    const Frame b = advanceAndRender(second, 0.25F);

    CHECK(a == b);
}

TEST_CASE("the picture changes over time")
{
    PlasmaApp app;

    const Frame early = advanceAndRender(app, 0.1F);
    const Frame later = advanceAndRender(app, 1.5F);

    CHECK(early != later);
}

TEST_CASE("rotating steps through the variants and wraps both ways")
{
    PlasmaApp app;
    REQUIRE(app.variant() == 0);

    send(app, InputType::Rotate, +1);
    CHECK(app.variant() == 1);

    // Backwards past zero must not produce a negative index.
    send(app, InputType::Rotate, -1);
    send(app, InputType::Rotate, -1);
    CHECK(app.variant() == PlasmaApp::kVariantCount - 1);

    send(app, InputType::Rotate, +1);
    CHECK(app.variant() == 0);
}

TEST_CASE("each variant looks different")
{
    std::vector<Frame> frames;
    for (int variant = 0; variant < PlasmaApp::kVariantCount; ++variant)
    {
        PlasmaApp app;
        for (int step = 0; step < variant; ++step)
        {
            send(app, InputType::Rotate, +1);
        }
        REQUIRE(app.variant() == variant);
        frames.push_back(advanceAndRender(app, 0.4F));
    }

    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        for (std::size_t j = i + 1; j < frames.size(); ++j)
        {
            CHECK(frames[i] != frames[j]);
        }
    }
}

TEST_CASE("pressing freezes the animation and pressing again resumes it")
{
    PlasmaApp app;
    const Frame running = advanceAndRender(app, 0.3F);

    send(app, InputType::Press);
    REQUIRE(app.paused());

    const Frame frozen = advanceAndRender(app, 2.0F);
    CHECK(frozen == running); // time did not move on

    send(app, InputType::Press);
    REQUIRE_FALSE(app.paused());

    CHECK(advanceAndRender(app, 2.0F) != frozen);
}

TEST_CASE("unused gestures are ignored rather than crashing")
{
    PlasmaApp app;

    send(app, InputType::DoublePress);
    send(app, InputType::LongPress);
    send(app, InputType::Home);

    CHECK(app.variant() == 0);
    CHECK_FALSE(app.paused());
}
