// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "net/http_server.h"

#include "http_client.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

using namespace matrixos;
using matrixos::test::HttpClient;

namespace
{

/// A started server on a port the kernel picked, stopped again on scope exit.
struct Fixture
{
    HttpServer server{0};

    void start() { REQUIRE(server.start()); }
    int port() const { return server.port(); }
};

} // namespace

TEST_CASE("a registered route answers with its body")
{
    Fixture fixture;
    fixture.server.route("GET", "/hello",
                         [](const HttpRequest &) { return HttpResponse::html("<b>hi</b>"); });
    fixture.start();

    HttpClient client(fixture.port());
    client.get("/hello");
    const auto reply = client.read();

    CHECK(reply.status == 200);
    CHECK(reply.body == "<b>hi</b>");
    CHECK(reply.header("content-type") == "text/html; charset=utf-8");
}

TEST_CASE("an unmatched path is 404, or the fallback when one is set")
{
    Fixture fixture;
    fixture.server.route("GET", "/known",
                         [](const HttpRequest &) { return HttpResponse::text(200, "yes"); });
    fixture.start();

    {
        HttpClient client(fixture.port());
        client.get("/elsewhere");
        CHECK(client.read().status == 404);
    }

    // What the captive portal needs: every unknown address lands on the page.
    fixture.server.fallback([](const HttpRequest &)
                            { return HttpResponse::redirect("http://10.42.0.1/"); });

    HttpClient client(fixture.port());
    client.get("/generate_204");
    const auto reply = client.read();

    CHECK(reply.status == 302);
    CHECK(reply.header("location") == "http://10.42.0.1/");
}

TEST_CASE("the method is part of the route, not decoration")
{
    Fixture fixture;
    fixture.server.route("POST", "/connect",
                         [](const HttpRequest &) { return HttpResponse::text(200, "posted"); });
    fixture.start();

    HttpClient client(fixture.port());
    client.get("/connect");
    CHECK(client.read().status == 404);
}

TEST_CASE("query parameters arrive decoded")
{
    Fixture fixture;
    fixture.server.route("GET", "/echo", [](const HttpRequest &request)
                         { return HttpResponse::text(200, std::string(request.param("ssid"))); });
    fixture.start();

    HttpClient client(fixture.port());
    client.get("/echo?ssid=Caf%C3%A9%20Zwei&other=1");

    CHECK(client.read().body == "Caf\xC3\xA9 Zwei");
}

TEST_CASE("a form body arrives decoded, and '+' means space")
{
    Fixture fixture;
    fixture.server.route("POST", "/connect",
                         [](const HttpRequest &request)
                         {
                             return HttpResponse::text(200, std::string(request.param("ssid")) +
                                                                "|" +
                                                                std::string(request.param("psk")));
                         });
    fixture.start();

    HttpClient client(fixture.port());
    client.postForm("/connect", "ssid=My+Net&psk=p%40ss%26word");

    CHECK(client.read().body == "My Net|p@ss&word");
}

TEST_CASE("a missing parameter yields the fallback, not an exception")
{
    Fixture fixture;
    fixture.server.route(
        "GET", "/x", [](const HttpRequest &request)
        { return HttpResponse::text(200, std::string(request.param("absent", "default"))); });
    fixture.start();

    HttpClient client(fixture.port());
    client.get("/x");
    CHECK(client.read().body == "default");
}

TEST_CASE("the Host header is available, without its port")
{
    Fixture fixture;
    fixture.server.route("GET", "/host", [](const HttpRequest &request)
                         { return HttpResponse::text(200, request.host); });
    fixture.start();

    HttpClient client(fixture.port());
    client.writeRaw("GET /host HTTP/1.1\r\nHost: matrixos-a3f1.local:80\r\n\r\n");

    CHECK(client.read().body == "matrixos-a3f1.local");
}

TEST_CASE("an absolute-form target is reduced to its path")
{
    // Some captive-portal probes send the whole URL on the request line.
    Fixture fixture;
    fixture.server.route("GET", "/hotspot-detect.html",
                         [](const HttpRequest &) { return HttpResponse::text(200, "portal"); });
    fixture.start();

    HttpClient client(fixture.port());
    client.writeRaw("GET http://captive.apple.com/hotspot-detect.html HTTP/1.1\r\n"
                    "Host: captive.apple.com\r\n\r\n");

    CHECK(client.read().body == "portal");
}

TEST_CASE("one connection serves several requests")
{
    Fixture fixture;
    int calls = 0;
    fixture.server.route("GET", "/count", [&calls](const HttpRequest &)
                         { return HttpResponse::text(200, std::to_string(++calls)); });
    fixture.start();

    HttpClient client(fixture.port());

    client.get("/count");
    CHECK(client.read().body == "1");

    client.get("/count");
    CHECK(client.read().body == "2");

    client.get("/count");
    CHECK(client.read().body == "3");
}

