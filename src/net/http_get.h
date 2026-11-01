// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/http_types.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace matrixos
{

/// An HTTPS GET through the `curl` binary (ADR-0017). Blocks for up to
/// `kHttpTimeoutSeconds`; never call it from the render thread. Any failure —
/// offline, DNS, a non-2xx status, a timeout — is `nullopt`.
std::optional<std::string> httpsGet(const std::string &url);

constexpr int kHttpTimeoutSeconds = 20;

/// Above any picture (ADR-0018) or forecast; a larger answer is a failure.
constexpr long kHttpMaxBytes = 2 * 1024 * 1024;

/// Any status comes back as a result; only a failure to talk to the server at
/// all is `nullopt`. Everything in `call` reaches curl through its standard
/// input, never its arguments (FR-24).
std::optional<HttpResult> httpsCall(const HttpCall &call);

/// What `httpsCall` writes to curl's standard input. Exposed for the tests.
std::string curlConfig(const HttpCall &call);

} // namespace matrixos
