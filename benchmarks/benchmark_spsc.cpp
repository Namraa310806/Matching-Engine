#include <benchmark/benchmark.h>
#include <engine/spsc_multi_instrument_engine.hpp>
#include <engine/mutex_multi_instrument_engine.hpp>
#include <engine/multi_instrument_engine.hpp>
#include <engine/types.hpp>
#include "order_generator.hpp"
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>

// Benchmark harness for Phase 5: SPSC Ingestion
// Compares SPSC architecture against Phase 5B mutex baseline
// Uses identical workloads for fair comparison

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
// SPSC benchmarks
// ============================================================================

// Benchmark: SPSC architecture with 1 producer / 1 consumer
static void BM_SPSC_1Producer(benchmark::State& state) {
    const size_t num_orders = state.range(0);

    auto orders = generate_deterministic_workload(num_orders, 0.9, 0.1, 12345);

    for (auto& order : orders) {
        if (order.side == engine::Side::Buy) {
            order.price = 99000 + (order.sequence % 1000);
        } else {
            order.price = 101000 + (order.sequence % 1000);
        }
    }

    for (auto _ : state) {
        engine::SpscMultiInstrumentEngine<1024> engine;

        std::vector<engine::OrderId> generated_order_ids;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                auto [success, order_id] = engine.submit_order("INST1", order);
                if (success) {
                    generated_order_ids.push_back(order_id);
                }
            } else if (workload_order.action == tools::OrderAction::SubmitMarket) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.quantity,
                    workload_order.sequence
                );
                auto [success, order_id] = engine.submit_order("INST1", order);
                if (success) {
                    generated_order_ids.push_back(order_id);
                }
            } else if (workload_order.action == tools::OrderAction::Cancel) {
                if (!generated_order_ids.empty()) {
                    engine.cancel_order(generated_order_ids.back());
                    generated_order_ids.pop_back();
                }
            }
        }

        // Wait for matching thread to process all commands
        engine.stop();
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// Benchmark: SPSC with same instrument (high contention on matching thread)
static void BM_SPSC_SameInstrument(benchmark::State& state) {
    const size_t num_orders = state.range(0);

    auto orders = generate_deterministic_workload(num_orders, 0.9, 0.1, 12345);

    for (auto& order : orders) {
        if (order.side == engine::Side::Buy) {
            order.price = 99000 + (order.sequence % 1000);
        } else {
            order.price = 101000 + (order.sequence % 1000);
        }
    }

    for (auto _ : state) {
        engine::SpscMultiInstrumentEngine<1024> engine;

        std::vector<engine::OrderId> generated_order_ids;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                auto [success, order_id] = engine.submit_order("INST1", order);
                if (success) {
                    generated_order_ids.push_back(order_id);
                }
            } else if (workload_order.action == tools::OrderAction::SubmitMarket) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.quantity,
                    workload_order.sequence
                );
                auto [success, order_id] = engine.submit_order("INST1", order);
                if (success) {
                    generated_order_ids.push_back(order_id);
                }
            } else if (workload_order.action == tools::OrderAction::Cancel) {
                if (!generated_order_ids.empty()) {
                    engine.cancel_order(generated_order_ids.back());
                    generated_order_ids.pop_back();
                }
            }
        }

        engine.stop();
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// Benchmark: SPSC with multiple instruments
static void BM_SPSC_MultipleInstruments(benchmark::State& state) {
    const size_t num_orders = state.range(0);

    auto orders = generate_deterministic_workload(num_orders, 0.9, 0.1, 12345);

    for (auto& order : orders) {
        if (order.side == engine::Side::Buy) {
            order.price = 99000 + (order.sequence % 1000);
        } else {
            order.price = 101000 + (order.sequence % 1000);
        }
    }

    for (auto _ : state) {
        engine::SpscMultiInstrumentEngine<1024> engine;

        std::vector<engine::OrderId> generated_order_ids;

        for (size_t i = 0; i < orders.size(); ++i) {
            const auto& workload_order = orders[i];
            std::string instrument = "INST" + std::to_string(i % 4);

            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                engine.submit_order(instrument, order);
            } else if (workload_order.action == tools::OrderAction::SubmitMarket) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.quantity,
                    workload_order.sequence
                );
                engine.submit_order(instrument, order);
            } else if (workload_order.action == tools::OrderAction::Cancel) {
                if (!generated_order_ids.empty()) {
                    engine.cancel_order(generated_order_ids.back());
                    generated_order_ids.pop_back();
                }
            }
        }

        engine.stop();
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// Benchmark: SPSC with mixed activity (add/cancel/match)
static void BM_SPSC_MixedActivity(benchmark::State& state) {
    const size_t num_orders = state.range(0);

    auto orders = generate_deterministic_workload(num_orders, 0.9, 0.15, 54321);

    for (auto& order : orders) {
        if (order.side == engine::Side::Buy) {
            order.price = 99000 + (order.sequence % 1000);
        } else {
            order.price = 101000 + (order.sequence % 1000);
        }
    }

    for (auto _ : state) {
        engine::SpscMultiInstrumentEngine<1024> engine;

        std::vector<engine::OrderId> generated_order_ids;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                auto [success, order_id] = engine.submit_order("INST1", order);
                if (success) {
                    generated_order_ids.push_back(order_id);
                }
            } else if (workload_order.action == tools::OrderAction::SubmitMarket) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.quantity,
                    workload_order.sequence
                );
                auto [success, order_id] = engine.submit_order("INST1", order);
                if (success) {
                    generated_order_ids.push_back(order_id);
                }
            } else if (workload_order.action == tools::OrderAction::Cancel) {
                if (!generated_order_ids.empty()) {
                    engine.cancel_order(generated_order_ids.back());
                    generated_order_ids.pop_back();
                }
            }
        }

        engine.stop();
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// ============================================================================
// Mutex baseline benchmarks (identical workloads for comparison)
// ============================================================================

