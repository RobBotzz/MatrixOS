cmake_minimum_required(VERSION 3.24)
project(MatrixOS VERSION 0.1.0 LANGUAGES C CXX)

#Testinglibrary
include(CTest)
enable_testing()

set(SOURCE_FILES
    main.cpp
)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)