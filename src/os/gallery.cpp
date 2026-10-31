// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/gallery.h"

#include "os/log.h"
#include "os/state.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <system_error>
#include <utility>

namespace matrixos
{
namespace
{

constexpr std::string_view kSection = "gallery";
constexpr std::string_view kTopicKey = "topic";
constexpr std::string_view kSinceKey = "since";
constexpr std::string_view kExtension = ".mxp";

bool safeId(std::string_view id)
{
    return !id.empty() && id.size() <= 32 &&
           std::all_of(id.begin(), id.end(),
                       [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; });
}

/// Zero-padded time first, so name order is arrival order.
std::string fileName(const InboxMessage &message)
{
    char time[24];
    std::snprintf(time, sizeof(time), "%012lld", message.time);
    return std::string(time) + "-" + message.id + std::string(kExtension);
}

std::optional<std::string> readFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), {});
}

} // namespace

std::vector<InboxMessage> parseInbox(std::string_view lines)
{
    std::vector<InboxMessage> messages;
    while (!lines.empty())
    {
        const auto end = lines.find('\n');
        const std::string_view line = lines.substr(0, end);
        lines.remove_prefix(end == std::string_view::npos ? lines.size() : end + 1);

        const nlohmann::json event = nlohmann::json::parse(line, nullptr, false);
        if (event.is_discarded() || !event.is_object() || event.value("event", "") != "message" ||
            !event.contains("attachment") || !event["attachment"].is_object())
        {
            continue;
        }

        const auto &attachment = event["attachment"];
        InboxMessage message;
        message.id = event.value("id", "");
        message.time = event.value("time", 0LL);
        message.url = attachment.value("url", "");
        message.size = attachment.value("size", 0LL);
        if (!message.id.empty() && !message.url.empty())
        {
            messages.push_back(std::move(message));
        }
    }
    return messages;
}

GalleryService::GalleryService(StateStore &store, std::filesystem::path directory, Fetch fetch)
    : GalleryService(store, std::move(directory), std::move(fetch), Timing{})
{
}

GalleryService::GalleryService(StateStore &store, std::filesystem::path directory, Fetch fetch,
                               Timing timing)
    : saved_(store.section(kSection)), directory_(std::move(directory)), fetch_(std::move(fetch)),
      timing_(timing)
{
    topic_ = saved_.getString(kTopicKey, "");
    since_ = saved_.getString(kSinceKey, "");
    if (topic_.empty())
    {
        drawTopic();
    }
}

GalleryService::~GalleryService()
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable())
    {
        worker_.join();
    }
}

void GalleryService::begin()
{
    worker_ = std::thread(
        [this]
        {
            load();
            run();
        });
}

GallerySnapshot GalleryService::snapshot() const
{
    std::lock_guard lock(mutex_);
    return {pictures_, arrivals_};
}

std::uint64_t GalleryService::arrivals() const
{
    std::lock_guard lock(mutex_);
    return arrivals_;
}

std::string GalleryService::topic() const
{
    std::lock_guard lock(mutex_);
    return topic_;
}

std::string GalleryService::sendLink() const
{
    return std::string(kSendPage) + "#" + topic();
}

void GalleryService::drawTopic()
{
    static constexpr std::string_view kAlphabet = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device random;
    std::uniform_int_distribution<std::size_t> pick(0, kAlphabet.size() - 1);

    std::string topic(kTopicPrefix);
    for (std::size_t i = 0; i < kTopicRandomChars; ++i)
    {
        topic += kAlphabet[pick(random)];
    }

    topic_ = std::move(topic);
    since_.clear();
    ++address_epoch_;
    saved_.setString(kTopicKey, topic_);
    saved_.setString(kSinceKey, "");
    saved_.save();
}

void GalleryService::newAddress()
{
    {
        std::lock_guard lock(mutex_);
        drawTopic();
    }
    logInfo("gallery: new address drawn");
}

