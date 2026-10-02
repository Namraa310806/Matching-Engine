#pragma once

#include <engine/orderbook.hpp>
#include <engine/types.hpp>
#include "order_generator.hpp"
#include <chrono>
#include <string>

namespace tools {

// Execution statistics
struct ExecutionStats {
    size_t orders_processed;
    size_t trades;
    uint64_t traded_quantity;
    size_t final_bid_depth;
    size_t final_ask_depth;
    size_t final_bid_levels;
    size_t final_ask_levels;
    double execution_time_ms;
    double throughput_orders_per_sec;
    
    ExecutionStats()
        : orders_processed(0)
        , trades(0)
        , traded_quantity(0)
        , final_bid_depth(0)
        , final_ask_depth(0)
        , final_bid_levels(0)
        , final_ask_levels(0)
        , execution_time_ms(0.0)
        , throughput_orders_per_sec(0.0)
    {}
};

// Engine CLI harness
class EngineCLI {
public:
    EngineCLI();

    // Execute a workload file against the engine
    bool execute_workload(const std::string& filename);

    // Execute a replay file against the engine (with version checking)
    bool execute_replay(const std::string& filename);

    // Get execution statistics
    const ExecutionStats& stats() const { return stats_; }
    
    // Print statistics to stdout
    void print_stats() const;
    
    // Write statistics to file
    bool write_stats(const std::string& filename) const;
    
private:
    engine::OrderBook orderbook_;
    ExecutionStats stats_;
    
    // Process a single workload order
    bool process_order(const WorkloadOrder& workload_order);
    
    // Track active orders for cancellation
    std::unordered_map<engine::OrderId, engine::Order> active_orders_;
};

} // namespace tools
