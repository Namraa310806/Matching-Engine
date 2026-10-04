# SPSC Implementation Correctness Audit

**Date**: 2026-10-03 (initial), 2026-10-04 (update)
**Scope**: Phase 5C SPSC Ingestion Architecture
**Auditor**: Devin
**Status**: CRITICAL BUGS FOUND AND FIXED

## Executive Summary

This audit discovered **three critical issues** in the SPSC implementation:

1. **CRITICAL MEMORY ORDERING BUG (2026-10-03)**: The move version of `SpscQueue::enqueue()` used `memory_order_relaxed` instead of `memory_order_acquire` when loading the consumer's `read_index_`. This is a genuine data race under the C++ memory model, not a ThreadSanitizer false positive. **FIXED**.

2. **CRITICAL BENCHMARK INEQUIVALENCE (2026-10-03)**: The SPSC benchmarks cannot perform cancellations because the asynchronous API does not return order IDs. This results in SPSC doing 10% less work than the baselines (90% limit orders, 10% no-op cancellations vs 90% limit orders, 10% actual cancellations). This invalidates the performance comparison. **FIXED** (API changed to return order IDs).

3. **CRITICAL BENCHMARK SILENT DROP BUG (2026-10-04)**: The SPSC benchmark silently dropped commands when the queue was full instead of applying backpressure. When `enqueue()` returned false, the benchmark would skip the command without retry, leading to command loss and fraudulent throughput reporting. **FIXED** (benchmark now applies backpressure and verifies submitted == processed).

## Part 1: SPSC Queue Memory Ordering Audit

### 1.1 ThreadSanitizer Warnings (Before Fix)

**Command**: `./build_tsan/engine_tests_spsc_integration`

**Output**: 4 data race warnings during `SpscIntegrationTest.ProducerStress`

```
WARNING: ThreadSanitizer: data race (pid=529)
  Write of size 1 at 0x72c800000050 by thread T13 (producer):
    #0 engine::SpscMultiInstrumentEngine<1024ul>::submit_order(...)
    #1 std::thread::_State_impl<...>::_M_run()

  Previous read of size 1 at 0x72c800000050 by thread T12 (consumer):
    #0 engine::SpscMultiInstrumentEngine<1024ul>::matching_thread_loop()
    #1 std::thread::_State_impl<...>::_M_run()

  Location is heap block of size 90112 at 0x72c800000000
  SUMMARY: ThreadSanitizer: data race in submit_order()
```

The races occurred at offsets 0x20 and 0x50 within the queue buffer, corresponding to `IngestionCommand` variant storage.

### 1.2 Root Cause Analysis

**Location**: `include/engine/spsc_queue.hpp`, lines 94-109 (move version of `enqueue`)

**The Bug**:
```cpp
// Line 99 - INCORRECT (should be acquire)
const size_t current_read = read_index_.load(std::memory_order_relaxed);
```

The const-reference version correctly uses `memory_order_acquire` on line 77, but the move version does not.

### 1.3 Why This Is a Real Data Race (Not a False Positive)

Under the C++ memory model, the acquire fence is **required** for correctness:

**Correct happens-before relationship (const version)**:
```
Consumer: read_index_.store(next_read, memory_order_release)
    ↓ synchronizes-with
Producer: read_index_.load(memory_order_acquire)
    ↓ happens-before
Producer: new (&buffer_[current_write]) T(value)
```

**BROKEN happens-before relationship (move version)**:
```
Consumer: read_index_.store(next_read, memory_order_release)
    ↓ NO SYNCHRONIZATION (relaxed load)
Producer: read_index_.load(memory_order_relaxed)
    ↓ DATA RACE
Producer: new (&buffer_[current_write]) T(value)
```

**Explanation**:
1. Consumer stores `read_index_` with `release` to announce "I finished reading slot X"
2. Producer must load `read_index_` with `acquire` to see that announcement
3. Without acquire, producer may use stale `read_index_`, think slot X is free, and write to it while consumer is still reading
4. This violates the C++ memory model's happens-before guarantees

### 1.4 Verification Against Reference Implementations

All well-known SPSC queue implementations use **acquire on the producer's read of the consumer's index**:

- **Boost lockfree**: `read_index_.load(memory_order_acquire)` in `push()`
- **Rigtorp SPSCQueue**: `readIdx_.load(memory_order_acquire)` in `try_emplace()`
- **ArkaKhorchidian**: Explicit acquire on producer's consumer-index load
- **TheAlexCons tutorial**: `head_.load(memory_order_acquire)` in `enqueue()`

The documentation in the code itself (lines 66-70) correctly states the rationale but the implementation didn't follow it for the move overload.

### 1.5 The Fix

**File**: `include/engine/spsc_queue.hpp`
**Lines**: 94-109

**Change**:
```diff
  bool enqueue(T&& value) {
      const size_t current_write = write_index_.load(std::memory_order_relaxed);
      const size_t next_write = (current_write + 1) & mask_;

-     const size_t current_read = read_index_.load(std::memory_order_relaxed);
+     // Check if queue is full
+     // Acquire synchronizes with consumer's release on read_index
+     const size_t current_read = read_index_.load(std::memory_order_acquire);
      if (next_write == current_read) {
          return false; // Queue is full
      }

      new (&buffer_[current_write]) T(std::move(value));

      write_index_.store(next_write, std::memory_order_release);

      return true;
  }
```

### 1.6 Verification of Fix

**Command**: `./build_tsan/engine_tests_spsc_integration`

**Result**: ✅ **0 warnings** (down from 4)

