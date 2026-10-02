# Baseline Profiling Report

## Overview

This document records the performance analysis of the v0 matching engine implementation. The profiling aims to identify performance bottlenecks and hot paths to guide future optimization efforts.

## Test Environment

### Hardware
- **CPU**: 13th Gen Intel(R) Core(TM) i7-1355U
- **Cores**: 4 (2 cores, 2 threads per core)
- **Clock Speed**: 2611 MHz
- **L1 Data Cache**: 48 KiB (2 instances)
- **L1 Instruction Cache**: 32 KiB (2 instances)
- **L2 Cache**: 1280 KiB (2 instances)
- **L3 Cache**: 12288 KiB (1 instance)
- **Hostname**: mahisha2509

### Software
- **Compiler**: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- **Build Type**: Release
- **Build Flags**: `-O3 -march=native -DNDEBUG`
- **CMake Version**: 3.20+
- **C++ Standard**: C++20

### Commit Information
- **Commit Hash**: bddb8c1100cc5bc3231d7add9ebf836c9701d558
- **Date**: 2026-10-02 11:34:41 +0530

## Profiling Limitations

**Important**: The WSL environment does not have `perf` or `valgrind` installed. Therefore, detailed CPU profiling (instructions, cycles, cache misses, branch misses) could not be collected. This report is based on:

1. **Measured benchmark results**: Actual throughput and latency measurements from Google Benchmark
2. **Code analysis**: Algorithmic complexity analysis of the v0 implementation
3. **Performance patterns**: Observed trends across different workload sizes

**No direct profiler data was collected**. All "hot path" and "bottleneck" identifications are based on code analysis and performance pattern correlation, not direct profiling instrumentation.

## Benchmark Configuration

### Workload Used for Analysis
- **Type**: Mixed workload (realistic trading scenario)
- **Size**: 10,000 orders
- **Configuration**:
  - num_orders: 10000
  - limit_ratio: 0.9
  - buy_ratio: 0.5
  - mid_price: 100000
  - spread: 100
  - volatility: 0.01
  - cancel_rate: 0.1
  - rng_seed: 12345
  - min_qty: 1
  - max_qty: 100

### Measured Performance (All Workloads)

#### Add-Only Workload

| Orders | Mean Throughput (ops/sec) | Median (p50) Throughput (ops/sec) | P95 Throughput (ops/sec) | P99 Throughput (ops/sec) |
|--------|---------------------------|----------------------------------|--------------------------|--------------------------|
| 100 | 8,804,909 | 8,713,837 | 8,151,287 | 8,151,287 |
| 512 | 5,051,566 | 5,351,002 | 4,212,396 | 4,212,396 |
| 4,096 | 5,243,458 | 5,440,694 | 4,560,038 | 4,560,038 |
| 32,768 | 6,284,684 | 6,919,568 | 5,081,124 | 5,081,124 |
| 100,000 | 3,446,355 | 3,699,414 | 2,659,939 | 2,659,939 |

#### Add+Cancel Workload

| Orders | Mean Throughput (ops/sec) | Median (p50) Throughput (ops/sec) | P95 Throughput (ops/sec) | P99 Throughput (ops/sec) |
|--------|---------------------------|----------------------------------|--------------------------|--------------------------|
| 100 | 7,825,767 | 8,062,890 | 7,336,218 | 7,336,218 |
| 512 | 4,300,840 | 4,288,610 | 4,046,918 | 4,046,918 |
| 4,096 | 689,024 | 687,565 | 658,136 | 658,136 |
| 32,768 | 70,975 | 75,173 | 59,113 | 59,113 |
| 100,000 | 14,063 | 14,745 | 11,592 | 11,592 |

#### Add+Match Workload

| Orders | Mean Throughput (ops/sec) | Median (p50) Throughput (ops/sec) | P95 Throughput (ops/sec) | P99 Throughput (ops/sec) |
|--------|---------------------------|----------------------------------|--------------------------|--------------------------|
| 100 | 6,842,425 | 6,837,139 | 6,484,663 | 6,484,663 |
| 512 | 7,313,460 | 7,434,458 | 6,720,218 | 6,720,218 |
| 4,096 | 7,230,901 | 7,202,068 | 6,884,299 | 6,884,299 |
| 32,768 | 7,482,167 | 7,639,059 | 6,844,679 | 6,844,679 |
| 100,000 | 7,577,365 | 7,630,847 | 7,141,881 | 7,141,881 |

#### Mixed Workload

| Orders | Mean Throughput (ops/sec) | Median (p50) Throughput (ops/sec) | P95 Throughput (ops/sec) | P99 Throughput (ops/sec) |
|--------|---------------------------|----------------------------------|--------------------------|--------------------------|
| 100 | 9,503,083 | 9,518,823 | 9,158,347 | 9,158,347 |
| 512 | 4,963,914 | 5,030,655 | 4,712,639 | 4,712,639 |
| 4,096 | 1,076,780 | 1,105,979 | 960,307 | 960,307 |
| 32,768 | 109,746 | 112,805 | 101,904 | 101,904 |
| 100,000 | 28,697 | 29,586 | 26,513 | 26,513 |

