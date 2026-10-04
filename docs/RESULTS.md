# Results Summary

This document contains the most important measured before/after results from the entire matching-engine project. All numbers are from actual benchmark runs; no estimates or fabrications.

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
- **Build Type**: Release (unless otherwise noted)
- **Build Flags**: `-O3 -march=native -DNDEBUG`
- **CMake Version**: 3.20+
- **C++ Standard**: C++20
- **Benchmark Library**: Google Benchmark v1.8.3

## V0 vs V1 Comparison (Phase 4A)

### Workload
100,000 orders, Release build, 10 repetitions

### Configuration
- **V0**: `std::map<Price, std::deque<Order>>` with O(k) index update on cancellation
- **V1**: `std::vector<PriceLevel>` with intrusive linked list, O(1) order-node removal

### Results

| Workload | V0 Throughput (ops/sec) | V1 Throughput (ops/sec) | V1 Speedup | V0 Mean Time (ns) | V1 Mean Time (ns) |
|----------|-------------------------|-------------------------|------------|-------------------|-------------------|
| Add-Only | 4,953,057 | 4,886,119 | 0.99x | 20,584,465 | 20,551,876 |
| Add+Cancel | 13,713 | 3,809,962 | **278x** | 7,332,074,930 | 26,404,085 |
| Add+Match | 8,085,378 | 6,642,254 | 0.82x | 12,422,874 | 15,070,207 |
| Mixed | 26,658 | 3,801,078 | **142x** | 3,760,528,890 | 26,643,964 |

### Interpretation

**Cancellation-heavy workloads**: V1 achieves dramatic speedup (278x at 100K orders in Add+Cancel, 142x in Mixed) by eliminating the O(k) index update cost that causes severe degradation in V0.

**Matching-heavy workloads**: V0 is actually faster (1.2x) because orders don't rest on the book, so the book state remains small and V0's `std::map` performs well.

**Add-only workloads**: Performance converges at larger scales (4.95M vs 4.89M ops/sec), suggesting memory bandwidth becomes the limiting factor regardless of data structure choice.

### Metrics Measured
- Throughput (orders/second)
- CPU time (nanoseconds)
- p50, p95, p99 percentiles (from 10 repetitions)
- Standard deviation, coefficient of variation

### Source
`docs/PERFORMANCE_NOTES.md` (Phase 4A section)

---

## Object Pool vs Baseline Allocator (Phase 4)

### Workload
100,000 orders, Release build, 5 repetitions

### Configuration
- **Baseline**: V1 with `new/delete` allocation
- **Pool**: V1 with chunk-based object pool (1K initial chunk, 64K max, 2x growth factor)

### Results

| Workload | Baseline Throughput (ops/sec) | Pool Throughput (ops/sec) | Improvement | Baseline Mean Time (ms) | Pool Mean Time (ms) |
|----------|-------------------|----------------|-------------|---------------------|-------------------|
| Add-Only | 5.24M | 7.81M | **+49%** | 19.27 | 12.67 |
| Add+Cancel | 5.26M | 7.26M | **+38%** | 19.16 | 13.78 |
| Add+Match | 7.73M | 9.44M | **+22%** | 12.95 | 10.52 |
| Mixed | 5.47M | 7.02M | **+28%** | 18.30 | 14.07 |

### Pool Statistics (Mixed Workload, 100K orders)
- Pool hits: 81,053
- Pool misses: 5
- Hit rate: 99.99%
- Chunks: 6
- Total capacity: 64,512 nodes

### Variance Reduction

| Workload | Baseline CV (%) | Pool CV (%) | Reduction |
|----------|----------------|-------------|-----------|
| Add-Only | 11.18% | 7.96% | 1.4x |
| Add+Cancel | 9.41% | 1.84% | 5.1x |
| Add+Match | 4.83% | 2.02% | 2.4x |
| Mixed | 4.04% | 1.36% | 3.0x |

### Interpretation

Object pool provides consistent 22-49% performance improvement across all workloads with significantly lower variance (1.4-5x reduction in coefficient of variation). The 99.99% hit rate demonstrates excellent freelist recycling efficiency.

