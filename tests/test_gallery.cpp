// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/gallery/gallery_app.h"
#include "gfx/surface.h"
#include "net/gallery_routes.h"
#include "os/gallery.h"
#include "os/picture.h"
#include "os/state.h"
#include "temp_dir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <map>

using namespace matrixos;
using Catch::Matchers::ContainsSubstring;
using matrixos::test::TempDir;

namespace
{

Picture solid(std::uint8_t red, std::size_t frames = 1, int delay = 100)
{
    Picture picture;
    for (std::size_t i = 0; i < frames; ++i)
    {
        Picture::Frame frame;
        frame.delay_ms = frames == 1 ? 0 : delay;
        frame.rgb.assign(Picture::kFrameBytes, 0);
        for (std::size_t at = 0; at < Picture::kFrameBytes; at += 3)
        {
            frame.rgb[at] = red;
        }
        picture.frames.push_back(std::move(frame));
    }
    return picture;
}

std::string message(const std::string &id, long long time, const std::string &url, long long size)
{
    return R"({"id":")" + id + R"(","time":)" + std::to_string(time) +
           R"(,"event":"message","topic":"t","message":"x","attachment":{"name":"picture.mxp","url":")" +
           url + R"(","size":)" + std::to_string(size) + "}}\n";
}

/// Answers the poll URL with `inbox` and attachment URLs from `files`.
struct FakeNtfy
{
    std::string inbox;
    std::map<std::string, std::string> files;
    std::vector<std::string> requested;

    GalleryService::Fetch fetch()
    {
        return [this](const std::string &url) -> std::optional<std::string>
        {
            requested.push_back(url);
            if (url.find("/json?poll=1") != std::string::npos)
            {
                return inbox;
            }
            const auto found = files.find(url);
            return found != files.end() ? std::optional(found->second) : std::nullopt;
        };
    }

    void send(const std::string &id, long long time, const Picture &picture)
    {
        const std::string url = "https://ntfy.sh/file/" + id + ".mxp";
        const std::string bytes = serializePicture(picture);
        files[url] = bytes;
        inbox += message(id, time, url, static_cast<long long>(bytes.size()));
    }
};

} // namespace

TEST_CASE("a picture survives the wire unchanged")
{
    const std::optional<Picture> back = parsePicture(serializePicture(solid(200, 3, 80)));
    REQUIRE(back);
    REQUIRE(back->frames.size() == 3);
    CHECK(back->frames[1].delay_ms == 80);
    CHECK(back->frames[2].rgb[0] == 200);
}

TEST_CASE("anything but an exact picture is refused")
{
    const std::string good = serializePicture(solid(1));

    CHECK_FALSE(parsePicture(""));
    CHECK_FALSE(parsePicture("MXP1"));
    CHECK_FALSE(parsePicture("XXXX" + good.substr(4)));
    CHECK_FALSE(parsePicture(good.substr(0, good.size() - 1)));
    CHECK_FALSE(parsePicture(good + "x"));

    std::string wide = good;
    wide[4] = 65;
    CHECK_FALSE(parsePicture(wide));

    std::string none = good;
    none[6] = 0;
    CHECK_FALSE(parsePicture(none));

    CHECK_FALSE(parsePicture(serializePicture(solid(1, Picture::kMaxFrames + 1))));
}

TEST_CASE("a frame cannot ask for less than the minimum delay")
{
    const std::optional<Picture> fast = parsePicture(serializePicture(solid(1, 2, 0)));
    REQUIRE(fast);
    CHECK(fast->frames[0].delay_ms == Picture::kMinDelayMs);
}

