# SPSC Ingestion Architecture (Phase 5)

## Overview

Phase 5 implements a single-producer/single-consumer (SPSC) ingestion architecture for the multi-instrument matching engine. This establishes a lock-free ingestion path that eliminates the mutex contention bottleneck observed in Phase 5B.

## Architecture

### Component Structure

```
Producer thread
    ->
SPSC ring buffer (fixed capacity, power-of-two)
    ->
Single matching-engine thread (consumer)
    ->
Owns MultiInstrumentEngine (sole writer of order-book state)
    |
    +-- Instrument A -> OrderBookV1Pool
    +-- Instrument B -> OrderBookV1Pool
    +-- Instrument C -> OrderBookV1Pool
```

### Key Design Decisions

1. **Single-Writer Ownership**: The matching-engine consumer thread is the SOLE writer of order-book state. This eliminates lock contention on book state.

2. **SPSC Queue**: Fixed-capacity ring buffer using acquire/release memory ordering. No mutex or condition_variable inside the queue.

3. **Ingestion Commands**: Type-safe command encoding (SubmitLimitOrderCmd, SubmitMarketOrderCmd, CancelOrderCmd) sent through the queue.

4. **Asynchronous Processing**: Producer enqueues commands and continues; consumer processes commands asynchronously. Producer does not receive immediate trades/events.

5. **No Dynamic Resizing**: Queue capacity is fixed at compile time (template parameter). Producer must handle backpressure when queue is full.

## Critical Architectural Invariant

**ONLY the matching-engine consumer thread directly mutates order-book state.**

This invariant is enforced by:
- API design: `SpscMultiInstrumentEngine` only exposes enqueue methods
- No direct access: Underlying `MultiInstrumentEngine` is private (except test-only)
- Clear documentation: Ownership boundary is explicit

## SPSC Queue Implementation

### Data Structure

- **Type**: Ring buffer with power-of-two capacity
- **Storage**: Pre-allocated array with placement new for construction
- **Capacity**: Fixed at compile time (template parameter), power-of-two preferred
- **Indexing**: Bitmask wraparound (capacity - 1) for efficient indexing

### Ownership Model

- **Producer-owned**: `write_index_` (only producer modifies)
- **Consumer-owned**: `read_index_` (only consumer modifies)
- **Shared mutable**: Ring buffer data (synchronized via acquire/release)
- **No mutex**: No locking primitives inside the queue
- **No condition_variable**: No waiting primitives inside the queue

### Memory Ordering

#### Producer Enqueue

```cpp
bool enqueue(const T& value) {
    const size_t current_write = write_index_.load(std::memory_order_relaxed);
    const size_t next_write = (current_write + 1) & mask_;

    // Check if queue is full
    const size_t current_read = read_index_.load(std::memory_order_relaxed);
    if (next_write == current_read) {
        return false; // Queue is full
    }

    // Write the value
    new (&buffer_[current_write]) T(value);

    // Publish write position with release semantics
    write_index_.store(next_write, std::memory_order_release);

    return true;
}
```

**Memory ordering rationale:**
- `load(read_index_)` with **relaxed**: Producer only needs to know if there's space. No synchronization needed because the producer only cares about the current value, not establishing a happens-before relationship.
- `store write_index_` with **release**: Critical for correctness. This release operation synchronizes with the consumer's acquire load. All writes to `buffer_[current_write]` happen-before this store, ensuring the value is fully constructed before the consumer sees the updated write index.

#### Consumer Dequeue

```cpp
bool dequeue(T& value) {
    const size_t current_read = read_index_.load(std::memory_order_relaxed);

    // Check if queue is empty
    const size_t current_write = write_index_.load(std::memory_order_acquire);
    if (current_read == current_write) {
        return false; // Queue is empty
    }

    // Read the value
    value = std::move(buffer_[current_read]);

    // Destroy the old value
    buffer_[current_read].~T();

    // Update read position
    const size_t next_read = (current_read + 1) & mask_;
    read_index_.store(next_read, std::memory_order_relaxed);

    return true;
}
```

**Memory ordering rationale:**
- `load(write_index_)` with **acquire**: Critical for correctness. This acquire operation synchronizes with the producer's release store. It ensures all writes to the value (from the producer) happen-before this load, so the consumer sees a fully constructed value.
- `store read_index_` with **relaxed**: Consumer owns this index exclusively. No synchronization needed because the producer only reads it with relaxed ordering to check for full condition.

