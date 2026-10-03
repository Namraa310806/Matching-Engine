#include <engine/mutex_multi_instrument_engine.hpp>

namespace engine {

MutexMultiInstrumentEngine::MutexMultiInstrumentEngine() = default;

MutexMultiInstrumentEngine::~MutexMultiInstrumentEngine() = default;

std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> MutexMultiInstrumentEngine::submit_order(
    const InstrumentId& instrument_id,
    const Order& order
) {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.submit_order(instrument_id, order);
}

std::pair<bool, std::vector<MarketDataEvent>> MutexMultiInstrumentEngine::cancel_order(OrderId order_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.cancel_order(order_id);
}

std::optional<Price> MutexMultiInstrumentEngine::best_bid(const InstrumentId& instrument_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.best_bid(instrument_id);
}

std::optional<Price> MutexMultiInstrumentEngine::best_ask(const InstrumentId& instrument_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.best_ask(instrument_id);
}

bool MutexMultiInstrumentEngine::buy_side_empty(const InstrumentId& instrument_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.buy_side_empty(instrument_id);
}

bool MutexMultiInstrumentEngine::sell_side_empty(const InstrumentId& instrument_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.sell_side_empty(instrument_id);
}

bool MutexMultiInstrumentEngine::empty(const InstrumentId& instrument_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.empty(instrument_id);
}

size_t MutexMultiInstrumentEngine::buy_order_count(const InstrumentId& instrument_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.buy_order_count(instrument_id);
}

size_t MutexMultiInstrumentEngine::sell_order_count(const InstrumentId& instrument_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.sell_order_count(instrument_id);
}

size_t MutexMultiInstrumentEngine::buy_price_level_count(const InstrumentId& instrument_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.buy_price_level_count(instrument_id);
}

size_t MutexMultiInstrumentEngine::sell_price_level_count(const InstrumentId& instrument_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.sell_price_level_count(instrument_id);
}

std::vector<Order> MutexMultiInstrumentEngine::get_all_buy_orders(const InstrumentId& instrument_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.get_all_buy_orders(instrument_id);
}

std::vector<Order> MutexMultiInstrumentEngine::get_all_sell_orders(const InstrumentId& instrument_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.get_all_sell_orders(instrument_id);
}

size_t MutexMultiInstrumentEngine::instrument_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.instrument_count();
}

bool MutexMultiInstrumentEngine::has_instrument(const InstrumentId& instrument_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.has_instrument(instrument_id);
}

InstrumentId MutexMultiInstrumentEngine::get_instrument_for_order(OrderId order_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.get_instrument_for_order(order_id);
}

#ifdef TESTING
size_t MutexMultiInstrumentEngine::order_to_instrument_index_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.order_to_instrument_index_size();
}

bool MutexMultiInstrumentEngine::order_id_in_routing_index(OrderId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.order_id_in_routing_index(id);
}

const OrderBookV1Pool* MutexMultiInstrumentEngine::get_order_book(const InstrumentId& instrument_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.get_order_book(instrument_id);
}
#endif

} // namespace engine
