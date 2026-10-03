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

## Matching Engine Design (v0 - Phase 1.3)

### Matching Algorithm
- **Decision**: Implement price-time priority matching with FIFO within price levels
- **Rationale**:
  - Standard exchange matching algorithm ensures fairness
  - Price priority ensures best execution for market participants
  - Time priority prevents order jumping at same price
  - Simple to implement and verify correctness
- **Trade-offs**: No optimization for high-frequency trading (intentional for correctness baseline)

### Matching Rules
- **Buy order matching**: A buy limit order matches when `resting_ask_price <= incoming_buy_price`
- **Sell order matching**: A sell limit order matches when `resting_bid_price >= incoming_sell_price`
- **Price priority**: Always match best available price first
  - Buy orders consume lowest ask price first
  - Sell orders consume highest bid price first
- **Time priority**: Within same price level, oldest order fills first (FIFO)

### Partial Fill Semantics
- **Decision**: Support partial fills for both incoming and resting orders
- **Implementation**:
  - Trade quantity = min(incoming_remaining_quantity, resting_remaining_quantity)
  - Reduce filled quantity for both orders
  - Remove resting order from book when fully filled
  - Stop matching when incoming order is fully filled
  - Add remaining limit order quantity to book if not fully filled
- **Rationale**: Standard exchange behavior, realistic market simulation

### Execution Price
- **Decision**: Execution price is always the resting order's price
- **Rationale**:
  - Standard exchange practice
  - Price improvement for market orders
  - Predictable and fair pricing
- **Example**: Buy at 105 crossing sell at 100 executes at 100

### Market Order Behavior
- **Decision**: Market orders never rest on the book
- **Implementation**:
  - Market buy consumes asks from best ask until filled or ask side empty
  - Market sell consumes bids from best bid until filled or bid side empty
  - Unfilled quantity is discarded (not added to book)
  - No price limit for market orders
- **Rationale**: Standard exchange behavior, market orders are for immediate execution
- **Trade-offs**: Potential for partial fills when liquidity insufficient

### Order Lifecycle Events
- **Decision**: Generate MarketDataEvent for order lifecycle stages
- **Events**:
  - OrderAdded: When order is received
  - OrderPartiallyFilled: When order receives a partial fill
  - OrderFullyFilled: When order is completely filled
- **Rationale**: Provides visibility into order state without complex pub/sub infrastructure
- **Trade-offs**: Simple vector return instead of event queue (sufficient for current phase)

### Trade Events
- **Decision**: Generate Trade event for every successful match
- **Trade contains**:
  - buy_order_id: ID of the buy order
  - sell_order_id: ID of the sell order
  - execution_price: Price at which trade occurred (resting order price)
  - execution_quantity: Quantity traded
  - sequence: Monotonic sequence number for ordering
- **Rationale**: Complete trade record for audit and reconciliation

### Order Ownership and Mutation
- **Decision**: Copy orders into the engine, do not mutate caller's state
- **Implementation**:
  - `submit_order` copies the incoming order
  - Caller's Order object remains unchanged
  - All modifications happen on the internal copy
- **Rationale**: Prevents unexpected side effects, clear ownership semantics
- **Trade-offs**: Slight performance overhead from copying (acceptable for correctness phase)

### Limit Order Resting Behavior
- **Decision**: Limit orders rest on book after matching if not fully filled
- **Implementation**:
  - After matching attempt, if limit order has remaining quantity, add to book
  - Market orders never rest (price = 0 indicates market order)
- **Rationale**: Standard limit order behavior, provides liquidity to market

### Empty Price Level Removal
- **Decision**: Remove price levels immediately when last order is filled
- **Implementation**: Erase price level from map when deque becomes empty
- **Rationale**: Maintains book integrity, prevents stale price levels
- **Trade-offs**: Slight overhead from map erase (acceptable)

### Quantity Conservation
- **Decision**: Enforce strict quantity conservation invariants
- **Invariants**:
  - `filled <= quantity` for all orders
  - `remaining = quantity - filled` for all orders
  - No negative quantities
  - No quantity creation or disappearance
- **Verification**: Tests verify `submitted_quantity = executed_quantity + remaining_quantity`
- **Rationale**: Critical for financial correctness, prevents accounting errors