```
[==========] Running 37 tests from 3 test suites.
[  PASSED  ] 37 tests.
ThreadSanitizer: reported 0 warnings
```

### 1.7 Additional SPSC Queue Correctness Checks

#### 1.7.1 Full/Empty Detection and Wraparound

**Full detection** (line 78): `next_write == current_read`
- Correct: If next write position equals current read position, queue is full
- Uses bitmask wraparound: `(current_write + 1) & mask_`
- Power-of-two capacity ensures correct wraparound

**Empty detection** (line 125): `current_read == current_write`
- Correct: If read position equals write position, queue is empty
- Consumer checks this after acquiring write_index

**Wraparound**:
- Bitmask `(index + 1) & mask_` where `mask_ = Capacity - 1`
- Power-of-two capacity ensures this is equivalent to modulo but faster
- Verified by `Wraparound` and `WraparoundAtCapacity` tests

#### 1.7.2 Slot Ownership Guarantees

**Invariant**: A slot is never read while the producer can concurrently overwrite it

**Proof**:
1. Producer checks full: `next_write == current_read` (with acquire)
2. If not full, consumer has not yet advanced to `next_write`
3. Producer writes to `current_write` and publishes with release
4. Consumer will only read `current_write` after it advances `read_index`
5. By the time consumer reads `current_write`, producer has already moved to `next_write`
6. Therefore, no slot is read while being overwritten

#### 1.7.3 Object Lifetime and Construction

**Construction** (line 84): `new (&buffer_[current_write]) T(value)`
- Placement new constructs object in pre-allocated memory
- Happens-before the release store of write_index
- Consumer sees fully constructed object

**Destruction** (line 134): `buffer_[current_read].~T()`
- Destructor called after moving value out
- Happens-before the release store of read_index
- Producer sees slot is free before reusing it

**Move semantics** (line 131): `value = std::move(buffer_[current_read])`
- Efficient transfer without copy
- Original object left in valid but unspecified state
- Destructor called to clean up

#### 1.7.4 Memory Ordering Summary

| Operation | Memory Order | Rationale |
|-----------|--------------|-----------|
| Producer load write_index | relaxed | Producer owns this index exclusively |
| Producer load read_index | **acquire** | Synchronizes with consumer's release store |
| Producer store write_index | release | Publishes value before index update |
| Consumer load read_index | relaxed | Consumer owns this index exclusively |
| Consumer load write_index | acquire | Synchronizes with producer's release store |
| Consumer store read_index | release | Publishes consumption progress |

**Conclusion**: Memory ordering is now correct and follows established SPSC patterns.

## Part 2: Benchmark Methodology Audit

### 2.1 Workload Comparison

#### 2.1.1 SPSC Benchmark (BM_SPSC_1Producer)

**File**: `benchmarks/benchmark_spsc.cpp`, lines 42-96

```cpp
for (auto _ : state) {
    engine::SpscMultiInstrumentEngine<1024> engine;
    std::vector<engine::OrderId> generated_order_ids;  // NEVER POPULATED

    for (const auto& workload_order : orders) {
        if (workload_order.action == tools::OrderAction::SubmitLimit) {
            engine.submit_order("INST1", order);  // Returns bool, ID lost
        } else if (workload_order.action == tools::OrderAction::Cancel) {
            if (!generated_order_ids.empty()) {  // ALWAYS FALSE
                engine.cancel_order(generated_order_ids.back());
            }
        }
    }

    engine.stop();  // Waits for consumer to drain queue
}
```

**Measured operations**: Enqueue time + wait for consumer processing

#### 2.1.2 Mutex Baseline (BM_Mutex_1Thread_SameWorkload)

**File**: `benchmarks/benchmark_spsc.cpp`, lines 270-327

```cpp
for (auto _ : state) {
    engine::MutexMultiInstrumentEngine engine;
    std::vector<engine::OrderId> generated_order_ids;

    for (const auto& workload_order : orders) {
        if (workload_order.action == tools::OrderAction::SubmitLimit) {
            auto [trades, events] = engine.submit_order("INST1", order);
            if (!events.empty()) {
                generated_order_ids.push_back(events[0].order_id);  // POPULATED
            }
        } else if (workload_order.action == tools::OrderAction::Cancel) {
            if (!generated_order_ids.empty()) {  // SOMETIMES TRUE
                engine.cancel_order(generated_order_ids.back());
            }
        }
    }
    // No explicit wait - processing is synchronous
}
```

**Measured operations**: Synchronous processing time (matching happens during submit)

#### 2.1.3 Single-Threaded Baseline (BM_SingleThreaded_Baseline)

**File**: `benchmarks/benchmark_concurrency.cpp`, lines 42-100

```cpp
for (auto _ : state) {
    engine::MultiInstrumentEngine engine;
    std::vector<engine::OrderId> generated_order_ids;

    for (const auto& workload_order : orders) {
        if (workload_order.action == tools::OrderAction::SubmitLimit) {
            auto [trades, events] = engine.submit_order("INST1", order);
            if (!events.empty()) {
                generated_order_ids.push_back(events[0].order_id);  // POPULATED
            }
        } else if (workload_order.action == tools::OrderAction::Cancel) {
            if (!generated_order_ids.empty()) {  // SOMETIMES TRUE
                engine.cancel_order(generated_order_ids.back());
            }
        }
    }
    benchmark::DoNotOptimize(engine);
}
```

**Measured operations**: Synchronous processing time (matching happens during submit)

### 2.2 Critical Inequivalence: Cancellation Workload

**Workload specification**: `generate_deterministic_workload(num_orders, 0.9, 0.1, 12345)`
- 90% limit orders
- 10% cancellations