TEST_CASE("only messages with an attachment come out of a poll")
{
    const std::string lines = R"({"id":"a1","time":1,"event":"open","topic":"t"})"
                              "\n" +
                              message("b2", 2, "https://ntfy.sh/file/b2.mxp", 10) +
                              R"({"id":"c3","time":3,"event":"message","topic":"t","message":"hi"})"
                              "\n"
                              "not json\n";

    const std::vector<InboxMessage> messages = parseInbox(lines);
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].id == "b2");
    CHECK(messages[0].url == "https://ntfy.sh/file/b2.mxp");
    CHECK(messages[0].size == 10);
}

TEST_CASE("every unit draws its own address and keeps it")
{
    TempDir dir;
    std::string first;
    {
        StateStore store(dir.path());
        GalleryService gallery(store, dir.path() / "gallery", nullptr);
        first = gallery.topic();
        CHECK(first.rfind("matrixos-", 0) == 0);
        CHECK(first.size() == 9 + GalleryService::kTopicRandomChars);
    }

    StateStore again(dir.path());
    CHECK(GalleryService(again, dir.path() / "gallery", nullptr).topic() == first);

    StateStore other = StateStore::inMemory();
    CHECK(GalleryService(other, dir.path() / "other", nullptr).topic() != first);
}

TEST_CASE("a sent picture is stored, counted and not fetched twice")
{
    TempDir dir;
    StateStore store(dir.path());
    FakeNtfy ntfy;
    GalleryService gallery(store, dir.path() / "gallery", ntfy.fetch());
    gallery.load();

    ntfy.send("abc123", 1000, solid(9));
    REQUIRE(gallery.poll());
    CHECK(gallery.snapshot().pictures.size() == 1);
    CHECK(gallery.arrivals() == 1);
    CHECK_THAT(ntfy.requested[0], ContainsSubstring("since=all"));

    REQUIRE(gallery.poll());
    CHECK(gallery.snapshot().pictures.size() == 1);
    CHECK(gallery.arrivals() == 1);
    CHECK_THAT(ntfy.requested.back(), ContainsSubstring("since=abc123"));
}

TEST_CASE("pictures are refused unless they are valid and come from ntfy")
{
    TempDir dir;
    StateStore store(dir.path());
    FakeNtfy ntfy;
    GalleryService gallery(store, dir.path() / "gallery", ntfy.fetch());
    gallery.load();

    ntfy.inbox += message("far", 1, "https://example.com/file/x.mxp", 100);
    ntfy.inbox += message("big", 2, "https://ntfy.sh/file/big.mxp",
                          static_cast<long long>(Picture::kMaxBytes) + 1);
    ntfy.files["https://ntfy.sh/file/junk.mxp"] = "junk";
    ntfy.inbox += message("junk", 3, "https://ntfy.sh/file/junk.mxp", 4);
    ntfy.inbox += message("../x", 4, "https://ntfy.sh/file/x.mxp", 100);

    REQUIRE(gallery.poll());
    CHECK(gallery.snapshot().pictures.empty());
    CHECK(std::filesystem::is_empty(dir.path() / "gallery"));
}

TEST_CASE("thirty pictures are kept, newest first, and they survive a restart")
{
    TempDir dir;
    FakeNtfy ntfy;
    for (int i = 0; i < 32; ++i)
    {
        ntfy.send("p" + std::to_string(i), 1000 + i, solid(static_cast<std::uint8_t>(i)));
    }

    {
        StateStore store(dir.path());
        GalleryService gallery(store, dir.path() / "gallery", ntfy.fetch());
        gallery.load();
        REQUIRE(gallery.poll());
        CHECK(gallery.snapshot().pictures.size() == GalleryService::kKeep);
        CHECK(gallery.snapshot().pictures.front()->frames[0].rgb[0] == 31);
    }

    StateStore store(dir.path());
    GalleryService gallery(store, dir.path() / "gallery", nullptr);
    gallery.load();
    const GallerySnapshot snapshot = gallery.snapshot();
    REQUIRE(snapshot.pictures.size() == GalleryService::kKeep);
    CHECK(snapshot.pictures.front()->frames[0].rgb[0] == 31);
    CHECK(snapshot.pictures.back()->frames[0].rgb[0] == 2);
}

