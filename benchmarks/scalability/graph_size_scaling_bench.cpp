#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include "../core/timing_utils.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/multi_agent_fixture.h"
#include "../fixtures/graph_generator.h"

using namespace DSR;
using namespace DSR::Benchmark;
using namespace std::chrono;

// For each operation, measures latency (1000 samples) and throughput (3-second
// window) at three pre-existing graph sizes: {100, 1000, 10000} nodes.
// "graph_size" = number of nodes already in the graph before measurement
// begins, so the benchmark captures the cost of operating on an N-node graph.

static constexpr auto GS_THR_DUR = std::chrono::seconds(3);

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
            graph->insert_node(node);
        }

        // Latency — 1000 samples
        LatencyTracker tracker(1000);
        for (int i = 0; i < 1000; ++i) {
            auto node = GraphGenerator::create_test_node(3000000 + i, graph->get_agent_id());
            auto t = tracker.scoped_record();
            graph->insert_node(node);
        }
        auto stats = tracker.stats();

        // Throughput — 3-second window
        uint64_t ops = 0;
        auto start = steady_clock::now();
        auto end = start + GS_THR_DUR;
        while (steady_clock::now() < end) {
            auto node = GraphGenerator::create_test_node(4000000 + ops, graph->get_agent_id());
            graph->insert_node(node);
            ops++;
        }
        auto dur = duration_cast<milliseconds>(steady_clock::now() - start);

        const std::string n_str = std::to_string(N);
        collector.record_latency_stats("node_insert", stats, {{"graph_size", n_str}});
        collector.record_throughput("node_insert", ops, dur, {{"graph_size", n_str}});
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
            if (res.has_value()) node_ids.push_back(res.value());
        }
        REQUIRE(!node_ids.empty());

        // Latency — 1000 samples
        LatencyTracker tracker(1000);
        for (int i = 0; i < 1000; ++i) {
            uint64_t id = node_ids[i % node_ids.size()];
            auto t = tracker.scoped_record();
            auto node = graph->get_node(id);
        }
        auto stats = tracker.stats();

        // Throughput — 3-second window
        uint64_t ops = 0;
        auto start = steady_clock::now();
        auto end = start + GS_THR_DUR;
        while (steady_clock::now() < end) {
            uint64_t id = node_ids[ops % node_ids.size()];
            auto node = graph->get_node(id);
            ops++;
        }
        auto dur = duration_cast<milliseconds>(steady_clock::now() - start);

        const std::string n_str = std::to_string(N);
        collector.record_latency_stats("node_read", stats, {{"graph_size", n_str}});
        collector.record_throughput("node_read", ops, dur, {{"graph_size", n_str}});
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
            if (res.has_value()) node_ids.push_back(res.value());
        }
        REQUIRE(!node_ids.empty());

        // Latency — 1000 samples
        LatencyTracker tracker(1000);
        for (int i = 0; i < 1000; ++i) {
            uint64_t id = node_ids[i % node_ids.size()];
            auto node = graph->get_node(id);
            if (node) {
                graph->add_or_modify_attrib_local<level_att>(
                    *node, static_cast<int32_t>(i % 1000));
                auto t = tracker.scoped_record();
                graph->update_node(*node);
            }
        }
        auto stats = tracker.stats();

        // Throughput — 3-second window
        uint64_t ops = 0;
        auto start = steady_clock::now();
        auto end = start + GS_THR_DUR;
        while (steady_clock::now() < end) {
            uint64_t id = node_ids[ops % node_ids.size()];
            auto node = graph->get_node(id);
            if (node) {
                graph->add_or_modify_attrib_local<level_att>(
                    *node, static_cast<int32_t>(ops % 1000));
                graph->update_node(*node);
                ops++;
            }
        }
        auto dur = duration_cast<milliseconds>(steady_clock::now() - start);

        const std::string n_str = std::to_string(N);
        collector.record_latency_stats("node_update", stats, {{"graph_size", n_str}});
        collector.record_throughput("node_update", ops, dur, {{"graph_size", n_str}});
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
            if (res.has_value()) node_ids.push_back(res.value());
        }
        REQUIRE(!node_ids.empty());

        // Latency — 1000 samples
        LatencyTracker tracker(1000);
        for (int i = 0; i < 1000; ++i) {
            uint64_t target = node_ids[i % node_ids.size()];
            auto edge = GraphGenerator::create_test_edge(
                root->id(), target, graph->get_agent_id());
            auto t = tracker.scoped_record();
            graph->insert_or_assign_edge(edge);
        }
        auto stats = tracker.stats();

        // Throughput — 3-second window
        uint64_t ops = 0;
        auto start = steady_clock::now();
        auto end = start + GS_THR_DUR;
        while (steady_clock::now() < end) {
            uint64_t target = node_ids[ops % node_ids.size()];
            auto edge = GraphGenerator::create_test_edge(
                root->id(), target, graph->get_agent_id());
            graph->insert_or_assign_edge(edge);
            ops++;
        }
        auto dur = duration_cast<milliseconds>(steady_clock::now() - start);

        const std::string n_str = std::to_string(N);
        collector.record_latency_stats("edge_insert", stats, {{"graph_size", n_str}});
        collector.record_throughput("edge_insert", ops, dur, {{"graph_size", n_str}});
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
            if (res.has_value()) {
                target_ids.push_back(res.value());
                auto edge = GraphGenerator::create_test_edge(
                    root->id(), res.value(), graph->get_agent_id());
                graph->insert_or_assign_edge(edge);
            }
        }
        REQUIRE(!target_ids.empty());

        // Latency — 1000 samples
        LatencyTracker tracker(1000);
        for (int i = 0; i < 1000; ++i) {
            uint64_t target = target_ids[i % target_ids.size()];
            auto t = tracker.scoped_record();
            auto edge = graph->get_edge(root->id(), target, "test_edge");
        }
        auto stats = tracker.stats();

        // Throughput — 3-second window
        uint64_t ops = 0;
        auto start = steady_clock::now();
        auto end = start + GS_THR_DUR;
        while (steady_clock::now() < end) {
            uint64_t target = target_ids[ops % target_ids.size()];
            auto edge = graph->get_edge(root->id(), target, "test_edge");
            ops++;
        }
        auto dur = duration_cast<milliseconds>(steady_clock::now() - start);

        const std::string n_str = std::to_string(N);
        collector.record_latency_stats("edge_read", stats, {{"graph_size", n_str}});
        collector.record_throughput("edge_read", ops, dur, {{"graph_size", n_str}});
        collector.record_scalability("edge_read", N, stats.mean_ns, "ns",
            {{"graph_size", n_str}, {"scale_dim", "graph_size"}});
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_read_graphsize_scaling");
}
