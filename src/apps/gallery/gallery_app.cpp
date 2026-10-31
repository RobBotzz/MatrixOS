// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "apps/gallery/gallery_app.h"

#include "gfx/font.h"
#include "gfx/surface.h"

namespace matrixos
{
namespace
{

constexpr Color kEmpty{70, 78, 96};
constexpr Color kHint{130, 145, 175};

constexpr float kReadInterval = 0.5F;
constexpr float kCycleInterval = 30.0F;

} // namespace

GalleryApp::GalleryApp(const GalleryService &gallery) : gallery_(gallery) {}

void GalleryApp::onEnter()
{
    since_read_ = 0.0F;
    read();
}

void GalleryApp::read()
{
    snapshot_ = gallery_.snapshot();
    if (snapshot_.arrivals != arrivals_seen_)
    {
        arrivals_seen_ = snapshot_.arrivals;
        show(0);
    }
    else if (index_ >= snapshot_.pictures.size())
    {
        show(0);
    }
}

void GalleryApp::show(std::size_t index)
{
    index_ = index;
    frame_ = 0;
    frame_time_ = 0.0F;
    since_switch_ = 0.0F;
}

void GalleryApp::onInput(const InputEvent &event)
{
    const auto count = static_cast<int>(snapshot_.pictures.size());
    if (event.type != InputType::Rotate || count == 0)
    {
        return;
    }

    const int next = (static_cast<int>(index_) + event.delta) % count;
    show(static_cast<std::size_t>((next + count) % count));
}

void GalleryApp::update(Duration dt)
{
    since_read_ += dt.count();
    if (since_read_ >= kReadInterval)
    {
        since_read_ = 0.0F;
        read();
    }

    if (snapshot_.pictures.empty())
    {
        return;
    }

    since_switch_ += dt.count();
    if (since_switch_ >= kCycleInterval && snapshot_.pictures.size() > 1)
    {
        show((index_ + 1) % snapshot_.pictures.size());
        return;
    }

    const Picture &picture = *snapshot_.pictures[index_];
    if (picture.frames.size() < 2)
    {
        return;
    }
    frame_time_ += dt.count() * 1000.0F;
    while (frame_time_ >= static_cast<float>(picture.frames[frame_].delay_ms))
    {
        frame_time_ -= static_cast<float>(picture.frames[frame_].delay_ms);
        frame_ = (frame_ + 1) % picture.frames.size();
    }
}

void GalleryApp::render(Surface &surface)
{
    if (snapshot_.pictures.empty())
    {
        drawTextCentered(surface, 6, "NO", kEmpty, 1);
        drawTextCentered(surface, 18, "PICTURES", kHint, 1);
        return;
    }

    const Picture::Frame &frame = snapshot_.pictures[index_]->frames[frame_];
    std::size_t at = 0;
    for (int y = 0; y < Picture::kHeight; ++y)
    {
        for (int x = 0; x < Picture::kWidth; ++x, at += 3)
        {
            surface.setPixel(x, y, {frame.rgb[at], frame.rgb[at + 1], frame.rgb[at + 2]});
        }
    }
}

} // namespace matrixos
