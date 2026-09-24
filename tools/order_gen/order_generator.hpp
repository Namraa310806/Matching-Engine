#pragma once

#include <engine/types.hpp>
#include <vector>
#include <string>
#include <random>
#include <optional>

namespace tools {

// Order action type for workload generation
enum class OrderAction {
    SubmitLimit,
    SubmitMarket,
    Cancel
};

// Generated order record for workload file
struct WorkloadOrder {
    uint64_t sequence;
    OrderAction action;
    engine::Side side;
    engine::Price price;
    engine::Qty quantity;
    engine::OrderId order_id;  // For cancel operations
    
    WorkloadOrder()
        : sequence(0)
        , action(OrderAction::SubmitLimit)
        , side(engine::Side::Buy)
        , price(0)
        , quantity(0)
        , order_id(0)
    {}
};

// Configuration for order flow generation
struct GeneratorConfig {
    size_t num_orders;              // Total number of orders to generate
    double limit_ratio;             // Ratio of limit orders (0.0 to 1.0)
    double buy_ratio;               // Ratio of buy orders (0.0 to 1.0)
    engine::Price mid_price;        // Mid price around which to distribute
    engine::Price spread;          // Spread around mid price (half-spread)
    double volatility;             // Price volatility (standard deviation as ratio of mid price)
    double cancel_rate;             // Probability of cancel vs submit (0.0 to 1.0)
    uint64_t rng_seed;              // RNG seed for reproducibility
    engine::Qty min_qty;            // Minimum order quantity
    engine::Qty max_qty;            // Maximum order quantity
    
    GeneratorConfig()
        : num_orders(1000)
        , limit_ratio(0.9)
        , buy_ratio(0.5)
        , mid_price(100000)  // $100.00 in cents
        , spread(100)         // $1.00 spread
        , volatility(0.01)   // 1% volatility
        , cancel_rate(0.1)    // 10% cancel rate
        , rng_seed(42)
        , min_qty(1)
        , max_qty(100)
    {}
};

// Deterministic order flow generator
class OrderGenerator {
public:
    explicit OrderGenerator(const GeneratorConfig& config);
    
    // Generate the full order flow
    std::vector<WorkloadOrder> generate();
    
    // Write workload to file (text format)
    bool write_to_file(const std::string& filename) const;
    
    // Read workload from file
    static bool read_from_file(const std::string& filename, std::vector<WorkloadOrder>& orders);
    
    // Get the generated orders
    const std::vector<WorkloadOrder>& orders() const { return orders_; }
    
private:
    GeneratorConfig config_;
    std::vector<WorkloadOrder> orders_;
    std::mt19937_64 rng_;
    
    // Track active order IDs for cancellation
    std::vector<engine::OrderId> active_order_ids_;
    
    // Helper functions
    OrderAction generate_action();
    engine::Side generate_side();
    engine::Price generate_price(engine::Side side);
    engine::Qty generate_quantity();
    engine::OrderId generate_order_id();
    
    // Select a random active order ID for cancellation
    std::optional<engine::OrderId> select_order_to_cancel();
};

} // namespace tools