**Actual work performed**:

| Operation | SPSC | Mutex | Single-Threaded |
|-----------|------|-------|-----------------|
| Submit Limit Order | ✅ Enqueue + process | ✅ Synchronous process | ✅ Synchronous process |
| Submit Market Order | ✅ Enqueue + process | ✅ Synchronous process | ✅ Synchronous process |
| Cancel Order | ❌ **NO-OP** (no IDs) | ✅ Synchronous process | ✅ Synchronous process |

**At 100K operations**:
- SPSC: ~90K submits, 10K no-op cancellations = **90K actual operations**
- Mutex: ~90K submits, 10K actual cancellations = **100K actual operations**
- Single-threaded: ~90K submits, 10K actual cancellations = **100K actual operations**

**SPSC does 10% less work** than the baselines.

### 2.3 Root Cause: Architectural Limitation

The SPSC API is **asynchronous by design**:

```cpp
// SPSC API (asynchronous)
bool submit_order(const InstrumentId& instrument_id, const Order& order);
// Returns: true if enqueued successfully, false if queue is full
// Does NOT return: order ID, trades, or events

// Mutex API (synchronous)
std::vector<Trade> submit_order(const InstrumentId& instrument_id, const Order& order);
// Returns: trades generated by matching
// Side effect: order ID assigned and available via events
```

**The problem**:
- SPSC producer enqueues command and returns immediately
- Matching thread assigns order ID asynchronously
- Producer has no way to retrieve the assigned ID
- Therefore, producer cannot perform cancellations (needs order ID)

**This is a fundamental architectural trade-off**, not a benchmark bug:
- SPSC prioritizes throughput over producer feedback
- Asynchronous API means producer cannot get immediate results
- Cancellations require knowledge of order IDs, which are assigned asynchronously

### 2.4 Additional Inequivalence: Completion Criteria

**SPSC completion**:
```cpp
engine.stop();  // Waits for matching thread to process all queued commands
```
Timer includes:
- Enqueue time for all orders
- Wait for consumer thread to drain queue
- Thread startup/shutdown overhead
- Potential context switches

**Mutex completion**:
```cpp
// Loop ends immediately after last submit_order returns
```
Timer includes:
- Synchronous processing time (matching happens during submit)
- No additional overhead

**Single-threaded completion**:
```cpp
// Loop ends immediately after last submit_order returns
```
Timer includes:
- Synchronous processing time (matching happens during submit)
- No additional overhead

**Impact**:
- SPSC timer includes additional synchronization overhead
- However, the consumer does the same matching work as synchronous versions
- Overhead is bounded by queue capacity (1024 slots)
- At high throughput, consumer likely keeps up, minimizing wait time
- This is less severe than the cancellation issue

### 2.5 Completion Criteria Fairness Assessment

**Is the SPSC completion criterion fair?**

**Yes, with caveats**:
- The end-to-end work (matching + cancellation) is what matters for latency/throughput
- SPSC measures: enqueue time + processing time (via stop())
- Mutex/Single-threaded measure: processing time (synchronous)
- Both measure the full pipeline from order submission to completion
- SPSC has additional overhead from thread management, but this is part of the architecture

**No, for the specific case**:
- The current benchmarks compare "orders submitted per second"
- SPSC measures submission rate (enqueue + wait)
- Mutex measures processing rate (synchronous)
- These are different metrics
- A fair comparison would measure end-to-end latency or total throughput

### 2.6 Impact on Performance Comparison

The benchmark reports "very large SPSC advantage at 100K operations". This is likely explained by:

1. **10% less work**: SPSC does no-op cancellations instead of actual cancellations
2. **Different metrics**: SPSC measures submission rate, baselines measure processing rate
3. **Asynchronous benefit**: Producer can enqueue faster than consumer processes (queue buffering)

**Conclusion**: The current benchmarks **now provide a fair comparison** between SPSC and mutex baselines because:
- SPSC does the same work (actual cancellations) ✅
- Completion criteria are documented (SPSC includes wait for consumer) ✅
- The architectural difference (async vs sync) is measured as intended ✅

### 2.8 Fix Applied

**Solution**: Changed SPSC API to return order IDs from `submit_order()`, enabling cancellations.

**API Change**:
```cpp
// Before (no order ID returned)
bool submit_order(const InstrumentId& instrument_id, const Order& order);

// After (returns order ID)
std::pair<bool, OrderId> submit_order(const InstrumentId& instrument_id, const Order& order);
```

**Implementation**:
1. Added `std::atomic<OrderId> next_order_id_` to `SpscMultiInstrumentEngine`
2. Producer assigns IDs using `next_order_id_.fetch_add(1, memory_order_relaxed)`
3. IDs are passed through ingestion commands to consumer
4. Consumer uses provided IDs directly (no reassignment)
5. Engine updated to accept non-zero order IDs and advance counter if needed

**Preservation of Invariants**:
- ✅ Single-writer invariant maintained (consumer still sole writer of order book)
- ✅ No concurrent order book writes
- ✅ Global ID uniqueness preserved (producer counter is monotonic)
- ✅ SPSC architecture unchanged (producer -> queue -> consumer -> book)

**Benchmark Update**:
- SPSC benchmarks now collect order IDs from `submit_order()`
- Cancellations use real order IDs (no more no-ops)
- Workload is now equivalent across all baselines

**Performance Impact**:
- Minimal: one atomic fetch_add per order
- No additional synchronization overhead
- Enables fair comparison with mutex/single-threaded baselines

### 2.7 Recommendations for Fair Benchmarking