void GalleryService::reset()
{
    std::vector<std::filesystem::path> files;
    {
        std::lock_guard lock(mutex_);
        drawTopic();
        files = std::move(files_);
        files_.clear();
        pictures_.clear();
    }

    std::error_code ignored;
    for (const auto &file : files)
    {
        std::filesystem::remove(file, ignored);
    }
    logInfo("gallery: reset, {} pictures deleted", files.size());
}

void GalleryService::load()
{
    std::error_code error;
    std::filesystem::create_directories(directory_, error);

    std::vector<std::filesystem::path> found;
    for (const auto &entry : std::filesystem::directory_iterator(directory_, error))
    {
        if (entry.path().extension() == kExtension)
        {
            found.push_back(entry.path());
        }
    }
    std::sort(found.begin(), found.end(), std::greater<>());

    std::vector<std::filesystem::path> files;
    std::vector<std::shared_ptr<const Picture>> pictures;
    for (const auto &path : found)
    {
        const std::optional<std::string> bytes = readFile(path);
        std::optional<Picture> picture = bytes ? parsePicture(*bytes) : std::nullopt;
        if (picture && files.size() < kKeep)
        {
            files.push_back(path);
            pictures.push_back(std::make_shared<const Picture>(std::move(*picture)));
        }
    }

    std::lock_guard lock(mutex_);
    files_ = std::move(files);
    pictures_ = std::move(pictures);
    logInfo("gallery: {} pictures stored", files_.size());
}

bool GalleryService::poll()
{
    std::string topic;
    std::string since;
    std::uint64_t epoch = 0;
    {
        std::lock_guard lock(mutex_);
        topic = topic_;
        since = since_;
        epoch = address_epoch_;
    }

    const std::optional<std::string> inbox =
        fetch_(std::string(kServer) + "/" + topic +
               "/json?poll=1&since=" + (since.empty() ? "all" : since));
    if (!inbox)
    {
        return false;
    }

    const std::string server_files = std::string(kServer) + "/file/";
    std::string last = since;
    for (const InboxMessage &message : parseInbox(*inbox))
    {
        last = message.id;
        if (!safeId(message.id) || message.url.rfind(server_files, 0) != 0 || message.size <= 0 ||
            message.size > static_cast<long long>(Picture::kMaxBytes))
        {
            logWarn("gallery: skipped message {}: not a picture from {}", message.id, kServer);
            continue;
        }

        const std::filesystem::path path = directory_ / fileName(message);
        {
            std::lock_guard lock(mutex_);
            if (std::find(files_.begin(), files_.end(), path) != files_.end())
            {
                continue;
            }
        }

        const std::optional<std::string> bytes = fetch_(message.url);
        std::optional<Picture> picture = bytes ? parsePicture(*bytes) : std::nullopt;
        if (!picture)
        {
            logWarn("gallery: skipped message {}: not a valid picture", message.id);
            continue;
        }

        std::lock_guard lock(mutex_);
        if (epoch != address_epoch_)
        {
            return false; // sent to an address that was replaced meanwhile
        }
        if (!writeAtomically(path, *bytes))
        {
            continue;
        }
        files_.insert(files_.begin(), path);
        pictures_.insert(pictures_.begin(), std::make_shared<const Picture>(std::move(*picture)));
        ++arrivals_;
        logInfo("gallery: picture {} arrived ({} frames)", message.id,
                pictures_.front()->frames.size());
        evict();
    }

    std::lock_guard lock(mutex_);
    if (epoch == address_epoch_ && last != since_)
    {
        since_ = last;
        saved_.setString(kSinceKey, since_);
        saved_.save();
    }
    return true;
}

void GalleryService::evict()
{
    std::error_code ignored;
    while (files_.size() > kKeep)
    {
        std::filesystem::remove(files_.back(), ignored);
        files_.pop_back();
        pictures_.pop_back();
    }
}

void GalleryService::run()
{
    while (!stopping_)
    {
        poll();

        std::unique_lock lock(mutex_);
        wake_.wait_for(lock, timing_.poll, [this] { return stopping_.load(); });
    }
}

} // namespace matrixos
