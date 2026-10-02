#include <gtest/gtest.h>
#include "order_generator.hpp"
#include "engine_cli.hpp"
#include <fstream>
#include <cstdio>

TEST(ReplayTest, ReplayFileGeneration) {
    // Generate a small deterministic workload
    tools::GeneratorConfig config;
    config.num_orders = 100;
    config.rng_seed = 12345;
    config.limit_ratio = 0.8;
    config.buy_ratio = 0.5;

    tools::OrderGenerator generator(config);
    generator.generate();

    // Write replay file
    std::string replay_file = "test_replay.txt";
    ASSERT_TRUE(generator.write_replay_file(replay_file));

    // Verify file exists
    std::ifstream file(replay_file);
    ASSERT_TRUE(file.is_open());

    // Check header contains version
    std::string line;
    bool found_version = false;
    while (std::getline(file, line)) {
        if (line.find("replay_version=") != std::string::npos) {
            found_version = true;
            EXPECT_EQ(line, "replay_version=1");
            break;
        }
    }
    EXPECT_TRUE(found_version);
    file.close();

    // Cleanup
    std::remove(replay_file.c_str());
}

TEST(ReplayTest, ReplayFileReading) {
    // Generate a small deterministic workload
    tools::GeneratorConfig config;
    config.num_orders = 50;
    config.rng_seed = 54321;

    tools::OrderGenerator generator(config);
    generator.generate();

    // Write replay file
    std::string replay_file = "test_replay_read.txt";
    ASSERT_TRUE(generator.write_replay_file(replay_file));

    // Read replay file
    std::vector<tools::WorkloadOrder> orders;
    uint32_t version = 0;
    ASSERT_TRUE(tools::OrderGenerator::read_replay_file(replay_file, orders, version));

    // Verify version
    EXPECT_EQ(version, 1);

    // Verify order count
    EXPECT_EQ(orders.size(), 50);

    // Verify sequence numbers are sequential
    for (size_t i = 0; i < orders.size(); ++i) {
        EXPECT_EQ(orders[i].sequence, i + 1);
    }

    // Cleanup
    std::remove(replay_file.c_str());
}

TEST(ReplayTest, ReplayVersionMismatch) {
    // Create a replay file with wrong version
    std::string replay_file = "test_replay_version.txt";
    std::ofstream file(replay_file);
    ASSERT_TRUE(file.is_open());
    file << "# Replay File\n";
    file << "replay_version=999\n";
    file << "num_orders=1\n";
    file << "# End Header\n";
    file << "# Format: sequence|action|side|price|quantity|order_id\n";
    file << "1|0|0|100000|50|1\n";
    file.close();

    // Try to read - should fail due to version mismatch
    std::vector<tools::WorkloadOrder> orders;
    uint32_t version = 0;
    EXPECT_FALSE(tools::OrderGenerator::read_replay_file(replay_file, orders, version));

    // Cleanup
    std::remove(replay_file.c_str());
}

TEST(ReplayTest, ReplayDeterminism) {
    // Generate a deterministic workload
    tools::GeneratorConfig config;
    config.num_orders = 200;
    config.rng_seed = 99999;
    config.limit_ratio = 0.7;
    config.buy_ratio = 0.6;
    config.cancel_rate = 0.15;

    tools::OrderGenerator generator(config);
    generator.generate();

    // Write replay file
    std::string replay_file = "test_replay_determinism.txt";
    ASSERT_TRUE(generator.write_replay_file(replay_file));

    // Execute replay file twice
    tools::ExecutionStats stats1, stats2;

    // First execution
    tools::EngineCLI cli1;
    ASSERT_TRUE(cli1.execute_replay(replay_file));
    stats1 = cli1.stats();

    // Second execution
    tools::EngineCLI cli2;
    ASSERT_TRUE(cli2.execute_replay(replay_file));
    stats2 = cli2.stats();

    // Verify identical results
    EXPECT_EQ(stats1.orders_processed, stats2.orders_processed);
    EXPECT_EQ(stats1.trades, stats2.trades);
    EXPECT_EQ(stats1.traded_quantity, stats2.traded_quantity);
    EXPECT_EQ(stats1.final_bid_depth, stats2.final_bid_depth);
    EXPECT_EQ(stats1.final_ask_depth, stats2.final_ask_depth);
    EXPECT_EQ(stats1.final_bid_levels, stats2.final_bid_levels);
    EXPECT_EQ(stats1.final_ask_levels, stats2.final_ask_levels);

    // Cleanup
    std::remove(replay_file.c_str());
}

TEST(ReplayTest, ReplayVsWorkloadIdentical) {
    // Generate a deterministic workload
    tools::GeneratorConfig config;
    config.num_orders = 150;
    config.rng_seed = 77777;

    tools::OrderGenerator generator(config);
    generator.generate();

    // Write both workload and replay files
    std::string workload_file = "test_workload.txt";
    std::string replay_file = "test_replay.txt";
    ASSERT_TRUE(generator.write_to_file(workload_file));
    ASSERT_TRUE(generator.write_replay_file(replay_file));

    // Execute workload file
    tools::EngineCLI cli_workload;
    ASSERT_TRUE(cli_workload.execute_workload(workload_file));
    auto stats_workload = cli_workload.stats();

    // Execute replay file
    tools::EngineCLI cli_replay;
    ASSERT_TRUE(cli_replay.execute_replay(replay_file));
    auto stats_replay = cli_replay.stats();

    // Verify identical results
    EXPECT_EQ(stats_workload.orders_processed, stats_replay.orders_processed);
    EXPECT_EQ(stats_workload.trades, stats_replay.trades);
    EXPECT_EQ(stats_workload.traded_quantity, stats_replay.traded_quantity);
    EXPECT_EQ(stats_workload.final_bid_depth, stats_replay.final_bid_depth);
    EXPECT_EQ(stats_workload.final_ask_depth, stats_replay.final_ask_depth);
    EXPECT_EQ(stats_workload.final_bid_levels, stats_replay.final_bid_levels);
    EXPECT_EQ(stats_workload.final_ask_levels, stats_replay.final_ask_levels);

    // Cleanup
    std::remove(workload_file.c_str());
    std::remove(replay_file.c_str());
}

TEST(ReplayTest, ReplaySelfContained) {
    // Generate a workload
    tools::GeneratorConfig config;
    config.num_orders = 100;
    config.rng_seed = 11111;

    tools::OrderGenerator generator(config);
    generator.generate();

    // Write replay file
    std::string replay_file = "test_replay_selfcontained.txt";
    ASSERT_TRUE(generator.write_replay_file(replay_file));

    // Read replay file without using the original generator
    std::vector<tools::WorkloadOrder> orders;
    uint32_t version = 0;
    ASSERT_TRUE(tools::OrderGenerator::read_replay_file(replay_file, orders, version));

    // Execute the orders directly
    tools::EngineCLI cli;
    ASSERT_TRUE(cli.execute_replay(replay_file));

    // Verify execution succeeded
    EXPECT_EQ(cli.stats().orders_processed, 100);

    // Cleanup
    std::remove(replay_file.c_str());
}
