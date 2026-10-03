#include <gtest/gtest.h>
#include <engine/orderbook_v1_pool.hpp>
#include <engine/orderbook_v1.hpp>
#include <engine/types.hpp>
#include <numeric>
#include <vector>
#include <unordered_set>
#include <limits>
#include <iostream>

using namespace engine;

// Helper function to verify book/index consistency for v1
bool verify_book_index_consistency_v1_pool(const OrderBookV1Pool& book) {
    auto buy_orders = book.get_all_buy_orders();
    auto sell_orders = book.get_all_sell_orders();

    std::unordered_set<OrderId> book_order_ids;

    for (const auto& order : buy_orders) {
        if (order.filled > order.quantity) return false;
        book_order_ids.insert(order.id);
    }
    for (const auto& order : sell_orders) {
        if (order.filled > order.quantity) return false;
        book_order_ids.insert(order.id);
    }

    size_t total_book_orders = buy_orders.size() + sell_orders.size();
    if (book_order_ids.size() != total_book_orders) {
        return false;
    }

    for (OrderId id : book_order_ids) {
        if (!book.order_id_in_index(id)) {
            return false;
        }
    }

    if (book.order_index_size() != total_book_orders) {
        return false;
    }

    return true;
}

// ============================================================================
// Basic Matching Tests (V1)
// ============================================================================

TEST(MatchingTestV1Pool, BuyCrossesOneSell) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 50, 11));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].buy_order_id, 2);
    EXPECT_EQ(trades[0].sell_order_id, 1);
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[0].execution_quantity, 50);

    auto sell_orders = book.get_orders_at_ask_price(100);
    ASSERT_EQ(sell_orders.size(), 1);
    EXPECT_EQ(sell_orders[0].remaining(), 50);
}

TEST(MatchingTestV1Pool, SellCrossesOneBuy) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 100, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Sell, 95, 50, 11));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].buy_order_id, 1);
    EXPECT_EQ(trades[0].sell_order_id, 2);
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[0].execution_quantity, 50);

    auto buy_orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(buy_orders.size(), 1);
    EXPECT_EQ(buy_orders[0].remaining(), 50);
}

TEST(MatchingTestV1Pool, NoCrossingBuyRests) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 95, 50, 11));

    EXPECT_EQ(trades.size(), 0);

    auto buy_orders = book.get_orders_at_bid_price(95);
    ASSERT_EQ(buy_orders.size(), 1);
    EXPECT_EQ(buy_orders[0].remaining(), 50);
}

TEST(MatchingTestV1Pool, NoCrossingSellRests) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 100, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Sell, 105, 50, 11));

    EXPECT_EQ(trades.size(), 0);

    auto sell_orders = book.get_orders_at_ask_price(105);
    ASSERT_EQ(sell_orders.size(), 1);
    EXPECT_EQ(sell_orders[0].remaining(), 50);
}

// ============================================================================
// Full Fill Tests (V1)
// ============================================================================

TEST(MatchingTestV1Pool, IncomingOrderCompletelyFillsRestingOrder) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 100, 11));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_quantity, 100);

    auto sell_orders = book.get_orders_at_ask_price(100);
    EXPECT_EQ(sell_orders.size(), 0);

    EXPECT_EQ(book.sell_price_level_count(), 0);
}

TEST(MatchingTestV1Pool, RestingOrderCompletelyFillsIncomingOrder) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 1000, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 100, 11));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_quantity, 100);

    auto sell_orders = book.get_orders_at_ask_price(100);
    ASSERT_EQ(sell_orders.size(), 1);
    EXPECT_EQ(sell_orders[0].remaining(), 900);
}

// ============================================================================
// Partial Fill Tests (V1)
// ============================================================================

TEST(MatchingTestV1Pool, IncomingOrderPartiallyFillsRestingOrder) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 50, 11));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_quantity, 50);

    auto sell_orders = book.get_orders_at_ask_price(100);
    ASSERT_EQ(sell_orders.size(), 1);
    EXPECT_EQ(sell_orders[0].remaining(), 50);
}

TEST(MatchingTestV1Pool, IncomingOrderCompletelyFillsMultipleRestingOrders) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 30, 11));
    book.add_limit_order(Order(3, Side::Sell, 100, 20, 12));

    auto [trades, events] = book.submit_order(Order(4, Side::Buy, 105, 100, 13));

    ASSERT_EQ(trades.size(), 3);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    EXPECT_EQ(trades[1].execution_quantity, 30);
    EXPECT_EQ(trades[2].execution_quantity, 20);

    auto sell_orders = book.get_orders_at_ask_price(100);
    EXPECT_EQ(sell_orders.size(), 0);
}

