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

## Domain Model Design

### Price Representation
- **Decision**: Use `int64_t` for Price type instead of floating-point (double)
- **Rationale**: 
  - Floating-point arithmetic introduces rounding errors and comparison issues
  - Integer ticks provide exact arithmetic and deterministic behavior
  - Prices can be represented as integer ticks (e.g., cents, basis points)
  - Avoids precision problems in matching logic and trade calculations
- **Trade-offs**: Requires price scaling logic (e.g., $10.50 becomes 1050 cents)

### Timestamp Representation
- **Decision**: Use monotonic sequence numbers instead of wall-clock timestamps
- **Rationale**:
  - Sequence numbers provide strict ordering guarantees
  - No issues with clock synchronization or adjustments
  - Simpler to generate and compare
  - Deterministic ordering is critical for matching engine correctness
- **Trade-offs**: Cannot correlate with real-world time, but this is acceptable for internal ordering

### Domain Events Architecture
- **Decision**: Use simple structs for domain events (Trade, MarketDataEvent) instead of full publisher/subscriber system
- **Rationale**:
  - Phase 1 focuses on core domain model, not event distribution
  - Simple structs are sufficient to represent event data
  - Avoids premature optimization and complexity
  - Publisher/subscriber can be added later when needed
- **Trade-offs**: No built-in event dispatch mechanism, but this is intentional for the current phase

### Type Aliases
- **Decision**: Define explicit type aliases (OrderId, Price, Qty)
- **Rationale**:
  - Improves code readability and self-documentation
  - Makes type system more expressive
  - Easier to change underlying types if needed
  - Prevents accidental misuse of types

### Order Structure
- **Decision**: Order struct with id, side, price, quantity, filled, sequence
- **Rationale**:
  - Captures all essential order state
  - Filled quantity tracking enables partial fill support
  - Sequence number for ordering
  - Simple data structure with basic validation

### Validation Strategy
- **Decision**: Basic invariant validation in constructors
- **Rationale**:
  - Catches obvious errors early (e.g., zero quantity, filled > quantity)
  - Not over-engineered - keeps validation simple
  - Can be enhanced later if needed
- **Trade-offs**: Limited validation scope, but sufficient for current phase

## Limit Order Book Design (v0 - Phase 1.2)

### Data Structure Choice
- **Decision**: Use `std::map<Price, std::deque<Order>>` for limit order book
- **Rationale for std::map**:
  - Provides O(log n) insertion and lookup
  - Maintains keys in sorted order automatically
  - Simple, well-understood standard library container
  - No custom memory management required
  - Easy to reason about and debug
  - Sufficient for correctness baseline before optimization
- **Rationale for std::deque within price levels**:
  - Provides O(1) insertion at both ends
  - Maintains FIFO ordering naturally
  - No pointer chasing or complex allocation patterns
  - Good cache locality for sequential access
  - Simple to implement and verify
- **Trade-offs**:
  - Not optimal for high-frequency trading (will be optimized in later phases)
  - Logarithmic time complexity instead of constant time
  - Additional memory overhead from tree structure
  - This is intentional - v0 is a correctness baseline, not a performance target

### Price Level Ordering
- **Buy side**: `std::map<Price, std::deque<Order>, std::greater<Price>>`
  - Descending order: highest price first (best bid at begin())
  - Best bid retrieval: O(1) by accessing bids_.begin()->first
- **Sell side**: `std::map<Price, std::deque<Order>, std::less<Price>>`
  - Ascending order: lowest price first (best ask at begin())
  - Best ask retrieval: O(1) by accessing asks_.begin()->first
- **Rationale**: Direct access to best prices without additional computation

### Complexity Analysis
- **Insertion (add_limit_order)**: O(log n) where n is number of price levels
  - Map lookup/insertion: O(log n)
  - Deque push_back: O(1) amortized
- **Best bid/ask lookup**: O(1)
  - Direct access to first element of map
- **Order count queries**: O(n) where n is number of price levels
  - Must iterate through all price levels to count orders
- **Price level inspection**: O(1) for map lookup + O(k) for copying k orders
- **Get all orders**: O(n + m) where n is price levels, m is total orders

### Why This is a Baseline
- **Purpose**: Establish correctness before optimization
- **No matching logic**: Orders simply rest on the book
- **No concurrency**: Single-threaded, no locks
- **No custom allocators**: Standard library memory management
- **No networking**: Pure in-memory data structure
- **No order ID indexing**: Orders accessed only by price/time priority
- **Future phases will optimize**: This design validates correctness before adding complexity

### Order Priority
- **FIFO at same price**: Orders with same price are stored in deque in insertion order
- **Sequence number**: Monotonic sequence number ensures strict ordering
- **Preservation**: Order objects are copied into the book, preserving all fields
- **No silent modification**: Caller's Order object is not modified

### API Design
- **add_limit_order**: Insert order without matching
- **best_bid/best_ask**: Query best prices (returns std::nullopt if empty)
- **buy_side_empty/sell_side_empty/empty**: Query emptiness
- **get_orders_at_bid_price/get_orders_at_ask_price**: Inspect specific price levels
- **buy_order_count/sell_order_count**: Count orders
- **buy_price_level_count/sell_price_level_count**: Count price levels
- **get_all_buy_orders/get_all_sell_orders**: Full book inspection for testing

## Version History
- v0.1.0 (2026-09-14): Initial project foundation
- v0.1.1 (2026-09-14): Core domain model implementation
- v0.1.2 (2026-09-16): Limit order book v0 (correctness baseline)
