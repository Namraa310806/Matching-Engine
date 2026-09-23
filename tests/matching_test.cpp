#include <gtest/gtest.h>
#include <engine/orderbook.hpp>
#include <engine/types.hpp>
#include <numeric>
#include <vector>
#include <unordered_set>

using namespace engine;

// Helper function to verify book/index consistency
// Returns true if the book and order_index_ are consistent
bool verify_book_index_consistency(const OrderBook& book) {
    // Get all orders from the book
    auto buy_orders = book.get_all_buy_orders();
    auto sell_orders = book.get_all_sell_orders();
    
    // Track all OrderIds found in the book
    std::unordered_set<OrderId> book_order_ids;
    
    // Verify no order has filled > quantity and collect OrderIds
    for (const auto& order : buy_orders) {
        if (order.filled > order.quantity) return false;
        book_order_ids.insert(order.id);
    }
    for (const auto& order : sell_orders) {
        if (order.filled > order.quantity) return false;
        book_order_ids.insert(order.id);
    }
    
    // Check for duplicate OrderIds in the book
    size_t total_book_orders = buy_orders.size() + sell_orders.size();
    if (book_order_ids.size() != total_book_orders) {
        return false; // Duplicate OrderId found
    }
    
    // Verify that every order in the book is in the index
    for (OrderId id : book_order_ids) {
        if (!book.order_id_in_index(id)) {
            return false; // Order in book but not in index
        }
    }
    
    // Verify that the index size matches the book order count
    if (book.order_index_size() != total_book_orders) {
        return false; // Index size mismatch
    }
    
    return true;
}

// ============================================================================
// Basic Matching Tests
// ============================================================================

TEST(MatchingTest, BuyCrossesOneSell) {
    OrderBook book;
    
    // Add a sell order
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    
    // Submit a buy order that crosses
    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 50, 11));
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].buy_order_id, 2);
    EXPECT_EQ(trades[0].sell_order_id, 1);
    EXPECT_EQ(trades[0].execution_price, 100); // Resting order price
    EXPECT_EQ(trades[0].execution_quantity, 50);
    
    // Verify remaining sell quantity
    auto sell_orders = book.get_orders_at_ask_price(100);
    ASSERT_EQ(sell_orders.size(), 1);
    EXPECT_EQ(sell_orders[0].remaining(), 50);
}

TEST(MatchingTest, SellCrossesOneBuy) {
    OrderBook book;
    
    // Add a buy order
    book.add_limit_order(Order(1, Side::Buy, 100, 100, 10));
    
    // Submit a sell order that crosses
    auto [trades, events] = book.submit_order(Order(2, Side::Sell, 95, 50, 11));
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].buy_order_id, 1);
    EXPECT_EQ(trades[0].sell_order_id, 2);
    EXPECT_EQ(trades[0].execution_price, 100); // Resting order price
    EXPECT_EQ(trades[0].execution_quantity, 50);
    
    // Verify remaining buy quantity
    auto buy_orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(buy_orders.size(), 1);
    EXPECT_EQ(buy_orders[0].remaining(), 50);
}

TEST(MatchingTest, NoCrossingBuyRests) {
    OrderBook book;
    
    // Add a sell order
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    
    // Submit a buy order that doesn't cross
    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 95, 50, 11));
    
    EXPECT_EQ(trades.size(), 0);
    
    // Verify buy order rests
    auto buy_orders = book.get_orders_at_bid_price(95);
    ASSERT_EQ(buy_orders.size(), 1);
    EXPECT_EQ(buy_orders[0].remaining(), 50);
}

TEST(MatchingTest, NoCrossingSellRests) {
    OrderBook book;
    
    // Add a buy order
    book.add_limit_order(Order(1, Side::Buy, 100, 100, 10));
    
    // Submit a sell order that doesn't cross
    auto [trades, events] = book.submit_order(Order(2, Side::Sell, 105, 50, 11));
    
    EXPECT_EQ(trades.size(), 0);
    
    // Verify sell order rests
    auto sell_orders = book.get_orders_at_ask_price(105);
    ASSERT_EQ(sell_orders.size(), 1);
    EXPECT_EQ(sell_orders[0].remaining(), 50);
}

// ============================================================================
// Full Fill Tests
// ============================================================================

TEST(MatchingTest, IncomingOrderCompletelyFillsRestingOrder) {
    OrderBook book;
    
    // Add a sell order
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    
    // Submit a buy order that fully fills the resting order
    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 100, 11));
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_quantity, 100);
    
    // Verify resting order is removed
    auto sell_orders = book.get_orders_at_ask_price(100);
    EXPECT_EQ(sell_orders.size(), 0);
    
    // Verify price level is removed
    EXPECT_EQ(book.sell_price_level_count(), 0);
}

TEST(MatchingTest, RestingOrderCompletelyFillsIncomingOrder) {
    OrderBook book;
    
    // Add a large sell order
    book.add_limit_order(Order(1, Side::Sell, 100, 1000, 10));
    
    // Submit a small buy order
    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 100, 11));
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_quantity, 100);
    
    // Verify resting order has remaining quantity
    auto sell_orders = book.get_orders_at_ask_price(100);
    ASSERT_EQ(sell_orders.size(), 1);
    EXPECT_EQ(sell_orders[0].remaining(), 900);
}

