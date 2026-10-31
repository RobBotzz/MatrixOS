// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/picture.h"

#include <algorithm>

namespace matrixos
{
namespace
{

constexpr std::string_view kMagic = "MXP1";
constexpr std::size_t kHeaderBytes = 8;

int u16(std::string_view bytes, std::size_t at)
{
    return static_cast<unsigned char>(bytes[at]) | static_cast<unsigned char>(bytes[at + 1]) << 8;
}

void putU16(std::string &out, int value)
{
    out += static_cast<char>(value & 0xff);
    out += static_cast<char>((value >> 8) & 0xff);
}

} // namespace

std::optional<Picture> parsePicture(std::string_view bytes)
{
    if (bytes.size() < kHeaderBytes || bytes.size() > Picture::kMaxBytes ||
        bytes.substr(0, 4) != kMagic)
    {
        return std::nullopt;
    }

    const int width = static_cast<unsigned char>(bytes[4]);
    const int height = static_cast<unsigned char>(bytes[5]);
    const std::size_t count = static_cast<std::size_t>(u16(bytes, 6));

    if (width != Picture::kWidth || height != Picture::kHeight || count == 0 ||
        count > Picture::kMaxFrames ||
        bytes.size() != kHeaderBytes + count * (2 + Picture::kFrameBytes))
    {
        return std::nullopt;
    }

    Picture picture;
    picture.frames.reserve(count);
    std::size_t at = kHeaderBytes;
    for (std::size_t i = 0; i < count; ++i)
    {
        Picture::Frame frame;
        const int delay = u16(bytes, at);
        frame.delay_ms = count == 1 ? 0 : std::max(delay, Picture::kMinDelayMs);
        at += 2;
        frame.rgb.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                         bytes.begin() + static_cast<std::ptrdiff_t>(at + Picture::kFrameBytes));
        at += Picture::kFrameBytes;
        picture.frames.push_back(std::move(frame));
    }
    return picture;
}

std::string serializePicture(const Picture &picture)
{
    std::string out(kMagic);
    out += static_cast<char>(Picture::kWidth);
    out += static_cast<char>(Picture::kHeight);
    putU16(out, static_cast<int>(picture.frames.size()));
    for (const Picture::Frame &frame : picture.frames)
    {
        putU16(out, frame.delay_ms);
        out.append(frame.rgb.begin(), frame.rgb.end());
    }
    return out;
}

} // namespace matrixos
