#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "../core/timing_utils.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/multi_agent_fixture.h"
#include "../fixtures/graph_generator.h"

using namespace DSR;
using namespace DSR::Benchmark;

TEST_CASE("Graph size impact on performance", "[SCALABILITY][graphsize]") {
    MetricsCollector collector("graph_size_impact");
    GraphGenerator generator;

    SECTION("Node lookup performance vs graph size") {
        for (uint32_t size : {100, 1000, 10000}) {
            MultiAgentFixture fixture;
            auto config_file = generator.generate_empty_graph();
            REQUIRE(fixture.create_agents(1, config_file));

            auto* graph = fixture.get_agent(0);
            REQUIRE(graph != nullptr);

            // Populate graph and store actual IDs
            std::vector<uint64_t> node_ids;
            node_ids.reserve(size);
            for (uint32_t i = 0; i < size; ++i) {
                auto node = GraphGenerator::create_test_node(
                    0, graph->get_agent_id());
                auto result = graph->insert_node(node);
                if (result.has_value()) {
                    node_ids.push_back(result.value());
                }
            }
            REQUIRE(!node_ids.empty());

            // Measure lookup performance
            LatencyTracker tracker(1000);

            for (int i = 0; i < 1000; ++i) {
                uint64_t id = node_ids[i % node_ids.size()];
                auto timer = tracker.scoped_record();
                auto node = graph->get_node(id);
            }

            auto stats = tracker.stats();
            collector.record_scalability(
                "node_lookup",
                size,
                stats.mean_ns,
                "ns",
                {{"graph_size", std::to_string(size)}});

            INFO(size << " nodes - Lookup: " << stats.mean_ns << " ns");
        }
    }

    SECTION("Node insertion performance vs graph size") {
        for (uint32_t size : {100, 1000, 10000}) {
            MultiAgentFixture fixture;
            auto config_file = generator.generate_empty_graph();
            REQUIRE(fixture.create_agents(1, config_file));

            auto* graph = fixture.get_agent(0);
            REQUIRE(graph != nullptr);

            // Populate graph to target size
            for (uint32_t i = 0; i < size; ++i) {
                auto node = GraphGenerator::create_test_node(
                    2000000 + i, graph->get_agent_id());
                graph->insert_node(node);
            }

            // Measure insertion performance
            LatencyTracker tracker(100);

            for (int i = 0; i < 100; ++i) {
                auto node = GraphGenerator::create_test_node(
                    3000000 + i, graph->get_agent_id());

                auto timer = tracker.scoped_record();
                graph->insert_node(node);
            }

            auto stats = tracker.stats();
            collector.record_scalability(
                "node_insert_latency",
                size,
                stats.mean_us(),
                "us",
                {{"graph_size", std::to_string(size)}});

            INFO(size << " existing nodes - Insert: " << stats.mean_us() << " us");
        }
    }

    SECTION("Edge operations vs edge count") {
        for (uint32_t edge_count : {100, 1000, 5000}) {
            MultiAgentFixture fixture;
            auto config_file = generator.generate_empty_graph();
            REQUIRE(fixture.create_agents(1, config_file));

            auto* graph = fixture.get_agent(0);
            REQUIRE(graph != nullptr);

            auto root = graph->get_node_root();
            REQUIRE(root.has_value());

            // Create nodes for edges and store actual IDs
            std::vector<uint64_t> node_ids;
            node_ids.reserve(edge_count + 100);
            for (uint32_t i = 0; i < edge_count + 100; ++i) {
                auto node = GraphGenerator::create_test_node(
                    0, graph->get_agent_id());
                auto result = graph->insert_node(node);
                if (result.has_value()) {
                    node_ids.push_back(result.value());
                }
            }
            REQUIRE(node_ids.size() >= edge_count);

            // Create edges
            for (uint32_t i = 0; i < edge_count; ++i) {
                auto edge = GraphGenerator::create_test_edge(
                    root->id(), node_ids[i], graph->get_agent_id());
                graph->insert_or_assign_edge(edge);
            }

            // Measure edge lookup performance
            LatencyTracker lookup_tracker(1000);
            for (int i = 0; i < 1000; ++i) {
                uint64_t target = node_ids[i % edge_count];
                auto timer = lookup_tracker.scoped_record();
                auto edge = graph->get_edge(root->id(), target, "test_edge");
            }

            auto lookup_stats = lookup_tracker.stats();
            collector.record_scalability(
                "edge_lookup",
                edge_count,
                lookup_stats.mean_ns,
                "ns",
                {{"edge_count", std::to_string(edge_count)}});

            // Measure edge insertion performance
            LatencyTracker insert_tracker(100);
            for (int i = 0; i < 100; ++i) {
                uint64_t target = node_ids[edge_count + i];
                auto edge = GraphGenerator::create_test_edge(
                    root->id(), target, graph->get_agent_id());

                auto timer = insert_tracker.scoped_record();
                graph->insert_or_assign_edge(edge);
            }

            auto insert_stats = insert_tracker.stats();
            collector.record_scalability(
                "edge_insert_latency",
                edge_count,
                insert_stats.mean_us(),
                "us",
                {{"edge_count", std::to_string(edge_count)}});

            INFO(edge_count << " edges - Lookup: " << lookup_stats.mean_ns
                 << " ns, Insert: " << insert_stats.mean_us() << " us");
        }
    }

    SECTION("get_nodes performance vs graph size") {
        for (uint32_t size : {100, 1000, 5000}) {
            MultiAgentFixture fixture;
            auto config_file = generator.generate_empty_graph();
            REQUIRE(fixture.create_agents(1, config_file));

            auto* graph = fixture.get_agent(0);
            REQUIRE(graph != nullptr);

            // Populate
            for (uint32_t i = 0; i < size; ++i) {
                auto node = GraphGenerator::create_test_node(
                    5000000 + i, graph->get_agent_id());
                graph->insert_node(node);
            }

            // Measure full scan
            LatencyTracker tracker(100);
            for (int i = 0; i < 100; ++i) {
                auto timer = tracker.scoped_record();
                auto nodes = graph->get_nodes();
            }

            auto stats = tracker.stats();
            collector.record_scalability(
                "get_all_nodes",
                size,
                stats.mean_us(),
                "us",
                {{"graph_size", std::to_string(size)}});

            INFO(size << " nodes - get_nodes: " << stats.mean_us() << " us");
        }
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "graph_size_impact");
}