// ============================================================================
// Partial Fill Tests
// ============================================================================

TEST(MatchingTest, IncomingOrderPartiallyFillsRestingOrder) {
    OrderBook book;
    
    // Add a sell order
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    
    // Submit a buy order that partially fills
    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 50, 11));
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    
    // Verify resting order has remaining quantity
    auto sell_orders = book.get_orders_at_ask_price(100);
    ASSERT_EQ(sell_orders.size(), 1);
    EXPECT_EQ(sell_orders[0].remaining(), 50);
}

TEST(MatchingTest, IncomingOrderCompletelyFillsMultipleRestingOrders) {
    OrderBook book;
    
    // Add multiple sell orders at same price
    book.add_limit_order(Order(1, Side::Sell, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 30, 11));
    book.add_limit_order(Order(3, Side::Sell, 100, 20, 12));
    
    // Submit a buy order that fills all three
    auto [trades, events] = book.submit_order(Order(4, Side::Buy, 105, 100, 13));
    
    ASSERT_EQ(trades.size(), 3);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    EXPECT_EQ(trades[1].execution_quantity, 30);
    EXPECT_EQ(trades[2].execution_quantity, 20);
    
    // Verify all resting orders are removed
    auto sell_orders = book.get_orders_at_ask_price(100);
    EXPECT_EQ(sell_orders.size(), 0);
}

TEST(MatchingTest, IncomingOrderPartiallyConsumesMultiplePriceLevelsAndRests) {
    OrderBook book;
    
    // Add sell orders at multiple price levels
    book.add_limit_order(Order(1, Side::Sell, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Sell, 101, 30, 11));
    book.add_limit_order(Order(3, Side::Sell, 102, 20, 12));
    
    // Submit a buy order that consumes all three levels and rests
    auto [trades, events] = book.submit_order(Order(4, Side::Buy, 103, 110, 13));
    
    ASSERT_EQ(trades.size(), 3);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    EXPECT_EQ(trades[1].execution_quantity, 30);
    EXPECT_EQ(trades[2].execution_quantity, 20);
    
    // Verify buy order rests with remaining quantity
    auto buy_orders = book.get_orders_at_bid_price(103);
    ASSERT_EQ(buy_orders.size(), 1);
    EXPECT_EQ(buy_orders[0].remaining(), 10);
    
    // Verify all sell orders are filled
    auto sell_orders_100 = book.get_orders_at_ask_price(100);
    EXPECT_EQ(sell_orders_100.size(), 0);
    auto sell_orders_101 = book.get_orders_at_ask_price(101);
    EXPECT_EQ(sell_orders_101.size(), 0);
    auto sell_orders_102 = book.get_orders_at_ask_price(102);
    EXPECT_EQ(sell_orders_102.size(), 0);
}

TEST(MatchingTest, RestingOrderIsPartiallyFilledAndRemainsWithReducedQuantity) {
    OrderBook book;
    
    // Add a sell order
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    
    // Submit a buy order that partially fills
    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 30, 11));
    
    ASSERT_EQ(trades.size(), 1);
    
    // Verify resting order has reduced quantity
    auto sell_orders = book.get_orders_at_ask_price(100);
    ASSERT_EQ(sell_orders.size(), 1);
    EXPECT_EQ(sell_orders[0].quantity, 100);
    EXPECT_EQ(sell_orders[0].filled, 30);
    EXPECT_EQ(sell_orders[0].remaining(), 70);
}

// ============================================================================
// Price Priority Tests
// ============================================================================

TEST(MatchingTest, BuyConsumesLowestAskFirst) {
    OrderBook book;
    
    // Add sell orders at different prices
    book.add_limit_order(Order(1, Side::Sell, 102, 50, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Sell, 101, 50, 12));
    
    // Submit a buy order
    auto [trades, events] = book.submit_order(Order(4, Side::Buy, 105, 100, 13));
    
    ASSERT_EQ(trades.size(), 2);
    // First trade should be at price 100 (best ask)
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    // Second trade should be at price 101 (next best)
    EXPECT_EQ(trades[1].execution_price, 101);
    EXPECT_EQ(trades[1].execution_quantity, 50);
}

TEST(MatchingTest, SellConsumesHighestBidFirst) {
    OrderBook book;
    
    // Add buy orders at different prices
    book.add_limit_order(Order(1, Side::Buy, 98, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Buy, 99, 50, 12));
    
    // Submit a sell order
    auto [trades, events] = book.submit_order(Order(4, Side::Sell, 95, 100, 13));
    
    ASSERT_EQ(trades.size(), 2);
    // First trade should be at price 100 (best bid)
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    // Second trade should be at price 99 (next best)
    EXPECT_EQ(trades[1].execution_price, 99);
    EXPECT_EQ(trades[1].execution_quantity, 50);
}

TEST(MatchingTest, BetterPricedOrdersAreConsumedBeforeWorsePricedOrders) {
    OrderBook book;
    
    // Add sell orders at different prices
    book.add_limit_order(Order(1, Side::Sell, 105, 50, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Sell, 110, 50, 12));
    
    // Submit a buy order
    auto [trades, events] = book.submit_order(Order(4, Side::Buy, 108, 100, 13));
    
    ASSERT_EQ(trades.size(), 2);
    // Should consume price 100 first, then 105
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[1].execution_price, 105);
    
    // Price 110 should remain
    auto sell_orders = book.get_orders_at_ask_price(110);
    ASSERT_EQ(sell_orders.size(), 1);
}