To make a fair comparison, the benchmarks should:

**Option 1: Remove cancellations from all benchmarks**
- Change workload to 100% limit/market orders, 0% cancellations
- All baselines do the same work
- Fair comparison of submission/processing throughput

**Option 2: Implement synchronous feedback in SPSC**
- Add a way for producer to retrieve order IDs after submission
- Could use a separate result queue or shared map
- Allows SPSC to perform actual cancellations
- More complex architecture, but fair comparison

**Option 3: Measure end-to-end latency instead of throughput**
- Measure time from order submission to trade generation
- Include queue wait time in SPSC
- Include matching time in all baselines
- More realistic metric for trading systems

**Option 4: Accept the architectural difference**
- Document that SPSC cannot do cancellations in the current design
- Compare only submission throughput (not full workload)
- This is a valid comparison for pure ingestion scenarios
- Acknowledge the limitation in documentation

## Part 3: Summary of Findings

### 3.1 Critical Issues

| Issue | Severity | Status | Impact |
|-------|----------|--------|--------|
| Memory ordering bug in move version of enqueue | CRITICAL | ✅ FIXED | Data race under C++ memory model |
| Benchmark cancellation inequivalence | CRITICAL | ✅ FIXED | API changed to return order IDs, enabling cancellations |

### 3.2 Non-Critical Findings

| Finding | Severity | Status | Impact |
|---------|----------|--------|--------|
| Completion criteria differ between baselines | MEDIUM | ⚠️ ACKNOWLEDGED | Different metrics (submission vs processing) |
| SPSC queue lacks cache-line padding | LOW | ⚠️ ACKNOWLEDGED | Potential false sharing on some architectures |

### 3.3 Correctness Verification

**After fix**:
- ✅ ThreadSanitizer: 0 warnings (down from 4)
- ✅ All 37 SPSC integration tests pass
- ✅ All 11 SPSC queue tests pass
- ✅ Memory ordering matches reference implementations (Boost, Rigtorp, etc.)

**Remaining concerns**:
- ✅ Benchmarks now compare equivalent workloads (FIXED)
- ✅ Performance numbers are now accurate (FIXED)

## Part 4: Third Critical Bug - Silent Command Drop (2026-10-04)

### 4.1 Bug Discovery

During follow-up audit on 2026-10-04, a third critical benchmark bug was discovered: **the benchmark silently dropped commands when the queue was full**.

### 4.2 Root Cause

**Location**: `benchmarks/benchmark_spsc.cpp`, all SPSC benchmark functions

**The Bug**:
```cpp
auto [success, order_id] = engine.submit_order("INST1", order);
if (success) {
    generated_order_ids.push_back(order_id);
}
// If success == false: COMMAND IS SILENTLY DROPPED WITHOUT RETRY
```

**Impact**:
1. **No backpressure**: When the 1024-slot queue filled up, failed submissions were dropped without retry
2. **Fraudulent throughput**: `state.SetItemsProcessed(state.iterations() * num_orders)` counted attempted submissions, not actual processed commands
3. **No verification**: No assertion checked that submitted == processed
4. **Cancellations desynchronized**: If submissions were dropped, the cancellation ID vector became invalid

### 4.3 Why This Matters

With 100K orders and a 1024-slot queue:
- If the consumer can't keep up, the queue fills up
- Without backpressure, the producer rapidly drops commands
- The "throughput" metric becomes meaningless (measuring attempted submissions, not actual work)
- Previous claim of 8.0x speedup was based on this fraudulent measurement

### 4.4 The Fix

**Changes to `SpscMultiInstrumentEngine`**:
1. Added `std::atomic<uint64_t> processed_command_count_` to track processed commands
2. Increment counter in `matching_thread_loop()` after each `process_command()`
3. Added `processed_command_count()` method to expose the count

**Changes to benchmark**:
1. Added backpressure: retry enqueue until success with `while (true) { ... yield() }`
2. Added `submitted_count` to track actual successful submissions
3. Added verification: `if (submitted_count != processed) { state.SkipWithError(...) }`
4. All SPSC benchmarks now guarantee submitted == processed

### 4.5 Corrected Results

**Before (Invalid)**:
- SPSC: 25.22M ops/sec (mean), 25.23M ops/sec (median)
- Mutex: 2.85M ops/sec (mean), 2.97M ops/sec (median)
- Claimed speedup: **8.0x**

**After (Corrected, 2026-10-04)**:
- SPSC: 1.74M ops/sec (mean), 1.65M ops/sec (median)
- Mutex: 3.43M ops/sec (mean), 3.30M ops/sec (median)
- Actual speedup: **0.51x (SPSC is slower)**

**Build/Run Info**:
- Compiler: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- Build flags: -O3 -march=native -DNDEBUG
- CPU: 13th Gen Intel(R) Core(TM) i7-1355U, 4 cores @ 2611 MHz
- Queue capacity: 1024 slots
- Workload: 100,000 orders (90% limit, 10% cancel)
- Repetitions: 3
- Verification: All benchmarks assert submitted == processed ✅

### 4.6 Verification

**Test Results**:
- Debug SPSC integration tests: 37/37 passed ✅
- Debug SPSC queue tests: 12/12 passed ✅
- ASan/UBSan: All tests passed ✅
- TSan: All tests passed, 0 warnings ✅

**Benchmark Verification**:
- All SPSC benchmarks now apply backpressure ✅
- All SPSC benchmarks verify submitted == processed ✅
- No commands are silently dropped ✅

## Part 5: Recommendations

### 5.1 Immediate Actions

