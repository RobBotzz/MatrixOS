// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/launcher.h"

#include "gfx/font.h"
#include "gfx/surface.h"
#include "os/log.h"

#include <algorithm>
#include <utility>

namespace matrixos
{
namespace
{

constexpr int kRowHeight = kGlyphHeight + 1;
constexpr int kTextLeft = 4;

constexpr Color kSelectedText{255, 255, 255};
constexpr Color kIdleText{70, 70, 90};
constexpr Color kSelectionBar{255, 190, 0};

} // namespace

Launcher::Launcher(std::vector<std::string_view> entries, StartHandler onStart)
    : entries_(std::move(entries)), on_start_(std::move(onStart))
{
}

void Launcher::onInput(const InputEvent &event)
{
    if (entries_.empty())
    {
        return;
    }

    switch (event.type)
    {
    case InputType::Rotate:
    {
        // Wraps in both directions. Second place in the codebase doing this; a
        // third one earns a shared helper (NFR-17).
        const int count = static_cast<int>(entries_.size());
        const int next = (static_cast<int>(selected_) + event.delta) % count;
        selected_ = static_cast<std::size_t>((next + count) % count);
        break;
    }
    case InputType::Press:
        logInfo("launcher starting '{}'", entries_[selected_]);
        if (on_start_)
        {
            on_start_(selected_);
        }
        break;
    default:
        break;
    }
}

void Launcher::render(Surface &surface)
{
    if (entries_.empty())
    {
        drawText(surface, kTextLeft, 1, "NO APPS", Color{255, 60, 60});
        return;
    }

    const int visible = std::max(1, surface.height() / kRowHeight);
    const int total = static_cast<int>(entries_.size());

    // Keep the selection roughly centred, but never scroll past either end.
    const int first =
        std::clamp(static_cast<int>(selected_) - visible / 2, 0, std::max(0, total - visible));

    for (int slot = 0; slot < visible; ++slot)
    {
        const int index = first + slot;
        if (index >= total)
        {
            break;
        }

        const int y = slot * kRowHeight;
        const bool is_selected = static_cast<std::size_t>(index) == selected_;

        if (is_selected)
        {
            // A bar rather than an inverted row: on an LED panel a filled
            // background is uncomfortably bright.
            for (int row = 0; row < kGlyphHeight; ++row)
            {
                surface.setPixel(0, y + row, kSelectionBar);
                surface.setPixel(1, y + row, kSelectionBar);
            }
        }

        // Names longer than the panel simply run off the edge; Surface drops
        // out-of-range pixels, so no clipping is needed here.
        drawText(surface, kTextLeft, y, entries_[static_cast<std::size_t>(index)],
                 is_selected ? kSelectedText : kIdleText);
    }
}

} // namespace matrixos
