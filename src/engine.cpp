#include <engine/engine.hpp>
#include <engine/orderbook.hpp>
#include <algorithm>

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

std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> OrderBook::submit_order(const Order& order) {
    std::vector<Trade> trades;
    std::vector<MarketDataEvent> events;

    // Copy the order to avoid mutating caller's state
    Order working_order = order;
    working_order.sequence = ++sequence_;

    // Generate order received event
    events.push_back(create_order_event(working_order, MarketDataEventType::OrderAdded));

    // Attempt to match based on side
    if (working_order.side == Side::Buy) {
        auto [matched_trades, match_events] = match_buy_order(working_order);
        trades.insert(trades.end(), matched_trades.begin(), matched_trades.end());
        events.insert(events.end(), match_events.begin(), match_events.end());
    } else {
        auto [matched_trades, match_events] = match_sell_order(working_order);
        trades.insert(trades.end(), matched_trades.begin(), matched_trades.end());
        events.insert(events.end(), match_events.begin(), match_events.end());
    }

    // If limit order has remaining quantity, add to book
    if (working_order.price > 0 && working_order.remaining() > 0) {
        add_resting_order(working_order);
    }

    return {trades, events};
}

std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> OrderBook::match_buy_order(Order& order) {
    std::vector<Trade> trades;
    std::vector<MarketDataEvent> events;

    // Match against asks while there are matching orders and quantity remains
    while (!asks_.empty() && order.remaining() > 0) {
        Price best_ask = asks_.begin()->first;

        // Check if buy price crosses the best ask
        // Market orders (price == 0) always match
        if (order.price > 0 && best_ask > order.price) {
            break; // No more matching prices
        }

        // Get the price level
        auto& price_level = asks_.begin()->second;

        // Match orders at this price level (FIFO)
        while (!price_level.empty() && order.remaining() > 0) {
            Order& resting_order = price_level.front();

            // Calculate trade quantity
            Qty trade_qty = std::min(order.remaining(), resting_order.remaining());

            // Create trade event
            Trade trade(order.id, resting_order.id, best_ask, trade_qty, ++sequence_);
            trades.push_back(trade);

            // Update filled quantities
            order.filled += trade_qty;
            resting_order.filled += trade_qty;

            // Generate market data events
            if (resting_order.is_fully_filled()) {
                events.push_back(create_order_event(resting_order, MarketDataEventType::OrderFullyFilled));
            } else {
                events.push_back(create_order_event(resting_order, MarketDataEventType::OrderPartiallyFilled));
            }

            // Remove fully filled resting order
            if (resting_order.is_fully_filled()) {
                price_level.pop_front();
            }
        }

        // Remove empty price level
        if (price_level.empty()) {
            asks_.erase(asks_.begin());
        }
    }

    // Generate event for incoming order if it was filled
    if (order.is_fully_filled()) {
        events.push_back(create_order_event(order, MarketDataEventType::OrderFullyFilled));
    } else if (order.is_partially_filled()) {
        events.push_back(create_order_event(order, MarketDataEventType::OrderPartiallyFilled));
    }

    return {trades, events};
}

std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> OrderBook::match_sell_order(Order& order) {
    std::vector<Trade> trades;
    std::vector<MarketDataEvent> events;

    // Match against bids while there are matching orders and quantity remains
    while (!bids_.empty() && order.remaining() > 0) {
        Price best_bid = bids_.begin()->first;

        // Check if sell price crosses the best bid
        // Market orders (price == 0) always match
        if (order.price > 0 && best_bid < order.price) {
            break; // No more matching prices
        }

        // Get the price level
        auto& price_level = bids_.begin()->second;

        // Match orders at this price level (FIFO)
        while (!price_level.empty() && order.remaining() > 0) {
            Order& resting_order = price_level.front();

            // Calculate trade quantity
            Qty trade_qty = std::min(order.remaining(), resting_order.remaining());

            // Create trade event
            Trade trade(resting_order.id, order.id, best_bid, trade_qty, ++sequence_);
            trades.push_back(trade);

            // Update filled quantities
            order.filled += trade_qty;
            resting_order.filled += trade_qty;

            // Generate market data events
            if (resting_order.is_fully_filled()) {
                events.push_back(create_order_event(resting_order, MarketDataEventType::OrderFullyFilled));
            } else {
                events.push_back(create_order_event(resting_order, MarketDataEventType::OrderPartiallyFilled));
            }

            // Remove fully filled resting order
            if (resting_order.is_fully_filled()) {
                price_level.pop_front();
            }
        }

        // Remove empty price level
        if (price_level.empty()) {
            bids_.erase(bids_.begin());
        }
    }

    // Generate event for incoming order if it was filled
    if (order.is_fully_filled()) {
        events.push_back(create_order_event(order, MarketDataEventType::OrderFullyFilled));
    } else if (order.is_partially_filled()) {
        events.push_back(create_order_event(order, MarketDataEventType::OrderPartiallyFilled));
    }

    return {trades, events};
}

void OrderBook::add_resting_order(const Order& order) {
    if (order.side == Side::Buy) {
        bids_[order.price].push_back(order);
    } else {
        asks_[order.price].push_back(order);
    }
}

MarketDataEvent OrderBook::create_order_event(const Order& order, MarketDataEventType type) const {
    return MarketDataEvent(
        order.id,
        order.side,
        order.price,
        order.quantity,
        order.filled,
        type,
        order.sequence
    );
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