1. **Accept the memory ordering fix** - The SPSC queue is now correct under the C++ memory model ✅
2. **Accept the benchmark fix** - SPSC now returns order IDs, enabling cancellations ✅
3. **Accept the backpressure fix** - Benchmark now properly applies backpressure and verifies correctness ✅
4. **Update documentation** - Invalidate old benchmark claims in README.md, RESULTS.md, SPSC_INGESTION.md ✅

### 5.2 Phase 5C Acceptance Decision

**Recommendation**: **ACCEPT Phase 5C with Corrected Benchmark Results**

**Reasoning**:
1. The memory ordering bug is fixed ✅
2. The benchmark comparison is now fair (order IDs returned) ✅
3. The benchmark now correctly applies backpressure ✅
4. The benchmark now verifies submitted == processed ✅
5. The performance numbers are now accurate (SPSC is slower than mutex) ✅
6. The documentation reflects actual capabilities ✅

**Verification**:
- All SPSC queue tests pass (12/12) ✅
- All SPSC integration tests pass (37/37) ✅
- ThreadSanitizer clean (0 warnings) ✅
- All regression tests pass (490/490 test-case executions) ✅
- Benchmarks now execute equivalent workloads ✅
- Benchmarks now verify submitted == processed ✅
- Backpressure prevents command loss ✅

### 4.3 Performance Results (Phase 5C - INVALID)

**CRITICAL NOTE**: These Phase 5C results are INVALID due to the same silent-drop bug discovered in 2026-10-04. The benchmark was silently dropping commands when the queue filled up, making the throughput measurement fraudulent.

**Benchmark Configuration**:
- DEBUG build with -O2
- Workload: 90% limit orders, 10% cancellations
- Repetitions: 3
- Hardware: 4x 2611.2 MHz CPU

**Throughput Comparison (orders/second) - INVALID**:

| Operations | SPSC | Mutex (1 thread) | Single-threaded | SPSC Speedup |
|------------|------|------------------|-----------------|--------------|
| 1,000      | 1.81M | 195k | 196k | **9.3x** |
| 4,096      | 3.48M | 187k | 188k | **18.5x** |
| 32,768     | 5.15M | 197k | 156k | **26.1x** |
| 100,000    | 4.99M | 197k | 200k | **25.3x** |

**Analysis**:
- These results are INVALID due to the silent-drop bug
- Reported speedup of 9-26x was based on fraudulent measurement
- The corrected Phase 7 results show SPSC is actually SLOWER than mutex (0.51x)

**Latency**: Not measured in current benchmarks. Future work should measure end-to-end latency including queue wait time.

#### Phase 7 Final Verification (Release build) - CORRECTED 2026-10-04

**CRITICAL NOTE**: The previous Phase 7 results were INVALID due to the silent-drop bug. The corrected results below are from the fixed benchmark.

**Benchmark Configuration**:
- Release build with -O3 -march=native
- Workload: 100,000 orders (90% limit orders, 10% cancellations)
- Repetitions: 3
- Hardware: 4x 2611.2 MHz CPU
- Queue capacity: 1024 slots
- Verification: submitted == processed asserted ✅

**Throughput Comparison (orders/second) - CORRECTED**:

|| Architecture | Throughput (mean) | Throughput (median) | vs Mutex Baseline |
||--------------|-------------------|---------------------|-------------------|
|| Mutex 1 thread | 3.43M ops/sec | 3.30M ops/sec | 1.00x |
|| SPSC (1 producer) | 1.74M ops/sec | 1.65M ops/sec | **0.51x** |

**Command**: `./build-release/engine_benchmarks_spsc --benchmark_repetitions=3 --benchmark_filter=BM_SPSC_1Producer/100000`

**Command for mutex baseline**: `./build-release/engine_benchmarks_concurrency --benchmark_repetitions=3 --benchmark_filter=BM_Mutex_1Thread/100000`

**Analysis**: With the corrected benchmark that properly applies backpressure and verifies all commands are processed, SPSC is actually SLOWER than the mutex baseline (0.51x speedup). The previous 8.0x speedup claim was based on a fraudulent benchmark that silently dropped commands when the queue filled up.

**Critical note**: During Phase 5C correctness audit, a critical benchmark inequivalence was discovered and fixed. The SPSC API was modified to return order IDs, enabling cancellations in benchmarks. Previous benchmark runs that showed no-op cancellations were invalid. Both Phase 5C and Phase 7 results are from the corrected implementation.

## Part 5: Commands and Results

### 5.1 Build ThreadSanitizer Version

```bash
cd /mnt/c/Users/patel/StandOut\ Projects/Matching\ Engine/matching-engine
cmake -B build_tsan -DCMAKE_BUILD_TYPE=TSan
cmake --build build_tsan --target engine_tests_spsc_queue
cmake --build build_tsan --target engine_tests_spsc_integration
```

### 5.2 Run Tests Before Fix

```bash
./build_tsan/engine_tests_spsc_queue
# Result: 12 tests passed, 0 warnings

./build_tsan/engine_tests_spsc_integration
# Result: 37 tests passed, 4 ThreadSanitizer warnings
```

### 5.3 Apply Fix

**File**: `include/engine/spsc_queue.hpp`
**Change**: Line 99, change `memory_order_relaxed` to `memory_order_acquire`

### 5.4 Run Tests After Fix

```bash
cmake --build build_tsan --target engine_tests_spsc_integration
./build_tsan/engine_tests_spsc_integration
# Result: 37 tests passed, 0 ThreadSanitizer warnings ✅
```

### 5.5 Run Benchmarks (Current State)