// Benchmark: Mutex 1 thread (baseline comparison)
static void BM_Mutex_1Thread_SameWorkload(benchmark::State& state) {
    const size_t num_orders = state.range(0);

    auto orders = generate_deterministic_workload(num_orders, 0.9, 0.1, 12345);

    for (auto& order : orders) {
        if (order.side == engine::Side::Buy) {
            order.price = 99000 + (order.sequence % 1000);
        } else {
            order.price = 101000 + (order.sequence % 1000);
        }
    }

    for (auto _ : state) {
        engine::MutexMultiInstrumentEngine engine;

        std::vector<engine::OrderId> generated_order_ids;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                auto [trades, events] = engine.submit_order("INST1", order);
                if (!events.empty()) {
                    generated_order_ids.push_back(events[0].order_id);
                }
            } else if (workload_order.action == tools::OrderAction::SubmitMarket) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.quantity,
                    workload_order.sequence
                );
                auto [trades, events] = engine.submit_order("INST1", order);
                if (!events.empty()) {
                    generated_order_ids.push_back(events[0].order_id);
                }
            } else if (workload_order.action == tools::OrderAction::Cancel) {
                if (!generated_order_ids.empty()) {
                    engine.cancel_order(generated_order_ids.back());
                    generated_order_ids.pop_back();
                }
            }
        }
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// ============================================================================
// Register benchmarks
// ============================================================================

// SPSC benchmarks
BENCHMARK(BM_SPSC_1Producer)->Range(1000, 100000);
BENCHMARK(BM_SPSC_SameInstrument)->Range(1000, 100000);
BENCHMARK(BM_SPSC_MultipleInstruments)->Range(1000, 100000);
BENCHMARK(BM_SPSC_MixedActivity)->Range(1000, 100000);

// Mutex baseline (for comparison)
BENCHMARK(BM_Mutex_1Thread_SameWorkload)->Range(1000, 100000);

BENCHMARK_MAIN();
