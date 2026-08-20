cmake_minimum_required(VERSION 3.24)
project(MatrixOS VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# =============================================================================
# Which target is this?
# =============================================================================
# Only aarch64 can drive the panel (ADR-0001). A host build gets the simulator
# backend instead, which is what makes development without hardware possible
# (ADR-0002). Detected rather than configured, so it cannot be forgotten.

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    set(MATRIXOS_TARGET_PI ON)
    set(MATRIXOS_TESTS_DEFAULT OFF)
else()
    set(MATRIXOS_TARGET_PI OFF)
    set(MATRIXOS_TESTS_DEFAULT ON)
endif()

option(MATRIXOS_BUILD_TESTS "Build the test suite (host builds only)" ${MATRIXOS_TESTS_DEFAULT})

message(STATUS "MatrixOS: LED panel backend .. ${MATRIXOS_TARGET_PI}")
message(STATUS "MatrixOS: test suite ......... ${MATRIXOS_BUILD_TESTS}")

find_package(Threads REQUIRED)

# =============================================================================
# Version, for the configuration page (FR-41)
# =============================================================================
# The commit is read at configure time, not at build time: a build-time hook
# would rerun on every compile for a string that changes once a day.

set(MATRIXOS_VERSION ${PROJECT_VERSION})
set(MATRIXOS_COMMIT "unknown")

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE MATRIXOS_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/os/version.h.in
    ${CMAKE_BINARY_DIR}/generated/os/version.h
    @ONLY
)

message(STATUS "MatrixOS: version ............ ${MATRIXOS_VERSION} (${MATRIXOS_COMMIT})")

# Warnings for our own code only — external/ is vendored and stays as it is.
add_library(matrixos_warnings INTERFACE)
target_compile_options(matrixos_warnings INTERFACE -Wall -Wextra -Wpedantic)

# =============================================================================
# rpi-rgb-led-matrix (Pi builds only)
# =============================================================================

if(MATRIXOS_TARGET_PI)
    set(MATRIX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/rpi-rgb-led-matrix)

    if(NOT EXISTS ${MATRIX_DIR}/include/led-matrix.h)
        message(FATAL_ERROR
            "external/rpi-rgb-led-matrix is empty. Run:\n"
            "  git submodule update --init --recursive")
    endif()

    add_library(matrix STATIC
        ${MATRIX_DIR}/lib/bdf-font.cc
        ${MATRIX_DIR}/lib/content-streamer-c.cc
        ${MATRIX_DIR}/lib/content-streamer.cc
        ${MATRIX_DIR}/lib/framebuffer.cc
        ${MATRIX_DIR}/lib/gpio.cc
        ${MATRIX_DIR}/lib/graphics.cc
        ${MATRIX_DIR}/lib/hardware-mapping.c
        ${MATRIX_DIR}/lib/led-matrix-c.cc
        ${MATRIX_DIR}/lib/led-matrix.cc
        ${MATRIX_DIR}/lib/multiplex-mappers.cc
        ${MATRIX_DIR}/lib/options-initialize.cc
        ${MATRIX_DIR}/lib/pixel-mapper.cc
        ${MATRIX_DIR}/lib/thread.cc
    )
    # SYSTEM: we compile the vendored sources but do not want to hear about
    # warnings in headers we are not going to fix.
    target_include_directories(matrix SYSTEM PUBLIC ${MATRIX_DIR}/include)
    target_link_libraries(matrix PUBLIC Threads::Threads)
endif()

# =============================================================================
# Portable core: shell, graphics, apps, HAL interfaces
# =============================================================================
# Note what is absent: this target does not link `matrix`, so it never sees that
# library's include path. A stray #include "led-matrix.h" in gfx/ or apps/ fails
# to compile — the layering from architecture.md is enforced by the linker rather
# than by review.

