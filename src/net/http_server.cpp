// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "net/http_server.h"

#include "os/log.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <list>

namespace matrixos
{
namespace
{

using Clock = std::chrono::steady_clock;

constexpr int kPollIntervalMs = 200;

bool equalsIgnoringCase(std::string_view lhs, std::string_view rhs)
{
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                      [](char a, char b)
                      {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}

std::string_view trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos)
    {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

const char *statusText(int status)
{
    switch (status)
    {
    case 200:
        return "OK";
    case 204:
        return "No Content";
    case 302:
        return "Found";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 413:
        return "Payload Too Large";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    case 503:
        return "Service Unavailable";
    default:
        return "OK";
    }
}

void setNonBlocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
    {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

/// Splits `a=1&b=2` into the parameter map, percent-decoding both halves.
void parsePairs(std::string_view text, std::map<std::string, std::string, std::less<>> &into)
{
    while (!text.empty())
    {
        const auto end = text.find('&');
        const std::string_view pair = text.substr(0, end);

        if (!pair.empty())
        {
            const auto equals = pair.find('=');
            if (equals == std::string_view::npos)
            {
                into[urlDecode(pair)] = std::string();
            }
            else
            {
                into[urlDecode(pair.substr(0, equals))] = urlDecode(pair.substr(equals + 1));
            }
        }

        if (end == std::string_view::npos)
        {
            return;
        }
        text.remove_prefix(end + 1);
    }
}

std::string serialize(const HttpResponse &response, bool keep_alive)
{
    std::string out = "HTTP/1.1 ";
    out += std::to_string(response.status);
    out += ' ';
    out += statusText(response.status);
    out += "\r\n";

    if (response.status != 204)
    {
        out += "Content-Type: " + response.content_type + "\r\n";
    }
    out += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";

    // Nothing this device serves is worth caching: the setup page changes with
    // every scan and the status is live. A stale portal page is a support call.
    out += "Cache-Control: no-store\r\n";
    out += keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";

    for (const auto &[name, value] : response.headers)
    {
        out += name + ": " + value + "\r\n";
    }

    out += "\r\n";
    out += response.body;
    return out;
}

} // namespace

std::string urlDecode(std::string_view text)
{
    std::string out;
    out.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '+')
        {
            out += ' ';
        }
        else if (text[i] == '%' && i + 2 < text.size() &&
                 std::isxdigit(static_cast<unsigned char>(text[i + 1])) != 0 &&
                 std::isxdigit(static_cast<unsigned char>(text[i + 2])) != 0)
        {
            out += static_cast<char>(std::stoi(std::string(text.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        }
        else
        {
            out += text[i];
        }
    }

    return out;
}

std::string htmlEscape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());

    for (const char c : text)
    {
        switch (c)
        {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&#39;";
            break;
        default:
            out += c;
            break;
        }
    }

    return out;
}

std::string jsonEscape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());

    for (const char c : text)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                static constexpr char kHex[] = "0123456789abcdef";
                out += "\\u00";
                out += kHex[(c >> 4) & 0xF];
                out += kHex[c & 0xF];
            }
            else
            {
                out += c;
            }
            break;
        }
    }

    return out;
}

std::string_view HttpRequest::param(std::string_view name, std::string_view fallback) const
{
    const auto found = params.find(name);
    return found == params.end() ? fallback : std::string_view(found->second);
}

HttpResponse HttpResponse::html(std::string body)
{
    return {200, "text/html; charset=utf-8", std::move(body), {}};
}

HttpResponse HttpResponse::json(std::string body)
{
    return {200, "application/json; charset=utf-8", std::move(body), {}};
}

HttpResponse HttpResponse::text(int status, std::string body)
{
    return {status, "text/plain; charset=utf-8", std::move(body), {}};
}

HttpResponse HttpResponse::redirect(std::string location)
{
    HttpResponse response{302, "text/plain; charset=utf-8", "", {}};
    response.headers.emplace_back("Location", std::move(location));
    return response;
}

HttpResponse HttpResponse::noContent()
{
    return {204, "", "", {}};
}

struct HttpServer::Connection
{
    int fd = -1;
    std::string in;
    std::string out;
    std::size_t sent = 0;
    bool closing = false; ///< close once `out` has been flushed
    Clock::time_point deadline;
};

HttpServer::HttpServer(int port) : requested_port_(port) {}

HttpServer::~HttpServer()
{
    stop();
}

void HttpServer::route(std::string method, std::string path, HttpHandler handler)
{
    routes_[method + " " + path] = std::move(handler);
}