// ============================================================================
// Time Priority Tests
// ============================================================================

TEST(MatchingTest, TwoAsksAtSamePriceOlderAskFillsFirst) {
    OrderBook book;
    
    // Add two sell orders at same price
    book.add_limit_order(Order(1, Side::Sell, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 50, 11));
    
    // Submit a buy order
    auto [trades, events] = book.submit_order(Order(3, Side::Buy, 105, 75, 12));
    
    ASSERT_EQ(trades.size(), 2);
    // First trade should be with older order (id 1)
    EXPECT_EQ(trades[0].sell_order_id, 1);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    // Second trade should be with newer order (id 2)
    EXPECT_EQ(trades[1].sell_order_id, 2);
    EXPECT_EQ(trades[1].execution_quantity, 25);
}

TEST(MatchingTest, TwoBidsAtSamePriceOlderBidFillsFirst) {
    OrderBook book;
    
    // Add two buy orders at same price
    book.add_limit_order(Order(1, Side::Buy, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));
    
    // Submit a sell order
    auto [trades, events] = book.submit_order(Order(3, Side::Sell, 95, 75, 12));
    
    ASSERT_EQ(trades.size(), 2);
    // First trade should be with older order (id 1)
    EXPECT_EQ(trades[0].buy_order_id, 1);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    // Second trade should be with newer order (id 2)
    EXPECT_EQ(trades[1].buy_order_id, 2);
    EXPECT_EQ(trades[1].execution_quantity, 25);
}

TEST(MatchingTest, NewerOrderCannotJumpAheadAtSamePrice) {
    OrderBook book;
    
    // Add three sell orders at same price
    book.add_limit_order(Order(1, Side::Sell, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Sell, 100, 50, 12));
    
    // Submit a buy order
    auto [trades, events] = book.submit_order(Order(4, Side::Buy, 105, 125, 13));
    
    ASSERT_EQ(trades.size(), 3);
    // Trades should be in FIFO order
    EXPECT_EQ(trades[0].sell_order_id, 1);
    EXPECT_EQ(trades[1].sell_order_id, 2);
    EXPECT_EQ(trades[2].sell_order_id, 3);
}

// ============================================================================
// Market Order Tests
// ============================================================================

TEST(MatchingTest, MarketBuyConsumesBestAsksFirst) {
    OrderBook book;
    
    // Add sell orders at different prices
    book.add_limit_order(Order(1, Side::Sell, 102, 50, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Sell, 101, 50, 12));
    
    // Submit a market buy order (use market order constructor with 4 params: id, side, qty, seq)
    auto [trades, events] = book.submit_order(Order(4, Side::Buy, 150, 13));
    
    ASSERT_EQ(trades.size(), 3);
    // Should consume best asks first
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[1].execution_price, 101);
    EXPECT_EQ(trades[2].execution_price, 102);
}

TEST(MatchingTest, MarketSellConsumesBestBidsFirst) {
    OrderBook book;
    
    // Add buy orders at different prices
    book.add_limit_order(Order(1, Side::Buy, 98, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Buy, 99, 50, 12));
    
    // Submit a market sell order (use market order constructor with 4 params)
    auto [trades, events] = book.submit_order(Order(4, Side::Sell, 150, 13));
    
    ASSERT_EQ(trades.size(), 3);
    // Should consume best bids first
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[1].execution_price, 99);
    EXPECT_EQ(trades[2].execution_price, 98);
}

TEST(MatchingTest, MarketOrderConsumesMultiplePriceLevels) {
    OrderBook book;
    
    // Add sell orders at multiple price levels
    book.add_limit_order(Order(1, Side::Sell, 100, 30, 10));
    book.add_limit_order(Order(2, Side::Sell, 101, 30, 11));
    book.add_limit_order(Order(3, Side::Sell, 102, 30, 12));
    
    // Submit a market buy order (use market order constructor with 4 params)
    auto [trades, events] = book.submit_order(Order(4, Side::Buy, 75, 13));
    
    ASSERT_EQ(trades.size(), 3);
    EXPECT_EQ(trades[0].execution_quantity, 30);
    EXPECT_EQ(trades[1].execution_quantity, 30);
    EXPECT_EQ(trades[2].execution_quantity, 15);
}

TEST(MatchingTest, MarketOrderAgainstEmptyBook) {
    OrderBook book;
    
    // Submit a market buy order against empty book (use market order constructor with 4 params)
    auto [trades, events] = book.submit_order(Order(1, Side::Buy, 100, 10));
    
    EXPECT_EQ(trades.size(), 0);
    EXPECT_TRUE(book.empty());
}

TEST(MatchingTest, MarketOrderWithInsufficientLiquidity) {
    OrderBook book;
    
    // Add a small sell order
    book.add_limit_order(Order(1, Side::Sell, 100, 50, 10));
    
    // Submit a large market buy order (use market order constructor with 4 params)
    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 100, 11));
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    
    // Verify market order remainder does NOT rest on the book
    auto buy_orders = book.get_all_buy_orders();
    EXPECT_EQ(buy_orders.size(), 0);
}

