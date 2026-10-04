#include <engine/orderbook_v1_pool.hpp>
#include <algorithm>

namespace engine {

OrderBookV1Pool::OrderBookV1Pool() = default;

OrderBookV1Pool::~OrderBookV1Pool() {
    // Pool automatically cleans up all allocated chunks
}

OrderNode* OrderBookV1Pool::allocate_order_node(const Order& order, Price price, Side side) {
    OrderNode* node = pool_.allocate();
    new (node) OrderNode(order, price, side);  // Placement new
    return node;
}

void OrderBookV1Pool::free_order_node(OrderNode* node) {
    if (!node) return;
    node->~OrderNode();  // Call destructor
    pool_.deallocate(node);
}

PriceLevel* OrderBookV1Pool::find_or_create_price_level(Side side, Price price) {
    std::vector<PriceLevel>& levels = (side == Side::Buy) ? bids_ : asks_;

    // Binary search for existing price level
    auto compare = (side == Side::Buy)
        ? [](const PriceLevel& level, Price p) { return level.price > p; }  // descending
        : [](const PriceLevel& level, Price p) { return level.price < p; };  // ascending

    auto it = std::lower_bound(levels.begin(), levels.end(), price, compare);

    if (it != levels.end() && it->price == price) {
        return &(*it);  // Found existing level
    }

    // Insert new price level at correct position
    it = levels.insert(it, PriceLevel(price));
    return &(*it);
}

PriceLevel* OrderBookV1Pool::find_price_level(Side side, Price price) {
    std::vector<PriceLevel>& levels = (side == Side::Buy) ? bids_ : asks_;

    auto compare = (side == Side::Buy)
        ? [](const PriceLevel& level, Price p) { return level.price > p; }
        : [](const PriceLevel& level, Price p) { return level.price < p; };

    auto it = std::lower_bound(levels.begin(), levels.end(), price, compare);

    if (it != levels.end() && it->price == price) {
        return &(*it);
    }
    return nullptr;
}

const PriceLevel* OrderBookV1Pool::find_price_level(Side side, Price price) const {
    const std::vector<PriceLevel>& levels = (side == Side::Buy) ? bids_ : asks_;

    auto compare = (side == Side::Buy)
        ? [](const PriceLevel& level, Price p) { return level.price > p; }
        : [](const PriceLevel& level, Price p) { return level.price < p; };

    auto it = std::lower_bound(levels.begin(), levels.end(), price, compare);

    if (it != levels.end() && it->price == price) {
        return &(*it);
    }
    return nullptr;
}

void OrderBookV1Pool::remove_price_level(Side side, Price price) {
    std::vector<PriceLevel>& levels = (side == Side::Buy) ? bids_ : asks_;

    auto compare = (side == Side::Buy)
        ? [](const PriceLevel& level, Price p) { return level.price > p; }
        : [](const PriceLevel& level, Price p) { return level.price < p; };

    auto it = std::lower_bound(levels.begin(), levels.end(), price, compare);

    if (it != levels.end() && it->price == price) {
        levels.erase(it);
    }
}

void OrderBookV1Pool::add_limit_order(const Order& order) {
    // Store order without matching logic (Phase 1.2)
    // Orders are copied to preserve caller's object
    PriceLevel* level = find_or_create_price_level(order.side, order.price);
    OrderNode* node = allocate_order_node(order, order.price, order.side);
    level->push_back(node);
    order_index_[order.id] = node;
}

std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> OrderBookV1Pool::submit_order(const Order& order) {
    std::vector<Trade> trades;
    std::vector<MarketDataEvent> events;

    // Copy the order to avoid mutating caller's state
    Order working_order = order;
    working_order.sequence = ++sequence_;

    // Generate order received event
    events.emplace_back(create_order_event(working_order, MarketDataEventType::OrderAdded));

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

std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> OrderBookV1Pool::match_buy_order(Order& order) {
    std::vector<Trade> trades;
    std::vector<MarketDataEvent> events;

    // Match against asks while there are matching orders and quantity remains
    while (!asks_.empty() && order.remaining() > 0) {
        Price best_ask = asks_.front().price;

        // Check if buy price crosses the best ask
        // Market orders (price == 0) always match
        if (order.price > 0 && best_ask > order.price) {
            break; // No more matching prices
        }

        // Get the price level
        PriceLevel& price_level = asks_.front();

        // Match orders at this price level (FIFO)
        while (!price_level.empty() && order.remaining() > 0) {
            OrderNode* resting_node = price_level.head;
            Order& resting_order = resting_node->order;

            // Calculate trade quantity
            Qty trade_qty = std::min(order.remaining(), resting_order.remaining());

            // Create trade event
            Trade trade("", order.id, resting_order.id, best_ask, trade_qty, ++sequence_);
            trades.emplace_back(std::move(trade));

            // Update filled quantities
            order.filled += trade_qty;
            resting_order.filled += trade_qty;

            // Generate market data events
            if (resting_order.is_fully_filled()) {
                events.emplace_back(create_order_event(resting_order, MarketDataEventType::OrderFullyFilled));
            } else {
                events.emplace_back(create_order_event(resting_order, MarketDataEventType::OrderPartiallyFilled));
            }

            // Remove fully filled resting order from index and list
            if (resting_order.is_fully_filled()) {
                order_index_.erase(resting_order.id);
                price_level.pop_front();
                free_order_node(resting_node);
            }
        }

        // Remove empty price level
        if (price_level.empty()) {
            asks_.erase(asks_.begin());
        }
    }

    // Generate event for incoming order if it was filled
    if (order.is_fully_filled()) {
        events.emplace_back(create_order_event(order, MarketDataEventType::OrderFullyFilled));
    } else if (order.is_partially_filled()) {
        events.emplace_back(create_order_event(order, MarketDataEventType::OrderPartiallyFilled));
    }

    return {trades, events};
}

std::pair<std::vector<Trade>, std::vector<MarketDataEvent>> OrderBookV1Pool::match_sell_order(Order& order) {
    std::vector<Trade> trades;
    std::vector<MarketDataEvent> events;

    // Match against bids while there are matching orders and quantity remains
    while (!bids_.empty() && order.remaining() > 0) {
        Price best_bid = bids_.front().price;

        // Check if sell price crosses the best bid
        // Market orders (price == 0) always match
        if (order.price > 0 && best_bid < order.price) {
            break; // No more matching prices
        }

        // Get the price level
        PriceLevel& price_level = bids_.front();

        // Match orders at this price level (FIFO)
        while (!price_level.empty() && order.remaining() > 0) {
            OrderNode* resting_node = price_level.head;
            Order& resting_order = resting_node->order;

            // Calculate trade quantity
            Qty trade_qty = std::min(order.remaining(), resting_order.remaining());

            // Create trade event
            Trade trade("", resting_order.id, order.id, best_bid, trade_qty, ++sequence_);
            trades.emplace_back(std::move(trade));

            // Update filled quantities
            order.filled += trade_qty;
            resting_order.filled += trade_qty;

            // Generate market data events
            if (resting_order.is_fully_filled()) {
                events.emplace_back(create_order_event(resting_order, MarketDataEventType::OrderFullyFilled));
            } else {
                events.emplace_back(create_order_event(resting_order, MarketDataEventType::OrderPartiallyFilled));
            }

            // Remove fully filled resting order from index and list
            if (resting_order.is_fully_filled()) {
                order_index_.erase(resting_order.id);
                price_level.pop_front();
                free_order_node(resting_node);
            }
        }

        // Remove empty price level
        if (price_level.empty()) {
            bids_.erase(bids_.begin());
        }
    }

    // Generate event for incoming order if it was filled
    if (order.is_fully_filled()) {
        events.emplace_back(create_order_event(order, MarketDataEventType::OrderFullyFilled));
    } else if (order.is_partially_filled()) {
        events.emplace_back(create_order_event(order, MarketDataEventType::OrderPartiallyFilled));
    }

    return {trades, events};
}

void OrderBookV1Pool::add_resting_order(const Order& order) {
    PriceLevel* level = find_or_create_price_level(order.side, order.price);
    OrderNode* node = allocate_order_node(order, order.price, order.side);
    level->push_back(node);
    order_index_[order.id] = node;
}

MarketDataEvent OrderBookV1Pool::create_order_event(const Order& order, MarketDataEventType type) const {
    return MarketDataEvent(
        "",
        order.id,
        order.side,
        order.price,
        order.quantity,
        order.filled,
        type,
        order.sequence
    );
}

std::optional<Price> OrderBookV1Pool::best_bid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.front().price;
}

std::optional<Price> OrderBookV1Pool::best_ask() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.front().price;
}

