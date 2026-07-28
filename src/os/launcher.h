// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "os/app.h"

#include <cstddef>
#include <functional>
#include <string_view>
#include <vector>

namespace matrixos
{

/// The app-selection menu — and itself an App, so the tick loop needs no special
/// case for it and its navigation is testable like any other app's.
///
/// Rotating moves the selection, pressing starts the highlighted entry. It knows
/// nothing about the shell beyond a callback, which is what keeps the dependency
/// pointing one way.
class Launcher : public App
{
public:
    /// Called with the index of the entry the user picked. The shell defers the
    /// actual switch, so this may be invoked from inside the launcher's own
    /// onInput without anything re-entering.
    using StartHandler = std::function<void(std::size_t)>;

    Launcher(std::vector<std::string_view> entries, StartHandler onStart);

    std::string_view name() const override { return "Launcher"; }

    void onInput(const InputEvent &event) override;
    void render(Surface &surface) override;

    void select(std::size_t index);

    std::size_t selected() const { return selected_; }
    std::size_t entryCount() const { return entries_.size(); }

private:
    std::vector<std::string_view> entries_;
    StartHandler on_start_;
    std::size_t selected_ = 0;
};

} // namespace matrixos