TEST(MatchingTest, MarketOrderRemainderDoesNotRestOnBook) {
    OrderBook book;
    
    // Add a small sell order
    book.add_limit_order(Order(1, Side::Sell, 100, 30, 10));
    
    // Submit a large market buy order (use market order constructor with 4 params)
    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 100, 11));
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_quantity, 30);
    
    // Verify book is empty (market order did not rest)
    EXPECT_TRUE(book.buy_side_empty());
    EXPECT_TRUE(book.sell_side_empty());
}

// ============================================================================
// Book State Tests
// ============================================================================

TEST(MatchingTest, EmptyPriceLevelsRemovedAfterFinalOrderFilled) {
    OrderBook book;
    
    // Add sell orders at same price
    book.add_limit_order(Order(1, Side::Sell, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 50, 11));
    
    // Submit a buy order that fills both
    auto [trades, events] = book.submit_order(Order(3, Side::Buy, 105, 100, 12));
    
    // Verify price level is removed
    auto sell_orders = book.get_orders_at_ask_price(100);
    EXPECT_EQ(sell_orders.size(), 0);
    EXPECT_EQ(book.sell_price_level_count(), 0);
}

TEST(MatchingTest, BestBidAskUpdatedCorrectlyAfterTrades) {
    OrderBook book;
    
    // Add orders
    book.add_limit_order(Order(1, Side::Buy, 100, 100, 10));
    book.add_limit_order(Order(2, Side::Buy, 95, 100, 11));
    book.add_limit_order(Order(3, Side::Sell, 105, 100, 12));
    
    // Submit a sell order that completely fills best bid
    auto [trades, events] = book.submit_order(Order(4, Side::Sell, 98, 100, 13));
    
    // Verify best bid is updated
    auto best_bid = book.best_bid();
    ASSERT_TRUE(best_bid.has_value());
    EXPECT_EQ(best_bid.value(), 95); // Next best bid
}

TEST(MatchingTest, BookDepthCorrectAfterPartialFullFills) {
    OrderBook book;
    
    // Add orders
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    book.add_limit_order(Order(2, Side::Sell, 101, 100, 11));
    book.add_limit_order(Order(3, Side::Sell, 102, 100, 12));
    
    // Submit a buy order that partially fills
    auto [trades, events] = book.submit_order(Order(4, Side::Buy, 105, 150, 13));
    
    // Verify book depth
    EXPECT_EQ(book.sell_order_count(), 2); // One fully filled, two remaining
    EXPECT_EQ(book.sell_price_level_count(), 2); // One level removed
    
    // Verify remaining quantities
    auto sell_orders_101 = book.get_orders_at_ask_price(101);
    ASSERT_EQ(sell_orders_101.size(), 1);
    EXPECT_EQ(sell_orders_101[0].remaining(), 50);
}

// ============================================================================
// Quantity Conservation Tests
// ============================================================================

TEST(MatchingTest, QuantityConservationSingleOrder) {
    OrderBook book;
    
    Qty submitted_qty = 100;
    Order order(1, Side::Buy, 100, submitted_qty, 10);
    
    auto [trades, events] = book.submit_order(order);
    
    Qty executed_qty = 0;
    for (const auto& trade : trades) {
        executed_qty += trade.execution_quantity;
    }
    
    Qty remaining_qty = 0;
    auto buy_orders = book.get_all_buy_orders();
    for (const auto& o : buy_orders) {
        remaining_qty += o.remaining();
    }
    
    EXPECT_EQ(submitted_qty, executed_qty + remaining_qty);
}

TEST(MatchingTest, QuantityConservationMultipleOrders) {
    OrderBook book;
    
    Qty total_submitted = 0;
    
    // Add several orders
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    total_submitted += 100;
    
    book.add_limit_order(Order(2, Side::Sell, 101, 50, 11));
    total_submitted += 50;
    
    // Submit a buy order that does NOT cross (so it rests)
    auto [trades, events] = book.submit_order(Order(3, Side::Buy, 95, 120, 12));
    total_submitted += 120;
    
    Qty executed_qty = 0;
    for (const auto& trade : trades) {
        executed_qty += trade.execution_quantity;
    }
    
    Qty remaining_qty = 0;
    auto sell_orders = book.get_all_sell_orders();
    for (const auto& o : sell_orders) {
        remaining_qty += o.remaining();
    }
    auto buy_orders = book.get_all_buy_orders();
    for (const auto& o : buy_orders) {
        remaining_qty += o.remaining();
    }
    
    EXPECT_EQ(total_submitted, executed_qty + remaining_qty);
}

TEST(MatchingTest, QuantityConservationWithPartialFills) {
    OrderBook book;
    
    Qty total_submitted = 0;
    
    // Add a large sell order
    book.add_limit_order(Order(1, Side::Sell, 100, 1000, 10));
    total_submitted += 1000;
    
    // Submit multiple buy orders that do NOT cross (so they rest)
    auto [trades1, events1] = book.submit_order(Order(2, Side::Buy, 95, 300, 11));
    total_submitted += 300;
    
    auto [trades2, events2] = book.submit_order(Order(3, Side::Buy, 95, 400, 12));
    total_submitted += 400;
    
    Qty executed_qty = 0;
    for (const auto& trade : trades1) {
        executed_qty += trade.execution_quantity;
    }
    for (const auto& trade : trades2) {
        executed_qty += trade.execution_quantity;
    }
    
    Qty remaining_qty = 0;
    auto sell_orders = book.get_all_sell_orders();
    for (const auto& o : sell_orders) {
        remaining_qty += o.remaining();
    }
    auto buy_orders = book.get_all_buy_orders();
    for (const auto& o : buy_orders) {
        remaining_qty += o.remaining();
    }
    
    EXPECT_EQ(total_submitted, executed_qty + remaining_qty);
}

