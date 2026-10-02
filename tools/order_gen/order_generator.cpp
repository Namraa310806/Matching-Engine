#include "order_generator.hpp"
#include <fstream>
#include <sstream>
#include <optional>
#include <cmath>
#include <iostream>

namespace tools {

OrderGenerator::OrderGenerator(const GeneratorConfig& config)
    : config_(config)
    , rng_(config.rng_seed)
{
}

std::vector<WorkloadOrder> OrderGenerator::generate() {
    orders_.clear();
    active_order_ids_.clear();
    orders_.reserve(config_.num_orders);
    
    std::uniform_real_distribution<double> action_dist(0.0, 1.0);
    std::uniform_real_distribution<double> side_dist(0.0, 1.0);
    
    for (size_t i = 0; i < config_.num_orders; ++i) {
        WorkloadOrder order;
        order.sequence = i + 1;
        
        // Determine action: submit or cancel
        double action_roll = action_dist(rng_);
        
        // Only cancel if we have active orders
        if (action_roll < config_.cancel_rate && !active_order_ids_.empty()) {
            order.action = OrderAction::Cancel;
            auto cancel_id = select_order_to_cancel();
            if (cancel_id) {
                order.order_id = *cancel_id;
                order.side = engine::Side::Buy;  // Side doesn't matter for cancel
                order.price = 0;
                order.quantity = 0;
            } else {
                // Fallback to submit if no active orders
                order.action = OrderAction::SubmitLimit;
                order.side = generate_side();
                order.price = generate_price(order.side);
                order.quantity = generate_quantity();
                order.order_id = generate_order_id();
            }
        } else {
            // Submit order
            double limit_roll = action_dist(rng_);
            if (limit_roll < config_.limit_ratio) {
                order.action = OrderAction::SubmitLimit;
            } else {
                order.action = OrderAction::SubmitMarket;
            }
            
            order.side = generate_side();
            order.price = (order.action == OrderAction::SubmitMarket) ? 0 : generate_price(order.side);
            order.quantity = generate_quantity();
            order.order_id = generate_order_id();
        }
        
        orders_.push_back(order);
    }
    
    return orders_;
}

OrderAction OrderGenerator::generate_action() {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    double roll = dist(rng_);
    
    if (roll < config_.cancel_rate && !active_order_ids_.empty()) {
        return OrderAction::Cancel;
    }
    
    std::uniform_real_distribution<double> limit_dist(0.0, 1.0);
    if (limit_dist(rng_) < config_.limit_ratio) {
        return OrderAction::SubmitLimit;
    }
    return OrderAction::SubmitMarket;
}

engine::Side OrderGenerator::generate_side() {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return (dist(rng_) < config_.buy_ratio) ? engine::Side::Buy : engine::Side::Sell;
}

engine::Price OrderGenerator::generate_price(engine::Side side) {
    // Use normal distribution for price around mid price
    double std_dev = config_.mid_price * config_.volatility;
    std::normal_distribution<double> price_dist(static_cast<double>(config_.mid_price), std_dev);
    
    double price = price_dist(rng_);
    
    // Ensure price is positive
    if (price < 1.0) {
        price = 1.0;
    }
    
    // Round to nearest integer (cents)
    engine::Price rounded_price = static_cast<engine::Price>(std::round(price));
    
    // Apply spread: buys at or below mid, sells at or above mid
    if (side == engine::Side::Buy) {
        // Buy orders should be at or below mid - spread
        engine::Price max_buy = config_.mid_price - config_.spread;
        if (rounded_price > max_buy) {
            rounded_price = max_buy;
        }
    } else {
        // Sell orders should be at or above mid + spread
        engine::Price min_sell = config_.mid_price + config_.spread;
        if (rounded_price < min_sell) {
            rounded_price = min_sell;
        }
    }
    
    return rounded_price;
}

engine::Qty OrderGenerator::generate_quantity() {
    std::uniform_int_distribution<engine::Qty> dist(config_.min_qty, config_.max_qty);
    return dist(rng_);
}

engine::OrderId OrderGenerator::generate_order_id() {
    static engine::OrderId next_id = 1;
    engine::OrderId id = next_id++;
    active_order_ids_.push_back(id);
    return id;
}

std::optional<engine::OrderId> OrderGenerator::select_order_to_cancel() {
    if (active_order_ids_.empty()) {
        return std::nullopt;
    }
    
    std::uniform_int_distribution<size_t> index_dist(0, active_order_ids_.size() - 1);
    size_t index = index_dist(rng_);
    
    engine::OrderId id = active_order_ids_[index];
    
    // Remove from active list
    active_order_ids_.erase(active_order_ids_.begin() + index);
    
    return id;
}

bool OrderGenerator::write_to_file(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
        return false;
    }

