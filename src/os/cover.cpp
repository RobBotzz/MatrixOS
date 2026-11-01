// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/cover.h"

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <algorithm>
#include <climits>
#include <memory>

namespace matrixos
{

std::optional<std::vector<std::uint8_t>> decodeCover(std::string_view jpeg, int size)
{
    if (jpeg.empty() || jpeg.size() > INT_MAX || size <= 0)
    {
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(jpeg.data()),
                              static_cast<int>(jpeg.size()), &width, &height, &channels, 3),
        &stbi_image_free);
    if (!pixels || width < size || height < size)
    {
        return std::nullopt;
    }

    const int side = std::min(width, height);
    const int left = (width - side) / 2;
    const int top = (height - side) / 2;

    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(size * size * 3));
    for (int y = 0; y < size; ++y)
    {
        const int y0 = top + y * side / size;
        const int y1 = std::max(y0 + 1, top + (y + 1) * side / size);
        for (int x = 0; x < size; ++x)
        {
            const int x0 = left + x * side / size;
            const int x1 = std::max(x0 + 1, left + (x + 1) * side / size);
            for (int c = 0; c < 3; ++c)
            {
                int sum = 0;
                for (int sy = y0; sy < y1; ++sy)
                {
                    for (int sx = x0; sx < x1; ++sx)
                    {
                        sum += pixels.get()[(sy * width + sx) * 3 + c];
                    }
                }
                rgb[static_cast<std::size_t>((y * size + x) * 3 + c)] =
                    static_cast<std::uint8_t>(sum / ((y1 - y0) * (x1 - x0)));
            }
        }
    }
    return rgb;
}

std::optional<std::vector<std::uint8_t>> decodeSprite(std::string_view png, int size)
{
    if (png.empty() || png.size() > INT_MAX || size <= 0)
    {
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(png.data()),
                              static_cast<int>(png.size()), &width, &height, &channels, 4),
        &stbi_image_free);
    if (!pixels)
    {
        return std::nullopt;
    }

    const auto alpha = [&](int x, int y) { return pixels.get()[(y * width + x) * 4 + 3]; };
    int left = width;
    int right = -1;
    int top = height;
    int bottom = -1;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (alpha(x, y) > 127)
            {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }
    if (right < 0)
    {
        return std::nullopt;
    }

    const int box_width = right - left + 1;
    const int box_height = bottom - top + 1;
    const int side = std::max(box_width, box_height);
    // The square around the figure, centred on it; outside the image counts as empty.
    const int square_left = left - (side - box_width) / 2;
    const int square_top = top - (side - box_height) / 2;

    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size * size * 4), 0);
    for (int y = 0; y < size; ++y)
    {
        const int y0 = square_top + y * side / size;
        const int y1 = std::max(y0 + 1, square_top + (y + 1) * side / size);
        for (int x = 0; x < size; ++x)
        {
            const int x0 = square_left + x * side / size;
            const int x1 = std::max(x0 + 1, square_left + (x + 1) * side / size);
            int seen = 0;
            int visible = 0;
            int sum[3] = {0, 0, 0};
            for (int sy = y0; sy < y1; ++sy)
            {
                for (int sx = x0; sx < x1; ++sx)
                {
                    ++seen;
                    if (sx < 0 || sy < 0 || sx >= width || sy >= height || alpha(sx, sy) <= 127)
                    {
                        continue;
                    }
                    ++visible;
                    for (int c = 0; c < 3; ++c)
                    {
                        sum[c] += pixels.get()[(sy * width + sx) * 4 + c];
                    }
                }
            }
            if (visible * 2 > seen)
            {
                const auto at = static_cast<std::size_t>((y * size + x) * 4);
                for (int c = 0; c < 3; ++c)
                {
                    rgba[at + static_cast<std::size_t>(c)] =
                        static_cast<std::uint8_t>(sum[c] / visible);
                }
                rgba[at + 3] = 255;
            }
        }
    }
    return rgba;
}

} // namespace matrixos
