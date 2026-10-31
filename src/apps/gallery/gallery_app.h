// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/app.h"
#include "os/gallery.h"

namespace matrixos
{

/// Pictures sent to this unit (v0.6, ADR-0018). Newest first; they take turns
/// every 30 seconds, rotating browses, and a new arrival is shown at once.
class GalleryApp : public App
{
public:
    explicit GalleryApp(const GalleryService &gallery);

    std::string_view name() const override { return "Gallery"; }

    void onEnter() override;
    void onInput(const InputEvent &event) override;
    void update(Duration dt) override;
    void render(Surface &surface) override;

    std::size_t index() const { return index_; }

private:
    void read();
    void show(std::size_t index);

    const GalleryService &gallery_;
    GallerySnapshot snapshot_;
    std::uint64_t arrivals_seen_ = 0;
    std::size_t index_ = 0;
    std::size_t frame_ = 0;
    float frame_time_ = 0.0F;
    float since_switch_ = 0.0F;
    float since_read_ = 0.0F;
};

} // namespace matrixos
