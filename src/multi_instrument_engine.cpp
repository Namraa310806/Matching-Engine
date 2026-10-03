#include <engine/multi_instrument_engine.hpp>

namespace engine {

MultiInstrumentEngine::MultiInstrumentEngine() = default;

MultiInstrumentEngine::~MultiInstrumentEngine() = default;

OrderBookV1Pool* MultiInstrumentEngine::get_or_create_book(const InstrumentId& instrument_id) {
    auto it = books_.find(instrument_id);
    if (it != books_.end()) {
        return it->second.get();
    }
    // Create new book for this instrument
    auto [new_it, inserted] = books_.emplace(instrument_id, std::make_unique<OrderBookV1Pool>());
    return new_it->second.get();
}

const OrderBookV1Pool* MultiInstrumentEngine::get_book(const InstrumentId& instrument_id) const {
    auto it = books_.find(instrument_id);
    if (it != books_.end()) {
        return it->second.get();
    }
    return nullptr;
}

OrderId MultiInstrumentEngine::next_order_id() {
    return next_order_id_++;
}

Trade MultiInstrumentEngine::wrap_trade(const InstrumentId& instrument_id, const Trade& trade) const {
    return Trade(
        instrument_id,
        trade.buy_order_id,
        trade.sell_order_id,
        trade.execution_price,
        trade.execution_quantity,
        trade.sequence
    );
}

MarketDataEvent MultiInstrumentEngine::wrap_event(const InstrumentId& instrument_id, const MarketDataEvent& event) const {
    return MarketDataEvent(
        instrument_id,
        event.order_id,
        event.side,
        event.price,
        event.quantity,
        event.filled,
        event.event_type,
        event.sequence
    );
}

std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> MultiInstrumentEngine::submit_order(
    const InstrumentId& instrument_id,
    const Order& order
) {
    // Get or create the order book for this instrument
    OrderBookV1Pool* book = get_or_create_book(instrument_id);

    // Use the provided order ID if non-zero, otherwise generate one
    OrderId global_order_id = (order.id != 0) ? order.id : next_order_id();

    // Create a copy of the order with the global ID
    Order order_with_global_id = order;
    order_with_global_id.id = global_order_id;

    // Submit to the order book
    auto [trades, events] = book->submit_order(order_with_global_id);

    // Register the order in the routing index
    order_to_instrument_[global_order_id] = instrument_id;

    // Update the next_order_id counter if we used a provided ID that's ahead
    if (order.id != 0 && order.id >= next_order_id_) {
        next_order_id_ = order.id + 1;
    }

    // Wrap all trades and events with the instrument ID
    std::vector<Trade> wrapped_trades;
    wrapped_trades.reserve(trades.size());
    for (const auto& trade : trades) {
        wrapped_trades.push_back(wrap_trade(instrument_id, trade));
    }

    std::vector<MarketDataEvent> wrapped_events;
    wrapped_events.reserve(events.size());
    for (const auto& event : events) {
        wrapped_events.push_back(wrap_event(instrument_id, event));
    }

    return {wrapped_trades, wrapped_events};
}

std::pair<bool, std::vector<MarketDataEvent>> MultiInstrumentEngine::cancel_order(OrderId order_id) {
    // Look up the instrument for this order ID
    auto it = order_to_instrument_.find(order_id);
    if (it == order_to_instrument_.end()) {
        // Order ID not found
        return {false, {}};
    }

    // Copy the instrument_id before erasing from the map
    InstrumentId instrument_id = it->second;

    // Get the order book for this instrument
    OrderBookV1Pool* book = get_or_create_book(instrument_id);
    if (!book) {
        // This should not happen if the index is consistent
        order_to_instrument_.erase(it);
        return {false, {}};
    }

    // Cancel the order in the book
    auto [cancelled, events] = book->cancel_order(order_id);

    if (cancelled) {
        // Remove from routing index
        order_to_instrument_.erase(it);

        // Wrap events with instrument ID
        std::vector<MarketDataEvent> wrapped_events;
        wrapped_events.reserve(events.size());
        for (const auto& event : events) {
            wrapped_events.push_back(wrap_event(instrument_id, event));
        }
        return {true, wrapped_events};
    }

    return {false, {}};
}

std::optional<Price> MultiInstrumentEngine::best_bid(const InstrumentId& instrument_id) const {
    const OrderBookV1Pool* book = get_book(instrument_id);
    if (!book) {
        return std::nullopt;
    }
    return book->best_bid();
}

std::optional<Price> MultiInstrumentEngine::best_ask(const InstrumentId& instrument_id) const {
    const OrderBookV1Pool* book = get_book(instrument_id);
    if (!book) {
        return std::nullopt;
    }
    return book->best_ask();
}

bool MultiInstrumentEngine::buy_side_empty(const InstrumentId& instrument_id) const {
    const OrderBookV1Pool* book = get_book(instrument_id);
    if (!book) {
        return true; // Non-existent instrument is considered empty
    }
    return book->buy_side_empty();
}

bool MultiInstrumentEngine::sell_side_empty(const InstrumentId& instrument_id) const {
    const OrderBookV1Pool* book = get_book(instrument_id);
    if (!book) {
        return true; // Non-existent instrument is considered empty
    }
    return book->sell_side_empty();
}

bool MultiInstrumentEngine::empty(const InstrumentId& instrument_id) const {
    const OrderBookV1Pool* book = get_book(instrument_id);
    if (!book) {
        return true; // Non-existent instrument is considered empty
    }
    return book->empty();
}

size_t MultiInstrumentEngine::buy_order_count(const InstrumentId& instrument_id) const {
    const OrderBookV1Pool* book = get_book(instrument_id);
    if (!book) {
        return 0;
    }
    return book->buy_order_count();
}

size_t MultiInstrumentEngine::sell_order_count(const InstrumentId& instrument_id) const {
    const OrderBookV1Pool* book = get_book(instrument_id);
    if (!book) {
        return 0;
    }
    return book->sell_order_count();
}

size_t MultiInstrumentEngine::buy_price_level_count(const InstrumentId& instrument_id) const {
    const OrderBookV1Pool* book = get_book(instrument_id);
    if (!book) {
        return 0;
    }
    return book->buy_price_level_count();
}

size_t MultiInstrumentEngine::sell_price_level_count(const InstrumentId& instrument_id) const {
    const OrderBookV1Pool* book = get_book(instrument_id);
    if (!book) {
        return 0;
    }
    return book->sell_price_level_count();
}

std::vector<Order> MultiInstrumentEngine::get_all_buy_orders(const InstrumentId& instrument_id) const {
    const OrderBookV1Pool* book = get_book(instrument_id);
    if (!book) {
        return {};
    }
    return book->get_all_buy_orders();
}

std::vector<Order> MultiInstrumentEngine::get_all_sell_orders(const InstrumentId& instrument_id) const {
    const OrderBookV1Pool* book = get_book(instrument_id);
    if (!book) {
        return {};
    }
    return book->get_all_sell_orders();
}

size_t MultiInstrumentEngine::instrument_count() const {
    return books_.size();
}

bool MultiInstrumentEngine::has_instrument(const InstrumentId& instrument_id) const {
    return books_.find(instrument_id) != books_.end();
}

InstrumentId MultiInstrumentEngine::get_instrument_for_order(OrderId order_id) const {
    auto it = order_to_instrument_.find(order_id);
    if (it != order_to_instrument_.end()) {
        return it->second;
    }
    return "";
}

#ifdef TESTING
const OrderBookV1Pool* MultiInstrumentEngine::get_order_book(const InstrumentId& instrument_id) const {
    return get_book(instrument_id);
}
#endif

} // namespace engine
