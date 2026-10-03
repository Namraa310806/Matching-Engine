# Multi-Instrument Architecture (Phase 5A)

## Overview

Phase 5A extends the matching engine to support multiple independent instruments within a single engine instance. Each instrument maintains its own isolated order book state, ensuring complete separation of orders, trades, and market data events across instruments.

## Architecture

### Component Structure

```
MultiInstrumentEngine
        |
        +---- Instrument A ---- OrderBookV1Pool
        |                           |
        |                           +-- bids_ (sorted vector)
        |                           +-- asks_ (sorted vector)
        |                           +-- order_index_ (unordered_map)
        |                           +-- pool_ (ObjectPool)
        |
        +---- Instrument B ---- OrderBookV1Pool
        |                           |
        |                           +-- bids_ (sorted vector)
        |                           +-- asks_ (sorted vector)
        |                           +-- order_index_ (unordered_map)
        |                           +-- pool_ (ObjectPool)
        |
        +---- Instrument C ---- OrderBookV1Pool
        |
        +---- ...
```

### Key Components

#### 1. InstrumentId Type
- **Type**: `std::string`
- **Purpose**: Lightweight, copyable, comparable identifier for instruments
- **Examples**: "AAPL", "MSFT", "GOOG", "BTC-USD", "NIFTY"
- **Properties**:
  - Hashable (usable as unordered_map key)
  - Deterministic
  - Suitable for tests and benchmarks

#### 2. MultiInstrumentEngine
- **Responsibilities**:
  - Manages multiple independent order books
  - Routes orders to the correct instrument
  - Maintains globally unique order IDs
  - Provides O(1) cancellation routing
  - Wraps trades/events with instrument context

- **Core Data Structures**:
  ```cpp
  std::unordered_map<InstrumentId, OrderBookV1Pool> books_;
  std::unordered_map<OrderId, InstrumentId> order_to_instrument_;
  OrderId next_order_id_ = 1;
  ```

#### 3. Extended Domain Types
- **Trade**: Now includes `InstrumentId instrument_id` field
- **MarketDataEvent**: Now includes `InstrumentId instrument_id` field
- **Backward Compatibility**: Single-instrument implementations use empty string for instrument_id

## Ownership Model

### Instrument Ownership
Each instrument owns an independent `OrderBookV1Pool` instance:
- **Per-Instrument State**:
  - Bid price levels (sorted vector)
  - Ask price levels (sorted vector)
  - Order ID index (unordered_map)
  - Object pool for order nodes
  - Sequence numbers

- **Global State**:
  - Instrument-to-book mapping (unordered_map)
  - Order-to-instrument routing index (unordered_map)
  - Global order ID counter

### Why Independent Books?

Independent books are a natural ownership boundary because:
1. **Matching is inherently instrument-local**: Orders on AAPL never match orders on MSFT
2. **Best bid/ask are instrument-local**: Each instrument has its own market state
3. **Order queues are instrument-local**: FIFO queues exist per price level per instrument
4. **Cancellation routing can be localized**: O(1) lookup via order-to-instrument index
5. **Future concurrency foundation**: Individual instruments can be assigned to workers/threads in later phases

## State Isolation

### Per-Instrument State (Mutable)
- Bid price levels
- Ask price levels
- Order index
- Object pool
- Sequence numbers

### Global State (Mutable)
- Instrument book registry
- Order routing index
- Global order ID counter

### Immutable/Shared State
- None (each engine instance is independent)

## Complexity Analysis

### Instrument Lookup
- **Operation**: `submit_order(instrument_id, order)`
- **Data Structure**: `unordered_map<InstrumentId, OrderBookV1Pool>`
- **Complexity**: O(1) average, O(n) worst case (hash collision)
- **Notes**: Uses default std::hash<std::string>

### Order Submission Routing
- **Operation**: Route order to correct book
- **Steps**:
  1. Lookup/create book: O(1) average
  2. Assign global order ID: O(1)
  3. Submit to underlying book: O(log m) where m = price levels
  4. Register in routing index: O(1)
- **Total**: O(log m) average (dominated by underlying book)

### Cancellation Routing
- **Operation**: `cancel_order(order_id)`
- **Steps**:
  1. Lookup instrument in routing index: O(1) average
  2. Lookup book for instrument: O(1) average
  3. Cancel in underlying book: O(1) with intrusive list
  4. Remove from routing index: O(1)
- **Total**: O(1) average
- **Important**: No scanning of multiple books required

### Order ID Lookup
- **Operation**: `get_instrument_for_order(order_id)`
- **Data Structure**: `unordered_map<OrderId, InstrumentId>`
- **Complexity**: O(1) average, O(n) worst case
- **Purpose**: Enables O(1) cancellation routing

