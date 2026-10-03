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

TEST(OrderBookTestV1Pool, MultiplePriceLevelsWithCorrectOrdering) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 99500, 100, 10));
    book.add_limit_order(Order(2, Side::Buy, 100000, 100, 11));
    book.add_limit_order(Order(3, Side::Buy, 99000, 100, 12));
    book.add_limit_order(Order(4, Side::Buy, 100500, 100, 13));

    EXPECT_EQ(book.best_bid(), 100500);

    auto all_orders = book.get_all_buy_orders();
    EXPECT_EQ(all_orders.size(), 4);
}

TEST(OrderBookTestV1Pool, CorrectQuantityStateOfRestingOrders) {
    OrderBookV1Pool book;

    Order order(1, Side::Buy, 100000, 1000, 10);
    book.add_limit_order(order);

    auto orders = book.get_orders_at_bid_price(100000);
    ASSERT_EQ(orders.size(), 1);

    EXPECT_EQ(orders[0].id, 1);
    EXPECT_EQ(orders[0].side, Side::Buy);
    EXPECT_EQ(orders[0].price, 100000);
    EXPECT_EQ(orders[0].quantity, 1000);
    EXPECT_EQ(orders[0].filled, 0);
    EXPECT_EQ(orders[0].sequence, 10);
    EXPECT_EQ(orders[0].remaining(), 1000);
}

TEST(OrderBookTestV1Pool, BookStateAfterSeveralIndependentAdditions) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100000, 1000, 10));
    book.add_limit_order(Order(2, Side::Sell, 100500, 500, 11));
    book.add_limit_order(Order(3, Side::Buy, 99500, 2000, 12));
    book.add_limit_order(Order(4, Side::Sell, 101000, 750, 13));
    book.add_limit_order(Order(5, Side::Buy, 100000, 500, 14));

    EXPECT_EQ(book.buy_order_count(), 3);
    EXPECT_EQ(book.buy_price_level_count(), 2);
    EXPECT_EQ(book.best_bid(), 100000);

    EXPECT_EQ(book.sell_order_count(), 2);
    EXPECT_EQ(book.sell_price_level_count(), 2);
    EXPECT_EQ(book.best_ask(), 100500);

    auto bid_orders = book.get_orders_at_bid_price(100000);
    EXPECT_EQ(bid_orders.size(), 2);

    auto ask_orders = book.get_orders_at_ask_price(100500);
    EXPECT_EQ(ask_orders.size(), 1);
}

TEST(OrderBookTestV1Pool, GetOrdersAtNonExistentPrice) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100000, 1000, 10));

    auto orders = book.get_orders_at_bid_price(99500);
    EXPECT_TRUE(orders.empty());

    orders = book.get_orders_at_ask_price(100000);
    EXPECT_TRUE(orders.empty());
}

TEST(OrderBookTestV1Pool, OrderPreservationAfterAddition) {
    OrderBookV1Pool book;

    Order original_order(1, Side::Buy, 100000, 1000, 10);
    book.add_limit_order(original_order);

    EXPECT_EQ(original_order.id, 1);
    EXPECT_EQ(original_order.side, Side::Buy);
    EXPECT_EQ(original_order.price, 100000);
    EXPECT_EQ(original_order.quantity, 1000);
    EXPECT_EQ(original_order.filled, 0);
    EXPECT_EQ(original_order.sequence, 10);

    auto orders = book.get_orders_at_bid_price(100000);
    ASSERT_EQ(orders.size(), 1);
    EXPECT_EQ(orders[0].id, 1);
    EXPECT_EQ(orders[0].side, Side::Buy);
    EXPECT_EQ(orders[0].price, 100000);
    EXPECT_EQ(orders[0].quantity, 1000);
    EXPECT_EQ(orders[0].filled, 0);
    EXPECT_EQ(orders[0].sequence, 10);
}

TEST(OrderBookTestV1Pool, BothSidesNonEmpty) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100000, 1000, 10));
    book.add_limit_order(Order(2, Side::Sell, 100500, 500, 11));

    EXPECT_FALSE(book.buy_side_empty());
    EXPECT_FALSE(book.sell_side_empty());
    EXPECT_FALSE(book.empty());
}

TEST(OrderBookTestV1Pool, GetAllBuyOrders) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 100000, 1000, 10));
    book.add_limit_order(Order(2, Side::Buy, 99500, 500, 11));
    book.add_limit_order(Order(3, Side::Buy, 100000, 750, 12));

    auto all_orders = book.get_all_buy_orders();
    EXPECT_EQ(all_orders.size(), 3);
}

TEST(OrderBookTestV1Pool, GetAllSellOrders) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 100000, 1000, 10));
    book.add_limit_order(Order(2, Side::Sell, 100500, 500, 11));
    book.add_limit_order(Order(3, Side::Sell, 100000, 750, 12));

    auto all_orders = book.get_all_sell_orders();
    EXPECT_EQ(all_orders.size(), 3);
}

TEST(OrderBookTestV1Pool, PriceLevelOrderingBuySide) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Buy, 99000, 100, 10));
    book.add_limit_order(Order(2, Side::Buy, 100000, 100, 11));
    book.add_limit_order(Order(3, Side::Buy, 99500, 100, 12));
    book.add_limit_order(Order(4, Side::Buy, 100500, 100, 13));
    book.add_limit_order(Order(5, Side::Buy, 100000, 100, 14));

    EXPECT_EQ(book.best_bid(), 100500);
    EXPECT_EQ(book.buy_price_level_count(), 4);
}

TEST(OrderBookTestV1Pool, PriceLevelOrderingSellSide) {
    OrderBookV1Pool book;

    book.add_limit_order(Order(1, Side::Sell, 99000, 100, 10));
    book.add_limit_order(Order(2, Side::Sell, 100000, 100, 11));
    book.add_limit_order(Order(3, Side::Sell, 99500, 100, 12));
    book.add_limit_order(Order(4, Side::Sell, 100500, 100, 13));
    book.add_limit_order(Order(5, Side::Sell, 100000, 100, 14));

    EXPECT_EQ(book.best_ask(), 99000);
    EXPECT_EQ(book.sell_price_level_count(), 4);
}

TEST(OrderBookTestV1Pool, SequenceNumberPreservation) {
    OrderBookV1Pool book;

    Order order1(1, Side::Buy, 100000, 1000, 999999);
    Order order2(2, Side::Buy, 100000, 500, 1000000);

    book.add_limit_order(order1);
    book.add_limit_order(order2);

    auto orders = book.get_orders_at_bid_price(100000);
    ASSERT_EQ(orders.size(), 2);

    EXPECT_EQ(orders[0].sequence, 999999);
    EXPECT_EQ(orders[1].sequence, 1000000);
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
