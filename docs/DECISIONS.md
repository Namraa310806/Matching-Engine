# Design Decisions

## Project Overview

This document records architectural and technical decisions for the Matching Engine project.

## Technology Stack

### C++20
- **Decision**: Use C++20 as the language standard
- **Rationale**: Modern C++ features (concepts, ranges, coroutines, improved constexpr) provide better expressiveness and performance for systems programming
- **Trade-offs**: Requires modern compiler support (GCC 10+, Clang 12+, MSVC 19.28+)

### CMake >= 3.20
- **Decision**: Use CMake 3.20 or higher for build system
- **Rationale**: Modern CMake features, better support for C++20, improved FetchContent module
- **Trade-offs**: Requires relatively recent CMake installation

### GoogleTest
- **Decision**: Use GoogleTest for unit testing
- **Rationale**: Industry standard, rich assertion library, good CMake integration, active maintenance
- **Trade-offs**: External dependency (managed via FetchContent)

### Google Benchmark
- **Decision**: Use Google Benchmark for performance testing
- **Rationale**: Industry standard, microbenchmarking support, statistical analysis, good CMake integration
- **Trade-offs**: External dependency (managed via FetchContent)

## Build Configuration

### Debug/Sanitizer Build
- **Flags**: `-O2 -Wall -Wextra -Wpedantic -fsanitize=address,undefined`
- **Rationale**: 
  - `-O2`: Optimized but still debuggable
  - `-Wall -Wextra -Wpedantic`: Maximum compiler warnings
  - `-fsanitize=address,undefined`: Memory safety and undefined behavior detection
- **Use Case**: Development and testing

### Release Benchmark Build
- **Flags**: `-O3 -march=native -DNDEBUG`
- **Rationale**:
  - `-O3`: Maximum optimization
  - `-march=native`: CPU-specific optimizations
  - `-DNDEBUG`: Disable assertions for benchmark accuracy
- **Use Case**: Performance benchmarking

## Project Structure

```
matching-engine/
├── CMakeLists.txt          # Main build configuration
├── include/engine/         # Public headers
├── src/                    # Implementation files
├── tests/                  # Unit tests
├── benchmarks/             # Performance benchmarks
├── tools/                  # Utility scripts and tools
└── docs/                   # Documentation
```

### Rationale
- **include/engine/**: Public API separation, clean interface
- **src/**: Implementation details hidden from users
- **tests/**: Separate test directory for organization
- **benchmarks/**: Separate benchmarks from tests
- **tools/**: Development and deployment utilities
- **docs/**: Design documentation and decisions

## Out-of-Source Build
- **Decision**: Enforce out-of-source builds
- **Rationale**: Keeps source tree clean, allows multiple build configurations
- **Implementation**: CMake builds should be run in separate build directories

## Static Library
- **Decision**: Create `engine_lib` as a static library
- **Rationale**: Simpler deployment, easier linking, no runtime dependencies
- **Trade-offs**: Larger binaries if used by multiple executables

## Testing Strategy
- **Decision**: Enable CTest integration with GoogleTest
- **Rationale**: Standard CMake testing infrastructure, easy CI integration
- **Implementation**: `gtest_discover_tests()` for automatic test discovery

## Future Considerations

### Data Structures
- Limit order book implementation (to be designed)
- Order matching algorithms (to be designed)
- Price/time priority queues (to be designed)

### Performance
- Lock-free data structures (potential)
- Memory pool allocation (potential)
- Cache-friendly data layout (potential)

### Concurrency
- Thread-safe order book (potential)
- Lock-free or fine-grained locking (to be decided)

## Non-Goals
- No networking components
- No trading APIs
- No GUI
- No database persistence
- No web server
- No external market data integration

## Version History
- v0.1.0 (2026-09-14): Initial project foundation
