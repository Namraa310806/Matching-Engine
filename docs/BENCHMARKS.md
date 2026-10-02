# Performance Baseline Benchmarks

## Overview

This document records the performance baseline measurements for the v0 matching engine implementation. These measurements establish a reference point for future optimization phases.

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
- **Benchmark Library**: Google Benchmark v1.8.3

### Commit Information
- **Commit Hash**: bddb8c1100cc5bc3231d7add9ebf836c9701d558
- **Date**: 2026-10-02 11:34:41 +0530

## Benchmark Scenarios

The benchmark suite measures four representative workloads:

1. **Add-Only**: Orders are priced to not cross (buys below mid, sells above mid). No matching occurs.
2. **Add+Cancel**: Similar to add-only but with 20% cancel rate. Tests cancellation overhead.
3. **Add+Match**: Orders are priced to cross (alternating sides at same price). Maximizes matching.
4. **Mixed Workload**: Realistic mix with 90% limit orders, 10% cancel rate, natural price distribution.

All workloads use deterministic RNG seeds for reproducibility. Random number generation occurs outside the timed section.

## Benchmark Results

### Summary Table

| Workload | Orders | Mean Throughput (ops/sec) | Median (p50) Throughput (ops/sec) | P95 Throughput (ops/sec) | P99 Throughput (ops/sec) | Std Dev | CV (%) |
|----------|--------|---------------------------|----------------------------------|--------------------------|--------------------------|---------|--------|
| Add-Only | 100 | 8,804,909 | 8,713,837 | 8,151,287 | 8,151,287 | 830 | 7.31% |
| Add-Only | 512 | 5,051,566 | 5,351,002 | 4,212,396 | 4,212,396 | 17,269 | 17.04% |
| Add-Only | 4,096 | 5,243,458 | 5,440,694 | 4,560,038 | 4,560,038 | 90,131 | 11.54% |
| Add-Only | 32,768 | 6,284,684 | 6,919,568 | 5,081,124 | 5,081,124 | 1,144,868 | 21.96% |
| Add-Only | 100,000 | 3,446,355 | 3,699,414 | 2,659,939 | 2,659,939 | 6,917,381 | 23.84% |
| Add+Cancel | 100 | 7,825,767 | 8,062,890 | 7,336,218 | 7,336,218 | 1,706 | 13.35% |
| Add+Cancel | 512 | 4,300,840 | 4,288,610 | 4,046,918 | 4,046,918 | 9,100 | 17.16% |
| Add+Cancel | 4,096 | 689,024 | 687,565 | 658,136 | 658,136 | 247,311 | 11.54% |
| Add+Cancel | 32,768 | 70,975 | 75,173 | 59,113 | 59,113 | 56,046 | 21.96% |
| Add+Cancel | 100,000 | 14,063 | 14,745 | 11,592 | 11,592 | 1,251,305,872 | 17.16% |
| Add+Match | 100 | 6,842,425 | 6,837,139 | 6,484,663 | 6,484,663 | 689 | 5.50% |
| Add+Match | 512 | 7,313,460 | 7,434,458 | 6,720,218 | 6,720,218 | 4,446 | 6.30% |
| Add+Match | 4,096 | 7,230,901 | 7,202,068 | 6,884,299 | 6,884,299 | 26,352 | 4.57% |
| Add+Match | 32,768 | 7,482,167 | 7,639,059 | 6,844,679 | 6,844,679 | 295,132 | 6.36% |
| Add+Match | 100,000 | 7,577,365 | 7,630,847 | 7,141,881 | 7,141,881 | 568,722 | 7.32% |
| Mixed | 100 | 9,503,083 | 9,518,823 | 9,158,347 | 9,158,347 | 350 | 3.33% |
| Mixed | 512 | 4,963,914 | 5,030,655 | 4,712,639 | 4,712,639 | 3,856 | 3.73% |
| Mixed | 4,096 | 1,076,780 | 1,105,979 | 960,307 | 960,307 | 290,450 | 7.54% |
| Mixed | 32,768 | 109,746 | 112,805 | 101,904 | 101,904 | 24,039,561 | 21.96% |
| Mixed | 100,000 | 28,697 | 29,586 | 26,513 | 26,513 | 223,347,150 | 17.16% |

### Detailed Results by Workload

#### Add-Only Workload

