#include <catch2/catch_test_macros.hpp>

#include "../core/nanobench_adapter.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/multi_agent_fixture.h"
#include "../fixtures/graph_generator.h"

using namespace DSR;
using namespace DSR::Benchmark;

// For each operation, measures latency (1000 samples) and derives throughput
// from the nanobench mean, at three pre-existing graph sizes: {100, 1000, 10000}.
// "graph_size" = number of nodes already in the graph before measurement begins.

// ── Node insert ───────────────────────────────────────────────────────────────

TEST_CASE("Node insert graph size scaling", "[SCALABILITY][graphsize]") {
    GraphGenerator generator;
    MetricsCollector collector("node_insert_graphsize_scaling");

    for (uint32_t N : {100u, 1000u, 10000u}) {
        MultiAgentFixture fixture;
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture.create_agents(1, config_file));
        auto* graph = fixture.get_agent(0);
        REQUIRE(graph != nullptr);

        // Pre-populate to target size
        for (uint32_t i = 0; i < N; ++i) {
            auto node = GraphGenerator::create_test_node(2000000 + i, graph->get_agent_id());
            auto res = graph->insert_node(node);
            REQUIRE(res.has_value());
        }

        // ~38µs/op: 300 iters/epoch × 50 epochs ≈ 0.57 s
        uint64_t id_counter = 3000000;
        auto bench = make_latency_bench(50);
        bench.minEpochIterations(300);
        bench.run("node_insert", [&] {
            auto node = GraphGenerator::create_test_node(id_counter++, graph->get_agent_id());
            auto res = graph->insert_node(node);
            REQUIRE(res.has_value());
            ankerl::nanobench::doNotOptimizeAway(res);
        });

        auto stats = nb_to_stats(bench);
        const std::string n_str = std::to_string(N);
        collector.record_latency_stats("node_insert", stats, {{"graph_size", n_str}});
        collector.record("node_insert", MetricCategory::Throughput,
            nb_throughput(bench), "ops/sec", {{"graph_size", n_str}});
        collector.record_scalability("node_insert", N, stats.mean_ns, "ns",
            {{"graph_size", n_str}, {"scale_dim", "graph_size"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_insert_graphsize_scaling");
}

// ── Node read ─────────────────────────────────────────────────────────────────

TEST_CASE("Node read graph size scaling", "[SCALABILITY][graphsize]") {
    GraphGenerator generator;
    MetricsCollector collector("node_read_graphsize_scaling");

    for (uint32_t N : {100u, 1000u, 10000u}) {
        MultiAgentFixture fixture;
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture.create_agents(1, config_file));
        auto* graph = fixture.get_agent(0);
        REQUIRE(graph != nullptr);

        std::vector<uint64_t> node_ids;
        node_ids.reserve(N);
        for (uint32_t i = 0; i < N; ++i) {
            auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
            auto res = graph->insert_node(node);
            REQUIRE(res.has_value());
            node_ids.push_back(res.value());
        }

        // Cache warmup: touch every node once so all N entries are in L3
        for (const auto id : node_ids) { (void)graph->get_node(id); }

        // ~900 ns–1 µs/op: 10 000 iters/epoch × 200 epochs ≈ 1.8 s
        size_t idx = 0;
        bool last_ok = true;
        auto bench = make_latency_bench(200, 0); // manual warmup done above
        bench.minEpochIterations(10000);
        bench.run("node_read", [&] {
            auto node = graph->get_node(node_ids[idx++ % node_ids.size()]);
            last_ok = node.has_value();
            ankerl::nanobench::doNotOptimizeAway(node);
        });
        REQUIRE(last_ok);

        auto stats = nb_to_stats(bench);
        const std::string n_str = std::to_string(N);
        collector.record_latency_stats("node_read", stats, {{"graph_size", n_str}});
        collector.record("node_read", MetricCategory::Throughput,
            nb_throughput(bench), "ops/sec", {{"graph_size", n_str}});
        collector.record_scalability("node_read", N, stats.mean_ns, "ns",
            {{"graph_size", n_str}, {"scale_dim", "graph_size"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_read_graphsize_scaling");
}

// ── Node update ───────────────────────────────────────────────────────────────

TEST_CASE("Node update graph size scaling", "[SCALABILITY][graphsize]") {
    GraphGenerator generator;
    MetricsCollector collector("node_update_graphsize_scaling");

    for (uint32_t N : {100u, 1000u, 10000u}) {
        MultiAgentFixture fixture;
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture.create_agents(1, config_file));
        auto* graph = fixture.get_agent(0);
        REQUIRE(graph != nullptr);

        std::vector<uint64_t> node_ids;
        node_ids.reserve(N);
        for (uint32_t i = 0; i < N; ++i) {
            auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
            auto res = graph->insert_node(node);
            REQUIRE(res.has_value());
            node_ids.push_back(res.value());
        }

        // ~35µs/op: 350 iters/epoch × 50 epochs ≈ 0.61 s
        uint64_t update_counter = 0;
        size_t idx = 0;
        auto bench = make_latency_bench(50);
        bench.minEpochIterations(350);
        bench.run("node_update", [&] {
            auto node = graph->get_node(node_ids[idx++ % node_ids.size()]);
            REQUIRE(node.has_value());
            graph->add_or_modify_attrib_local<level_att>(
                *node, static_cast<int32_t>(update_counter++ % 1000));
            bool ok = graph->update_node(*node);
            REQUIRE(ok);
            ankerl::nanobench::doNotOptimizeAway(ok);
        });

        auto stats = nb_to_stats(bench);
        const std::string n_str = std::to_string(N);
        collector.record_latency_stats("node_update", stats, {{"graph_size", n_str}});
        collector.record("node_update", MetricCategory::Throughput,
            nb_throughput(bench), "ops/sec", {{"graph_size", n_str}});
        collector.record_scalability("node_update", N, stats.mean_ns, "ns",
            {{"graph_size", n_str}, {"scale_dim", "graph_size"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_update_graphsize_scaling");
}

// ── Edge insert ───────────────────────────────────────────────────────────────

TEST_CASE("Edge insert graph size scaling", "[SCALABILITY][graphsize]") {
    GraphGenerator generator;
    MetricsCollector collector("edge_insert_graphsize_scaling");

    for (uint32_t N : {100u, 1000u, 10000u}) {
        MultiAgentFixture fixture;
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture.create_agents(1, config_file));
        auto* graph = fixture.get_agent(0);
        REQUIRE(graph != nullptr);

        auto root = graph->get_node_root();
        REQUIRE(root.has_value());

        // Pre-populate N target nodes
        std::vector<uint64_t> node_ids;
        node_ids.reserve(N);
        for (uint32_t i = 0; i < N; ++i) {
            auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
            auto res = graph->insert_node(node);
            REQUIRE(res.has_value());
            node_ids.push_back(res.value());
        }

        // ~12µs (N≤1000): 800 iters/epoch × 50 epochs ≈ 0.48 s
        // ~225µs (N=10000): 100 iters/epoch × 50 epochs ≈ 1.13 s
        size_t idx = 0;
        auto bench = make_latency_bench(50);
        bench.minEpochIterations(N <= 1000 ? 800 : 100);
        bench.run("edge_insert", [&] {
            uint64_t target = node_ids[idx++ % node_ids.size()];
            auto edge = GraphGenerator::create_test_edge(
                root->id(), target, graph->get_agent_id());
            bool ok = graph->insert_or_assign_edge(edge);
            REQUIRE(ok);
            ankerl::nanobench::doNotOptimizeAway(ok);
        });

        auto stats = nb_to_stats(bench);
        const std::string n_str = std::to_string(N);
        collector.record_latency_stats("edge_insert", stats, {{"graph_size", n_str}});
        collector.record("edge_insert", MetricCategory::Throughput,
            nb_throughput(bench), "ops/sec", {{"graph_size", n_str}});
        collector.record_scalability("edge_insert", N, stats.mean_ns, "ns",
            {{"graph_size", n_str}, {"scale_dim", "graph_size"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_insert_graphsize_scaling");
}

// ── Edge read ─────────────────────────────────────────────────────────────────

TEST_CASE("Edge read graph size scaling", "[SCALABILITY][graphsize]") {
    GraphGenerator generator;
    MetricsCollector collector("edge_read_graphsize_scaling");

    for (uint32_t N : {100u, 1000u, 10000u}) {
        MultiAgentFixture fixture;
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture.create_agents(1, config_file));
        auto* graph = fixture.get_agent(0);
        REQUIRE(graph != nullptr);

        auto root = graph->get_node_root();
        REQUIRE(root.has_value());

        // Pre-populate N nodes + edges
        std::vector<uint64_t> target_ids;
        target_ids.reserve(N);
        for (uint32_t i = 0; i < N; ++i) {
            auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
            auto res = graph->insert_node(node);
            REQUIRE(res.has_value());
            target_ids.push_back(res.value());
            auto edge = GraphGenerator::create_test_edge(
                root->id(), res.value(), graph->get_agent_id());
            REQUIRE(graph->insert_or_assign_edge(edge));
        }

        // Cache warmup: touch every edge once
        for (const auto id : target_ids) { (void)graph->get_edge(root->id(), id, "test_edge"); }

        size_t idx = 0;
        bool last_ok = true;
        auto bench = make_latency_bench(1000, 0); // manual warmup done above
        bench.run("edge_read", [&] {
            uint64_t target = target_ids[idx++ % target_ids.size()];
            auto edge = graph->get_edge(root->id(), target, "test_edge");
            last_ok = edge.has_value();
            ankerl::nanobench::doNotOptimizeAway(edge);
        });
        REQUIRE(last_ok);

        auto stats = nb_to_stats(bench);
        const std::string n_str = std::to_string(N);
        collector.record_latency_stats("edge_read", stats, {{"graph_size", n_str}});
        collector.record("edge_read", MetricCategory::Throughput,
            nb_throughput(bench), "ops/sec", {{"graph_size", n_str}});
        collector.record_scalability("edge_read", N, stats.mean_ns, "ns",
            {{"graph_size", n_str}, {"scale_dim", "graph_size"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_read_graphsize_scaling");
}
