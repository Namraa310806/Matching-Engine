#pragma once

#include <engine/types.hpp>
#include <variant>
#include <string>

namespace engine {

// Ingestion command types sent through SPSC queue from producer to consumer
//
// Purpose: Represent order operations for the matching thread to process
// Ownership: Producer constructs and enqueues, consumer dequeues and processes
// Lifetime: Commands are copied/moved through the queue, no external references
//
// Supported operations:
// - SubmitLimitOrder: Submit a limit order to an instrument
// - SubmitMarketOrder: Submit a market order to an instrument
// - CancelOrder: Cancel an existing order by ID
//
// Design rationale:
// - Use std::variant for type-safe operation encoding
// - No synchronization primitives inside command type
// - Trivially movable for efficient queue transfer
// - Contains all necessary information for the matching engine

struct SubmitLimitOrderCmd {
    InstrumentId instrument_id;
    Order order;  // Order with side, price, quantity, sequence, and id (assigned by producer)
};

struct SubmitMarketOrderCmd {
    InstrumentId instrument_id;
    Order order;  // Order with side, quantity, sequence, and id (assigned by producer)
};

struct CancelOrderCmd {
    OrderId order_id;
};

// Ingestion command variant representing all supported operations
using IngestionCommand = std::variant<
    SubmitLimitOrderCmd,
    SubmitMarketOrderCmd,
    CancelOrderCmd
>;

// Helper to create submit limit order command
inline IngestionCommand make_submit_limit_cmd(
    const InstrumentId& instrument_id,
    const Order& order
) {
    SubmitLimitOrderCmd cmd;
    cmd.instrument_id = instrument_id;
    cmd.order = order;  // id is already assigned by producer
    return cmd;
}

// Helper to create submit market order command
inline IngestionCommand make_submit_market_cmd(
    const InstrumentId& instrument_id,
    const Order& order
) {
    SubmitMarketOrderCmd cmd;
    cmd.instrument_id = instrument_id;
    cmd.order = order;  // id is already assigned by producer
    return cmd;
}

// Helper to create cancel order command
inline IngestionCommand make_cancel_cmd(OrderId order_id) {
    CancelOrderCmd cmd;
    cmd.order_id = order_id;
    return cmd;
}

} // namespace engine
