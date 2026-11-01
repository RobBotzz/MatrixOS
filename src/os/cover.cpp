// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/cover.h"

#define STBI_ONLY_JPEG
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

} // namespace matrixos
