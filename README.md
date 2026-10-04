# Matching Engine

A high-performance limit order book and matching engine implemented in C++20 as a systems-learning project. This project explores data structure optimization, memory management, and concurrency architectures for financial order matching.

## Project Overview

This project implements a production-grade matching engine from scratch, progressing through multiple implementation phases:

- **Phase 1-3**: Core domain model, price-time matching, order cancellation, and performance baseline (V0)
- **Phase 4**: Cache-friendly data structures (V1) and object pool optimization (V1+Pool)
- **Phase 5**: Multi-instrument orchestration, mutex concurrency baseline, and SPSC lock-free ingestion
- **Phase 6**: Hot-path profiling and optimization
- **Phase 7**: Final benchmark verification and documentation

## Implementation Versions

The project includes multiple implementation versions for performance comparison:

- **V0**: Reference implementation using `std::map` + `std::deque` (correctness baseline)
- **V1**: Cache-friendly implementation using sorted `std::vector` + intrusive linked lists
- **V1 + Object Pool**: V1 with chunk-based object pooling for order node allocation
- **Multi-Instrument Engine**: Multi-instrument orchestration layer using V1 + Object Pool
- **Mutex Concurrency Baseline**: Global mutex-protected multi-instrument engine
- **SPSC Ingestion Architecture**: Lock-free single-producer/single-consumer queue for order ingestion

## Architecture

### Order Book Data Structures

#### V0 (Baseline)
- **Price levels**: `std::map<Price, std::deque<Order>>` for each side
- **Order indexing**: `std::unordered_map<OrderId, OrderLocation>` with deque index tracking
- **Complexity**: O(log n) insertion, O(k) cancellation (where k = orders at price level)
- **Trade-off**: Simple implementation but O(k) index update on cancellation causes severe degradation

#### V1 (Cache-Friendly)
- **Price levels**: `std::vector<PriceLevel>` (sorted, contiguous memory)
- **Order linkage**: Intrusive doubly-linked list within each price level
- **Order indexing**: `std::unordered_map<OrderId, OrderNode*>` (direct pointer)
- **Complexity**: O(log n) insertion, O(1) order-node removal
- **Benefit**: Eliminates O(k) index update cost, enabling 278x speedup on cancellation-heavy workloads

#### V1 + Object Pool
- **Allocation**: Chunk-based freelist with exponential growth (1K → 2K → 4K → ... → 64K)
- **Hit rate**: 99.99% on typical workloads
- **Benefit**: 22-49% throughput improvement with significantly lower variance

### Multi-Instrument Architecture

The multi-instrument engine routes orders to independent order books per instrument:

```
MultiInstrumentEngine
    ├── Instrument A → OrderBookV1Pool
    ├── Instrument B → OrderBookV1Pool
    └── Instrument C → OrderBookV1Pool
```

Each instrument maintains its own order book with:
- Independent bid/ask sides
- Local order book state
- Global order ID uniqueness across all instruments

### Concurrency Architectures

#### Mutex Baseline
- **Mechanism**: Global `std::mutex` protecting all engine state
- **API**: `MutexMultiInstrumentEngine` wraps single-threaded `MultiInstrumentEngine`
- **Performance**: 1 thread is 8% slower than single-threaded baseline; 2-4 threads are 3.5-4x slower due to contention
- **Use case**: Simple, measurable baseline for lock-free comparison

#### SPSC Ingestion
- **Mechanism**: Lock-free SPSC ring buffer (fixed capacity, power-of-two)
- **Architecture**: Producer thread → SPSC queue → Consumer thread (sole writer of order book state)
- **Memory ordering**: Acquire/release semantics for correct synchronization
- **Single-writer invariant**: Only the consumer thread directly mutates order-book state
- **Performance**: 0.51x vs mutex baseline at 100K orders (SPSC is slower in corrected benchmarks - see docs/SPSC_CORRECTNESS_AUDIT.md for details)

## Order Lifecycle

1. **Submission**: Order received via `submit_order()`
2. **Matching**: Incoming order matches against resting orders at best prices
3. **Resting**: Unfilled limit orders rest on book at their price level
4. **Cancellation**: Orders can be cancelled by ID while resting
5. **Events**: Trades and market data events generated for each state change

## Matching Algorithm

### Price-Time Priority
- **Price priority**: Better prices (higher bids, lower asks) filled before worse prices
- **Time priority**: At same price, older orders fill first (FIFO)
- **Execution price**: Always the resting order's price (price improvement for market orders)

### Matching Rules
- Buy limit order matches when `resting_ask_price <= incoming_buy_price`
- Sell limit order matches when `resting_bid_price >= incoming_sell_price`
- Market orders consume available liquidity and never rest on book
- Partial fills supported for both incoming and resting orders

## Memory Ordering Rationale (SPSC Queue)

The SPSC queue uses acquire/release memory ordering to establish correct happens-before relationships:

