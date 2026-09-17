#include <gtest/gtest.h>
#include <engine/orderbook.hpp>
#include <engine/types.hpp>
#include <set>

using namespace engine;

TEST(OrderBookTest, EmptyBuyBook) {
    OrderBook book;
    EXPECT_TRUE(book.buy_side_empty());
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.buy_order_count(), 0);
    EXPECT_EQ(book.buy_price_level_count(), 0);
}

TEST(OrderBookTest, EmptySellBook) {
    OrderBook book;
    EXPECT_TRUE(book.sell_side_empty());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.sell_order_count(), 0);
    EXPECT_EQ(book.sell_price_level_count(), 0);
}

TEST(OrderBookTest, EmptyBook) {
    OrderBook book;
    EXPECT_TRUE(book.empty());
    EXPECT_TRUE(book.buy_side_empty());
    EXPECT_TRUE(book.sell_side_empty());
}

TEST(OrderBookTest, AddOneBuyOrder) {
    OrderBook book;
    Order order(1, Side::Buy, 100, 1000, 10);
    
    book.add_limit_order(order);
    
    EXPECT_FALSE(book.buy_side_empty());
    EXPECT_TRUE(book.sell_side_empty());
    EXPECT_FALSE(book.empty());
    EXPECT_EQ(book.buy_order_count(), 1);
    EXPECT_EQ(book.buy_price_level_count(), 1);
    
    auto best_bid = book.best_bid();
    ASSERT_TRUE(best_bid.has_value());
    EXPECT_EQ(best_bid.value(), 100);
}

TEST(OrderBookTest, AddOneSellOrder) {
    OrderBook book;
    Order order(1, Side::Sell, 100, 1000, 10);
    
    book.add_limit_order(order);
    
    EXPECT_TRUE(book.buy_side_empty());
    EXPECT_FALSE(book.sell_side_empty());
    EXPECT_FALSE(book.empty());
    EXPECT_EQ(book.sell_order_count(), 1);
    EXPECT_EQ(book.sell_price_level_count(), 1);
    
    auto best_ask = book.best_ask();
    ASSERT_TRUE(best_ask.has_value());
    EXPECT_EQ(best_ask.value(), 100);
}

TEST(OrderBookTest, AddMultipleBuyPriceLevels) {
    OrderBook book;
    
    book.add_limit_order(Order(1, Side::Buy, 100, 1000, 10));
    book.add_limit_order(Order(2, Side::Buy, 95, 500, 11));
    book.add_limit_order(Order(3, Side::Buy, 105, 2000, 12));
    
    EXPECT_EQ(book.buy_order_count(), 3);
    EXPECT_EQ(book.buy_price_level_count(), 3);
    
    auto best_bid = book.best_bid();
    ASSERT_TRUE(best_bid.has_value());
    EXPECT_EQ(best_bid.value(), 105); // Highest price
}

TEST(OrderBookTest, AddMultipleSellPriceLevels) {
    OrderBook book;
    
    book.add_limit_order(Order(1, Side::Sell, 100, 1000, 10));
    book.add_limit_order(Order(2, Side::Sell, 105, 500, 11));
    book.add_limit_order(Order(3, Side::Sell, 95, 2000, 12));
    
    EXPECT_EQ(book.sell_order_count(), 3);
    EXPECT_EQ(book.sell_price_level_count(), 3);
    
    auto best_ask = book.best_ask();
    ASSERT_TRUE(best_ask.has_value());
    EXPECT_EQ(best_ask.value(), 95); // Lowest price
}

TEST(OrderBookTest, CorrectBestBid) {
    OrderBook book;
    
    book.add_limit_order(Order(1, Side::Buy, 90, 100, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 100, 11));
    book.add_limit_order(Order(3, Side::Buy, 95, 100, 12));
    book.add_limit_order(Order(4, Side::Buy, 110, 100, 13));
    
    auto best_bid = book.best_bid();
    ASSERT_TRUE(best_bid.has_value());
    EXPECT_EQ(best_bid.value(), 110);
}

