cmake_minimum_required(VERSION 3.24)
project(MatrixOS VERSION 0.1.0 LANGUAGES C CXX)

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
)
target_include_directories(matrixos_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(matrixos_core PRIVATE matrixos_warnings)

# =============================================================================
# HAL backends
# =============================================================================

add_library(matrixos_hal_sim STATIC
    src/hal/sim/terminal_display.cpp
)
target_link_libraries(matrixos_hal_sim
    PUBLIC matrixos_core
    PRIVATE matrixos_warnings
)

if(MATRIXOS_TARGET_PI)
    add_library(matrixos_hal_matrix STATIC
        src/hal/matrix/matrix_display.cpp
    )
    target_link_libraries(matrixos_hal_matrix
        PUBLIC matrixos_core matrix
        PRIVATE matrixos_warnings
    )
endif()

# =============================================================================
# Executable
# =============================================================================

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE
    matrixos_core
    matrixos_hal_sim
    matrixos_warnings
    Threads::Threads
)

if(MATRIXOS_TARGET_PI)
    target_link_libraries(${PROJECT_NAME} PRIVATE matrixos_hal_matrix)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MATRIXOS_HAS_MATRIX)
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
    )
    target_link_libraries(matrixos_tests PRIVATE
        matrixos_core
        matrixos_warnings
        Catch2::Catch2WithMain
    )

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(matrixos_tests)
endif()