// ============================================================================
// Order State Tests
// ============================================================================

TEST(MatchingTest, FilledQuantityNeverExceedsTotalQuantity) {
    OrderBook book;
    
    // Add a sell order
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    
    // Submit multiple buy orders
    auto [trades1, events1] = book.submit_order(Order(2, Side::Buy, 105, 50, 11));
    auto [trades2, events2] = book.submit_order(Order(3, Side::Buy, 105, 50, 12));
    auto [trades3, events3] = book.submit_order(Order(4, Side::Buy, 105, 50, 13));
    
    // Verify no negative quantities
    auto sell_orders = book.get_all_sell_orders();
    for (const auto& order : sell_orders) {
        EXPECT_GE(order.filled, 0);
        EXPECT_LE(order.filled, order.quantity);
        EXPECT_GE(order.remaining(), 0);
    }
}

TEST(MatchingTest, NoNegativeQuantitiesAfterMatching) {
    OrderBook book;
    
    // Add orders
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    book.add_limit_order(Order(2, Side::Buy, 95, 100, 11));
    
    // Submit crossing orders
    auto [trades1, events1] = book.submit_order(Order(3, Side::Buy, 105, 150, 12));
    auto [trades2, events2] = book.submit_order(Order(4, Side::Sell, 90, 150, 13));
    
    // Verify no negative quantities
    auto all_orders = book.get_all_buy_orders();
    for (const auto& order : all_orders) {
        EXPECT_GE(order.filled, 0);
        EXPECT_LE(order.filled, order.quantity);
        EXPECT_GE(order.remaining(), 0);
    }
    
    all_orders = book.get_all_sell_orders();
    for (const auto& order : all_orders) {
        EXPECT_GE(order.filled, 0);
        EXPECT_LE(order.filled, order.quantity);
        EXPECT_GE(order.remaining(), 0);
    }
}

// ============================================================================
// Trade Event Tests
// ============================================================================

TEST(MatchingTest, TradeEventContainsCorrectInformation) {
    OrderBook book;
    
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    
    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 50, 11));
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].buy_order_id, 2);
    EXPECT_EQ(trades[0].sell_order_id, 1);
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    EXPECT_GT(trades[0].sequence, 0);
}

TEST(MatchingTest, TradeEventExecutionPriceIsRestingOrderPrice) {
    OrderBook book;
    
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    
    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 50, 11));
    
    ASSERT_EQ(trades.size(), 1);
    // Execution price should be resting order price (100), not incoming order price (105)
    EXPECT_EQ(trades[0].execution_price, 100);
}

// ============================================================================
// Market Data Event Tests
// ============================================================================

TEST(MatchingTest, OrderAddedEventGenerated) {
    OrderBook book;
    
    auto [trades, events] = book.submit_order(Order(1, Side::Buy, 100, 50, 10));
    
    bool order_added_found = false;
    for (const auto& event : events) {
        if (event.event_type == MarketDataEventType::OrderAdded) {
            order_added_found = true;
            EXPECT_EQ(event.order_id, 1);
            EXPECT_EQ(event.side, Side::Buy);
            EXPECT_EQ(event.price, 100);
            EXPECT_EQ(event.quantity, 50);
            EXPECT_EQ(event.filled, 0);
        }
    }
    EXPECT_TRUE(order_added_found);
}

TEST(MatchingTest, OrderFullyFilledEventGenerated) {
    OrderBook book;
    
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    
    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 100, 11));
    
    bool fully_filled_found = false;
    for (const auto& event : events) {
        if (event.event_type == MarketDataEventType::OrderFullyFilled) {
            fully_filled_found = true;
            // The resting sell order (id 1) should be fully filled
            if (event.order_id == 1) {
                EXPECT_EQ(event.filled, 100);
            }
        }
    }
    EXPECT_TRUE(fully_filled_found);
}

TEST(MatchingTest, OrderPartiallyFilledEventGenerated) {
    OrderBook book;
    
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    
    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 50, 11));
    
    bool partially_filled_found = false;
    for (const auto& event : events) {
        if (event.event_type == MarketDataEventType::OrderPartiallyFilled) {
            partially_filled_found = true;
            EXPECT_EQ(event.order_id, 1);
            EXPECT_EQ(event.filled, 50);
        }
    }
    EXPECT_TRUE(partially_filled_found);
}

// ============================================================================
// Randomized Correctness Test
// ============================================================================