### Correctness Requirements
- **No negative quantities**: All quantities must be >= 0
- **Filled <= quantity**: Filled quantity cannot exceed total quantity
- **No duplicate order IDs**: Each order ID is unique in active book
- **No filled orders on book**: Fully filled orders are immediately removed
- **No empty price levels**: Price levels removed when last order filled
- **Best bid/ask correctness**: Must always reflect actual best prices
- **FIFO within price levels**: Older orders at same price fill first
- **Price priority across levels**: Better prices filled before worse prices
- **No quantity creation**: Total quantity cannot increase
- **No quantity disappearance**: Total quantity cannot decrease (except through execution)

### API Design
- **submit_order**: Main entry point for order submission
  - Returns pair of vectors: (trades, market_data_events)
  - Handles both limit and market orders
  - Performs matching and resting logic
- **add_limit_order**: Legacy method for direct order addition (no matching)
- **Query methods**: best_bid, best_ask, buy_side_empty, sell_side_empty, empty
- **Inspection methods**: get_orders_at_bid_price, get_orders_at_ask_price, get_all_buy_orders, get_all_sell_orders
- **Count methods**: buy_order_count, sell_order_count, buy_price_level_count, sell_price_level_count

### Sequence Number Management
- **Decision**: Use monotonic sequence numbers for event ordering
- **Implementation**: Increment sequence counter for each event (trade, market data)
- **Rationale**: Provides strict ordering for event streams
- **Trade-offs**: Cannot correlate with wall-clock time (acceptable for internal ordering)

### Testing Strategy
- **Unit tests**: 35+ comprehensive test cases covering:
  - Basic matching (crossing and non-crossing)
  - Full fills (incoming fills resting, resting fills incoming)
  - Partial fills (single and multiple orders)
  - Price priority (best price first)
  - Time priority (FIFO at same price)
  - Market orders (consumption, exhaustion, no resting)
  - Book state (empty levels, best bid/ask updates)
  - Quantity conservation (no loss or creation)
  - Order state (filled <= quantity, no negatives)
  - Trade events (correct information)
  - Market data events (lifecycle tracking)
- **Randomized testing**: Fixed seed RNG generates deterministic order sequences
- **Invariant verification**: Check all correctness invariants after each operation

### Non-Goals (Current Phase)
- No cancellation functionality
- No order modification
- No OrderId lookup index
- No performance optimization
- No concurrency
- No SPSC queue
- No networking
- No custom allocators
- No lock-free structures

## Order Cancellation Design (v0 - Phase 1.4)

### OrderId Index
- **Decision**: Use `std::unordered_map<OrderId, OrderLocation>` for O(1) order lookup
- **Rationale**:
  - Enables efficient cancellation by OrderId
  - OrderLocation tracks side, price, and deque_index for direct access
  - Critical for cancellation performance
- **Trade-offs**: Additional memory overhead and index maintenance complexity

### Index Maintenance
- **Add to index**: When order rests on book (add_resting_order)
- **Remove from index**: When order is cancelled (cancel_order)
- **Update after removal**: When order removed from middle of deque (update_order_indices_after_removal)
- **Rationale**: Ensures index always reflects current book state

### Cancellation Semantics
- **Cancel resting order**: Remove from book and index, generate OrderCancelled event
- **Cancel non-existent order**: Return false, no events generated
- **Cancel already filled order**: Return false, order already removed from book
- **Cancel market order**: Return false, market orders never indexed
- **Rationale**: Standard exchange behavior, clear error handling

### Index Consistency Invariant
- **Invariant**: Every resting order in book must be in index
- **Invariant**: Every entry in index must correspond to a resting order in book
- **Invariant**: Index size must equal total resting order count
- **Verification**: `verify_book_index_consistency()` helper function checks these invariants

### Deque Index Updates
- **Decision**: Update deque_index for all orders after removed index
- **Implementation**: When removing from middle of deque, increment indices of subsequent orders
- **Rationale**: Maintains correct mapping from OrderId to deque position
- **Trade-offs**: O(k) update cost where k is orders after removed order (acceptable for correctness baseline)

## Correctness Invariants

