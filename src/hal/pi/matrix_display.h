// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "hal/display.h"

#include <memory>

namespace rgb_matrix
{
class FrameCanvas;
class RGBMatrix;
} // namespace rgb_matrix

namespace matrixos
{

/// Drives the real HUB75 panel through rpi-rgb-led-matrix.
///
/// Only built for aarch64 (ADR-0001) and only usable as root, because the library
/// maps /dev/mem directly (C-2). This is the one place in the project that touches
/// that library.
class MatrixDisplay : public Display
{
public:
    /// Creates the panel, consuming the library's own --led-* flags from argc/argv.
    /// Returns nullptr if the matrix could not be initialised.
    static std::unique_ptr<MatrixDisplay> createFromFlags(int *argc, char ***argv);

    ~MatrixDisplay() override;

    int width() const override;
    int height() const override;

    void present(const Surface &frame) override;
    void clear() override;

private:
    MatrixDisplay(std::unique_ptr<rgb_matrix::RGBMatrix> matrix, rgb_matrix::FrameCanvas *back);

    std::unique_ptr<rgb_matrix::RGBMatrix> matrix_;
    rgb_matrix::FrameCanvas *back_; // owned by matrix_
};

} // namespace matrixos
