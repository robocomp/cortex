#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <atomic>
#include <barrier>
#include <vector>
#include <chrono>
#include <string>

#include "../core/timing_utils.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/multi_agent_fixture.h"
#include "../fixtures/graph_generator.h"

using namespace DSR;
using namespace DSR::Benchmark;
using namespace std::chrono;

// Measures throughput + latency across {1, 2, 4, 8} threads for each
// operation.  Each iteration runs a 5-second window; per-thread raw latency
// samples are merged into a single LatencyTracker for aggregate stats.
// A record_scalability() entry is added so the Scalability tab can plot
// the efficiency curve (scale_dim = "threads").

static constexpr auto THREAD_DUR = std::chrono::seconds(5);

// ── Node insert ───────────────────────────────────────────────────────────────

TEST_CASE("Node insert thread scaling", "[SCALABILITY][threads]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("node_insert_thread_scaling");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    for (uint32_t N : {1u, 2u, 4u, 8u}) {
        std::atomic<uint64_t> total_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(N);

        std::vector<std::vector<uint64_t>> per_thread_samples(N);
        for (auto& s : per_thread_samples) s.reserve(2000000 / N);

        std::vector<std::thread> threads;
        threads.reserve(N);

        auto wall_start = steady_clock::now();

        for (uint32_t t = 0; t < N; ++t) {
            threads.emplace_back([&, tid = t]() {
                uint64_t base_id = 200000ULL + tid * 200000ULL;
                uint64_t local_ops = 0;
                auto& samples = per_thread_samples[tid];

                sync_point.arrive_and_wait();

                while (!stop_flag.load(std::memory_order_relaxed)) {
                    auto node = GraphGenerator::create_test_node(
                        base_id + local_ops, graph->get_agent_id());
                    uint64_t ts = bench_now();
                    graph->insert_node(node);
                    samples.push_back(bench_now() - ts);
                    local_ops++;
                }

                total_ops.fetch_add(local_ops, std::memory_order_relaxed);
            });
        }

        std::this_thread::sleep_for(THREAD_DUR);
        stop_flag.store(true, std::memory_order_relaxed);
        for (auto& th : threads) th.join();

        auto dur = duration_cast<milliseconds>(steady_clock::now() - wall_start);

        LatencyTracker merged;
        for (auto& s : per_thread_samples)
            for (auto v : s) merged.record(v);

        const std::string n_str = std::to_string(N);
        collector.record_throughput("node_insert", total_ops.load(), dur,
            {{"threads", n_str}});
        if (!merged.empty())
            collector.record_latency_stats("node_insert", merged.stats(),
                {{"threads", n_str}});

        double ops_per_sec = static_cast<double>(total_ops.load()) /
                             (static_cast<double>(dur.count()) / 1000.0);
        collector.record_scalability("node_insert", N, ops_per_sec, "ops/sec",
            {{"threads", n_str}, {"scale_dim", "threads"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_insert_thread_scaling");
}

// ── Node read ─────────────────────────────────────────────────────────────────

TEST_CASE("Node read thread scaling", "[SCALABILITY][threads]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("node_read_thread_scaling");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    // Pre-populate once; all thread-count iterations share this pool.
    std::vector<uint64_t> node_ids;
    node_ids.reserve(1000);
    for (uint64_t i = 0; i < 1000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        if (res.has_value()) node_ids.push_back(res.value());
    }
    REQUIRE(!node_ids.empty());
    const size_t pool_size = node_ids.size();

    for (uint32_t N : {1u, 2u, 4u, 8u}) {
        std::atomic<uint64_t> total_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(N);

        std::vector<std::vector<uint64_t>> per_thread_samples(N);
        for (auto& s : per_thread_samples) s.reserve(2000000 / N);

        std::vector<std::thread> threads;
        threads.reserve(N);

        auto wall_start = steady_clock::now();

        for (uint32_t t = 0; t < N; ++t) {
            threads.emplace_back([&, tid = t]() {
                uint64_t local_ops = 0;
                auto& samples = per_thread_samples[tid];

                sync_point.arrive_and_wait();

                while (!stop_flag.load(std::memory_order_relaxed)) {
                    uint64_t id = node_ids[local_ops % pool_size];
                    uint64_t ts = bench_now();
                    auto node = graph->get_node(id);
                    samples.push_back(bench_now() - ts);
                    local_ops++;
                }

                total_ops.fetch_add(local_ops, std::memory_order_relaxed);
            });
        }

        std::this_thread::sleep_for(THREAD_DUR);
        stop_flag.store(true, std::memory_order_relaxed);
        for (auto& th : threads) th.join();

        auto dur = duration_cast<milliseconds>(steady_clock::now() - wall_start);

        LatencyTracker merged;
        for (auto& s : per_thread_samples)
            for (auto v : s) merged.record(v);

        const std::string n_str = std::to_string(N);
        collector.record_throughput("node_read", total_ops.load(), dur,
            {{"threads", n_str}});
        if (!merged.empty())
            collector.record_latency_stats("node_read", merged.stats(),
                {{"threads", n_str}});

        double ops_per_sec = static_cast<double>(total_ops.load()) /
                             (static_cast<double>(dur.count()) / 1000.0);
        collector.record_scalability("node_read", N, ops_per_sec, "ops/sec",
            {{"threads", n_str}, {"scale_dim", "threads"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_read_thread_scaling");
}

// ── Node update ───────────────────────────────────────────────────────────────

TEST_CASE("Node update thread scaling", "[SCALABILITY][threads]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("node_update_thread_scaling");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    // Pre-insert 8 nodes (one per thread for the largest N); each thread
    // updates its own node to measure scaling without lock contention.
    constexpr uint32_t MAX_THREADS = 8;
    std::vector<uint64_t> node_ids;
    node_ids.reserve(MAX_THREADS);
    for (uint32_t t = 0; t < MAX_THREADS; ++t) {
        auto node = GraphGenerator::create_test_node(
            500000 + t, graph->get_agent_id(),
            "update_node_" + std::to_string(t));
        auto res = graph->insert_node(node);
        REQUIRE(res.has_value());
        node_ids.push_back(res.value());
    }

    for (uint32_t N : {1u, 2u, 4u, 8u}) {
        std::atomic<uint64_t> total_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(N);

        std::vector<std::vector<uint64_t>> per_thread_samples(N);
        for (auto& s : per_thread_samples) s.reserve(2000000 / N);

        std::vector<std::thread> threads;
        threads.reserve(N);

        auto wall_start = steady_clock::now();

        for (uint32_t t = 0; t < N; ++t) {
            threads.emplace_back([&, tid = t]() {
                uint64_t local_ops = 0;
                auto& samples = per_thread_samples[tid];
                uint64_t nid = node_ids[tid];

                sync_point.arrive_and_wait();

                while (!stop_flag.load(std::memory_order_relaxed)) {
                    auto node = graph->get_node(nid);
                    if (node) {
                        graph->add_or_modify_attrib_local<level_att>(
                            *node, static_cast<int32_t>(local_ops % 1000));
                        uint64_t ts = bench_now();
                        graph->update_node(*node);
                        samples.push_back(bench_now() - ts);
                        local_ops++;
                    }
                }

                total_ops.fetch_add(local_ops, std::memory_order_relaxed);
            });
        }

        std::this_thread::sleep_for(THREAD_DUR);
        stop_flag.store(true, std::memory_order_relaxed);
        for (auto& th : threads) th.join();

        auto dur = duration_cast<milliseconds>(steady_clock::now() - wall_start);

        LatencyTracker merged;
        for (auto& s : per_thread_samples)
            for (auto v : s) merged.record(v);

        const std::string n_str = std::to_string(N);
        collector.record_throughput("node_update", total_ops.load(), dur,
            {{"threads", n_str}});
        if (!merged.empty())
            collector.record_latency_stats("node_update", merged.stats(),
                {{"threads", n_str}});

        double ops_per_sec = static_cast<double>(total_ops.load()) /
                             (static_cast<double>(dur.count()) / 1000.0);
        collector.record_scalability("node_update", N, ops_per_sec, "ops/sec",
            {{"threads", n_str}, {"scale_dim", "threads"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_update_thread_scaling");
}

// ── Edge insert ───────────────────────────────────────────────────────────────

TEST_CASE("Edge insert thread scaling", "[SCALABILITY][threads]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("edge_insert_thread_scaling");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    auto root = graph->get_node_root();
    REQUIRE(root.has_value());

    // Pre-populate target node pool; shared across all N iterations.
    constexpr uint32_t POOL_SIZE = 10000;
    std::vector<uint64_t> pool;
    pool.reserve(POOL_SIZE);
    for (uint64_t i = 0; i < POOL_SIZE; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        if (res.has_value()) pool.push_back(res.value());
    }
    REQUIRE(!pool.empty());
    const size_t pool_size = pool.size();

    for (uint32_t N : {1u, 2u, 4u, 8u}) {
        std::atomic<uint64_t> total_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(N);

        std::vector<std::vector<uint64_t>> per_thread_samples(N);
        for (auto& s : per_thread_samples) s.reserve(2000000 / N);

        std::vector<std::thread> threads;
        threads.reserve(N);

        const uint32_t stride = static_cast<uint32_t>(pool_size / N) + 1;
        auto wall_start = steady_clock::now();

        for (uint32_t t = 0; t < N; ++t) {
            threads.emplace_back([&, tid = t]() {
                uint64_t local_ops = 0;
                auto& samples = per_thread_samples[tid];

                sync_point.arrive_and_wait();

                while (!stop_flag.load(std::memory_order_relaxed)) {
                    uint64_t idx = (local_ops + tid * stride) % pool_size;
                    auto edge = GraphGenerator::create_test_edge(
                        root->id(), pool[idx], graph->get_agent_id());
                    uint64_t ts = bench_now();
                    graph->insert_or_assign_edge(edge);
                    samples.push_back(bench_now() - ts);
                    local_ops++;
                }

                total_ops.fetch_add(local_ops, std::memory_order_relaxed);
            });
        }

        std::this_thread::sleep_for(THREAD_DUR);
        stop_flag.store(true, std::memory_order_relaxed);
        for (auto& th : threads) th.join();

        auto dur = duration_cast<milliseconds>(steady_clock::now() - wall_start);

        LatencyTracker merged;
        for (auto& s : per_thread_samples)
            for (auto v : s) merged.record(v);

        const std::string n_str = std::to_string(N);
        collector.record_throughput("edge_insert", total_ops.load(), dur,
            {{"threads", n_str}});
        if (!merged.empty())
            collector.record_latency_stats("edge_insert", merged.stats(),
                {{"threads", n_str}});

        double ops_per_sec = static_cast<double>(total_ops.load()) /
                             (static_cast<double>(dur.count()) / 1000.0);
        collector.record_scalability("edge_insert", N, ops_per_sec, "ops/sec",
            {{"threads", n_str}, {"scale_dim", "threads"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_insert_thread_scaling");
}

// ── Edge read ─────────────────────────────────────────────────────────────────

TEST_CASE("Edge read thread scaling", "[SCALABILITY][threads]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("edge_read_thread_scaling");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    auto root = graph->get_node_root();
    REQUIRE(root.has_value());

    // Pre-populate 1000 nodes + edges; shared across all N iterations.
    constexpr uint32_t POOL_SIZE = 1000;
    std::vector<uint64_t> pool;
    pool.reserve(POOL_SIZE);
    for (uint64_t i = 0; i < POOL_SIZE; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        if (res.has_value()) {
            pool.push_back(res.value());
            auto edge = GraphGenerator::create_test_edge(
                root->id(), res.value(), graph->get_agent_id());
            graph->insert_or_assign_edge(edge);
        }
    }
    REQUIRE(!pool.empty());
    const size_t pool_size = pool.size();

    for (uint32_t N : {1u, 2u, 4u, 8u}) {
        std::atomic<uint64_t> total_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(N);

        std::vector<std::vector<uint64_t>> per_thread_samples(N);
        for (auto& s : per_thread_samples) s.reserve(2000000 / N);

        std::vector<std::thread> threads;
        threads.reserve(N);

        const uint32_t stride = static_cast<uint32_t>(pool_size / N) + 1;
        auto wall_start = steady_clock::now();

        for (uint32_t t = 0; t < N; ++t) {
            threads.emplace_back([&, tid = t]() {
                uint64_t local_ops = 0;
                auto& samples = per_thread_samples[tid];

                sync_point.arrive_and_wait();

                while (!stop_flag.load(std::memory_order_relaxed)) {
                    uint64_t idx = (local_ops + tid * stride) % pool_size;
                    uint64_t ts = bench_now();
                    auto edge = graph->get_edge(root->id(), pool[idx], "test_edge");
                    samples.push_back(bench_now() - ts);
                    local_ops++;
                }

                total_ops.fetch_add(local_ops, std::memory_order_relaxed);
            });
        }

        std::this_thread::sleep_for(THREAD_DUR);
        stop_flag.store(true, std::memory_order_relaxed);
        for (auto& th : threads) th.join();

        auto dur = duration_cast<milliseconds>(steady_clock::now() - wall_start);

        LatencyTracker merged;
        for (auto& s : per_thread_samples)
            for (auto v : s) merged.record(v);

        const std::string n_str = std::to_string(N);
        collector.record_throughput("edge_read", total_ops.load(), dur,
            {{"threads", n_str}});
        if (!merged.empty())
            collector.record_latency_stats("edge_read", merged.stats(),
                {{"threads", n_str}});

        double ops_per_sec = static_cast<double>(total_ops.load()) /
                             (static_cast<double>(dur.count()) / 1000.0);
        collector.record_scalability("edge_read", N, ops_per_sec, "ops/sec",
            {{"threads", n_str}, {"scale_dim", "threads"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_read_thread_scaling");
}