TEST(OrderBookTest, CorrectBestAsk) {
    OrderBook book;
    
    book.add_limit_order(Order(1, Side::Sell, 90, 100, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 100, 11));
    book.add_limit_order(Order(3, Side::Sell, 95, 100, 12));
    book.add_limit_order(Order(4, Side::Sell, 110, 100, 13));
    
    auto best_ask = book.best_ask();
    ASSERT_TRUE(best_ask.has_value());
    EXPECT_EQ(best_ask.value(), 90);
}

TEST(OrderBookTest, MultipleOrdersAtSamePrice) {
    OrderBook book;
    
    book.add_limit_order(Order(1, Side::Buy, 100, 1000, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 500, 11));
    book.add_limit_order(Order(3, Side::Buy, 100, 750, 12));
    
    EXPECT_EQ(book.buy_order_count(), 3);
    EXPECT_EQ(book.buy_price_level_count(), 1);
    
    auto orders = book.get_orders_at_bid_price(100);
    EXPECT_EQ(orders.size(), 3);
}

TEST(OrderBookTest, FIFOOrderingAtSamePrice) {
    OrderBook book;
    
    Order order1(1, Side::Buy, 100, 1000, 10);
    Order order2(2, Side::Buy, 100, 500, 11);
    Order order3(3, Side::Buy, 100, 750, 12);
    
    book.add_limit_order(order1);
    book.add_limit_order(order2);
    book.add_limit_order(order3);
    
    auto orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(orders.size(), 3);
    
    // Verify FIFO ordering by sequence number
    EXPECT_EQ(orders[0].id, 1);
    EXPECT_EQ(orders[0].sequence, 10);
    EXPECT_EQ(orders[1].id, 2);
    EXPECT_EQ(orders[1].sequence, 11);
    EXPECT_EQ(orders[2].id, 3);
    EXPECT_EQ(orders[2].sequence, 12);
}

TEST(OrderBookTest, MultiplePriceLevelsWithCorrectOrdering) {
    OrderBook book;
    
    // Add orders at different prices
    book.add_limit_order(Order(1, Side::Buy, 95, 100, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 100, 11));
    book.add_limit_order(Order(3, Side::Buy, 90, 100, 12));
    book.add_limit_order(Order(4, Side::Buy, 105, 100, 13));
    
    // Verify best bid is highest price
    auto best_bid = book.best_bid();
    ASSERT_TRUE(best_bid.has_value());
    EXPECT_EQ(best_bid.value(), 105);
    
    // Verify all orders are present
    auto all_orders = book.get_all_buy_orders();
    EXPECT_EQ(all_orders.size(), 4);
}

TEST(OrderBookTest, CorrectQuantityStateOfRestingOrders) {
    OrderBook book;
    
    Order order(1, Side::Buy, 100, 1000, 10);
    book.add_limit_order(order);
    
    auto orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(orders.size(), 1);
    
    // Verify order fields are preserved
    EXPECT_EQ(orders[0].id, 1);
    EXPECT_EQ(orders[0].side, Side::Buy);
    EXPECT_EQ(orders[0].price, 100);
    EXPECT_EQ(orders[0].quantity, 1000);
    EXPECT_EQ(orders[0].filled, 0);
    EXPECT_EQ(orders[0].sequence, 10);
    EXPECT_EQ(orders[0].remaining(), 1000);
}