### Quantity Conservation Invariants
- **Order-level invariant**: `filled + remaining = quantity` for all orders
- **No overflow**: `filled <= quantity` always true
- **No underflow**: `remaining >= 0` always true
- **No creation**: Total quantity in system cannot increase
- **No disappearance**: Total quantity decreases only through execution or cancellation

### Order State Invariants
- **Filled quantity**: Never exceeds total quantity
- **Remaining quantity**: Never negative
- **Order ID uniqueness**: No duplicate OrderIds in active book
- **Sequence monotonicity**: Sequence numbers strictly increase

### Book Structure Invariants
- **Empty price levels**: Removed immediately when last order filled
- **Best bid/ask**: Always reflect actual best prices in book
- **Price level ordering**: Buy side descending, sell side ascending
- **FIFO within levels**: Orders at same price in insertion order

### Matching Invariants
- **Price priority**: Better prices always filled before worse prices
- **Time priority**: At same price, older orders fill first
- **Execution price**: Always resting order's price
- **Market order behavior**: Never rest on book, unfilled quantity discarded

### Index Consistency Invariants
- **Index completeness**: Every resting order in book must be in index
- **Index accuracy**: Every entry in index must correspond to a resting order in book
- **Index size**: Index size must equal total resting order count

## Mutex Concurrency Baseline (Phase 5B)

### Architecture
- **Decision**: Use global mutex to protect all shared mutable state
- **Implementation**: `MutexMultiInstrumentEngine` wraps `MultiInstrumentEngine` with `std::mutex`
- **Rationale**: Establishes a simple, measurable baseline for lock contention overhead
- **Trade-offs**: Coarse-grained locking serializes all operations, even independent instruments

### Protected State
The mutex protects:
- Instrument-to-book map (`std::unordered_map<InstrumentId, std::unique_ptr<OrderBookV1Pool>>`)
- Order routing index (`std::unordered_map<OrderId, InstrumentId>`)
- Global order ID counter (`OrderId next_order_id_`)
- Access to individual order books

### Global Order ID Generation
- **Decision**: Use mutex-protected increment for order ID generation
- **Rationale**: Simple and correct for baseline, no atomics needed
- **Trade-offs**: Lock contention on every order submission (acceptable for baseline)

### Contention Model
- **Single-threaded**: ~8% overhead from lock acquisition/release
- **Multi-threaded**: 2-4x slower due to mutex contention and thread overhead
- **Bottleneck**: Global mutex serializes all operations regardless of instrument distribution

## SPSC Ingestion Architecture (Phase 5)

### Architecture Overview
- **Decision**: Single-producer, single-consumer queue for order ingestion
- **Implementation**: `SpscMultiInstrumentEngine` with `SpscQueue<IngestionCommand, Capacity>`
- **Key Invariant**: ONLY the matching-engine consumer thread directly mutates order-book state
- **Rationale**: Eliminates lock contention by establishing single-writer ownership of book state

### Thread Roles
```
Producer thread
    ->
SPSC ring buffer (fixed capacity, power-of-two)
    ->
Single matching-engine thread (consumer)
    ->
Owns MultiInstrumentEngine (sole writer of order-book state)
```

### Producer Responsibilities
- Construct ingestion commands (SubmitLimitOrderCmd, SubmitMarketOrderCmd, CancelOrderCmd)
- Enqueue commands to SPSC queue
- Never directly call matching/book mutation functions
- Owns the write position in the queue exclusively

### Consumer Responsibilities
- Dequeue commands from SPSC queue
- Process commands by invoking the underlying `MultiInstrumentEngine`
- Owns the read position in the queue exclusively
- **Sole writer** of all order-book state

### SPSC Queue Design

#### Data Structure
- **Implementation**: Ring buffer with power-of-two capacity
- **Storage**: Pre-allocated array with placement new for construction
- **Capacity**: Fixed at compile time (template parameter), power-of-two preferred
- **Indexing**: Bitmask wraparound (capacity - 1) for efficiency

#### Ownership Model
- **Producer-owned state**: `write_index_` (only producer modifies)
- **Consumer-owned state**: `read_index_` (only consumer modifies)
- **Shared mutable state**: Ring buffer data (synchronized via acquire/release)
- **No mutex**: No locking primitives inside the queue
- **No condition_variable**: No waiting primitives inside the queue

#### Memory Ordering

##### Producer Enqueue
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