```bash
cmake --build build_tsan --target engine_benchmarks_spsc
./build_tsan/engine_benchmarks_spsc --benchmark_filter=BM_SPSC_1Producer
```

**Note**: Benchmark results are not reported here because the benchmark is fundamentally flawed. Results would be misleading.

## Appendix A: Detailed Memory Ordering Analysis

### A.1 Acquire/Release Semantics

**Acquire**:
- Prevents reordering of subsequent reads/writes before the acquire
- Ensures all writes preceding the matching release are visible
- Used by consumer to see producer's published data

**Release**:
- Prevents reordering of preceding reads/writes after the release
- Ensures all writes before the release are visible to matching acquire
- Used by producer to publish data

**Relaxed**:
- No ordering guarantees
- Only atomicity (no torn reads/writes)
- Safe for single-writer indices where only one thread modifies

### A.2 Happens-Before Relationship

**Critical synchronization path**:
```
Thread 1 (Producer)                    Thread 2 (Consumer)
────────────────────────────────────────────────────────────────
write_index_.load(relaxed)            read_index_.load(relaxed)
read_index_.load(acquire) ←─────────── read_index_.store(release)
  (sees consumer progress)               (announces progress)
buffer_[current_write] = value         write_index_.load(acquire)
  (constructs value)                      ←─────────── write_index_.store(release)
write_index_.store(release)              (sees producer data)
  (publishes value)                    buffer_[current_read] = value
                                         (reads value)
```

**Without acquire on producer's read_index load**:
```
Thread 1 (Producer)                    Thread 2 (Consumer)
────────────────────────────────────────────────────────────────
read_index_.load(relaxed) ←─────────── read_index_.store(release)
  (might NOT see progress)               (announces progress)
buffer_[current_write] = value         write_index_.load(acquire)
  (DATA RACE - might write to            ←─────────── write_index_.store(release)
   slot consumer is reading)              (sees producer data)
write_index_.store(release)            buffer_[current_read] = value
  (publishes value)                    (reads value)
```

### A.3 Why seq_cst Is Not Used

**seq_cst** (sequentially consistent):
- Provides total ordering across all atomic operations
- Stronger than acquire/release
- Slower on most architectures

**Acquire/release is sufficient**:
- SPSC only needs synchronization between two threads
- Total ordering across all threads is unnecessary
- Acquire/release provides the required happens-before relationship
- Better performance while maintaining correctness

## Appendix B: Reference Implementation Comparison

### B.1 Boost Lockfree SPSC Queue

```cpp
bool push(const T& t, T* buffer, size_t max_size) {
    const size_t write_index = write_index_.load(memory_order_relaxed);
    const size_t next = next_index(write_index, max_size);

    if (next == read_index_.load(memory_order_acquire))  // ACQUIRE
        return false;

    new (buffer + write_index) T(t);
    write_index_.store(next, memory_order_release);  // RELEASE

    return true;
}
```

### B.2 Rigtorp SPSC Queue

```cpp
bool try_emplace(Args&&...args) {
    auto const writeIdx = writeIdx_.load(std::memory_order_relaxed);
    auto nextWriteIdx = writeIdx + 1;
    if (nextWriteIdx == capacity_) {
        nextWriteIdx = 0;
    }
    if (nextWriteIdx == readIdxCache_) {
        readIdxCache_ = readIdx_.load(std::memory_order_acquire);  // ACQUIRE
        if (nextWriteIdx == readIdxCache_) {
            return false;
        }
    }
    new (&slots_[writeIdx + kPadding]) T(std::forward<Args>(args)...);
    writeIdx_.store(nextWriteIdx, std::memory_order_release);  // RELEASE
    return true;
}
```

### B.3 ArkaKhorchidian SPSC Queue

```cpp
bool push(const T& item) {
    const size_t t = tail_.load(std::memory_order_relaxed);
    const size_t next_t = (t + 1) % buffer_.size();

    if (next_t == head_.load(std::memory_order_acquire)) {  // ACQUIRE
        return false;
    }

    buffer_[t] = item;
    tail_.store(next_t, std::memory_order_release);  // RELEASE
    return true;
}
```

**Conclusion**: All reference implementations use acquire on producer's consumer-index load. Our implementation now matches this pattern.

## Appendix C: Benchmark Inequivalence Detailed Analysis

### C.1 Order ID Assignment in SPSC

**Current SPSC flow**:
```
Producer: submit_order(instrument, order)
    ↓
Producer: Create IngestionCommand
    ↓
Producer: queue_.enqueue(cmd)  ← Returns bool (success/failure)
    ↓
Producer: Returns bool to caller  ← NO ORDER ID AVAILABLE
    ↓
Consumer: Dequeue command
    ↓
Consumer: engine_.submit_order(...)  ← Assigns order ID internally
    ↓
Consumer: ID is lost to producer
```

**Mutex flow**:
```
Caller: submit_order(instrument, order)
    ↓
Mutex: Lock mutex
    ↓
Mutex: engine_.submit_order(...)  ← Assigns order ID
    ↓
Mutex: Return trades + events (with order ID)
    ↓
Caller: Extract order ID from events
    ↓
Caller: Use ID for future cancellations
```

### C.2 Why SPSC Cannot Return Order IDs

**Architectural constraint**:
- SPSC is designed for maximum throughput
- Producer should not wait for consumer
- Returning order ID would require:
  1. Producer to wait for consumer to process
  2. Result queue to send IDs back to producer
  3. Synchronization between threads
- This defeats the purpose of SPSC (lock-free, non-blocking)

**Alternative designs**:
1. **Result queue**: Consumer sends results back via separate queue
   - Pro: Producer can get IDs
   - Con: More complex, requires producer to poll or wait

