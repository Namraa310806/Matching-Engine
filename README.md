# Matching Engine

A high-performance limit order book and matching engine implemented in C++20 as a systems-learning project.

## Project Overview

This project aims to build a production-grade matching engine from scratch, focusing on:
- High-performance order book data structures
- Efficient order matching algorithms
- Low-latency execution
- Memory-efficient implementations
- Multi-instrument support

## Implementation Versions

The project includes multiple implementation versions for performance comparison:

- **V0**: Reference implementation using `std::map` + `std::deque`
- **V1**: Cache-friendly implementation using sorted `std::vector` + intrusive linked lists
- **V1 + Object Pool**: V1 with object pooling for order node allocation (Phase 4 optimized version)
- **Multi-Instrument Engine**: Multi-instrument orchestration layer using V1 + Object Pool (Phase 5A)

## Requirements

- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 19.28+)
- CMake >= 3.20
- Git (for FetchContent dependencies)

## Project Structure

```
matching-engine/
├── CMakeLists.txt          # Main build configuration
├── include/engine/         # Public headers
├── src/                    # Implementation files
├── tests/                  # Unit tests (GoogleTest)
├── benchmarks/             # Performance benchmarks (Google Benchmark)
├── tools/                  # Utility scripts and tools
└── docs/                   # Documentation (including DECISIONS.md)
```

## Building

The project uses out-of-source builds. Always build in a separate directory.

### Debug/Sanitizer Build (Recommended for Development)

```bash
# Create build directory
mkdir build-debug
cd build-debug

# Configure with Debug flags (includes sanitizers)
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Build
cmake --build .

# Run tests
ctest --verbose
```

The Debug build uses:
- `-O2` optimization
- `-Wall -Wextra -Wpedantic` warnings
- `-fsanitize=address,undefined` sanitizers

### Release Benchmark Build

```bash
# Create build directory
mkdir build-release
cd build-release

# Configure with Release flags
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build
cmake --build .
```

The Release build uses:
- `-O3` optimization
- `-march=native` CPU-specific optimizations
- `-DNDEBUG` disable assertions

## Running Tests

After building, run the test suite:

```bash
# From build directory
ctest --verbose

# Or run the test executable directly
./tests/engine_tests  # Linux/WSL2
# or
.\tests\Debug\engine_tests.exe  # Windows
```

## Running Benchmarks

After building in Release mode, run benchmarks:

```bash
# From build-release directory
./benchmarks/engine_benchmarks  # Linux/WSL2
# or
.\benchmarks\Release\engine_benchmarks.exe  # Windows
```

Benchmark options:
- `--benchmark_filter=<regex>`: Filter benchmarks
- `--benchmark_repetitions=<N>`: Number of repetitions
- `--benchmark_min_time=<N>`: Minimum time per benchmark
- `--help`: Show all options

## Dependencies

Dependencies are automatically fetched via CMake FetchContent:
- **GoogleTest** v1.14.0: Testing framework
- **Google Benchmark** v1.8.3: Benchmarking framework

## Development Workflow

A local CI-equivalent script is provided in `tools/ci.sh` (Linux/WSL2) or `tools/ci.ps1` (Windows):

```bash
# Linux/WSL2
./tools/ci.sh

# Windows PowerShell
.\tools\ci.ps1
```

This script:
1. Creates a clean build directory
2. Configures with Debug/sanitizer flags
3. Builds the project
4. Runs all tests via CTest

## Documentation

- `docs/DECISIONS.md`: Architectural and technical decisions
- This README: Build and usage instructions

## Non-Goals

This project intentionally does NOT include:
- Networking components
- Trading APIs
- GUI
- Database persistence
- Web server
- External market data integration

Focus is on the core matching engine algorithms and data structures.

## License

[To be determined]

## Contributing

[To be determined]
