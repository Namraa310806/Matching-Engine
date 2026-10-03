#include <benchmark/benchmark.h>
#include <engine/mutex_multi_instrument_engine.hpp>
#include <engine/multi_instrument_engine.hpp>
#include <engine/types.hpp>
#include "order_generator.hpp"
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>

// Benchmark harness for Phase 5B: Mutex Concurrency Baseline
// Compares single-threaded vs mutex-protected multi-threaded workloads
// Measures synchronization overhead and contention characteristics

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
// Single-threaded baseline (for comparison)
// ============================================================================

// Benchmark: Single-threaded baseline using original MultiInstrumentEngine
static void BM_SingleThreaded_Baseline(benchmark::State& state) {
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
        engine::MultiInstrumentEngine engine;

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

        benchmark::DoNotOptimize(engine);
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// ============================================================================
// Mutex-protected benchmarks with varying thread counts
// ============================================================================

// Benchmark: Mutex implementation with 1 thread (measures lock overhead without contention)
static void BM_Mutex_1Thread(benchmark::State& state) {
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

        benchmark::DoNotOptimize(engine);
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// Benchmark: Mutex implementation with 2 threads
static void BM_Mutex_2Threads(benchmark::State& state) {
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
        std::atomic<size_t> order_index{0};
        std::vector<engine::OrderId> generated_order_ids;
        std::mutex ids_mutex;

        auto worker = [&]() {
            std::vector<engine::OrderId> local_ids;
            while (true) {
                size_t idx = order_index.fetch_add(1);
                if (idx >= orders.size()) break;

                const auto& workload_order = orders[idx];
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
                        local_ids.push_back(events[0].order_id);
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
                        local_ids.push_back(events[0].order_id);
                    }
                } else if (workload_order.action == tools::OrderAction::Cancel) {
                    // Cancel a previously generated order ID
                    if (!local_ids.empty()) {
                        engine.cancel_order(local_ids.back());
                        local_ids.pop_back();
                    }
                }
            }

            // Merge local IDs for potential use by other threads
            std::lock_guard<std::mutex> lock(ids_mutex);
            generated_order_ids.insert(generated_order_ids.end(),
                                       local_ids.begin(), local_ids.end());
        };

        std::thread t1(worker);
        std::thread t2(worker);
        t1.join();
        t2.join();

        benchmark::DoNotOptimize(engine);
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// Benchmark: Mutex implementation with 4 threads
static void BM_Mutex_4Threads(benchmark::State& state) {
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
        std::atomic<size_t> order_index{0};
        std::vector<engine::OrderId> generated_order_ids;
        std::mutex ids_mutex;

        auto worker = [&]() {
            std::vector<engine::OrderId> local_ids;
            while (true) {
                size_t idx = order_index.fetch_add(1);
                if (idx >= orders.size()) break;

                const auto& workload_order = orders[idx];
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
                        local_ids.push_back(events[0].order_id);
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
                        local_ids.push_back(events[0].order_id);
                    }
                } else if (workload_order.action == tools::OrderAction::Cancel) {
                    // Cancel a previously generated order ID
                    if (!local_ids.empty()) {
                        engine.cancel_order(local_ids.back());
                        local_ids.pop_back();
                    }
                }
            }

            // Merge local IDs for potential use by other threads
            std::lock_guard<std::mutex> lock(ids_mutex);
            generated_order_ids.insert(generated_order_ids.end(),
                                       local_ids.begin(), local_ids.end());
        };

        std::thread t1(worker);
        std::thread t2(worker);
        std::thread t3(worker);
        std::thread t4(worker);
        t1.join();
        t2.join();
        t3.join();
        t4.join();

        benchmark::DoNotOptimize(engine);
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// Benchmark: Mutex implementation with 8 threads
static void BM_Mutex_8Threads(benchmark::State& state) {
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
        std::atomic<size_t> order_index{0};
        std::vector<engine::OrderId> generated_order_ids;
        std::mutex ids_mutex;

        auto worker = [&]() {
            std::vector<engine::OrderId> local_ids;
            while (true) {
                size_t idx = order_index.fetch_add(1);
                if (idx >= orders.size()) break;

                const auto& workload_order = orders[idx];
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
                        local_ids.push_back(events[0].order_id);
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
                        local_ids.push_back(events[0].order_id);
                    }
                } else if (workload_order.action == tools::OrderAction::Cancel) {
                    // Cancel a previously generated order ID
                    if (!local_ids.empty()) {
                        engine.cancel_order(local_ids.back());
                        local_ids.pop_back();
                    }
                }
            }

            // Merge local IDs for potential use by other threads
            std::lock_guard<std::mutex> lock(ids_mutex);
            generated_order_ids.insert(generated_order_ids.end(),
                                       local_ids.begin(), local_ids.end());
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i) {
            threads.emplace_back(worker);
        }
        for (auto& t : threads) {
            t.join();
        }

        benchmark::DoNotOptimize(engine);
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// ============================================================================
// Workload A: Same instrument (high contention)
// ============================================================================

