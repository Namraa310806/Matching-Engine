#pragma once

#include <map>
#include <deque>
#include <optional>
#include <vector>
#include <engine/types.hpp>

namespace engine {

// Limit order book implementation (v0 - correctness baseline)
// Uses std::map<Price, std::deque<Order>> for each side
// - Buy side: descending order (highest price first = best bid)
// - Sell side: ascending order (lowest price first = best ask)
class OrderBook {
public:
    // Add a limit order to the book
    // Orders are stored without matching logic in this phase
    void add_limit_order(const Order& order);

    // Submit an order to the matching engine
    // Returns a vector of Trade events generated from matching
    // Returns a vector of MarketDataEvent for order lifecycle events
    // For limit orders: attempts to match, then rests remaining quantity
    // For market orders: attempts to match, never rests
    std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> submit_order(const Order& order);

    // Get the best bid (highest buy price)
    // Returns std::nullopt if buy side is empty
    std::optional<Price> best_bid() const;

    // Get the best ask (lowest sell price)
    // Returns std::nullopt if sell side is empty
    std::optional<Price> best_ask() const;

    // Check if the buy side is empty
    bool buy_side_empty() const;

    // Check if the sell side is empty
    bool sell_side_empty() const;

    // Check if both sides are empty
    bool empty() const;

    // Get all orders at a specific price level on the buy side
    // Returns empty vector if price level doesn't exist
    std::vector<Order> get_orders_at_bid_price(Price price) const;

    // Get all orders at a specific price level on the sell side
    // Returns empty vector if price level doesn't exist
    std::vector<Order> get_orders_at_ask_price(Price price) const;

    // Get total number of buy orders in the book
    size_t buy_order_count() const;

    // Get total number of sell orders in the book
    size_t sell_order_count() const;

    // Get number of price levels on buy side
    size_t buy_price_level_count() const;

    // Get number of price levels on sell side
    size_t sell_price_level_count() const;

    // Get all buy orders (for testing/inspection)
    // Returns vector of orders grouped by price level
    std::vector<Order> get_all_buy_orders() const;

    // Get all sell orders (for testing/inspection)
    // Returns vector of orders grouped by price level
    std::vector<Order> get_all_sell_orders() const;

private:
    // Match a buy order against the sell side
    // Returns trades and market data events
    std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> match_buy_order(Order& order);

    // Match a sell order against the buy side
    // Returns trades and market data events
    std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> match_sell_order(Order& order);

    // Add a limit order to the appropriate side (resting order)
    void add_resting_order(const Order& order);

    // Generate market data event for order lifecycle
    MarketDataEvent create_order_event(const Order& order, MarketDataEventType type) const;

    // Buy side: descending order (std::greater for highest price first)
    // Best bid is at begin()
    std::map<Price, std::deque<Order>, std::greater<Price>> bids_;

    // Sell side: ascending order (std::less for lowest price first)
    // Best ask is at begin()
    std::map<Price, std::deque<Order>, std::less<Price>> asks_;

    // Sequence number for ordering events
    uint64_t sequence_ = 0;
};

} // namespace engine
