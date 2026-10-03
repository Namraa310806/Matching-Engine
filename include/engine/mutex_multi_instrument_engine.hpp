#pragma once

#include <mutex>
#include <engine/multi_instrument_engine.hpp>
#include <engine/types.hpp>

namespace engine {

// Mutex-protected multi-instrument matching engine
// Wraps MultiInstrumentEngine with a global mutex for thread-safe concurrent access
// This is a baseline implementation for Phase 5B to measure mutex contention overhead
// The mutex protects all shared mutable state:
// - instrument-to-book map
// - order routing index (OrderId -> InstrumentId)
// - global order ID generation
// - access to individual order books
//
// Architecture:
// Multiple producer threads
//         |
//         v
//      global mutex
//         |
//         v
// MultiInstrumentEngine
//         |
//         +-- Instrument A -> OrderBookV1Pool
//         +-- Instrument B -> OrderBookV1Pool
//         +-- Instrument C -> OrderBookV1Pool
//
// Critical section: All operations through submit_order() and cancel_order()
// are serialized through the mutex, providing simple but coarse-grained synchronization.
//
// This is deliberately simple to establish a measurable baseline.
// Later phases (5C) will investigate lock-free/SPSC architectures.
class MutexMultiInstrumentEngine {
public:
    MutexMultiInstrumentEngine();
    ~MutexMultiInstrumentEngine();

    // Submit an order to a specific instrument (thread-safe)
    // Returns trades and market data events
    std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> submit_order(
        const InstrumentId& instrument_id,
        const Order& order
    );

    // Cancel an order by ID (thread-safe)
    // Routes to the correct instrument using the order-to-instrument index
    std::pair<bool, std::vector<MarketDataEvent>> cancel_order(OrderId order_id);

    // Get the best bid for a specific instrument (thread-safe)
    std::optional<Price> best_bid(const InstrumentId& instrument_id) const;

    // Get the best ask for a specific instrument (thread-safe)
    std::optional<Price> best_ask(const InstrumentId& instrument_id) const;

    // Check if an instrument's buy side is empty (thread-safe)
    bool buy_side_empty(const InstrumentId& instrument_id) const;

    // Check if an instrument's sell side is empty (thread-safe)
    bool sell_side_empty(const InstrumentId& instrument_id) const;

    // Check if an instrument's book is empty (thread-safe)
    bool empty(const InstrumentId& instrument_id) const;

    // Get total number of buy orders for an instrument (thread-safe)
    size_t buy_order_count(const InstrumentId& instrument_id) const;

    // Get total number of sell orders for an instrument (thread-safe)
    size_t sell_order_count(const InstrumentId& instrument_id) const;

    // Get number of price levels on buy side for an instrument (thread-safe)
    size_t buy_price_level_count(const InstrumentId& instrument_id) const;

    // Get number of price levels on sell side for an instrument (thread-safe)
    size_t sell_price_level_count(const InstrumentId& instrument_id) const;

    // Get all buy orders for an instrument (thread-safe)
    std::vector<Order> get_all_buy_orders(const InstrumentId& instrument_id) const;

    // Get all sell orders for an instrument (thread-safe)
    std::vector<Order> get_all_sell_orders(const InstrumentId& instrument_id) const;

    // Get the number of instruments in the engine (thread-safe)
    size_t instrument_count() const;

    // Check if an instrument exists in the engine (thread-safe)
    bool has_instrument(const InstrumentId& instrument_id) const;

    // Get the instrument ID for a given order ID (thread-safe)
    // Returns empty string if order ID not found
    InstrumentId get_instrument_for_order(OrderId order_id) const;

#ifdef TESTING
    // Test-only: Get the size of the order-to-instrument index (thread-safe)
    size_t order_to_instrument_index_size() const;

    // Test-only: Check if an OrderId exists in the routing index (thread-safe)
    bool order_id_in_routing_index(OrderId id) const;

    // Test-only: Get the order book for an instrument (for direct inspection)
    // WARNING: This returns a pointer to internal state.
    // The caller must ensure the engine is not accessed concurrently
    // while using the returned pointer, or race conditions will occur.
    // This is for testing only.
    const OrderBookV1Pool* get_order_book(const InstrumentId& instrument_id) const;
#endif

private:
    // The underlying single-threaded multi-instrument engine
    MultiInstrumentEngine engine_;

    // Global mutex protecting all engine state
    // This is deliberately coarse-grained to establish a simple baseline
    mutable std::mutex mutex_;
};

} // namespace engine
