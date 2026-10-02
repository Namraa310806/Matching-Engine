#include "engine_cli.hpp"
#include <iostream>
#include <fstream>

namespace tools {

EngineCLI::EngineCLI() {
}

bool EngineCLI::execute_workload(const std::string& filename) {
    // Reset state
    orderbook_ = engine::OrderBook();
    stats_ = ExecutionStats();
    active_orders_.clear();

    // Read workload
    std::vector<WorkloadOrder> orders;
    if (!OrderGenerator::read_from_file(filename, orders)) {
        std::cerr << "Failed to read workload file: " << filename << std::endl;
        return false;
    }

    std::cout << "Executing " << orders.size() << " orders...\n";

    // Start timing
    auto start_time = std::chrono::high_resolution_clock::now();

    // Process each order
    for (const auto& workload_order : orders) {
        if (!process_order(workload_order)) {
            std::cerr << "Failed to process order at sequence " << workload_order.sequence << std::endl;
            return false;
        }
        stats_.orders_processed++;
    }

    // End timing
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    stats_.execution_time_ms = duration.count() / 1000.0;

    if (stats_.execution_time_ms > 0) {
        stats_.throughput_orders_per_sec = (stats_.orders_processed / stats_.execution_time_ms) * 1000.0;
    }

    // Capture final book state
    stats_.final_bid_depth = orderbook_.buy_order_count();
    stats_.final_ask_depth = orderbook_.sell_order_count();
    stats_.final_bid_levels = orderbook_.buy_price_level_count();
    stats_.final_ask_levels = orderbook_.sell_price_level_count();

    std::cout << "Execution completed.\n";

    return true;
}

bool EngineCLI::execute_replay(const std::string& filename) {
    // Reset state
    orderbook_ = engine::OrderBook();
    stats_ = ExecutionStats();
    active_orders_.clear();

    // Read replay file with version checking
    std::vector<WorkloadOrder> orders;
    uint32_t version = 0;
    if (!OrderGenerator::read_replay_file(filename, orders, version)) {
        std::cerr << "Failed to read replay file: " << filename << std::endl;
        return false;
    }

    std::cout << "Executing replay file (version " << version << ") with " << orders.size() << " orders...\n";

    // Start timing
    auto start_time = std::chrono::high_resolution_clock::now();

    // Process each order
    for (const auto& workload_order : orders) {
        if (!process_order(workload_order)) {
            std::cerr << "Failed to process order at sequence " << workload_order.sequence << std::endl;
            return false;
        }
        stats_.orders_processed++;
    }

    // End timing
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    stats_.execution_time_ms = duration.count() / 1000.0;

    if (stats_.execution_time_ms > 0) {
        stats_.throughput_orders_per_sec = (stats_.orders_processed / stats_.execution_time_ms) * 1000.0;
    }

    // Capture final book state
    stats_.final_bid_depth = orderbook_.buy_order_count();
    stats_.final_ask_depth = orderbook_.sell_order_count();
    stats_.final_bid_levels = orderbook_.buy_price_level_count();
    stats_.final_ask_levels = orderbook_.sell_price_level_count();

    std::cout << "Replay execution completed.\n";

    return true;
}

bool EngineCLI::process_order(const WorkloadOrder& workload_order) {
    switch (workload_order.action) {
        case OrderAction::SubmitLimit: {
            engine::Order order(
                workload_order.order_id,
                workload_order.side,
                workload_order.price,
                workload_order.quantity,
                workload_order.sequence
            );
            
            auto [trades, events] = orderbook_.submit_order(order);
            
            // Update trade statistics
            for (const auto& trade : trades) {
                stats_.trades++;
                stats_.traded_quantity += trade.execution_quantity;
            }
            
            // Track active orders (only if not fully filled)
            if (!order.is_fully_filled()) {
                active_orders_[order.id] = order;
            } else {
                active_orders_.erase(order.id);
            }
            
            break;
        }
        
        case OrderAction::SubmitMarket: {
            engine::Order order(
                workload_order.order_id,
                workload_order.side,
                workload_order.quantity,
                workload_order.sequence
            );
            
            auto [trades, events] = orderbook_.submit_order(order);
            
            // Update trade statistics
            for (const auto& trade : trades) {
                stats_.trades++;
                stats_.traded_quantity += trade.execution_quantity;
            }
            
            // Market orders never rest, so don't track them
            break;
        }
        
        case OrderAction::Cancel: {
            auto [success, events] = orderbook_.cancel_order(workload_order.order_id);
            
            if (success) {
                active_orders_.erase(workload_order.order_id);
            }
            
            break;
        }
        
        default:
            std::cerr << "Unknown order action: " << static_cast<int>(workload_order.action) << std::endl;
            return false;
    }
    
    return true;
}

void EngineCLI::print_stats() const {
    std::cout << "\n=== Execution Statistics ===\n";
    std::cout << "Orders processed: " << stats_.orders_processed << "\n";
    std::cout << "Trades: " << stats_.trades << "\n";
    std::cout << "Traded quantity: " << stats_.traded_quantity << "\n";
    std::cout << "Final book depth:\n";
    std::cout << "  Bid orders: " << stats_.final_bid_depth << "\n";
    std::cout << "  Ask orders: " << stats_.final_ask_depth << "\n";
    std::cout << "Final book levels:\n";
    std::cout << "  Bid levels: " << stats_.final_bid_levels << "\n";
    std::cout << "  Ask levels: " << stats_.final_ask_levels << "\n";
    std::cout << "Execution time: " << stats_.execution_time_ms << " ms\n";
    std::cout << "Throughput: " << stats_.throughput_orders_per_sec << " orders/sec\n";
    std::cout << "============================\n";
}

bool EngineCLI::write_stats(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
        return false;
    }
    
    file << "orders_processed=" << stats_.orders_processed << "\n";
    file << "trades=" << stats_.trades << "\n";
    file << "traded_quantity=" << stats_.traded_quantity << "\n";
    file << "final_bid_depth=" << stats_.final_bid_depth << "\n";
    file << "final_ask_depth=" << stats_.final_ask_depth << "\n";
    file << "final_bid_levels=" << stats_.final_bid_levels << "\n";
    file << "final_ask_levels=" << stats_.final_ask_levels << "\n";
    file << "execution_time_ms=" << stats_.execution_time_ms << "\n";
    file << "throughput_orders_per_sec=" << stats_.throughput_orders_per_sec << "\n";
    
    file.close();
    return true;
}

} // namespace tools
