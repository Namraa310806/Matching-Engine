#pragma once

#include <engine/multi_instrument_engine.hpp>
#include <engine/spsc_queue.hpp>
#include <engine/ingestion_command.hpp>
#include <thread>
#include <atomic>
#include <vector>
#include <functional>

namespace engine {

// SPSC multi-instrument matching engine
//
// Architecture:
// Producer thread
//     ->
// SPSC ring buffer (fixed capacity)
//     ->
// Single matching-engine thread (consumer)
//     ->
// Owns MultiInstrumentEngine (sole writer of order-book state)
//
// Key Architectural Invariant:
// ONLY the matching-engine consumer thread directly mutates order-book state.
// The producer thread never directly calls matching/book mutation functions.
// The producer constructs ingestion commands and enqueues them.
// The consumer dequeues commands and invokes the existing matching engine.
//
// This invariant ensures:
// - No lock contention on order book state
// - Single-writer ownership of all book state
// - Clear ownership boundary between producer and consumer
//
// Thread Safety:
// - Producer calls: submit_order(), cancel_order() (enqueue commands)
// - Consumer thread: runs matching loop, processes commands
// - Query methods: NOT thread-safe, should only be called when producer is stopped
//
// Capacity: Fixed at construction time (power-of-two)
template<size_t QueueCapacity = 1024>
class SpscMultiInstrumentEngine {
public:
    static_assert(QueueCapacity > 0, "Queue capacity must be positive");
    static_assert((QueueCapacity & (QueueCapacity - 1)) == 0, "Queue capacity must be power of two");

    // Constructor: starts the matching thread
    SpscMultiInstrumentEngine();

    // Destructor: stops the matching thread and waits for completion
    ~SpscMultiInstrumentEngine();

    // Non-copyable, non-movable
    SpscMultiInstrumentEngine(const SpscMultiInstrumentEngine&) = delete;
    SpscMultiInstrumentEngine& operator=(const SpscMultiInstrumentEngine&) = delete;
    SpscMultiInstrumentEngine(SpscMultiInstrumentEngine&&) = delete;
    SpscMultiInstrumentEngine& operator=(SpscMultiInstrumentEngine&&) = delete;

    // Submit a limit order (producer enqueues command)
    // Returns pair of (success, order_id) where:
    //   success: true if enqueued successfully, false if queue is full
    //   order_id: the globally unique order ID assigned to this order
    // Note: This does NOT return trades/events (unlike synchronous API)
    // Trades/events are processed asynchronously by the matching thread
    std::pair<bool, OrderId> submit_order(
        const InstrumentId& instrument_id,
        const Order& order
    );

    // Cancel an order by ID (producer enqueues command)
    // Returns true if enqueued successfully, false if queue is full
    bool cancel_order(OrderId order_id);

    // Stop the matching thread (call before destruction if needed)
    // Waits for all queued commands to be processed
    void stop();

    // Check if matching thread is running
    bool is_running() const;

    // Get queue capacity
    static constexpr size_t queue_capacity() {
        return QueueCapacity;
    }

    // Get current queue size (approximate, from producer perspective)
    size_t queue_size() const;

    // Get number of commands processed by consumer (thread-safe)
    uint64_t processed_command_count() const;

    // WARNING: Query methods are NOT thread-safe
    // They should only be called when the producer is stopped
    // These methods directly access the underlying MultiInstrumentEngine
    // which is owned exclusively by the matching thread
    std::optional<Price> best_bid(const InstrumentId& instrument_id) const;
    std::optional<Price> best_ask(const InstrumentId& instrument_id) const;
    bool buy_side_empty(const InstrumentId& instrument_id) const;
    bool sell_side_empty(const InstrumentId& instrument_id) const;
    bool empty(const InstrumentId& instrument_id) const;
    size_t buy_order_count(const InstrumentId& instrument_id) const;
    size_t sell_order_count(const InstrumentId& instrument_id) const;
    size_t instrument_count() const;
    bool has_instrument(const InstrumentId& instrument_id) const;

#ifdef TESTING
    // Test-only: Access to underlying engine (use with caution)
    // Only valid when matching thread is stopped
    const MultiInstrumentEngine* get_engine() const { return &engine_; }
#endif

private:
    // Matching thread main loop
    void matching_thread_loop();

    // Process a single ingestion command
    void process_command(const IngestionCommand& cmd);

    // The underlying single-threaded multi-instrument engine
    // Owned exclusively by the matching thread (sole writer)
    MultiInstrumentEngine engine_;

    // SPSC queue for ingestion commands
    SpscQueue<IngestionCommand, QueueCapacity> queue_;

    // Matching thread handle
    std::thread matching_thread_;

    // Flag to signal thread to stop
    std::atomic<bool> stop_flag_{false};

    // Flag indicating if thread is running
    std::atomic<bool> running_{false};

    // Order ID counter for producer to assign IDs
    // Producer assigns IDs to enable cancellations in async API
    // Consumer uses these IDs directly, preserving global uniqueness
    std::atomic<OrderId> next_order_id_{1};