### Metrics Measured
- Throughput (orders/second)
- CPU time (milliseconds)
- Standard deviation, coefficient of variation
- Pool statistics (hits, misses, hit rate, chunks, capacity)

### Source
`docs/PERFORMANCE_NOTES.md` (Phase 4 section)

---

## Single-Threaded vs Mutex Baseline (Phase 5B)

### Workload
100,000 orders (90,000 submits + 10,000 cancels), Release build

### Configuration
- **Single-threaded baseline**: `MultiInstrumentEngine` (no concurrency)
- **Mutex 1 thread**: `MutexMultiInstrumentEngine` with 1 producer thread
- **Mutex 2 threads**: `MutexMultiInstrumentEngine` with 2 producer threads
- **Mutex 4 threads**: `MutexMultiInstrumentEngine` with 4 producer threads
- **Mutex 8 threads**: `MutexMultiInstrumentEngine` with 8 producer threads

### Results (Wall Time)

| Benchmark | Wall Time (ms) | Throughput (ops/sec) | vs Baseline |
|-----------|----------------|---------------------|-------------|
| Single-threaded baseline | 62.6 | 1.60M | 1.00x |
| Mutex 1 thread | 68.3 | 1.46M | 0.91x (8% slower) |
| Mutex 2 threads | 253.8 | 394K | 0.25x (4x slower) |
| Mutex 4 threads | 217.9 | 459K | 0.29x (3.5x slower) |
| Mutex 8 threads | 86.4 | 1.16M | 0.73x (1.4x slower) |

### Interpretation

Global mutex serializes all operations, making multi-threaded execution slower than single-threaded due to contention and thread overhead. The best performance is with a single thread, and adding more threads degrades performance due to mutex contention.

### Metrics Measured
- Wall time (milliseconds)
- Throughput (orders/second)
- Latency observations (qualitative)

### Source
`docs/MUTEX_CONCURRENCY_BASELINE.md`

---

## Mutex vs SPSC Ingestion (Phase 5C and Phase 7)

### Phase 5C Measurements

**Workload**: 90% limit orders, 10% cancellations, DEBUG build with -O2, 3 repetitions

**Configuration**:
- **SPSC**: `SpscMultiInstrumentEngine` with lock-free SPSC queue (API modified to return order IDs for cancellations)
- **Mutex (1 thread)**: `MutexMultiInstrumentEngine` with 1 producer thread
- **Single-threaded**: `MultiInstrumentEngine` (no concurrency)

**Results (Throughput)**:

| Operations | SPSC (ops/sec) | Mutex 1 thread (ops/sec) | Single-threaded (ops/sec) | SPSC vs Mutex | SPSC vs Single-threaded |
|------------|----------------|---------------------------|-----------------------------|---------------|------------------------|
| 1,000 | 1.81M | 195K | 196K | **9.3x** | 9.2x |
| 4,096 | 3.48M | 187K | 188K | **18.5x** | 18.5x |
| 32,768 | 5.15M | 197K | 156K | **26.1x** | 33.0x |
| 100,000 | 4.99M | 197K | 200K | **25.3x** | 25.0x |

### Phase 7 Final Verification - INVALIDATED BY CRITICAL BUG

**CRITICAL BUG DISCOVERED (2026-10-04)**: The previous Phase 7 SPSC benchmark results (25.22M ops/sec, 8.0x speedup) were INVALID due to a critical bug:

**The Bug**: The benchmark silently dropped commands when the SPSC queue was full instead of applying backpressure. When `enqueue()` returned false (queue full), the benchmark would skip the command without retry, leading to:
- Commands being silently lost
- Throughput calculation counting attempted submissions, not actual processed commands
- No verification that submitted == processed commands
- Cancellations targeting wrong order IDs when submissions were dropped

**Corrected Results (2026-10-04)**:

**Workload**: 100,000 orders, Release build with -O3 -march=native, 3 repetitions

**Configuration**: Corrected benchmark with:
- Backpressure: producer retries enqueue until success
- Verification: asserts submitted == processed commands
- Proper workload: all commands actually processed

**Results (Throughput)**:

| Benchmark | SPSC Throughput (mean ops/sec) | SPSC Throughput (median ops/sec) | Mutex 1 Thread (mean ops/sec) | Mutex 1 Thread (median ops/sec) | SPSC Speedup |
|-----------|-------------------------------|----------------------------------|-------------------------------|----------------------------------|--------------|
| 100K orders | 1.74M | 1.65M | 3.43M | 3.30M | **0.51x** |

**Command**: `./build-release/engine_benchmarks_spsc --benchmark_repetitions=3 --benchmark_filter=BM_SPSC_1Producer/100000`

**Command for mutex baseline**: `./build-release/engine_benchmarks_concurrency --benchmark_repetitions=3 --benchmark_filter=BM_Mutex_1Thread/100000`

### Interpretation

**Corrected Results**: With the benchmark fixed to properly apply backpressure and verify all commands are processed, SPSC is actually SLOWER than the mutex baseline (0.51x speedup). The previous 8.0x speedup claim was based on a fraudulent benchmark that measured submission rate while silently dropping commands.

**Previous (Invalid) Results**: The old Phase 7 results showed 8.0x speedup, but this was measuring:
- Time to attempt submissions (including failed ones)
- NOT time to actually process commands
- With an unknown percentage of commands silently dropped

**Root Cause**: The 1024-slot queue capacity was insufficient for 100K orders when the consumer couldn't keep up. Without backpressure, the producer would rapidly fill the queue and then silently drop subsequent commands, making the "throughput" metric meaningless.

### Metrics Measured
- Throughput (orders/second)
- Latency: Not measured in current benchmarks

### Source
`docs/SPSC_CORRECTNESS_AUDIT.md`

### Phase 7 Comprehensive Benchmark Suite (2026-10-04)

**CRITICAL NOTE**: The corrected results below are from the comprehensive benchmark audit performed on 2026-10-04 with 5 repetitions and multiple queue capacities.

**Workload**: 90% limit orders, 10% cancellations (unless noted), Release build with -O3 -march=native -DNDEBUG, 5 repetitions

**Configuration**: Corrected benchmark with:
- Backpressure: producer retries enqueue until success with yield()
- Verification: asserts submitted == processed commands
- Proper workload: all commands actually processed
- API: returns order IDs enabling cancellations

**Build Command**:
```bash
cmake --build build-release --target engine_benchmarks_spsc
./build-release/engine_benchmarks_spsc --benchmark_repetitions=5 --benchmark_counters_tabular=true
```

#### 7.1 Queue Capacity 1024 Results

**BM_SPSC_1Producer (Single Instrument)**:

|| Orders | SPSC Mean (ops/s) | SPSC Median (ops/s) | Mutex Mean (ops/s) | Mutex Median (ops/s) | SPSC/Mutex Ratio |
||--------|-------------------|---------------------|---------------------|----------------------|------------------|
|| 1,000  | 10.36M            | 10.95M              | 3.22M               | 3.23M                | 3.22x            |
|| 4,096  | 2.67M             | 2.75M               | 2.98M               | 3.10M                | 0.90x            |
|| 32,768 | 1.74M             | 1.74M               | 2.05M               | 2.06M                | 0.85x            |
|| 100,000| 1.36M             | 1.27M               | 2.02M               | 2.07M                | 0.67x            |

**BM_SPSC_SameInstrument (High Contention)**:

|| Orders | SPSC Mean (ops/s) | SPSC Median (ops/s) |
||--------|-------------------|---------------------|
|| 1,000  | 7.59M             | 7.91M               |
|| 4,096  | 2.42M             | 2.41M               |
|| 32,768 | 1.85M             | 1.90M               |
|| 100,000| 1.64M             | 1.69M               |

**BM_SPSC_MultipleInstruments (4 Instruments)**:

|| Orders | SPSC Mean (ops/s) | SPSC Median (ops/s) | Mutex Mean (ops/s) | Mutex Median (ops/s) | SPSC/Mutex Ratio |
||--------|-------------------|---------------------|---------------------|----------------------|------------------|
|| 1,000  | 7.85M             | 8.14M               | 3.44M               | 3.55M                | 2.28x            |
|| 4,096  | 2.65M             | 2.69M               | 2.78M               | 2.95M                | 0.95x            |
|| 32,768 | 1.92M             | 1.92M               | 2.02M               | 1.95M                | 0.95x            |
|| 100,000| 1.57M             | 1.74M               | 2.02M               | 2.07M                | 0.78x            |