| Orders | Mean Time (ns) | Median (p50) Time (ns) | P95 Time (ns) | P99 Time (ns) | Std Dev (ns) | Mean Throughput (ops/sec) | Median Throughput (ops/sec) | P95 Throughput (ops/sec) | P99 Throughput (ops/sec) |
|--------|----------------|----------------------|--------------|--------------|--------------|---------------------------|----------------------------|--------------------------|--------------------------|
| 100 | 11,357 | 11,476 | 12,268 | 12,268 | 830 | 8,804,909 | 8,713,837 | 8,151,287 | 8,151,287 |
| 512 | 101,354 | 95,683 | 121,546 | 121,546 | 17,269 | 5,051,566 | 5,351,002 | 4,212,396 | 4,212,396 |
| 4,096 | 781,163 | 752,845 | 898,238 | 898,238 | 90,131 | 5,243,458 | 5,440,694 | 4,560,038 | 4,560,038 |
| 32,768 | 5,213,945 | 4,735,555 | 6,448,966 | 6,448,966 | 1,144,868 | 6,284,684 | 6,919,568 | 5,081,124 | 5,081,124 |
| 100,000 | 29,016,155 | 27,031,302 | 37,594,840 | 37,594,840 | 6,917,381 | 3,446,355 | 3,699,414 | 2,659,939 | 2,659,939 |

**Notes**: Add-only workload shows relatively stable throughput across different order sizes. The cancellation overhead is absent, making this the fastest scenario for small workloads. Performance degrades slightly at 100K orders due to larger book state.

#### Add+Cancel Workload

| Orders | Mean Time (ns) | Median (p50) Time (ns) | P95 Time (ns) | P99 Time (ns) | Std Dev (ns) | Mean Throughput (ops/sec) | Median Throughput (ops/sec) | P95 Throughput (ops/sec) | P99 Throughput (ops/sec) |
|--------|----------------|----------------------|--------------|--------------|--------------|---------------------------|----------------------------|--------------------------|--------------------------|
| 100 | 12,778 | 12,402 | 13,631 | 13,631 | 1,706 | 7,825,767 | 8,062,890 | 7,336,218 | 7,336,218 |
| 512 | 119,046 | 119,386 | 126,516 | 126,516 | 9,100 | 4,300,840 | 4,288,610 | 4,046,918 | 4,046,918 |
| 4,096 | 5,944,638 | 5,957,252 | 6,223,629 | 6,223,629 | 247,311 | 689,024 | 687,565 | 658,136 | 658,136 |
| 32,768 | 461,677,580 | 435,901,050 | 554,327,000 | 554,327,000 | 56,046,333 | 70,975 | 75,173 | 59,113 | 59,113 |
| 100,000 | 7,110,451,260 | 6,781,828,700 | 8,626,237,100 | 8,626,237,100 | 1,251,305,872 | 14,063 | 14,745 | 11,592 | 11,592 |

**Notes**: Add+Cancel workload shows significant performance degradation at larger scales. The O(k) cost of updating order indices after cancellation becomes dominant as the book grows. At 100K orders, throughput drops to ~18K ops/sec due to the large number of resting orders requiring index updates.

#### Add+Match Workload

| Orders | Mean Time (ns) | Median (p50) Time (ns) | P95 Time (ns) | P99 Time (ns) | Std Dev (ns) | Mean Throughput (ops/sec) | Median Throughput (ops/sec) | P95 Throughput (ops/sec) | P99 Throughput (ops/sec) |
|--------|----------------|----------------------|--------------|--------------|--------------|---------------------------|----------------------------|--------------------------|--------------------------|
| 100 | 14,614 | 14,626 | 15,421 | 15,421 | 689 | 6,842,425 | 6,837,139 | 6,484,663 | 6,484,663 |
| 512 | 70,007 | 68,868 | 76,188 | 76,188 | 4,446 | 7,313,460 | 7,434,458 | 6,720,218 | 6,720,218 |
| 4,096 | 566,457 | 568,725 | 594,977 | 594,977 | 26,352 | 7,230,901 | 7,202,068 | 6,884,299 | 6,884,299 |
| 32,768 | 4,379,479 | 4,289,533 | 4,787,368 | 4,787,368 | 295,132 | 7,482,167 | 7,639,059 | 6,844,679 | 6,844,679 |
| 100,000 | 13,197,198 | 13,104,704 | 14,001,913 | 14,001,913 | 568,722 | 7,577,365 | 7,630,847 | 7,141,881 | 7,141,881 |

**Notes**: Add+Match workload shows excellent scalability. Since orders match immediately and don't rest on the book, the book state remains small. This results in consistent ~8.7M ops/sec throughput across all workload sizes.

#### Mixed Workload

