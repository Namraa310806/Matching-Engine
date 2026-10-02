#include <benchmark/benchmark.h>
#include <engine/orderbook_v1.hpp>
#include <engine/orderbook_v1_pool.hpp>
#include <engine/types.hpp>
#include "order_generator.hpp"
#include <vector>
#include <algorithm>
#include <numeric>

// Benchmark harness for Phase 4: Object Pool vs Baseline
// Compares OrderBookV1 (baseline new/delete) vs OrderBookV1Pool (object pool)
// Uses the same deterministic workloads for fair comparison

// Helper to generate deterministic workload orders (outside timed section)
std::vector<tools::WorkloadOrder> generate_deterministic_workload(
    size_t num_orders,
    double limit_ratio,
    double cancel_rate,
    uint64_t seed
) {
    tools::GeneratorConfig config;
    config.num_orders = num_orders;
    config.limit_ratio = limit_ratio;
    config.cancel_rate = cancel_rate;
    config.buy_ratio = 0.5;
    config.rng_seed = seed;
    config.min_qty = 1;
    config.max_qty = 100;

    tools::OrderGenerator generator(config);
    generator.generate();
    return generator.orders();
}

// ============================================================================
// Baseline Benchmarks (OrderBookV1 with new/delete)
// ============================================================================

// Benchmark: Add-only workload (baseline)
static void BM_Baseline_AddOnly(benchmark::State& state) {
    const size_t num_orders = state.range(0);

    auto orders = generate_deterministic_workload(num_orders, 1.0, 0.0, 12345);

    for (auto& order : orders) {
        if (order.side == engine::Side::Buy) {
            order.price = 99000 + (order.sequence % 1000);
        } else {
            order.price = 101000 + (order.sequence % 1000);
        }
    }

    for (auto _ : state) {
        engine::OrderBookV1 orderbook;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    workload_order.order_id,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                orderbook.submit_order(order);
            }
        }

        benchmark::DoNotOptimize(orderbook);
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// Benchmark: Add + Cancel workload (baseline)
static void BM_Baseline_AddCancel(benchmark::State& state) {
    const size_t num_orders = state.range(0);

    auto orders = generate_deterministic_workload(num_orders, 1.0, 0.2, 54321);

    for (auto& order : orders) {
        if (order.side == engine::Side::Buy) {
            order.price = 99000 + (order.sequence % 1000);
        } else {
            order.price = 101000 + (order.sequence % 1000);
        }
    }

    for (auto _ : state) {
        engine::OrderBookV1 orderbook;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    workload_order.order_id,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                orderbook.submit_order(order);
            } else if (workload_order.action == tools::OrderAction::Cancel) {
                orderbook.cancel_order(workload_order.order_id);
            }
        }

        benchmark::DoNotOptimize(orderbook);
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// Benchmark: Add + Match workload (baseline)
static void BM_Baseline_AddMatch(benchmark::State& state) {
    const size_t num_orders = state.range(0);

    auto orders = generate_deterministic_workload(num_orders, 1.0, 0.0, 98765);

    for (size_t i = 0; i < orders.size(); ++i) {
        orders[i].price = 100000;
        if (i % 2 == 0) {
            orders[i].side = engine::Side::Buy;
        } else {
            orders[i].side = engine::Side::Sell;
        }
    }

    for (auto _ : state) {
        engine::OrderBookV1 orderbook;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    workload_order.order_id,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                orderbook.submit_order(order);
            }
        }

        benchmark::DoNotOptimize(orderbook);
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// Benchmark: Mixed workload (baseline)
static void BM_Baseline_MixedWorkload(benchmark::State& state) {
    const size_t num_orders = state.range(0);

    auto orders = generate_deterministic_workload(num_orders, 0.9, 0.1, 11111);

    for (auto _ : state) {
        engine::OrderBookV1 orderbook;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    workload_order.order_id,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                orderbook.submit_order(order);
            } else if (workload_order.action == tools::OrderAction::SubmitMarket) {
                engine::Order order(
                    workload_order.order_id,
                    workload_order.side,
                    workload_order.quantity,
                    workload_order.sequence
                );
                orderbook.submit_order(order);
            } else if (workload_order.action == tools::OrderAction::Cancel) {
                orderbook.cancel_order(workload_order.order_id);
            }
        }

        benchmark::DoNotOptimize(orderbook);
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// ============================================================================
// Object Pool Benchmarks (OrderBookV1Pool)
// ============================================================================

