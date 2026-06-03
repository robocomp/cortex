#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <atomic>
#include <barrier>
#include <vector>
#include <chrono>
#include <string>
#include <iostream>

#include <nanobench.h>
#include "../core/nanobench_adapter.h"
#include "../core/timing_utils.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/multi_agent_fixture.h"
#include "../fixtures/graph_generator.h"

using namespace DSR;
using namespace DSR::Benchmark;
using namespace std::chrono;

// Measures throughput across {1, 2, 4, 8} threads for each operation.
// Each iteration runs a 3-second window.  Per-thread latency is intentionally
// not collected here — merged percentiles from concurrent threads are
// statistically misleading.  Use the single-agent latency benchmarks instead.

static constexpr auto THREAD_DUR = std::chrono::seconds(3);

// ── Node insert ───────────────────────────────────────────────────────────────

TEST_CASE("Node insert thread scaling", "[SCALABILITY][threads]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("node_insert_thread_scaling");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    ankerl::nanobench::Bench bench;
    bench.output(&nb_report_stream()).warmup(0).epochs(1).epochIterations(1);

    for (uint32_t N : {1u, 2u, 4u, 8u}) {
        std::atomic<uint64_t> total_ops{0};
        std::atomic<uint64_t> failed_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(N);

        std::vector<std::thread> threads;
        threads.reserve(N);

        auto wall_start = steady_clock::now();

        bench.run("node_insert_" + std::to_string(N) + "t", [&] {
            for (uint32_t t = 0; t < N; ++t) {
                threads.emplace_back([&, tid = t]() {
                    uint64_t base_id = 200000ULL + tid * 200000ULL;
                    uint64_t local_ops = 0;

                    sync_point.arrive_and_wait();

                    while (!stop_flag.load(std::memory_order_relaxed)) {
                        auto node = GraphGenerator::create_test_node(
                            base_id + local_ops, graph->get_agent_id());
                        auto res = graph->insert_node(node);
                        if (!res.has_value())
                            failed_ops.fetch_add(1, std::memory_order_relaxed);
                        local_ops++;
                    }

                    total_ops.fetch_add(local_ops, std::memory_order_relaxed);
                });
            }

            std::this_thread::sleep_for(THREAD_DUR);
            stop_flag.store(true, std::memory_order_relaxed);
            for (auto& th : threads) th.join();

            bench.batch(total_ops.load());
            ankerl::nanobench::doNotOptimizeAway(total_ops.load());
        });

        if (failed_ops.load() > 0)
            std::cerr << "[BENCH node_insert threads=" << N << "] "
                      << failed_ops.load() << " insert_node calls failed\n";

        auto dur = duration_cast<milliseconds>(steady_clock::now() - wall_start);

        const std::string n_str = std::to_string(N);
        collector.record_throughput("node_insert", total_ops.load(), dur, {{"threads", n_str}});

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

    std::vector<uint64_t> node_ids;
    node_ids.reserve(1000);
    for (uint64_t i = 0; i < 1000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        REQUIRE(res.has_value());
        node_ids.push_back(res.value());
    }
    const size_t pool_size = node_ids.size();

    ankerl::nanobench::Bench bench;
    bench.output(&nb_report_stream()).warmup(0).epochs(1).epochIterations(1);

    for (uint32_t N : {1u, 2u, 4u, 8u}) {
        std::atomic<uint64_t> total_ops{0};
        std::atomic<uint64_t> failed_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(N);

        std::vector<std::thread> threads;
        threads.reserve(N);

        auto wall_start = steady_clock::now();

        bench.run("node_read_" + std::to_string(N) + "t", [&] {
            for (uint32_t t = 0; t < N; ++t) {
                threads.emplace_back([&, tid = t]() {
                    uint64_t local_ops = 0;

                    sync_point.arrive_and_wait();

                    while (!stop_flag.load(std::memory_order_relaxed)) {
                        uint64_t id = node_ids[local_ops % pool_size];
                        auto node = graph->get_node(id);
                        if (!node.has_value())
                            failed_ops.fetch_add(1, std::memory_order_relaxed);
                        local_ops++;
                    }

                    total_ops.fetch_add(local_ops, std::memory_order_relaxed);
                });
            }

            std::this_thread::sleep_for(THREAD_DUR);
            stop_flag.store(true, std::memory_order_relaxed);
            for (auto& th : threads) th.join();

            bench.batch(total_ops.load());
            ankerl::nanobench::doNotOptimizeAway(total_ops.load());
        });

        if (failed_ops.load() > 0)
            std::cerr << "[BENCH node_read threads=" << N << "] "
                      << failed_ops.load() << " get_node calls returned empty\n";

        auto dur = duration_cast<milliseconds>(steady_clock::now() - wall_start);

        const std::string n_str = std::to_string(N);
        collector.record_throughput("node_read", total_ops.load(), dur, {{"threads", n_str}});

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

    constexpr uint32_t MAX_THREADS = 8;
    std::vector<uint64_t> node_ids;
    node_ids.reserve(MAX_THREADS);
    for (uint32_t t = 0; t < MAX_THREADS; ++t) {
        auto node = GraphGenerator::create_test_node(
            500000 + t, graph->get_agent_id(), "update_node_" + std::to_string(t));
        auto res = graph->insert_node(node);
        REQUIRE(res.has_value());
        node_ids.push_back(res.value());
    }

    ankerl::nanobench::Bench bench;
    bench.output(&nb_report_stream()).warmup(0).epochs(1).epochIterations(1);

    for (uint32_t N : {1u, 2u, 4u, 8u}) {
        std::atomic<uint64_t> total_ops{0};
        std::atomic<uint64_t> failed_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(N);

        std::vector<std::thread> threads;
        threads.reserve(N);

        auto wall_start = steady_clock::now();

        bench.run("node_update_" + std::to_string(N) + "t", [&] {
            for (uint32_t t = 0; t < N; ++t) {
                threads.emplace_back([&, tid = t]() {
                    uint64_t local_ops = 0;
                    uint64_t nid = node_ids[tid];

                    sync_point.arrive_and_wait();

                    while (!stop_flag.load(std::memory_order_relaxed)) {
                        auto node = graph->get_node(nid);
                        if (node) {
                            graph->add_or_modify_attrib_local<level_att>(
                                *node, static_cast<int32_t>(local_ops % 1000));
                            bool ok = graph->update_node(*node);
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

            std::this_thread::sleep_for(THREAD_DUR);
            stop_flag.store(true, std::memory_order_relaxed);
            for (auto& th : threads) th.join();

            bench.batch(total_ops.load());
            ankerl::nanobench::doNotOptimizeAway(total_ops.load());
        });

        if (failed_ops.load() > 0)
            std::cerr << "[BENCH node_update threads=" << N << "] "
                      << failed_ops.load() << " get_node/update_node calls failed\n";

        auto dur = duration_cast<milliseconds>(steady_clock::now() - wall_start);

        const std::string n_str = std::to_string(N);
        collector.record_throughput("node_update", total_ops.load(), dur, {{"threads", n_str}});

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

    constexpr uint32_t POOL_SIZE = 10000;
    std::vector<uint64_t> pool;
    pool.reserve(POOL_SIZE);
    for (uint64_t i = 0; i < POOL_SIZE; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        REQUIRE(res.has_value());
        pool.push_back(res.value());
    }
    const size_t pool_size = pool.size();

    ankerl::nanobench::Bench bench;
    bench.output(&nb_report_stream()).warmup(0).epochs(1).epochIterations(1);

    for (uint32_t N : {1u, 2u, 4u, 8u}) {
        std::atomic<uint64_t> total_ops{0};
        std::atomic<uint64_t> failed_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(N);

        std::vector<std::thread> threads;
        threads.reserve(N);

        const uint32_t stride = static_cast<uint32_t>(pool_size / N) + 1;
        auto wall_start = steady_clock::now();

        bench.run("edge_insert_" + std::to_string(N) + "t", [&] {
            for (uint32_t t = 0; t < N; ++t) {
                threads.emplace_back([&, tid = t]() {
                    uint64_t local_ops = 0;

                    sync_point.arrive_and_wait();

                    while (!stop_flag.load(std::memory_order_relaxed)) {
                        uint64_t idx = (local_ops + tid * stride) % pool_size;
                        auto edge = GraphGenerator::create_test_edge(
                            root->id(), pool[idx], graph->get_agent_id());
                        bool ok = graph->insert_or_assign_edge(edge);
                        if (!ok)
                            failed_ops.fetch_add(1, std::memory_order_relaxed);
                        local_ops++;
                    }

                    total_ops.fetch_add(local_ops, std::memory_order_relaxed);
                });
            }

            std::this_thread::sleep_for(THREAD_DUR);
            stop_flag.store(true, std::memory_order_relaxed);
            for (auto& th : threads) th.join();

            bench.batch(total_ops.load());
            ankerl::nanobench::doNotOptimizeAway(total_ops.load());
        });

        if (failed_ops.load() > 0)
            std::cerr << "[BENCH edge_insert threads=" << N << "] "
                      << failed_ops.load() << " insert_or_assign_edge calls failed\n";

        auto dur = duration_cast<milliseconds>(steady_clock::now() - wall_start);

        const std::string n_str = std::to_string(N);
        collector.record_throughput("edge_insert", total_ops.load(), dur, {{"threads", n_str}});

        double ops_per_sec = static_cast<double>(total_ops.load()) /
                             (static_cast<double>(dur.count()) / 1000.0);
        collector.record_scalability("edge_insert", N, ops_per_sec, "ops/sec",
            {{"threads", n_str}, {"scale_dim", "threads"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_insert_thread_scaling");
}

TEST_CASE("Edge changed-attr reassign thread scaling", "[SCALABILITY][threads][diagnostic][edge]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("edge_reassign_attr_thread_scaling");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    auto root = graph->get_node_root();
    REQUIRE(root.has_value());

    constexpr uint32_t POOL_SIZE = 10000;
    std::vector<uint64_t> pool;
    pool.reserve(POOL_SIZE);
    for (uint64_t i = 0; i < POOL_SIZE; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        REQUIRE(res.has_value());
        pool.push_back(res.value());

        auto edge = GraphGenerator::create_test_edge(root->id(), res.value(), graph->get_agent_id());
        REQUIRE(graph->insert_or_assign_edge(edge));
    }
    const size_t pool_size = pool.size();

    ankerl::nanobench::Bench bench;
    bench.output(&nb_report_stream()).warmup(0).epochs(1).epochIterations(1);

    for (uint32_t N : {1u, 2u, 4u, 8u}) {
        std::atomic<uint64_t> total_ops{0};
        std::atomic<uint64_t> failed_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(N);

        std::vector<std::thread> threads;
        threads.reserve(N);

        const uint32_t stride = static_cast<uint32_t>(pool_size / N) + 1;
        auto wall_start = steady_clock::now();

        bench.run("edge_reassign_attr_" + std::to_string(N) + "t", [&] {
            for (uint32_t t = 0; t < N; ++t) {
                threads.emplace_back([&, tid = t]() {
                    uint64_t local_ops = 0;

                    sync_point.arrive_and_wait();

                    while (!stop_flag.load(std::memory_order_relaxed)) {
                        uint64_t idx = (local_ops + tid * stride) % pool_size;
                        auto edge = GraphGenerator::create_test_edge(
                            root->id(), pool[idx], graph->get_agent_id());
                        edge.attrs().insert_or_assign("diagnostic_value",
                            Attribute(static_cast<int32_t>(local_ops), 0, graph->get_agent_id()));
                        bool ok = graph->insert_or_assign_edge(edge);
                        if (!ok)
                            failed_ops.fetch_add(1, std::memory_order_relaxed);
                        local_ops++;
                    }

                    total_ops.fetch_add(local_ops, std::memory_order_relaxed);
                });
            }

            std::this_thread::sleep_for(THREAD_DUR);
            stop_flag.store(true, std::memory_order_relaxed);
            for (auto& th : threads) th.join();

            bench.batch(total_ops.load());
            ankerl::nanobench::doNotOptimizeAway(total_ops.load());
        });

        if (failed_ops.load() > 0)
            std::cerr << "[BENCH edge_reassign_attr threads=" << N << "] "
                      << failed_ops.load() << " insert_or_assign_edge calls failed\n";

        auto dur = duration_cast<milliseconds>(steady_clock::now() - wall_start);

        const std::string n_str = std::to_string(N);
        collector.record_throughput("edge_reassign_changed_attr", total_ops.load(), dur, {{"threads", n_str}});

        double ops_per_sec = static_cast<double>(total_ops.load()) /
                             (static_cast<double>(dur.count()) / 1000.0);
        collector.record_scalability("edge_reassign_changed_attr", N, ops_per_sec, "ops/sec",
            {{"threads", n_str}, {"scale_dim", "threads"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_reassign_attr_thread_scaling");
}

TEST_CASE("Edge same-empty reassign thread scaling", "[SCALABILITY][threads][diagnostic][edge]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("edge_reassign_empty_thread_scaling");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    auto root = graph->get_node_root();
    REQUIRE(root.has_value());

    constexpr uint32_t POOL_SIZE = 10000;
    std::vector<uint64_t> pool;
    pool.reserve(POOL_SIZE);
    for (uint64_t i = 0; i < POOL_SIZE; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        REQUIRE(res.has_value());
        pool.push_back(res.value());

        auto edge = GraphGenerator::create_test_edge(root->id(), res.value(), graph->get_agent_id());
        REQUIRE(graph->insert_or_assign_edge(edge));
    }
    const size_t pool_size = pool.size();

    ankerl::nanobench::Bench bench;
    bench.output(&nb_report_stream()).warmup(0).epochs(1).epochIterations(1);

    for (uint32_t N : {1u, 2u, 4u, 8u}) {
        std::atomic<uint64_t> total_ops{0};
        std::atomic<uint64_t> failed_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(N);

        std::vector<std::thread> threads;
        threads.reserve(N);

        const uint32_t stride = static_cast<uint32_t>(pool_size / N) + 1;
        auto wall_start = steady_clock::now();

        bench.run("edge_reassign_empty_" + std::to_string(N) + "t", [&] {
            for (uint32_t t = 0; t < N; ++t) {
                threads.emplace_back([&, tid = t]() {
                    uint64_t local_ops = 0;

                    sync_point.arrive_and_wait();

                    while (!stop_flag.load(std::memory_order_relaxed)) {
                        uint64_t idx = (local_ops + tid * stride) % pool_size;
                        auto edge = GraphGenerator::create_test_edge(
                            root->id(), pool[idx], graph->get_agent_id());
                        bool ok = graph->insert_or_assign_edge(edge);
                        if (!ok)
                            failed_ops.fetch_add(1, std::memory_order_relaxed);
                        local_ops++;
                    }

                    total_ops.fetch_add(local_ops, std::memory_order_relaxed);
                });
            }

            std::this_thread::sleep_for(THREAD_DUR);
            stop_flag.store(true, std::memory_order_relaxed);
            for (auto& th : threads) th.join();

            bench.batch(total_ops.load());
            ankerl::nanobench::doNotOptimizeAway(total_ops.load());
        });

        if (failed_ops.load() > 0)
            std::cerr << "[BENCH edge_reassign_empty threads=" << N << "] "
                      << failed_ops.load() << " insert_or_assign_edge calls failed\n";

        auto dur = duration_cast<milliseconds>(steady_clock::now() - wall_start);

        const std::string n_str = std::to_string(N);
        collector.record_throughput("edge_reassign_same_empty", total_ops.load(), dur, {{"threads", n_str}});

        double ops_per_sec = static_cast<double>(total_ops.load()) /
                             (static_cast<double>(dur.count()) / 1000.0);
        collector.record_scalability("edge_reassign_same_empty", N, ops_per_sec, "ops/sec",
            {{"threads", n_str}, {"scale_dim", "threads"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_reassign_empty_thread_scaling");
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

    constexpr uint32_t POOL_SIZE = 1000;
    std::vector<uint64_t> pool;
    pool.reserve(POOL_SIZE);
    for (uint64_t i = 0; i < POOL_SIZE; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        REQUIRE(res.has_value());
        pool.push_back(res.value());
        auto edge = GraphGenerator::create_test_edge(root->id(), res.value(), graph->get_agent_id());
        REQUIRE(graph->insert_or_assign_edge(edge));
    }
    const size_t pool_size = pool.size();

    ankerl::nanobench::Bench bench;
    bench.output(&nb_report_stream()).warmup(0).epochs(1).epochIterations(1);

    for (uint32_t N : {1u, 2u, 4u, 8u}) {
        std::atomic<uint64_t> total_ops{0};
        std::atomic<uint64_t> failed_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(N);

        std::vector<std::thread> threads;
        threads.reserve(N);

        const uint32_t stride = static_cast<uint32_t>(pool_size / N) + 1;
        auto wall_start = steady_clock::now();

        bench.run("edge_read_" + std::to_string(N) + "t", [&] {
            for (uint32_t t = 0; t < N; ++t) {
                threads.emplace_back([&, tid = t]() {
                    uint64_t local_ops = 0;

                    sync_point.arrive_and_wait();

                    while (!stop_flag.load(std::memory_order_relaxed)) {
                        uint64_t idx = (local_ops + tid * stride) % pool_size;
                        auto edge = graph->get_edge(root->id(), pool[idx], "test_edge");
                        if (!edge.has_value())
                            failed_ops.fetch_add(1, std::memory_order_relaxed);
                        local_ops++;
                    }

                    total_ops.fetch_add(local_ops, std::memory_order_relaxed);
                });
            }

            std::this_thread::sleep_for(THREAD_DUR);
            stop_flag.store(true, std::memory_order_relaxed);
            for (auto& th : threads) th.join();

            bench.batch(total_ops.load());
            ankerl::nanobench::doNotOptimizeAway(total_ops.load());
        });

        if (failed_ops.load() > 0)
            std::cerr << "[BENCH edge_read threads=" << N << "] "
                      << failed_ops.load() << " get_edge calls returned empty\n";

        auto dur = duration_cast<milliseconds>(steady_clock::now() - wall_start);

        const std::string n_str = std::to_string(N);
        collector.record_throughput("edge_read", total_ops.load(), dur, {{"threads", n_str}});

        double ops_per_sec = static_cast<double>(total_ops.load()) /
                             (static_cast<double>(dur.count()) / 1000.0);
        collector.record_scalability("edge_read", N, ops_per_sec, "ops/sec",
            {{"threads", n_str}, {"scale_dim", "threads"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_read_thread_scaling");
}