void HttpServer::fallback(HttpHandler handler)
{
    fallback_ = std::move(handler);
}

bool HttpServer::claimPort()
{
    if (listen_fd_ >= 0)
    {
        return true;
    }

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
    {
        logError("http: socket failed: {}", std::strerror(errno));
        return false;
    }

    const int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<uint16_t>(requested_port_));

    if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
        ::listen(listen_fd_, static_cast<int>(kMaxConnections)) != 0)
    {
        logError("http: cannot listen on port {}: {}", requested_port_, std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    socklen_t length = sizeof(address);
    if (::getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&address), &length) == 0)
    {
        port_ = ntohs(address.sin_port);
    }

    setNonBlocking(listen_fd_);
    logInfo("http: listening on port {}", port_);
    return true;
}

bool HttpServer::start()
{
    if (running_)
    {
        return true;
    }

    if (!claimPort())
    {
        return false;
    }

    if (::pipe(wake_fds_) != 0)
    {
        logError("http: pipe failed: {}", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    setNonBlocking(wake_fds_[0]);

    running_ = true;
    thread_ = std::thread(&HttpServer::serve, this);
    return true;
}

void HttpServer::stop()
{
    if (running_)
    {
        running_ = false;

        // Wakes the poll immediately; without it the thread would linger for up
        // to one poll interval, which is noticeable when the panel is already
        // dark.
        const char byte = 'x';
        [[maybe_unused]] const ssize_t ignored = ::write(wake_fds_[1], &byte, 1);

        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    // Also runs when the port was claimed but serving never started, which is
    // what happens if the display fails between the two.
    for (int &fd : wake_fds_)
    {
        if (fd >= 0)
        {
            ::close(fd);
            fd = -1;
        }
    }

    if (listen_fd_ >= 0)
    {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

void HttpServer::serve()
{
    std::list<Connection> connections;

    while (running_)
    {
        std::vector<pollfd> fds;
        fds.push_back({wake_fds_[0], POLLIN, 0});

        // Stop accepting at the limit instead of dropping: the backlog holds the
        // request and the browser waits, which beats a connection reset.
        if (connections.size() < kMaxConnections)
        {
            fds.push_back({listen_fd_, POLLIN, 0});
        }

        const std::size_t connection_offset = fds.size();
        for (const Connection &connection : connections)
        {
            short events = POLLIN;
            if (connection.sent < connection.out.size())
            {
                events |= POLLOUT;
            }
            fds.push_back({connection.fd, events, 0});
        }

        if (::poll(fds.data(), fds.size(), kPollIntervalMs) < 0 && errno != EINTR)
        {
            logError("http: poll failed: {}", std::strerror(errno));
            break;
        }

        if ((fds[0].revents & POLLIN) != 0)
        {
            char drain[64];
            [[maybe_unused]] const ssize_t ignored = ::read(wake_fds_[0], drain, sizeof(drain));
        }

        if (connection_offset > 1 && (fds[1].revents & POLLIN) != 0)
        {
            const int fd = ::accept(listen_fd_, nullptr, nullptr);
            if (fd >= 0)
            {
                setNonBlocking(fd);
                Connection fresh;
                fresh.fd = fd;
                fresh.deadline = Clock::now() + std::chrono::milliseconds(kConnectionTimeoutMs);
                connections.push_back(std::move(fresh));
            }
        }

        std::size_t index = connection_offset;
        for (auto it = connections.begin(); it != connections.end(); ++index)
        {
            Connection &connection = *it;
            const short revents = index < fds.size() ? fds[index].revents : 0;
            bool drop = false;

            if ((revents & POLLIN) != 0)
            {
                char buffer[4096];
                const ssize_t count = ::recv(connection.fd, buffer, sizeof(buffer), 0);
                if (count > 0)
                {
                    connection.in.append(buffer, static_cast<std::size_t>(count));
                    connection.deadline =
                        Clock::now() + std::chrono::milliseconds(kConnectionTimeoutMs);
                    handle(connection);
                }
                else if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
                {
                    drop = true;
                }
            }

            if (!drop && connection.sent < connection.out.size())
            {
                const ssize_t count = ::send(connection.fd, connection.out.data() + connection.sent,
                                             connection.out.size() - connection.sent, MSG_NOSIGNAL);
                if (count > 0)
                {
                    connection.sent += static_cast<std::size_t>(count);
                }
                else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    drop = true;
                }
            }

            const bool flushed = connection.sent >= connection.out.size();
            if (flushed)
            {
                connection.out.clear();
                connection.sent = 0;
            }

            if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
                (connection.closing && flushed) || Clock::now() > connection.deadline)
            {
                drop = true;
            }

            if (drop)
            {
                ::close(connection.fd);
                it = connections.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    for (Connection &connection : connections)
    {
        ::close(connection.fd);
    }
}

void HttpServer::handle(Connection &connection)
{
    const auto header_end = connection.in.find("\r\n\r\n");

    if (header_end == std::string::npos)
    {
        if (connection.in.size() > kMaxHeaderBytes)
        {
            connection.out = serialize(HttpResponse::text(431, "headers too large"), false);
            connection.closing = true;
        }
        return;
    }

    const std::string_view head(connection.in.data(), header_end);
    const auto line_end = head.find("\r\n");
    const std::string_view request_line = head.substr(0, line_end);

    HttpRequest request;
    bool keep_alive = true;
    std::size_t content_length = 0;

    const auto method_end = request_line.find(' ');
    const auto target_end = request_line.find(' ', method_end + 1);
    if (method_end == std::string_view::npos || target_end == std::string_view::npos)
    {
        connection.out = serialize(HttpResponse::text(400, "bad request line"), false);
        connection.closing = true;
        return;
    }

    request.method = std::string(request_line.substr(0, method_end));
    std::string_view target = request_line.substr(method_end + 1, target_end - method_end - 1);
    const std::string_view version = request_line.substr(target_end + 1);
    keep_alive = version.find("1.0") == std::string_view::npos;

    // Absolute-form targets ("GET http://host/path") are legal and are what some
    // captive-portal probes send.
    if (target.starts_with("http://"))
    {
        const auto slash = target.find('/', 7);
        target = slash == std::string_view::npos ? std::string_view("/") : target.substr(slash);
    }

    const auto question = target.find('?');
    request.path = urlDecode(target.substr(0, question));
    if (question != std::string_view::npos)
    {
        parsePairs(target.substr(question + 1), request.params);
    }

    bool form_encoded = false;
    std::string_view rest =
        head.substr(line_end == std::string_view::npos ? head.size() : line_end + 2);
    while (!rest.empty())
    {
        const auto end = rest.find("\r\n");
        const std::string_view line = rest.substr(0, end);
        const auto colon = line.find(':');

        if (colon != std::string_view::npos)
        {
            const std::string_view name = trim(line.substr(0, colon));
            const std::string_view value = trim(line.substr(colon + 1));

            if (equalsIgnoringCase(name, "content-length"))
            {
                content_length =
                    static_cast<std::size_t>(std::strtoul(std::string(value).c_str(), nullptr, 10));
            }
            else if (equalsIgnoringCase(name, "connection"))
            {
                keep_alive = !equalsIgnoringCase(value, "close");
            }
            else if (equalsIgnoringCase(name, "content-type"))
            {
                form_encoded = value.starts_with("application/x-www-form-urlencoded");
            }
            else if (equalsIgnoringCase(name, "host"))
            {
                request.host = std::string(value.substr(0, value.find(':')));
            }
        }

        if (end == std::string_view::npos)
        {
            break;
        }
        rest.remove_prefix(end + 2);
    }

    if (content_length > kMaxBodyBytes)
    {
        connection.out = serialize(HttpResponse::text(413, "body too large"), false);
        connection.closing = true;
        return;
    }

    const std::size_t body_start = header_end + 4;
    if (connection.in.size() < body_start + content_length)
    {
        return; // body still arriving
    }

    request.body = connection.in.substr(body_start, content_length);
    connection.in.erase(0, body_start + content_length);

    if (form_encoded)
    {
        parsePairs(request.body, request.params);
    }

    HttpResponse response = dispatch(request);

    connection.out += serialize(response, keep_alive);
    connection.closing = connection.closing || !keep_alive;

    // A pipelined second request may already be in the buffer.
    if (!connection.closing && !connection.in.empty())
    {
        handle(connection);
    }
}

HttpResponse HttpServer::dispatch(const HttpRequest &request) const
{
    const auto found = routes_.find(request.method + " " + request.path);
    const HttpHandler &handler = found != routes_.end() ? found->second : fallback_;

    if (!handler)
    {
        return HttpResponse::text(404, "not found");
    }

    // The same rule the shell applies to apps (FR-17): a handler that throws
    // costs its request, not the device.
    try
    {
        return handler(request);
    }
    catch (const std::exception &error)
    {
        logError("http: handler for {} threw: {}", request.path, error.what());
        return HttpResponse::text(500, "internal error");
    }
    catch (...)
    {
        logError("http: handler for {} threw", request.path);
        return HttpResponse::text(500, "internal error");
    }
}

} // namespace matrixos
