# Mutex Concurrency Baseline (Phase 5B)

## Overview

Phase 5B implements a deliberately simple mutex-based concurrency baseline for the multi-instrument matching engine. This establishes a measurable baseline whose contention characteristics can be compared against Phase 5C's lock-free/SPSC architecture.

## Architecture

### Component Structure

```
Multiple producer threads
        |
        v
   global mutex (std::mutex)
        |
        v
MutexMultiInstrumentEngine (wrapper)
        |
        v
MultiInstrumentEngine (single-threaded)
        |
        +-- Instrument A -> OrderBookV1Pool
        +-- Instrument B -> OrderBookV1Pool
        +-- Instrument C -> OrderBookV1Pool
```

### Key Design Decisions

1. **Separate Wrapper Class**: `MutexMultiInstrumentEngine` wraps the existing `MultiInstrumentEngine` without modifying the single-threaded implementation. This preserves the original implementation as a baseline.

2. **Global Mutex**: A single `std::mutex` protects all shared mutable state. This is intentionally coarse-grained to establish a simple, measurable baseline.

3. **No Atomics**: Phase 5B does NOT use `std::atomic` for order ID generation or any other state. The mutex itself provides the required synchronization.

4. **API Preservation**: The mutex-protected engine exposes the same API as the single-threaded engine, making it a drop-in replacement for concurrent use cases.

## Critical Section

The mutex protects the following shared mutable state:

- **Instrument-to-book map** (`std::unordered_map<InstrumentId, std::unique_ptr<OrderBookV1Pool>> books_`)
- **Order routing index** (`std::unordered_map<OrderId, InstrumentId> order_to_instrument_`)
- **Global order ID counter** (`OrderId next_order_id_`)
- **Access to individual order books** (all operations through `submit_order` and `cancel_order`)

All public methods in `MutexMultiInstrumentEngine` acquire the mutex before delegating to the underlying `MultiInstrumentEngine`:

```cpp
std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> submit_order(
    const InstrumentId& instrument_id,
    const Order& order
) {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.submit_order(instrument_id, order);
}
```

## Contention Model

### Source of Contention

Multiple producer threads contending for the same global mutex create serialization:

1. **Thread A** acquires mutex, submits order to instrument
2. **Thread B** blocks on mutex
3. **Thread A** releases mutex
4. **Thread B** acquires mutex, submits order to instrument
5. ...

The critical section includes:
- Order ID generation (simple increment)
- Instrument book lookup/creation
- Order submission to book
- Routing index update
- Trade/event generation

### Workload Characteristics

Different workloads exhibit different contention patterns:

#### Workload A: Same Instrument (High Contention)
- All threads submit to the same instrument
- Maximum mutex contention
- No opportunity for parallelism
- Expected: Throughput plateaus or decreases with more threads

#### Workload B: Multiple Instruments (Distributed Contention)
- Threads submit to different instruments
- Still contended by global mutex (even though books are independent)
- Opportunity for later per-instrument locking optimization
- Expected: Slightly better than same-instrument, but still serialized

#### Workload C: Mixed Activity
- Mix of add, cancel, and match operations
- Same contention pattern as Workload A/B
- Additional complexity from cancellation routing
- Expected: Similar to same-instrument contention

## Benchmark Results

### Test Environment
- CPU: 4 cores @ 2611.2 MHz
- L1 Cache: 48 KiB data, 32 KiB instruction (per core)
- L2 Cache: 1280 KiB (per core)
- L3 Cache: 12288 KiB (shared)
- Build: Release (-O3 -march=native)
- Workload: 100,000 orders (90,000 submits + 10,000 cancels)

### Throughput Comparison (wall time, 100,000 orders)

| Benchmark | Wall Time | Throughput | vs Baseline |
|-----------|-----------|-----------|-------------|
| **Single-threaded baseline** | 62.6 ms | 1.60M ops/sec | 1.00x |
| **Mutex 1 thread** | 68.3 ms | 1.46M ops/sec | 0.91x (8% slower) |
| **Mutex 2 threads** | 253.8 ms | 394K ops/sec | 0.25x (4x slower) |
| **Mutex 4 threads** | 217.9 ms | 459K ops/sec | 0.29x (3.5x slower) |
| **Mutex 8 threads** | 86.4 ms | 1.16M ops/sec | 0.73x (1.4x slower) |

### Scalability Observations

1. **1 Thread**: Mutex overhead is ~8% slower than baseline due to lock acquisition/release overhead on every operation

2. **2 Threads**: **4x slower** than single-threaded baseline due to mutex contention. Multiple threads contend for the same global mutex, serializing all operations and adding thread synchronization overhead

3. **4 Threads**: **3.5x slower** than single-threaded baseline. Contention remains high as all threads compete for the same mutex

4. **8 Threads**: **1.4x slower** than single-threaded baseline. Better than 2/4 threads due to work distribution and possibly more favorable thread scheduling, but still slower than single-threaded due to contention

5. **Contention dominates**: The global mutex serializes all engine operations, so adding more threads does not improve throughput and typically makes it worse due to:
   - Mutex contention (threads block waiting for lock)
   - Thread creation/join overhead
   - Context switching
   - Cache invalidation from multiple cores accessing the same lock

### Why Throughput Decreases with More Threads

The global mutex creates a serialization point:
- Thread A acquires mutex, processes one order, releases mutex
- Thread B (waiting) acquires mutex, processes one order, releases mutex
- Thread C (waiting) acquires mutex, processes one order, releases mutex
- ...

This is actually **slower** than single-threaded execution because:
1. **Lock overhead**: Every operation now requires lock acquisition/release
2. **Thread overhead**: Thread creation, joining, and context switching
3. **Contention**: Threads spend time blocked waiting for the mutex
4. **Cache effects**: Multiple cores accessing the same lock line causes cache invalidation