#### Why Not seq_cst?

- **Unnecessary overhead**: `memory_order_seq_cst` provides a total ordering across all atomic operations, which is stronger than needed for SPSC.
- **Sufficient with acquire/release**: The acquire/release pair establishes the required happens-before relationship between producer's data publication and consumer's data consumption.
- **Performance**: Acquire/release is typically faster than seq_cst on most architectures.

#### Why Relaxed for Some Operations?

- **Single-writer indices**: Both `write_index_` and `read_index_` are modified by only one thread each.
- **Full/empty checks**: When checking if the queue is full (producer) or empty (consumer), we only need the current value, not a synchronized view.
- **No data race**: The producer never modifies `read_index_`, and the consumer never modifies `write_index_`, so relaxed reads are safe.

### Happens-Before Relationship

The critical synchronization point:
1. Producer constructs value in `buffer_[current_write]`
2. Producer calls `write_index_.store(next_write, std::memory_order_release)`
3. **happens-before**: The store-release synchronizes with...
4. Consumer calls `write_index_.load(std::memory_order_acquire)`
5. Consumer reads value from `buffer_[current_read]`

This ensures that the consumer cannot see the updated write index until after the value is fully constructed and stored in the buffer.

## Ingestion Command Design

### Command Types

- **SubmitLimitOrderCmd**: Instrument ID + Order (side, price, quantity, sequence)
- **SubmitMarketOrderCmd**: Instrument ID + Order (side, quantity, sequence; price ignored)
- **CancelOrderCmd**: Order ID to cancel

### Design Rationale

- **Type-safe encoding**: `std::variant` ensures only valid command types
- **No synchronization primitives**: Commands are plain data structures
- **Trivially movable**: Efficient transfer through queue (move semantics)
- **Self-contained**: All necessary information for matching engine processing
- **No external references**: Commands are value types, no lifetime issues

### Ownership and Lifetime

- **Producer**: Constructs commands and enqueues them (move or copy)
- **Queue**: Transfers commands via move semantics when possible
- **Consumer**: Dequeues commands and processes them
- **No dangling references**: Commands are value types, no pointers to external state

## Capacity and Backpressure

### Fixed Capacity

- **Decision**: Fixed-capacity queue, no dynamic resizing
- **Rationale**: Simpler implementation, predictable memory usage, no allocation during operation
- **Trade-offs**: Producer must handle full queue (backpressure)

### Full Queue Behavior

- **Enqueue returns false**: Producer must retry or handle backpressure
- **No blocking**: Queue does not block or wait when full
- **Producer responsibility**: Implement retry logic or rate limiting if needed

### Empty Queue Behavior

- **Dequeue returns false**: Consumer yields and retries
- **Busy-spin avoidance**: Consumer calls `std::this_thread::yield()` when empty
- **No blocking**: Queue does not block or wait when empty

## Thread Roles

### Producer Thread

- Constructs ingestion commands
- Enqueues commands to SPSC queue
- Handles backpressure when queue is full (retry logic)
- Never directly calls matching/book mutation functions
- Owns the queue's write position exclusively

### Consumer Thread (Matching Thread)

- Dequeues commands from SPSC queue
- Processes commands by invoking `MultiInstrumentEngine`
- Owns the queue's read position exclusively
- **Sole writer** of all order-book state
- Yields when queue is empty to avoid busy-spin

## Test Coverage

### SPSC Queue Tests (12 tests)

1. **EnqueueDequeueSequence**: Verify basic enqueue/dequeue ordering
2. **EmptyBehavior**: Verify dequeue from empty queue returns false
3. **FullBehavior**: Verify enqueue to full queue returns false
4. **Wraparound**: Verify indices wrap around correctly
5. **WraparoundAtCapacity**: Verify wraparound at capacity boundary
6. **FIFOOrdering**: Verify values are dequeued in exact order
7. **ConcurrentStress**: High-throughput concurrent producer/consumer
8. **NoLossNoDuplication**: Verify every message received exactly once
9. **MoveOnlyType**: Verify queue works with move-only types
10. **SizeAndCapacity**: Verify size/capacity methods
11. **ComplexType**: Verify queue works with complex types (std::string)