TEST(MatchingTestV1Pool, IncomingOrderPartiallyConsumesMultiplePriceLevelsAndRests) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Sell, 101, 30, 11));
    book.add_limit_order(Order(3, Side::Sell, 102, 20, 12));

    auto [trades, events] = book.submit_order(Order(4, Side::Buy, 103, 110, 13));

    ASSERT_EQ(trades.size(), 3);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    EXPECT_EQ(trades[1].execution_quantity, 30);
    EXPECT_EQ(trades[2].execution_quantity, 20);

    auto buy_orders = book.get_orders_at_bid_price(103);
    ASSERT_EQ(buy_orders.size(), 1);
    EXPECT_EQ(buy_orders[0].remaining(), 10);

    auto sell_orders_100 = book.get_orders_at_ask_price(100);
    EXPECT_EQ(sell_orders_100.size(), 0);
    auto sell_orders_101 = book.get_orders_at_ask_price(101);
    EXPECT_EQ(sell_orders_101.size(), 0);
    auto sell_orders_102 = book.get_orders_at_ask_price(102);
    EXPECT_EQ(sell_orders_102.size(), 0);
}

TEST(MatchingTestV1Pool, RestingOrderIsPartiallyFilledAndRemainsWithReducedQuantity) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 30, 11));

    ASSERT_EQ(trades.size(), 1);

    auto sell_orders = book.get_orders_at_ask_price(100);
    ASSERT_EQ(sell_orders.size(), 1);
    EXPECT_EQ(sell_orders[0].quantity, 100);
    EXPECT_EQ(sell_orders[0].filled, 30);
    EXPECT_EQ(sell_orders[0].remaining(), 70);
}

// ============================================================================
// Price Priority Tests (V1)
// ============================================================================

TEST(MatchingTestV1Pool, BuyConsumesLowestAskFirst) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 102, 50, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Sell, 101, 50, 12));

    auto [trades, events] = book.submit_order(Order(4, Side::Buy, 105, 100, 13));

    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    EXPECT_EQ(trades[1].execution_price, 101);
    EXPECT_EQ(trades[1].execution_quantity, 50);
}

TEST(MatchingTestV1Pool, BetterPricedOrdersAreConsumedBeforeWorsePricedOrders) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 105, 50, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Sell, 110, 50, 12));

    auto [trades, events] = book.submit_order(Order(4, Side::Buy, 108, 100, 13));

    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[1].execution_price, 105);

    auto sell_orders = book.get_orders_at_ask_price(110);
    ASSERT_EQ(sell_orders.size(), 1);
}

TEST(MatchingTestV1Pool, SellConsumesHighestBidFirst) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 98, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Buy, 99, 50, 12));

    auto [trades, events] = book.submit_order(Order(4, Side::Sell, 95, 100, 13));

    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    EXPECT_EQ(trades[1].execution_price, 99);
    EXPECT_EQ(trades[1].execution_quantity, 50);
}

// ============================================================================
// Time Priority Tests (V1)
// ============================================================================

TEST(MatchingTestV1Pool, TwoAsksAtSamePriceOlderAskFillsFirst) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 50, 11));

    auto [trades, events] = book.submit_order(Order(3, Side::Buy, 105, 75, 12));

    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].sell_order_id, 1);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    EXPECT_EQ(trades[1].sell_order_id, 2);
    EXPECT_EQ(trades[1].execution_quantity, 25);
}

TEST(MatchingTestV1Pool, TwoBidsAtSamePriceOlderBidFillsFirst) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));

    auto [trades, events] = book.submit_order(Order(3, Side::Sell, 95, 75, 12));

    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].buy_order_id, 1);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    EXPECT_EQ(trades[1].buy_order_id, 2);
    EXPECT_EQ(trades[1].execution_quantity, 25);
}

TEST(MatchingTestV1Pool, NewerOrderCannotJumpAheadAtSamePrice) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Sell, 100, 50, 12));

    auto [trades, events] = book.submit_order(Order(4, Side::Buy, 105, 125, 13));

    ASSERT_EQ(trades.size(), 3);
    EXPECT_EQ(trades[0].sell_order_id, 1);
    EXPECT_EQ(trades[1].sell_order_id, 2);
    EXPECT_EQ(trades[2].sell_order_id, 3);
}