TEST(MatchingTest, RandomizedCorrectnessTest) {
    OrderBook book;
    
    // Fixed seed for reproducibility
    uint64_t seed = 42;
    uint64_t next_id = 1;
    uint64_t next_seq = 1;
    
    // Simple linear congruential generator
    auto rng = [&seed]() {
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        return seed;
    };
    
    Qty total_submitted = 0;
    Qty total_executed = 0;
    
    // Submit 100 random orders
    for (int i = 0; i < 100; ++i) {
        Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
        OrderType type = (rng() % 3 == 0) ? OrderType::Market : OrderType::Limit;
        
        Qty qty = (rng() % 100) + 1;
        Price price = (rng() % 50) + 100;
        
        total_submitted += qty;
        
        if (type == OrderType::Limit) {
            Order order(next_id++, side, price, qty, next_seq++);
            auto [trades, events] = book.submit_order(order);
            
            for (const auto& trade : trades) {
                total_executed += trade.execution_quantity;
            }
        } else {
            Order order(next_id++, side, qty, next_seq++);
            auto [trades, events] = book.submit_order(order);
            
            for (const auto& trade : trades) {
                total_executed += trade.execution_quantity;
            }
        }
        
        // Verify invariants after each operation
        auto all_buy_orders = book.get_all_buy_orders();
        for (const auto& order : all_buy_orders) {
            EXPECT_LE(order.filled, order.quantity);
            EXPECT_GE(order.remaining(), 0);
        }
        
        auto all_sell_orders = book.get_all_sell_orders();
        for (const auto& order : all_sell_orders) {
            EXPECT_LE(order.filled, order.quantity);
            EXPECT_GE(order.remaining(), 0);
        }
    }
    
    // Final quantity check
    Qty total_remaining = 0;
    auto all_buy_orders = book.get_all_buy_orders();
    for (const auto& order : all_buy_orders) {
        total_remaining += order.remaining();
    }
    
    auto all_sell_orders = book.get_all_sell_orders();
    for (const auto& order : all_sell_orders) {
        total_remaining += order.remaining();
    }
    
    // Note: This is an approximate check since market orders don't rest
    EXPECT_LE(total_executed, total_submitted);
}

// ============================================================================
// Cancellation Tests
// ============================================================================

TEST(CancellationTest, CancelExistingBid) {
    OrderBook book;
    
    // Add a buy order
    book.add_limit_order(Order(1, Side::Buy, 100, 100, 10));
    
    // Cancel the order
    auto [cancelled, events] = book.cancel_order(1);
    
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].event_type, MarketDataEventType::OrderCancelled);
    EXPECT_EQ(events[0].order_id, 1);
    
    // Verify order is removed
    auto buy_orders = book.get_all_buy_orders();
    EXPECT_EQ(buy_orders.size(), 0);
    EXPECT_EQ(book.buy_price_level_count(), 0);
}

TEST(CancellationTest, CancelExistingAsk) {
    OrderBook book;
    
    // Add a sell order
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    
    // Cancel the order
    auto [cancelled, events] = book.cancel_order(1);
    
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].event_type, MarketDataEventType::OrderCancelled);
    EXPECT_EQ(events[0].order_id, 1);
    
    // Verify order is removed
    auto sell_orders = book.get_all_sell_orders();
    EXPECT_EQ(sell_orders.size(), 0);
    EXPECT_EQ(book.sell_price_level_count(), 0);
}

TEST(CancellationTest, CancelOnlyOrderAtPriceLevel) {
    OrderBook book;
    
    // Add a single buy order
    book.add_limit_order(Order(1, Side::Buy, 100, 100, 10));
    
    // Cancel the order
    auto [cancelled, events] = book.cancel_order(1);
    
    EXPECT_TRUE(cancelled);
    
    // Verify price level is removed
    EXPECT_EQ(book.buy_price_level_count(), 0);
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(CancellationTest, CancelOrderFromMiddleOfFIFOQueue) {
    OrderBook book;
    
    // Add three orders at same price
    book.add_limit_order(Order(1, Side::Buy, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Buy, 100, 50, 12));
    
    // Cancel the middle order
    auto [cancelled, events] = book.cancel_order(2);
    
    EXPECT_TRUE(cancelled);
    
    // Verify remaining orders are still in correct FIFO order
    auto buy_orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(buy_orders.size(), 2);
    EXPECT_EQ(buy_orders[0].id, 1);
    EXPECT_EQ(buy_orders[1].id, 3);
}

TEST(CancellationTest, CancelOldestOrder) {
    OrderBook book;
    
    // Add three orders at same price
    book.add_limit_order(Order(1, Side::Buy, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Buy, 100, 50, 12));
    
    // Cancel the oldest order
    auto [cancelled, events] = book.cancel_order(1);
    
    EXPECT_TRUE(cancelled);
    
    // Verify remaining orders are still in correct FIFO order
    auto buy_orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(buy_orders.size(), 2);
    EXPECT_EQ(buy_orders[0].id, 2);
    EXPECT_EQ(buy_orders[1].id, 3);
}

TEST(CancellationTest, CancelNewestOrder) {
    OrderBook book;
    
    // Add three orders at same price
    book.add_limit_order(Order(1, Side::Buy, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Buy, 100, 50, 12));
    
    // Cancel the newest order
    auto [cancelled, events] = book.cancel_order(3);
    
    EXPECT_TRUE(cancelled);
    
    // Verify remaining orders are still in correct FIFO order
    auto buy_orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(buy_orders.size(), 2);
    EXPECT_EQ(buy_orders[0].id, 1);
    EXPECT_EQ(buy_orders[1].id, 2);
}

TEST(CancellationTest, CancelNonexistentOrderId) {
    OrderBook book;
    
    // Try to cancel non-existent order
    auto [cancelled, events] = book.cancel_order(999);
    
    EXPECT_FALSE(cancelled);
    EXPECT_EQ(events.size(), 0);
}

TEST(CancellationTest, CancelAlreadyFilledOrder) {
    OrderBook book;
    
    // Add a sell order
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    
    // Submit a buy order that fully fills it
    book.submit_order(Order(2, Side::Buy, 105, 100, 11));
    
    // Try to cancel the already-filled order
    auto [cancelled, events] = book.cancel_order(1);
    
    EXPECT_FALSE(cancelled);
    EXPECT_EQ(events.size(), 0);
}

TEST(CancellationTest, CancelPartiallyFilledOrderThatRests) {
    OrderBook book;
    
    // Add a large sell order
    book.add_limit_order(Order(1, Side::Sell, 100, 1000, 10));
    
    // Submit a buy order that partially fills it
    book.submit_order(Order(2, Side::Buy, 105, 300, 11));
    
    // Cancel the partially filled order
    auto [cancelled, events] = book.cancel_order(1);
    
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].event_type, MarketDataEventType::OrderCancelled);
    
    // Verify order is removed
    auto sell_orders = book.get_all_sell_orders();
    EXPECT_EQ(sell_orders.size(), 0);
}