TEST(OrderBookTest, BookStateAfterSeveralIndependentAdditions) {
    OrderBook book;
    
    // Add multiple orders independently
    book.add_limit_order(Order(1, Side::Buy, 100, 1000, 10));
    book.add_limit_order(Order(2, Side::Sell, 105, 500, 11));
    book.add_limit_order(Order(3, Side::Buy, 95, 2000, 12));
    book.add_limit_order(Order(4, Side::Sell, 110, 750, 13));
    book.add_limit_order(Order(5, Side::Buy, 100, 500, 14));
    
    // Verify buy side
    EXPECT_EQ(book.buy_order_count(), 3);
    EXPECT_EQ(book.buy_price_level_count(), 2);
    auto best_bid = book.best_bid();
    ASSERT_TRUE(best_bid.has_value());
    EXPECT_EQ(best_bid.value(), 100);
    
    // Verify sell side
    EXPECT_EQ(book.sell_order_count(), 2);
    EXPECT_EQ(book.sell_price_level_count(), 2);
    auto best_ask = book.best_ask();
    ASSERT_TRUE(best_ask.has_value());
    EXPECT_EQ(best_ask.value(), 105);
    
    // Verify orders at specific price levels
    auto bid_orders = book.get_orders_at_bid_price(100);
    EXPECT_EQ(bid_orders.size(), 2);
    
    auto ask_orders = book.get_orders_at_ask_price(105);
    EXPECT_EQ(ask_orders.size(), 1);
}

TEST(OrderBookTest, LargeNumberOfOrders) {
    OrderBook book;
    
    const size_t num_orders = 1000;
    
    // Add many buy orders at different prices
    for (size_t i = 0; i < num_orders; ++i) {
        Price price = 100 + (i % 10); // 10 different price levels
        book.add_limit_order(Order(i + 1, Side::Buy, price, 100, i));
    }
    
    EXPECT_EQ(book.buy_order_count(), num_orders);
    EXPECT_EQ(book.buy_price_level_count(), 10);
    
    // Add many sell orders
    for (size_t i = 0; i < num_orders; ++i) {
        Price price = 105 + (i % 10);
        book.add_limit_order(Order(num_orders + i + 1, Side::Sell, price, 100, num_orders + i));
    }
    
    EXPECT_EQ(book.sell_order_count(), num_orders);
    EXPECT_EQ(book.sell_price_level_count(), 10);
}

TEST(OrderBookTest, GetOrdersAtNonExistentPrice) {
    OrderBook book;
    
    book.add_limit_order(Order(1, Side::Buy, 100, 1000, 10));
    
    auto orders = book.get_orders_at_bid_price(95);
    EXPECT_TRUE(orders.empty());
    
    orders = book.get_orders_at_ask_price(100);
    EXPECT_TRUE(orders.empty());
}

TEST(OrderBookTest, OrderPreservationAfterAddition) {
    OrderBook book;
    
    Order original_order(1, Side::Buy, 100, 1000, 10);
    book.add_limit_order(original_order);
    
    // Verify original order is not modified
    EXPECT_EQ(original_order.id, 1);
    EXPECT_EQ(original_order.side, Side::Buy);
    EXPECT_EQ(original_order.price, 100);
    EXPECT_EQ(original_order.quantity, 1000);
    EXPECT_EQ(original_order.filled, 0);
    EXPECT_EQ(original_order.sequence, 10);
    
    // Verify book has correct copy
    auto orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(orders.size(), 1);
    EXPECT_EQ(orders[0].id, 1);
    EXPECT_EQ(orders[0].side, Side::Buy);
    EXPECT_EQ(orders[0].price, 100);
    EXPECT_EQ(orders[0].quantity, 1000);
    EXPECT_EQ(orders[0].filled, 0);
    EXPECT_EQ(orders[0].sequence, 10);
}

TEST(OrderBookTest, BothSidesNonEmpty) {
    OrderBook book;
    
    book.add_limit_order(Order(1, Side::Buy, 100, 1000, 10));
    book.add_limit_order(Order(2, Side::Sell, 105, 500, 11));
    
    EXPECT_FALSE(book.buy_side_empty());
    EXPECT_FALSE(book.sell_side_empty());
    EXPECT_FALSE(book.empty());
    
    EXPECT_TRUE(book.best_bid().has_value());
    EXPECT_TRUE(book.best_ask().has_value());
}

