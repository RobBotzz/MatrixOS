// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "net/portal.h"

#include "http_client.h"
#include "net/fake_wifi.h"
#include "os/provisioning.h"
#include "os/state.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace matrixos;
using matrixos::test::HttpClient;

namespace
{

constexpr Provisioning::Timing kInstant{std::chrono::milliseconds(0), std::chrono::milliseconds(0)};

Identity testIdentity()
{
    return Identity{"a3f1", "matrixos-a3f1", "MatrixOS-a3f1"};
}

/// A device in setup mode, with a portal in front of it.
struct Fixture
{
    FakeWifi wifi;
    StateStore store = StateStore::inMemory();
    Provisioning provisioning{wifi, store, kInstant};
    Portal portal{provisioning, testIdentity(), "0.4.0", "abc1234"};
    HttpServer server{0};

    void boot()
    {
        provisioning.begin();
        provisioning.waitForIdle();
    }

    void serve()
    {
        portal.install(server);
        REQUIRE(server.start());
    }
};

bool contains(const std::string &haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("the setup page offers the networks that were found")
{
    Fixture fixture;
    fixture.boot();

    const std::string html = fixture.portal.setupPage();

    CHECK(contains(html, "matrixos-a3f1"));
    CHECK(contains(html, "action=\"/connect\""));
    CHECK(contains(html, "<option value=\"Kitchen\">Kitchen</option>"));
    CHECK(contains(html, "Cafe Gast (open)"));

    // The page the captive-portal WebView has to render: no script tags at all
    // (ADR-0014).
    CHECK_FALSE(contains(html, "<script"));
}

TEST_CASE("an SSID cannot break out of the page it is printed in")
{
    Fixture fixture;
    fixture.wifi.networks = {{"</option><script>alert(1)</script>", 70, true}};
    fixture.boot();

    const std::string html = fixture.portal.setupPage();

    CHECK_FALSE(contains(html, "<script>alert"));
    CHECK(contains(html, "&lt;script&gt;"));
}

TEST_CASE("with no networks in range the page says so instead of showing an empty list")
{
    Fixture fixture;
    fixture.wifi.networks.clear();
    fixture.boot();

    CHECK(contains(fixture.portal.setupPage(), "No networks found"));
}

TEST_CASE("the connecting page reloads itself, because it has no JavaScript to do it")
{
    Fixture fixture;
    fixture.boot();
    fixture.wifi.holdConnect();
    REQUIRE(fixture.provisioning.requestConnect("Kitchen", "letmein"));

    const std::string html = fixture.portal.setupPage();
    CHECK(contains(html, "http-equiv=\"refresh\""));
    CHECK(contains(html, "Connecting to Kitchen"));

    // The page cannot survive its own success: one radio, so the network this
    // page arrived over closes the moment the device joins the other one. Saying
    // so beforehand is the only confirmation the browser can give.
    CHECK(contains(html, "this network closes"));
    CHECK(contains(html, "Watch the panel"));

    fixture.wifi.release();
    fixture.provisioning.waitForIdle();
}

TEST_CASE("the connected page names the address the device answers to next")
{
    Fixture fixture;
    fixture.boot();

    REQUIRE(fixture.provisioning.requestConnect("Kitchen", ""));
    fixture.provisioning.waitForIdle();

    const std::string html = fixture.portal.setupPage();
    CHECK(contains(html, "Connected to Kitchen"));
    CHECK(contains(html, "matrixos-a3f1.local"));
}

TEST_CASE("a failed attempt is stated on the page, not just in the log")
{
    Fixture fixture;
    fixture.wifi.password = "letmein";
    fixture.boot();

    REQUIRE(fixture.provisioning.requestConnect("Kitchen", "wrong"));
    fixture.provisioning.waitForIdle();

    const std::string html = fixture.portal.setupPage();
    CHECK(contains(html, "Could not connect"));
    CHECK(contains(html, "action=\"/connect\"")); // and the form is back
}

TEST_CASE("the status JSON carries the version, which is what FR-41 is about")
{
    Fixture fixture;
    fixture.boot();

    const std::string json = fixture.portal.statusJson();

    CHECK(contains(json, "\"version\":\"0.4.0\""));
    CHECK(contains(json, "\"commit\":\"abc1234\""));
    CHECK(contains(json, "\"hostname\":\"matrixos-a3f1\""));
    CHECK(contains(json, "\"state\":\"setup\""));
    CHECK(contains(json, "\"accessPoint\":\"MatrixOS-a3f1\""));
    CHECK(contains(json, "\"ssid\":\"Kitchen\",\"signal\":82,\"secured\":true"));
}

TEST_CASE("a quote in an SSID cannot break the status JSON")
{
    Fixture fixture;
    fixture.wifi.networks = {{"say \"hi\"", 40, true}};
    fixture.boot();

    CHECK(contains(fixture.portal.statusJson(), "\"ssid\":\"say \\\"hi\\\"\""));
}

TEST_CASE("GET / is the setup page while setup is needed, and the app page afterwards")
{
    Fixture fixture;
    fixture.boot();
    fixture.serve();

    {
        HttpClient client(fixture.server.port());
        client.get("/");
        const auto reply = client.read();
        CHECK(reply.status == 200);
        CHECK(contains(reply.body, "action=\"/connect\""));
    }

    REQUIRE(fixture.provisioning.requestConnect("Kitchen", ""));
    fixture.provisioning.waitForIdle();
    REQUIRE_FALSE(fixture.provisioning.needsSetup());

    HttpClient client(fixture.server.port());
    client.get("/");
    const auto reply = client.read();

    CHECK(reply.status == 200);
    CHECK(contains(reply.body, "<div id=\"root\">")); // the React page
    CHECK(reply.body.size() > 10000);
}

TEST_CASE("a captive-portal probe is redirected to the setup page by address")
{
    Fixture fixture;
    fixture.boot();
    fixture.serve();

    HttpClient client(fixture.server.port());
    client.get("/generate_204", "connectivitycheck.gstatic.com");
    const auto reply = client.read();

    CHECK(reply.status == 302);
    CHECK(reply.header("location") == std::string("http://") + Portal::kAccessPointAddress + "/");
}

TEST_CASE("an unknown path during setup lands on the portal as well")
{
    Fixture fixture;
    fixture.boot();
    fixture.serve();

    HttpClient client(fixture.server.port());
    client.get("/whatever/the/phone/asked/for");

    CHECK(client.read().status == 302);
}

TEST_CASE("once online, a probe is not hijacked any more")
{
    Fixture fixture;
    fixture.wifi.pretendJoined("Kitchen");
    fixture.boot();
    fixture.serve();
    REQUIRE_FALSE(fixture.provisioning.needsSetup());

    HttpClient client(fixture.server.port());
    client.get("/generate_204");

    CHECK(client.read().status == 404);
}

TEST_CASE("submitting the form joins the network and redirects, so a reload resends nothing")
{
    Fixture fixture;
    fixture.boot();
    fixture.serve();

    HttpClient client(fixture.server.port());
    client.postForm("/connect", "ssid=Kitchen&psk=letmein");
    const auto reply = client.read();

    CHECK(reply.status == 302);
    CHECK(reply.header("location") == "/");

    fixture.provisioning.waitForIdle();
    CHECK(fixture.provisioning.status().state == SetupState::Connected);
}

TEST_CASE("the configuration page gets a status code, not a redirect")
{
    Fixture fixture;
    fixture.boot();
    fixture.serve();

    HttpClient client(fixture.server.port());
    client.postForm("/connect", "ssid=Kitchen&psk=letmein&source=config");

    CHECK(client.read().status == 204);
    fixture.provisioning.waitForIdle();
}

TEST_CASE("submitting without a network says so rather than starting a blank attempt")
{
    Fixture fixture;
    fixture.boot();
    fixture.serve();

    HttpClient client(fixture.server.port());
    client.postForm("/connect", "ssid=&psk=");
    const auto reply = client.read();

    CHECK(reply.status == 200);
    CHECK(contains(reply.body, "pick a network"));
    CHECK(fixture.wifi.attempts == 0);
}

TEST_CASE("a factory reset is reachable from the page and needs no terminal")
{
    Fixture fixture;
    fixture.wifi.pretendJoined("Kitchen");
    fixture.boot();
    fixture.serve();
    REQUIRE_FALSE(fixture.provisioning.needsSetup());

    HttpClient client(fixture.server.port());
    client.writeRaw("POST /api/reset HTTP/1.1\r\nHost: matrixos-a3f1.local\r\n"
                    "Content-Length: 0\r\n\r\n");

    CHECK(client.read().status == 204);

    fixture.provisioning.waitForIdle();
    CHECK(fixture.wifi.forgets == 1);
    CHECK(fixture.provisioning.needsSetup());
}

TEST_CASE("a reset cannot be triggered by fetching a URL")
{
    Fixture fixture;
    fixture.wifi.pretendJoined("Kitchen");
    fixture.boot();
    fixture.serve();

    HttpClient client(fixture.server.port());
    client.get("/api/reset");

    CHECK(client.read().status == 404); // GET is not that route
    CHECK(fixture.wifi.forgets == 0);
}
