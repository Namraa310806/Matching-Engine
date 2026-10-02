#include <benchmark/benchmark.h>
#include <engine/orderbook.hpp>
#include <engine/orderbook_v1.hpp>
#include <engine/types.hpp>
#include "order_generator.hpp"
#include <vector>
#include <algorithm>
#include <numeric>

// Benchmark harness for Phase 4A v1 cache-friendly order book
// Uses the same deterministic workloads as Phase 3 for fair comparison

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
// V0 Benchmarks (original implementation)
// ============================================================================

// Benchmark: Add-only workload (v0)
static void BM_V0_AddOnly(benchmark::State& state) {
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
        engine::OrderBook orderbook;

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

// Benchmark: Add + Cancel workload (v0)
static void BM_V0_AddCancel(benchmark::State& state) {
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
        engine::OrderBook orderbook;

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

// Benchmark: Add + Match workload (v0)
static void BM_V0_AddMatch(benchmark::State& state) {
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
        engine::OrderBook orderbook;

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

// Benchmark: Mixed workload (v0)
static void BM_V0_MixedWorkload(benchmark::State& state) {
    const size_t num_orders = state.range(0);

    auto orders = generate_deterministic_workload(num_orders, 0.9, 0.1, 11111);

    for (auto _ : state) {
        engine::OrderBook orderbook;

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
// V1 Benchmarks (cache-friendly implementation)
// ============================================================================

// Benchmark: Add-only workload (v1)
static void BM_V1_AddOnly(benchmark::State& state) {
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

// Benchmark: Add + Cancel workload (v1)
static void BM_V1_AddCancel(benchmark::State& state) {
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

// Benchmark: Add + Match workload (v1)
static void BM_V1_AddMatch(benchmark::State& state) {
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

// Benchmark: Mixed workload (v1)
static void BM_V1_MixedWorkload(benchmark::State& state) {
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

// Register benchmarks with different workload sizes
// V0 benchmarks
BENCHMARK(BM_V0_AddOnly)->Range(100, 100000);
BENCHMARK(BM_V0_AddCancel)->Range(100, 100000);
BENCHMARK(BM_V0_AddMatch)->Range(100, 100000);
BENCHMARK(BM_V0_MixedWorkload)->Range(100, 100000);

// V1 benchmarks
BENCHMARK(BM_V1_AddOnly)->Range(100, 100000);
BENCHMARK(BM_V1_AddCancel)->Range(100, 100000);
BENCHMARK(BM_V1_AddMatch)->Range(100, 100000);
BENCHMARK(BM_V1_MixedWorkload)->Range(100, 100000);

BENCHMARK_MAIN();
