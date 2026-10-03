#include <gtest/gtest.h>
#include <engine/multi_instrument_engine.hpp>
#include <engine/types.hpp>
#include <set>

using namespace engine;

// Test creating multiple instruments
TEST(MultiInstrumentTest, CreateMultipleInstruments) {
    MultiInstrumentEngine engine;

    // Submit orders to two different instruments
    auto [trades1, events1] = engine.submit_order("AAPL", Order(0, Side::Buy, 100000, 100, 1));
    auto [trades2, events2] = engine.submit_order("MSFT", Order(0, Side::Sell, 200000, 50, 2));

    // Verify both instruments exist
    EXPECT_TRUE(engine.has_instrument("AAPL"));
    EXPECT_TRUE(engine.has_instrument("MSFT"));
    EXPECT_EQ(engine.instrument_count(), 2);

    // Verify no trades were generated (orders don't cross)
    EXPECT_EQ(trades1.size(), 0);
    EXPECT_EQ(trades2.size(), 0);
}

// Test independent books - orders at identical prices on different instruments
TEST(MultiInstrumentTest, IndependentBooks) {
    MultiInstrumentEngine engine;

    // Submit resting orders at identical prices on two instruments
    engine.submit_order("AAPL", Order(0, Side::Buy, 100000, 100, 1));
    engine.submit_order("MSFT", Order(0, Side::Buy, 100000, 100, 2));

    // Verify both books have orders
    EXPECT_EQ(engine.buy_order_count("AAPL"), 1);
    EXPECT_EQ(engine.buy_order_count("MSFT"), 1);

    // Verify best bid independently
    EXPECT_EQ(engine.best_bid("AAPL"), 100000);
    EXPECT_EQ(engine.best_bid("MSFT"), 100000);

    // Verify they remain separate by cancelling one
    auto aapl_buy_orders = engine.get_all_buy_orders("AAPL");
    ASSERT_EQ(aapl_buy_orders.size(), 1);
    engine.cancel_order(aapl_buy_orders[0].id);

    // AAPL should be empty, MSFT should still have its order
    EXPECT_EQ(engine.buy_order_count("AAPL"), 0);
    EXPECT_EQ(engine.buy_order_count("MSFT"), 1);
}

// Test no cross-instrument matching
TEST(MultiInstrumentTest, NoCrossInstrumentMatching) {
    MultiInstrumentEngine engine;

    // AAPL: Buy 100 @ 100000
    auto [trades1, events1] = engine.submit_order("AAPL", Order(0, Side::Buy, 100000, 100, 1));

    // MSFT: Sell 100 @ 100000
    auto [trades2, events2] = engine.submit_order("MSFT", Order(0, Side::Sell, 100000, 100, 2));

    // Verify no trades were generated (different instruments)
    EXPECT_EQ(trades1.size(), 0);
    EXPECT_EQ(trades2.size(), 0);

    // Submit AAPL sell that should match
    auto [trades3, events3] = engine.submit_order("AAPL", Order(0, Side::Sell, 100000, 100, 3));

    // Now we should have a trade on AAPL
    EXPECT_EQ(trades3.size(), 1);
    EXPECT_EQ(trades3[0].instrument_id, "AAPL");
    EXPECT_EQ(trades3[0].execution_quantity, 100);

    // MSFT should still have its resting sell order
    EXPECT_EQ(engine.sell_order_count("MSFT"), 1);
}

// Test simultaneous activity across instruments
TEST(MultiInstrumentTest, SimultaneousActivity) {
    MultiInstrumentEngine engine;

    // Interleave operations across instruments (use non-crossing prices)
    engine.submit_order("AAPL", Order(0, Side::Buy, 100000, 100, 1));
    engine.submit_order("MSFT", Order(0, Side::Buy, 200000, 100, 2));
    engine.submit_order("GOOG", Order(0, Side::Buy, 300000, 100, 3));

    engine.submit_order("AAPL", Order(0, Side::Sell, 101000, 50, 4));
    engine.submit_order("MSFT", Order(0, Side::Sell, 201000, 50, 5));
    engine.submit_order("GOOG", Order(0, Side::Sell, 301000, 50, 6));

    // Verify each instrument has the correct state
    EXPECT_EQ(engine.buy_order_count("AAPL"), 1);
    EXPECT_EQ(engine.sell_order_count("AAPL"), 1);
    EXPECT_EQ(engine.buy_order_count("MSFT"), 1);
    EXPECT_EQ(engine.sell_order_count("MSFT"), 1);
    EXPECT_EQ(engine.buy_order_count("GOOG"), 1);
    EXPECT_EQ(engine.sell_order_count("GOOG"), 1);

    // Cancel AAPL buy
    auto aapl_buy_orders = engine.get_all_buy_orders("AAPL");
    ASSERT_EQ(aapl_buy_orders.size(), 1);
    engine.cancel_order(aapl_buy_orders[0].id);

    // Verify only AAPL was affected
    EXPECT_EQ(engine.buy_order_count("AAPL"), 0);
    EXPECT_EQ(engine.buy_order_count("MSFT"), 1);
    EXPECT_EQ(engine.buy_order_count("GOOG"), 1);
}

