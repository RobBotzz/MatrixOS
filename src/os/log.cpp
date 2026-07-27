// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/log.h"

#include <cstdio>

namespace matrixos
{
namespace
{

LogLevel g_threshold = LogLevel::Info;

const char *levelName(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "?";
}

} // namespace

void logSetLevel(LogLevel level)
{
    g_threshold = level;
}

void logWrite(LogLevel level, std::string_view message)
{
    if (level < g_threshold)
    {
        return;
    }

    // stderr, so stdout stays exclusively the simulator's canvas.
    std::fprintf(stderr, "[%-5s] %.*s\n", levelName(level), static_cast<int>(message.size()),
                 message.data());
}

} // namespace matrixos
