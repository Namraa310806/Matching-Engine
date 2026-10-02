#pragma once

#include <vector>
#include <optional>
#include <unordered_map>
#include <algorithm>
#include <engine/types.hpp>
#include <engine/object_pool.hpp>
#include <engine/orderbook_v1.hpp>

namespace engine {

// V1 with object pool for order node allocation
// Uses ObjectPool instead of direct new/delete for OrderNode allocation
// Preserves all semantics of OrderBookV1
// Reuses OrderNode and PriceLevel definitions from orderbook_v1.hpp

class OrderBookV1Pool {
public:
    OrderBookV1Pool();
    ~OrderBookV1Pool();

    // Add a limit order to the book
    void add_limit_order(const Order& order);

    // Submit an order to the matching engine
    std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> submit_order(const Order& order);

    // Cancel a resting order by OrderId
    std::pair<bool, std::vector<MarketDataEvent>> cancel_order(OrderId order_id);

    // Get the best bid (highest buy price)
    std::optional<Price> best_bid() const;

    // Get the best ask (lowest sell price)
    std::optional<Price> best_ask() const;

    // Check if the buy side is empty
    bool buy_side_empty() const;

    // Check if the sell side is empty
    bool sell_side_empty() const;

    // Check if both sides are empty
    bool empty() const;

    // Get all orders at a specific price level on the buy side
    std::vector<Order> get_orders_at_bid_price(Price price) const;

    // Get all orders at a specific price level on the sell side
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
    std::vector<Order> get_all_buy_orders() const;

    // Get all sell orders (for testing/inspection)
    std::vector<Order> get_all_sell_orders() const;

    // Get pool statistics for benchmarking
    ObjectPool<OrderNode>::Stats pool_stats() const { return pool_.stats(); }

#ifdef TESTING
    // Test-only: Get the size of the order index
    size_t order_index_size() const { return order_index_.size(); }

    // Test-only: Check if an OrderId exists in the index
    bool order_id_in_index(OrderId id) const { return order_index_.find(id) != order_index_.end(); }
#endif

private:
    // Find or create price level (sorted insertion)
    PriceLevel* find_or_create_price_level(Side side, Price price);

    // Find price level (binary search)
    PriceLevel* find_price_level(Side side, Price price);
    const PriceLevel* find_price_level(Side side, Price price) const;

    // Remove empty price level
    void remove_price_level(Side side, Price price);

    // Match a buy order against the sell side
    std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> match_buy_order(Order& order);

    // Match a sell order against the buy side
    std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> match_sell_order(Order& order);

    // Add a limit order to the appropriate side (resting order)
    void add_resting_order(const Order& order);

    // Allocate order node from pool
    OrderNode* allocate_order_node(const Order& order, Price price, Side side);

    // Free order node back to pool
    void free_order_node(OrderNode* node);

    // Generate market data event for order lifecycle
    MarketDataEvent create_order_event(const Order& order, MarketDataEventType type) const;

    // Buy side: descending order (highest price first = best bid)
    std::vector<PriceLevel> bids_;

    // Sell side: ascending order (lowest price first = best ask)
    std::vector<PriceLevel> asks_;

    // OrderId -> OrderNode* index for O(1) lookup
    std::unordered_map<OrderId, OrderNode*> order_index_;

    // Object pool for order node allocation
    ObjectPool<OrderNode> pool_;

    // Sequence number for order ordering
    uint64_t sequence_ = 0;
};

} // namespace engine