// Test global order ID uniqueness
TEST(MultiInstrumentTest, GlobalOrderIdUniqueness) {
    MultiInstrumentEngine engine;

    // Generate many orders across multiple instruments
    std::vector<OrderId> order_ids;
    for (int i = 0; i < 10; ++i) {
        auto [trades, events] = engine.submit_order("AAPL", Order(0, Side::Buy, 100000, 10, i + 1));
        order_ids.push_back(events[0].order_id);
    }

    for (int i = 0; i < 10; ++i) {
        auto [trades, events] = engine.submit_order("MSFT", Order(0, Side::Sell, 200000, 10, i + 11));
        order_ids.push_back(events[0].order_id);
    }

    for (int i = 0; i < 10; ++i) {
        auto [trades, events] = engine.submit_order("GOOG", Order(0, Side::Buy, 300000, 10, i + 21));
        order_ids.push_back(events[0].order_id);
    }

    // Verify all order IDs are unique
    std::set<OrderId> unique_ids(order_ids.begin(), order_ids.end());
    EXPECT_EQ(unique_ids.size(), order_ids.size());
}

// Test cancellation by ID
TEST(MultiInstrumentTest, CancellationById) {
    MultiInstrumentEngine engine;

    // Add orders to two instruments
    auto [trades1, events1] = engine.submit_order("AAPL", Order(0, Side::Buy, 100000, 100, 1));
    OrderId aapl_order_id = events1[0].order_id;

    auto [trades2, events2] = engine.submit_order("MSFT", Order(0, Side::Buy, 200000, 100, 2));
    OrderId msft_order_id = events2[0].order_id;

    // Cancel AAPL order
    auto [cancelled, aapl_cancel_events] = engine.cancel_order(aapl_order_id);
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(engine.buy_order_count("AAPL"), 0);

    // Verify MSFT is unchanged
    EXPECT_EQ(engine.buy_order_count("MSFT"), 1);

    // Verify we can still cancel MSFT order
    auto [cancelled2, msft_cancel_events] = engine.cancel_order(msft_order_id);
    EXPECT_TRUE(cancelled2);
    EXPECT_EQ(engine.buy_order_count("MSFT"), 0);
}

// Test cancellation after activity on multiple instruments
TEST(MultiInstrumentTest, CancellationAfterActivity) {
    MultiInstrumentEngine engine;

    // Build up state on multiple instruments
    engine.submit_order("AAPL", Order(0, Side::Buy, 100000, 100, 1));
    engine.submit_order("MSFT", Order(0, Side::Buy, 200000, 100, 2));
    engine.submit_order("GOOG", Order(0, Side::Buy, 300000, 100, 3));

    // Submit matching orders that create fills
    engine.submit_order("AAPL", Order(0, Side::Sell, 100000, 50, 4));
    engine.submit_order("MSFT", Order(0, Side::Sell, 200000, 50, 5));

    // Get remaining order IDs
    auto aapl_orders = engine.get_all_buy_orders("AAPL");
    auto msft_orders = engine.get_all_buy_orders("MSFT");
    auto goog_orders = engine.get_all_buy_orders("GOOG");

    ASSERT_GT(aapl_orders.size(), 0);
    ASSERT_GT(msft_orders.size(), 0);
    ASSERT_GT(goog_orders.size(), 0);

    // Cancel orders after activity
    engine.cancel_order(aapl_orders[0].id);
    engine.cancel_order(msft_orders[0].id);
    engine.cancel_order(goog_orders[0].id);

    // Verify all cancellations worked
    EXPECT_EQ(engine.buy_order_count("AAPL"), 0);
    EXPECT_EQ(engine.buy_order_count("MSFT"), 0);
    EXPECT_EQ(engine.buy_order_count("GOOG"), 0);
}

