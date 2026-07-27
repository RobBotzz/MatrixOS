// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "hal/matrix/matrix_display.h"

#include "gfx/surface.h"

#include "led-matrix.h"

#include <algorithm>
#include <utility>

namespace matrixos
{

std::unique_ptr<MatrixDisplay> MatrixDisplay::createFromFlags(int *argc, char ***argv)
{
    rgb_matrix::RGBMatrix::Options options;
    options.hardware_mapping = "regular"; // direct wiring, no Adafruit HAT
    options.rows = 32;                    // one 64x32 panel
    options.cols = 64;
    options.chain_length = 1;
    options.parallel = 1;

    rgb_matrix::RuntimeOptions runtime;

    // Command line flags win over these defaults, which is what FR-5 asks for:
    // geometry and tuning are supplied at startup rather than compiled in.
    std::unique_ptr<rgb_matrix::RGBMatrix> matrix(
        rgb_matrix::RGBMatrix::CreateFromFlags(argc, argv, &options, &runtime));
    if (!matrix)
    {
        return nullptr;
    }

    // Double buffering: we draw into this canvas and swap it in on vsync (FR-3).
    rgb_matrix::FrameCanvas *back = matrix->CreateFrameCanvas();
    if (!back)
    {
        return nullptr;
    }

    return std::unique_ptr<MatrixDisplay>(new MatrixDisplay(std::move(matrix), back));
}

MatrixDisplay::MatrixDisplay(std::unique_ptr<rgb_matrix::RGBMatrix> matrix,
                             rgb_matrix::FrameCanvas *back)
    : matrix_(std::move(matrix)), back_(back)
{
}

MatrixDisplay::~MatrixDisplay() = default;

int MatrixDisplay::width() const
{
    return back_->width();
}

int MatrixDisplay::height() const
{
    return back_->height();
}

void MatrixDisplay::present(const Surface &frame)
{
    const int w = std::min(frame.width(), back_->width());
    const int h = std::min(frame.height(), back_->height());

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const Color color = frame.pixel(x, y);
            back_->SetPixel(x, y, color.r, color.g, color.b);
        }
    }

    // Returns the canvas that was on screen; it becomes our next back buffer.
    back_ = matrix_->SwapOnVSync(back_);
}

void MatrixDisplay::clear()
{
    back_->Clear();
    matrix_->Clear();
}

} // namespace matrixos
