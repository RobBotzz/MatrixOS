// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace matrixos
{

enum class LogLevel
{
    Debug,
    Info,
    Warn,
    Error,
};

/// Everything goes to **stderr**, never stdout.
///
/// That is not a style choice: the terminal simulator paints the frame on stdout,
/// so a single log line there would corrupt the picture. journald captures stderr
/// just as happily, which is all FR-21 needs.
void logWrite(LogLevel level, std::string_view message);

/// Messages below this level are dropped. Defaults to Info.
void logSetLevel(LogLevel level);

template <typename... Args> void logDebug(std::format_string<Args...> fmt, Args &&...args)
{
    logWrite(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args> void logInfo(std::format_string<Args...> fmt, Args &&...args)
{
    logWrite(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args> void logWarn(std::format_string<Args...> fmt, Args &&...args)
{
    logWrite(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args> void logError(std::format_string<Args...> fmt, Args &&...args)
{
    logWrite(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace matrixos