// ============================================================================
// Market Order Tests (V1)
// ============================================================================

TEST(MatchingTestV1Pool, MarketBuyConsumesBestAsksFirst) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 102, 50, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Sell, 101, 50, 12));

    auto [trades, events] = book.submit_order(Order(4, Side::Buy, 150, 13));

    ASSERT_EQ(trades.size(), 3);
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[1].execution_price, 101);
    EXPECT_EQ(trades[2].execution_price, 102);
}

TEST(MatchingTestV1Pool, MarketOrderAgainstEmptyBook) {
    OrderBookV1Pool book;

    auto [trades, events] = book.submit_order(Order(1, Side::Buy, 100, 10));

    EXPECT_EQ(trades.size(), 0);
    EXPECT_TRUE(book.empty());
}

TEST(MatchingTestV1Pool, MarketOrderRemainderDoesNotRestOnBook) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 30, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 100, 11));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_quantity, 30);

    EXPECT_TRUE(book.buy_side_empty());
    EXPECT_TRUE(book.sell_side_empty());
}

TEST(MatchingTestV1Pool, MarketSellConsumesBestBidsFirst) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 98, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Buy, 99, 50, 12));

    auto [trades, events] = book.submit_order(Order(4, Side::Sell, 150, 13));

    ASSERT_EQ(trades.size(), 3);
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[1].execution_price, 99);
    EXPECT_EQ(trades[2].execution_price, 98);
}

TEST(MatchingTestV1Pool, MarketOrderConsumesMultiplePriceLevels) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 30, 10));
    book.add_limit_order(Order(2, Side::Sell, 101, 30, 11));
    book.add_limit_order(Order(3, Side::Sell, 102, 30, 12));

    auto [trades, events] = book.submit_order(Order(4, Side::Buy, 75, 13));

    ASSERT_EQ(trades.size(), 3);
    EXPECT_EQ(trades[0].execution_quantity, 30);
    EXPECT_EQ(trades[1].execution_quantity, 30);
    EXPECT_EQ(trades[2].execution_quantity, 15);
}

TEST(MatchingTestV1Pool, MarketOrderWithInsufficientLiquidity) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 50, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 100, 11));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_quantity, 50);

    auto buy_orders = book.get_all_buy_orders();
    EXPECT_EQ(buy_orders.size(), 0);
}

TEST(MatchingTestV1Pool, EmptyPriceLevelsRemovedAfterFinalOrderFilled) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 50, 11));

    auto [trades, events] = book.submit_order(Order(3, Side::Buy, 105, 100, 12));

    auto sell_orders = book.get_orders_at_ask_price(100);
    EXPECT_EQ(sell_orders.size(), 0);
    EXPECT_EQ(book.sell_price_level_count(), 0);
}

TEST(MatchingTestV1Pool, BestBidAskUpdatedCorrectlyAfterTrades) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 100, 10));
    book.add_limit_order(Order(2, Side::Buy, 95, 100, 11));
    book.add_limit_order(Order(3, Side::Sell, 105, 100, 12));

    auto [trades, events] = book.submit_order(Order(4, Side::Sell, 98, 100, 13));

    auto best_bid = book.best_bid();
    ASSERT_TRUE(best_bid.has_value());
    EXPECT_EQ(best_bid.value(), 95);
}

TEST(MatchingTestV1Pool, BookDepthCorrectAfterPartialFullFills) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    book.add_limit_order(Order(2, Side::Sell, 101, 100, 11));
    book.add_limit_order(Order(3, Side::Sell, 102, 100, 12));

    auto [trades, events] = book.submit_order(Order(4, Side::Buy, 105, 150, 13));

    EXPECT_EQ(book.sell_order_count(), 2);
    EXPECT_EQ(book.sell_price_level_count(), 2);

    auto sell_orders_101 = book.get_orders_at_ask_price(101);
    ASSERT_EQ(sell_orders_101.size(), 1);
    EXPECT_EQ(sell_orders_101[0].remaining(), 50);
}