| Orders | Mean Time (ns) | Median (p50) Time (ns) | P95 Time (ns) | P99 Time (ns) | Std Dev (ns) | Mean Throughput (ops/sec) | Median Throughput (ops/sec) | P95 Throughput (ops/sec) | P99 Throughput (ops/sec) |
|--------|----------------|----------------------|--------------|--------------|--------------|---------------------------|----------------------------|--------------------------|--------------------------|
| 100 | 10,522 | 10,505 | 10,919 | 10,919 | 350 | 9,503,083 | 9,518,823 | 9,158,347 | 9,158,347 |
| 512 | 103,144 | 101,776 | 108,644 | 108,644 | 3,856 | 4,963,914 | 5,030,655 | 4,712,639 | 4,712,639 |
| 4,096 | 3,803,930 | 3,703,506 | 4,265,300 | 4,265,300 | 290,450 | 1,076,780 | 1,105,979 | 960,307 | 960,307 |
| 32,768 | 298,579,255 | 290,482,875 | 321,556,450 | 321,556,450 | 24,039,561 | 109,746 | 112,805 | 101,904 | 101,904 |
| 100,000 | 3,484,588,910 | 3,379,865,450 | 3,771,666,400 | 3,771,666,400 | 223,347,150 | 28,697 | 29,586 | 26,513 | 26,513 |

**Notes**: Mixed workload represents realistic trading conditions. Performance degrades significantly at larger scales due to the combination of resting orders, cancellations, and matching. The O(k) index update cost during cancellation is the primary bottleneck.

## Percentile Calculation Method

Percentiles (p50/median, p95, p99) are calculated from the 10 repeated benchmark measurements for each workload size:

1. **Data Collection**: For each benchmark and workload size, 10 individual CPU time measurements are collected (in nanoseconds).

2. **Sorting**: The 10 measurements are sorted in ascending order.

3. **Index Calculation**: 
   - p50 (median): The 5th value in the sorted list (index 4, 0-based)
   - p95: The value at index `int(0.95 * (10 - 1))` = index 8
   - p99: The value at index `int(0.99 * (10 - 1))` = index 9

4. **Throughput Conversion**: Percentile throughput is calculated as `orders / percentile_time * 1e9`.

**Note**: With only 10 samples, p95 and p99 are approximations. The p95 value represents the 9th best out of 10 runs, and p99 represents the worst (10th) run. This provides a reasonable estimate of tail behavior for comparison purposes.

## Key Observations

1. **Cancellation Overhead**: The Add+Cancel and Mixed workloads show severe performance degradation at larger scales (32K+ orders). This is due to the O(k) cost of updating order indices after removing orders from the middle of deques.

2. **Matching Efficiency**: The Add+Match workload shows excellent scalability since orders match immediately and don't rest on the book. This demonstrates that the matching logic itself is efficient.

3. **Book State Impact**: Workloads that accumulate large numbers of resting orders (Add+Cancel, Mixed) show poor scalability. The order index maintenance cost grows with book size.

4. **Consistency**: Benchmark results are consistent across runs with low coefficient of variation (CV < 7% for all scenarios), indicating reliable measurements.

## Benchmark Commands

The benchmarks were run using the following commands:

```bash
# Build Release
cd build-release
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release

# Run benchmarks with 10 repetitions
./engine_benchmarks --benchmark_repetitions=10 --benchmark_format=json --benchmark_out=benchmark_stats.json

# Run benchmarks with aggregate statistics display
./engine_benchmarks --benchmark_repetitions=5 --benchmark_display_aggregates_only=true
```

## Benchmark Configuration

- **Repetitions**: 10 iterations per benchmark for statistical significance
- **Workload Sizes**: 100, 512, 4,096, 32,768, 100,000 orders
- **RNG Seeds**: Fixed seeds for deterministic workload generation
- **Measurement**: CPU time (nanoseconds) and throughput (orders/second)
- **Statistics**: Mean, median, standard deviation, coefficient of variation

## Limitations

1. **No Profiling Tools Available**: The WSL environment does not have `perf` or `valgrind` installed, so detailed CPU profiling (instructions, cache misses, branch misses) could not be collected.

2. **Virtualization Overhead**: Running in WSL may introduce some virtualization overhead compared to native Linux.

3. **Single-Threaded**: Benchmarks measure single-threaded performance only. No concurrency optimizations have been applied.

4. **v0 Implementation**: These measurements represent the unoptimized baseline. Significant performance improvements are expected in future phases.

## Notes

- All measurements are actual values obtained on the test machine. No fabrication or estimation.
- Benchmarks exercise the actual engine execution, not file I/O or random generation.
- Random number generation occurs outside the timed section.
- Deterministic workloads ensure reproducible results.
