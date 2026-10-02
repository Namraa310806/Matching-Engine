#include <gtest/gtest.h>
#include <engine/orderbook_v1_pool.hpp>
#include <engine/orderbook_v1.hpp>
#include <engine/types.hpp>

using namespace engine;

// OrderBookV1Pool matching tests - mirrors MatchingTestV1 to ensure correctness
TEST(MatchingTestV1Pool, BuyCrossesOneSell) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Sell, 100000, 100, 1));

    Order buy(2, Side::Buy, 100000, 50, 2);
    auto [trades, events] = book.submit_order(buy);

    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    EXPECT_EQ(trades[0].execution_price, 100000);
}

TEST(MatchingTestV1Pool, SellCrossesOneBuy) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Buy, 100000, 100, 1));

    Order sell(2, Side::Sell, 100000, 50, 2);
    auto [trades, events] = book.submit_order(sell);

    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    EXPECT_EQ(trades[0].execution_price, 100000);
}

TEST(MatchingTestV1Pool, NoCrossingBuyRests) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Sell, 100500, 100, 1));

    Order buy(2, Side::Buy, 100000, 50, 2);
    auto [trades, events] = book.submit_order(buy);

    EXPECT_EQ(trades.size(), 0);
    EXPECT_EQ(book.buy_order_count(), 1);
}

TEST(MatchingTestV1Pool, NoCrossingSellRests) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Buy, 99500, 100, 1));

    Order sell(2, Side::Sell, 100000, 50, 2);
    auto [trades, events] = book.submit_order(sell);

    EXPECT_EQ(trades.size(), 0);
    EXPECT_EQ(book.sell_order_count(), 1);
}

TEST(MatchingTestV1Pool, IncomingOrderCompletelyFillsRestingOrder) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Sell, 100000, 100, 1));

    Order buy(2, Side::Buy, 100000, 100, 2);
    auto [trades, events] = book.submit_order(buy);

    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_quantity, 100);
    EXPECT_EQ(book.sell_order_count(), 0);
}

TEST(MatchingTestV1Pool, RestingOrderCompletelyFillsIncomingOrder) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Sell, 100000, 200, 1));

    Order buy(2, Side::Buy, 100000, 100, 2);
    auto [trades, events] = book.submit_order(buy);

    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_quantity, 100);
    EXPECT_EQ(book.sell_order_count(), 1);
}

TEST(MatchingTestV1Pool, IncomingOrderPartiallyFillsRestingOrder) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Sell, 100000, 200, 1));

    Order buy(2, Side::Buy, 100000, 100, 2);
    auto [trades, events] = book.submit_order(buy);

    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].execution_quantity, 100);
    EXPECT_EQ(book.sell_order_count(), 1);
}

TEST(MatchingTestV1Pool, IncomingOrderCompletelyFillsMultipleRestingOrders) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Sell, 100000, 50, 1));
    book.add_limit_order(Order(2, Side::Sell, 100000, 50, 2));

    Order buy(3, Side::Buy, 100000, 100, 3);
    auto [trades, events] = book.submit_order(buy);

    EXPECT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].execution_quantity, 50);
    EXPECT_EQ(trades[1].execution_quantity, 50);
    EXPECT_EQ(book.sell_order_count(), 0);
}

TEST(MatchingTestV1Pool, TwoAsksAtSamePriceOlderAskFillsFirst) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Sell, 100000, 50, 1));
    book.add_limit_order(Order(2, Side::Sell, 100000, 50, 2));

    Order buy(3, Side::Buy, 100000, 100, 3);
    auto [trades, events] = book.submit_order(buy);

    EXPECT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].sell_order_id, 1);  // Older order fills first
    EXPECT_EQ(trades[1].sell_order_id, 2);
}

TEST(MatchingTestV1Pool, TwoBidsAtSamePriceOlderBidFillsFirst) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Buy, 100000, 50, 1));
    book.add_limit_order(Order(2, Side::Buy, 100000, 50, 2));

    Order sell(3, Side::Sell, 100000, 100, 3);
    auto [trades, events] = book.submit_order(sell);

    EXPECT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].buy_order_id, 1);  // Older order fills first
    EXPECT_EQ(trades[1].buy_order_id, 2);
}

TEST(MatchingTestV1Pool, QuantityConservationSingleOrder) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Sell, 100000, 100, 1));

    Order buy(2, Side::Buy, 100000, 50, 2);
    auto [trades, events] = book.submit_order(buy);

    EXPECT_EQ(trades[0].execution_quantity, 50);
    EXPECT_EQ(book.sell_order_count(), 1);
    EXPECT_EQ(book.get_orders_at_ask_price(100000)[0].remaining(), 50);
}

TEST(MatchingTestV1Pool, IdenticalToBaseline) {
    // Run same matching operations on both V1 and V1Pool
    OrderBookV1 baseline;
    OrderBookV1Pool pool;

    // Add resting orders
    baseline.add_limit_order(Order(1, Side::Sell, 100000, 100, 1));
    baseline.add_limit_order(Order(2, Side::Sell, 100000, 100, 2));
    pool.add_limit_order(Order(1, Side::Sell, 100000, 100, 1));
    pool.add_limit_order(Order(2, Side::Sell, 100000, 100, 2));

    // Submit matching order
    Order buy(3, Side::Buy, 100000, 150, 3);
    auto [baseline_trades, baseline_events] = baseline.submit_order(buy);
    auto [pool_trades, pool_events] = pool.submit_order(buy);

    // Verify identical results
    EXPECT_EQ(baseline_trades.size(), pool_trades.size());
    EXPECT_EQ(baseline_events.size(), pool_events.size());
    EXPECT_EQ(baseline.sell_order_count(), pool.sell_order_count());
}