TEST(MatchingTestV1Pool, QuantityConservationSingleOrder) {
    OrderBookV1Pool book;

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

TEST(MatchingTestV1Pool, QuantityConservationMultipleOrders) {
    OrderBookV1Pool book;

    Qty total_submitted = 0;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    total_submitted += 100;

    book.add_limit_order(Order(2, Side::Sell, 101, 50, 11));
    total_submitted += 50;

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

TEST(MatchingTestV1Pool, QuantityConservationWithPartialFills) {
    OrderBookV1Pool book;

    Qty total_submitted = 0;

    book.add_limit_order(Order(1, Side::Sell, 100, 1000, 10));
    total_submitted += 1000;

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

TEST(MatchingTestV1Pool, FilledQuantityNeverExceedsTotalQuantity) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));

    auto [trades1, events1] = book.submit_order(Order(2, Side::Buy, 105, 50, 11));
    auto [trades2, events2] = book.submit_order(Order(3, Side::Buy, 105, 50, 12));
    auto [trades3, events3] = book.submit_order(Order(4, Side::Buy, 105, 50, 13));

    auto sell_orders = book.get_all_sell_orders();
    for (const auto& order : sell_orders) {
        EXPECT_GE(order.filled, 0);
        EXPECT_LE(order.filled, order.quantity);
        EXPECT_GE(order.remaining(), 0);
    }
}

TEST(MatchingTestV1Pool, NoNegativeQuantitiesAfterMatching) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    book.add_limit_order(Order(2, Side::Buy, 95, 100, 11));

    auto [trades1, events1] = book.submit_order(Order(3, Side::Buy, 105, 150, 12));
    auto [trades2, events2] = book.submit_order(Order(4, Side::Sell, 90, 150, 13));

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

TEST(MatchingTestV1Pool, TradeEventContainsCorrectInformation) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 50, 11));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].buy_order_id, 2);
    EXPECT_EQ(trades[0].sell_order_id, 1);
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    EXPECT_GT(trades[0].sequence, 0);
}

TEST(MatchingTestV1Pool, TradeEventExecutionPriceIsRestingOrderPrice) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 50, 11));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_price, 100);
}

TEST(MatchingTestV1Pool, OrderAddedEventGenerated) {
    OrderBookV1Pool book;

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

TEST(MatchingTestV1Pool, OrderFullyFilledEventGenerated) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 100, 11));

    bool fully_filled_found = false;
    for (const auto& event : events) {
        if (event.event_type == MarketDataEventType::OrderFullyFilled) {
            fully_filled_found = true;
            if (event.order_id == 1) {
                EXPECT_EQ(event.filled, 100);
            }
        }
    }
    EXPECT_TRUE(fully_filled_found);
}

TEST(MatchingTestV1Pool, OrderPartiallyFilledEventGenerated) {
    OrderBookV1Pool book;

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

TEST(MatchingTestV1Pool, RandomizedCorrectnessTest) {
    OrderBookV1Pool book;

    uint64_t seed = 42;
    uint64_t next_id = 1;
    uint64_t next_seq = 1;

    auto rng = [&seed]() {
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        return seed;
    };

    Qty total_submitted = 0;
    Qty total_executed = 0;

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

    Qty total_remaining = 0;
    auto all_buy_orders = book.get_all_buy_orders();
    for (const auto& order : all_buy_orders) {
        total_remaining += order.remaining();
    }

    auto all_sell_orders = book.get_all_sell_orders();
    for (const auto& order : all_sell_orders) {
        total_remaining += order.remaining();
    }

    EXPECT_LE(total_executed, total_submitted);
}

// ============================================================================
// Cancellation Tests (V1)
// ============================================================================

TEST(CancellationTestV1Pool, CancelExistingBid) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 100, 10));

    auto [cancelled, events] = book.cancel_order(1);

    EXPECT_TRUE(cancelled);
    EXPECT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].event_type, MarketDataEventType::OrderCancelled);
    EXPECT_EQ(events[0].order_id, 1);

    auto buy_orders = book.get_all_buy_orders();
    EXPECT_EQ(buy_orders.size(), 0);
    EXPECT_EQ(book.buy_price_level_count(), 0);
}

TEST(CancellationTestV1Pool, CancelExistingAsk) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));

    auto [cancelled, events] = book.cancel_order(1);

    EXPECT_TRUE(cancelled);
    EXPECT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].event_type, MarketDataEventType::OrderCancelled);
    EXPECT_EQ(events[0].order_id, 1);

    auto sell_orders = book.get_all_sell_orders();
    EXPECT_EQ(sell_orders.size(), 0);
    EXPECT_EQ(book.sell_price_level_count(), 0);
}

TEST(CancellationTestV1Pool, CancelOrderFromMiddleOfFIFOQueue) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Buy, 100, 50, 12));

    auto [cancelled, events] = book.cancel_order(2);

    EXPECT_TRUE(cancelled);

    auto buy_orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(buy_orders.size(), 2);
    EXPECT_EQ(buy_orders[0].id, 1);
    EXPECT_EQ(buy_orders[1].id, 3);
}