- **Producer load of read_index**: `memory_order_acquire` - synchronizes with consumer's release store
- **Producer store of write_index**: `memory_order_release` - publishes value before index update
- **Consumer load of write_index**: `memory_order_acquire` - synchronizes with producer's release store
- **Consumer store of read_index**: `memory_order_release` - announces consumption progress

This ensures the consumer sees fully constructed values and the producer sees announced consumption progress, without the overhead of `memory_order_seq_cst`.

## Testing Strategy

### Test Executables and Counts

| Executable | Test Count | Description |
|------------|------------|-------------|
| engine_tests (V0) | 122 | Baseline correctness tests |
| engine_tests_v1 | 122 | V1 correctness tests (shared suites) |
| engine_tests_v1_pool | 124 | V1+Pool correctness tests (shared suites + pool statistics) |
| engine_tests_multi_instrument | 38 | Multi-instrument routing tests |
| engine_tests_concurrency | 35 | Mutex concurrency tests |
| engine_tests_spsc_queue | 12 | SPSC queue correctness tests |
| engine_tests_spsc_integration | 37 | SPSC integration tests |

**Total**: 490 test-case executions across all test executables, including shared suites executed against multiple implementations.

### Sanitizer Verification

- **ASan/UBSan**: 406 executed tests across engine_tests, engine_tests_v1, engine_tests_v1_pool, engine_tests_multi_instrument
- **TSan**: 84 executed concurrency tests across engine_tests_concurrency, engine_tests_spsc_queue, engine_tests_spsc_integration

### Test Coverage

- Domain model validation (Order, Trade, MarketDataEvent)
- Order book operations (add, cancel, query)
- Matching logic (price-time priority, partial fills, market orders)
- Cancellation correctness (index consistency, edge cases)
- Multi-instrument isolation (no cross-instrument matching)
- Concurrency correctness (global ID uniqueness, no data races)
- SPSC queue correctness (wraparound, FIFO ordering, memory ordering)

## Benchmark Methodology

### Workloads

- **Add-Only**: Orders priced to not cross (no matching)
- **Add+Cancel**: 20% cancellation rate
- **Add+Match**: Orders priced to cross (maximizes matching)
- **Mixed**: 90% limit orders, 10% cancellations, realistic price distribution

### Measurement

- Throughput (orders/second)
- CPU time (nanoseconds/milliseconds)
- Percentiles (p50, p95, p99) from multiple repetitions
- Standard deviation and coefficient of variation

### Hardware Configuration

- **CPU**: 13th Gen Intel(R) Core(TM) i7-1355U
- **Cores**: 4 (2 cores, 2 threads per core)
- **Clock Speed**: 2611 MHz
- **L1 Data Cache**: 48 KiB (2 instances)
- **L1 Instruction Cache**: 32 KiB (2 instances)
- **L2 Cache**: 1280 KiB (2 instances)
- **L3 Cache**: 12288 KiB (1 instance)

### Software Configuration

- **Compiler**: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- **Build Type**: Release (-O3 -march=native -DNDEBUG)
- **C++ Standard**: C++20
- **Benchmark Library**: Google Benchmark v1.8.3

## Performance Results

### V0 vs V1 (100K orders, Release build)

| Workload | V0 Throughput | V1 Throughput | V1 Speedup |
|----------|---------------|---------------|------------|
| Add-Only | 4.95M ops/sec | 4.89M ops/sec | 0.99x |
| Add+Cancel | 13.7K ops/sec | 3.81M ops/sec | **278x** |
| Add+Match | 8.09M ops/sec | 6.64M ops/sec | 0.82x |
| Mixed | 26.7K ops/sec | 3.80M ops/sec | **142x** |

**Interpretation**: V1 eliminates the O(k) index update bottleneck, achieving dramatic speedup on cancellation-heavy workloads. V0 remains faster on matching-heavy workloads where orders don't rest on the book.

### Object Pool Optimization (100K orders, Release build)

| Workload | Baseline Throughput | Pool Throughput | Improvement |
|----------|---------------------|-----------------|-------------|
| Add-Only | 5.24M ops/sec | 7.81M ops/sec | **+49%** |
| Add+Cancel | 5.26M ops/sec | 7.26M ops/sec | **+38%** |
| Add+Match | 7.73M ops/sec | 9.44M ops/sec | **+22%** |
| Mixed | 5.47M ops/sec | 7.02M ops/sec | **+28%** |

**Pool statistics (Mixed workload)**: 99.99% hit rate, 6 chunks, 64K total capacity.

### Mutex vs SPSC (100K orders, Release build)

**CRITICAL CORRECTION**: Previous SPSC benchmark results (25.22M ops/sec, 8.0x speedup) were INVALID due to a critical bug: the benchmark silently dropped commands when the queue was full instead of applying backpressure. The corrected benchmark now ensures all submitted commands are processed and verifies submitted == processed.

**Corrected Results (2026-10-04 Comprehensive Benchmark Suite)**:

See `docs/SPSC_CORRECTNESS_AUDIT.md` (Part 6) for complete benchmark results including:
- Multiple workload sizes (1K, 4K, 32K, 100K orders)
- Multiple configurations (single instrument, multiple instruments, mixed activity)
- Queue capacity comparison (1024, 16384 slots)
- Detailed SPSC vs mutex comparison

