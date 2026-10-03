#include <gtest/gtest.h>
#include <engine/spsc_multi_instrument_engine.hpp>
#include <engine/types.hpp>
#include <thread>
#include <vector>
#include <set>
#include <atomic>
#include <algorithm>

using namespace engine;

// ============================================================================
// Basic integration tests
// ============================================================================

// Test: Producer submits orders, matching thread processes them
TEST(SpscIntegrationTest, BasicSubmitOrder) {
    SpscMultiInstrumentEngine<1024> engine;

    // Submit a limit order
    Order order(0, Side::Buy, 100000, 10, 1);
    auto [success, order_id] = engine.submit_order("INST1", order);
    EXPECT_TRUE(success);

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Stop the engine to access query methods safely
    engine.stop();

    // Verify order was processed
    EXPECT_TRUE(engine.has_instrument("INST1"));
    EXPECT_EQ(engine.buy_order_count("INST1"), 1);
}

// Test: Multiple orders are processed correctly
TEST(SpscIntegrationTest, MultipleOrders) {
    SpscMultiInstrumentEngine<1024> engine;

    // Submit multiple orders
    for (int i = 0; i < 10; ++i) {
        Order order(0, Side::Buy, 100000 + i * 100, 10, i);
        auto [success, order_id] = engine.submit_order("INST1", order);
    EXPECT_TRUE(success);
    }

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    engine.stop();

    EXPECT_EQ(engine.buy_order_count("INST1"), 10);
}

// Test: Market order processing
TEST(SpscIntegrationTest, MarketOrder) {
    SpscMultiInstrumentEngine<1024> engine;

    // Submit a sell order first
    Order sell_order(0, Side::Sell, 100000, 10, 1);
    auto [success1, order_id1] = engine.submit_order("INST1", sell_order);
    EXPECT_TRUE(success1);
    (void)order_id1;

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Submit a market buy order
    Order market_order(0, Side::Buy, 10, 2);
    auto [success2, order_id2] = engine.submit_order("INST1", market_order);
    EXPECT_TRUE(success2);
    (void)order_id2;

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    engine.stop();

    // Market order should have matched and not rested
    EXPECT_EQ(engine.buy_order_count("INST1"), 0);
    EXPECT_EQ(engine.sell_order_count("INST1"), 0);
}

// Test: Cancellation works correctly
TEST(SpscIntegrationTest, CancelOrder) {
    SpscMultiInstrumentEngine<1024> engine;

    // Submit an order
    Order order(0, Side::Buy, 100000, 10, 1);
    auto [success, order_id] = engine.submit_order("INST1", order);
    EXPECT_TRUE(success);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Cancel the order using the returned ID
    EXPECT_TRUE(engine.cancel_order(order_id));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    engine.stop();

    EXPECT_EQ(engine.buy_order_count("INST1"), 0);
}

// ============================================================================
// Multi-instrument tests
// ============================================================================

