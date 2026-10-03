cmake_minimum_required(VERSION 3.20)
project(MatchingEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build configurations
set(CMAKE_CXX_FLAGS_DEBUG "-O2 -Wall -Wextra -Wpedantic -fsanitize=address,undefined" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_TSAN "-O2 -Wall -Wextra -Wpedantic -fsanitize=thread" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG" CACHE STRING "" FORCE)

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
# Disable Werror for benchmark to avoid GCC 13 std::function regex false positive
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

# engine_lib static library
add_library(engine_lib STATIC
    src/engine.cpp
    src/types.cpp
    src/engine_v1.cpp
    src/engine_v1_pool.cpp
    src/multi_instrument_engine.cpp
    src/mutex_multi_instrument_engine.cpp
)

target_include_directories(engine_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Link threads library for std::mutex/std::thread support
find_package(Threads REQUIRED)
target_link_libraries(engine_lib
    PUBLIC
        Threads::Threads
)

# engine_tests executable
add_executable(engine_tests
    tests/test_main.cpp
    tests/domain_test.cpp
    tests/orderbook_test.cpp
    tests/matching_test.cpp
    tests/replay_test.cpp
    tools/order_gen/order_generator.cpp
    tools/engine_cli/engine_cli.cpp
)

target_include_directories(engine_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/order_gen
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/engine_cli
)

target_link_libraries(engine_tests
    PRIVATE
        engine_lib
        gtest
        gtest_main
)

target_compile_definitions(engine_tests
    PRIVATE
        TESTING
)

include(GoogleTest)
gtest_discover_tests(engine_tests)

# engine_tests_v1 executable (v1 correctness tests)
add_executable(engine_tests_v1
    tests/test_main.cpp
    tests/domain_test.cpp
    tests/orderbook_test_v1.cpp
    tests/matching_test_v1.cpp
    tests/replay_test.cpp
    tools/order_gen/order_generator.cpp
    tools/engine_cli/engine_cli.cpp
)

target_include_directories(engine_tests_v1
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/order_gen
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/engine_cli
)

target_link_libraries(engine_tests_v1
    PRIVATE
        engine_lib
        gtest
        gtest_main
)

target_compile_definitions(engine_tests_v1
    PRIVATE
        TESTING
)

gtest_discover_tests(engine_tests_v1)

# engine_tests_v1_pool executable (pool correctness tests)
add_executable(engine_tests_v1_pool
    tests/test_main.cpp
    tests/domain_test.cpp
    tests/orderbook_test_v1_pool.cpp
    tests/matching_test_v1_pool.cpp
    tests/replay_test.cpp
    tools/order_gen/order_generator.cpp
    tools/engine_cli/engine_cli.cpp
)

target_include_directories(engine_tests_v1_pool
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/order_gen
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/engine_cli
)

target_link_libraries(engine_tests_v1_pool
    PRIVATE
        engine_lib
        gtest
        gtest_main
)

target_compile_definitions(engine_tests_v1_pool
    PRIVATE
        TESTING
)

gtest_discover_tests(engine_tests_v1_pool)

# engine_tests_multi_instrument executable (multi-instrument correctness tests)
add_executable(engine_tests_multi_instrument
    tests/test_main.cpp
    tests/domain_test.cpp
    tests/multi_instrument_test.cpp
    tools/order_gen/order_generator.cpp
    tools/engine_cli/engine_cli.cpp
)

target_include_directories(engine_tests_multi_instrument
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/order_gen
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/engine_cli
)

target_link_libraries(engine_tests_multi_instrument
    PRIVATE
        engine_lib
        gtest
        gtest_main
)

target_compile_definitions(engine_tests_multi_instrument
    PRIVATE
        TESTING
)

gtest_discover_tests(engine_tests_multi_instrument)

# engine_tests_concurrency executable (mutex concurrency tests)
add_executable(engine_tests_concurrency
    tests/test_main.cpp
    tests/domain_test.cpp
    tests/concurrency_test.cpp
    tools/order_gen/order_generator.cpp
    tools/engine_cli/engine_cli.cpp
)

target_include_directories(engine_tests_concurrency
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/order_gen
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/engine_cli
)

target_link_libraries(engine_tests_concurrency
    PRIVATE
        engine_lib
        gtest
        gtest_main
)

target_compile_definitions(engine_tests_concurrency
    PRIVATE
        TESTING
)

gtest_discover_tests(engine_tests_concurrency)

# engine_tests_spsc_queue executable (SPSC queue tests)
add_executable(engine_tests_spsc_queue
    tests/test_main.cpp
    tests/spsc_queue_test.cpp
)

target_link_libraries(engine_tests_spsc_queue
    PRIVATE
        engine_lib
        gtest
        gtest_main
)

target_compile_definitions(engine_tests_spsc_queue
    PRIVATE
        TESTING
)

gtest_discover_tests(engine_tests_spsc_queue)

# engine_tests_spsc_integration executable (SPSC integration tests)
add_executable(engine_tests_spsc_integration
    tests/test_main.cpp
    tests/domain_test.cpp
    tests/spsc_integration_test.cpp
    tools/order_gen/order_generator.cpp
    tools/engine_cli/engine_cli.cpp
)

target_include_directories(engine_tests_spsc_integration
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/order_gen
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/engine_cli
)

target_link_libraries(engine_tests_spsc_integration
    PRIVATE
        engine_lib
        gtest
        gtest_main
)

target_compile_definitions(engine_tests_spsc_integration
    PRIVATE
        TESTING
)

gtest_discover_tests(engine_tests_spsc_integration)

# Benchmark suite (v0)
add_executable(engine_benchmarks
    benchmarks/benchmark_main.cpp
    tools/order_gen/order_generator.cpp
)

target_include_directories(engine_benchmarks
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/order_gen
)

target_link_libraries(engine_benchmarks
    PRIVATE
        engine_lib
        benchmark::benchmark
        benchmark::benchmark_main
)

# Benchmark suite (v1 comparison)
add_executable(engine_benchmarks_v1
    benchmarks/benchmark_v1.cpp
    tools/order_gen/order_generator.cpp
)

target_include_directories(engine_benchmarks_v1
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/order_gen
)

target_link_libraries(engine_benchmarks_v1
    PRIVATE
        engine_lib
        benchmark::benchmark
        benchmark::benchmark_main
)

# Benchmark suite (object pool comparison)
add_executable(engine_benchmarks_pool
    benchmarks/benchmark_pool.cpp
    tools/order_gen/order_generator.cpp
)

target_include_directories(engine_benchmarks_pool
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/order_gen
)

target_link_libraries(engine_benchmarks_pool
    PRIVATE
        engine_lib
        benchmark::benchmark
        benchmark::benchmark_main
)

# Benchmark suite (multi-instrument comparison)
add_executable(engine_benchmarks_multi
    benchmarks/benchmark_multi.cpp
    tools/order_gen/order_generator.cpp
)

target_include_directories(engine_benchmarks_multi
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/order_gen
)

target_link_libraries(engine_benchmarks_multi
    PRIVATE
        engine_lib
        benchmark::benchmark
        benchmark::benchmark_main
)

# Benchmark suite (mutex concurrency baseline)
add_executable(engine_benchmarks_concurrency
    benchmarks/benchmark_concurrency.cpp
    tools/order_gen/order_generator.cpp
)

target_include_directories(engine_benchmarks_concurrency
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/order_gen
)

target_link_libraries(engine_benchmarks_concurrency
    PRIVATE
        engine_lib
        benchmark::benchmark
        benchmark::benchmark_main
)

# Benchmark suite (SPSC ingestion comparison)
add_executable(engine_benchmarks_spsc
    benchmarks/benchmark_spsc.cpp
    tools/order_gen/order_generator.cpp
)

target_include_directories(engine_benchmarks_spsc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/order_gen
)

target_link_libraries(engine_benchmarks_spsc
    PRIVATE
        engine_lib
        benchmark::benchmark
        benchmark::benchmark_main
)

# order_gen tool
add_executable(order_gen
    tools/order_gen/main.cpp
    tools/order_gen/order_generator.cpp
)

target_link_libraries(order_gen
    PRIVATE
        engine_lib
)

target_include_directories(order_gen
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/order_gen
)

# engine_cli tool
add_executable(engine_cli
    tools/engine_cli/main.cpp
    tools/engine_cli/engine_cli.cpp
    tools/order_gen/order_generator.cpp
)

target_link_libraries(engine_cli
    PRIVATE
        engine_lib
)

target_include_directories(engine_cli
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/engine_cli
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/order_gen
)
