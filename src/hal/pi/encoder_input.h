// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "hal/gestures.h"
#include "hal/input.h"
#include "hal/quadrature.h"

#include <memory>
#include <optional>

// Forward-declared so the kernel header stays out of this file's users.
struct gpio_v2_line_event;

namespace matrixos
{

/// Reads the encoder and the home button from the kernel's GPIO character device,
/// using edge events rather than polled levels (Q-4): the kernel timestamps and
/// buffers every transition, so one read per frame cannot lose a detent.
///
/// Decoding and press timing live in QuadratureDecoder and GestureRecognizer; what
/// remains here is the part that cannot be tested without hardware.
class EncoderInput : public Input
{
public:
    /// Pins resolved in Q-1: free with `regular` mapping and a single chain.
    struct Pins
    {
        unsigned encoder_a = 5;
        unsigned encoder_b = 6;
        unsigned encoder_button = 13;
        unsigned home_button = 19;
    };

    /// Returns nullptr if the lines cannot be claimed, so the caller can fall back
    /// to the keyboard.
    ///
    /// Two overloads because GCC cannot form `Pins{}` as a default argument before
    /// the enclosing class is complete.
    static std::unique_ptr<EncoderInput> create();
    static std::unique_ptr<EncoderInput> create(const Pins &pins,
                                                const char *chip = "/dev/gpiochip0");

    ~EncoderInput() override;

    std::vector<InputEvent> poll() override;

private:
    EncoderInput(int requestFd, const Pins &pins, bool aHigh, bool bHigh);

    void handleEvent(const ::gpio_v2_line_event &event, std::vector<InputEvent> &out);

    int request_fd_;
    Pins pins_;

    QuadratureDecoder decoder_;
    GestureRecognizer button_;

    bool a_high_;
    bool b_high_;
    std::optional<InputTime> last_home_press_;
};

} // namespace matrixos