**BM_SPSC_MixedActivity (15% Cancel Rate)**:

|| Orders | SPSC Mean (ops/s) | SPSC Median (ops/s) | Mutex Mean (ops/s) | Mutex Median (ops/s) | SPSC/Mutex Ratio |
||--------|-------------------|---------------------|---------------------|----------------------|------------------|
|| 1,000  | 9.93M             | 10.79M              | 3.08M               | 2.98M                | 3.22x            |
|| 4,096  | 2.58M             | 2.59M               | 3.07M               | 3.07M                | 0.84x            |
|| 32,768 | 1.94M             | 1.95M               | 2.58M               | 2.64M                | 0.75x            |
|| 100,000| 1.85M             | 1.85M               | 2.48M               | 2.50M                | 0.75x            |

#### 7.2 Queue Capacity Comparison

**BM_SPSC_1Producer - Queue Capacity 1024 vs 16384**:

|| Orders | Capacity 1024 (ops/s) | Capacity 16384 (ops/s) | 16384/1024 |
||--------|------------------------|-------------------------|------------|
|| 1,000  | 10.36M                 | 10.23M                  | 0.99x       |
|| 4,096  | 2.67M                  | 12.33M                  | 4.62x       |
|| 32,768 | 1.74M                  | 3.55M                   | 2.04x       |
|| 100,000| 1.36M                  | 1.73M                   | 1.27x       |

**Note**: Capacity 65536 benchmark did not complete in the allocated time.

#### 7.3 Sanitizer Test Results

**ASan/UBSan (Debug Build)**:
- SPSC queue tests: 12/12 passed ✅
- SPSC integration tests: 37/37 passed ✅

**TSan (ThreadSanitizer Build)**:
- SPSC queue tests: 12/12 passed, 0 warnings ✅
- SPSC integration tests: 37/37 passed, 0 warnings ✅

#### 7.4 Interpretation

**Corrected Results**: With the benchmark fixed to properly apply backpressure and verify all commands are processed:

1. **At small workloads (1,000 orders)**: SPSC shows 2-3x speedup over mutex due to queue buffering and reduced synchronization overhead
2. **At medium workloads (4,096 orders)**: SPSC parity with mutex (0.84-0.95x ratio) - queue overhead becomes significant
3. **At large workloads (32,768-100,000 orders)**: SPSC is slower than mutex (0.67-0.85x ratio) - consumer thread overhead and queue management costs dominate
4. **Queue capacity impact**: Larger queue (16384) shows 4.62x improvement at 4,096 orders but only 1.27x at 100,000 orders - diminishing returns at scale
5. **Multiple instruments**: Slightly reduces contention but SPSC still slower at scale
6. **Cancel rate**: Higher cancel rate (15%) has minimal impact on SPSC vs mutex ratio

**What the measurements prove**:
- SPSC benchmarks are now correct (no silent drops, proper backpressure, submitted == processed)
- SPSC shows advantage at small workloads but disadvantage at large workloads
- Queue capacity affects performance but doesn't change the fundamental trend
- The architectural trade-off (async vs sync) has measurable costs

**What the measurements do NOT prove**:
- SPSC is intrinsically slower than mutex - the difference may be due to implementation details, queue overhead, or consumer thread scheduling
- SPSC is unsuitable for production - the benchmark includes consumer drain time which may not be required in all scenarios
- The architectural trade-off is inherently worse - this is a design choice with different use cases

---

## Phase 6 Optimization (Profile and Optimize Hot Path)

### Workload
Mixed workload, 100,000 orders, Release build

### Configuration
- **Baseline**: V1 with object pool before optimization
- **Optimized**: V1 with object pool after applying `emplace_back` for Trade and MarketDataEvent vectors

### Results (Mixed Workload, 100K orders)

| Metric | Baseline | Optimized | Change |
|--------|----------|-----------|--------|
| CPU Time (ns) | 22,460,000 | 19,520,000 | -13.1% |
| Throughput (ops/sec) | 4,460,000 | 5,150,000 | +15.3% |

