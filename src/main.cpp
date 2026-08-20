// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.
//
// Composition root: the only place that knows which backends exist.

#include "apps/clock/clock_app.h"
#include "apps/morse/morse.h"
#include "apps/plasma/plasma.h"
#include "apps/pomodoro/pomodoro.h"
#include "apps/settings/settings_app.h"
#include "apps/setup/setup_app.h"
#include "apps/snake/snake.h"
#include "apps/testpattern/test_pattern.h"
#include "gfx/surface.h"
#include "hal/display.h"
#include "hal/sim/keyboard_input.h"
#include "hal/sim/terminal_display.h"
#include "net/fake_wifi.h"
#include "net/http_server.h"
#include "net/nmcli_wifi.h"
#include "net/portal.h"
#include "os/clock.h"
#include "os/identity.h"
#include "os/log.h"
#include "os/provisioning.h"
#include "os/shell.h"
#include "os/state.h"
#include "os/version.h"

#ifdef MATRIXOS_HAS_PI_HARDWARE
#include "hal/pi/encoder_input.h"
#include "hal/pi/matrix_display.h"
#endif

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
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

/// The web server's port. 80 is what a phone assumes when it opens a captive
/// portal, and binding it needs root — which is why the socket is claimed before
/// the panel drops privileges.
constexpr int kDefaultHttpPort = 80;

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

/// Reads `--flag value`. Absent or malformed means the fallback.
int intOption(int argc, char *argv[], const char *flag, int fallback)
{
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (std::strcmp(argv[i], flag) == 0)
        {
            return std::atoi(argv[i + 1]);
        }
    }
    return fallback;
}

const char *environment(const char *name)
{
    const char *value = std::getenv(name);
    return value != nullptr && value[0] != '\0' ? value : nullptr;
}

/// Where everything the device remembers lives (FR-39, ADR-0011).
std::filesystem::path stateRoot()
{
    if (const char *override_path = environment("MATRIXOS_STATE_DIR"))
    {
        return override_path;
    }

#ifdef MATRIXOS_HAS_PI_HARDWARE
    return "/var/lib/matrixos";
#else
    if (const char *xdg = environment("XDG_STATE_HOME"))
    {
        return std::filesystem::path(xdg) / "matrixos";
    }
    if (const char *home = environment("HOME"))
    {
        return std::filesystem::path(home) / ".local" / "state" / "matrixos";
    }
    return std::filesystem::temp_directory_path() / "matrixos";
#endif
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

    // Everything that needs root is claimed before the display, and the order
    // matters: the matrix library drops privileges from root to 'daemon' once
    // the panel is initialised, after which neither /dev/gpiochip0 nor port 80
    // can be opened. An already-open descriptor keeps working, so claiming both
    // first is enough.
    matrixos::HttpServer server(intOption(argc, argv, "--port", kDefaultHttpPort));
    if (!test_pattern && !server.claimPort())
    {
        matrixos::logWarn("no web interface: the port is taken or needs root");
    }

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

    // After the display: the matrix library drops privileges to 'daemon' while
    // creating the panel, and every state write happens after that point.
    matrixos::StateStore store(stateRoot());

    // Who this unit is, derived from the CPU serial so a cloned image does not
    // produce two devices with one name (FR-32).
    const matrixos::Identity identity = matrixos::deviceIdentity();
    matrixos::logInfo("this is {} ({}), MatrixOS {} [{}]", identity.hostname, identity.access_point,
                      matrixos::kVersion, matrixos::kBuildCommit);

    // --fake-wifi drives the whole provisioning flow from variables, which is
    // how the setup screens and the portal are developed without a device.
    matrixos::NoWifi no_wifi;
    matrixos::FakeWifi fake_wifi;
    matrixos::WifiControl *wifi = &no_wifi;

#ifdef MATRIXOS_HAS_PI_HARDWARE
    matrixos::NmcliWifi nmcli;
    wifi = &nmcli;
#endif

    if (hasFlag(argc, argv, "--fake-wifi"))
    {
        wifi = &fake_wifi;
        matrixos::logWarn("using a simulated radio (--fake-wifi)");
    }

    matrixos::Provisioning provisioning(*wifi, store);
    matrixos::Portal portal(provisioning, identity, matrixos::kVersion, matrixos::kBuildCommit);
    portal.install(server);
    server.start();

    provisioning.begin();

    matrixos::SystemTimeProvider time;

    matrixos::Shell shell(*display, *input, store);
    shell.add(std::make_unique<matrixos::ClockApp>(time));
    shell.add(std::make_unique<matrixos::MorseApp>(store, std::random_device{}()));
    shell.add(std::make_unique<matrixos::PlasmaApp>());
    shell.add(std::make_unique<matrixos::PomodoroApp>());
    shell.add(std::make_unique<matrixos::SnakeApp>(store, std::random_device{}()));
    shell.add(std::make_unique<matrixos::TestPatternApp>());

    const std::size_t setup_app =
        shell.add(std::make_unique<matrixos::SetupApp>(provisioning, identity));

    // Last, so its list of startup choices is complete.
    shell.add(std::make_unique<matrixos::SettingsApp>(store, shell.appNames()));

    shell.useTimeProvider(time);
    shell.superviseSetup(provisioning, setup_app);

    shell.run([] { return g_interrupted != 0; });

    server.stop();
    return 0;
}