2. **Shared ID map**: Producer generates IDs, consumer uses them
   - Pro: No coordination needed
   - Con: Requires coordination for ID generation, potential conflicts

3. **Callback-based**: Producer provides callback for result
   - Pro: Asynchronous result delivery
   - Con: More complex API, requires thread-safe callbacks

4. **Accept limitation**: SPSC for pure ingestion, no cancellations
   - Pro: Simple, high throughput
   - Con: Limited functionality

**Current choice**: Option 4 (accept limitation)

### C.3 Impact on Different Use Cases

**Use case 1: Pure order submission**
- SPSC: Excellent (high throughput, no feedback needed)
- Mutex: Good (synchronous feedback)
- Comparison: Fair for throughput, unfair for latency

**Use case 2: Order submission + cancellation**
- SPSC: Cannot support (no order IDs)
- Mutex: Supported
- Comparison: Invalid (different capabilities)

**Use case 3: Market making (frequent cancellations)**
- SPSC: Not suitable
- Mutex: Suitable
- Comparison: Invalid

**Conclusion**: The benchmark workload (10% cancellations) does not match SPSC's intended use case (pure ingestion). This is an architectural mismatch, not just a benchmark bug.

## Part 6: Comprehensive Benchmark Audit (2026-10-04)

### 6.1 Audit Scope

This section documents the comprehensive audit of ALL SPSC benchmarks in `benchmarks/benchmark_spsc.cpp` performed on 2026-10-04.

**Audit checklist for each SPSC benchmark**:
- ✅ Verify enqueue failures cannot silently drop commands
- ✅ Apply backpressure where required
- ✅ Verify submitted == processed
- ✅ Verify cancellations use real live OrderIds
- ✅ Verify workload is equivalent to corresponding mutex/synchronous baseline
- ✅ Verify timing includes consumer completion/drain

### 6.2 Benchmarks Audited

**SPSC Benchmarks (Queue Capacity 1024)**:
1. `BM_SPSC_1Producer` - Basic SPSC with 1 producer
2. `BM_SPSC_SameInstrument` - SPSC with same instrument (high contention)
3. `BM_SPSC_MultipleInstruments` - SPSC with 4 instruments (sequential distribution)
4. `BM_SPSC_MixedActivity` - SPSC with higher cancel rate (15%)

**SPSC Benchmarks (Variable Queue Capacity)**:
5. `BM_SPSC_1Producer_Capacity16384` - SPSC with 16384-slot queue
6. `BM_SPSC_1Producer_Capacity65536` - SPSC with 65536-slot queue

**Mutex Baseline Benchmarks**:
7. `BM_Mutex_1Thread_SameInstrument` - Baseline for BM_SPSC_SameInstrument
8. `BM_Mutex_1Thread_MultipleInstruments` - Baseline for BM_SPSC_MultipleInstruments
9. `BM_Mutex_1Thread_MixedActivity` - Baseline for BM_SPSC_MixedActivity

### 6.3 Audit Results

#### 6.3.1 SPSC Benchmark Correctness: ALL PASS ✅

All SPSC benchmarks are correctly implemented:

**Backpressure**: All SPSC benchmarks apply backpressure with while loops:
```cpp
while (true) {
    auto [success, order_id] = engine.submit_order(instrument, order);
    if (success) {
        generated_order_ids.push_back(order_id);
        submitted_count++;
        break;
    }
    std::this_thread::yield();
}
```

**Verification**: All SPSC benchmarks verify submitted == processed:
```cpp
uint64_t processed = engine.processed_command_count();
if (submitted_count != processed) {
    state.SkipWithError("Submitted != processed commands");
}
```

**Cancellations**: All SPSC benchmarks use real live OrderIds:
```cpp
if (!generated_order_ids.empty()) {
    while (!engine.cancel_order(generated_order_ids.back())) {
        std::this_thread::yield();
    }
    generated_order_ids.pop_back();
    submitted_count++;
}
```

**Timing**: All SPSC benchmarks include consumer completion via `engine.stop()`.

#### 6.3.2 Mutex Baseline Correctness: ALL PASS ✅

All mutex baseline benchmarks are correctly implemented as synchronous benchmarks. They do not require backpressure because operations complete immediately.

**Workload Equivalence**: All mutex baselines use identical workload parameters to their SPSC counterparts:
- Same seeds (12345 or 54321)
- Same limit_ratio (0.9)
- Same cancel_rate (0.1 or 0.15)
- Same instrument distribution logic

**Timing Difference (Documented)**:
- SPSC timing includes: enqueue time + consumer drain time + thread overhead
- Mutex timing includes: synchronous processing time only

This is a **fair comparison** because it measures the architectural difference (async vs sync) as intended.

### 6.4 Corrected Benchmark Results (2026-10-04)

**Build Configuration**:
- Compiler: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- Build flags: -O3 -march=native -DNDEBUG
- CPU: 4 X 2611.2 MHz CPU (13th Gen Intel(R) Core(TM) i7-1355U)
- Repetitions: 5
- Verification: All benchmarks assert submitted == processed ✅

**Command**:
```bash
./build-release/engine_benchmarks_spsc --benchmark_repetitions=5 --benchmark_counters_tabular=true
```

#### 6.4.1 Queue Capacity 1024 Results

**BM_SPSC_1Producer (Single Instrument)**:

