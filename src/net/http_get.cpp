// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "net/http_get.h"

#include "os/process.h"
#include "os/version.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace matrixos
{

std::optional<std::string> httpsGet(const std::string &url)
{
    std::string body;
    const int status = runCommand({"curl", "--fail", "--silent", "--show-error", "--proto",
                                   "=https", "--max-time", std::to_string(kHttpTimeoutSeconds),
                                   "--max-filesize", std::to_string(kHttpMaxBytes), "--user-agent",
                                   std::string("MatrixOS/") + kVersion, url},
                                  &body, (kHttpTimeoutSeconds + 5) * 1000);
    if (status != 0)
    {
        return std::nullopt;
    }
    return body;
}

namespace
{

std::string quoted(std::string_view value)
{
    std::string out = "\"";
    for (const char c : value)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

} // namespace

std::string curlConfig(const HttpCall &call)
{
    std::string config = "url = " + quoted(call.url) + "\n";
    config += "request = " + quoted(call.method) + "\n";
    for (const std::string &header : call.headers)
    {
        config += "header = " + quoted(header) + "\n";
    }
    if (call.send_body || !call.body.empty())
    {
        config += "data-binary = " + quoted(call.body) + "\n";
    }
    return config;
}

std::optional<HttpResult> httpsCall(const HttpCall &call)
{
    const std::string config = curlConfig(call);
    std::string output;
    const int status = runCommand({"curl", "--silent", "--show-error", "--proto", "=https",
                                   "--max-time", std::to_string(kHttpTimeoutSeconds),
                                   "--max-filesize", std::to_string(kHttpMaxBytes), "--user-agent",
                                   std::string("MatrixOS/") + kVersion, "--write-out",
                                   "\n%{http_code}", "--config", "-"},
                                  &output, (kHttpTimeoutSeconds + 5) * 1000, &config);
    const auto split = output.rfind('\n');
    if (status != 0 || split == std::string::npos)
    {
        return std::nullopt;
    }

    HttpResult result;
    result.status = std::atoi(output.c_str() + split + 1);
    result.body = output.substr(0, split);
    if (result.status == 0)
    {
        return std::nullopt;
    }
    return result;
}

} // namespace matrixos