TEST(CancellationTestV1Pool, CancelNonexistentOrderId) {
    OrderBookV1Pool book;

    auto [cancelled, events] = book.cancel_order(999);

    EXPECT_FALSE(cancelled);
    EXPECT_EQ(events.size(), 0);
}

TEST(CancellationTestV1Pool, CancelAlreadyFilledOrder) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));

    book.submit_order(Order(2, Side::Buy, 105, 100, 11));

    auto [cancelled, events] = book.cancel_order(1);

    EXPECT_FALSE(cancelled);
    EXPECT_EQ(events.size(), 0);
}

TEST(CancellationTestV1Pool, BookIndexConsistencyAfterCancellation) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Sell, 105, 50, 12));
    book.add_limit_order(Order(4, Side::Sell, 105, 50, 13));

    EXPECT_TRUE(verify_book_index_consistency_v1_pool(book));

    book.cancel_order(2);
    EXPECT_TRUE(verify_book_index_consistency_v1_pool(book));

    book.cancel_order(3);
    EXPECT_TRUE(verify_book_index_consistency_v1_pool(book));

    book.cancel_order(1);
    book.cancel_order(4);
    EXPECT_TRUE(verify_book_index_consistency_v1_pool(book));
}

TEST(CancellationTestV1Pool, CancelOnlyOrderAtPriceLevel) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 100, 10));

    auto [cancelled, events] = book.cancel_order(1);

    EXPECT_TRUE(cancelled);

    EXPECT_EQ(book.buy_price_level_count(), 0);
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(CancellationTestV1Pool, CancelOldestOrder) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Buy, 100, 50, 12));

    auto [cancelled, events] = book.cancel_order(1);

    EXPECT_TRUE(cancelled);

    auto buy_orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(buy_orders.size(), 2);
    EXPECT_EQ(buy_orders[0].id, 2);
    EXPECT_EQ(buy_orders[1].id, 3);
}

TEST(CancellationTestV1Pool, CancelNewestOrder) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Buy, 100, 50, 12));

    auto [cancelled, events] = book.cancel_order(3);

    EXPECT_TRUE(cancelled);

    auto buy_orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(buy_orders.size(), 2);
    EXPECT_EQ(buy_orders[0].id, 1);
    EXPECT_EQ(buy_orders[1].id, 2);
}

TEST(CancellationTestV1Pool, CancelPartiallyFilledOrderThatRests) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 1000, 10));

    book.submit_order(Order(2, Side::Buy, 105, 300, 11));

    auto sell_orders = book.get_orders_at_ask_price(100);
    ASSERT_EQ(sell_orders.size(), 1);
    EXPECT_EQ(sell_orders[0].remaining(), 700);

    auto [cancelled, events] = book.cancel_order(1);

    EXPECT_TRUE(cancelled);
    EXPECT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].event_type, MarketDataEventType::OrderCancelled);

    sell_orders = book.get_orders_at_ask_price(100);
    EXPECT_EQ(sell_orders.size(), 0);
}

TEST(CancellationTestV1Pool, CancelAfterMultiplePriceLevelsExist) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 95, 50, 11));
    book.add_limit_order(Order(3, Side::Buy, 90, 50, 12));

    auto [cancelled, events] = book.cancel_order(2);

    EXPECT_TRUE(cancelled);

    EXPECT_EQ(book.buy_price_level_count(), 2);
    EXPECT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(book.best_bid().value(), 100);
}

TEST(CancellationTestV1Pool, PriceLevelDisappearsWhenEmpty) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));

    book.cancel_order(1);
    book.cancel_order(2);

    EXPECT_EQ(book.buy_price_level_count(), 0);
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(CancellationTestV1Pool, FIFOOrderingUnchangedAfterCancellation) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 20, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 20, 11));
    book.add_limit_order(Order(3, Side::Buy, 100, 20, 12));
    book.add_limit_order(Order(4, Side::Buy, 100, 20, 13));
    book.add_limit_order(Order(5, Side::Buy, 100, 20, 14));

    book.cancel_order(3);

    auto buy_orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(buy_orders.size(), 4);
    EXPECT_EQ(buy_orders[0].id, 1);
    EXPECT_EQ(buy_orders[1].id, 2);
    EXPECT_EQ(buy_orders[2].id, 4);
    EXPECT_EQ(buy_orders[3].id, 5);
}