TEST_CASE("a second connection is served while the first stays open")
{
    // The reason for poll() over a one-at-a-time accept loop: a browser opens
    // several sockets for one page, and a stalled first one must not hold up the
    // rest (ADR-0012).
    Fixture fixture;
    fixture.server.route("GET", "/a",
                         [](const HttpRequest &) { return HttpResponse::text(200, "first"); });
    fixture.start();

    HttpClient idle(fixture.port());
    idle.get("/a");
    CHECK(idle.read().body == "first");

    HttpClient second(fixture.port());
    second.get("/a");
    CHECK(second.read().body == "first");

    // And the first one is still usable afterwards.
    idle.get("/a");
    CHECK(idle.read().body == "first");
}

TEST_CASE("a request split across packets is assembled")
{
    Fixture fixture;
    fixture.server.route("POST", "/connect", [](const HttpRequest &request)
                         { return HttpResponse::text(200, std::string(request.param("ssid"))); });
    fixture.start();

    HttpClient client(fixture.port());
    client.writeRaw("POST /connect HTTP/1.1\r\nHost: x\r\n"
                    "Content-Type: application/x-www-form-urlencoded\r\n");
    client.writeRaw("Content-Length: 9\r\n\r\nssid=");
    client.writeRaw("Home");

    CHECK(client.read().body == "Home");
}

TEST_CASE("an oversized body is refused instead of buffered")
{
    Fixture fixture;
    fixture.server.route("POST", "/connect",
                         [](const HttpRequest &) { return HttpResponse::text(200, "ok"); });
    fixture.start();

    HttpClient client(fixture.port());
    client.writeRaw("POST /connect HTTP/1.1\r\nHost: x\r\nContent-Length: " +
                    std::to_string(HttpServer::kMaxBodyBytes + 1) + "\r\n\r\n");

    CHECK(client.read().status == 413);
}

TEST_CASE("oversized headers are refused instead of buffered")
{
    Fixture fixture;
    fixture.start();

    HttpClient client(fixture.port());
    client.writeRaw("GET / HTTP/1.1\r\nHost: x\r\n");
    client.writeRaw("X-Filler: " + std::string(HttpServer::kMaxHeaderBytes + 16, 'a') + "\r\n");

    CHECK(client.read().status == 431);
}

TEST_CASE("a malformed request line is answered, not crashed on")
{
    Fixture fixture;
    fixture.start();

    HttpClient client(fixture.port());
    client.writeRaw("NONSENSE\r\n\r\n");

    CHECK(client.read().status == 400);
}

TEST_CASE("a handler that throws costs its request, not the server")
{
    Fixture fixture;
    fixture.server.route("GET", "/boom", [](const HttpRequest &) -> HttpResponse
                         { throw std::runtime_error("x"); });
    fixture.server.route("GET", "/fine",
                         [](const HttpRequest &) { return HttpResponse::text(200, "still here"); });
    fixture.start();

    {
        HttpClient client(fixture.port());
        client.get("/boom");
        CHECK(client.read().status == 500);
    }

    HttpClient client(fixture.port());
    client.get("/fine");
    CHECK(client.read().body == "still here");
}

TEST_CASE("stop() releases the port, so a restart binds again")
{
    HttpServer first(0);
    REQUIRE(first.start());
    const int port = first.port();
    first.stop();
    CHECK_FALSE(first.running());

    HttpServer second(port);
    CHECK(second.start());
}

TEST_CASE("percent-decoding keeps a mangled escape rather than dropping it")
{
    CHECK(urlDecode("a%20b") == "a b");
    CHECK(urlDecode("a+b") == "a b");
    CHECK(urlDecode("100%%") == "100%%");
    CHECK(urlDecode("%zz") == "%zz");
    CHECK(urlDecode("%4") == "%4");
    CHECK(urlDecode("") == "");
}

TEST_CASE("an SSID cannot end an HTML element early")
{
    CHECK(htmlEscape("<script>") == "&lt;script&gt;");
    CHECK(htmlEscape("Bob & Alice") == "Bob &amp; Alice");
    CHECK(htmlEscape("say \"hi\"") == "say &quot;hi&quot;");
    CHECK(htmlEscape("it's") == "it&#39;s");
}

TEST_CASE("an SSID cannot end a JSON string early")
{
    CHECK(jsonEscape("plain") == "plain");
    CHECK(jsonEscape("a\"b") == "a\\\"b");
    CHECK(jsonEscape("a\\b") == "a\\\\b");
    CHECK(jsonEscape("line\nbreak") == "line\\nbreak");
    CHECK(jsonEscape(std::string(1, '\x01')) == "\\u0001");
}