// Benchmark: 4 threads, same instrument (high contention)
static void BM_Mutex_4Threads_SameInstrument(benchmark::State& state) {
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
        std::atomic<size_t> order_index{0};
        std::vector<engine::OrderId> generated_order_ids;
        std::mutex ids_mutex;

        auto worker = [&]() {
            std::vector<engine::OrderId> local_ids;
            while (true) {
                size_t idx = order_index.fetch_add(1);
                if (idx >= orders.size()) break;

                const auto& workload_order = orders[idx];
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
                        local_ids.push_back(events[0].order_id);
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
                        local_ids.push_back(events[0].order_id);
                    }
                } else if (workload_order.action == tools::OrderAction::Cancel) {
                    // Cancel a previously generated order ID
                    if (!local_ids.empty()) {
                        engine.cancel_order(local_ids.back());
                        local_ids.pop_back();
                    }
                }
            }

            // Merge local IDs for potential use by other threads
            std::lock_guard<std::mutex> lock(ids_mutex);
            generated_order_ids.insert(generated_order_ids.end(),
                                       local_ids.begin(), local_ids.end());
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back(worker);
        }
        for (auto& t : threads) {
            t.join();
        }

        benchmark::DoNotOptimize(engine);
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// ============================================================================
// Workload B: Multiple instruments (lower contention per instrument)
// ============================================================================

// Benchmark: 4 threads, 4 instruments (distributes contention)
static void BM_Mutex_4Threads_FourInstruments(benchmark::State& state) {
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
        std::atomic<size_t> order_index{0};
        std::vector<engine::OrderId> generated_order_ids;
        std::mutex ids_mutex;

        auto worker = [&](int thread_id) {
            std::string instrument = "INST" + std::to_string(thread_id);
            std::vector<engine::OrderId> local_ids;
            while (true) {
                size_t idx = order_index.fetch_add(1);
                if (idx >= orders.size()) break;

                const auto& workload_order = orders[idx];
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
                        local_ids.push_back(events[0].order_id);
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
                        local_ids.push_back(events[0].order_id);
                    }
                } else if (workload_order.action == tools::OrderAction::Cancel) {
                    // Cancel a previously generated order ID
                    if (!local_ids.empty()) {
                        engine.cancel_order(local_ids.back());
                        local_ids.pop_back();
                    }
                }
            }

            // Merge local IDs for potential use by other threads
            std::lock_guard<std::mutex> lock(ids_mutex);
            generated_order_ids.insert(generated_order_ids.end(),
                                       local_ids.begin(), local_ids.end());
        };

        std::thread t1(worker, 0);
        std::thread t2(worker, 1);
        std::thread t3(worker, 2);
        std::thread t4(worker, 3);
        t1.join();
        t2.join();
        t3.join();
        t4.join();

        benchmark::DoNotOptimize(engine);
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// Benchmark: 8 threads, 8 instruments
static void BM_Mutex_8Threads_EightInstruments(benchmark::State& state) {
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
        std::atomic<size_t> order_index{0};
        std::vector<engine::OrderId> generated_order_ids;
        std::mutex ids_mutex;

        auto worker = [&](int thread_id) {
            std::string instrument = "INST" + std::to_string(thread_id);
            std::vector<engine::OrderId> local_ids;
            while (true) {
                size_t idx = order_index.fetch_add(1);
                if (idx >= orders.size()) break;

                const auto& workload_order = orders[idx];
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
                        local_ids.push_back(events[0].order_id);
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
                        local_ids.push_back(events[0].order_id);
                    }
                } else if (workload_order.action == tools::OrderAction::Cancel) {
                    // Cancel a previously generated order ID
                    if (!local_ids.empty()) {
                        engine.cancel_order(local_ids.back());
                        local_ids.pop_back();
                    }
                }
            }

            // Merge local IDs for potential use by other threads
            std::lock_guard<std::mutex> lock(ids_mutex);
            generated_order_ids.insert(generated_order_ids.end(),
                                       local_ids.begin(), local_ids.end());
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i) {
            threads.emplace_back(worker, i);
        }
        for (auto& t : threads) {
            t.join();
        }

        benchmark::DoNotOptimize(engine);
    }

    state.SetItemsProcessed(state.iterations() * num_orders);
    state.counters["orders_per_sec"] = benchmark::Counter(
        state.iterations() * num_orders,
        benchmark::Counter::kIsRate
    );
}

