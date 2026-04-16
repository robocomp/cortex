#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <atomic>
#include <mutex>
#include <thread>

#include "../core/timing_utils.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/multi_agent_fixture.h"
#include "../fixtures/graph_generator.h"

using namespace DSR;
using namespace DSR::Benchmark;

TEST_CASE("Convergence time benchmarks", "[CONSISTENCY][convergence][.multi][PROFILE][MULTIAGENT]") {
    GraphGenerator generator;
    MetricsCollector collector("convergence_time");

    SECTION("Single update convergence") {
        MultiAgentFixture fixture;
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture.create_agents(2, config_file));
        fixture.wait_for_sync();

        auto* agent_a = fixture.get_agent(0);
        auto* agent_b = fixture.get_agent(1);
        REQUIRE(agent_a != nullptr);
        REQUIRE(agent_b != nullptr);

        LatencyTracker tracker(100);

        for (int i = 0; i < 100; ++i) {
            auto node = GraphGenerator::create_test_node(
                0, agent_a->get_agent_id(),
                "conv_node_" + std::to_string(i));

            uint64_t start = get_unix_timestamp();
            auto result = agent_a->insert_node(node);
            if (!result.has_value()) continue;
            uint64_t node_id = result.value();

            // Poll until agent B sees the node
            auto poll_start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - poll_start < std::chrono::seconds(5)) {
                fixture.process_events(1);
                auto b_node = agent_b->get_node(node_id);
                if (b_node.has_value()) {
                    uint64_t conv_time = get_unix_timestamp() - start;
                    tracker.record(conv_time);
                    break;
                }
            }
        }

        auto stats = tracker.stats();
        collector.record_latency_stats("single_node_convergence", stats);
        collector.record_consistency("convergence_success_rate",
            (static_cast<double>(tracker.count()) / 100.0) * 100, "%");

        INFO("Single node convergence - Mean: " << stats.mean_us() << " us, "
             << "P99: " << stats.p99_us() << " us");
        INFO("Success rate: " << tracker.count() << "/100");
    }

    SECTION("Batch convergence time") {
        MultiAgentFixture fixture;
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture.create_agents(2, config_file));
        fixture.wait_for_sync();

        auto* agent_a = fixture.get_agent(0);
        auto* agent_b = fixture.get_agent(1);

        LatencyTracker tracker(20);

        for (int batch = 0; batch < 20; ++batch) {
            // Insert batch of 10 nodes and capture actual IDs
            std::vector<uint64_t> node_ids;
            node_ids.reserve(10);

            uint64_t start = get_unix_timestamp();

            for (int i = 0; i < 10; ++i) {
                auto node = GraphGenerator::create_test_node(
                    0, agent_a->get_agent_id());
                auto result = agent_a->insert_node(node);
                if (result.has_value()) {
                    node_ids.push_back(result.value());
                }
            }

            // Wait for all nodes to converge
            auto poll_start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - poll_start < std::chrono::seconds(10)) {
                fixture.process_events(1);

                bool all_converged = true;
                for (auto id : node_ids) {
                    if (!agent_b->get_node(id).has_value()) {
                        all_converged = false;
                        break;
                    }
                }

                if (all_converged) {
                    uint64_t conv_time = get_unix_timestamp() - start;
                    tracker.record(conv_time);
                    break;
                }
            }
        }

        auto stats = tracker.stats();
        collector.record_latency_stats("batch_convergence_10_nodes", stats);

        INFO("Batch convergence (10 nodes) - Mean: " << stats.mean_ms() << " ms");
    }

    SECTION("Convergence under concurrent updates") {
        MultiAgentFixture fixture;
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture.create_agents(4, config_file));
        fixture.wait_for_sync();

        LatencyTracker tracker(50);

        // Each agent creates nodes concurrently
        for (int round = 0; round < 50; ++round) {
            std::vector<uint64_t> all_node_ids;
            std::mutex ids_mutex;

            uint64_t start = get_unix_timestamp();

            // Each agent creates 5 nodes in parallel
            std::vector<std::thread> threads;
            for (size_t agent_idx = 0; agent_idx < 4; ++agent_idx) {
                threads.emplace_back([&, agent_idx]() {
                    auto* agent = fixture.get_agent(agent_idx);
                    for (int i = 0; i < 5; ++i) {
                        auto node = GraphGenerator::create_test_node(
                            0, agent->get_agent_id());
                        auto result = agent->insert_node(node);
                        if (result.has_value()) {
                            std::lock_guard<std::mutex> lock(ids_mutex);
                            all_node_ids.push_back(result.value());
                        }
                    }
                });
            }
            for (auto& t : threads) t.join();

            // Wait for all agents to see all nodes
            auto poll_start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - poll_start < std::chrono::seconds(15)) {
                fixture.process_events(5);

                bool all_converged = true;
                for (size_t agent_idx = 0; agent_idx < 4 && all_converged; ++agent_idx) {
                    auto* agent = fixture.get_agent(agent_idx);
                    for (auto id : all_node_ids) {
                        if (!agent->get_node(id).has_value()) {
                            all_converged = false;
                            break;
                        }
                    }
                }

                if (all_converged) {
                    uint64_t conv_time = get_unix_timestamp() - start;
                    tracker.record(conv_time);
                    break;
                }
            }
        }

        auto stats = tracker.stats();
        collector.record_latency_stats("concurrent_convergence_4_agents", stats);

        INFO("Concurrent convergence (4 agents) - Mean: " << stats.mean_ms() << " ms, "
             << "P99: " << stats.p99_ms() << " ms");

        // Check against timeout
        CHECK(stats.p99_ms() < 1000);  // Should converge within 1 second p99
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "convergence_time");
}

TEST_CASE("Attribute convergence", "[CONSISTENCY][convergence][attributes][.multi][PROFILE][MULTIAGENT]") {
    GraphGenerator generator;
    MetricsCollector collector("attribute_convergence");

    MultiAgentFixture fixture;
    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(2, config_file));
    fixture.wait_for_sync();

    auto* agent_a = fixture.get_agent(0);
    auto* agent_b = fixture.get_agent(1);

    // Create shared test node and capture actual ID
    auto test_node = GraphGenerator::create_test_node(
        0, agent_a->get_agent_id(), "attr_conv_test");
    auto insert_result = agent_a->insert_node(test_node);
    REQUIRE(insert_result.has_value());
    uint64_t shared_node_id = insert_result.value();

    fixture.wait_for_sync();
    REQUIRE(fixture.verify_convergence());

    SECTION("Attribute update convergence") {
        LatencyTracker tracker(100);

        for (int i = 0; i < 100; ++i) {
            auto node = agent_a->get_node(shared_node_id);
            REQUIRE(node.has_value());

            int32_t new_value = 1000 + i;
            agent_a->add_or_modify_attrib_local<level_att>(*node, new_value);

            uint64_t start = get_unix_timestamp();
            agent_a->update_node(*node);

            // Wait for attribute to converge
            auto poll_start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - poll_start < std::chrono::seconds(5)) {
                fixture.process_events(1);

                auto b_node = agent_b->get_node(shared_node_id);
                if (b_node.has_value()) {
                    auto attr = agent_b->get_attrib_by_name<level_att>(*b_node);
                    if (attr.has_value() && attr.value() == new_value) {
                        uint64_t conv_time = get_unix_timestamp() - start;
                        tracker.record(conv_time);
                        break;
                    }
                }
            }
        }

        auto stats = tracker.stats();
        collector.record_latency_stats("attribute_update_convergence", stats);

        INFO("Attribute convergence - Mean: " << stats.mean_us() << " us");
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "attribute_convergence");
}
