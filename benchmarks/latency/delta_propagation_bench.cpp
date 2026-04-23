#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <atomic>
#include <latch>

#include "../core/timing_utils.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/multi_agent_fixture.h"
#include "../fixtures/graph_generator.h"

using namespace DSR;
using namespace DSR::Benchmark;

// Multi-agent tests require working DDS synchronization
// Skip these by default - run with "[DELTA]" tag explicitly to test
TEST_CASE("Delta propagation latency between agents", "[LATENCY][DELTA][PROFILE][MULTIAGENT]") {
    // Setup
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("delta_propagation");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(2, config_file));

    // Wait for DDS discovery and initial sync
    fixture.wait_for_sync(std::chrono::milliseconds(500));
    REQUIRE(fixture.verify_convergence(std::chrono::seconds(10)));

    auto* agent_a = fixture.get_agent(0);
    auto* agent_b = fixture.get_agent(1);
    REQUIRE(agent_a != nullptr);
    REQUIRE(agent_b != nullptr);

    SECTION("Node insertion propagation latency") {
        LatencyTracker tracker(100);
        std::atomic<uint64_t> receive_time{0};
        std::atomic<bool> received{false};
        std::atomic<uint64_t> expected_node_id{0};

        // Connect to agent B's signal
        QObject::connect(agent_b, &DSR::DSRGraph::update_node_signal, agent_b,
            [&](uint64_t id, const std::string& type, DSR::SignalInfo) {
                if (id == expected_node_id.load(std::memory_order_acquire)) {
                    receive_time.store(bench_now());
                    received.store(true);
                }
            }, Qt::DirectConnection);

        // Warmup
        for (int i = 0; i < 10; ++i) {
            auto node = GraphGenerator::create_test_node(
                2000 + i, agent_a->get_agent_id(), "warmup_" + std::to_string(i));
            agent_a->insert_node(node);
            fixture.wait_for_sync(std::chrono::milliseconds(50));
        }

        // Measurement iterations
        for (int i = 0; i < 100; ++i) {
            received.store(false);

            auto node = GraphGenerator::create_test_node(
                expected_node_id, agent_a->get_agent_id(),
                "bench_node_" + std::to_string(i));

            uint64_t send_time = bench_now();
            auto ins_result = agent_a->insert_node(node);
            REQUIRE(ins_result.has_value());
            expected_node_id.store(ins_result.value(), std::memory_order_release);
            
            // Wait for signal with timeout
            auto start = std::chrono::steady_clock::now();
            while (!received.load()) {
                fixture.process_events(1);
                if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
                    FAIL("Timeout waiting for node propagation");
                }
            }

            uint64_t latency = receive_time.load() - send_time;
            tracker.record(latency);
        }

        auto stats = tracker.stats();
        collector.record_latency_stats("node_propagation", stats);

        INFO("Node propagation latency - Mean: " << stats.mean_us() << " us, "
             << "P99: " << stats.p99_us() << " us");

        // Validation
        CHECK(stats.p99_ns < MAX_EXPECTED_LATENCY_NS);
    }

    SECTION("Edge insertion propagation latency") {
        LatencyTracker tracker(100);
        std::atomic<uint64_t> receive_time{0};
        std::atomic<bool> received{false};

        // First create nodes on agent A
        auto root = agent_a->get_node_root();
        REQUIRE(root.has_value());

        std::vector<uint64_t> node_to_ids = {};

        for (int i = 0; i < 110; ++i) {
            auto node = GraphGenerator::create_test_node(
                4000 + i, agent_a->get_agent_id(), "edge_node_" + std::to_string(i));
            auto ins = agent_a->insert_node(node);
            REQUIRE(ins.has_value());
            node_to_ids.push_back(ins.value());
        }

        // Wait for all nodes to sync to agent B before creating edges
        fixture.wait_for_sync(std::chrono::milliseconds(500));
        REQUIRE(fixture.verify_convergence(std::chrono::seconds(10)));

        // Connect to agent B's edge signal
        std::atomic<uint64_t> expected_from{0};
        std::atomic<uint64_t> expected_to{0};
        QObject::connect(agent_b, &DSR::DSRGraph::update_edge_signal, agent_b,
            [&](uint64_t from, uint64_t to, const std::string& type, DSR::SignalInfo) {
                if (from == expected_from.load(std::memory_order_acquire) &&
                    to   == expected_to.load(std::memory_order_acquire)) {
                    receive_time.store(bench_now());
                    received.store(true);
                }
            }, Qt::DirectConnection);

        // Warmup
        for (int i = 0; i < 10; ++i) {
            auto edge = GraphGenerator::create_test_edge(
                root->id(), node_to_ids[i], agent_a->get_agent_id());
            agent_a->insert_or_assign_edge(edge);
            fixture.wait_for_sync(std::chrono::milliseconds(50));
        }

        // Measurement iterations
        for (int i = 10; i < 110; ++i) {
            expected_from.store(root->id(), std::memory_order_release);
            expected_to.store(node_to_ids[i], std::memory_order_release);
            received.store(false);

            auto edge = GraphGenerator::create_test_edge(
                expected_from, expected_to, agent_a->get_agent_id());

            uint64_t send_time = bench_now();
            agent_a->insert_or_assign_edge(edge);

            // Wait for signal with timeout
            auto start = std::chrono::steady_clock::now();
            while (!received.load()) {
                fixture.process_events(1);
                if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
                    FAIL("Timeout waiting for edge propagation");
                }
            }

            uint64_t latency = receive_time.load() - send_time;
            tracker.record(latency);
        }

        auto stats = tracker.stats();
        collector.record_latency_stats("edge_propagation", stats);

        INFO("Edge propagation latency - Mean: " << stats.mean_us() << " us, "
             << "P99: " << stats.p99_us() << " us");

        CHECK(stats.p99_ns < MAX_EXPECTED_LATENCY_NS);
    }

    SECTION("Attribute update propagation latency") {
        LatencyTracker tracker(100);
        std::atomic<uint64_t> receive_time{0};
        std::atomic<bool> received{false};

        // Create a node for attribute updates
        auto test_node = GraphGenerator::create_test_node(
            5000, agent_a->get_agent_id(), "attr_test_node");
        auto insert_result = agent_a->insert_node(test_node);
        REQUIRE(insert_result.has_value());

        // Wait for sync to agent B
        fixture.wait_for_sync(std::chrono::milliseconds(500));
        REQUIRE(fixture.verify_convergence(std::chrono::seconds(10)));

        // Verify node exists on agent A
        auto check_node = agent_a->get_node(*insert_result);
        REQUIRE(check_node.has_value());

        // Connect to agent B's attribute signal
        QObject::connect(agent_b, &DSR::DSRGraph::update_node_attr_signal, agent_b,
            [&](uint64_t id, const std::vector<std::string>& att_names, DSR::SignalInfo) {
                if (id == *insert_result) {
                    receive_time.store(bench_now());
                    received.store(true);
                }
            }, Qt::DirectConnection);

        // Warmup
        for (int i = 0; i < 10; ++i) {
            auto node = agent_a->get_node(*insert_result);
            if (node) {
                agent_a->add_or_modify_attrib_local<level_att>(*node, static_cast<int32_t>(i));
                agent_a->update_node(*node);
            }
            fixture.wait_for_sync(std::chrono::milliseconds(50));
        }

        // Measurement iterations
        for (int i = 0; i < 100; ++i) {
            received.store(false);

            auto node = agent_a->get_node(*insert_result);
            REQUIRE(node.has_value());

            agent_a->add_or_modify_attrib_local<level_att>(*node, static_cast<int32_t>(1000 + i));

            uint64_t send_time = bench_now();
            agent_a->update_node(*node);

            // Wait for signal with timeout
            auto start = std::chrono::steady_clock::now();
            while (!received.load()) {
                fixture.process_events(1);
                if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
                    FAIL("Timeout waiting for attribute propagation");
                }
            }

            uint64_t latency = receive_time.load() - send_time;
            tracker.record(latency);
        }

        auto stats = tracker.stats();
        collector.record_latency_stats("attribute_propagation", stats);

        INFO("Attribute propagation latency - Mean: " << stats.mean_us() << " us, "
             << "P99: " << stats.p99_us() << " us");

        CHECK(stats.p99_ns < MAX_EXPECTED_LATENCY_NS);
    }

    // Export results
    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "delta_propagation");
}

