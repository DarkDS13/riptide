cmake_minimum_required(VERSION 3.20)

project(riptide
    VERSION 1.0.0
    DESCRIPTION "Parallel gzip decompression with log-depth window resolution"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RIPTIDE_BUILD_TESTS "Build the test suite" OFF)
option(RIPTIDE_BUILD_TOOLS "Build the command-line tool and benchmark driver" ON)
option(RIPTIDE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(RIPTIDE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

find_package(Threads REQUIRED)
# zlib is only used as an independent reference: by the tests to produce and
# check gzip data, and by the benchmark driver as a single-threaded baseline.
find_package(ZLIB REQUIRED)

set(RIPTIDE_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
if(RIPTIDE_WARNINGS_AS_ERRORS)
    list(APPEND RIPTIDE_WARNINGS -Werror)
endif()

add_library(riptide STATIC
    src/riptide/gzip/crc32.cpp
    src/riptide/gzip/gzip_format.cpp
    src/riptide/huffman/huffman_decoder.cpp
    src/riptide/inflate/block_finder.cpp
    src/riptide/inflate/chunk_decoder.cpp
    src/riptide/inflate/dynamic_header.cpp
    src/riptide/inflate/expansion_estimator.cpp
    src/riptide/io/mapped_file.cpp
    src/riptide/parallel/thread_pool.cpp
    src/riptide/pipeline/parallel_decompressor.cpp
    src/riptide/pipeline/partitioner.cpp
    src/riptide/pipeline/sequential_decompressor.cpp
    src/riptide/pipeline/statistics.cpp
    src/riptide/window/window.cpp
    src/riptide/window/window_resolver.cpp
)
target_include_directories(riptide PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(riptide PRIVATE ${RIPTIDE_WARNINGS})
target_link_libraries(riptide PUBLIC Threads::Threads)

if(RIPTIDE_SANITIZE)
    target_compile_options(riptide PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(riptide PUBLIC -fsanitize=address,undefined)
endif()

if(RIPTIDE_BUILD_TOOLS)
    add_executable(riptide_cli tools/riptide_cli.cpp tools/cli_options.cpp)
    set_target_properties(riptide_cli PROPERTIES OUTPUT_NAME riptide)
    target_compile_options(riptide_cli PRIVATE ${RIPTIDE_WARNINGS})
    target_link_libraries(riptide_cli PRIVATE riptide)

endif()

if(RIPTIDE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
