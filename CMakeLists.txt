cmake_minimum_required(VERSION 3.24)
project(MatrixOS VERSION 0.1.0 LANGUAGES C CXX)

#Executable is put in subdirectory bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# =============================================================================
# rpi-rgb-led-matrix
# =============================================================================

set(MATRIX_DIR ${CMAKE_SOURCE_DIR}/external/rpi-rgb-led-matrix)
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
    #Add additional files here
)

target_include_directories(matrix PUBLIC ${MATRIX_DIR}/include)

# =============================================================================
# Build
# =============================================================================

set(SOURCE_FILES
    main.cpp
)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    matrix
)