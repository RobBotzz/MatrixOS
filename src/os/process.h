// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <string>
#include <vector>

namespace matrixos
{

/// Runs a command with an explicit argument vector — no shell, so nothing in an
/// argument can be interpreted (ADR-0013, ADR-0017). Blocks for up to
/// `timeout_ms`; never call it from the render thread.
///
/// Returns the exit status, or -1 if the child could not be started or had to be
/// killed on the timeout. The child is always reaped: a device that runs for
/// months must not accumulate a zombie per request.
int runCommand(const std::vector<std::string> &args, std::string *output, int timeout_ms);

} // namespace matrixos
