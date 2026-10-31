// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/process.h"

#include "os/log.h"

#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

extern char **environ;

namespace matrixos
{

int runCommand(const std::vector<std::string> &args, std::string *output, int timeout_ms)
{
    int fds[2] = {-1, -1};
    if (::pipe(fds) != 0)
    {
        return -1;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addclose(&actions, fds[1]);

    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (const std::string &arg : args)
    {
        argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int spawned = ::posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(fds[1]);

    if (spawned != 0)
    {
        ::close(fds[0]);
        logWarn("process: cannot run '{}': {}", args[0], std::strerror(spawned));
        return -1;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    bool timed_out = false;

    for (;;)
    {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();
        if (remaining <= 0)
        {
            timed_out = true;
            break;
        }

        pollfd fd{fds[0], POLLIN, 0};
        const int ready = ::poll(&fd, 1, static_cast<int>(remaining));
        if (ready == 0)
        {
            timed_out = true;
            break;
        }
        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }

        char chunk[4096];
        const ssize_t count = ::read(fds[0], chunk, sizeof(chunk));
        if (count <= 0)
        {
            break; // the child closed stdout, which it does when it exits
        }
        if (output != nullptr)
        {
            output->append(chunk, static_cast<std::size_t>(count));
        }
    }

    ::close(fds[0]);

    if (timed_out)
    {
        logWarn("process: '{}' timed out after {} ms", args[0], timeout_ms);
        ::kill(pid, SIGKILL);
    }

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR)
    {
    }

    if (timed_out)
    {
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

} // namespace matrixos
