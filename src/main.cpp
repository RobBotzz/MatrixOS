// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.
//
// Composition root: the only place that knows which backends exist.

#include "apps/plasma/plasma.h"
#include "apps/testpattern/test_pattern.h"
#include "gfx/surface.h"
#include "hal/display.h"
#include "hal/sim/keyboard_input.h"
#include "hal/sim/terminal_display.h"
#include "os/log.h"
#include "os/shell.h"

#ifdef MATRIXOS_HAS_PI_HARDWARE
#include "hal/pi/encoder_input.h"
#include "hal/pi/matrix_display.h"
#endif

#include <chrono>
#include <csignal>
#include <cstring>
#include <memory>
#include <thread>

namespace
{

// One 64x32 panel (C-7).
constexpr int kPanelWidth = 64;
constexpr int kPanelHeight = 32;

volatile std::sig_atomic_t g_interrupted = 0;

void onSignal(int)
{
    g_interrupted = 1;
}

bool hasFlag(int argc, char *argv[], const char *flag)
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], flag) == 0)
        {
            return true;
        }
    }
    return false;
}

/// Holds the diagnostic frame until asked to stop. No shell, no input.
///
/// The same app is reachable from the launcher; this mode exists so a panel can
/// be checked before anything else is known to work.
int runTestPattern(matrixos::Display &display)
{
    matrixos::TestPatternApp app;
    matrixos::Surface frame(display.width(), display.height());

    app.render(frame);
    display.present(frame);

    while (g_interrupted == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    display.clear();
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    [[maybe_unused]] const bool force_simulator = hasFlag(argc, argv, "--simulate");
    [[maybe_unused]] const bool force_keyboard = hasFlag(argc, argv, "--keyboard");
    const bool test_pattern = hasFlag(argc, argv, "--test-pattern");

    if (hasFlag(argc, argv, "--verbose"))
    {
        matrixos::logSetLevel(matrixos::LogLevel::Debug);
    }

    // Input is claimed before the display, and the order matters: the matrix
    // library drops privileges from root to 'daemon' once the panel is
    // initialised, after which /dev/gpiochip0 can no longer be opened. An
    // already-open descriptor keeps working, so claiming the lines first is enough.
    std::unique_ptr<matrixos::Input> input;

    if (!test_pattern)
    {
#ifdef MATRIXOS_HAS_PI_HARDWARE
        // --keyboard keeps the panel but takes input from stdin, which is how the
        // device can be driven over SSH.
        if (!force_keyboard)
        {
            input = matrixos::EncoderInput::create();
            if (!input)
            {
                matrixos::logWarn("encoder unavailable, falling back to the keyboard");
            }
        }
#endif
        if (!input)
        {
            input = std::make_unique<matrixos::KeyboardInput>();
        }
    }

    std::unique_ptr<matrixos::Display> display;

#ifdef MATRIXOS_HAS_PI_HARDWARE
    if (!force_simulator)
    {
        // Also consumes the library's own --led-* flags from argc/argv.
        display = matrixos::MatrixDisplay::createFromFlags(&argc, &argv);
        if (!display)
        {
            matrixos::logWarn("LED panel unavailable, falling back to the simulator");
        }
    }
#endif

    if (!display)
    {
        display = std::make_unique<matrixos::TerminalDisplay>(kPanelWidth, kPanelHeight);
        matrixos::logInfo("using the terminal simulator");
    }
    else
    {
        matrixos::logInfo("using the LED panel");
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    if (test_pattern)
    {
        return runTestPattern(*display);
    }

    matrixos::Shell shell(*display, *input);
    shell.add(std::make_unique<matrixos::PlasmaApp>());
    shell.add(std::make_unique<matrixos::TestPatternApp>());
    shell.run([] { return g_interrupted != 0; });

    return 0;
}
