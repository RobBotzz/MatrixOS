// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <unistd.h>

#include <filesystem>
#include <string>
#include <system_error>

namespace matrixos::test
{

/// A real directory in the real filesystem, removed again afterwards.
class TempDir
{
public:
    TempDir()
    {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() /
                ("matrixos-test-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
        std::filesystem::remove_all(path_);
    }

    ~TempDir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;

    const std::filesystem::path &path() const { return path_; }
    std::filesystem::path file(const std::string &name) const { return path_ / name; }

private:
    std::filesystem::path path_;
};

} // namespace matrixos::test
