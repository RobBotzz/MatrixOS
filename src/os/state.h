// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace matrixos
{

/// One namespace of persisted state, backed by a single `key=value` file
/// (ADR-0011). Each file is replaced independently and atomically, so one
/// namespace cannot corrupt another.
class StateSection
{
public:
    /// A missing key and an unparseable value both return `fallback`.
    int getInt(std::string_view key, int fallback) const;
    std::string getString(std::string_view key, std::string_view fallback) const;

    void setInt(std::string_view key, int value);
    void setString(std::string_view key, std::string_view value);

    bool dirty() const { return dirty_; }

    /// Atomic replace (FR-40). Fails only if a pending write could not be made;
    /// nothing to write, or nowhere to write to, both count as success.
    bool save();

private:
    friend class StateStore;

    explicit StateSection(std::filesystem::path path); // empty path: memory only

    void load();
    const std::string *find(std::string_view key) const;
    void set(std::string_view key, std::string value);

    std::filesystem::path path_;
    bool dirty_ = false;
    std::map<std::string, std::string, std::less<>> values_;
};

/// Everything the device remembers, in one writable location (FR-39).
class StateStore
{
public:
    /// Creates `root` if missing. If it is not usable, degrades to memory-only
    /// and logs once rather than refusing to start (a directory owned by the
    /// wrong user must not brick a device).
    explicit StateStore(std::filesystem::path root);

    /// A store that writes nothing, for tests.
    static StateStore inMemory() { return StateStore{}; }

    /// Loaded on first use and kept afterwards. `name` is an explicit identifier
    /// (`snake`, `settings`), never an app's display name.
    StateSection &section(std::string_view name);

    bool persistent() const { return persistent_; }
    const std::filesystem::path &root() const { return root_; }

    bool saveAll();

private:
    StateStore() = default;

    std::filesystem::path root_;
    bool persistent_ = false;
    std::map<std::string, std::unique_ptr<StateSection>, std::less<>> sections_;
};

} // namespace matrixos