### Matching
- **Operation**: Performed by underlying `OrderBookV1Pool`
- **Complexity**: O(k log m) where k = orders matched, m = price levels
- **Isolation**: Matching never crosses instrument boundaries

## Order ID Uniqueness

### Global ID Generation
- **Strategy**: Monotonic counter starting at 1
- **Scope**: Global across all instruments
- **Guarantee**: No collisions even with rapid instrument switching
- **Example**:
  ```
  AAPL order -> ID 1
  MSFT order -> ID 2
  AAPL order -> ID 3
  GOOG order -> ID 4
  ```

### Routing Index
- **Purpose**: Map OrderId -> InstrumentId for O(1) cancellation
- **Lifetime**: Entry exists while order is resting in book
- **Cleanup**: Removed on cancellation or full fill
- **Complexity**: O(1) lookup, O(1) insertion, O(1) deletion

## Invariants

### Instrument Isolation Invariants

1. **No Cross-Instrument Matching**:
   - AAPL buy + MSFT sell can never match
   - Trades always reference a single instrument

2. **Independent Best Bid/Ask**:
   - Each instrument maintains its own best bid/ask
   - Operations on AAPL do not affect MSFT's best prices

3. **Independent Order Queues**:
   - Orders at identical prices on different instruments remain separate
   - FIFO ordering is preserved per instrument

4. **Independent Cancellation**:
   - Cancelling an order on instrument A does not affect instrument B
   - Cancellation by ID routes to correct instrument in O(1)

5. **Independent Trade Generation**:
   - Each instrument generates its own trades
   - Trade events carry the correct instrument_id

### Quantity Conservation Invariants

For each instrument independently:
- `filled_quantity + remaining_quantity == original_quantity`
- No negative quantities
- No order filled beyond original quantity
- Price-time priority preserved
- Market order behavior unchanged

## API Design

### Order Submission
```cpp
std::pair<std::vector<Trade>, std::vector<MarketDataEvent>>
submit_order(const InstrumentId& instrument_id, const Order& order);
```
- **Note**: Order ID in parameter is ignored; engine assigns global ID
- **Returns**: Trades and events wrapped with instrument_id

### Cancellation
```cpp
std::pair<bool, std::vector<MarketDataEvent>>
cancel_order(OrderId order_id);
```
- **Routing**: O(1) lookup via order-to-instrument index
- **Returns**: Success flag and events wrapped with instrument_id

### Query Operations
```cpp
std::optional<Price> best_bid(const InstrumentId& instrument_id) const;
std::optional<Price> best_ask(const InstrumentId& instrument_id) const;
size_t buy_order_count(const InstrumentId& instrument_id) const;
size_t sell_order_count(const InstrumentId& instrument_id) const;
```
- **Behavior**: Returns 0/empty for non-existent instruments
- **Complexity**: O(1) average

## Design Tradeoffs

### Tradeoff 1: String vs Integer InstrumentId
- **Choice**: Used `std::string` for InstrumentId
- **Rationale**:
  - Natural fit for existing project conventions
  - Human-readable for tests and debugging
  - No need for additional symbol registry
- **Alternative**: Integer ID with symbol registry
  - More compact, but adds indirection
  - Would require symbol lookup table

### Tradeoff 2: Global Order ID Counter
- **Choice**: Simple monotonic counter
- **Rationale**:
  - No concurrency in this phase
  - Deterministic and simple
  - Sufficient for correctness
- **Future**: May need atomic counter in Phase 5B/5C

### Tradeoff 3: Order-to-Instrument Index
- **Choice**: Separate routing index for O(1) cancellation
- **Rationale**:
  - Avoids scanning all books on cancellation
  - Clean separation of concerns
  - Minimal memory overhead (one entry per resting order)
- **Alternative**: Store instrument in order book index
  - Would require modifying OrderBookV1Pool internals
  - Less clean separation

### Tradeoff 4: Empty String for Single-Instrument Compatibility
- **Choice**: Single-instrument implementations use empty string for instrument_id
- **Rationale**:
  - Minimal changes to existing code
  - Preserves backward compatibility
  - Clear semantic (no instrument = single-instrument mode)
- **Alternative**: Add separate constructor overloads
  - More code duplication
  - Less flexible

## Implementation Details

### Thread Safety
- **Current Status**: Not thread-safe (no concurrency in Phase 5A)
- **Future**: Individual instruments can be made thread-local in Phase 5B/5C