// ============================================================================
// Workload C: Mixed activity (add/cancel/match)
// ============================================================================

// Benchmark: 4 threads, mixed activity
static void BM_Mutex_4Threads_MixedActivity(benchmark::State& state) {
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
        std::atomic<size_t> order_index{0};
        std::vector<engine::OrderId> generated_order_ids;
        std::mutex ids_mutex;

        auto worker = [&]() {
            std::vector<engine::OrderId> local_ids;
            while (true) {
                size_t idx = order_index.fetch_add(1);
                if (idx >= orders.size()) break;

                const auto& workload_order = orders[idx];
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
                        local_ids.push_back(events[0].order_id);
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
                        local_ids.push_back(events[0].order_id);
                    }
                } else if (workload_order.action == tools::OrderAction::Cancel) {
                    // Try to cancel a locally-generated ID first
                    if (!local_ids.empty()) {
                        engine.cancel_order(local_ids.back());
                        local_ids.pop_back();
                    }
                }
            }

            // Merge local IDs for potential use by other threads
            std::lock_guard<std::mutex> lock(ids_mutex);
            generated_order_ids.insert(generated_order_ids.end(),
                                       local_ids.begin(), local_ids.end());
        };

        std::thread t1(worker);
        std::thread t2(worker);
        std::thread t3(worker);
        std::thread t4(worker);
        t1.join();
        t2.join();
        t3.join();
        t4.join();

        benchmark::DoNotOptimize(engine);
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

// Single-threaded baseline
BENCHMARK(BM_SingleThreaded_Baseline)->Range(1000, 100000);

// Mutex implementation: 1, 2, 4, 8 threads
BENCHMARK(BM_Mutex_1Thread)->Range(1000, 100000);
BENCHMARK(BM_Mutex_2Threads)->Range(1000, 100000);
BENCHMARK(BM_Mutex_4Threads)->Range(1000, 100000);
BENCHMARK(BM_Mutex_8Threads)->Range(1000, 100000);

// Workload A: Same instrument (high contention)
BENCHMARK(BM_Mutex_4Threads_SameInstrument)->Range(1000, 100000);

// Workload B: Multiple instruments
BENCHMARK(BM_Mutex_4Threads_FourInstruments)->Range(1000, 100000);
BENCHMARK(BM_Mutex_8Threads_EightInstruments)->Range(1000, 100000);

// Workload C: Mixed activity
BENCHMARK(BM_Mutex_4Threads_MixedActivity)->Range(1000, 100000);

BENCHMARK_MAIN();