## Performance Analysis

### Measured Performance Patterns

**Measured**: Cancellation-heavy workloads (Add+Cancel, Mixed) show severe performance degradation with increasing order count:
- Add+Cancel: 7.83M ops/sec at 100 orders → 14.1K ops/sec at 100K orders (555x degradation)
- Mixed: 9.50M ops/sec at 100 orders → 28.7K ops/sec at 100K orders (331x degradation)

**Measured**: Matching-heavy workload (Add+Match) shows excellent scalability:
- Add+Match: 6.84M ops/sec at 100 orders → 7.58M ops/sec at 100K orders (1.11x improvement)

**Measured**: Add-only workload shows moderate degradation:
- Add-Only: 8.80M ops/sec at 100 orders → 3.45M ops/sec at 100K orders (2.55x degradation)

### Code Analysis

**Code analysis** of the v0 implementation reveals the following algorithmic characteristics:

#### `update_order_indices_after_removal` (O(k))
**Location**: `src/engine.cpp:376-384`

```cpp
void OrderBook::update_order_indices_after_removal(Side side, Price price, size_t removed_index) {
    for (auto& [id, loc] : order_index_) {
        if (loc.side == side && loc.price == price && loc.deque_index > removed_index) {
            loc.deque_index--;
        }
    }
}
```

**Complexity**: O(k) where k is the number of orders at the price level being cancelled.

**Analysis**: This function is called after every cancellation that removes an order from the middle of a deque. It iterates through the entire order index to update deque indices.

#### `cancel_order` (O(k) + O(log n))
**Location**: `src/engine.cpp:289-370`

**Operations**:
- Order index lookup: O(1)
- Price level lookup: O(log n)
- Deque erase: O(k) for middle removal
- Index update: O(k) via `update_order_indices_after_removal`

**Analysis**: The combined O(k) cost of deque erase and index update makes cancellation expensive for large books.

#### `match_buy_order` / `match_sell_order` (O(m * k))
**Location**: `src/engine.cpp:55-177`

**Operations**:
- Iterate through price levels: O(m) where m is number of price levels
- Iterate through orders at each level: O(k) where k is orders per level
- Trade generation: O(1) per trade
- Index removal: O(1) per filled order

**Analysis**: Matching cost depends on the number of crossing orders. When orders match immediately (small book state), performance is excellent.

### Hypotheses about Bottlenecks

**Hypothesis**: The O(k) cost of `update_order_indices_after_removal` is a likely contributor to the observed severe degradation in cancellation-heavy workloads.

**Rationale**: 
- Measured: Add+Cancel workload degrades 555x from 100 to 100K orders
- Code analysis: Cancellation performs O(k) index repair
- Correlation: The O(k) cost grows with book size, matching the observed degradation pattern

**Hypothesis**: The deque middle removal cost contributes to cancellation overhead.

**Rationale**:
- Code analysis: `std::deque::erase` requires shifting elements when removing from the middle
- Measured: Cancellation-heavy workloads show poor scalability
- Correlation: The shifting cost is O(k) and scales with book size

**Hypothesis**: Book state size is the primary determinant of performance.

**Rationale**:
- Measured: Add+Match workload maintains consistent ~7.5M ops/sec across all sizes because orders don't rest on the book
- Measured: Workloads that accumulate resting orders (Add+Cancel, Mixed) show severe degradation
- Correlation: Small book state = good performance; large book state = poor performance

### Complexity Summary

| Operation | Time Complexity | Notes |
|-----------|----------------|-------|
| Add limit order | O(log n) | Map insertion dominates |
| Cancel order | O(k) + O(log n) | k = orders at price level, O(k) from index update |
| Match order | O(m * k) | m = price levels, k = orders per level (worst case) |
| Index lookup | O(1) | Hash table lookup |
| Best bid/ask | O(1) | Direct map access |

## Limitations of Measurements

1. **No `perf` Available**: Could not measure instructions, cycles, cache misses, or branch misses directly.

2. **No `valgrind`/Callgrind Available**: Could not obtain detailed function call graphs and time spent in each function.

3. **Virtualization Overhead**: WSL may introduce overhead compared to native Linux.

4. **Code Analysis Only**: Hot path identification is based on code analysis and performance patterns, not direct profiling data.

5. **No CPU Counter Data**: Could not measure IPC, cache hit rates, or branch prediction accuracy.

## Conclusion

The v0 implementation has a critical performance bottleneck in the cancellation path due to the O(k) cost of updating order indices. This bottleneck causes severe performance degradation in workloads with many cancellations or large resting order counts. The matching logic itself is efficient when the book state is small.

Future optimization phases should focus on:
1. Eliminating the O(k) index update cost
2. Improving cache locality of data structures
3. Reducing allocation overhead
4. Maintaining the correctness guarantees established in Phase 1