TEST(CancellationTestV1Pool, FullyMatchedOrdersRemovedFromIndex) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));

    book.submit_order(Order(2, Side::Buy, 105, 100, 11));

    auto [cancelled, events] = book.cancel_order(1);

    EXPECT_FALSE(cancelled);
}

TEST(CancellationTestV1Pool, MarketOrdersNeverIndexed) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));

    book.submit_order(Order(2, Side::Buy, 50, 11));

    auto [cancelled, events] = book.cancel_order(2);

    EXPECT_FALSE(cancelled);
}

TEST(CancellationTestV1Pool, RepeatedCancellationAttempts) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 100, 10));

    auto [cancelled1, events1] = book.cancel_order(1);
    EXPECT_TRUE(cancelled1);

    auto [cancelled2, events2] = book.cancel_order(1);
    EXPECT_FALSE(cancelled2);
}

TEST(CancellationTestV1Pool, CancellationFollowedByNewOrderAtSamePrice) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 100, 10));

    book.cancel_order(1);

    book.add_limit_order(Order(2, Side::Buy, 100, 50, 11));

    auto buy_orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(buy_orders.size(), 1);
    EXPECT_EQ(buy_orders[0].id, 2);
}

TEST(CancellationTestV1Pool, CancelOrderAfterPartialFillAndRest) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 1000, 10));

    book.submit_order(Order(2, Side::Buy, 105, 300, 11));

    auto sell_orders = book.get_orders_at_ask_price(100);
    ASSERT_EQ(sell_orders.size(), 1);
    EXPECT_EQ(sell_orders[0].remaining(), 700);

    auto [cancelled, events] = book.cancel_order(1);

    EXPECT_TRUE(cancelled);
    EXPECT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].event_type, MarketDataEventType::OrderCancelled);

    sell_orders = book.get_orders_at_ask_price(100);
    EXPECT_EQ(sell_orders.size(), 0);
}

TEST(CancellationTestV1Pool, CancelOrderFromDifferentPriceLevels) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 50, 10));
    book.add_limit_order(Order(2, Side::Buy, 95, 50, 11));
    book.add_limit_order(Order(3, Side::Buy, 90, 50, 12));

    book.cancel_order(1);
    EXPECT_EQ(book.best_bid().value(), 95);

    book.cancel_order(3);
    EXPECT_EQ(book.best_bid().value(), 95);

    book.cancel_order(2);
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(CancellationTestV1Pool, BookIndexConsistencyAfterMatching) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 50, 11));
    book.add_limit_order(Order(3, Side::Sell, 105, 50, 12));

    book.submit_order(Order(4, Side::Buy, 110, 150, 13));

    EXPECT_TRUE(verify_book_index_consistency_v1_pool(book));

    book.cancel_order(1);
    EXPECT_TRUE(verify_book_index_consistency_v1_pool(book));
}

TEST(CancellationTestV1Pool, BookIndexConsistencyAfterMultipleOperations) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 95, 100, 10));
    book.add_limit_order(Order(2, Side::Buy, 95, 50, 11));
    book.add_limit_order(Order(3, Side::Sell, 100, 100, 12));
    book.add_limit_order(Order(4, Side::Sell, 100, 50, 13));

    EXPECT_TRUE(verify_book_index_consistency_v1_pool(book));

    book.submit_order(Order(5, Side::Buy, 105, 75, 14));
    EXPECT_TRUE(verify_book_index_consistency_v1_pool(book));

    book.cancel_order(2);
    EXPECT_TRUE(verify_book_index_consistency_v1_pool(book));

    book.add_limit_order(Order(6, Side::Buy, 90, 50, 15));
    EXPECT_TRUE(verify_book_index_consistency_v1_pool(book));

    book.submit_order(Order(7, Side::Sell, 85, 100, 16));
    EXPECT_TRUE(verify_book_index_consistency_v1_pool(book));
}

