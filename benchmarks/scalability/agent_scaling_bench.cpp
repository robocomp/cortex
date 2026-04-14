#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <atomic>
#include <barrier>
#include <vector>
#include <chrono>
#include <string>
#include <iostream>

#include "../core/timing_utils.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/multi_agent_fixture.h"
#include "../fixtures/graph_generator.h"

using namespace DSR;
using namespace DSR::Benchmark;
using namespace std::chrono;

// Multi-agent scaling benchmarks.  Tagged [.multi] so they are excluded from
// the default test run (DDS multi-agent tests are slow and require specific
// network setup).  Opt in with: --cpp-filter "[SCALABILITY][agents]"
//
// Loop over {1, 2, 4} agents.  One thread per agent operates on its own
// DSRGraph instance; a 3-second window measures total throughput and latency.

static constexpr auto AGENT_DUR = std::chrono::seconds(3);

// ── Node insert ───────────────────────────────────────────────────────────────

TEST_CASE("Node insert agent scaling", "[SCALABILITY][agents][.multi]") {
    GraphGenerator generator;
    MetricsCollector collector("node_insert_agent_scaling");

    for (uint32_t N : {1u, 2u, 4u}) {
        MultiAgentFixture fixture;
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture.create_agents(N, config_file));
        fixture.wait_for_sync();

        std::atomic<uint64_t> total_ops{0};
        std::atomic<uint64_t> failed_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(N);

        std::vector<std::vector<uint64_t>> per_thread_samples(N);
        for (auto& s : per_thread_samples) s.reserve(500000 / N);

        std::vector<std::thread> threads;
        threads.reserve(N);

        auto wall_start = steady_clock::now();

        for (uint32_t i = 0; i < N; ++i) {
            threads.emplace_back([&, agent_idx = i]() {
                auto* graph = fixture.get_agent(agent_idx);
                uint64_t base_id = 800000ULL + agent_idx * 200000ULL;
                uint64_t local_ops = 0;
                auto& samples = per_thread_samples[agent_idx];

                sync_point.arrive_and_wait();

                while (!stop_flag.load(std::memory_order_relaxed)) {
                    auto node = GraphGenerator::create_test_node(
                        base_id + local_ops, graph->get_agent_id());
                    uint64_t ts = bench_now();
                    auto res = graph->insert_node(node);
                    samples.push_back(bench_now() - ts);
                    if (!res.has_value())
                        failed_ops.fetch_add(1, std::memory_order_relaxed);
                    local_ops++;
                }

                total_ops.fetch_add(local_ops, std::memory_order_relaxed);
            });
        }

        std::this_thread::sleep_for(AGENT_DUR);
        stop_flag.store(true, std::memory_order_relaxed);
        for (auto& th : threads) th.join();

        if (failed_ops.load() > 0)
            std::cerr << "[BENCH node_insert agents=" << N << "] "
                      << failed_ops.load() << " insert_node calls failed\n";

        auto dur = duration_cast<milliseconds>(steady_clock::now() - wall_start);

        LatencyTracker merged;
        for (auto& s : per_thread_samples)
            for (auto v : s) merged.record(v);

        const std::string n_str = std::to_string(N);
        collector.record_throughput("node_insert", total_ops.load(), dur,
            {{"agents", n_str}});
        if (!merged.empty())
            collector.record_latency_stats("node_insert", merged.stats(),
                {{"agents", n_str}});

        double ops_per_sec = static_cast<double>(total_ops.load()) /
                             (static_cast<double>(dur.count()) / 1000.0);
        collector.record_scalability("node_insert", N, ops_per_sec, "ops/sec",
            {{"agents", n_str}, {"scale_dim", "agents"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_insert_agent_scaling");
}

// ── Node read ─────────────────────────────────────────────────────────────────

TEST_CASE("Node read agent scaling", "[SCALABILITY][agents][.multi]") {
    GraphGenerator generator;
    MetricsCollector collector("node_read_agent_scaling");

    for (uint32_t N : {1u, 2u, 4u}) {
        MultiAgentFixture fixture;
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture.create_agents(N, config_file));
        fixture.wait_for_sync();

        // Pre-populate 1000 nodes on agent 0; they sync to all agents.
        auto* graph0 = fixture.get_agent(0);
        std::vector<uint64_t> node_ids;
        node_ids.reserve(1000);
        for (uint64_t i = 0; i < 1000; ++i) {
            auto node = GraphGenerator::create_test_node(0, graph0->get_agent_id());
            auto res = graph0->insert_node(node);
            REQUIRE(res.has_value());
            node_ids.push_back(res.value());
        }
        fixture.wait_for_sync();

        const size_t pool_size = node_ids.size();

        std::atomic<uint64_t> total_ops{0};
        std::atomic<uint64_t> failed_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(N);

        std::vector<std::vector<uint64_t>> per_thread_samples(N);
        for (auto& s : per_thread_samples) s.reserve(500000 / N);

        std::vector<std::thread> threads;
        threads.reserve(N);

        auto wall_start = steady_clock::now();

        for (uint32_t i = 0; i < N; ++i) {
            threads.emplace_back([&, agent_idx = i]() {
                auto* graph = fixture.get_agent(agent_idx);
                uint64_t local_ops = 0;
                auto& samples = per_thread_samples[agent_idx];

                sync_point.arrive_and_wait();

                while (!stop_flag.load(std::memory_order_relaxed)) {
                    uint64_t id = node_ids[local_ops % pool_size];
                    uint64_t ts = bench_now();
                    auto node = graph->get_node(id);
                    samples.push_back(bench_now() - ts);
                    if (!node.has_value())
                        failed_ops.fetch_add(1, std::memory_order_relaxed);
                    local_ops++;
                }

                total_ops.fetch_add(local_ops, std::memory_order_relaxed);
            });
        }

        std::this_thread::sleep_for(AGENT_DUR);
        stop_flag.store(true, std::memory_order_relaxed);
        for (auto& th : threads) th.join();

        if (failed_ops.load() > 0)
            std::cerr << "[BENCH node_read agents=" << N << "] "
                      << failed_ops.load() << " get_node calls returned empty\n";

        auto dur = duration_cast<milliseconds>(steady_clock::now() - wall_start);

        LatencyTracker merged;
        for (auto& s : per_thread_samples)
            for (auto v : s) merged.record(v);

        const std::string n_str = std::to_string(N);
        collector.record_throughput("node_read", total_ops.load(), dur,
            {{"agents", n_str}});
        if (!merged.empty())
            collector.record_latency_stats("node_read", merged.stats(),
                {{"agents", n_str}});

        double ops_per_sec = static_cast<double>(total_ops.load()) /
                             (static_cast<double>(dur.count()) / 1000.0);
        collector.record_scalability("node_read", N, ops_per_sec, "ops/sec",
            {{"agents", n_str}, {"scale_dim", "agents"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_read_agent_scaling");
}

// ── Node update ───────────────────────────────────────────────────────────────

TEST_CASE("Node update agent scaling", "[SCALABILITY][agents][.multi]") {
    GraphGenerator generator;
    MetricsCollector collector("node_update_agent_scaling");

    for (uint32_t N : {1u, 2u, 4u}) {
        MultiAgentFixture fixture;
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture.create_agents(N, config_file));
        fixture.wait_for_sync();

        // Each agent gets its own dedicated node to avoid update contention.
        std::vector<uint64_t> agent_node_ids(N);
        for (uint32_t i = 0; i < N; ++i) {
            auto* graph = fixture.get_agent(i);
            auto node = GraphGenerator::create_test_node(
                700000 + i, graph->get_agent_id(),
                "agent_update_node_" + std::to_string(i));
            auto res = graph->insert_node(node);
            REQUIRE(res.has_value());
            agent_node_ids[i] = res.value();
        }
        fixture.wait_for_sync();

        std::atomic<uint64_t> total_ops{0};
        std::atomic<uint64_t> failed_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(N);

        std::vector<std::vector<uint64_t>> per_thread_samples(N);
        for (auto& s : per_thread_samples) s.reserve(500000 / N);

        std::vector<std::thread> threads;
        threads.reserve(N);

        auto wall_start = steady_clock::now();

        for (uint32_t i = 0; i < N; ++i) {
            threads.emplace_back([&, agent_idx = i]() {
                auto* graph = fixture.get_agent(agent_idx);
                uint64_t nid = agent_node_ids[agent_idx];
                uint64_t local_ops = 0;
                auto& samples = per_thread_samples[agent_idx];

                sync_point.arrive_and_wait();

                while (!stop_flag.load(std::memory_order_relaxed)) {
                    auto node = graph->get_node(nid);
                    if (node) {
                        graph->add_or_modify_attrib_local<level_att>(
                            *node, static_cast<int32_t>(local_ops % 1000));
                        uint64_t ts = bench_now();
                        bool ok = graph->update_node(*node);
                        samples.push_back(bench_now() - ts);
                        if (!ok)
                            failed_ops.fetch_add(1, std::memory_order_relaxed);
                        local_ops++;
                    } else {
                        failed_ops.fetch_add(1, std::memory_order_relaxed);
                    }
                }

                total_ops.fetch_add(local_ops, std::memory_order_relaxed);
            });
        }

        std::this_thread::sleep_for(AGENT_DUR);
        stop_flag.store(true, std::memory_order_relaxed);
        for (auto& th : threads) th.join();

        if (failed_ops.load() > 0)
            std::cerr << "[BENCH node_update agents=" << N << "] "
                      << failed_ops.load() << " get_node/update_node calls failed\n";

        auto dur = duration_cast<milliseconds>(steady_clock::now() - wall_start);

        LatencyTracker merged;
        for (auto& s : per_thread_samples)
            for (auto v : s) merged.record(v);

        const std::string n_str = std::to_string(N);
        collector.record_throughput("node_update", total_ops.load(), dur,
            {{"agents", n_str}});
        if (!merged.empty())
            collector.record_latency_stats("node_update", merged.stats(),
                {{"agents", n_str}});

        double ops_per_sec = static_cast<double>(total_ops.load()) /
                             (static_cast<double>(dur.count()) / 1000.0);
        collector.record_scalability("node_update", N, ops_per_sec, "ops/sec",
            {{"agents", n_str}, {"scale_dim", "agents"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_update_agent_scaling");
}
