cmake_minimum_required(VERSION 3.20)
project(MatchingEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build configurations
set(CMAKE_CXX_FLAGS_DEBUG "-O2 -Wall -Wextra -Wpedantic -fsanitize=address,undefined")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

# Enable testing
enable_testing()

# Find GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Find Google Benchmark
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
)
FetchContent_MakeAvailable(benchmark)

# engine_lib static library
add_library(engine_lib STATIC
    src/engine.cpp
)

target_include_directories(engine_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# engine_tests executable
add_executable(engine_tests
    tests/test_main.cpp
)

target_link_libraries(engine_tests
    PRIVATE
        engine_lib
        gtest
        gtest_main
)

include(GoogleTest)
gtest_discover_tests(engine_tests)

# Trivial benchmark
add_executable(engine_benchmarks
    benchmarks/benchmark_main.cpp
)

target_link_libraries(engine_benchmarks
    PRIVATE
        engine_lib
        benchmark::benchmark
        benchmark::benchmark_main
)
