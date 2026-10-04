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
        size_t submitted_count = 0;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                // Apply backpressure: retry until enqueue succeeds
                while (true) {
                    auto [success, order_id] = engine.submit_order("INST1", order);
                    if (success) {
                        generated_order_ids.push_back(order_id);
                        submitted_count++;
                        break;
                    }
                    std::this_thread::yield();
                }
            } else if (workload_order.action == tools::OrderAction::SubmitMarket) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.quantity,
                    workload_order.sequence
                );
                // Apply backpressure: retry until enqueue succeeds
                while (true) {
                    auto [success, order_id] = engine.submit_order("INST1", order);
                    if (success) {
                        generated_order_ids.push_back(order_id);
                        submitted_count++;
                        break;
                    }
                    std::this_thread::yield();
                }
            } else if (workload_order.action == tools::OrderAction::Cancel) {
                if (!generated_order_ids.empty()) {
                    // Apply backpressure: retry until enqueue succeeds
                    while (!engine.cancel_order(generated_order_ids.back())) {
                        std::this_thread::yield();
                    }
                    generated_order_ids.pop_back();
                    submitted_count++;
                }
            }
        }

        // Wait for matching thread to process all commands
        engine.stop();

        // Verify all submitted commands were processed
        uint64_t processed = engine.processed_command_count();
        if (submitted_count != processed) {
            state.SkipWithError("Submitted != processed commands");
        }
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
        size_t submitted_count = 0;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                while (true) {
                    auto [success, order_id] = engine.submit_order("INST1", order);
                    if (success) {
                        generated_order_ids.push_back(order_id);
                        submitted_count++;
                        break;
                    }
                    std::this_thread::yield();
                }
            } else if (workload_order.action == tools::OrderAction::SubmitMarket) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.quantity,
                    workload_order.sequence
                );
                while (true) {
                    auto [success, order_id] = engine.submit_order("INST1", order);
                    if (success) {
                        generated_order_ids.push_back(order_id);
                        submitted_count++;
                        break;
                    }
                    std::this_thread::yield();
                }
            } else if (workload_order.action == tools::OrderAction::Cancel) {
                if (!generated_order_ids.empty()) {
                    while (!engine.cancel_order(generated_order_ids.back())) {
                        std::this_thread::yield();
                    }
                    generated_order_ids.pop_back();
                    submitted_count++;
                }
            }
        }

        engine.stop();

        uint64_t processed = engine.processed_command_count();
        if (submitted_count != processed) {
            state.SkipWithError("Submitted != processed commands");
        }
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// Benchmark: SPSC with multiple instruments
// Distributes orders across 4 instruments sequentially (first 1/4 to INST0, next 1/4 to INST1, etc.)
// This matches the mutex baseline workload where each thread is assigned a fixed instrument
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
        size_t submitted_count = 0;

        for (size_t i = 0; i < orders.size(); ++i) {
            const auto& workload_order = orders[i];
            // Distribute sequentially: first 1/4 to INST0, next 1/4 to INST1, etc.
            // This matches the mutex baseline where each thread gets a fixed instrument
            size_t instrument_idx = (i * 4) / orders.size();
            std::string instrument = "INST" + std::to_string(instrument_idx);

            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                while (true) {
                    auto [success, order_id] = engine.submit_order(instrument, order);
                    if (success) {
                        generated_order_ids.push_back(order_id);
                        submitted_count++;
                        break;
                    }
                    std::this_thread::yield();
                }
            } else if (workload_order.action == tools::OrderAction::SubmitMarket) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.quantity,
                    workload_order.sequence
                );
                while (true) {
                    auto [success, order_id] = engine.submit_order(instrument, order);
                    if (success) {
                        generated_order_ids.push_back(order_id);
                        submitted_count++;
                        break;
                    }
                    std::this_thread::yield();
                }
            } else if (workload_order.action == tools::OrderAction::Cancel) {
                if (!generated_order_ids.empty()) {
                    while (!engine.cancel_order(generated_order_ids.back())) {
                        std::this_thread::yield();
                    }
                    generated_order_ids.pop_back();
                    submitted_count++;
                }
            }
        }

        engine.stop();

        uint64_t processed = engine.processed_command_count();
        if (submitted_count != processed) {
            state.SkipWithError("Submitted != processed commands");
        }
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
        size_t submitted_count = 0;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                while (true) {
                    auto [success, order_id] = engine.submit_order("INST1", order);
                    if (success) {
                        generated_order_ids.push_back(order_id);
                        submitted_count++;
                        break;
                    }
                    std::this_thread::yield();
                }
            } else if (workload_order.action == tools::OrderAction::SubmitMarket) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.quantity,
                    workload_order.sequence
                );
                while (true) {
                    auto [success, order_id] = engine.submit_order("INST1", order);
                    if (success) {
                        generated_order_ids.push_back(order_id);
                        submitted_count++;
                        break;
                    }
                    std::this_thread::yield();
                }
            } else if (workload_order.action == tools::OrderAction::Cancel) {
                if (!generated_order_ids.empty()) {
                    while (!engine.cancel_order(generated_order_ids.back())) {
                        std::this_thread::yield();
                    }
                    generated_order_ids.pop_back();
                    submitted_count++;
                }
            }
        }

        engine.stop();

        uint64_t processed = engine.processed_command_count();
        if (submitted_count != processed) {
            state.SkipWithError("Submitted != processed commands");
        }
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

