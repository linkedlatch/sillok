cmake_minimum_required(VERSION 3.28)
project(sillok VERSION 0.1.0 LANGUAGES C CXX)

option(SILLOK_WITH_MODELS "Build the model backends (whisper.cpp, NeMo-Speech.cpp)" ON)
option(SILLOK_BUILD_CLI "Build the sillok command-line app" ON)
option(SILLOK_BUILD_TESTS "Build the tests" ON)
option(SILLOK_VULKAN "Use the ggml Vulkan backend for GPU runs" OFF)
option(SILLOK_COREML "Run the Whisper encoder through Core ML (Apple)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)  # no C++20 modules in use
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Only the C ABI is exported from the products (cmake/library.cmake).
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/warnings.cmake)
include(cmake/deps.cmake)

add_subdirectory(src/core)
add_subdirectory(src/capi)
if(SILLOK_WITH_MODELS)
    add_subdirectory(src/backends)
    include(cmake/library.cmake)
    if(SILLOK_BUILD_CLI)
        add_subdirectory(apps/cli)
    endif()
endif()

if(SILLOK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