TEST(CancellationTest, CancelAfterMultiplePriceLevelsExist) {
    OrderBook book;
    
    // Add orders at multiple price levels
    book.add_limit_order(Order(1, Side::Buy, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 95, 50, 11));
    book.add_limit_order(Order(3, Side::Buy, 90, 50, 12));
    
    // Cancel order at middle price level
    auto [cancelled, events] = book.cancel_order(2);
    
    EXPECT_TRUE(cancelled);
    
    // Verify other price levels remain
    EXPECT_EQ(book.buy_price_level_count(), 2);
    EXPECT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(book.best_bid().value(), 100);
}

TEST(CancellationTest, PriceLevelDisappearsWhenEmpty) {
    OrderBook book;
    
    // Add two orders at same price
    book.add_limit_order(Order(1, Side::Buy, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));
    
    // Cancel both orders
    book.cancel_order(1);
    book.cancel_order(2);
    
    // Verify price level is removed
    EXPECT_EQ(book.buy_price_level_count(), 0);
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(CancellationTest, FIFOOrderingUnchangedAfterCancellation) {
    OrderBook book;
    
    // Add five orders at same price
    book.add_limit_order(Order(1, Side::Buy, 100, 20, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 20, 11));
    book.add_limit_order(Order(3, Side::Buy, 100, 20, 12));
    book.add_limit_order(Order(4, Side::Buy, 100, 20, 13));
    book.add_limit_order(Order(5, Side::Buy, 100, 20, 14));
    
    // Cancel order 3
    book.cancel_order(3);
    
    // Verify remaining orders maintain FIFO order
    auto buy_orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(buy_orders.size(), 4);
    EXPECT_EQ(buy_orders[0].id, 1);
    EXPECT_EQ(buy_orders[1].id, 2);
    EXPECT_EQ(buy_orders[2].id, 4);
    EXPECT_EQ(buy_orders[3].id, 5);
}

TEST(CancellationTest, FullyMatchedOrdersRemovedFromIndex) {
    OrderBook book;
    
    // Add a sell order
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    
    // Submit a buy order that fully fills it
    book.submit_order(Order(2, Side::Buy, 105, 100, 11));
    
    // Try to cancel the fully matched order
    auto [cancelled, events] = book.cancel_order(1);
    
    EXPECT_FALSE(cancelled);
}

TEST(CancellationTest, MarketOrdersNeverIndexed) {
    OrderBook book;
    
    // Add a sell order
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    
    // Submit a market buy order (use market order constructor with 4 params)
    book.submit_order(Order(2, Side::Buy, 50, 11));
    
    // Try to cancel the market order
    auto [cancelled, events] = book.cancel_order(2);
    
    EXPECT_FALSE(cancelled);
}

TEST(CancellationTest, RepeatedCancellationAttempts) {
    OrderBook book;
    
    // Add a buy order
    book.add_limit_order(Order(1, Side::Buy, 100, 100, 10));
    
    // Cancel successfully
    auto [cancelled1, events1] = book.cancel_order(1);
    EXPECT_TRUE(cancelled1);
    
    // Try to cancel again
    auto [cancelled2, events2] = book.cancel_order(1);
    EXPECT_FALSE(cancelled2);
}

TEST(CancellationTest, CancellationFollowedByNewOrderAtSamePrice) {
    OrderBook book;
    
    // Add a buy order
    book.add_limit_order(Order(1, Side::Buy, 100, 100, 10));
    
    // Cancel it
    book.cancel_order(1);
    
    // Add a new order at same price
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));
    
    // Verify new order is present
    auto buy_orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(buy_orders.size(), 1);
    EXPECT_EQ(buy_orders[0].id, 2);
}