    // Counter for commands processed by consumer (for benchmark verification)
    std::atomic<uint64_t> processed_command_count_{0};
};

// Template implementation (must be in header for templates)
template<size_t QueueCapacity>
SpscMultiInstrumentEngine<QueueCapacity>::SpscMultiInstrumentEngine() {
    running_.store(true, std::memory_order_relaxed);
    matching_thread_ = std::thread(&SpscMultiInstrumentEngine::matching_thread_loop, this);
}

template<size_t QueueCapacity>
SpscMultiInstrumentEngine<QueueCapacity>::~SpscMultiInstrumentEngine() {
    stop();
}

template<size_t QueueCapacity>
void SpscMultiInstrumentEngine<QueueCapacity>::stop() {
    if (running_.load(std::memory_order_relaxed)) {
        stop_flag_.store(true, std::memory_order_release);
        if (matching_thread_.joinable()) {
            matching_thread_.join();
        }
        running_.store(false, std::memory_order_relaxed);
    }
}

template<size_t QueueCapacity>
bool SpscMultiInstrumentEngine<QueueCapacity>::is_running() const {
    return running_.load(std::memory_order_relaxed);
}

template<size_t QueueCapacity>
size_t SpscMultiInstrumentEngine<QueueCapacity>::queue_size() const {
    return queue_.size();
}

template<size_t QueueCapacity>
uint64_t SpscMultiInstrumentEngine<QueueCapacity>::processed_command_count() const {
    return processed_command_count_.load(std::memory_order_relaxed);
}

template<size_t QueueCapacity>
std::pair<bool, OrderId> SpscMultiInstrumentEngine<QueueCapacity>::submit_order(
    const InstrumentId& instrument_id,
    const Order& order
) {
    // Assign a globally unique order ID from the producer's counter
    OrderId assigned_id = next_order_id_.fetch_add(1, std::memory_order_relaxed);

    // Create a copy of the order with the assigned ID
    Order order_with_id = order;
    order_with_id.id = assigned_id;

    IngestionCommand cmd;
    if (order.price > 0) {
        // Limit order
        SubmitLimitOrderCmd limit_cmd;
        limit_cmd.instrument_id = instrument_id;
        limit_cmd.order = order_with_id;
        cmd = limit_cmd;
    } else {
        // Market order (price <= 0 indicates market)
        SubmitMarketOrderCmd market_cmd;
        market_cmd.instrument_id = instrument_id;
        market_cmd.order = order_with_id;
        cmd = market_cmd;
    }

    bool enqueued = queue_.enqueue(std::move(cmd));
    return {enqueued, assigned_id};
}

template<size_t QueueCapacity>
bool SpscMultiInstrumentEngine<QueueCapacity>::cancel_order(OrderId order_id) {
    IngestionCommand cmd = make_cancel_cmd(order_id);
    return queue_.enqueue(std::move(cmd));
}

template<size_t QueueCapacity>
void SpscMultiInstrumentEngine<QueueCapacity>::matching_thread_loop() {
    while (!stop_flag_.load(std::memory_order_acquire)) {
        IngestionCommand cmd;
        if (queue_.dequeue(cmd)) {
            process_command(cmd);
            processed_command_count_.fetch_add(1, std::memory_order_relaxed);
        } else {
            // Queue is empty, yield briefly to avoid busy-spin
            std::this_thread::yield();
        }
    }

    // Process remaining commands before exiting
    IngestionCommand cmd;
    while (queue_.dequeue(cmd)) {
        process_command(cmd);
        processed_command_count_.fetch_add(1, std::memory_order_relaxed);
    }
}

template<size_t QueueCapacity>
void SpscMultiInstrumentEngine<QueueCapacity>::process_command(const IngestionCommand& cmd) {
    std::visit([&](const auto& command) {
        using CmdType = std::decay_t<decltype(command)>;

        if constexpr (std::is_same_v<CmdType, SubmitLimitOrderCmd>) {
            // Submit limit order to engine
            // The order already has the producer-assigned ID
            engine_.submit_order(command.instrument_id, command.order);
        } else if constexpr (std::is_same_v<CmdType, SubmitMarketOrderCmd>) {
            // Submit market order to engine
            // The order already has the producer-assigned ID
            engine_.submit_order(command.instrument_id, command.order);
        } else if constexpr (std::is_same_v<CmdType, CancelOrderCmd>) {
            // Cancel order in engine
            engine_.cancel_order(command.order_id);
        }
    }, cmd);
}

// Query methods (NOT thread-safe, only use when stopped)
template<size_t QueueCapacity>
std::optional<Price> SpscMultiInstrumentEngine<QueueCapacity>::best_bid(const InstrumentId& instrument_id) const {
    return engine_.best_bid(instrument_id);
}

template<size_t QueueCapacity>
std::optional<Price> SpscMultiInstrumentEngine<QueueCapacity>::best_ask(const InstrumentId& instrument_id) const {
    return engine_.best_ask(instrument_id);
}

template<size_t QueueCapacity>
bool SpscMultiInstrumentEngine<QueueCapacity>::buy_side_empty(const InstrumentId& instrument_id) const {
    return engine_.buy_side_empty(instrument_id);
}

template<size_t QueueCapacity>
bool SpscMultiInstrumentEngine<QueueCapacity>::sell_side_empty(const InstrumentId& instrument_id) const {
    return engine_.sell_side_empty(instrument_id);
}

template<size_t QueueCapacity>
bool SpscMultiInstrumentEngine<QueueCapacity>::empty(const InstrumentId& instrument_id) const {
    return engine_.empty(instrument_id);
}

template<size_t QueueCapacity>
size_t SpscMultiInstrumentEngine<QueueCapacity>::buy_order_count(const InstrumentId& instrument_id) const {
    return engine_.buy_order_count(instrument_id);
}

template<size_t QueueCapacity>
size_t SpscMultiInstrumentEngine<QueueCapacity>::sell_order_count(const InstrumentId& instrument_id) const {
    return engine_.sell_order_count(instrument_id);
}

template<size_t QueueCapacity>
size_t SpscMultiInstrumentEngine<QueueCapacity>::instrument_count() const {
    return engine_.instrument_count();
}

template<size_t QueueCapacity>
bool SpscMultiInstrumentEngine<QueueCapacity>::has_instrument(const InstrumentId& instrument_id) const {
    return engine_.has_instrument(instrument_id);
}

} // namespace engine