TEST(CancellationTestV1Pool, RandomizedCancellationScenarios) {
    OrderBookV1Pool book;

    uint64_t seed = 12345;
    uint64_t next_id = 1;
    uint64_t next_seq = 1;

    auto rng = [&seed]() {
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        return seed;
    };

    std::vector<OrderId> submitted_ids;
    for (int i = 0; i < 50; ++i) {
        Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
        Qty qty = (rng() % 100) + 1;
        Price price = (rng() % 50) + 100;

        Order order(next_id++, side, price, qty, next_seq++);
        book.submit_order(order);
        submitted_ids.push_back(order.id);
    }

    for (int i = 0; i < 20; ++i) {
        size_t idx = rng() % submitted_ids.size();
        OrderId id_to_cancel = submitted_ids[idx];

        auto [cancelled, events] = book.cancel_order(id_to_cancel);
    }

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

// ============================================================================
// Self-Crossing Behavior Tests (V1)
// ============================================================================

TEST(EdgeCaseTestV1Pool, BuyOrderAtSamePriceAsBestAskCrosses) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 100, 50, 11));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[0].execution_quantity, 50);

    auto sell_orders = book.get_orders_at_ask_price(100);
    ASSERT_EQ(sell_orders.size(), 1);
    EXPECT_EQ(sell_orders[0].remaining(), 50);
}

TEST(EdgeCaseTestV1Pool, SellOrderAtSamePriceAsBestBidCrosses) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 100, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Sell, 100, 50, 11));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[0].execution_quantity, 50);

    auto buy_orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(buy_orders.size(), 1);
    EXPECT_EQ(buy_orders[0].remaining(), 50);
}

TEST(EdgeCaseTestV1Pool, BuyOrderOneTickAboveBestAskCrosses) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 100, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 101, 50, 11));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[0].execution_quantity, 50);
}

TEST(EdgeCaseTestV1Pool, SellOrderOneTickBelowBestBidCrosses) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 100, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Sell, 99, 50, 11));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_price, 100);
    EXPECT_EQ(trades[0].execution_quantity, 50);
}

// ============================================================================
// Price Boundary Tests (V1)
// ============================================================================

TEST(EdgeCaseTestV1Pool, MinimumPriceOrder) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 1, 100, 10));

    auto sell_orders = book.get_orders_at_ask_price(1);
    ASSERT_EQ(sell_orders.size(), 1);
    EXPECT_EQ(sell_orders[0].price, 1);
}

TEST(EdgeCaseTestV1Pool, MaximumPriceOrder) {
    OrderBookV1Pool book;

    Price max_price = std::numeric_limits<Price>::max();
    book.add_limit_order(Order(1, Side::Buy, max_price, 100, 10));

    auto buy_orders = book.get_orders_at_bid_price(max_price);
    ASSERT_EQ(buy_orders.size(), 1);
    EXPECT_EQ(buy_orders[0].price, max_price);
}

TEST(EdgeCaseTestV1Pool, VeryLargePriceSpread) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 1, 100, 10));
    book.add_limit_order(Order(2, Side::Sell, 1000000, 100, 11));

    auto best_bid = book.best_bid();
    auto best_ask = book.best_ask();

    ASSERT_TRUE(best_bid.has_value());
    ASSERT_TRUE(best_ask.has_value());
    EXPECT_EQ(best_bid.value(), 1);
    EXPECT_EQ(best_ask.value(), 1000000);
}

// ============================================================================
// Zero/Invalid Quantity Tests (V1)
// ============================================================================

TEST(EdgeCaseTestV1Pool, MinimumQuantityOrder) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100, 1, 10));

    auto buy_orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(buy_orders.size(), 1);
    EXPECT_EQ(buy_orders[0].quantity, 1);
}

TEST(EdgeCaseTestV1Pool, MaximumQuantityOrder) {
    OrderBookV1Pool book;

    Qty max_qty = std::numeric_limits<Qty>::max();
    book.add_limit_order(Order(1, Side::Buy, 100, max_qty, 10));

    auto buy_orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(buy_orders.size(), 1);
    EXPECT_EQ(buy_orders[0].quantity, max_qty);
}

TEST(EdgeCaseTestV1Pool, OrderWithQuantityOneFullyFilled) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100, 1, 10));

    auto [trades, events] = book.submit_order(Order(2, Side::Buy, 105, 1, 11));

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_quantity, 1);

    auto sell_orders = book.get_orders_at_ask_price(100);
    EXPECT_EQ(sell_orders.size(), 0);
}

// ============================================================================
// Large-Scale Randomized Correctness Test (V1)
// ============================================================================