### Full Benchmark Suite Results (After Optimization)

| Workload | Orders | Baseline Throughput | Optimized Throughput | Change |
|----------|--------|-------------------|---------------------|--------|
| Add-Only | 100,000 | 5.71M | 6.52M | +14.2% |
| Add+Cancel | 100,000 | 4.43M | 5.70M | +28.7% |
| Add+Match | 100,000 | 5.38M | 6.09M | +13.2% |
| Mixed | 100,000 | 4.46M | 5.15M | +15.3% |

### Experiments Tried

1. **Hash table capacity reservation**: 2.7% improvement → REVERTED (insufficient benefit vs memory overhead)
2. **Price level vector capacity reservation**: 11.7% slower → REVERTED (cache inefficiency from over-allocation)
3. **emplace_back for vectors**: 15.3% improvement → KEPT

### Interpretation

The `emplace_back` optimization provides a measured 15.3% throughput improvement on large workloads. Other optimizations were tried but reverted due to insufficient benefit or negative impact on performance.

### Metrics Measured
- CPU time (nanoseconds)
- Throughput (orders/second)

### Source
`docs/PERFORMANCE_NOTES.md` (Phase 6 section)

---

## Summary of All Improvements

### V0 → V1 (Cache-Friendly)
- Add+Cancel (100K orders): 13.7K → 3.81M ops/sec (**278x speedup**)
- Mixed (100K orders): 26.7K → 3.80M ops/sec (**142x speedup**)

### V1 → V1 + Object Pool
- Add-Only (100K orders): 5.24M → 7.81M ops/sec (**+49%**)
- Add+Cancel (100K orders): 5.26M → 7.26M ops/sec (**+38%**)
- Add+Match (100K orders): 7.73M → 9.44M ops/sec (**+22%**)
- Mixed (100K orders): 5.47M → 7.02M ops/sec (**+28%**)

### Phase 6 Optimization
- Mixed (100K orders): 4.46M → 5.15M ops/sec (**+15.3%**)

### Mutex vs SPSC (100K orders)
- **Phase 5C (DEBUG build)**: Mutex 1 thread: 197K ops/sec, SPSC: 4.99M ops/sec (**25.3x speedup**) - ALSO INVALID (same silent-drop bug)
- **Phase 7 (Release build - CORRECTED 2026-10-04)**: Mutex 1 thread: 3.43M ops/sec (mean), 3.30M ops/sec (median), SPSC: 1.74M ops/sec (mean), 1.65M ops/sec (median) (**0.51x speedup - SPSC is slower**)

### Overall Progression (Mixed Workload, 100K orders)

| Implementation | Throughput (ops/sec) | vs Previous |
|----------------|---------------------|------------|
| V0 baseline | 26,658 | - |
| V1 cache-friendly | 3,801,078 | **142x** |
| V1 + object pool | 7,02M | 1.85x |
| Phase 6 optimized | 5.15M (baseline 4.46M) | 15.3% over pool baseline |
| SPSC (corrected) | 1.74M | Not applicable (different architecture) |

---

## Metrics Summary

### Throughput
Measured as orders/second across all benchmarks. Primary metric for comparing implementation performance.

### Latency
- p50, p95, p99 percentiles measured for V0 vs V1 and object pool comparisons
- Latency not measured for mutex vs SPSC comparison (only throughput)
- End-to-end latency not measured for SPSC architecture

### CPU Time
Measured in nanoseconds (small workloads) or milliseconds (large workloads) for V0 vs V1 and object pool comparisons.

### Wall Time
Measured in milliseconds for mutex baseline (measures total execution time including thread overhead).

### Not Measured
- CPU instructions, cycles, cache misses, branch misses (perf/valgrind unavailable in WSL environment)
- Detailed profiling data (no profiler tools available)
- End-to-end latency for SPSC architecture
- Lock contention statistics
- Memory usage patterns

---

## Reproducibility

All results can be reproduced using the benchmark commands documented in README.md. Benchmark workloads use deterministic RNG seeds for consistency across runs.

Note: Normal environmental variation may cause small differences in exact numbers between runs. All results reported here are from actual benchmark runs in the documented test environment.