// Test: Orders for multiple instruments remain isolated
TEST(SpscIntegrationTest, MultipleInstruments) {
    SpscMultiInstrumentEngine<1024> engine;

    // Submit orders to different instruments
    for (int i = 0; i < 5; ++i) {
        Order order(0, Side::Buy, 100000 + i * 100, 10, i);
        auto [success, order_id] = engine.submit_order("AAPL", order);
        EXPECT_TRUE(success);
        (void)order_id;
    }

    for (int i = 0; i < 7; ++i) {
        Order order(0, Side::Buy, 100000 + i * 100, 10, i + 5);
        auto [success, order_id] = engine.submit_order("MSFT", order);
        EXPECT_TRUE(success);
        (void)order_id;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    engine.stop();

    EXPECT_EQ(engine.buy_order_count("AAPL"), 5);
    EXPECT_EQ(engine.buy_order_count("MSFT"), 7);
    EXPECT_EQ(engine.instrument_count(), 2);
}

// ============================================================================
// Matching and trade generation tests
// ============================================================================

// Test: Crossing orders generate trades
TEST(SpscIntegrationTest, CrossingOrdersGenerateTrades) {
    SpscMultiInstrumentEngine<1024> engine;

    // Submit a sell order
    Order sell(0, Side::Sell, 100000, 10, 1);
    auto [success1, order_id1] = engine.submit_order("INST1", sell);
    EXPECT_TRUE(success1);
    (void)order_id1;

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Submit a buy order that crosses
    Order buy(0, Side::Buy, 100100, 10, 2);
    auto [success2, order_id2] = engine.submit_order("INST1", buy);
    EXPECT_TRUE(success2);
    (void)order_id2;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    engine.stop();

    // Both orders should be fully filled and removed
    EXPECT_EQ(engine.buy_order_count("INST1"), 0);
    EXPECT_EQ(engine.sell_order_count("INST1"), 0);
}

// Test: Partial fills work correctly
TEST(SpscIntegrationTest, PartialFills) {
    SpscMultiInstrumentEngine<1024> engine;

    // Submit a sell order with quantity 100
    Order sell(0, Side::Sell, 100000, 100, 1);
    auto [success1, order_id1] = engine.submit_order("INST1", sell);
    EXPECT_TRUE(success1);
    (void)order_id1;

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Submit a buy order with quantity 50 (should partially fill)
    Order buy(0, Side::Buy, 100100, 50, 2);
    auto [success2, order_id2] = engine.submit_order("INST1", buy);
    EXPECT_TRUE(success2);
    (void)order_id2;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    engine.stop();

    // Buy order should be gone (fully filled)
    EXPECT_EQ(engine.buy_order_count("INST1"), 0);
    // Sell order should remain with 50 quantity
    EXPECT_EQ(engine.sell_order_count("INST1"), 1);
}

// ============================================================================
// Queue full behavior tests
// ============================================================================

// Test: Queue returns false when full
TEST(SpscIntegrationTest, QueueFullBehavior) {
    SpscMultiInstrumentEngine<16> engine;  // Small queue

    // Fill the queue
    int success_count = 0;
    for (int i = 0; i < 100; ++i) {
        Order order(0, Side::Buy, 100000 + i, 10, i);
        auto [success, order_id] = engine.submit_order("INST1", order);
        if (success) {
            success_count++;
            (void)order_id;
        } else {
            // Queue is full
            break;
        }
    }

    // Should have filled at least the queue capacity
    EXPECT_GT(success_count, 0);
    EXPECT_LE(success_count, 16);

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    engine.stop();

    // All successful enqueues should have been processed
    EXPECT_EQ(engine.buy_order_count("INST1"), success_count);
}

// ============================================================================
// High-throughput stress test
// ============================================================================

// Test: High throughput with many orders
TEST(SpscIntegrationTest, HighThroughputStress) {
    SpscMultiInstrumentEngine<1024> engine;

    const int num_orders = 1000;

    // Submit many orders
    for (int i = 0; i < num_orders; ++i) {
        Order order(0, Side::Buy, 100000 + (i % 100) * 100, 10, i);
        auto [success, order_id] = engine.submit_order("INST1", order);
        if (!success) {
            // Queue full, wait a bit and retry
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            auto [retry_success, retry_id] = engine.submit_order("INST1", order);
            (void)retry_success;
            (void)retry_id;
        }
    }

    // Wait for all processing
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    engine.stop();

    EXPECT_EQ(engine.buy_order_count("INST1"), num_orders);
}

// ============================================================================
// Global ID uniqueness test
// ============================================================================

// Test: Order IDs remain unique through SPSC ingestion
TEST(SpscIntegrationTest, GlobalIdUniqueness) {
    SpscMultiInstrumentEngine<1024> engine;

    const int num_orders = 100;

    // Submit orders
    for (int i = 0; i < num_orders; ++i) {
        Order order(0, Side::Buy, 100000 + i, 10, i);
        auto [success, order_id] = engine.submit_order("INST1", order);
    EXPECT_TRUE(success);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    engine.stop();

    // Get all orders and verify IDs are unique
    const auto* eng = engine.get_engine();
    ASSERT_NE(eng, nullptr);
    auto orders = eng->get_all_buy_orders("INST1");

    std::set<OrderId> ids;
    for (const auto& order : orders) {
        ids.insert(order.id);
    }

    EXPECT_EQ(ids.size(), static_cast<size_t>(num_orders));
}

// ============================================================================
// Concurrent producer stress test
// ============================================================================

// Test: Producer keeps up with high submission rate
TEST(SpscIntegrationTest, ProducerStress) {
    SpscMultiInstrumentEngine<1024> engine;

    const int num_orders = 5000;
    std::atomic<int> enqueued_count{0};

    // Producer thread
    auto producer = [&]() {
        for (int i = 0; i < num_orders; ++i) {
            Order order(0, Side::Buy, 100000 + (i % 100) * 100, 10, i);
            while (true) {
                auto [success, order_id] = engine.submit_order("INST1", order);
                if (success) {
                    (void)order_id;
                    break;
                }
                std::this_thread::yield();
            }
            enqueued_count.fetch_add(1);
        }
    };

    std::thread t(producer);
    t.join();

    // Wait for matching thread to process
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    engine.stop();

    EXPECT_EQ(enqueued_count.load(), num_orders);
    EXPECT_EQ(engine.buy_order_count("INST1"), num_orders);
}

// ============================================================================
// Quantity conservation test
// ============================================================================

// Test: Quantity is conserved through matching
TEST(SpscIntegrationTest, QuantityConservation) {
    SpscMultiInstrumentEngine<1024> engine;

    const Qty order_qty = 100;
    const int num_buy_orders = 10;
    const int num_sell_orders = 10;

    // Submit buy orders
    for (int i = 0; i < num_buy_orders; ++i) {
        Order order(0, Side::Buy, 99000 + i * 100, order_qty, i);
        auto [success, order_id] = engine.submit_order("INST1", order);
        EXPECT_TRUE(success);
        (void)order_id;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Submit market sell orders that should match all
    for (int i = 0; i < num_sell_orders; ++i) {
        Order order(0, Side::Sell, order_qty, num_buy_orders + i);
        auto [success, order_id] = engine.submit_order("INST1", order);
        EXPECT_TRUE(success);
        (void)order_id;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    engine.stop();

    // All orders should be matched and removed
    EXPECT_EQ(engine.buy_order_count("INST1"), 0);
    EXPECT_EQ(engine.sell_order_count("INST1"), 0);
}

// ============================================================================
// Mixed workload test
// ============================================================================

// Test: Mix of add, cancel, and match operations
TEST(SpscIntegrationTest, MixedWorkload) {
    SpscMultiInstrumentEngine<1024> engine;

    // Submit some orders
    for (int i = 0; i < 10; ++i) {
        Order order(0, Side::Buy, 100000 + i * 100, 10, i);
        auto [success, order_id] = engine.submit_order("INST1", order);
        EXPECT_TRUE(success);
        (void)order_id;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Submit crossing orders to create matches
    for (int i = 0; i < 5; ++i) {
        Order order(0, Side::Sell, 100000 + i * 100, 10, 10 + i);
        auto [success, order_id] = engine.submit_order("INST1", order);
        EXPECT_TRUE(success);
        (void)order_id;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    engine.stop();

    // Verify some orders remain (non-crossing ones)
    EXPECT_GT(engine.buy_order_count("INST1"), 0);
}
