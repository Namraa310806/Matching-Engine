#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <engine/types.hpp>
#include <engine/orderbook_v1_pool.hpp>

namespace engine {

// Multi-instrument matching engine
// Manages independent order books for multiple instruments
// Each instrument has its own isolated order book state
// Uses OrderBookV1Pool as the underlying single-instrument implementation
class MultiInstrumentEngine {
public:
    MultiInstrumentEngine();
    ~MultiInstrumentEngine();

    // Submit an order to a specific instrument
    // Returns trades and market data events
    // If order.id is 0, the engine assigns a globally unique ID
    // If order.id is non-zero, the engine uses that ID (must be unique)
    std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> submit_order(
        const InstrumentId& instrument_id,
        const Order& order
    );

    // Cancel an order by ID
    // Routes to the correct instrument using the order-to-instrument index
    std::pair<bool, std::vector<MarketDataEvent>> cancel_order(OrderId order_id);

    // Get the best bid for a specific instrument
    std::optional<Price> best_bid(const InstrumentId& instrument_id) const;

    // Get the best ask for a specific instrument
    std::optional<Price> best_ask(const InstrumentId& instrument_id) const;

    // Check if an instrument's buy side is empty
    bool buy_side_empty(const InstrumentId& instrument_id) const;

    // Check if an instrument's sell side is empty
    bool sell_side_empty(const InstrumentId& instrument_id) const;

    // Check if an instrument's book is empty
    bool empty(const InstrumentId& instrument_id) const;

    // Get total number of buy orders for an instrument
    size_t buy_order_count(const InstrumentId& instrument_id) const;

    // Get total number of sell orders for an instrument
    size_t sell_order_count(const InstrumentId& instrument_id) const;

    // Get number of price levels on buy side for an instrument
    size_t buy_price_level_count(const InstrumentId& instrument_id) const;

    // Get number of price levels on sell side for an instrument
    size_t sell_price_level_count(const InstrumentId& instrument_id) const;

    // Get all buy orders for an instrument
    std::vector<Order> get_all_buy_orders(const InstrumentId& instrument_id) const;

    // Get all sell orders for an instrument
    std::vector<Order> get_all_sell_orders(const InstrumentId& instrument_id) const;

    // Get the number of instruments in the engine
    size_t instrument_count() const;

    // Check if an instrument exists in the engine
    bool has_instrument(const InstrumentId& instrument_id) const;

    // Get the instrument ID for a given order ID
    // Returns empty string if order ID not found
    InstrumentId get_instrument_for_order(OrderId order_id) const;

#ifdef TESTING
    // Test-only: Get the size of the order-to-instrument index
    size_t order_to_instrument_index_size() const { return order_to_instrument_.size(); }

    // Test-only: Check if an OrderId exists in the routing index
    bool order_id_in_routing_index(OrderId id) const {
        return order_to_instrument_.find(id) != order_to_instrument_.end();
    }

    // Test-only: Get the order book for an instrument (for direct inspection)
    const OrderBookV1Pool* get_order_book(const InstrumentId& instrument_id) const;
#endif

private:
    // Get or create the order book for an instrument
    OrderBookV1Pool* get_or_create_book(const InstrumentId& instrument_id);

    // Get the order book for an instrument (const version)
    const OrderBookV1Pool* get_book(const InstrumentId& instrument_id) const;

    // Wrap trade with instrument ID
    Trade wrap_trade(const InstrumentId& instrument_id, const Trade& trade) const;

    // Wrap market data event with instrument ID
    MarketDataEvent wrap_event(const InstrumentId& instrument_id, const MarketDataEvent& event) const;

    // Generate next globally unique order ID
    OrderId next_order_id();

    // Instrument ID -> OrderBook mapping (use unique_ptr to prevent move/copy issues)
    std::unordered_map<InstrumentId, std::unique_ptr<OrderBookV1Pool>> books_;

    // OrderId -> InstrumentId routing index for O(1) cancellation lookup
    std::unordered_map<OrderId, InstrumentId> order_to_instrument_;

    // Global order ID counter (ensures uniqueness across all instruments)
    OrderId next_order_id_ = 1;
};

} // namespace engine
