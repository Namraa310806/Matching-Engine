#include "order_generator.hpp"
#include <iostream>
#include <sstream>
#include <string>

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n\n"
              << "Generate deterministic order flow workloads for the matching engine.\n\n"
              << "Options:\n"
              << "  -o, --output FILE       Output workload file (default: workload.txt)\n"
              << "  -R, --replay FILE       Output replay file (versioned, self-contained)\n"
              << "  -n, --num-orders N      Number of orders to generate (default: 1000)\n"
              << "  -l, --limit-ratio R     Ratio of limit orders 0.0-1.0 (default: 0.9)\n"
              << "  -b, --buy-ratio R       Ratio of buy orders 0.0-1.0 (default: 0.5)\n"
              << "  -m, --mid-price P       Mid price in cents (default: 100000)\n"
              << "  -s, --spread S          Spread in cents (default: 100)\n"
              << "  -v, --volatility V      Price volatility ratio (default: 0.01)\n"
              << "  -c, --cancel-rate R     Cancel probability 0.0-1.0 (default: 0.1)\n"
              << "  -r, --rng-seed SEED     RNG seed for reproducibility (default: 42)\n"
              << "  -q, --min-qty Q         Minimum order quantity (default: 1)\n"
              << "  -Q, --max-qty Q         Maximum order quantity (default: 100)\n"
              << "  -h, --help              Show this help message\n\n"
              << "Examples:\n"
              << "  " << program_name << " -o small.txt -n 100\n"
              << "  " << program_name << " -R small.replay -n 100\n"
              << "  " << program_name << " -o large.txt -n 1000000 -r 12345\n"
              << "  " << program_name << " -R large.replay -n 1000000 -r 12345\n"
              << "  " << program_name << " -o custom.txt -l 0.8 -b 0.6 -v 0.02\n";
}

int main(int argc, char* argv[]) {
    tools::GeneratorConfig config;
    std::string output_file = "workload.txt";
    std::string replay_file;
    bool generate_replay = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                output_file = argv[++i];
            } else {
                std::cerr << "Error: " << arg << " requires a filename argument\n";
                return 1;
            }
        } else if (arg == "-R" || arg == "--replay") {
            if (i + 1 < argc) {
                replay_file = argv[++i];
                generate_replay = true;
            } else {
                std::cerr << "Error: " << arg << " requires a filename argument\n";
                return 1;
            }
        } else if (arg == "-n" || arg == "--num-orders") {
            if (i + 1 < argc) {
                config.num_orders = std::stoull(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires a number argument\n";
                return 1;
            }
        } else if (arg == "-l" || arg == "--limit-ratio") {
            if (i + 1 < argc) {
                config.limit_ratio = std::stod(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires a number argument\n";
                return 1;
            }
        } else if (arg == "-b" || arg == "--buy-ratio") {
            if (i + 1 < argc) {
                config.buy_ratio = std::stod(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires a number argument\n";
                return 1;
            }
        } else if (arg == "-m" || arg == "--mid-price") {
            if (i + 1 < argc) {
                config.mid_price = std::stoll(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires a number argument\n";
                return 1;
            }
        } else if (arg == "-s" || arg == "--spread") {
            if (i + 1 < argc) {
                config.spread = std::stoll(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires a number argument\n";
                return 1;
            }
        } else if (arg == "-v" || arg == "--volatility") {
            if (i + 1 < argc) {
                config.volatility = std::stod(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires a number argument\n";
                return 1;
            }
        } else if (arg == "-c" || arg == "--cancel-rate") {
            if (i + 1 < argc) {
                config.cancel_rate = std::stod(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires a number argument\n";
                return 1;
            }
        } else if (arg == "-r" || arg == "--rng-seed") {
            if (i + 1 < argc) {
                config.rng_seed = std::stoull(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires a number argument\n";
                return 1;
            }
        } else if (arg == "-q" || arg == "--min-qty") {
            if (i + 1 < argc) {
                config.min_qty = std::stoull(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires a number argument\n";
                return 1;
            }
        } else if (arg == "-Q" || arg == "--max-qty") {
            if (i + 1 < argc) {
                config.max_qty = std::stoull(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires a number argument\n";
                return 1;
            }
        } else {
            std::cerr << "Error: Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Validate configuration
    if (config.num_orders == 0) {
        std::cerr << "Error: num_orders must be positive\n";
        return 1;
    }
    if (config.limit_ratio < 0.0 || config.limit_ratio > 1.0) {
        std::cerr << "Error: limit_ratio must be between 0.0 and 1.0\n";
        return 1;
    }
    if (config.buy_ratio < 0.0 || config.buy_ratio > 1.0) {
        std::cerr << "Error: buy_ratio must be between 0.0 and 1.0\n";
        return 1;
    }
    if (config.cancel_rate < 0.0 || config.cancel_rate > 1.0) {
        std::cerr << "Error: cancel_rate must be between 0.0 and 1.0\n";
        return 1;
    }
    if (config.mid_price <= 0) {
        std::cerr << "Error: mid_price must be positive\n";
        return 1;
    }
    if (config.spread < 0) {
        std::cerr << "Error: spread must be non-negative\n";
        return 1;
    }
    if (config.volatility < 0.0) {
        std::cerr << "Error: volatility must be non-negative\n";
        return 1;
    }
    if (config.min_qty == 0 || config.max_qty == 0) {
        std::cerr << "Error: min_qty and max_qty must be positive\n";
        return 1;
    }
    if (config.min_qty > config.max_qty) {
        std::cerr << "Error: min_qty cannot be greater than max_qty\n";
        return 1;
    }
    
    // Generate orders
    std::cout << "Generating " << config.num_orders << " orders with seed " << config.rng_seed << "...\n";
    tools::OrderGenerator generator(config);
    generator.generate();

    // Write to file
    if (generate_replay) {
        if (!generator.write_replay_file(replay_file)) {
            std::cerr << "Error: Failed to write replay file\n";
            return 1;
        }
        std::cout << "Replay file written to " << replay_file << "\n";
    } else {
        if (!generator.write_to_file(output_file)) {
            std::cerr << "Error: Failed to write workload to file\n";
            return 1;
        }
        std::cout << "Workload written to " << output_file << "\n";
    }

    std::cout << "Configuration:\n";
    std::cout << "  num_orders: " << config.num_orders << "\n";
    std::cout << "  limit_ratio: " << config.limit_ratio << "\n";
    std::cout << "  buy_ratio: " << config.buy_ratio << "\n";
    std::cout << "  mid_price: " << config.mid_price << "\n";
    std::cout << "  spread: " << config.spread << "\n";
    std::cout << "  volatility: " << config.volatility << "\n";
    std::cout << "  cancel_rate: " << config.cancel_rate << "\n";
    std::cout << "  rng_seed: " << config.rng_seed << "\n";
    std::cout << "  min_qty: " << config.min_qty << "\n";
    std::cout << "  max_qty: " << config.max_qty << "\n";

    return 0;
}
