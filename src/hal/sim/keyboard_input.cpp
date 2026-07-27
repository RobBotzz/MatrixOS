// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "hal/sim/keyboard_input.h"

#include "os/log.h"

#include <unistd.h>

namespace matrixos
{

KeyboardInput::KeyboardInput()
{
    if (isatty(STDIN_FILENO) == 0)
    {
        logInfo("stdin is not a terminal, keyboard input disabled");
        return;
    }

    if (tcgetattr(STDIN_FILENO, &original_) != 0)
    {
        logWarn("could not read terminal settings, keyboard input disabled");
        return;
    }

    termios raw = original_;

    // Keys arrive immediately and are not echoed over the rendered frame. ISIG
    // stays on deliberately, so Ctrl-C still produces SIGINT and the shutdown
    // path in main.cpp keeps working.
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 0;  // read() returns straight away ...
    raw.c_cc[VTIME] = 0; // ... with whatever is there, possibly nothing

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
    {
        logWarn("could not switch the terminal to raw mode, keyboard input disabled");
        return;
    }

    raw_mode_ = true;
}

KeyboardInput::~KeyboardInput()
{
    if (raw_mode_)
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_);
    }
}

std::vector<InputEvent> KeyboardInput::poll()
{
    std::vector<InputEvent> events;
    if (!raw_mode_)
    {
        return events;
    }

    char buffer[32];
    const ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
    if (count <= 0)
    {
        return events;
    }

    for (ssize_t i = 0; i < count; ++i)
    {
        // Arrow keys arrive as the three bytes ESC '[' letter.
        if (buffer[i] == '\x1b' && i + 2 < count && buffer[i + 1] == '[')
        {
            switch (buffer[i + 2])
            {
            case 'A': // up
            case 'C': // right
                events.push_back({InputType::Rotate, +1});
                break;
            case 'B': // down
            case 'D': // left
                events.push_back({InputType::Rotate, -1});
                break;
            default:
                break;
            }
            i += 2;
            continue;
        }

        switch (buffer[i])
        {
        case ' ':
        case '\r':
        case '\n':
            events.push_back({InputType::Press, 0});
            break;
        case 'h':
        case 'H':
            events.push_back({InputType::Home, 0});
            break;
        case 'd':
        case 'D':
            events.push_back({InputType::DoublePress, 0});
            break;
        case 'l':
        case 'L':
            events.push_back({InputType::LongPress, 0});
            break;
        default:
            break;
        }
    }

    // Only when something happened, so this stays quiet at 60 polls per second.
    // Run with --verbose to trace the input path key by key.
    if (!events.empty())
    {
        logDebug("keyboard produced {} event(s) from {} byte(s)", events.size(), count);
    }

    return events;
}

} // namespace matrixos