The single-threaded baseline processes orders sequentially without any synchronization overhead, which is more efficient than the multi-threaded mutex-protected version.

### Latency Observations

- **Single-threaded baseline**: Consistent latency, no blocking
- **Mutex 1 thread**: Slightly higher latency due to lock acquisition/release
- **Multi-threaded**: Variable latency with high tail latency due to mutex contention and thread scheduling

### Instrument Distribution Note

The global mutex serializes all operations regardless of which instrument is being accessed. Therefore, distributing orders across multiple instruments does not improve throughput - all threads still contend for the same global mutex. This is expected behavior for the mutex baseline and highlights the opportunity for per-instrument locking in Phase 5C.

## Global Order ID Correctness

The mutex implementation preserves global order ID uniqueness:

- **Mechanism**: The mutex serializes access to `next_order_id_` counter
- **No atomics**: ID generation is protected by the mutex, not by `std::atomic`
- **Testing**: Concurrent submission tests verify no duplicate IDs across threads
- **Result**: All 800 concurrent order submissions (8 threads × 100 orders) generated unique IDs

## Test Coverage

### Concurrency Test Suite (11 tests)

1. **ConcurrentSubmissionSameInstrument**: 4 threads, 100 orders each, same instrument
2. **ConcurrentSubmissionMultipleInstruments**: 4 threads, 50 orders each, 2 instruments
3. **SameInstrumentContention**: 8 threads, 200 orders each, same instrument (high contention)
4. **ConcurrentCancellation**: 4 threads cancelling distinct orders
5. **ConcurrentCancellationAfterActivity**: Multi-instrument cancellation after activity
6. **ConcurrentMixedWorkload**: 4 threads, mixed add/cancel/match operations
7. **GlobalIdUniqueness**: 8 threads, 100 orders each, verify ID uniqueness
8. **CancellationRoutingWithConcurrentSubmission**: Concurrent submission + cancellation routing
9. **RepeatedStressTest**: 5 iterations of concurrent workload
10. **HighContentionStressTest**: 16 threads, 50 orders each, same instrument
11. **QuantityConservation**: Concurrent matching with quantity verification

### ThreadSanitizer Results

All concurrency tests pass with ThreadSanitizer (`-fsanitize=thread`):
- **No data races detected**
- **No synchronization errors**
- **No undefined behavior attributable to concurrent access**

Test execution time with TSan: ~167ms (vs ~438ms without TSan)

## Limitations

1. **Coarse-grained locking**: Global mutex serializes all operations, even when independent instruments could be processed in parallel

2. **No lock-free structures**: Phase 5B deliberately avoids lock-free data structures, atomics, or custom synchronization primitives

3. **No thread pools**: Threads are created explicitly for each benchmark; no reusable thread pool

4. **No speculative execution**: No speculative lock-free or optimistic concurrency techniques

5. **Contended cancellation**: Cancellation routing requires holding the mutex, adding to contention

6. **No cache optimization**: No padding for false sharing, no cache-aware data layout

## Future Work (Phase 5C)

Phase 5C will investigate:

1. **SPSC queues**: Single-producer, single-consumer queues per instrument
2. **Single-writer architecture**: Dedicated writer thread for each instrument
3. **Lock-free order book modifications**: Atomics for hot-path operations
4. **Per-instrument locking**: Finer-grained synchronization
5. **Performance comparison**: Quantify improvements over mutex baseline

## Files Created/Modified

### New Files
- `include/engine/mutex_multi_instrument_engine.hpp` - Mutex-protected engine header
- `src/mutex_multi_instrument_engine.cpp` - Mutex-protected engine implementation
- `tests/concurrency_test.cpp` - Concurrency test suite (11 tests)
- `benchmarks/benchmark_concurrency.cpp` - Concurrency benchmark suite
- `docs/MUTEX_CONCURRENCY_BASELINE.md` - This documentation

### Modified Files
- `CMakeLists.txt` - Added mutex engine source, test executable, benchmark executable, TSan build config

## Verification

### Existing Tests (All Passing)
- V0: 122 tests
- V1: 122 tests
- V1+Pool: 124 tests
- Multi-instrument: 38 tests

### New Concurrency Tests (All Passing)
- Concurrency suite: 11 tests
- TSan clean: No data races or synchronization errors

### Benchmarks (100,000 orders, wall time)
- Single-threaded baseline: 1.60M ops/sec (62.6 ms)
- Mutex 1 thread: 1.46M ops/sec (68.3 ms) - 8% slower
- Mutex 2 threads: 394K ops/sec (253.8 ms) - 4x slower
- Mutex 4 threads: 459K ops/sec (217.9 ms) - 3.5x slower
- Mutex 8 threads: 1.16M ops/sec (86.4 ms) - 1.4x slower

## Conclusion

Phase 5B successfully establishes a clean, measurable mutex-based concurrency baseline. The implementation:

- Preserves all matching semantics and correctness invariants
- Provides thread-safe concurrent access to the multi-instrument engine
- Demonstrates measurable contention characteristics
- Establishes a foundation for Phase 5C's lock-free/SPSC architecture comparison

The mutex baseline shows that **coarse-grained locking with a global mutex is slower than single-threaded execution** for this workload:
- 1 thread: 8% slower (lock overhead)
- 2 threads: 4x slower (contention + thread overhead)
- 4 threads: 3.5x slower (contention dominates)
- 8 threads: 1.4x slower (better thread scheduling but still slower than single-threaded)

This provides a clear baseline: any Phase 5C architecture must demonstrate **better than single-threaded throughput** to be worthwhile. The global mutex serialization point is the fundamental bottleneck that Phase 5C's SPSC/lock-free architecture must overcome.
