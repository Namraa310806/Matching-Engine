#include <gtest/gtest.h>
#include <engine/orderbook_v1_pool.hpp>
#include <engine/types.hpp>

using namespace engine;

// OrderBookV1Pool tests - mirrors OrderBookTestV1 to ensure correctness
TEST(OrderBookTestV1Pool, EmptyBuyBook) {
    OrderBookV1Pool book;
    EXPECT_TRUE(book.buy_side_empty());
    EXPECT_EQ(book.buy_order_count(), 0);
}

TEST(OrderBookTestV1Pool, EmptySellBook) {
    OrderBookV1Pool book;
    EXPECT_TRUE(book.sell_side_empty());
    EXPECT_EQ(book.sell_order_count(), 0);
}

TEST(OrderBookTestV1Pool, EmptyBook) {
    OrderBookV1Pool book;
    EXPECT_TRUE(book.empty());
}

TEST(OrderBookTestV1Pool, AddOneBuyOrder) {
    OrderBookV1Pool book;
    Order order(1, Side::Buy, 100000, 100, 1);
    book.add_limit_order(order);

    EXPECT_FALSE(book.buy_side_empty());
    EXPECT_EQ(book.buy_order_count(), 1);
    EXPECT_EQ(book.best_bid(), 100000);
}

TEST(OrderBookTestV1Pool, AddOneSellOrder) {
    OrderBookV1Pool book;
    Order order(1, Side::Sell, 100000, 100, 1);
    book.add_limit_order(order);

    EXPECT_FALSE(book.sell_side_empty());
    EXPECT_EQ(book.sell_order_count(), 1);
    EXPECT_EQ(book.best_ask(), 100000);
}

TEST(OrderBookTestV1Pool, AddMultipleBuyPriceLevels) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Buy, 100000, 100, 1));
    book.add_limit_order(Order(2, Side::Buy, 100500, 100, 2));
    book.add_limit_order(Order(3, Side::Buy, 99500, 100, 3));

    EXPECT_EQ(book.buy_order_count(), 3);
    EXPECT_EQ(book.buy_price_level_count(), 3);
    EXPECT_EQ(book.best_bid(), 100500);  // Highest price first
}

TEST(OrderBookTestV1Pool, AddMultipleSellPriceLevels) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Sell, 100000, 100, 1));
    book.add_limit_order(Order(2, Side::Sell, 100500, 100, 2));
    book.add_limit_order(Order(3, Side::Sell, 99500, 100, 3));

    EXPECT_EQ(book.sell_order_count(), 3);
    EXPECT_EQ(book.sell_price_level_count(), 3);
    EXPECT_EQ(book.best_ask(), 99500);  // Lowest price first
}

TEST(OrderBookTestV1Pool, CorrectBestBid) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Buy, 100000, 100, 1));
    book.add_limit_order(Order(2, Side::Buy, 100500, 100, 2));
    book.add_limit_order(Order(3, Side::Buy, 99500, 100, 3));

    EXPECT_EQ(book.best_bid(), 100500);
}

TEST(OrderBookTestV1Pool, CorrectBestAsk) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Sell, 100000, 100, 1));
    book.add_limit_order(Order(2, Side::Sell, 100500, 100, 2));
    book.add_limit_order(Order(3, Side::Sell, 99500, 100, 3));

    EXPECT_EQ(book.best_ask(), 99500);
}

TEST(OrderBookTestV1Pool, MultipleOrdersAtSamePrice) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Buy, 100000, 100, 1));
    book.add_limit_order(Order(2, Side::Buy, 100000, 100, 2));
    book.add_limit_order(Order(3, Side::Buy, 100000, 100, 3));

    EXPECT_EQ(book.buy_order_count(), 3);
    EXPECT_EQ(book.buy_price_level_count(), 1);
}

TEST(OrderBookTestV1Pool, FIFOOrderingAtSamePrice) {
    OrderBookV1Pool book;
    book.add_limit_order(Order(1, Side::Buy, 100000, 100, 1));
    book.add_limit_order(Order(2, Side::Buy, 100000, 100, 2));
    book.add_limit_order(Order(3, Side::Buy, 100000, 100, 3));

    auto orders = book.get_orders_at_bid_price(100000);
    EXPECT_EQ(orders.size(), 3);
    EXPECT_EQ(orders[0].id, 1);
    EXPECT_EQ(orders[1].id, 2);
    EXPECT_EQ(orders[2].id, 3);
}

TEST(OrderBookTestV1Pool, LargeNumberOfOrders) {
    OrderBookV1Pool book;
    const size_t num_orders = 10000;

    for (size_t i = 0; i < num_orders; ++i) {
        book.add_limit_order(Order(i + 1, Side::Buy, 100000 + (i % 1000), 100, i + 1));
    }

    EXPECT_EQ(book.buy_order_count(), num_orders);
}

TEST(OrderBookTestV1Pool, PoolStatistics) {
    OrderBookV1Pool book;

    // Add some orders
    for (int i = 0; i < 100; ++i) {
        book.add_limit_order(Order(i + 1, Side::Buy, 100000, 100, i + 1));
    }

    // Cancel some orders to test pool recycling
    book.cancel_order(1);
    book.cancel_order(2);
    book.cancel_order(3);

    auto stats = book.pool_stats();
    EXPECT_GT(stats.allocated_count, 0);
    EXPECT_GT(stats.freed_count, 0);
    EXPECT_GT(stats.pool_hits, 0);
}

TEST(OrderBookTestV1Pool, IdenticalToBaseline) {
    // Run same operations on both V1 and V1Pool and verify identical results
    OrderBookV1 baseline;
    OrderBookV1Pool pool;

    // Add orders
    for (int i = 0; i < 100; ++i) {
        Order order(i + 1, Side::Buy, 100000 + (i % 1000), 100, i + 1);
        baseline.add_limit_order(order);
        pool.add_limit_order(order);
    }

    // Verify identical state
    EXPECT_EQ(baseline.buy_order_count(), pool.buy_order_count());
    EXPECT_EQ(baseline.buy_price_level_count(), pool.buy_price_level_count());
    EXPECT_EQ(baseline.best_bid(), pool.best_bid());

    // Cancel some orders
    for (int i = 0; i < 10; ++i) {
        baseline.cancel_order(i + 1);
        pool.cancel_order(i + 1);
    }

    // Verify identical state after cancellation
    EXPECT_EQ(baseline.buy_order_count(), pool.buy_order_count());
    EXPECT_EQ(baseline.buy_price_level_count(), pool.buy_price_level_count());
}
