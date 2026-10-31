// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/picture.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace matrixos
{

class StateSection;
class StateStore;

/// One `message` event from an ntfy poll that carries an attachment.
struct InboxMessage
{
    std::string id;
    long long time = 0;
    std::string url;
    long long size = 0;
};

/// ntfy's `json?poll=1` answer: one JSON object per line. Lines that are not a
/// message with an attachment are skipped.
std::vector<InboxMessage> parseInbox(std::string_view lines);

struct GallerySnapshot
{
    std::vector<std::shared_ptr<const Picture>> pictures; ///< newest first
    std::uint64_t arrivals = 0;                           ///< counts up whenever a picture arrives
};

/// Collects pictures sent to this unit's ntfy topic and keeps the newest
/// `kKeep` on the state partition (ADR-0018). Apps read `snapshot()`, which
/// never waits on the network or the card.
class GalleryService
{
public:
    using Fetch = std::function<std::optional<std::string>(const std::string &url)>;

    static constexpr std::size_t kKeep = 30;
    static constexpr const char *kServer = "https://ntfy.sh";
    static constexpr const char *kSendPage = "https://robbotzz.github.io/MatrixOS/send/";
    static constexpr std::string_view kTopicPrefix = "matrixos-";
    static constexpr std::size_t kTopicRandomChars = 20;

    struct Timing
    {
        std::chrono::milliseconds poll{std::chrono::seconds(60)};
    };

    GalleryService(StateStore &store, std::filesystem::path directory, Fetch fetch);
    GalleryService(StateStore &store, std::filesystem::path directory, Fetch fetch, Timing timing);
    ~GalleryService();

    GalleryService(const GalleryService &) = delete;
    GalleryService &operator=(const GalleryService &) = delete;

    /// Loads what is stored, then starts polling.
    void begin();

    GallerySnapshot snapshot() const;
    std::uint64_t arrivals() const;

    std::string topic() const;
    std::string sendLink() const;

    /// Draws a new address; the old link stops working.
    void newAddress();

    /// FR-42: a new address and no pictures, for a unit that changes hands.
    void reset();

    /// Reads the stored pictures. Part of `begin()`; public for the tests.
    void load();

    /// One poll, synchronously. The worker's loop body; public for the tests.
    bool poll();

private:
    void run();
    void drawTopic();
    void evict();

    StateSection &saved_;
    std::filesystem::path directory_;
    Fetch fetch_;
    Timing timing_;

    mutable std::mutex mutex_;
    std::string topic_;
    std::string since_;
    std::uint64_t address_epoch_ = 0;
    std::vector<std::filesystem::path> files_; ///< newest first, parallel to pictures_
    std::vector<std::shared_ptr<const Picture>> pictures_;
    std::uint64_t arrivals_ = 0;

    std::condition_variable wake_;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};

} // namespace matrixos
