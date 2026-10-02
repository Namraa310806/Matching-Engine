#include "engine_cli.hpp"
#include <iostream>

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS] WORKLOAD_FILE\n\n"
              << "Execute a workload or replay file against the matching engine.\n\n"
              << "Options:\n"
              << "  -r, --replay           Execute as replay file (with version checking)\n"
              << "  -s, --stats FILE       Write statistics to file (optional)\n"
              << "  -h, --help             Show this help message\n\n"
              << "Examples:\n"
              << "  " << program_name << " workload.txt\n"
              << "  " << program_name << " -r replay.txt\n"
              << "  " << program_name << " -s results.txt workload.txt\n"
              << "  " << program_name << " -r -s results.txt replay.txt\n";
}

int main(int argc, char* argv[]) {
    std::string workload_file;
    std::string stats_file;
    bool replay_mode = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-r" || arg == "--replay") {
            replay_mode = true;
        } else if (arg == "-s" || arg == "--stats") {
            if (i + 1 < argc) {
                stats_file = argv[++i];
            } else {
                std::cerr << "Error: " << arg << " requires a filename argument\n";
                return 1;
            }
        } else if (arg[0] == '-') {
            std::cerr << "Error: Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        } else {
            workload_file = arg;
        }
    }

    if (workload_file.empty()) {
        std::cerr << "Error: No workload file specified\n";
        print_usage(argv[0]);
        return 1;
    }

    // Execute workload or replay
    tools::EngineCLI cli;
    bool success;
    if (replay_mode) {
        success = cli.execute_replay(workload_file);
    } else {
        success = cli.execute_workload(workload_file);
    }

    if (!success) {
        std::cerr << "Error: Failed to execute workload\n";
        return 1;
    }

    // Print statistics
    cli.print_stats();

    // Write statistics to file if requested
    if (!stats_file.empty()) {
        if (!cli.write_stats(stats_file)) {
            std::cerr << "Error: Failed to write statistics file\n";
            return 1;
        }
        std::cout << "Statistics written to " << stats_file << "\n";
    }

    return 0;
}
