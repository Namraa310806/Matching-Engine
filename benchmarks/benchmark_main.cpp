#include <benchmark/benchmark.h>
#include <engine/orderbook.hpp>
#include <engine/types.hpp>
#include "order_generator.hpp"
#include <vector>
#include <algorithm>
#include <numeric>

// Benchmark harness for Phase 3 performance baseline
// Uses deterministic workloads to measure actual engine performance

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

// Benchmark: Add-only workload (no cancels, no matching)
// Orders are priced to not cross (all buys below mid, all sells above mid)
static void BM_AddOnly(benchmark::State& state) {
    const size_t num_orders = state.range(0);
    
    // Generate deterministic workload with no cancels
    auto orders = generate_deterministic_workload(num_orders, 1.0, 0.0, 12345);
    
    // Modify prices to ensure no crossing (buy below mid, sell above mid)
    for (auto& order : orders) {
        if (order.side == engine::Side::Buy) {
            order.price = 99000 + (order.sequence % 1000);  // 99.00 - 99.99
        } else {
            order.price = 101000 + (order.sequence % 1000); // 101.00 - 101.99
        }
    }
    
    // Benchmark the engine execution
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
    
    // Report throughput
    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// Benchmark: Add + Cancel workload
static void BM_AddCancel(benchmark::State& state) {
    const size_t num_orders = state.range(0);
    
    // Generate deterministic workload with cancels
    auto orders = generate_deterministic_workload(num_orders, 1.0, 0.2, 54321);
    
    // Modify prices to ensure no crossing
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

// Benchmark: Add + Match workload (crossing orders to trigger matching)
static void BM_AddMatch(benchmark::State& state) {
    const size_t num_orders = state.range(0);
    
    // Generate deterministic workload with no cancels
    auto orders = generate_deterministic_workload(num_orders, 1.0, 0.0, 98765);
    
    // Modify prices to ensure crossing (alternating sides at same price)
    for (size_t i = 0; i < orders.size(); ++i) {
        orders[i].price = 100000; // All at same price to maximize matching
        // Alternate buy/sell to ensure crossing
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

// Benchmark: Realistic mixed workload (add, cancel, match)
static void BM_MixedWorkload(benchmark::State& state) {
    const size_t num_orders = state.range(0);
    
    // Generate realistic mixed workload
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

// Register benchmarks with different workload sizes
BENCHMARK(BM_AddOnly)->Range(100, 100000);
BENCHMARK(BM_AddCancel)->Range(100, 100000);
BENCHMARK(BM_AddMatch)->Range(100, 100000);
BENCHMARK(BM_MixedWorkload)->Range(100, 100000);

BENCHMARK_MAIN();
