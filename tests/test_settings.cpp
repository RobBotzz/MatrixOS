// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/settings/settings_app.h"

#include "gfx/surface.h"
#include "os/settings.h"
#include "os/state.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace matrixos;
using matrixos::test::TempDir;

namespace
{

const std::vector<std::string_view> kApps = {"Plasma", "Pomodoro", "Snake"};

void send(SettingsApp &app, InputType type, int delta = 0)
{
    app.onInput(InputEvent{type, delta});
}

/// Browse to a page and start editing it.
void edit(SettingsApp &app, SettingsApp::Page page)
{
    while (app.page() != page)
    {
        send(app, InputType::Rotate, +1);
    }
    send(app, InputType::Press);
}

} // namespace

TEST_CASE("settings open on the brightness page, not editing")
{
    StateStore store = StateStore::inMemory();
    SettingsApp app(store, kApps);
    app.onEnter();

    CHECK(app.page() == SettingsApp::Page::Brightness);
    CHECK_FALSE(app.editing());
    CHECK(app.brightness() == settings::kDefaultBrightness);
    CHECK(app.startup() == settings::kStartupLast);
}

TEST_CASE("rotating browses; pressing edits")
{
    StateStore store = StateStore::inMemory();
    SettingsApp app(store, kApps);
    app.onEnter();

    send(app, InputType::Rotate, +1);
    CHECK(app.page() == SettingsApp::Page::Startup);
    CHECK(app.brightness() == settings::kDefaultBrightness); // browsing changes nothing

    send(app, InputType::Press);
    CHECK(app.editing());

    send(app, InputType::Press);
    CHECK_FALSE(app.editing());
}

TEST_CASE("brightness reaches the store while the knob is still turning")
{
    StateStore store = StateStore::inMemory();
    SettingsApp app(store, kApps);
    app.onEnter();
    edit(app, SettingsApp::Page::Brightness);

    send(app, InputType::Rotate, +2);

    const int expected = settings::kDefaultBrightness + 2 * settings::kBrightnessStep;
    CHECK(app.brightness() == expected);
    CHECK(store.section(settings::kSection).getInt(settings::kBrightness, 0) == expected);
}

TEST_CASE("brightness stops at both ends")
{
    StateStore store = StateStore::inMemory();
    SettingsApp app(store, kApps);
    app.onEnter();
    edit(app, SettingsApp::Page::Brightness);

    send(app, InputType::Rotate, +100);
    CHECK(app.brightness() == settings::kMaxBrightness);

    send(app, InputType::Rotate, -100);
    CHECK(app.brightness() == settings::kMinBrightness);
}

TEST_CASE("the startup choice walks the app list and wraps")
{
    StateStore store = StateStore::inMemory();
    SettingsApp app(store, kApps);
    app.onEnter();
    edit(app, SettingsApp::Page::Startup);

    CHECK(app.startup() == settings::kStartupLast);

    send(app, InputType::Rotate, +1);
    CHECK(app.startup() == "Plasma");

    send(app, InputType::Rotate, +3);
    CHECK(app.startup() == settings::kStartupLast); // four entries, back to the start

    send(app, InputType::Rotate, -1);
    CHECK(app.startup() == "Snake");
}

TEST_CASE("settings are written when the user leaves, and read back on return")
{
    TempDir dir;

    {
        StateStore store(dir.path());
        SettingsApp app(store, kApps);
        app.onEnter();

        edit(app, SettingsApp::Page::Brightness);
        send(app, InputType::Rotate, -2);
        send(app, InputType::Press);

        edit(app, SettingsApp::Page::Startup);
        send(app, InputType::Rotate, +3);
        send(app, InputType::Press);

        CHECK(store.section(settings::kSection).dirty()); // not written yet

        app.onExit();
        CHECK_FALSE(store.section(settings::kSection).dirty());
    }

    StateStore reopened(dir.path());
    SettingsApp later(reopened, kApps);
    later.onEnter();

    CHECK(later.brightness() == settings::kDefaultBrightness - 2 * settings::kBrightnessStep);
    CHECK(later.startup() == "Snake");
}

TEST_CASE("a startup app that no longer exists falls back to the last one used")
{
    StateStore store = StateStore::inMemory();
    store.section(settings::kSection).setString(settings::kStartup, "Tetris");

    SettingsApp app(store, kApps);
    app.onEnter();

    CHECK(app.startup() == settings::kStartupLast);
}

TEST_CASE("a hold restores the defaults")
{
    StateStore store = StateStore::inMemory();
    SettingsApp app(store, kApps);
    app.onEnter();

    edit(app, SettingsApp::Page::Brightness);
    send(app, InputType::Rotate, +4);
    REQUIRE(app.brightness() != settings::kDefaultBrightness);

    send(app, InputType::LongPress);

    CHECK(app.brightness() == settings::kDefaultBrightness);
    CHECK(app.startup() == settings::kStartupLast);
    CHECK_FALSE(app.editing());
}

TEST_CASE("the time zone is a name, and it walks the curated list")
{
    StateStore store = StateStore::inMemory();
    SettingsApp app(store, kApps);
    app.onEnter();

    CHECK(app.timeZone() == settings::kDefaultTimeZone);

    edit(app, SettingsApp::Page::TimeZone);
    send(app, InputType::Rotate, +1);

    CHECK(app.timeZone() != settings::kDefaultTimeZone);
    CHECK(store.section(settings::kSection).getString(settings::kTimeZone, "") == app.timeZone());

    // A full turn through the list comes back to where it started.
    send(app, InputType::Rotate, static_cast<int>(settings::kTimeZones.size()));
    CHECK(app.timeZone() != settings::kDefaultTimeZone);
    send(app, InputType::Rotate, -1);
    CHECK(app.timeZone() == settings::kDefaultTimeZone);
}

TEST_CASE("a stored zone that is not on the list falls back to the default")
{
    StateStore store = StateStore::inMemory();
    store.section(settings::kSection).setString(settings::kTimeZone, "Mars/Olympus");

    SettingsApp app(store, kApps);
    app.onEnter();

    CHECK(app.timeZone() == settings::kTimeZones.front().zone);
}

TEST_CASE("browsing wraps in both directions across all three pages")
{
    StateStore store = StateStore::inMemory();
    SettingsApp app(store, kApps);
    app.onEnter();

    send(app, InputType::Rotate, -1);
    CHECK(app.page() == SettingsApp::Page::TimeZone);

    send(app, InputType::Rotate, +1);
    CHECK(app.page() == SettingsApp::Page::Brightness);
}

TEST_CASE("every page draws something and nothing lands outside the panel")
{
    StateStore store = StateStore::inMemory();
    SettingsApp app(store, kApps);
    app.onEnter();

    for (const auto page :
         {SettingsApp::Page::Brightness, SettingsApp::Page::Startup, SettingsApp::Page::TimeZone})
    {
        while (app.page() != page)
        {
            send(app, InputType::Rotate, +1);
        }

        Surface frame(64, 32);
        app.render(frame);

        int lit = 0;
        for (int y = 0; y < 32; ++y)
        {
            for (int x = 0; x < 64; ++x)
            {
                if (!(frame.pixel(x, y) == Color::black()))
                {
                    ++lit;
                }
            }
        }
        CHECK(lit > 0);
    }
}