TEST_CASE("Memory pressure impact", "[SCALABILITY][memory]") {
    MetricsCollector collector("memory_pressure");
    GraphGenerator generator;

    SECTION("Operation latency under memory pressure") {
        MultiAgentFixture fixture;
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture.create_agents(1, config_file));

        auto* graph = fixture.get_agent(0);
        REQUIRE(graph != nullptr);

        // Create increasingly large graph and measure periodically
        std::vector<std::pair<uint32_t, double>> size_vs_latency;

        for (uint32_t target_size : {1000, 5000, 10000, 20000}) {
            // Add nodes to reach target size
            uint64_t current_size = graph->get_nodes().size();
            for (uint64_t i = current_size; i < target_size; ++i) {
                auto node = GraphGenerator::create_test_node(
                    6000000 + i, graph->get_agent_id());
                graph->insert_node(node);
            }

            // Measure insertion latency
            LatencyTracker tracker(50);
            for (int i = 0; i < 50; ++i) {
                auto node = GraphGenerator::create_test_node(
                    7000000 + target_size * 100 + i, graph->get_agent_id());

                auto timer = tracker.scoped_record();
                graph->insert_node(node);
            }

            auto stats = tracker.stats();
            collector.record_scalability(
                "insert_under_pressure",
                target_size,
                stats.mean_us(),
                "us",
                {{"graph_size", std::to_string(target_size)}});

            size_vs_latency.push_back({target_size, stats.mean_us()});
            INFO(target_size << " nodes - Insert latency: " << stats.mean_us() << " us");
        }

        // Check for non-linear degradation
        if (size_vs_latency.size() >= 2) {
            double first_latency = size_vs_latency.front().second;
            double last_latency = size_vs_latency.back().second;
            double size_ratio = static_cast<double>(size_vs_latency.back().first) /
                               static_cast<double>(size_vs_latency.front().first);
            double latency_ratio = last_latency / first_latency;

            collector.record("latency_degradation_ratio", MetricCategory::Scalability,
                latency_ratio / size_ratio, "x");

            INFO("Latency degradation ratio: " << latency_ratio / size_ratio << "x");
        }
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "memory_pressure");
}