add_library(matrixos_core STATIC
    src/gfx/surface.cpp
    src/gfx/font.cpp
    src/hal/quadrature.cpp
    src/hal/gestures.cpp
    src/os/log.cpp
    src/os/shell.cpp
    src/os/launcher.cpp
    src/os/state.cpp
    src/os/provisioning.cpp
    src/os/identity.cpp
    src/os/clock.cpp
    src/apps/clock/clock_app.cpp
    src/apps/morse/morse.cpp
    src/apps/plasma/plasma.cpp
    src/apps/pomodoro/pomodoro.cpp
    src/apps/settings/settings_app.cpp
    src/apps/setup/setup_app.cpp
    src/apps/snake/snake.cpp
    src/apps/testpattern/test_pattern.cpp
)
target_include_directories(matrixos_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(matrixos_core
    PUBLIC Threads::Threads   # the shell sleeps between frames
    PRIVATE matrixos_warnings
)

# =============================================================================
# HAL backends
# =============================================================================

add_library(matrixos_hal_sim STATIC
    src/hal/sim/terminal_display.cpp
    src/hal/sim/keyboard_input.cpp
)
target_link_libraries(matrixos_hal_sim
    PUBLIC matrixos_core
    PRIVATE matrixos_warnings
)

if(MATRIXOS_TARGET_PI)
    # Both Pi-only backends: the LED panel and the encoder. The encoder needs no
    # third-party library — it talks to the kernel's GPIO character device, whose
    # header ships with the cross toolchain.
    add_library(matrixos_hal_pi STATIC
        src/hal/pi/matrix_display.cpp
        src/hal/pi/encoder_input.cpp
    )
    target_link_libraries(matrixos_hal_pi
        PUBLIC matrixos_core matrix
        PRIVATE matrixos_warnings
    )
endif()

# =============================================================================
# Platform networking (from v0.4)
# =============================================================================
# The setup portal, the configuration page and, from v0.7, the OAuth callback.
# Platform infrastructure rather than a detail of one app (ADR-0007 §9.1), and
# built for host and Pi alike so it can be tested without the device.

add_library(matrixos_net STATIC
    src/net/http_server.cpp
    src/net/nmcli_wifi.cpp
    src/net/portal.cpp
)
target_link_libraries(matrixos_net
    PUBLIC matrixos_core
    PRIVATE matrixos_warnings
)

# The configuration page is one 200 KB string literal in a generated header
# (ADR-0014). -Wpedantic would otherwise complain that it exceeds the length ISO
# C++ requires an implementation to support; the alternative encoding is a byte
# array six times the size and unreadable in a diff.
target_compile_options(matrixos_net PRIVATE -Wno-overlength-strings)

# =============================================================================
# Executable
# =============================================================================

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE
    matrixos_core
    matrixos_hal_sim
    matrixos_net
    matrixos_warnings
    Threads::Threads
)

if(MATRIXOS_TARGET_PI)
    target_link_libraries(${PROJECT_NAME} PRIVATE matrixos_hal_pi)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MATRIXOS_HAS_PI_HARDWARE)
endif()

# =============================================================================
# Tests (host only)
# =============================================================================

if(MATRIXOS_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.15.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    enable_testing()

    add_executable(matrixos_tests
        tests/test_surface.cpp
        tests/test_font.cpp
        tests/test_quadrature.cpp
        tests/test_gestures.cpp
        tests/test_shell.cpp
        tests/test_launcher.cpp
        tests/test_state.cpp
        tests/test_plasma.cpp
        tests/test_pomodoro.cpp
        tests/test_snake.cpp
        tests/test_morse.cpp
        tests/test_settings.cpp
        tests/test_http_server.cpp
        tests/test_provisioning.cpp
        tests/test_portal.cpp
        tests/test_identity.cpp
        tests/test_clock.cpp
        tests/test_setup_app.cpp
    )
    target_link_libraries(matrixos_tests PRIVATE
        matrixos_core
        matrixos_net
        matrixos_warnings
        Catch2::Catch2WithMain
    )

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(matrixos_tests)
endif()