// Test trades reference correct instrument
TEST(MultiInstrumentTest, TradesReferenceCorrectInstrument) {
    MultiInstrumentEngine engine;

    // Create crossed books on multiple instruments
    engine.submit_order("AAPL", Order(0, Side::Buy, 100000, 100, 1));
    engine.submit_order("MSFT", Order(0, Side::Buy, 200000, 100, 2));

    auto [aapl_trades, aapl_events] = engine.submit_order("AAPL", Order(0, Side::Sell, 100000, 50, 3));
    auto [msft_trades, msft_events] = engine.submit_order("MSFT", Order(0, Side::Sell, 200000, 50, 4));

    // Verify trades reference correct instruments
    ASSERT_EQ(aapl_trades.size(), 1);
    EXPECT_EQ(aapl_trades[0].instrument_id, "AAPL");

    ASSERT_EQ(msft_trades.size(), 1);
    EXPECT_EQ(msft_trades[0].instrument_id, "MSFT");

    // Verify events also reference correct instruments
    for (const auto& event : aapl_events) {
        EXPECT_EQ(event.instrument_id, "AAPL");
    }

    for (const auto& event : msft_events) {
        EXPECT_EQ(event.instrument_id, "MSFT");
    }
}

// Test instrument isolation under matching
TEST(MultiInstrumentTest, InstrumentIsolationUnderMatching) {
    MultiInstrumentEngine engine;

    // Create resting orders on multiple instruments (non-crossing)
    engine.submit_order("AAPL", Order(0, Side::Buy, 100000, 100, 1));
    engine.submit_order("MSFT", Order(0, Side::Buy, 200000, 100, 2));
    engine.submit_order("GOOG", Order(0, Side::Buy, 300000, 100, 3));

    // Verify resting orders exist
    EXPECT_EQ(engine.buy_order_count("AAPL"), 1);
    EXPECT_EQ(engine.buy_order_count("MSFT"), 1);
    EXPECT_EQ(engine.buy_order_count("GOOG"), 1);

    // Now submit crossing orders on each instrument
    auto [aapl_trades, _] = engine.submit_order("AAPL", Order(0, Side::Sell, 100000, 100, 4));
    auto [msft_trades, __] = engine.submit_order("MSFT", Order(0, Side::Sell, 200000, 100, 5));
    auto [goog_trades, ___] = engine.submit_order("GOOG", Order(0, Side::Sell, 300000, 100, 6));

    // Each instrument should produce exactly one trade
    EXPECT_EQ(aapl_trades.size(), 1);
    EXPECT_EQ(msft_trades.size(), 1);
    EXPECT_EQ(goog_trades.size(), 1);

    // Each trade should reference only its own instrument
    EXPECT_EQ(aapl_trades[0].instrument_id, "AAPL");
    EXPECT_EQ(msft_trades[0].instrument_id, "MSFT");
    EXPECT_EQ(goog_trades[0].instrument_id, "GOOG");

    // All books should be empty after matching
    EXPECT_EQ(engine.buy_order_count("AAPL"), 0);
    EXPECT_EQ(engine.sell_order_count("AAPL"), 0);
    EXPECT_EQ(engine.buy_order_count("MSFT"), 0);
    EXPECT_EQ(engine.sell_order_count("MSFT"), 0);
    EXPECT_EQ(engine.buy_order_count("GOOG"), 0);
    EXPECT_EQ(engine.sell_order_count("GOOG"), 0);
}

// Test empty/nonexistent instrument behavior
TEST(MultiInstrumentTest, EmptyNonexistentInstrument) {
    MultiInstrumentEngine engine;

    // Querying a nonexistent instrument should return empty/zero values
    EXPECT_FALSE(engine.has_instrument("NONEXISTENT"));
    EXPECT_TRUE(engine.empty("NONEXISTENT"));
    EXPECT_TRUE(engine.buy_side_empty("NONEXISTENT"));
    EXPECT_TRUE(engine.sell_side_empty("NONEXISTENT"));
    EXPECT_EQ(engine.buy_order_count("NONEXISTENT"), 0);
    EXPECT_EQ(engine.sell_order_count("NONEXISTENT"), 0);
    EXPECT_EQ(engine.buy_price_level_count("NONEXISTENT"), 0);
    EXPECT_EQ(engine.sell_price_level_count("NONEXISTENT"), 0);
    EXPECT_FALSE(engine.best_bid("NONEXISTENT").has_value());
    EXPECT_FALSE(engine.best_ask("NONEXISTENT").has_value());

    // Cancelling an unknown order ID should fail
    auto [cancelled, events] = engine.cancel_order(99999);
    EXPECT_FALSE(cancelled);
    EXPECT_EQ(events.size(), 0);

    // Submitting first order to an instrument should create it
    auto [trades, order_events] = engine.submit_order("NEWINST", Order(0, Side::Buy, 100000, 100, 1));
    EXPECT_TRUE(engine.has_instrument("NEWINST"));
    EXPECT_EQ(engine.instrument_count(), 1);
}

