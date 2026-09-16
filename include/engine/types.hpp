#pragma once

#include <cstdint>
#include <stdexcept>

namespace engine {

// Core type aliases
using OrderId = uint64_t;
using Price = int64_t;
using Qty = uint64_t;

// Order side
enum class Side {
    Buy,
    Sell
};

// Order type
enum class OrderType {
    Limit,
    Market
};

// Order structure
struct Order {
    OrderId id;
    Side side;
    Price price;
    Qty quantity;
    Qty filled;
    uint64_t sequence;

    // Constructor for limit orders
    Order(OrderId id_, Side side_, Price price_, Qty quantity_, uint64_t sequence_)
        : id(id_)
        , side(side_)
        , price(price_)
        , quantity(quantity_)
        , filled(0)
        , sequence(sequence_)
    {
        validate();
    }

    // Constructor for market orders (price is irrelevant)
    Order(OrderId id_, Side side_, Qty quantity_, uint64_t sequence_)
        : id(id_)
        , side(side_)
        , price(0)
        , quantity(quantity_)
        , filled(0)
        , sequence(sequence_)
    {
        validate();
    }

    // Default constructor
    Order()
        : id(0)
        , side(Side::Buy)
        , price(0)
        , quantity(0)
        , filled(0)
        , sequence(0)
    {}

    // Get remaining quantity
    Qty remaining() const {
        return quantity - filled;
    }

    // Check if order is fully filled
    bool is_fully_filled() const {
        return filled == quantity;
    }

    // Check if order is partially filled
    bool is_partially_filled() const {
        return filled > 0 && filled < quantity;
    }

    // Validate order invariants
    void validate() const {
        if (quantity == 0) {
            throw std::invalid_argument("Quantity must be positive");
        }
        if (filled > quantity) {
            throw std::invalid_argument("Filled quantity cannot exceed total quantity");
        }
    }
};

// Trade event
struct Trade {
    OrderId buy_order_id;
    OrderId sell_order_id;
    Price execution_price;
    Qty execution_quantity;
    uint64_t sequence;

    Trade(OrderId buy_id, OrderId sell_id, Price price, Qty qty, uint64_t seq)
        : buy_order_id(buy_id)
        , sell_order_id(sell_id)
        , execution_price(price)
        , execution_quantity(qty)
        , sequence(seq)
    {
        if (execution_quantity == 0) {
            throw std::invalid_argument("Execution quantity must be positive");
        }
        if (execution_price < 0) {
            throw std::invalid_argument("Execution price cannot be negative");
        }
    }

    // Default constructor
    Trade()
        : buy_order_id(0)
        , sell_order_id(0)
        , execution_price(0)
        , execution_quantity(0)
        , sequence(0)
    {}
};

// Market data event types
enum class MarketDataEventType {
    OrderAdded,
    OrderCancelled,
    OrderPartiallyFilled,
    OrderFullyFilled
};

// Market data event
struct MarketDataEvent {
    OrderId order_id;
    Side side;
    Price price;
    Qty quantity;
    Qty filled;
    MarketDataEventType event_type;
    uint64_t sequence;

    MarketDataEvent(OrderId id, Side s, Price p, Qty qty, Qty f, MarketDataEventType type, uint64_t seq)
        : order_id(id)
        , side(s)
        , price(p)
        , quantity(qty)
        , filled(f)
        , event_type(type)
        , sequence(seq)
    {
        if (quantity == 0) {
            throw std::invalid_argument("Quantity must be positive");
        }
        if (filled > quantity) {
            throw std::invalid_argument("Filled quantity cannot exceed total quantity");
        }
    }

    // Default constructor
    MarketDataEvent()
        : order_id(0)
        , side(Side::Buy)
        , price(0)
        , quantity(0)
        , filled(0)
        , event_type(MarketDataEventType::OrderAdded)
        , sequence(0)
    {}
};

} // namespace engine