**Memory ordering rationale for enqueue:**
- `load(read_index_)` with **relaxed**: Producer only needs to know if there's space. No synchronization needed with consumer's reads because the producer only cares about the current value, not establishing a happens-before relationship.
- `store write_index_` with **release**: Critical for correctness. This release operation synchronizes with the consumer's acquire load. All writes to `buffer_[current_write]` happen-before this store, ensuring the value is fully constructed before the consumer sees the updated write index.

##### Consumer Dequeue
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

**Memory ordering rationale for dequeue:**
- `load(write_index_)` with **acquire**: Critical for correctness. This acquire operation synchronizes with the producer's release store. It ensures all writes to the value (from the producer) happen-before this load, so the consumer sees a fully constructed value.
- `store read_index_` with **relaxed**: Consumer owns this index exclusively. No synchronization needed because the producer only reads it with relaxed ordering to check for full condition.

##### Why Not seq_cst?
- **Unnecessary overhead**: `memory_order_seq_cst` provides a total ordering across all atomic operations, which is stronger than needed for SPSC.
- **Sufficient with acquire/release**: The acquire/release pair establishes the required happens-before relationship between producer's data publication and consumer's data consumption.
- **Performance**: Acquire/release is typically faster than seq_cst on most architectures.

##### Why Relaxed for Some Operations?
- **Single-writer indices**: Both `write_index_` and `read_index_` are modified by only one thread each.
- **Full/empty checks**: When checking if the queue is full (producer) or empty (consumer), we only need the current value, not a synchronized view.
- **No data race**: The producer never modifies `read_index_`, and the consumer never modifies `write_index_`, so relaxed reads are safe.

#### Happens-Before Relationship
The critical synchronization point:
1. Producer constructs value in `buffer_[current_write]`
2. Producer calls `write_index_.store(next_write, std::memory_order_release)`
3. **happens-before**: The store-release synchronizes with...
4. Consumer calls `write_index_.load(std::memory_order_acquire)`
5. Consumer reads value from `buffer_[current_read]`

This ensures that the consumer cannot see the updated write index until after the value is fully constructed and stored in the buffer.

#### Publication of Queue Data
- **Producer writes**: Value construction in buffer happens-before release store of write index
- **Consumer reads**: Acquire load of write index synchronizes with producer's release store
- **Result**: Consumer sees fully constructed values, no data races on buffer data

