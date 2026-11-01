// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/app.h"
#include "os/spotify.h"

namespace matrixos
{

/// What the owner's Spotify is playing (v0.7, ADR-0019). Press plays or pauses,
/// double-press skips, a long press goes back, rotating sets the volume.
class SpotifyApp : public App
{
public:
    explicit SpotifyApp(SpotifyService &spotify);

    std::string_view name() const override { return "Spotify"; }
    bool wantsDoublePress() const override { return true; }

    void onEnter() override;
    void onExit() override;
    void onInput(const InputEvent &event) override;
    void update(Duration dt) override;
    void render(Surface &surface) override;

    /// The level being dialled in, or -1 while the knob is not being turned.
    int pendingVolume() const { return pending_volume_; }

private:
    void renderTrack(Surface &surface, const Track &track) const;
    void renderMessage(Surface &surface, std::string_view top, std::string_view bottom) const;

    SpotifyService &spotify_;
    SpotifySnapshot snapshot_;
    std::uint64_t revision_seen_ = 0;
    float since_revision_ = 0.0F; ///< progress runs on between polls
    float since_read_ = 0.0F;
    float scroll_ = 0.0F;

    int pending_volume_ = -1;
    float since_turn_ = 0.0F;
    float volume_shown_ = 0.0F;
};

} // namespace matrixos
