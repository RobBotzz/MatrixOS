// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/state.h"

#include "os/log.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cstring>
#include <fstream>
#include <system_error>
#include <utility>

namespace matrixos
{

namespace
{

constexpr std::string_view kSuffix = ".conf";
constexpr std::string_view kTempSuffix = ".tmp";
constexpr std::string_view kBlanks = " \t\r\n";

bool isKeyChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '.' || c == '-';
}

// No dots: a namespace becomes a file name, so ".." must not be expressible.
bool isNamespaceChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

template <typename Predicate> bool allOf(std::string_view text, Predicate ok)
{
    if (text.empty())
    {
        return false;
    }
    for (const char c : text)
    {
        if (!ok(c))
        {
            return false;
        }
    }
    return true;
}

std::string_view trim(std::string_view text)
{
    const auto first = text.find_first_not_of(kBlanks);
    if (first == std::string_view::npos)
    {
        return {};
    }
    return text.substr(first, text.find_last_not_of(kBlanks) - first + 1);
}

bool writeFully(int fd, std::string_view content)
{
    std::size_t written = 0;
    while (written < content.size())
    {
        const ssize_t chunk = ::write(fd, content.data() + written, content.size() - written);
        if (chunk < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (chunk == 0)
        {
            return false;
        }
        written += static_cast<std::size_t>(chunk);
    }
    return true;
}

/// Temp file, `fsync`, `rename`, `fsync` the directory (FR-40). The final
/// directory `fsync` is what makes the rename itself durable.
bool writeAtomically(const std::filesystem::path &target, std::string_view content)
{
    const std::filesystem::path temp = target.string() + std::string(kTempSuffix);

    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
    {
        logError("state: cannot write '{}': {}", temp.string(), std::strerror(errno));
        return false;
    }

    bool ok = writeFully(fd, content) && ::fsync(fd) == 0;
    if (!ok)
    {
        logError("state: writing '{}' failed: {}", temp.string(), std::strerror(errno));
    }
    ::close(fd);

    if (ok && ::rename(temp.c_str(), target.c_str()) != 0)
    {
        logError("state: cannot replace '{}': {}", target.string(), std::strerror(errno));
        ok = false;
    }

    if (!ok)
    {
        ::unlink(temp.c_str());
        return false;
    }

    const int dir = ::open(target.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
    if (dir >= 0)
    {
        ::fsync(dir);
        ::close(dir);
    }
    return true;
}

} // namespace

StateSection::StateSection(std::filesystem::path path) : path_(std::move(path)) {}

void StateSection::load()
{
    if (path_.empty())
    {
        return;
    }

    std::ifstream file(path_);
    if (!file)
    {
        return; // no file yet: a fresh device, not an error
    }

    std::string line;
    while (std::getline(file, line))
    {
        const std::string_view text = line;
        const auto separator = text.find('=');
        const std::string_view key =
            separator == std::string_view::npos ? trim(text) : trim(text.substr(0, separator));

        if (separator == std::string_view::npos || !allOf(key, isKeyChar))
        {
            if (!key.empty())
            {
                logWarn("state: ignoring unreadable line in '{}': {}", path_.string(), line);
            }
            continue;
        }

        values_.insert_or_assign(std::string(key), std::string(trim(text.substr(separator + 1))));
    }
}

const std::string *StateSection::find(std::string_view key) const
{
    const auto found = values_.find(key);
    return found == values_.end() ? nullptr : &found->second;
}

int StateSection::getInt(std::string_view key, int fallback) const
{
    const std::string *raw = find(key);
    if (raw == nullptr)
    {
        return fallback;
    }

    int value = 0;
    const char *const end = raw->data() + raw->size();
    const auto [stop, error] = std::from_chars(raw->data(), end, value);

    // No logging here (unlike load()): this runs on the render path.
    return error == std::errc{} && stop == end ? value : fallback;
}

std::string StateSection::getString(std::string_view key, std::string_view fallback) const
{
    const std::string *raw = find(key);
    return raw == nullptr ? std::string(fallback) : *raw;
}

void StateSection::setInt(std::string_view key, int value)
{
    set(key, std::to_string(value));
}

void StateSection::setString(std::string_view key, std::string_view value)
{
    set(key, std::string(value));
}

void StateSection::set(std::string_view key, std::string value)
{
    if (!allOf(key, isKeyChar))
    {
        logError("state: refusing key '{}'", key);
        return;
    }

    std::string trimmed(trim(value)); // matches load(), so a reload reads the same
    if (trimmed.find('\n') != std::string::npos)
    {
        logError("state: refusing a value containing a newline for key '{}'", key);
        return;
    }

    const auto existing = values_.find(key);
    if (existing != values_.end() && existing->second == trimmed)
    {
        return;
    }

    values_.insert_or_assign(std::string(key), std::move(trimmed));
    dirty_ = true;
}

bool StateSection::save()
{
    if (!dirty_)
    {
        return true;
    }

    if (path_.empty())
    {
        dirty_ = false; // no root: memory-only is a supported state, not a failure
        return true;
    }

    std::string content;
    for (const auto &[key, value] : values_)
    {
        content += key;
        content += '=';
        content += value;
        content += '\n';
    }

    if (!writeAtomically(path_, content))
    {
        return false;
    }

    dirty_ = false;
    return true;
}

StateStore::StateStore(std::filesystem::path root) : root_(std::move(root))
{
    std::error_code error;
    std::filesystem::create_directories(root_, error);

    if (!std::filesystem::is_directory(root_, error))
    {
        logWarn("state: '{}' is not a usable directory — running without persistence",
                root_.string());
        return;
    }

    if (::access(root_.c_str(), W_OK | X_OK) != 0)
    {
        logWarn("state: '{}' is not writable by uid {} — running without persistence. "
                "The panel library drops privileges to 'daemon'; see docs/device-setup.md",
                root_.string(), ::geteuid());
        return;
    }

    persistent_ = true;
    logInfo("state: using '{}'", root_.string());
}

StateSection &StateStore::section(std::string_view name)
{
    const auto existing = sections_.find(name);
    if (existing != sections_.end())
    {
        return *existing->second;
    }

    std::filesystem::path path;
    if (!allOf(name, isNamespaceChar))
    {
        logError("state: '{}' is not a valid namespace — that section stays in memory", name);
    }
    else if (persistent_)
    {
        path = root_ / (std::string(name) + std::string(kSuffix));
    }

    std::unique_ptr<StateSection> section(new StateSection(std::move(path)));
    section->load();

    return *sections_.insert_or_assign(std::string(name), std::move(section)).first->second;
}

bool StateStore::saveAll()
{
    bool ok = true;
    for (auto &[name, section] : sections_)
    {
        ok = section->save() && ok;
    }
    return ok;
}

} // namespace matrixos