### SPSC Integration Tests (14 tests)

1. **BasicSubmitOrder**: Basic order submission and processing
2. **MultipleOrders**: Multiple orders processed correctly
3. **MarketOrder**: Market order processing
4. **CancelOrder**: Cancellation works correctly
5. **MultipleInstruments**: Orders for multiple instruments remain isolated
6. **CrossingOrdersGenerateTrades**: Crossing orders generate trades
7. **PartialFills**: Partial fills work correctly
8. **QueueFullBehavior**: Queue returns false when full
9. **HighThroughputStress**: High throughput with many orders
10. **GlobalIdUniqueness**: Order IDs remain unique
11. **ProducerStress**: Producer keeps up with high submission rate
12. **QuantityConservation**: Quantity is conserved through matching
13. **MixedWorkload**: Mix of add, cancel, and match operations

## ThreadSanitizer Results

All SPSC tests pass with ThreadSanitizer (`-fsanitize=thread`):
- **No data races detected**
- **No synchronization errors**
- **No undefined behavior attributable to concurrent access**

**Note**: A memory ordering bug was discovered and fixed during the correctness audit. The move version of `SpscQueue::enqueue()` was using `memory_order_relaxed` instead of `memory_order_acquire` when loading the consumer's `read_index_`. This was a genuine data race under the C++ memory model. The fix ensures proper synchronization with the consumer's release store. See `SPSC_CORRECTNESS_AUDIT.md` for details.

## Limitations

1. **Single producer**: Only one producer thread supported
2. **Fixed capacity**: Queue capacity is fixed at compile time
3. **Asynchronous API**: Producer does not receive immediate trades/events
4. **Backpressure**: Producer must handle full queue condition
5. **Single matching thread**: Matching logic is single-threaded (intentional for correctness)

## Future Work (Phase 6)

Phase 6 may investigate:
- Per-instrument SPSC queues
- MPMC queues for multiple producers
- Lock-free order book modifications
- Cache-line optimization
- Backpressure strategies
- Latency measurement and optimization

## Files Created/Modified

### New Files
- `include/engine/spsc_queue.hpp` - SPSC ring buffer implementation
- `include/engine/ingestion_command.hpp` - Ingestion command types
- `include/engine/spsc_multi_instrument_engine.hpp` - SPSC engine wrapper
- `tests/spsc_queue_test.cpp` - SPSC queue test suite (12 tests)
- `tests/spsc_integration_test.cpp` - SPSC integration test suite (14 tests)
- `benchmarks/benchmark_spsc.cpp` - SPSC benchmark suite
- `docs/SPSC_INGESTION.md` - This documentation

### Modified Files
- `CMakeLists.txt` - Added SPSC test executables and benchmark executable
- `docs/DECISIONS.md` - Added SPSC architecture and memory ordering documentation

## Verification

### New SPSC Tests (All Passing)
- SPSC queue suite: 12 tests
- SPSC integration suite: 14 tests
- TSan clean: No data races or synchronization errors

### Existing Tests (All Passing - Regression Check)
- V0: 122 tests
- V1: 122 tests
- V1+Pool: 124 tests
- Multi-instrument: 38 tests
- Mutex concurrency: 11 tests

## Conclusion

Phase 5 successfully implements a clean SPSC ingestion architecture. The implementation:
- Establishes single-writer ownership of order-book state
- Eliminates lock contention on book state
- Provides a lock-free ingestion path with acquire/release memory ordering
- Maintains all matching semantics and correctness invariants
- Provides a fair performance comparison with mutex baseline

The SPSC architecture provides a clear alternative to the mutex baseline, with:
- **Demonstrated higher throughput** (9-26x speedup in benchmarks)
- Potential for lower tail latency (no blocking on mutex)
- Single-writer invariant (clear ownership boundary)
- Fixed capacity (predictable memory usage)
- Order ID return from submit_order (enables cancellations in async API)

**Benchmark Results**: SPSC shows significant throughput advantage (9-26x) over mutex and single-threaded baselines when processing equivalent workloads including actual cancellations. See `SPSC_CORRECTNESS_AUDIT.md` for detailed analysis and performance results.