**Summary**:
- At 1,000 orders: SPSC 2-3x faster than mutex (queue buffering advantage)
- At 4,096 orders: SPSC parity with mutex (0.84-0.95x ratio)
- At 32,768-100,000 orders: SPSC slower than mutex (0.67-0.85x ratio)
- Queue capacity 16384 shows 4.62x improvement at 4,096 orders but only 1.27x at 100,000 orders

**Command**: `./build-release/engine_benchmarks_spsc --benchmark_repetitions=5 --benchmark_counters_tabular=true`

**Interpretation**: With the corrected benchmark that properly applies backpressure and verifies all commands are processed, SPSC shows advantage at small workloads but disadvantage at large workloads. The architectural trade-off (async vs sync) has measurable costs. See `docs/SPSC_CORRECTNESS_AUDIT.md` for detailed analysis.

### Overall Progression (Mixed workload, 100K orders)

| Implementation | Throughput | vs Previous |
|----------------|------------|-------------|
| V0 baseline | 26.7K ops/sec | - |
| V1 cache-friendly | 3.80M ops/sec | **142x** |
| V1 + object pool | 7.02M ops/sec | 1.85x |
| Phase 6 optimized | 5.15M ops/sec | +15.3% over pool baseline |

## Known Limitations

- **Single-threaded matching**: The matching engine itself is single-threaded (intentional for correctness)
- **Fixed SPSC queue capacity**: Queue capacity is fixed at compile time; producer must handle backpressure
- **Asynchronous SPSC API**: Producer does not receive immediate trades/events from SPSC engine
- **No networking**: Focus is on core matching algorithms, not network I/O
- **No persistence**: Order book state is not persisted to disk
- **No GUI**: Command-line interface only

## Design Decisions

Key architectural decisions are documented in `docs/DECISIONS.md`:

- C++20 for modern language features
- Integer price representation (avoiding floating-point precision issues)
- Monotonic sequence numbers for event ordering
- Out-of-source builds with CMake
- GoogleTest and Google Benchmark for testing and benchmarking
- V0 preservation as correctness baseline

## Build Instructions

### Requirements

- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 19.28+)
- CMake >= 3.20
- Git (for FetchContent dependencies)

### Debug/Sanitizer Build (Recommended for Development)

```bash
mkdir build-debug
cd build-debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
ctest --verbose
```

The Debug build uses:
- `-O2` optimization
- `-Wall -Wextra -Wpedantic` warnings
- `-fsanitize=address,undefined` sanitizers

### Release Benchmark Build

```bash
mkdir build-release
cd build-release
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

The Release build uses:
- `-O3` optimization
- `-march=native` CPU-specific optimizations
- `-DNDEBUG` disable assertions

### ThreadSanitizer Build (for concurrency testing)

```bash
mkdir build-tsan
cd build-tsan
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS_TSAN="-O2 -Wall -Wextra -Wpedantic -fsanitize=thread" ..
cmake --build .
```

## Test Instructions

### Run all tests

```bash
# From build directory
ctest --verbose
```

### Run specific test executables

```bash
# From build directory
./engine_tests                # V0 tests
./engine_tests_v1             # V1 tests
./engine_tests_v1_pool        # V1+Pool tests
./engine_tests_multi_instrument  # Multi-instrument tests
./engine_tests_concurrency    # Mutex concurrency tests
./engine_tests_spsc_queue     # SPSC queue tests
./engine_tests_spsc_integration  # SPSC integration tests
```

## Benchmark Reproduction Instructions

### V0 vs V1 comparison

```bash
./build-release/engine_benchmarks_v1 --benchmark_repetitions=10
```

### Object pool comparison

```bash
./build-release/engine_benchmarks_pool --benchmark_repetitions=5
```

### Mutex baseline

```bash
./build-release/engine_benchmarks_concurrency --benchmark_repetitions=3 --benchmark_filter=BM_Mutex_1Thread/100000
```

### SPSC ingestion (Phase 7 final measurement)

```bash
./build-release/engine_benchmarks_spsc --benchmark_repetitions=3 --benchmark_filter=BM_SPSC_1Producer/100000
```

## Documentation

- `docs/ARCHITECTURE.md`: System architecture diagrams and evolution
- `docs/RESULTS.md`: Comprehensive benchmark results
- `docs/DECISIONS.md`: Architectural and technical decisions
- `docs/PERFORMANCE_NOTES.md`: Detailed performance analysis
- `docs/SPSC_CORRECTNESS_AUDIT.md`: SPSC implementation correctness audit
- `docs/SPSC_INGESTION.md`: SPSC architecture details
- `docs/MUTEX_CONCURRENCY_BASELINE.md`: Mutex baseline analysis
- `docs/MULTI_INSTRUMENT_ARCHITECTURE.md`: Multi-instrument design
- `docs/WORKLOAD_FORMAT.md`: Workload generation specification

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
