#include <engine/engine.hpp>
#include <engine/orderbook.hpp>

namespace engine {

void OrderBook::add_limit_order(const Order& order) {
    // Store order without matching logic (Phase 1.2)
    // Orders are copied to preserve caller's object
    if (order.side == Side::Buy) {
        bids_[order.price].push_back(order);
    } else {
        asks_[order.price].push_back(order);
    }
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

bool OrderBook::buy_side_empty() const {
    return bids_.empty();
}

bool OrderBook::sell_side_empty() const {
    return asks_.empty();
}

bool OrderBook::empty() const {
    return bids_.empty() && asks_.empty();
}

std::vector<Order> OrderBook::get_orders_at_bid_price(Price price) const {
    auto it = bids_.find(price);
    if (it == bids_.end()) {
        return {};
    }
    return std::vector<Order>(it->second.begin(), it->second.end());
}

std::vector<Order> OrderBook::get_orders_at_ask_price(Price price) const {
    auto it = asks_.find(price);
    if (it == asks_.end()) {
        return {};
    }
    return std::vector<Order>(it->second.begin(), it->second.end());
}

size_t OrderBook::buy_order_count() const {
    size_t count = 0;
    for (const auto& [price, orders] : bids_) {
        count += orders.size();
    }
    return count;
}

size_t OrderBook::sell_order_count() const {
    size_t count = 0;
    for (const auto& [price, orders] : asks_) {
        count += orders.size();
    }
    return count;
}

size_t OrderBook::buy_price_level_count() const {
    return bids_.size();
}

size_t OrderBook::sell_price_level_count() const {
    return asks_.size();
}

std::vector<Order> OrderBook::get_all_buy_orders() const {
    std::vector<Order> result;
    for (const auto& [price, orders] : bids_) {
        result.insert(result.end(), orders.begin(), orders.end());
    }
    return result;
}

std::vector<Order> OrderBook::get_all_sell_orders() const {
    std::vector<Order> result;
    for (const auto& [price, orders] : asks_) {
        result.insert(result.end(), orders.begin(), orders.end());
    }
    return result;
}

} // namespace engine
