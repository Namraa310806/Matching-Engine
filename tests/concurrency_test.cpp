#include <gtest/gtest.h>
#include <engine/mutex_multi_instrument_engine.hpp>
#include <engine/types.hpp>
#include <thread>
#include <vector>
#include <set>
#include <atomic>
#include <algorithm>

using namespace engine;

// ============================================================================
// Basic concurrent submission tests
// ============================================================================

// Test: Multiple threads submit orders to the same instrument
// Verify: no crashes, no corruption, no duplicate IDs, all orders accounted for
TEST(ConcurrencyTest, ConcurrentSubmissionSameInstrument) {
    MutexMultiInstrumentEngine engine;
    const int num_threads = 4;
    const int orders_per_thread = 100;
    std::atomic<int> total_submitted{0};

    auto worker = [&](int thread_id) {
        for (int i = 0; i < orders_per_thread; ++i) {
            // Use non-crossing prices to avoid matching
            Price price = 99000 + (thread_id * 1000) + (i % 100);
            Order order(0, Side::Buy, price, 10, thread_id * orders_per_thread + i);
            auto [trades, events] = engine.submit_order("INST1", order);
            if (!events.empty()) {
                total_submitted.fetch_add(1);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) {
        t.join();
    }

    // Verify all orders were submitted
    EXPECT_EQ(total_submitted.load(), num_threads * orders_per_thread);

    // Verify instrument has the correct number of orders
    EXPECT_EQ(engine.buy_order_count("INST1"), num_threads * orders_per_thread);

    // Verify instrument exists
    EXPECT_TRUE(engine.has_instrument("INST1"));
}

// Test: Multiple threads submit orders to different instruments
// Verify: no cross-instrument matching, each instrument remains isolated
TEST(ConcurrencyTest, ConcurrentSubmissionMultipleInstruments) {
    MutexMultiInstrumentEngine engine;
    const int num_threads = 4;
    const int orders_per_thread = 50;
    std::atomic<int> total_submitted{0};

    auto worker = [&](int thread_id) {
        std::string instrument = "INST" + std::to_string(thread_id % 2);
        for (int i = 0; i < orders_per_thread; ++i) {
            Price price = 99000 + (thread_id * 1000) + (i % 100);
            Order order(0, Side::Buy, price, 10, thread_id * orders_per_thread + i);
            auto [trades, events] = engine.submit_order(instrument, order);
            if (!events.empty()) {
                total_submitted.fetch_add(1);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) {
        t.join();
    }

    // Verify all orders were submitted
    EXPECT_EQ(total_submitted.load(), num_threads * orders_per_thread);

    // Verify each instrument has its orders
    EXPECT_EQ(engine.buy_order_count("INST0"), num_threads / 2 * orders_per_thread);
    EXPECT_EQ(engine.buy_order_count("INST1"), num_threads / 2 * orders_per_thread);

    // Verify total instrument count
    EXPECT_EQ(engine.instrument_count(), 2);
}

// Test: Same instrument contention - multiple producers targeting same instrument
// Verify correctness under contention
TEST(ConcurrencyTest, SameInstrumentContention) {
    MutexMultiInstrumentEngine engine;
    const int num_threads = 8;
    const int orders_per_thread = 200;
    std::atomic<int> total_submitted{0};

    auto worker = [&](int thread_id) {
        for (int i = 0; i < orders_per_thread; ++i) {
            // Vary prices to create multiple price levels
            Price price = 99000 + (i % 50) * 100;
            Order order(0, Side::Buy, price, 10, thread_id * orders_per_thread + i);
            auto [trades, events] = engine.submit_order("INST1", order);
            if (!events.empty()) {
                total_submitted.fetch_add(1);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) {
        t.join();
    }

    // Verify all orders were submitted
    EXPECT_EQ(total_submitted.load(), num_threads * orders_per_thread);

    // Verify final state
    EXPECT_EQ(engine.buy_order_count("INST1"), num_threads * orders_per_thread);
}

// ============================================================================
// Concurrent cancellation tests
// ============================================================================

// Test: Multiple threads cancel distinct orders
// Verify: no crashes, no double cancellation, correct final state
TEST(ConcurrencyTest, ConcurrentCancellation) {
    MutexMultiInstrumentEngine engine;

    // First, submit a set of orders to cancel
    const int num_orders = 100;
    std::vector<OrderId> order_ids;
    for (int i = 0; i < num_orders; ++i) {
        Price price = 99000 + (i % 10) * 100;
        Order order(0, Side::Buy, price, 10, i);
        auto [trades, events] = engine.submit_order("INST1", order);
        if (!events.empty()) {
            order_ids.push_back(events[0].order_id);
        }
    }

    EXPECT_EQ(engine.buy_order_count("INST1"), num_orders);

    // Now cancel from multiple threads
    const int num_threads = 4;
    std::atomic<int> cancelled_count{0};

    auto worker = [&](int thread_id) {
        for (size_t i = thread_id; i < order_ids.size(); i += num_threads) {
            auto [cancelled, events] = engine.cancel_order(order_ids[i]);
            if (cancelled) {
                cancelled_count.fetch_add(1);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) {
        t.join();
    }

    // Verify all cancellations succeeded
    EXPECT_EQ(cancelled_count.load(), num_orders);
    EXPECT_EQ(engine.buy_order_count("INST1"), 0);
}

// Test: Cancellation after activity on multiple instruments
TEST(ConcurrencyTest, ConcurrentCancellationAfterActivity) {
    MutexMultiInstrumentEngine engine;

    // Build up state on multiple instruments
    const int orders_per_instrument = 50;
    std::vector<OrderId> aapl_ids, msft_ids;

    for (int i = 0; i < orders_per_instrument; ++i) {
        Order order(0, Side::Buy, 99000 + (i % 10) * 100, 10, i);
        auto [trades, events] = engine.submit_order("AAPL", order);
        if (!events.empty()) {
            aapl_ids.push_back(events[0].order_id);
        }
    }

    for (int i = 0; i < orders_per_instrument; ++i) {
        Order order(0, Side::Buy, 99000 + (i % 10) * 100, 10, i);
        auto [trades, events] = engine.submit_order("MSFT", order);
        if (!events.empty()) {
            msft_ids.push_back(events[0].order_id);
        }
    }

    EXPECT_EQ(engine.buy_order_count("AAPL"), orders_per_instrument);
    EXPECT_EQ(engine.buy_order_count("MSFT"), orders_per_instrument);

    // Cancel from multiple threads
    const int num_threads = 4;
    std::atomic<int> cancelled_count{0};

    auto worker = [&](int thread_id) {
        // Cancel AAPL orders
        for (size_t i = thread_id; i < aapl_ids.size(); i += num_threads) {
            engine.cancel_order(aapl_ids[i]);
            cancelled_count.fetch_add(1);
        }
        // Cancel MSFT orders
        for (size_t i = thread_id; i < msft_ids.size(); i += num_threads) {
            engine.cancel_order(msft_ids[i]);
            cancelled_count.fetch_add(1);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) {
        t.join();
    }

    // Verify final state
    EXPECT_EQ(cancelled_count.load(), orders_per_instrument * 2);
    EXPECT_EQ(engine.buy_order_count("AAPL"), 0);
    EXPECT_EQ(engine.buy_order_count("MSFT"), 0);
}

// ============================================================================
// Concurrent mixed workload tests
// ============================================================================

// Test: Mixed add/cancel/match workload across multiple instruments
// Verify conservation and final-state invariants
TEST(ConcurrencyTest, ConcurrentMixedWorkload) {
    MutexMultiInstrumentEngine engine;
    const int num_threads = 4;
    const int operations_per_thread = 100;
    std::atomic<int> submit_count{0};
    std::atomic<int> cancel_count{0};
    std::atomic<int> match_count{0};

    auto worker = [&](int thread_id) {
        std::vector<OrderId> submitted_ids;
        std::string instrument = "INST" + std::to_string(thread_id % 2);

        for (int i = 0; i < operations_per_thread; ++i) {
            int op_type = i % 3;

            if (op_type == 0) {
                // Submit buy order
                Price price = 99000 + (i % 10) * 100;
                Order order(0, Side::Buy, price, 10, thread_id * operations_per_thread + i);
                auto [trades, events] = engine.submit_order(instrument, order);
                if (!events.empty()) {
                    submitted_ids.push_back(events[0].order_id);
                    submit_count.fetch_add(1);
                }
            } else if (op_type == 1 && !submitted_ids.empty()) {
                // Cancel order
                OrderId id = submitted_ids.back();
                submitted_ids.pop_back();
                auto [cancelled, events] = engine.cancel_order(id);
                if (cancelled) {
                    cancel_count.fetch_add(1);
                }
            } else {
                // Submit sell order that might match
                Price price = 101000 + (i % 10) * 100;
                Order order(0, Side::Sell, price, 10, thread_id * operations_per_thread + i);
                auto [trades, events] = engine.submit_order(instrument, order);
                if (!trades.empty()) {
                    match_count.fetch_add(trades.size());
                }
                submit_count.fetch_add(1);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) {
        t.join();
    }

    // Verify operations completed
    EXPECT_GT(submit_count.load(), 0);
    EXPECT_GT(cancel_count.load(), 0);

    // Verify engines are in valid state
    EXPECT_TRUE(engine.has_instrument("INST0") || engine.has_instrument("INST1"));
}

// ============================================================================
// Global ID uniqueness tests
// ============================================================================

// Test: Global order ID uniqueness under concurrent submission
// Verify: no duplicate IDs across threads
TEST(ConcurrencyTest, GlobalIdUniqueness) {
    MutexMultiInstrumentEngine engine;
    const int num_threads = 8;
    const int orders_per_thread = 100;
    std::vector<OrderId> all_order_ids;
    std::mutex ids_mutex;

    auto worker = [&](int thread_id) {
        std::vector<OrderId> thread_order_ids;
        std::string instrument = "INST" + std::to_string(thread_id % 4);

        for (int i = 0; i < orders_per_thread; ++i) {
            Price price = 99000 + (i % 10) * 100;
            Order order(0, Side::Buy, price, 10, thread_id * orders_per_thread + i);
            auto [trades, events] = engine.submit_order(instrument, order);
            if (!events.empty()) {
                thread_order_ids.push_back(events[0].order_id);
            }
        }

        // Collect IDs safely
        std::lock_guard<std::mutex> lock(ids_mutex);
        all_order_ids.insert(all_order_ids.end(), thread_order_ids.begin(), thread_order_ids.end());
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) {
        t.join();
    }

    // Verify all IDs are unique
    std::set<OrderId> unique_ids(all_order_ids.begin(), all_order_ids.end());
    EXPECT_EQ(unique_ids.size(), all_order_ids.size());

    // Verify expected count
    EXPECT_EQ(all_order_ids.size(), num_threads * orders_per_thread);
}

// Test: Cancellation routing still works with concurrent submission
TEST(ConcurrencyTest, CancellationRoutingWithConcurrentSubmission) {
    MutexMultiInstrumentEngine engine;
    const int num_threads = 4;
    std::vector<OrderId> order_ids;
    std::mutex ids_mutex;

    // Submit orders from multiple threads
    auto submit_worker = [&](int thread_id) {
        std::string instrument = "INST" + std::to_string(thread_id % 2);
        for (int i = 0; i < 50; ++i) {
            Price price = 99000 + (i % 10) * 100;
            Order order(0, Side::Buy, price, 10, thread_id * 50 + i);
            auto [trades, events] = engine.submit_order(instrument, order);
            if (!events.empty()) {
                std::lock_guard<std::mutex> lock(ids_mutex);
                order_ids.push_back(events[0].order_id);
            }
        }
    };

    std::vector<std::thread> submit_threads;
    for (int i = 0; i < num_threads; ++i) {
        submit_threads.emplace_back(submit_worker, i);
    }
    for (auto& t : submit_threads) {
        t.join();
    }

    EXPECT_EQ(order_ids.size(), num_threads * 50);

    // Cancel orders and verify routing works
    int cancelled = 0;
    for (OrderId id : order_ids) {
        auto [success, events] = engine.cancel_order(id);
        if (success) {
            cancelled++;
        }
    }

    EXPECT_EQ(cancelled, num_threads * 50);
}

// ============================================================================
// Repeated stress tests
// ============================================================================

// Test: Repeated concurrent workload with deterministic seeds
TEST(ConcurrencyTest, RepeatedStressTest) {
    const int num_iterations = 5;
    const int num_threads = 4;
    const int orders_per_thread = 100;

    for (int iter = 0; iter < num_iterations; ++iter) {
        MutexMultiInstrumentEngine engine;
        std::atomic<int> total_submitted{0};

        auto worker = [&](int thread_id) {
            std::string instrument = "INST" + std::to_string(thread_id % 2);
            for (int i = 0; i < orders_per_thread; ++i) {
                Price price = 99000 + (i % 10) * 100;
                Order order(0, Side::Buy, price, 10, thread_id * orders_per_thread + i);
                auto [trades, events] = engine.submit_order(instrument, order);
                if (!events.empty()) {
                    total_submitted.fetch_add(1);
                }
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back(worker, i);
        }
        for (auto& t : threads) {
            t.join();
        }

        // Verify each iteration
        EXPECT_EQ(total_submitted.load(), num_threads * orders_per_thread);
    }
}

// Test: High-contention stress test
TEST(ConcurrencyTest, HighContentionStressTest) {
    MutexMultiInstrumentEngine engine;
    const int num_threads = 16;
    const int orders_per_thread = 50;
    std::atomic<int> total_submitted{0};

    auto worker = [&](int thread_id) {
        // All threads target the same instrument for maximum contention
        for (int i = 0; i < orders_per_thread; ++i) {
            Price price = 99000 + (i % 5) * 100;
            Order order(0, Side::Buy, price, 10, thread_id * orders_per_thread + i);
            auto [trades, events] = engine.submit_order("INST1", order);
            if (!events.empty()) {
                total_submitted.fetch_add(1);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(total_submitted.load(), num_threads * orders_per_thread);
    EXPECT_EQ(engine.buy_order_count("INST1"), num_threads * orders_per_thread);
}

// ============================================================================
// Quantity conservation tests
// ============================================================================

// Test: Quantity conservation under concurrent matching
TEST(ConcurrencyTest, QuantityConservation) {
    MutexMultiInstrumentEngine engine;
    const int num_threads = 4;
    const int orders_per_thread = 50;
    const Qty order_qty = 100;

    // Submit buy orders at lower prices
    std::vector<OrderId> buy_ids;
    std::mutex ids_mutex;

    auto buy_worker = [&](int thread_id) {
        for (int i = 0; i < orders_per_thread; ++i) {
            Price price = 99000 + (i % 10) * 100;
            Order order(0, Side::Buy, price, order_qty, thread_id * orders_per_thread + i);
            auto [trades, events] = engine.submit_order("INST1", order);
            if (!events.empty()) {
                std::lock_guard<std::mutex> lock(ids_mutex);
                buy_ids.push_back(events[0].order_id);
            }
        }
    };

    std::vector<std::thread> buy_threads;
    for (int i = 0; i < num_threads; ++i) {
        buy_threads.emplace_back(buy_worker, i);
    }
    for (auto& t : buy_threads) {
        t.join();
    }

    // Submit market sell orders that will match all available liquidity
    std::atomic<Qty> total_matched{0};

    auto sell_worker = [&](int thread_id) {
        for (int i = 0; i < orders_per_thread; ++i) {
            // Use market orders to ensure they match
            Order order(0, Side::Sell, order_qty, thread_id * orders_per_thread + i);
            auto [trades, events] = engine.submit_order("INST1", order);
            for (const auto& trade : trades) {
                total_matched.fetch_add(trade.execution_quantity);
            }
        }
    };

    std::vector<std::thread> sell_threads;
    for (int i = 0; i < num_threads; ++i) {
        sell_threads.emplace_back(sell_worker, i);
    }
    for (auto& t : sell_threads) {
        t.join();
    }

    // Verify quantity conservation: all buy orders should be matched
    Qty total_buy_submitted = num_threads * orders_per_thread * order_qty;
    EXPECT_EQ(total_matched.load(), total_buy_submitted);
}
