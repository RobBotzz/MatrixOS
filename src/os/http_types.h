// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace matrixos
{

/// A request whose URL, headers or body may carry a secret (ADR-0019). Services
/// take a function that performs it, so tests hand them canned answers.
struct HttpCall
{
    std::string method = "GET";
    std::string url;
    std::vector<std::string> headers;
    std::string body;
    bool send_body = false; ///< also for an empty body, which some APIs want as Content-Length: 0
};

struct HttpResult
{
    int status = 0;
    std::string body;
};

/// RFC 3986 unreserved characters stay, everything else is `%XX`.
std::string percentEncode(std::string_view text);

/// `application/x-www-form-urlencoded`, which is also a URL query.
std::string formEncode(const std::vector<std::pair<std::string, std::string>> &fields);

} // namespace matrixos
