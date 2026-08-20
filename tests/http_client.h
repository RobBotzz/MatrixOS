// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace matrixos::test
{

struct HttpReply
{
    int status = 0;
    std::map<std::string, std::string> headers; ///< names lowercased
    std::string body;

    std::string header(std::string_view name) const
    {
        const auto found = headers.find(std::string(name));
        return found == headers.end() ? std::string() : found->second;
    }
};

/// A real TCP client for the tests, because the thing under test is a socket
/// server: faking the socket would test the fake. Deliberately dumb — it speaks
/// exactly as much HTTP as the tests need to say.
class HttpClient
{
public:
    explicit HttpClient(int port)
    {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0)
        {
            throw std::runtime_error("socket");
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<uint16_t>(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (::connect(fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
        {
            ::close(fd_);
            throw std::runtime_error("connect");
        }
    }

    ~HttpClient()
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
        }
    }

    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;

    void writeRaw(std::string_view raw)
    {
        std::size_t sent = 0;
        while (sent < raw.size())
        {
            const ssize_t count = ::send(fd_, raw.data() + sent, raw.size() - sent, MSG_NOSIGNAL);
            if (count <= 0)
            {
                throw std::runtime_error("send");
            }
            sent += static_cast<std::size_t>(count);
        }
    }

    void get(std::string_view path, std::string_view host = "matrixos.local")
    {
        writeRaw(std::string("GET ") + std::string(path) +
                 " HTTP/1.1\r\nHost: " + std::string(host) + "\r\n\r\n");
    }

    void postForm(std::string_view path, std::string_view body)
    {
        writeRaw(std::string("POST ") + std::string(path) +
                 " HTTP/1.1\r\nHost: matrixos.local\r\n"
                 "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: " +
                 std::to_string(body.size()) + "\r\n\r\n" + std::string(body));
    }

    /// Reads exactly one response, using Content-Length to know where it ends.
    HttpReply read(std::chrono::milliseconds timeout = std::chrono::milliseconds(3000))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;

        std::size_t header_end = std::string::npos;
        while ((header_end = buffer_.find("\r\n\r\n")) == std::string::npos)
        {
            if (!pump(deadline))
            {
                throw std::runtime_error("timed out waiting for headers");
            }
        }

        HttpReply reply;
        const std::string head = buffer_.substr(0, header_end);

        const auto first = head.find("\r\n");
        const auto space = head.find(' ');
        reply.status = std::atoi(head.substr(space + 1, 3).c_str());

        std::size_t at = first == std::string::npos ? head.size() : first + 2;
        while (at < head.size())
        {
            const auto end = head.find("\r\n", at);
            const std::string line = head.substr(at, end == std::string::npos ? end : end - at);
            const auto colon = line.find(':');
            if (colon != std::string::npos)
            {
                std::string name = line.substr(0, colon);
                for (char &c : name)
                {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                const auto value_start = line.find_first_not_of(' ', colon + 1);
                reply.headers[name] =
                    value_start == std::string::npos ? "" : line.substr(value_start);
            }
            if (end == std::string::npos)
            {
                break;
            }
            at = end + 2;
        }

        std::size_t length = 0;
        const auto content_length = reply.headers.find("content-length");
        if (content_length != reply.headers.end())
        {
            length = static_cast<std::size_t>(std::atol(content_length->second.c_str()));
        }

        while (buffer_.size() < header_end + 4 + length)
        {
            if (!pump(deadline))
            {
                throw std::runtime_error("timed out waiting for the body");
            }
        }

        reply.body = buffer_.substr(header_end + 4, length);
        buffer_.erase(0, header_end + 4 + length);
        return reply;
    }

private:
    /// One read with a deadline. False means nothing more is coming.
    bool pump(std::chrono::steady_clock::time_point deadline)
    {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();
        if (remaining <= 0)
        {
            return false;
        }

        pollfd fd{fd_, POLLIN, 0};
        if (::poll(&fd, 1, static_cast<int>(remaining)) <= 0)
        {
            return false;
        }

        char chunk[4096];
        const ssize_t count = ::recv(fd_, chunk, sizeof(chunk), 0);
        if (count <= 0)
        {
            return false;
        }

        buffer_.append(chunk, static_cast<std::size_t>(count));
        return true;
    }

    int fd_ = -1;
    std::string buffer_;
};

} // namespace matrixos::test