    // Write header
    file << "# Workload Configuration\n";
    file << "num_orders=" << config_.num_orders << "\n";
    file << "limit_ratio=" << config_.limit_ratio << "\n";
    file << "buy_ratio=" << config_.buy_ratio << "\n";
    file << "mid_price=" << config_.mid_price << "\n";
    file << "spread=" << config_.spread << "\n";
    file << "volatility=" << config_.volatility << "\n";
    file << "cancel_rate=" << config_.cancel_rate << "\n";
    file << "rng_seed=" << config_.rng_seed << "\n";
    file << "min_qty=" << config_.min_qty << "\n";
    file << "max_qty=" << config_.max_qty << "\n";
    file << "# End Configuration\n";
    file << "# Format: sequence|action|side|price|quantity|order_id\n";
    file << "# Actions: 0=SubmitLimit, 1=SubmitMarket, 2=Cancel\n";
    file << "# Sides: 0=Buy, 1=Sell\n";

    // Write orders
    for (const auto& order : orders_) {
        file << order.sequence << "|"
            << static_cast<int>(order.action) << "|"
            << static_cast<int>(order.side) << "|"
            << order.price << "|"
            << order.quantity << "|"
            << order.order_id << "\n";
    }

    file.close();
    return true;
}

bool OrderGenerator::write_replay_file(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open replay file for writing: " << filename << std::endl;
        return false;
    }

    // Write replay file header with version
    file << "# Replay File - Self-contained order stream for deterministic replay\n";
    file << "replay_version=" << REPLAY_FORMAT_VERSION << "\n";
    file << "num_orders=" << orders_.size() << "\n";
    file << "# End Header\n";
    file << "# Format: sequence|action|side|price|quantity|order_id\n";
    file << "# Actions: 0=SubmitLimit, 1=SubmitMarket, 2=Cancel\n";
    file << "# Sides: 0=Buy, 1=Sell\n";

    // Write orders
    for (const auto& order : orders_) {
        file << order.sequence << "|"
            << static_cast<int>(order.action) << "|"
            << static_cast<int>(order.side) << "|"
            << order.price << "|"
            << order.quantity << "|"
            << order.order_id << "\n";
    }

    file.close();
    return true;
}

bool OrderGenerator::read_from_file(const std::string& filename, std::vector<WorkloadOrder>& orders) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading: " << filename << std::endl;
        return false;
    }

    orders.clear();
    std::string line;

    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Check if this is a configuration line
        if (line.find('=') != std::string::npos) {
            continue;  // Skip configuration lines
        }

        // Parse order line
        std::istringstream iss(line);
        std::string token;
        WorkloadOrder order;

        // sequence
        if (!std::getline(iss, token, '|')) break;
        order.sequence = std::stoull(token);

        // action
        if (!std::getline(iss, token, '|')) break;
        order.action = static_cast<OrderAction>(std::stoi(token));

        // side
        if (!std::getline(iss, token, '|')) break;
        order.side = static_cast<engine::Side>(std::stoi(token));

        // price
        if (!std::getline(iss, token, '|')) break;
        order.price = std::stoll(token);

        // quantity
        if (!std::getline(iss, token, '|')) break;
        order.quantity = std::stoull(token);

        // order_id
        if (!std::getline(iss, token, '|')) break;
        order.order_id = std::stoull(token);

        orders.push_back(order);
    }

    file.close();
    return true;
}

bool OrderGenerator::read_replay_file(const std::string& filename, std::vector<WorkloadOrder>& orders, uint32_t& version) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open replay file for reading: " << filename << std::endl;
        return false;
    }

    orders.clear();
    version = 0;
    std::string line;
    bool header_complete = false;

    while (std::getline(file, line)) {
        // Skip empty lines
        if (line.empty()) {
            continue;
        }

        // Process header
        if (!header_complete) {
            if (line[0] == '#') {
                // Comment line
                if (line.find("# End Header") != std::string::npos) {
                    header_complete = true;
                }
                continue;
            }

            // Parse header fields
            size_t eq_pos = line.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = line.substr(0, eq_pos);
                std::string value = line.substr(eq_pos + 1);

                if (key == "replay_version") {
                    version = std::stoul(value);
                }
                // num_orders is also in header but we don't need it for parsing
            }
            continue;
        }

        // Skip comments after header
        if (line[0] == '#') {
            continue;
        }

        // Parse order line
        std::istringstream iss(line);
        std::string token;
        WorkloadOrder order;

        // sequence
        if (!std::getline(iss, token, '|')) break;
        order.sequence = std::stoull(token);

        // action
        if (!std::getline(iss, token, '|')) break;
        order.action = static_cast<OrderAction>(std::stoi(token));

        // side
        if (!std::getline(iss, token, '|')) break;
        order.side = static_cast<engine::Side>(std::stoi(token));

        // price
        if (!std::getline(iss, token, '|')) break;
        order.price = std::stoll(token);

        // quantity
        if (!std::getline(iss, token, '|')) break;
        order.quantity = std::stoull(token);

        // order_id
        if (!std::getline(iss, token, '|')) break;
        order.order_id = std::stoull(token);

        orders.push_back(order);
    }

    file.close();

    // Validate version
    if (version == 0) {
        std::cerr << "Error: No replay_version found in replay file\n";
        return false;
    }

    if (version != REPLAY_FORMAT_VERSION) {
        std::cerr << "Error: Replay file version " << version << " is not supported (current version: " << REPLAY_FORMAT_VERSION << ")\n";
        return false;
    }

    return true;
}

} // namespace tools