// Benchmark: Mutex 1 thread - same instrument (baseline for BM_SPSC_SameInstrument)
static void BM_Mutex_1Thread_SameInstrument(benchmark::State& state) {
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

// Benchmark: Mutex 1 thread - multiple instruments (baseline for BM_SPSC_MultipleInstruments)
static void BM_Mutex_1Thread_MultipleInstruments(benchmark::State& state) {
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

        for (size_t i = 0; i < orders.size(); ++i) {
            const auto& workload_order = orders[i];
            // Distribute sequentially: first 1/4 to INST0, next 1/4 to INST1, etc.
            size_t instrument_idx = (i * 4) / orders.size();
            std::string instrument = "INST" + std::to_string(instrument_idx);

            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                auto [trades, events] = engine.submit_order(instrument, order);
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
                auto [trades, events] = engine.submit_order(instrument, order);
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

// Benchmark: Mutex 1 thread - mixed activity (baseline for BM_SPSC_MixedActivity)
static void BM_Mutex_1Thread_MixedActivity(benchmark::State& state) {
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
// Queue capacity parameterized benchmarks
// ============================================================================

// Benchmark: SPSC with queue capacity 16384
static void BM_SPSC_1Producer_Capacity16384(benchmark::State& state) {
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
        engine::SpscMultiInstrumentEngine<16384> engine;

        std::vector<engine::OrderId> generated_order_ids;
        size_t submitted_count = 0;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                while (true) {
                    auto [success, order_id] = engine.submit_order("INST1", order);
                    if (success) {
                        generated_order_ids.push_back(order_id);
                        submitted_count++;
                        break;
                    }
                    std::this_thread::yield();
                }
            } else if (workload_order.action == tools::OrderAction::SubmitMarket) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.quantity,
                    workload_order.sequence
                );
                while (true) {
                    auto [success, order_id] = engine.submit_order("INST1", order);
                    if (success) {
                        generated_order_ids.push_back(order_id);
                        submitted_count++;
                        break;
                    }
                    std::this_thread::yield();
                }
            } else if (workload_order.action == tools::OrderAction::Cancel) {
                if (!generated_order_ids.empty()) {
                    while (!engine.cancel_order(generated_order_ids.back())) {
                        std::this_thread::yield();
                    }
                    generated_order_ids.pop_back();
                    submitted_count++;
                }
            }
        }

        engine.stop();

        uint64_t processed = engine.processed_command_count();
        if (submitted_count != processed) {
            state.SkipWithError("Submitted != processed commands");
        }
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// Benchmark: SPSC with queue capacity 65536
static void BM_SPSC_1Producer_Capacity65536(benchmark::State& state) {
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
        engine::SpscMultiInstrumentEngine<65536> engine;

        std::vector<engine::OrderId> generated_order_ids;
        size_t submitted_count = 0;

        for (const auto& workload_order : orders) {
            if (workload_order.action == tools::OrderAction::SubmitLimit) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.price,
                    workload_order.quantity,
                    workload_order.sequence
                );
                while (true) {
                    auto [success, order_id] = engine.submit_order("INST1", order);
                    if (success) {
                        generated_order_ids.push_back(order_id);
                        submitted_count++;
                        break;
                    }
                    std::this_thread::yield();
                }
            } else if (workload_order.action == tools::OrderAction::SubmitMarket) {
                engine::Order order(
                    0,
                    workload_order.side,
                    workload_order.quantity,
                    workload_order.sequence
                );
                while (true) {
                    auto [success, order_id] = engine.submit_order("INST1", order);
                    if (success) {
                        generated_order_ids.push_back(order_id);
                        submitted_count++;
                        break;
                    }
                    std::this_thread::yield();
                }
            } else if (workload_order.action == tools::OrderAction::Cancel) {
                if (!generated_order_ids.empty()) {
                    while (!engine.cancel_order(generated_order_ids.back())) {
                        std::this_thread::yield();
                    }
                    generated_order_ids.pop_back();
                    submitted_count++;
                }
            }
        }

        engine.stop();

        uint64_t processed = engine.processed_command_count();
        if (submitted_count != processed) {
            state.SkipWithError("Submitted != processed commands");
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

// SPSC benchmarks (queue capacity 1024)
BENCHMARK(BM_SPSC_1Producer)->Range(1000, 100000);
BENCHMARK(BM_SPSC_SameInstrument)->Range(1000, 100000);
BENCHMARK(BM_SPSC_MultipleInstruments)->Range(1000, 100000);
BENCHMARK(BM_SPSC_MixedActivity)->Range(1000, 100000);

// SPSC benchmarks with different queue capacities
BENCHMARK(BM_SPSC_1Producer_Capacity16384)->Range(1000, 100000);
BENCHMARK(BM_SPSC_1Producer_Capacity65536)->Range(1000, 100000);

// Mutex baseline (for comparison)
BENCHMARK(BM_Mutex_1Thread_SameInstrument)->Range(1000, 100000);
BENCHMARK(BM_Mutex_1Thread_MultipleInstruments)->Range(1000, 100000);
BENCHMARK(BM_Mutex_1Thread_MixedActivity)->Range(1000, 100000);

BENCHMARK_MAIN();
