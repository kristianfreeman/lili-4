cmake_minimum_required(VERSION 3.25)

project(LILI8 VERSION 0.1.0 LANGUAGES C CXX)

option(LILI_BUILD_PLUGIN "Build the JUCE plugin (AU/VST3/Standalone)" ON)
option(LILI_BUILD_TESTS "Build the engine tests and render tool" ON)
option(LILI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(LILI_JUCE_DIR "" CACHE PATH "Local JUCE checkout (fetched from GitHub when empty)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

add_subdirectory(dsp)

if(LILI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(LILI_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()