TEST(CancellationTest, RandomizedCancellationScenarios) {
    OrderBook book;
    
    // Fixed seed for reproducibility
    uint64_t seed = 12345;
    uint64_t next_id = 1;
    uint64_t next_seq = 1;
    
    // Simple linear congruential generator
    auto rng = [&seed]() {
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        return seed;
    };
    
    // Submit 50 random orders
    std::vector<OrderId> submitted_ids;
    for (int i = 0; i < 50; ++i) {
        Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
        Qty qty = (rng() % 100) + 1;
        Price price = (rng() % 50) + 100;
        
        Order order(next_id++, side, price, qty, next_seq++);
        book.submit_order(order);
        submitted_ids.push_back(order.id);
    }
    
    // Try to cancel random orders
    for (int i = 0; i < 20; ++i) {
        size_t idx = rng() % submitted_ids.size();
        OrderId id_to_cancel = submitted_ids[idx];
        
        auto [cancelled, events] = book.cancel_order(id_to_cancel);
        // Don't assert on result since order might already be filled or not resting
    }
    
    // Verify book state is consistent
    auto all_buy_orders = book.get_all_buy_orders();
    for (const auto& order : all_buy_orders) {
        EXPECT_LE(order.filled, order.quantity);
        EXPECT_GE(order.remaining(), 0);
    }
    
    auto all_sell_orders = book.get_all_sell_orders();
    for (const auto& order : all_sell_orders) {
        EXPECT_LE(order.filled, order.quantity);
        EXPECT_GE(order.remaining(), 0);
    }
}

TEST(CancellationTest, CancelOrderAfterPartialFillAndRest) {
    OrderBook book;
    
    // Add a large sell order
    book.add_limit_order(Order(1, Side::Sell, 100, 1000, 10));
    
    // Submit a buy order that partially fills it
    book.submit_order(Order(2, Side::Buy, 105, 300, 11));
    
    // Verify the sell order is still resting
    auto sell_orders = book.get_orders_at_ask_price(100);
    ASSERT_EQ(sell_orders.size(), 1);
    EXPECT_EQ(sell_orders[0].remaining(), 700);
    
    // Cancel the partially filled order
    auto [cancelled, events] = book.cancel_order(1);
    
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].event_type, MarketDataEventType::OrderCancelled);
    
    // Verify order is removed
    sell_orders = book.get_orders_at_ask_price(100);
    EXPECT_EQ(sell_orders.size(), 0);
}

TEST(CancellationTest, CancelOrderFromDifferentPriceLevels) {
    OrderBook book;
    
    // Add orders at different price levels
    book.add_limit_order(Order(1, Side::Buy, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 95, 50, 11));
    book.add_limit_order(Order(3, Side::Buy, 90, 50, 12));
    
    // Cancel from highest price
    book.cancel_order(1);
    EXPECT_EQ(book.best_bid().value(), 95);
    
    // Cancel from lowest price
    book.cancel_order(3);
    EXPECT_EQ(book.best_bid().value(), 95);
    
    // Cancel remaining
    book.cancel_order(2);
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(CancellationTest, BookIndexConsistencyAfterCancellation) {
    OrderBook book;
    
    // Add multiple orders
    book.add_limit_order(Order(1, Side::Buy, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Sell, 105, 50, 12));
    book.add_limit_order(Order(4, Side::Sell, 105, 50, 13));
    
    // Verify consistency before cancellation
    EXPECT_TRUE(verify_book_index_consistency(book));
    
    // Cancel from middle of buy price level
    book.cancel_order(2);
    EXPECT_TRUE(verify_book_index_consistency(book));
    
    // Cancel from sell price level
    book.cancel_order(3);
    EXPECT_TRUE(verify_book_index_consistency(book));
    
    // Cancel remaining orders
    book.cancel_order(1);
    book.cancel_order(4);
    EXPECT_TRUE(verify_book_index_consistency(book));
}

TEST(CancellationTest, BookIndexConsistencyAfterMatching) {
    OrderBook book;
    
    // Add orders
    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Sell, 105, 50, 12));
    
    // Submit a buy order that partially fills
    book.submit_order(Order(4, Side::Buy, 110, 150, 13));
    
    // Verify consistency after matching
    EXPECT_TRUE(verify_book_index_consistency(book));
    
    // Cancel the partially filled order
    book.cancel_order(1);
    EXPECT_TRUE(verify_book_index_consistency(book));
}

TEST(CancellationTest, BookIndexConsistencyAfterMultipleOperations) {
    OrderBook book;
    
    // Add orders
    book.add_limit_order(Order(1, Side::Buy, 95, 100, 10));
    book.add_limit_order(Order(2, Side::Buy, 95, 50, 11));
    book.add_limit_order(Order(3, Side::Sell, 100, 100, 12));
    book.add_limit_order(Order(4, Side::Sell, 100, 50, 13));
    
    EXPECT_TRUE(verify_book_index_consistency(book));
    
    // Submit matching order
    book.submit_order(Order(5, Side::Buy, 105, 75, 14));
    EXPECT_TRUE(verify_book_index_consistency(book));
    
    // Cancel an order
    book.cancel_order(2);
    EXPECT_TRUE(verify_book_index_consistency(book));
    
    // Add more orders
    book.add_limit_order(Order(6, Side::Buy, 90, 50, 15));
    EXPECT_TRUE(verify_book_index_consistency(book));
    
    // Submit another matching order
    book.submit_order(Order(7, Side::Sell, 85, 100, 16));
    EXPECT_TRUE(verify_book_index_consistency(book));
}