// Benchmark: Add-only workload (pool)
static void BM_Pool_AddOnly(benchmark::State& state) {
    const size_t num_orders = state.range(0);

    auto orders = generate_deterministic_workload(num_orders, 1.0, 0.0, 12345);

    for (auto& order : orders) {
        if (order.side == engine::Side::Buy) {
            order.price = 99000 + (order.sequence % 1000);
        } else {
            order.price = 101000 + (order.sequence % 1000);
        }
    }

    for (auto _ : state) {
        engine::OrderBookV1Pool orderbook;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    workload_order.order_id,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                orderbook.submit_order(order);
            }
        }

        benchmark::DoNotOptimize(orderbook);
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// Benchmark: Add + Cancel workload (pool)
static void BM_Pool_AddCancel(benchmark::State& state) {
    const size_t num_orders = state.range(0);

    auto orders = generate_deterministic_workload(num_orders, 1.0, 0.2, 54321);

    for (auto& order : orders) {
        if (order.side == engine::Side::Buy) {
            order.price = 99000 + (order.sequence % 1000);
        } else {
            order.price = 101000 + (order.sequence % 1000);
        }
    }

    for (auto _ : state) {
        engine::OrderBookV1Pool orderbook;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    workload_order.order_id,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                orderbook.submit_order(order);
            } else if (workload_order.action == tools::OrderAction::Cancel) {
                orderbook.cancel_order(workload_order.order_id);
            }
        }

        benchmark::DoNotOptimize(orderbook);
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// Benchmark: Add + Match workload (pool)
static void BM_Pool_AddMatch(benchmark::State& state) {
    const size_t num_orders = state.range(0);

    auto orders = generate_deterministic_workload(num_orders, 1.0, 0.0, 98765);

    for (size_t i = 0; i < orders.size(); ++i) {
        orders[i].price = 100000;
        if (i % 2 == 0) {
            orders[i].side = engine::Side::Buy;
        } else {
            orders[i].side = engine::Side::Sell;
        }
    }

    for (auto _ : state) {
        engine::OrderBookV1Pool orderbook;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    workload_order.order_id,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                orderbook.submit_order(order);
            }
        }

        benchmark::DoNotOptimize(orderbook);
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// Benchmark: Mixed workload (pool)
static void BM_Pool_MixedWorkload(benchmark::State& state) {
    const size_t num_orders = state.range(0);

    auto orders = generate_deterministic_workload(num_orders, 0.9, 0.1, 11111);

    for (auto _ : state) {
        engine::OrderBookV1Pool orderbook;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    workload_order.order_id,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                orderbook.submit_order(order);
            } else if (workload_order.action == tools::OrderAction::SubmitMarket) {
                engine::Order order(
                    workload_order.order_id,
                    workload_order.side,
                    workload_order.quantity,
                    workload_order.sequence
                );
                orderbook.submit_order(order);
            } else if (workload_order.action == tools::OrderAction::Cancel) {
                orderbook.cancel_order(workload_order.order_id);
            }
        }

        benchmark::DoNotOptimize(orderbook);
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// ============================================================================
// Pool Statistics Benchmark
// ============================================================================

// Benchmark: Measure pool hit rate and allocation behavior
static void BM_Pool_Statistics(benchmark::State& state) {
    const size_t num_orders = state.range(0);

    auto orders = generate_deterministic_workload(num_orders, 0.9, 0.1, 11111);

    for (auto _ : state) {
        engine::OrderBookV1Pool orderbook;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    workload_order.order_id,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                orderbook.submit_order(order);
            } else if (workload_order.action == tools::OrderAction::SubmitMarket) {
                engine::Order order(
                    workload_order.order_id,
                    workload_order.side,
                    workload_order.quantity,
                    workload_order.sequence
                );
                orderbook.submit_order(order);
            } else if (workload_order.action == tools::OrderAction::Cancel) {
                orderbook.cancel_order(workload_order.order_id);
            }
        }

        auto stats = orderbook.pool_stats();
        state.counters["pool_hits"] = benchmark::Counter(stats.pool_hits);
        state.counters["pool_misses"] = benchmark::Counter(stats.pool_misses);
        state.counters["hit_rate"] = benchmark::Counter(
            stats.pool_hits > 0 ? static_cast<double>(stats.pool_hits) / (stats.pool_hits + stats.pool_misses) : 0.0
        );
        state.counters["chunks"] = benchmark::Counter(stats.chunk_count);
        state.counters["total_capacity"] = benchmark::Counter(stats.total_capacity);

        benchmark::DoNotOptimize(orderbook);
    }
}

// Register benchmarks with different workload sizes
// Baseline benchmarks
BENCHMARK(BM_Baseline_AddOnly)->Range(100, 100000);
BENCHMARK(BM_Baseline_AddCancel)->Range(100, 100000);
BENCHMARK(BM_Baseline_AddMatch)->Range(100, 100000);
BENCHMARK(BM_Baseline_MixedWorkload)->Range(100, 100000);

// Pool benchmarks
BENCHMARK(BM_Pool_AddOnly)->Range(100, 100000);
BENCHMARK(BM_Pool_AddCancel)->Range(100, 100000);
BENCHMARK(BM_Pool_AddMatch)->Range(100, 100000);
BENCHMARK(BM_Pool_MixedWorkload)->Range(100, 100000);

// Pool statistics
BENCHMARK(BM_Pool_Statistics)->Range(100, 100000);

BENCHMARK_MAIN();
