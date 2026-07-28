// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/state.h"

#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

using namespace matrixos;
using matrixos::test::TempDir;

namespace
{

void writeFile(const std::filesystem::path &path, std::string_view content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::set<std::string> entryNames(const std::filesystem::path &directory)
{
    std::set<std::string> names;
    for (const auto &entry : std::filesystem::directory_iterator(directory))
    {
        names.insert(entry.path().filename().string());
    }
    return names;
}

} // namespace

TEST_CASE("an empty root reads as factory defaults")
{
    TempDir dir;
    StateStore store(dir.path());

    REQUIRE(store.persistent());
    CHECK(store.section("snake").getInt("highscore", 0) == 0);
    CHECK(store.section("settings").getString("startup", "Last app") == "Last app");
    CHECK_FALSE(store.section("snake").dirty());
}

TEST_CASE("values survive a restart")
{
    TempDir dir;

    {
        StateStore store(dir.path());
        store.section("snake").setInt("highscore", 42);
        store.section("shell").setString("last_app", "Snake");
        REQUIRE(store.saveAll());
    }

    StateStore reopened(dir.path());
    CHECK(reopened.section("snake").getInt("highscore", 0) == 42);
    CHECK(reopened.section("shell").getString("last_app", "") == "Snake");
}

TEST_CASE("each namespace is its own file")
{
    TempDir dir;
    StateStore store(dir.path());

    store.section("snake").setInt("highscore", 7);
    REQUIRE(store.saveAll());

    CHECK(entryNames(dir.path()) == std::set<std::string>{"snake.conf"});
    CHECK(readFile(dir.file("snake.conf")) == "highscore=7\n");
}

TEST_CASE("a completed write leaves no temporary file behind")
{
    TempDir dir;
    StateStore store(dir.path());

    store.section("settings").setInt("brightness", 60);
    REQUIRE(store.saveAll());

    CHECK(entryNames(dir.path()) == std::set<std::string>{"settings.conf"});
}

TEST_CASE("a temporary file left by an interrupted write is ignored")
{
    TempDir dir;
    writeFile(dir.file("snake.conf"), "highscore=10\n");
    writeFile(dir.file("snake.conf.tmp"), "highscore=999\n");

    StateStore store(dir.path());

    CHECK(store.section("snake").getInt("highscore", 0) == 10); // FR-40

    store.section("snake").setInt("highscore", 11);
    REQUIRE(store.saveAll());
    CHECK(entryNames(dir.path()) == std::set<std::string>{"snake.conf"});
}

TEST_CASE("unreadable lines are dropped and the rest of the file survives")
{
    TempDir dir;
    writeFile(dir.file("snake.conf"), "highscore=42\n"
                                      "\n"
                                      "this line has no separator\n"
                                      "bad key=1\n"
                                      "  games  =  3  \n");

    StateStore store(dir.path());
    StateSection &snake = store.section("snake");

    CHECK(snake.getInt("highscore", 0) == 42);
    CHECK(snake.getInt("games", 0) == 3);
    CHECK(snake.getInt("bad key", -1) == -1);
    CHECK_FALSE(snake.dirty());
}

TEST_CASE("a value that does not parse reads as the fallback")
{
    TempDir dir;
    writeFile(dir.file("settings.conf"), "brightness=high\n"
                                         "contrast=12x\n"
                                         "empty=\n");

    StateStore store(dir.path());
    StateSection &settings = store.section("settings");

    CHECK(settings.getInt("brightness", 50) == 50);
    CHECK(settings.getInt("contrast", 50) == 50);
    CHECK(settings.getInt("empty", 50) == 50);
    CHECK(settings.getString("brightness", "") == "high");
}

TEST_CASE("negative numbers round-trip")
{
    TempDir dir;
    StateStore store(dir.path());

    store.section("snake").setInt("offset", -5);
    REQUIRE(store.saveAll());

    StateStore reopened(dir.path());
    CHECK(reopened.section("snake").getInt("offset", 0) == -5);
}

TEST_CASE("writing the value that is already there is not a change")
{
    TempDir dir;
    StateStore store(dir.path());
    StateSection &snake = store.section("snake");

    snake.setInt("highscore", 42);
    CHECK(snake.dirty());
    REQUIRE(snake.save());
    CHECK_FALSE(snake.dirty());

    snake.setInt("highscore", 42);
    CHECK_FALSE(snake.dirty());

    snake.setInt("highscore", 43);
    CHECK(snake.dirty());
}

TEST_CASE("values are trimmed on the way in, so a reload reads the same string")
{
    TempDir dir;
    StateStore store(dir.path());

    store.section("shell").setString("last_app", "  Snake  ");
    CHECK(store.section("shell").getString("last_app", "") == "Snake");
    REQUIRE(store.saveAll());

    StateStore reopened(dir.path());
    CHECK(reopened.section("shell").getString("last_app", "") == "Snake");
}

TEST_CASE("a value containing '=' survives")
{
    TempDir dir;
    StateStore store(dir.path());

    store.section("settings").setString("note", "a=b=c");
    REQUIRE(store.saveAll());

    StateStore reopened(dir.path());
    CHECK(reopened.section("settings").getString("note", "") == "a=b=c");
}

TEST_CASE("keys and values that cannot be represented are refused")
{
    TempDir dir;
    StateStore store(dir.path());
    StateSection &settings = store.section("settings");

    settings.setString("has space", "x");
    settings.setString("has/slash", "x");
    settings.setString("", "x");
    settings.setString("note", "two\nlines");

    CHECK_FALSE(settings.dirty());
    CHECK(settings.getString("note", "none") == "none");
}

TEST_CASE("a namespace that is not a safe file name stays in memory")
{
    TempDir dir;
    StateStore store(dir.path());

    StateSection &escaped = store.section("../etc");
    escaped.setInt("value", 1);
    REQUIRE(store.saveAll());

    CHECK(entryNames(dir.path()).empty());
    CHECK(store.section("../etc").getInt("value", 0) == 1);
}

TEST_CASE("a section is loaded once and kept")
{
    TempDir dir;
    writeFile(dir.file("snake.conf"), "highscore=1\n");

    StateStore store(dir.path());
    store.section("snake").setInt("highscore", 2);

    CHECK(store.section("snake").getInt("highscore", 0) == 2);
    CHECK(store.section("snake").dirty());
}

TEST_CASE("an unusable root degrades to memory instead of failing")
{
    TempDir dir;
    // A file, not an unwritable directory: works regardless of the test's uid.
    writeFile(dir.file("blocked"), "not a directory\n");

    StateStore store(dir.file("blocked"));
    REQUIRE_FALSE(store.persistent());

    StateSection &snake = store.section("snake");
    snake.setInt("highscore", 42);
    CHECK(snake.dirty());
    CHECK(store.saveAll()); // acceptance criterion 6
    CHECK_FALSE(snake.dirty());
    CHECK(snake.getInt("highscore", 0) == 42);
}

TEST_CASE("the root is created when it does not exist yet")
{
    TempDir dir;
    const std::filesystem::path nested = dir.file("a") / "b";

    StateStore store(nested);
    REQUIRE(store.persistent());
    CHECK(std::filesystem::is_directory(nested));
}

TEST_CASE("saving without changes writes nothing")
{
    TempDir dir;
    writeFile(dir.file("snake.conf"), "highscore=42\n");

    StateStore store(dir.path());
    REQUIRE(store.section("snake").getInt("highscore", 0) == 42);

    const auto before = std::filesystem::last_write_time(dir.file("snake.conf"));
    REQUIRE(store.saveAll());
    CHECK(std::filesystem::last_write_time(dir.file("snake.conf")) == before);
}

TEST_CASE("keys are written sorted, one per line")
{
    TempDir dir;
    StateStore store(dir.path());
    StateSection &settings = store.section("settings");

    settings.setString("startup", "Snake");
    settings.setInt("brightness", 60);
    REQUIRE(settings.save());

    CHECK(readFile(dir.file("settings.conf")) == "brightness=60\nstartup=Snake\n");
}