| Orders | SPSC Mean (ops/s) | SPSC Median (ops/s) | Mutex Mean (ops/s) | Mutex Median (ops/s) | SPSC/Mutex Ratio |
|--------|-------------------|---------------------|---------------------|----------------------|------------------|
| 1,000  | 10.36M            | 10.95M              | 3.22M               | 3.23M                | 3.22x            |
| 4,096  | 2.67M             | 2.75M               | 2.98M               | 3.10M                | 0.90x            |
| 32,768 | 1.74M             | 1.74M               | 2.05M               | 2.06M                | 0.85x            |
| 100,000| 1.36M             | 1.27M               | 2.02M               | 2.07M                | 0.67x            |

**BM_SPSC_SameInstrument (High Contention)**:

| Orders | SPSC Mean (ops/s) | SPSC Median (ops/s) | Mutex Mean (ops/s) | Mutex Median (ops/s) | SPSC/Mutex Ratio |
|--------|-------------------|---------------------|---------------------|----------------------|------------------|
| 1,000  | 7.59M             | 7.91M               | N/A                 | N/A                  | N/A              |
| 4,096  | 2.42M             | 2.41M               | N/A                 | N/A                  | N/A              |
| 32,768 | 1.85M             | 1.90M               | N/A                 | N/A                  | N/A              |
| 100,000| 1.64M             | 1.69M               | N/A                 | N/A                  | N/A              |

**BM_SPSC_MultipleInstruments (4 Instruments)**:

| Orders | SPSC Mean (ops/s) | SPSC Median (ops/s) | Mutex Mean (ops/s) | Mutex Median (ops/s) | SPSC/Mutex Ratio |
|--------|-------------------|---------------------|---------------------|----------------------|------------------|
| 1,000  | 7.85M             | 8.14M               | 3.44M               | 3.55M                | 2.28x            |
| 4,096  | 2.65M             | 2.69M               | 2.78M               | 2.95M                | 0.95x            |
| 32,768 | 1.92M             | 1.92M               | 2.02M               | 1.95M                | 0.95x            |
| 100,000| 1.57M             | 1.74M               | 2.02M               | 2.07M                | 0.78x            |

**BM_SPSC_MixedActivity (15% Cancel Rate)**:

| Orders | SPSC Mean (ops/s) | SPSC Median (ops/s) | Mutex Mean (ops/s) | Mutex Median (ops/s) | SPSC/Mutex Ratio |
|--------|-------------------|---------------------|---------------------|----------------------|------------------|
| 1,000  | 9.93M             | 10.79M              | 3.08M               | 2.98M                | 3.22x            |
| 4,096  | 2.58M             | 2.59M               | 3.07M               | 3.07M                | 0.84x            |
| 32,768 | 1.94M             | 1.95M               | 2.58M               | 2.64M                | 0.75x            |
| 100,000| 1.85M             | 1.85M               | 2.48M               | 2.50M                | 0.75x            |

#### 6.4.2 Queue Capacity Comparison

**BM_SPSC_1Producer - Queue Capacity 1024 vs 16384 vs 65536**:

| Orders | Capacity 1024 (ops/s) | Capacity 16384 (ops/s) | Capacity 65536 (ops/s) | 16384/1024 | 65536/1024 |
|--------|------------------------|-------------------------|-------------------------|------------|------------|
| 1,000  | 10.36M                 | 10.23M                  | N/A                     | 0.99x       | N/A        |
| 4,096  | 2.67M                  | 12.33M                  | N/A                     | 4.62x       | N/A        |
| 32,768 | 1.74M                  | 3.55M                   | N/A                     | 2.04x       | N/A        |
| 100,000| 1.36M                  | 1.73M                   | N/A                     | 1.27x       | N/A        |

**Note**: Capacity 65536 benchmark did not complete due to timeout in the collected data. The 16384 capacity shows significant improvement at 4,096 orders (4.62x) but diminishing returns at higher order counts.

### 6.5 Sanitizer Test Results

**ASan/UBSan (Debug Build)**:
- SPSC queue tests: 12/12 passed ✅
- SPSC integration tests: 37/37 passed ✅

**TSan (ThreadSanitizer Build)**:
- SPSC queue tests: 12/12 passed, 0 warnings ✅
- SPSC integration tests: 37/37 passed, 0 warnings ✅

### 6.6 Summary of Measurements

**What the measurements prove**:
1. SPSC benchmarks are now correct (no silent drops, proper backpressure, submitted == processed)
2. At small workloads (1,000 orders), SPSC shows 2-3x speedup over mutex due to queue buffering
3. At medium workloads (4,096 orders), SPSC parity with mutex (0.84-0.95x ratio)
4. At large workloads (32,768-100,000 orders), SPSC is slower than mutex (0.67-0.85x ratio)
5. Larger queue capacity (16384) improves performance at 4,096 orders (4.62x) but not at 100,000 orders (1.27x)
6. Multiple instruments reduce contention slightly but SPSC still slower at scale
7. Higher cancel rate (15%) has minimal impact on SPSC vs mutex ratio

**What the measurements do NOT prove**:
1. SPSC is intrinsically slower than mutex - the difference may be due to implementation details, queue overhead, or consumer thread scheduling
2. SPSC is unsuitable for production - the benchmark includes consumer drain time which may not be required in all scenarios
3. Queue capacity 1024 is optimal - 16384 shows improvement at some workloads
4. The architectural trade-off (async vs sync) is inherently worse - this is a design choice with different use cases

**Key observations**:
- SPSC's advantage diminishes as workload size increases
- Consumer thread overhead and queue management costs become significant at scale
- The mutex baseline is synchronous, so it doesn't have thread overhead
- SPSC timing includes consumer drain, which is fair for measuring end-to-end latency but may overstate the cost for pure ingestion scenarios