TEST_CASE("a new address ends the old one; a reset also deletes the pictures")
{
    TempDir dir;
    StateStore store(dir.path());
    FakeNtfy ntfy;
    GalleryService gallery(store, dir.path() / "gallery", ntfy.fetch());
    gallery.load();
    ntfy.send("one", 1, solid(1));
    REQUIRE(gallery.poll());

    const std::string old = gallery.topic();
    gallery.newAddress();
    CHECK(gallery.topic() != old);
    CHECK(gallery.snapshot().pictures.size() == 1);

    REQUIRE(gallery.poll());
    CHECK_THAT(ntfy.requested.back(),
               ContainsSubstring(gallery.topic() + "/json?poll=1&since=all"));

    gallery.reset();
    CHECK(gallery.snapshot().pictures.empty());
    CHECK(std::filesystem::is_empty(dir.path() / "gallery"));
}

TEST_CASE("nothing is written to the card while nothing arrives")
{
    TempDir dir;
    StateStore store(dir.path());
    FakeNtfy ntfy;
    GalleryService gallery(store, dir.path() / "gallery", ntfy.fetch());
    gallery.load();

    const auto state = dir.path() / "gallery.conf";
    const auto before = std::filesystem::last_write_time(state);
    REQUIRE(gallery.poll());
    REQUIRE(gallery.poll());
    CHECK(std::filesystem::last_write_time(state) == before);
}

TEST_CASE("the configuration page gets the send link")
{
    StateStore store = StateStore::inMemory();
    GalleryService gallery(store, "/nonexistent", nullptr);
    const std::string json = galleryJson(gallery);
    CHECK_THAT(json, ContainsSubstring(GalleryService::kSendPage));
    CHECK_THAT(json, ContainsSubstring("#" + gallery.topic()));
    CHECK_THAT(json, ContainsSubstring("\"pictures\":0"));
}

TEST_CASE("the gallery shows the newest arrival at once and browses by rotating")
{
    TempDir dir;
    StateStore store(dir.path());
    FakeNtfy ntfy;
    GalleryService gallery(store, dir.path() / "gallery", ntfy.fetch());
    gallery.load();
    GalleryApp app(gallery);
    app.onEnter();

    Surface empty(64, 32);
    app.render(empty);
    bool lit = false;
    for (int y = 0; y < 32 && !lit; ++y)
    {
        for (int x = 0; x < 64 && !lit; ++x)
        {
            lit = !(empty.pixel(x, y) == Color::black());
        }
    }
    CHECK(lit);

    ntfy.send("a", 1, solid(10));
    ntfy.send("b", 2, solid(20));
    REQUIRE(gallery.poll());
    app.update(Duration{1.0F});
    CHECK(app.index() == 0);

    app.onInput({InputType::Rotate, 1});
    CHECK(app.index() == 1);
    Surface frame(64, 32);
    app.render(frame);
    CHECK(frame.pixel(5, 5).r == 10);

    ntfy.send("c", 3, solid(30));
    REQUIRE(gallery.poll());
    app.update(Duration{1.0F});
    CHECK(app.index() == 0);
    app.render(frame);
    CHECK(frame.pixel(5, 5).r == 30);
}

TEST_CASE("pictures take turns every 30 seconds")
{
    TempDir dir;
    StateStore store(dir.path());
    FakeNtfy ntfy;
    GalleryService gallery(store, dir.path() / "gallery", ntfy.fetch());
    gallery.load();
    ntfy.send("a", 1, solid(10));
    ntfy.send("b", 2, solid(20));
    REQUIRE(gallery.poll());

    GalleryApp app(gallery);
    app.onEnter();
    app.update(Duration{29.0F});
    CHECK(app.index() == 0);
    app.update(Duration{1.0F});
    CHECK(app.index() == 1);
}