TEST(PropertyBasedTestV1Pool, LargeScaleRandomizedCorrectnessWithConservationInvariant) {
    OrderBookV1Pool book;

    uint64_t seed = 123456789;
    uint64_t next_id = 1;
    uint64_t next_seq = 1;

    int limit_order_count = 0;
    int market_order_count = 0;
    int cancel_count = 0;
    int modify_count = 0;

    auto rng = [&seed]() {
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        return seed;
    };

    std::vector<OrderId> resting_order_ids;

    for (int i = 0; i < 10000; ++i) {
        int op_type = rng() % 10;

        if (!resting_order_ids.empty()) {
            if (op_type < 2) {
                size_t idx = rng() % resting_order_ids.size();
                OrderId id_to_cancel = resting_order_ids[idx];

                book.cancel_order(id_to_cancel);
                cancel_count++;
                resting_order_ids.erase(resting_order_ids.begin() + idx);
            } else if (op_type == 2) {
                size_t idx = rng() % resting_order_ids.size();
                OrderId id_to_modify = resting_order_ids[idx];

                Order order_to_modify;
                bool found = false;
                auto all_buy = book.get_all_buy_orders();
                for (const auto& o : all_buy) {
                    if (o.id == id_to_modify) {
                        order_to_modify = o;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    auto all_sell = book.get_all_sell_orders();
                    for (const auto& o : all_sell) {
                        if (o.id == id_to_modify) {
                            order_to_modify = o;
                            found = true;
                            break;
                        }
                    }
                }

                if (found && order_to_modify.remaining() > 0) {
                    book.cancel_order(id_to_modify);

                    Qty old_qty = order_to_modify.quantity;
                    Qty old_filled = order_to_modify.filled;

                    int change_pct = (rng() % 41) - 20;
                    Qty new_qty = old_qty + (old_qty * change_pct / 100);
                    if (new_qty < old_filled + 1) new_qty = old_filled + 1;

                    Order modified_order(next_id++, order_to_modify.side, order_to_modify.price, new_qty, next_seq++);
                    book.submit_order(modified_order);
                    modify_count++;

                    resting_order_ids.erase(resting_order_ids.begin() + idx);
                    if (modified_order.remaining() > 0) {
                        resting_order_ids.push_back(modified_order.id);
                    }
                }
            }
        }

        if (op_type >= 3) {
            Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            OrderType type = (rng() % 5 == 0) ? OrderType::Market : OrderType::Limit;

            Qty qty = (rng() % 1000) + 1;
            Price price = (rng() % 1000) + 100;

            if (type == OrderType::Limit) {
                Order order(next_id++, side, price, qty, next_seq++);
                book.submit_order(order);
                limit_order_count++;

                if (order.remaining() > 0) {
                    resting_order_ids.push_back(order.id);
                }
            } else {
                Order order(next_id++, side, qty, next_seq++);
                book.submit_order(order);
                market_order_count++;
            }
        }

        std::vector<OrderId> still_resting;
        auto all_buy = book.get_all_buy_orders();
        auto all_sell = book.get_all_sell_orders();

        std::unordered_set<OrderId> current_resting;
        for (const auto& o : all_buy) {
            current_resting.insert(o.id);
        }
        for (const auto& o : all_sell) {
            current_resting.insert(o.id);
        }

        for (OrderId id : resting_order_ids) {
            if (current_resting.count(id)) {
                still_resting.push_back(id);
            }
        }
        resting_order_ids = still_resting;

        auto all_buy_orders = book.get_all_buy_orders();
        for (const auto& order : all_buy_orders) {
            EXPECT_LE(order.filled, order.quantity);
            EXPECT_GE(order.remaining(), 0);
            EXPECT_EQ(order.filled + order.remaining(), order.quantity);
        }

        auto all_sell_orders = book.get_all_sell_orders();
        for (const auto& order : all_sell_orders) {
            EXPECT_LE(order.filled, order.quantity);
            EXPECT_GE(order.remaining(), 0);
            EXPECT_EQ(order.filled + order.remaining(), order.quantity);
        }
    }

    EXPECT_GT(modify_count, 0) << "Modification operations should have been executed";

    std::cout << "V1 Operation distribution:\n";
    std::cout << "  Limit orders: " << limit_order_count << "\n";
    std::cout << "  Market orders: " << market_order_count << "\n";
    std::cout << "  Cancellations: " << cancel_count << "\n";
    std::cout << "  Modifications: " << modify_count << "\n";
    std::cout << "  Total: " << (limit_order_count + market_order_count + cancel_count + modify_count) << "\n";

    auto all_buy_orders = book.get_all_buy_orders();
    for (const auto& order : all_buy_orders) {
        EXPECT_EQ(order.filled + order.remaining(), order.quantity);
    }

    auto all_sell_orders = book.get_all_sell_orders();
    for (const auto& order : all_sell_orders) {
        EXPECT_EQ(order.filled + order.remaining(), order.quantity);
    }
}
