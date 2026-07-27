// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "hal/pi/encoder_input.h"

#include "os/log.h"

#include <linux/gpio.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>

namespace matrixos
{
namespace
{

constexpr std::size_t kLineCount = 4;

/// Plain debounce rather than a GestureRecognizer: the home button has no hold or
/// double-press meaning (ADR-0009).
constexpr auto kHomeDebounce = std::chrono::milliseconds(20);

} // namespace

std::unique_ptr<EncoderInput> EncoderInput::create()
{
    return create(Pins{});
}

std::unique_ptr<EncoderInput> EncoderInput::create(const Pins &pins, const char *chip)
{
    const int chip_fd = ::open(chip, O_RDONLY | O_CLOEXEC);
    if (chip_fd < 0)
    {
        logWarn("cannot open {}: {}", chip, std::strerror(errno));
        return nullptr;
    }

    gpio_v2_line_request request{};
    request.offsets[0] = pins.encoder_a;
    request.offsets[1] = pins.encoder_b;
    request.offsets[2] = pins.encoder_button;
    request.offsets[3] = pins.home_button;
    request.num_lines = kLineCount;
    std::snprintf(request.consumer, sizeof(request.consumer), "matrixos");

    // Both edges, because a quadrature signal carries information in each one. The
    // pull-up lets every switch simply short its line to ground.
    request.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING |
                           GPIO_V2_LINE_FLAG_EDGE_FALLING | GPIO_V2_LINE_FLAG_BIAS_PULL_UP;

    // Room for a brisk turn between two frames; an overflow would drop events.
    request.event_buffer_size = 64;

    if (::ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &request) < 0)
    {
        logWarn("cannot claim GPIO lines {}/{}/{}/{}: {}", pins.encoder_a, pins.encoder_b,
                pins.encoder_button, pins.home_button, std::strerror(errno));
        ::close(chip_fd);
        return nullptr;
    }

    ::close(chip_fd); // the line request outlives it

    const int request_fd = request.fd;

    // Start the decoder from the resting position rather than from a guess.
    bool a_high = true;
    bool b_high = true;
    gpio_v2_line_values values{};
    values.mask = (1U << kLineCount) - 1U;
    if (::ioctl(request_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) == 0)
    {
        a_high = (values.bits & 0b0001) != 0;
        b_high = (values.bits & 0b0010) != 0;
    }
    else
    {
        logWarn("cannot read initial GPIO levels: {}", std::strerror(errno));
    }

    // Non-blocking, so the render loop never waits on input.
    const int flags = ::fcntl(request_fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(request_fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        logWarn("cannot switch the GPIO descriptor to non-blocking: {}", std::strerror(errno));
        ::close(request_fd);
        return nullptr;
    }

    logInfo("encoder on GPIO {}/{}, button {}, home {}", pins.encoder_a, pins.encoder_b,
            pins.encoder_button, pins.home_button);

    return std::unique_ptr<EncoderInput>(new EncoderInput(request_fd, pins, a_high, b_high));
}

EncoderInput::EncoderInput(int requestFd, const Pins &pins, bool aHigh, bool bHigh)
    : request_fd_(requestFd), pins_(pins), a_high_(aHigh), b_high_(bHigh)
{
    decoder_.update(a_high_, b_high_);
}

EncoderInput::~EncoderInput()
{
    if (request_fd_ >= 0)
    {
        ::close(request_fd_);
    }
}

std::vector<InputEvent> EncoderInput::poll()
{
    std::vector<InputEvent> events;

    std::array<gpio_v2_line_event, 16> batch{};
    for (;;)
    {
        const ssize_t bytes = ::read(request_fd_, batch.data(), sizeof(batch));
        if (bytes <= 0)
        {
            break; // EAGAIN: nothing buffered
        }

        const std::size_t count = static_cast<std::size_t>(bytes) / sizeof(batch[0]);
        for (std::size_t i = 0; i < count; ++i)
        {
            handleEvent(batch[i], events);
        }

        if (count < batch.size())
        {
            break;
        }
    }

    // A hold must fire without any further edge arriving.
    const std::vector<InputEvent> held = button_.tick(InputClock::now());
    events.insert(events.end(), held.begin(), held.end());

    return events;
}

void EncoderInput::handleEvent(const ::gpio_v2_line_event &event, std::vector<InputEvent> &out)
{
    const bool high = event.id == GPIO_V2_LINE_EVENT_RISING_EDGE;
    const InputTime when{std::chrono::nanoseconds(event.timestamp_ns)};

    // With --verbose this answers the only question that matters while wiring:
    // does the signal arrive at all?
    logDebug("gpio {} -> {}", event.offset, high ? "high" : "low");

    if (event.offset == pins_.encoder_a || event.offset == pins_.encoder_b)
    {
        if (event.offset == pins_.encoder_a)
        {
            a_high_ = high;
        }
        else
        {
            b_high_ = high;
        }

        const int detents = decoder_.update(a_high_, b_high_);
        if (detents != 0)
        {
            logDebug("detent {:+d}", detents);
            out.push_back({InputType::Rotate, detents});
        }
        return;
    }

    if (event.offset == pins_.encoder_button)
    {
        // Pull-up: a low line means the switch is closed.
        const std::vector<InputEvent> produced = button_.onButtonChange(!high, when);
        out.insert(out.end(), produced.begin(), produced.end());
        return;
    }

    if (event.offset == pins_.home_button)
    {
        if (high)
        {
            return; // Home fires on the press, not the release
        }
        if (last_home_press_.has_value() && when - *last_home_press_ < kHomeDebounce)
        {
            return;
        }
        last_home_press_ = when;
        out.push_back({InputType::Home, 0});
    }
}

} // namespace matrixos
