#include <gtest/gtest.h>
#include <engine/types.hpp>

using namespace engine;

TEST(DomainTest, OrderConstructionValid) {
    Order order(1, Side::Buy, 100, 1000, 42);
    EXPECT_EQ(order.id, 1);
    EXPECT_EQ(order.side, Side::Buy);
    EXPECT_EQ(order.price, 100);
    EXPECT_EQ(order.quantity, 1000);
    EXPECT_EQ(order.filled, 0);
    EXPECT_EQ(order.sequence, 42);
}

TEST(DomainTest, OrderConstructionMarketOrder) {
    Order order(1, Side::Sell, 1000, 42);
    EXPECT_EQ(order.id, 1);
    EXPECT_EQ(order.side, Side::Sell);
    EXPECT_EQ(order.quantity, 1000);
    EXPECT_EQ(order.filled, 0);
    EXPECT_EQ(order.sequence, 42);
}

TEST(DomainTest, OrderDefaultConstruction) {
    Order order;
    EXPECT_EQ(order.id, 0);
    EXPECT_EQ(order.side, Side::Buy);
    EXPECT_EQ(order.price, 0);
    EXPECT_EQ(order.quantity, 0);
    EXPECT_EQ(order.filled, 0);
    EXPECT_EQ(order.sequence, 0);
}

TEST(DomainTest, OrderBuySide) {
    Order buy_order(1, Side::Buy, 100, 1000, 1);
    EXPECT_EQ(buy_order.side, Side::Buy);
}

TEST(DomainTest, OrderSellSide) {
    Order sell_order(2, Side::Sell, 100, 1000, 2);
    EXPECT_EQ(sell_order.side, Side::Sell);
}

TEST(DomainTest, OrderRemainingQuantity) {
    Order order(1, Side::Buy, 100, 1000, 1);
    EXPECT_EQ(order.remaining(), 1000);
    
    order.filled = 300;
    EXPECT_EQ(order.remaining(), 700);
}

TEST(DomainTest, OrderIsFullyFilled) {
    Order order(1, Side::Buy, 100, 1000, 1);
    EXPECT_FALSE(order.is_fully_filled());
    
    order.filled = 1000;
    EXPECT_TRUE(order.is_fully_filled());
}

TEST(DomainTest, OrderIsPartiallyFilled) {
    Order order(1, Side::Buy, 100, 1000, 1);
    EXPECT_FALSE(order.is_partially_filled());
    
    order.filled = 500;
    EXPECT_TRUE(order.is_partially_filled());
    
    order.filled = 1000;
    EXPECT_FALSE(order.is_partially_filled());
}

TEST(DomainTest, OrderValidationZeroQuantity) {
    EXPECT_THROW(Order(1, Side::Buy, 100, 0, 1), std::invalid_argument);
}

TEST(DomainTest, OrderValidationFilledExceedsQuantity) {
    Order order(1, Side::Buy, 100, 1000, 1);
    order.filled = 1500;
    EXPECT_THROW(order.validate(), std::invalid_argument);
}

TEST(DomainTest, OrderValidationFilledEqualsQuantity) {
    Order order(1, Side::Buy, 100, 1000, 1);
    order.filled = 1000;
    EXPECT_NO_THROW(order.validate());
}

TEST(DomainTest, TradeConstructionValid) {
    Trade trade(1, 2, 100, 500, 42);
    EXPECT_EQ(trade.buy_order_id, 1);
    EXPECT_EQ(trade.sell_order_id, 2);
    EXPECT_EQ(trade.execution_price, 100);
    EXPECT_EQ(trade.execution_quantity, 500);
    EXPECT_EQ(trade.sequence, 42);
}

TEST(DomainTest, TradeDefaultConstruction) {
    Trade trade;
    EXPECT_EQ(trade.buy_order_id, 0);
    EXPECT_EQ(trade.sell_order_id, 0);
    EXPECT_EQ(trade.execution_price, 0);
    EXPECT_EQ(trade.execution_quantity, 0);
    EXPECT_EQ(trade.sequence, 0);
}

TEST(DomainTest, TradeValidationZeroQuantity) {
    EXPECT_THROW(Trade(1, 2, 100, 0, 42), std::invalid_argument);
}

TEST(DomainTest, TradeValidationNegativePrice) {
    EXPECT_THROW(Trade(1, 2, -1, 500, 42), std::invalid_argument);
}

TEST(DomainTest, MarketDataEventConstructionOrderAdded) {
    MarketDataEvent event(1, Side::Buy, 100, 1000, 0, MarketDataEventType::OrderAdded, 42);
    EXPECT_EQ(event.order_id, 1);
    EXPECT_EQ(event.side, Side::Buy);
    EXPECT_EQ(event.price, 100);
    EXPECT_EQ(event.quantity, 1000);
    EXPECT_EQ(event.filled, 0);
    EXPECT_EQ(event.event_type, MarketDataEventType::OrderAdded);
    EXPECT_EQ(event.sequence, 42);
}

TEST(DomainTest, MarketDataEventConstructionOrderCancelled) {
    MarketDataEvent event(1, Side::Sell, 100, 1000, 0, MarketDataEventType::OrderCancelled, 43);
    EXPECT_EQ(event.event_type, MarketDataEventType::OrderCancelled);
}

TEST(DomainTest, MarketDataEventConstructionOrderPartiallyFilled) {
    MarketDataEvent event(1, Side::Buy, 100, 1000, 300, MarketDataEventType::OrderPartiallyFilled, 44);
    EXPECT_EQ(event.event_type, MarketDataEventType::OrderPartiallyFilled);
    EXPECT_EQ(event.filled, 300);
}

TEST(DomainTest, MarketDataEventConstructionOrderFullyFilled) {
    MarketDataEvent event(1, Side::Sell, 100, 1000, 1000, MarketDataEventType::OrderFullyFilled, 45);
    EXPECT_EQ(event.event_type, MarketDataEventType::OrderFullyFilled);
    EXPECT_EQ(event.filled, 1000);
}

TEST(DomainTest, MarketDataEventDefaultConstruction) {
    MarketDataEvent event;
    EXPECT_EQ(event.order_id, 0);
    EXPECT_EQ(event.side, Side::Buy);
    EXPECT_EQ(event.price, 0);
    EXPECT_EQ(event.quantity, 0);
    EXPECT_EQ(event.filled, 0);
    EXPECT_EQ(event.event_type, MarketDataEventType::OrderAdded);
    EXPECT_EQ(event.sequence, 0);
}

TEST(DomainTest, MarketDataEventValidationZeroQuantity) {
    EXPECT_THROW(MarketDataEvent(1, Side::Buy, 100, 0, 0, MarketDataEventType::OrderAdded, 42), std::invalid_argument);
}

TEST(DomainTest, MarketDataEventValidationFilledExceedsQuantity) {
    EXPECT_THROW(MarketDataEvent(1, Side::Buy, 100, 1000, 1500, MarketDataEventType::OrderAdded, 42), std::invalid_argument);
}

TEST(DomainTest, TypeAliases) {
    OrderId id = 12345;
    Price price = 10050;
    Qty quantity = 1000000;
    
    EXPECT_EQ(id, 12345);
    EXPECT_EQ(price, 10050);
    EXPECT_EQ(quantity, 1000000);
}