TEST_CASE("Delta propagation with varying agent counts", "[LATENCY][DELTA][scalability][PROFILE][MULTIAGENT]") {
    MetricsCollector collector("delta_propagation_scaling");
    GraphGenerator generator;

    for (uint32_t num_agents : {2, 4}) {
        SECTION("With " + std::to_string(num_agents) + " agents") {
            MultiAgentFixture fixture;
            auto config_file = generator.generate_empty_graph();

            if (!fixture.create_agents(num_agents, config_file)) {
                WARN("Could not create " << num_agents << " agents, skipping");
                continue;
            }

            // Wait for DDS discovery with all agents
            fixture.wait_for_sync(std::chrono::milliseconds(500 * num_agents));
            if (!fixture.verify_convergence(std::chrono::seconds(15))) {
                WARN("Agents failed to converge, skipping");
                continue;
            }

            auto* sender = fixture.get_agent(0);
            REQUIRE(sender != nullptr);

            LatencyTracker tracker(50);

            // Track reception across all other agents
            std::atomic<uint32_t> received_count{0};
            std::vector<std::atomic<uint64_t>> receive_times(num_agents - 1);
            std::atomic<uint64_t> current_expected_id{0};

            for (size_t i = 1; i < num_agents; ++i) {
                auto* receiver = fixture.get_agent(i);
                QObject::connect(receiver, &DSR::DSRGraph::update_node_signal, receiver,
                    [&, idx = i - 1](uint64_t id, const std::string& type, DSR::SignalInfo) {
                        if (id == current_expected_id.load()) {
                            receive_times[idx].store(bench_now());
                            received_count.fetch_add(1);
                        }
                    }, Qt::DirectConnection);
            }

            // Measurement
            for (int i = 0; i < 50; ++i) {
                received_count.store(0);
                for (auto& rt : receive_times) rt.store(0);

                auto node = GraphGenerator::create_test_node(
                    0, sender->get_agent_id(),
                    "scale_node_" + std::to_string(i));

                uint64_t send_time = bench_now();
                auto result = sender->insert_node(node);
                REQUIRE(result.has_value());
                current_expected_id.store(result.value());

                // Wait for all receivers
                auto start = std::chrono::steady_clock::now();
                while (received_count.load() < num_agents - 1) {
                    fixture.process_events(1);
                    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(10)) {
                        break;
                    }
                }

                // Record max latency (time for all to receive)
                uint64_t max_receive = 0;
                for (const auto& rt : receive_times) {
                    max_receive = std::max(max_receive, rt.load());
                }
                if (max_receive > 0) {
                    tracker.record(max_receive - send_time);
                }
            }

            auto stats = tracker.stats();
            collector.record_latency_stats(
                "propagation_" + std::to_string(num_agents) + "_agents",
                stats,
                {{"num_agents", std::to_string(num_agents)}});

            INFO(num_agents << " agents - Mean: " << stats.mean_us() << " us, "
                 << "P99: " << stats.p99_us() << " us");
        }
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "delta_propagation_scaling");
}