bool OrderBookV1Pool::buy_side_empty() const {
    return bids_.empty();
}

bool OrderBookV1Pool::sell_side_empty() const {
    return asks_.empty();
}

bool OrderBookV1Pool::empty() const {
    return bids_.empty() && asks_.empty();
}

std::vector<Order> OrderBookV1Pool::get_orders_at_bid_price(Price price) const {
    const PriceLevel* level = find_price_level(Side::Buy, price);
    if (!level) {
        return {};
    }

    std::vector<Order> result;
    OrderNode* node = level->head;
    while (node) {
        result.push_back(node->order);
        node = node->next;
    }
    return result;
}

std::vector<Order> OrderBookV1Pool::get_orders_at_ask_price(Price price) const {
    const PriceLevel* level = find_price_level(Side::Sell, price);
    if (!level) {
        return {};
    }

    std::vector<Order> result;
    OrderNode* node = level->head;
    while (node) {
        result.push_back(node->order);
        node = node->next;
    }
    return result;
}

size_t OrderBookV1Pool::buy_order_count() const {
    size_t count = 0;
    for (const auto& level : bids_) {
        count += level.order_count;
    }
    return count;
}

size_t OrderBookV1Pool::sell_order_count() const {
    size_t count = 0;
    for (const auto& level : asks_) {
        count += level.order_count;
    }
    return count;
}

