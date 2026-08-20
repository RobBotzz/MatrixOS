// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace matrixos
{

/// One parsed request. Handlers see this and never a socket.
struct HttpRequest
{
    std::string method;
    std::string path; ///< without the query string
    std::string body;

    /// Query string and form body merged, both percent-decoded. A form field
    /// wins over a query parameter of the same name, because it was typed.
    std::map<std::string, std::string, std::less<>> params;

    std::string_view param(std::string_view name, std::string_view fallback = {}) const;

    /// The Host header, without the port. Used to tell "someone typed our name"
    /// from "a captive-portal probe asked for connectivitycheck.gstatic.com".
    std::string host;
};

struct HttpResponse
{
    int status = 200;
    std::string content_type = "text/html; charset=utf-8";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;

    static HttpResponse html(std::string body);
    static HttpResponse json(std::string body);
    static HttpResponse text(int status, std::string body);
    static HttpResponse redirect(std::string location);

    /// 204 with no body — what Android's captive-portal probe wants to hear when
    /// the network is fine, and must not hear while we are the portal.
    static HttpResponse noContent();
};

using HttpHandler = std::function<HttpResponse(const HttpRequest &)>;

/// A small HTTP/1.1 server on its own thread (ADR-0012).
///
/// It exists because the render loop may never wait on a socket (FR-27, NFR-1),
/// and it is deliberately minimal: LAN only, no TLS, tiny bodies, one user.
/// Handlers run on the server thread, so anything they touch must be safe to
/// touch from there — in practice that is the provisioning state and nothing
/// else. No handler may block: a handler that waits is a page that hangs.
///
/// It serves several connections at once through poll(). That is not premature:
/// a browser opens up to six sockets for one page, and a one-at-a-time loop
/// would stall until the first one timed out.
class HttpServer
{
public:
    /// Requests larger than these are answered with a status, not buffered. On a
    /// device without swap an unbounded reader is an OOM kill (NFR-4, NFR-20).
    static constexpr std::size_t kMaxHeaderBytes = 8 * 1024;
    static constexpr std::size_t kMaxBodyBytes = 64 * 1024;
    static constexpr std::size_t kMaxConnections = 8;
    static constexpr int kConnectionTimeoutMs = 5000;

    /// Port 0 asks the kernel for a free one, which is what tests use.
    explicit HttpServer(int port);
    ~HttpServer();

    HttpServer(const HttpServer &) = delete;
    HttpServer &operator=(const HttpServer &) = delete;

    /// Exact method and path. Registering the same pair twice replaces it.
    void route(std::string method, std::string path, HttpHandler handler);

    /// Answers anything no route matched. Without one, unmatched paths are 404 —
    /// with one, they are the captive portal (ADR-0013).
    void fallback(HttpHandler handler);

    /// Binds and listens, without starting the thread yet.
    ///
    /// Split from `start()` for the same reason the encoder claims its GPIO
    /// lines early: the matrix library drops privileges from root to `daemon`
    /// while creating the panel, and port 80 can only be bound as root. An
    /// already-open socket keeps working afterwards, so claiming it first is all
    /// that is needed.
    ///
    /// False means the port was taken or forbidden. The caller logs it and the
    /// device runs on without a web page rather than refusing to start.
    bool claimPort();

    /// Starts serving. Claims the port first if that has not happened yet.
    bool start();
    void stop();

    bool running() const { return running_; }

    /// The port actually bound, which is what a test needs after asking for 0.
    int port() const { return port_; }

private:
    struct Connection;

    void serve();
    void handle(Connection &connection);
    HttpResponse dispatch(const HttpRequest &request) const;

    int requested_port_;
    int port_ = 0;
    int listen_fd_ = -1;
    int wake_fds_[2] = {-1, -1};

    std::atomic<bool> running_{false};
    std::thread thread_;

    std::map<std::string, HttpHandler> routes_; // keyed "METHOD path"
    HttpHandler fallback_;
};

/// Percent-decoding, `+` included, as browsers send form bodies. Invalid escapes
/// are kept verbatim rather than dropped — a mangled SSID is easier to recognise
/// than a missing one.
std::string urlDecode(std::string_view text);

/// Escapes the five characters that can end an HTML element or attribute early.
/// Every SSID the portal prints goes through this: it is a string chosen by
/// someone else and shown in our page.
std::string htmlEscape(std::string_view text);

/// Escapes a string for inclusion in a JSON document, without the quotes.
std::string jsonEscape(std::string_view text);

} // namespace matrixos