### Memory Management
- **Books**: Stored in unordered_map, value semantics
- **Object Pools**: Each book has its own pool
- **Routing Index**: Entries removed on cancellation/fill

### Error Handling
- **Non-existent Instrument**: Returns 0/empty values (no exceptions)
- **Unknown Order ID**: Returns {false, {}} for cancellation
- **Consistent with existing project conventions**

## Testing Strategy

### Multi-Instrument Test Suite
Located in `tests/multi_instrument_test.cpp`

#### Test Coverage
1. **Multiple Instruments**: Create and access multiple instruments
2. **Independent Books**: Orders at identical prices remain separate
3. **No Cross-Instrument Matching**: Verify isolation
4. **Simultaneous Activity**: Interleaved operations across instruments
5. **Global Order ID Uniqueness**: Verify no collisions
6. **Cancellation by ID**: Verify O(1) routing
7. **Trades Reference Correct Instrument**: Verify event correctness
8. **Instrument Isolation Under Matching**: Crossed books on multiple instruments
9. **Empty/Nonexistent Instrument**: Define and test edge cases
10. **Quantity Conservation**: Verify invariants per instrument
11. **Market Orders**: Verify across instruments
12. **Deterministic Randomized**: Stress test with fixed seed

### Backward Compatibility
All existing test suites must pass:
- `engine_tests` (V0)
- `engine_tests_v1` (V1)
- `engine_tests_v1_pool` (V1 + Pool)

## Benchmarking Strategy

### Multi-Instrument Benchmark Suite
Located in `benchmarks/benchmark_multi.cpp`

#### Benchmarks
1. **Single-Instrument Direct**: Baseline (OrderBookV1Pool directly)
2. **Single-Instrument via MultiEngine**: Measures routing overhead
3. **Two Instruments**: Scalability with 2 instruments
4. **Four Instruments**: Scalability with 4 instruments
5. **Eight Instruments**: Scalability with 8 instruments
6. **Cancellation Routing**: Measures O(1) routing performance

#### Metrics
- Throughput (orders/sec)
- Total execution time
- p50, p95, p99 latencies (via Google Benchmark)

#### Comparison Goal
Establish routing overhead by comparing:
- Single-instrument direct vs single-instrument via MultiEngine
- 1 vs 2 vs 4 vs 8 instruments

## Future Work (Out of Scope for Phase 5A)

The following are explicitly deferred to later phases:
- Mutexes and thread safety (implemented in Phase 5B)
- Thread pools for parallel matching
- Atomics for shared state
- SPSC queues for order routing
- Lock-free data structures
- Networking
- External market feeds
- Exchange connectivity
- Phase 6 micro-optimizations

## Phase 5B: Mutex Concurrency Baseline

Phase 5B adds a mutex-protected wrapper (`MutexMultiInstrumentEngine`) that provides thread-safe concurrent access to the multi-instrument engine. Key characteristics:

- **Global mutex**: Single `std::mutex` protects all shared state
- **API preservation**: Same interface as single-threaded engine
- **No atomics**: Mutex provides all synchronization (no `std::atomic` in this phase)
- **Baseline purpose**: Establishes measurable contention characteristics for comparison with Phase 5C's lock-free architecture

### Architecture (Phase 5B)

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

### Critical Section
The mutex protects:
- Instrument-to-book map
- Order routing index (OrderId -> InstrumentId)
- Global order ID generation
- Access to individual order books

### Benchmark Results Summary (100,000 orders, wall time)
- Single-threaded baseline: 1.60M ops/sec (62.6 ms)
- Mutex 1 thread: 1.46M ops/sec (68.3 ms) - 8% slower
- Mutex 2 threads: 394K ops/sec (253.8 ms) - 4x slower
- Mutex 4 threads: 459K ops/sec (217.9 ms) - 3.5x slower
- Mutex 8 threads: 1.16M ops/sec (86.4 ms) - 1.4x slower

The global mutex serializes all operations, making multi-threaded execution slower than single-threaded due to contention and thread overhead.

See `docs/MUTEX_CONCURRENCY_BASELINE.md` for detailed analysis.

## Summary

Phase 5A establishes a clean, correct multi-instrument architecture:
- **Instrument -> Independent Order Book** ownership model
- O(1) routing for order submission and cancellation
- Complete isolation between instruments
- Global order ID uniqueness
- Minimal overhead over single-instrument baseline
- Foundation for future concurrency work

Phase 5B adds a mutex concurrency baseline:
- **Global mutex** protection for thread-safe concurrent access
- Measurable contention characteristics
- Clean separation from single-threaded implementation
- Foundation for Phase 5C's lock-free/SPSC architecture comparison