// Test get_instrument_for_order
TEST(MultiInstrumentTest, GetInstrumentForOrder) {
    MultiInstrumentEngine engine;

    // Submit orders to different instruments
    auto [trades1, events1] = engine.submit_order("AAPL", Order(0, Side::Buy, 100000, 100, 1));
    OrderId aapl_id = events1[0].order_id;

    auto [trades2, events2] = engine.submit_order("MSFT", Order(0, Side::Buy, 200000, 100, 2));
    OrderId msft_id = events2[0].order_id;

    // Verify we can look up the instrument for each order
    EXPECT_EQ(engine.get_instrument_for_order(aapl_id), "AAPL");
    EXPECT_EQ(engine.get_instrument_for_order(msft_id), "MSFT");

    // Cancel one order
    engine.cancel_order(aapl_id);

    // Cancelled order should no longer be found
    EXPECT_EQ(engine.get_instrument_for_order(aapl_id), "");
    EXPECT_EQ(engine.get_instrument_for_order(msft_id), "MSFT");

    // Unknown order ID should return empty string
    EXPECT_EQ(engine.get_instrument_for_order(99999), "");
}

// Test quantity conservation across instruments
TEST(MultiInstrumentTest, QuantityConservation) {
    MultiInstrumentEngine engine;

    // Submit orders to multiple instruments
    engine.submit_order("AAPL", Order(0, Side::Buy, 100000, 100, 1));
    engine.submit_order("MSFT", Order(0, Side::Buy, 200000, 200, 2));

    // Partially fill AAPL
    auto [trades, events] = engine.submit_order("AAPL", Order(0, Side::Sell, 100000, 50, 3));
    EXPECT_EQ(trades[0].execution_quantity, 50);

    // Verify AAPL has remaining quantity
    auto aapl_orders = engine.get_all_buy_orders("AAPL");
    ASSERT_EQ(aapl_orders.size(), 1);
    EXPECT_EQ(aapl_orders[0].remaining(), 50);
    EXPECT_EQ(aapl_orders[0].filled, 50);

    // Verify MSFT is unchanged
    auto msft_orders = engine.get_all_buy_orders("MSFT");
    ASSERT_EQ(msft_orders.size(), 1);
    EXPECT_EQ(msft_orders[0].remaining(), 200);
    EXPECT_EQ(msft_orders[0].filled, 0);
}

// Test market orders across instruments
TEST(MultiInstrumentTest, MarketOrdersAcrossInstruments) {
    MultiInstrumentEngine engine;

    // Add resting orders on multiple instruments
    engine.submit_order("AAPL", Order(0, Side::Sell, 100000, 100, 1));
    engine.submit_order("MSFT", Order(0, Side::Sell, 200000, 100, 2));

    // Submit market orders
    auto [aapl_trades, _] = engine.submit_order("AAPL", Order(0, Side::Buy, 50, 3));
    auto [msft_trades, __] = engine.submit_order("MSFT", Order(0, Side::Buy, 50, 4));

    // Both should execute
    EXPECT_EQ(aapl_trades.size(), 1);
    EXPECT_EQ(msft_trades.size(), 1);

    // Verify correct instruments
    EXPECT_EQ(aapl_trades[0].instrument_id, "AAPL");
    EXPECT_EQ(msft_trades[0].instrument_id, "MSFT");
}

// Test deterministic randomized operations across instruments
TEST(MultiInstrumentTest, DeterministicRandomizedMultiInstrument) {
    MultiInstrumentEngine engine;

    const std::vector<InstrumentId> instruments = {"AAPL", "MSFT", "GOOG"};
    std::vector<OrderId> all_order_ids;

    // Submit orders in a deterministic pattern
    for (int i = 0; i < 30; ++i) {
        InstrumentId inst = instruments[i % 3];
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        Price price = 100000 + (i / 3) * 1000;
        Qty qty = 10 + (i % 5) * 10;

        auto [trades, events] = engine.submit_order(inst, Order(0, side, price, qty, i + 1));
        all_order_ids.push_back(events[0].order_id);
    }

    // Verify all order IDs are unique
    std::set<OrderId> unique_ids(all_order_ids.begin(), all_order_ids.end());
    EXPECT_EQ(unique_ids.size(), all_order_ids.size());

    // Verify each instrument has orders
    for (const auto& inst : instruments) {
        EXPECT_TRUE(engine.has_instrument(inst));
        EXPECT_GT(engine.buy_order_count(inst) + engine.sell_order_count(inst), 0);
    }
}
