#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <atomic>
#include <thread>
#include <barrier>

#include "../core/timing_utils.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/multi_agent_fixture.h"
#include "../fixtures/graph_generator.h"

using namespace DSR;
using namespace DSR::Benchmark;

TEST_CASE("Conflict rate benchmarks", "[CONSISTENCY][conflict][.multi][PROFILE][MULTIAGENT]") {
    GraphGenerator generator;
    MetricsCollector collector("conflict_rate");

    SECTION("Concurrent attribute updates - same node") {
        MultiAgentFixture fixture;
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture.create_agents(4, config_file));
        fixture.wait_for_sync();

        // Create shared node and capture actual ID
        auto* agent_0 = fixture.get_agent(0);
        auto shared_node = GraphGenerator::create_test_node(
            0, agent_0->get_agent_id(), "conflict_test");
        auto insert_result = agent_0->insert_node(shared_node);
        REQUIRE(insert_result.has_value());
        uint64_t shared_node_id = insert_result.value();

        fixture.wait_for_sync();
        REQUIRE(fixture.verify_convergence());

        constexpr int NUM_ROUNDS = 50;
        constexpr int UPDATES_PER_AGENT = 10;
        constexpr size_t NUM_AGENTS = 4;

        std::atomic<uint64_t> total_updates{0};
        uint64_t conflicts_detected = 0;

        std::barrier sync_point(NUM_AGENTS);

        for (int round = 0; round < NUM_ROUNDS; ++round) {
            std::vector<std::thread> threads;
            threads.reserve(NUM_AGENTS);

            // Record initial values before concurrent updates
            std::vector<int32_t> expected_values(NUM_AGENTS);
            for (size_t i = 0; i < NUM_AGENTS; ++i) {
                expected_values[i] = static_cast<int32_t>(round * 1000 + i * 100);
            }

            for (size_t agent_idx = 0; agent_idx < NUM_AGENTS; ++agent_idx) {
                threads.emplace_back([&, agent_idx, node_id = shared_node_id]() {
                    auto* agent = fixture.get_agent(agent_idx);
                    sync_point.arrive_and_wait();

                    for (int i = 0; i < UPDATES_PER_AGENT; ++i) {
                        auto node = agent->get_node(node_id);
                        if (node) {
                            int32_t value = static_cast<int32_t>(
                                round * 1000 + agent_idx * 100 + i);
                            agent->add_or_modify_attrib_local<level_att>(*node, value);
                            agent->update_node(*node);
                            total_updates.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                });
            }

            for (auto& t : threads) {
                t.join();
            }

            // Wait for convergence
            fixture.wait_for_sync(std::chrono::milliseconds(500));

            // Check if all agents converged to the same value
            std::set<int32_t> final_values;
            for (size_t i = 0; i < NUM_AGENTS; ++i) {
                auto* agent = fixture.get_agent(i);
                auto node = agent->get_node(shared_node_id);
                if (node) {
                    auto attr = agent->get_attrib_by_name<level_att>(*node);
                    if (attr.has_value()) {
                        final_values.insert(attr.value());
                    }
                }
            }

            // If agents have different values, conflict resolution may still be in progress
            // or there was a conflict that resolved differently
            if (final_values.size() > 1) {
                conflicts_detected++;
            }
        }

        double conflict_rate = static_cast<double>(conflicts_detected) /
                              static_cast<double>(NUM_ROUNDS) * 100.0;

        collector.record_consistency("concurrent_update_conflict_rate",
            conflict_rate, "%",
            {{"num_agents", std::to_string(NUM_AGENTS)},
             {"updates_per_round", std::to_string(UPDATES_PER_AGENT * NUM_AGENTS)}});

        INFO("Conflict rate: " << conflict_rate << "% (" << conflicts_detected
             << "/" << NUM_ROUNDS << " rounds)");
        INFO("Total updates: " << total_updates.load());

        // Verify final convergence
        fixture.wait_for_sync(std::chrono::milliseconds(1000));
        CHECK(fixture.verify_convergence());
    }

    SECTION("Concurrent node creations - potential ID conflicts") {
        // This tests CRDT behavior when multiple agents create nodes
        MultiAgentFixture fixture;
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture.create_agents(4, config_file));
        fixture.wait_for_sync();

        constexpr int NODES_PER_AGENT = 100;
        constexpr size_t NUM_AGENTS = 4;

        std::atomic<uint64_t> total_created{0};
        std::atomic<uint64_t> creation_failures{0};

        std::barrier sync_point(NUM_AGENTS);
        std::vector<std::thread> threads;
        threads.reserve(NUM_AGENTS);

        for (size_t agent_idx = 0; agent_idx < NUM_AGENTS; ++agent_idx) {
            threads.emplace_back([&, agent_idx]() {
                auto* agent = fixture.get_agent(agent_idx);
                sync_point.arrive_and_wait();

                for (int i = 0; i < NODES_PER_AGENT; ++i) {
                    // Each agent uses unique IDs in its range
                    uint64_t node_id = 8500000 + agent_idx * 10000 + i;
                    auto node = GraphGenerator::create_test_node(
                        node_id, agent->get_agent_id(),
                        "agent" + std::to_string(agent_idx) + "_node" + std::to_string(i));

                    auto result = agent->insert_node(node);
                    if (result.has_value()) {
                        total_created.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        creation_failures.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        // Wait for convergence
        fixture.wait_for_sync(std::chrono::milliseconds(2000));

        // Verify all agents have the same nodes
        auto* agent_0 = fixture.get_agent(0);
        size_t expected_node_count = agent_0->get_nodes().size();

        bool all_match = true;
        for (size_t i = 1; i < NUM_AGENTS; ++i) {
            auto* agent = fixture.get_agent(i);
            if (agent->get_nodes().size() != expected_node_count) {
                all_match = false;
            }
        }

        collector.record_consistency("node_creation_success_rate",
            static_cast<double>(total_created.load()) /
            static_cast<double>(NODES_PER_AGENT * NUM_AGENTS) * 100.0, "%");

        collector.record_consistency("final_convergence",
            all_match ? 100.0 : 0.0, "%");

        INFO("Created: " << total_created.load() << "/" << NODES_PER_AGENT * NUM_AGENTS);
        INFO("Failures: " << creation_failures.load());
        INFO("All agents converged: " << (all_match ? "yes" : "no"));

        CHECK(fixture.verify_convergence());
    }

    SECTION("Edge conflict resolution") {
        MultiAgentFixture fixture;
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture.create_agents(2, config_file));
        fixture.wait_for_sync();

        auto* agent_a = fixture.get_agent(0);
        auto* agent_b = fixture.get_agent(1);

        // Create shared nodes and capture actual IDs
        auto node1 = GraphGenerator::create_test_node(0, agent_a->get_agent_id(), "edge_node_1");
        auto node2 = GraphGenerator::create_test_node(0, agent_a->get_agent_id(), "edge_node_2");
        auto result1 = agent_a->insert_node(node1);
        auto result2 = agent_a->insert_node(node2);
        REQUIRE(result1.has_value());
        REQUIRE(result2.has_value());
        uint64_t node1_id = result1.value();
        uint64_t node2_id = result2.value();

        fixture.wait_for_sync();
        REQUIRE(fixture.verify_convergence());

        uint64_t conflicts = 0;
        constexpr int NUM_ROUNDS = 50;

        for (int round = 0; round < NUM_ROUNDS; ++round) {
            // Both agents try to create the same edge simultaneously
            auto edge_a = GraphGenerator::create_test_edge(
                node1_id, node2_id, agent_a->get_agent_id(), "test_edge");
            auto edge_b = GraphGenerator::create_test_edge(
                node1_id, node2_id, agent_b->get_agent_id(), "test_edge");

            std::thread ta([&]() { agent_a->insert_or_assign_edge(edge_a); });
            std::thread tb([&]() { agent_b->insert_or_assign_edge(edge_b); });

            ta.join();
            tb.join();

            fixture.wait_for_sync(std::chrono::milliseconds(200));

            // Check both agents see the edge
            auto edge_on_a = agent_a->get_edge(node1_id, node2_id, "test_edge");
            auto edge_on_b = agent_b->get_edge(node1_id, node2_id, "test_edge");

            if (!edge_on_a.has_value() || !edge_on_b.has_value()) {
                conflicts++;
            }

            // Delete edge for next round
            agent_a->delete_edge(node1_id, node2_id, "test_edge");
            fixture.wait_for_sync(std::chrono::milliseconds(100));
        }

        double conflict_rate = static_cast<double>(conflicts) /
                              static_cast<double>(NUM_ROUNDS) * 100.0;

        collector.record_consistency("edge_conflict_rate",
            conflict_rate, "%");

        INFO("Edge conflict rate: " << conflict_rate << "%");
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "conflict_rate");
}

TEST_CASE("CRDT eventual consistency verification", "[CONSISTENCY][eventual][.multi][PROFILE][MULTIAGENT]") {
    GraphGenerator generator;
    MetricsCollector collector("eventual_consistency");

    MultiAgentFixture fixture;
    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(4, config_file));
    fixture.wait_for_sync();

    SECTION("All agents eventually converge after chaos") {
        constexpr size_t NUM_AGENTS = 4;
        constexpr int OPS_PER_AGENT = 50;

        std::barrier sync_point(NUM_AGENTS);
        std::atomic<bool> stop_flag{false};

        // Each agent performs random operations
        std::vector<std::thread> threads;
        for (size_t agent_idx = 0; agent_idx < NUM_AGENTS; ++agent_idx) {
            threads.emplace_back([&, agent_idx]() {
                auto* agent = fixture.get_agent(agent_idx);
                uint64_t base_id = 8700000 + agent_idx * 10000;

                sync_point.arrive_and_wait();

                for (int i = 0; i < OPS_PER_AGENT && !stop_flag.load(); ++i) {
                    int op = i % 3;

                    if (op == 0) {
                        // Insert node
                        auto node = GraphGenerator::create_test_node(
                            base_id + i, agent->get_agent_id());
                        agent->insert_node(node);
                    } else if (op == 1) {
                        // Update existing node
                        auto node = agent->get_node(base_id + (i % (std::max(1, i / 2))));
                        if (node) {
                            agent->add_or_modify_attrib_local<level_att>(
                                *node, static_cast<int32_t>(i));
                            agent->update_node(*node);
                        }
                    } else {
                        // Insert edge
                        auto root = agent->get_node_root();
                        if (root) {
                            auto existing = agent->get_node(base_id + (i % (std::max(1, i / 2))));
                            if (existing) {
                                auto edge = GraphGenerator::create_test_edge(
                                    root->id(), existing->id(), agent->get_agent_id());
                                agent->insert_or_assign_edge(edge);
                            }
                        }
                    }

                    // Small delay between operations
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        // Wait for eventual consistency
        INFO("Waiting for eventual consistency...");

        auto start = std::chrono::steady_clock::now();
        bool converged = fixture.verify_convergence(std::chrono::seconds(30));
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        collector.record_consistency("eventual_consistency_achieved",
            converged ? 100.0 : 0.0, "%");
        collector.record_consistency("convergence_duration_after_chaos",
            static_cast<double>(duration.count()), "ms");

        INFO("Convergence " << (converged ? "achieved" : "FAILED")
             << " in " << duration.count() << " ms");

        CHECK(converged);

        if (converged) {
            // Verify all agents have same node count
            auto* agent_0 = fixture.get_agent(0);
            size_t node_count = agent_0->get_nodes().size();

            for (size_t i = 1; i < NUM_AGENTS; ++i) {
                auto* agent = fixture.get_agent(i);
                CHECK(agent->get_nodes().size() == node_count);
            }
        }
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "eventual_consistency");
}