TEST(OrderBookTest, GetAllBuyOrders) {
    OrderBook book;
    
    book.add_limit_order(Order(1, Side::Buy, 100, 1000, 10));
    book.add_limit_order(Order(2, Side::Buy, 95, 500, 11));
    book.add_limit_order(Order(3, Side::Buy, 100, 750, 12));
    
    auto all_orders = book.get_all_buy_orders();
    EXPECT_EQ(all_orders.size(), 3);
    
    // Verify all orders are present
    std::set<OrderId> ids;
    for (const auto& order : all_orders) {
        ids.insert(order.id);
    }
    EXPECT_EQ(ids.size(), 3);
    EXPECT_TRUE(ids.contains(1));
    EXPECT_TRUE(ids.contains(2));
    EXPECT_TRUE(ids.contains(3));
}

TEST(OrderBookTest, GetAllSellOrders) {
    OrderBook book;
    
    book.add_limit_order(Order(1, Side::Sell, 100, 1000, 10));
    book.add_limit_order(Order(2, Side::Sell, 105, 500, 11));
    book.add_limit_order(Order(3, Side::Sell, 100, 750, 12));
    
    auto all_orders = book.get_all_sell_orders();
    EXPECT_EQ(all_orders.size(), 3);
    
    // Verify all orders are present
    std::set<OrderId> ids;
    for (const auto& order : all_orders) {
        ids.insert(order.id);
    }
    EXPECT_EQ(ids.size(), 3);
    EXPECT_TRUE(ids.contains(1));
    EXPECT_TRUE(ids.contains(2));
    EXPECT_TRUE(ids.contains(3));
}

TEST(OrderBookTest, PriceLevelOrderingBuySide) {
    OrderBook book;
    
    book.add_limit_order(Order(1, Side::Buy, 90, 100, 10));
    book.add_limit_order(Order(2, Side::Buy, 100, 100, 11));
    book.add_limit_order(Order(3, Side::Buy, 95, 100, 12));
    book.add_limit_order(Order(4, Side::Buy, 110, 100, 13));
    book.add_limit_order(Order(5, Side::Buy, 105, 100, 14));
    
    // Best bid should be highest price
    auto best_bid = book.best_bid();
    ASSERT_TRUE(best_bid.has_value());
    EXPECT_EQ(best_bid.value(), 110);
    
    // Should have 5 price levels
    EXPECT_EQ(book.buy_price_level_count(), 5);
}

TEST(OrderBookTest, PriceLevelOrderingSellSide) {
    OrderBook book;
    
    book.add_limit_order(Order(1, Side::Sell, 90, 100, 10));
    book.add_limit_order(Order(2, Side::Sell, 100, 100, 11));
    book.add_limit_order(Order(3, Side::Sell, 95, 100, 12));
    book.add_limit_order(Order(4, Side::Sell, 110, 100, 13));
    book.add_limit_order(Order(5, Side::Sell, 105, 100, 14));
    
    // Best ask should be lowest price
    auto best_ask = book.best_ask();
    ASSERT_TRUE(best_ask.has_value());
    EXPECT_EQ(best_ask.value(), 90);
    
    // Should have 5 price levels
    EXPECT_EQ(book.sell_price_level_count(), 5);
}

TEST(OrderBookTest, SequenceNumberPreservation) {
    OrderBook book;
    
    Order order1(1, Side::Buy, 100, 1000, 999999);
    Order order2(2, Side::Buy, 100, 500, 1000000);
    
    book.add_limit_order(order1);
    book.add_limit_order(order2);
    
    auto orders = book.get_orders_at_bid_price(100);
    ASSERT_EQ(orders.size(), 2);
    
    EXPECT_EQ(orders[0].sequence, 999999);
    EXPECT_EQ(orders[1].sequence, 1000000);
}
