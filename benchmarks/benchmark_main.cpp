#include <benchmark/benchmark.h>

// Trivial benchmark to verify benchmark framework is working
static void BM_Trivial(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(state.iterations());
    }
}
BENCHMARK(BM_Trivial);

BENCHMARK_MAIN();