#### Consumer Progress Visibility
- **Consumer updates**: Consumer updates `read_index_` with relaxed store
- **Producer reads**: Producer reads `read_index_` with relaxed load to check for full condition
- **Visibility**: Relaxed is sufficient because producer only needs an approximate view to check for available space. If the producer overestimates (thinks queue is full when it's not), it will retry and succeed on the next attempt.

### Ingestion Command Design

#### Command Types
- **SubmitLimitOrderCmd**: Instrument ID + Order (side, price, quantity, sequence)
- **SubmitMarketOrderCmd**: Instrument ID + Order (side, quantity, sequence; price ignored)
- **CancelOrderCmd**: Order ID to cancel

#### Design Rationale
- **Type-safe encoding**: `std::variant` ensures only valid command types
- **No synchronization primitives**: Commands are plain data structures
- **Trivially movable**: Efficient transfer through queue (move semantics)
- **Self-contained**: All necessary information for matching engine processing
- **No external references**: Commands own their data, no lifetime issues

#### Ownership and Lifetime
- **Producer**: Constructs commands and enqueues them (move or copy)
- **Queue**: Transfers commands via move semantics when possible
- **Consumer**: Dequeues commands and processes them
- **No dangling references**: Commands are value types, no pointers to external state

### Matching Thread Ownership

#### Architectural Invariant
**ONLY the matching-engine consumer thread directly mutates order-book state.**

This invariant is critical for:
- **No lock contention**: Single-writer ownership eliminates the need for locks on book state
- **Clear ownership boundary**: Producer and consumer have distinct responsibilities
- **Correctness**: Prevents concurrent modification of book state

#### Producer Boundary
- Producer **cannot** call `MultiInstrumentEngine::submit_order()` directly
- Producer **cannot** call `MultiInstrumentEngine::cancel_order()` directly
- Producer **must** construct ingestion commands and enqueue them
- Producer **owns** the queue's write position exclusively

#### Consumer Boundary
- Consumer **owns** the `MultiInstrumentEngine` instance
- Consumer **owns** the queue's read position exclusively
- Consumer **only** mutates book state after dequeuing commands
- Consumer **processes** commands by invoking the engine

#### Enforcement
- **API design**: `SpscMultiInstrumentEngine` only exposes `submit_order()` and `cancel_order()` which enqueue commands
- **No direct access**: Underlying `MultiInstrumentEngine` is private (except test-only access)
- **Documentation**: Ownership invariant is clearly documented

### Capacity and Backpressure

#### Fixed Capacity
- **Decision**: Fixed-capacity queue, no dynamic resizing
- **Rationale**: Simpler implementation, predictable memory usage, no allocation during operation
- **Trade-offs**: Producer must handle full queue (backpressure)

#### Full Queue Behavior
- **Enqueue returns false**: Producer must retry or handle backpressure
- **No blocking**: Queue does not block or wait when full
- **Producer responsibility**: Implement retry logic or rate limiting if needed

#### Empty Queue Behavior
- **Dequeue returns false**: Consumer yields and retries
- **Busy-spin avoidance**: Consumer calls `std::this_thread::yield()` when empty
- **No blocking**: Queue does not block or wait when empty

### Non-Goals (Current Phase)
- No MPMC queues (multi-producer, multi-consumer)
- No per-instrument lock-free queues
- No dynamic queue resizing
- No networking or external market feeds
- No hazard pointers or RCU
- No custom allocators for queue
- No NUMA optimization
- No cache-line padding optimization (beyond correctness)
- No kernel-bypass networking
- No io_uring integration

### Comparison with Mutex Baseline

#### Expected Characteristics
- **Throughput**: SPSC may have higher throughput by eliminating lock contention
- **Latency**: SPSC may have lower tail latency by avoiding blocking on mutex
- **Contention**: SPSC eliminates producer-side contention (single producer)
- **Bottleneck**: SPSC shifts bottleneck to single matching thread (intentional)

#### Trade-offs
- **Single writer**: SPSC limits ingestion to one producer thread
- **Queue capacity**: Fixed capacity requires backpressure handling
- **Asynchronous API**: Producer does not get immediate trades/events (processed by consumer)
- **Complexity**: SPSC requires additional thread management and queue implementation

#### When SPSC is Advantageous
- High contention scenarios where mutex baseline slows down significantly
- Workloads where single producer is sufficient
- Scenarios where asynchronous processing is acceptable
- Systems that can tolerate queue capacity limits

#### When Mutex May Be Preferable
- Multiple producer threads needed
- Synchronous API required (immediate trades/events)
- Simple implementation priority over performance
- Low contention scenarios where mutex overhead is negligible
- **Index accuracy**: Every index entry must point to valid order location
- **Index size**: Must equal total resting order count
- **No stale entries**: Filled/cancelled orders removed from index

### Cross-Price Matching Rules
- **Buy crossing**: Buy order matches when `resting_ask_price <= incoming_buy_price`
- **Sell crossing**: Sell order matches when `resting_bid_price >= incoming_sell_price`
- **Same-price crossing**: Orders at same price DO cross (buy >= ask, sell <= bid)
- **Non-crossing**: Orders rest when price condition not met

### Test Coverage
- **116 total tests** across 7 test suites:
  - DomainTest: Basic type validation (24 tests)
  - OrderBookTest: Order book operations (23 tests)
  - MatchingTest: Matching logic (36 tests)
  - CancellationTest: Cancellation operations (22 tests)
  - EdgeCaseTest: Boundary conditions (10 tests)
  - PropertyBasedTest: Randomized testing (1 test with 10,000 operations)
- **Randomized testing**: Fixed seed (123456789) generates deterministic 10,000-operation sequences
- **Invariant verification**: All invariants checked after each operation in randomized test

## Version History
- v0.1.0 (2026-09-14): Initial project foundation
- v0.1.1 (2026-09-14): Core domain model implementation
- v0.1.2 (2026-09-16): Limit order book v0 (correctness baseline)
- v0.1.3 (2026-09-17): Matching engine v0 (correctness-first implementation)
- v0.1.4 (2026-09-23): Order cancellation and OrderId index