size_t OrderBookV1Pool::buy_price_level_count() const {
    return bids_.size();
}

size_t OrderBookV1Pool::sell_price_level_count() const {
    return asks_.size();
}

std::vector<Order> OrderBookV1Pool::get_all_buy_orders() const {
    std::vector<Order> result;
    for (const auto& level : bids_) {
        OrderNode* node = level.head;
        while (node) {
            result.push_back(node->order);
            node = node->next;
        }
    }
    return result;
}

std::vector<Order> OrderBookV1Pool::get_all_sell_orders() const {
    std::vector<Order> result;
    for (const auto& level : asks_) {
        OrderNode* node = level.head;
        while (node) {
            result.push_back(node->order);
            node = node->next;
        }
    }
    return result;
}

std::pair<bool, std::vector<MarketDataEvent>> OrderBookV1Pool::cancel_order(OrderId order_id) {
    std::vector<MarketDataEvent> events;

    // Look up the order in the index
    auto it = order_index_.find(order_id);
    if (it == order_index_.end()) {
        // Order not found or not resting
        return {false, events};
    }

    OrderNode* node = it->second;
    Side side = node->side;
    Price price = node->price;

    // Get the price level
    PriceLevel* level = find_price_level(side, price);
    if (!level) {
        // Should not happen if index is consistent
        order_index_.erase(it);
        return {false, events};
    }

    // Generate cancellation event
    events.emplace_back(create_order_event(node->order, MarketDataEventType::OrderCancelled));

    // Remove from price level (O(1) with intrusive list)
    level->remove(node);

    // Remove empty price level
    if (level->empty()) {
        remove_price_level(side, price);
    }

    // Free node back to pool
    free_order_node(node);

    // Remove from index
    order_index_.erase(it);

    return {true, events};
}

} // namespace engine
