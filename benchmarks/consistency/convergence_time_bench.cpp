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

TEST_CASE("Convergence time benchmarks", "[CONSISTENCY][PROFILE][MULTIAGENT]") {
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

        constexpr int ITERATIONS = 50;
        constexpr uint64_t TIMEOUT_NS = 5'000'000'000ULL; // 5 s in ns
        LatencyTracker tracker(ITERATIONS);

        for (int i = 0; i < ITERATIONS; ++i) {
            auto node = GraphGenerator::create_test_node(
                0, agent_a->get_agent_id(),
                "conv_node_" + std::to_string(i));

            uint64_t start = bench_now();
            auto result = agent_a->insert_node(node);
            if (!result.has_value()) continue;
            uint64_t node_id = result.value();

            bool converged = false;
            auto poll_start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - poll_start < std::chrono::seconds(5)) {
                fixture.process_events(1);
                if (agent_b->get_node(node_id).has_value()) {
                    converged = true;
                    break;
                }
            }

            tracker.record(converged ? bench_now() - start : TIMEOUT_NS);
        }

        auto stats = tracker.stats();
        collector.record_latency_stats("single_node_convergence", stats);
        collector.record_consistency("convergence_success_rate",
            (static_cast<double>(tracker.count()) / ITERATIONS) * 100, "%");

        INFO("Single node convergence - Mean: " << stats.mean_us() << " us, "
             << "P99: " << stats.p99_us() << " us");
    }

    SECTION("Batch convergence time") {
        MultiAgentFixture fixture;
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture.create_agents(2, config_file));
        fixture.wait_for_sync();

        auto* agent_a = fixture.get_agent(0);
        auto* agent_b = fixture.get_agent(1);

        constexpr int BATCHES = 10;
        constexpr uint64_t TIMEOUT_NS = 10'000'000'000ULL;
        LatencyTracker tracker(BATCHES);

        for (int batch = 0; batch < BATCHES; ++batch) {
            std::vector<uint64_t> node_ids;
            node_ids.reserve(10);

            uint64_t start = bench_now();

            for (int i = 0; i < 10; ++i) {
                auto node = GraphGenerator::create_test_node(0, agent_a->get_agent_id());
                auto result = agent_a->insert_node(node);
                if (result.has_value()) node_ids.push_back(result.value());
            }

            bool converged = false;
            auto poll_start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - poll_start < std::chrono::seconds(10)) {
                fixture.process_events(1);

                bool all = true;
                for (auto id : node_ids) {
                    if (!agent_b->get_node(id).has_value()) { all = false; break; }
                }
                if (all) { converged = true; break; }
            }

            tracker.record(converged ? bench_now() - start : TIMEOUT_NS);
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

        constexpr int ROUNDS = 30;
        constexpr uint64_t TIMEOUT_NS = 15'000'000'000ULL;
        LatencyTracker tracker(ROUNDS);

        for (int round = 0; round < ROUNDS; ++round) {
            std::vector<uint64_t> all_node_ids;
            std::mutex ids_mutex;

            uint64_t start = bench_now();

            std::vector<std::thread> threads;
            for (size_t agent_idx = 0; agent_idx < 4; ++agent_idx) {
                threads.emplace_back([&, agent_idx]() {
                    auto* agent = fixture.get_agent(agent_idx);
                    for (int i = 0; i < 5; ++i) {
                        auto node = GraphGenerator::create_test_node(0, agent->get_agent_id());
                        auto result = agent->insert_node(node);
                        if (result.has_value()) {
                            std::lock_guard<std::mutex> lock(ids_mutex);
                            all_node_ids.push_back(result.value());
                        }
                    }
                });
            }
            for (auto& t : threads) t.join();

            bool converged = false;
            auto poll_start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - poll_start < std::chrono::seconds(15)) {
                fixture.process_events(5);

                bool all = true;
                for (size_t agent_idx = 0; agent_idx < 4 && all; ++agent_idx) {
                    auto* agent = fixture.get_agent(agent_idx);
                    for (auto id : all_node_ids) {
                        if (!agent->get_node(id).has_value()) { all = false; break; }
                    }
                }
                if (all) { converged = true; break; }
            }

            tracker.record(converged ? bench_now() - start : TIMEOUT_NS);
        }

        auto stats = tracker.stats();
        collector.record_latency_stats("concurrent_convergence_4_agents", stats);

        INFO("Concurrent convergence (4 agents) - Mean: " << stats.mean_ms() << " ms, "
             << "P99: " << stats.p99_ms() << " ms");

        CHECK(stats.p99_ms() < 1000);
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "convergence_time");
}

TEST_CASE("Attribute convergence", "[CONSISTENCY][PROFILE][MULTIAGENT]") {
    GraphGenerator generator;
    MetricsCollector collector("attribute_convergence");

    MultiAgentFixture fixture;
    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(2, config_file));
    fixture.wait_for_sync();

    auto* agent_a = fixture.get_agent(0);
    auto* agent_b = fixture.get_agent(1);

    auto test_node = GraphGenerator::create_test_node(
        0, agent_a->get_agent_id(), "attr_conv_test");
    auto insert_result = agent_a->insert_node(test_node);
    REQUIRE(insert_result.has_value());
    uint64_t shared_node_id = insert_result.value();

    fixture.wait_for_sync();
    REQUIRE(fixture.verify_convergence());

    SECTION("Attribute update convergence") {
        constexpr int ITERATIONS = 50;
        constexpr uint64_t TIMEOUT_NS = 5'000'000'000ULL;
        LatencyTracker tracker(ITERATIONS);

        for (int i = 0; i < ITERATIONS; ++i) {
            auto node = agent_a->get_node(shared_node_id);
            REQUIRE(node.has_value());

            int32_t new_value = 1000 + i;
            agent_a->add_or_modify_attrib_local<level_att>(*node, new_value);

            uint64_t start = bench_now();
            agent_a->update_node(*node);

            bool converged = false;
            auto poll_start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - poll_start < std::chrono::seconds(5)) {
                fixture.process_events(1);

                auto b_node = agent_b->get_node(shared_node_id);
                if (b_node.has_value()) {
                    auto attr = agent_b->get_attrib_by_name<level_att>(*b_node);
                    if (attr.has_value() && attr.value() == new_value) {
                        converged = true;
                        break;
                    }
                }
            }

            tracker.record(converged ? bench_now() - start : TIMEOUT_NS);
        }

        auto stats = tracker.stats();
        collector.record_latency_stats("attribute_update_convergence", stats);

        INFO("Attribute convergence - Mean: " << stats.mean_us() << " us");
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "attribute_convergence");
}
