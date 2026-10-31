// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "net/http_get.h"

#include "os/process.h"
#include "os/version.h"

namespace matrixos
{

std::optional<std::string> httpsGet(const std::string &url)
{
    std::string body;
    const int status = runCommand({"curl", "--fail", "--silent", "--show-error", "--proto",
                                   "=https", "--max-time", std::to_string(kHttpTimeoutSeconds),
                                   "--user-agent", std::string("MatrixOS/") + kVersion, url},
                                  &body, (kHttpTimeoutSeconds + 5) * 1000);
    if (status != 0)
    {
        return std::nullopt;
    }
    return body;
}

} // namespace matrixos
