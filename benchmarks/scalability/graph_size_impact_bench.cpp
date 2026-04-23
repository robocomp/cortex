#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "../core/nanobench_adapter.h"
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
            for (uint32_t i = 0; i < static_cast<uint32_t>(size); ++i) {
                auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
                auto result = graph->insert_node(node);
                REQUIRE(result.has_value());
                node_ids.push_back(result.value());
            }

            // Cache warmup: touch every node once
            for (const auto id : node_ids) { (void)graph->get_node(id); }

            size_t idx = 0;
            bool last_ok = true;
            auto bench = make_latency_bench(1000, 0); // manual warmup done above
            bench.run("node_lookup", [&] {
                auto node = graph->get_node(node_ids[idx++ % node_ids.size()]);
                last_ok = node.has_value();
                ankerl::nanobench::doNotOptimizeAway(node);
            });
            REQUIRE(last_ok);

            auto stats = nb_to_stats(bench);
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
            for (uint32_t i = 0; i < static_cast<uint32_t>(size); ++i) {
                auto node = GraphGenerator::create_test_node(
                    2000000 + i, graph->get_agent_id());
                auto res = graph->insert_node(node);
                REQUIRE(res.has_value());
            }

            // ~35µs/op: 300 iters/epoch × 50 epochs ≈ 0.53 s
            uint64_t id_counter = 3000000;
            auto bench = make_latency_bench(50);
            bench.minEpochIterations(300);
            bench.run("node_insert", [&] {
                auto node = GraphGenerator::create_test_node(
                    id_counter++, graph->get_agent_id());
                auto res = graph->insert_node(node);
                REQUIRE(res.has_value());
                ankerl::nanobench::doNotOptimizeAway(res);
            });

            auto stats = nb_to_stats(bench);
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
                auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
                auto result = graph->insert_node(node);
                REQUIRE(result.has_value());
                node_ids.push_back(result.value());
            }

            // Create edges for the first edge_count nodes
            for (uint32_t i = 0; i < edge_count; ++i) {
                auto edge = GraphGenerator::create_test_edge(
                    root->id(), node_ids[i], graph->get_agent_id());
                REQUIRE(graph->insert_or_assign_edge(edge));
            }

            // Cache warmup: touch every existing edge once
            for (uint32_t i = 0; i < edge_count; ++i) {
                (void)graph->get_edge(root->id(), node_ids[i], "test_edge");
            }

            // Measure edge lookup performance
            // ~32µs at edge_count=100 (unstable, needs 300 iters); larger counts are stable.
            size_t lookup_min_iters = (edge_count <= 100) ? 300 : 1;
            size_t lookup_epochs   = (edge_count <= 100) ? 50  : 200;
            size_t lookup_idx = 0;
            bool last_ok = true;
            auto lookup_bench = make_latency_bench(lookup_epochs, 0); // manual warmup done above
            lookup_bench.minEpochIterations(lookup_min_iters);
            lookup_bench.run("edge_lookup", [&] {
                uint64_t target = node_ids[lookup_idx++ % edge_count];
                auto edge = graph->get_edge(root->id(), target, "test_edge");
                last_ok = edge.has_value();
                ankerl::nanobench::doNotOptimizeAway(edge);
            });
            REQUIRE(last_ok);

            auto lookup_stats = nb_to_stats(lookup_bench);
            collector.record_scalability(
                "edge_lookup",
                edge_count,
                lookup_stats.mean_ns,
                "ns",
                {{"edge_count", std::to_string(edge_count)}});

            // Measure edge insertion performance (last 100 nodes have no edges yet)
            // ~13µs/op (idempotent upsert): 800 iters/epoch × 50 epochs ≈ 0.52 s
            size_t insert_idx = 0;
            auto insert_bench = make_latency_bench(50);
            insert_bench.minEpochIterations(800);
            insert_bench.run("edge_insert", [&] {
                uint64_t target = node_ids[edge_count + (insert_idx++ % 100)];
                auto edge = GraphGenerator::create_test_edge(
                    root->id(), target, graph->get_agent_id());
                bool ok = graph->insert_or_assign_edge(edge);
                REQUIRE(ok);
                ankerl::nanobench::doNotOptimizeAway(ok);
            });

            auto insert_stats = nb_to_stats(insert_bench);
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

    SECTION("Query operations vs graph size") {
        // get_nodes, get_nodes_by_type, and name/id lookups — all at fixed graph sizes.
        // These are O(n) or O(1) operations; measuring them here lets us detect
        // regressions in their scaling without a separate query bench file.
        for (uint32_t size : {100u, 1000u, 5000u}) {
            MultiAgentFixture fixture;
            auto config_file = generator.generate_empty_graph();
            REQUIRE(fixture.create_agents(1, config_file));

            auto* graph = fixture.get_agent(0);
            REQUIRE(graph != nullptr);

            std::vector<uint64_t> node_ids;
            node_ids.reserve(size);
            for (uint32_t i = 0; i < size; ++i) {
                auto node = GraphGenerator::create_test_node(
                    i, graph->get_agent_id(), "query_node_" + std::to_string(i));
                auto inserted = graph->insert_node(node);
                REQUIRE(inserted.has_value());
                node_ids.push_back(*inserted);
            }

            // Cache warmup
            (void)graph->get_nodes();
            (void)graph->get_nodes_by_type("test_node");

            const std::string sz = std::to_string(size);

            {
                auto bench = make_latency_bench(100, 0);
                bench.minEpochIterations(5);
                bench.run("get_nodes", [&] {
                    auto nodes = graph->get_nodes();
                    ankerl::nanobench::doNotOptimizeAway(nodes);
                });
                auto stats = nb_to_stats(bench);
                collector.record_scalability("get_nodes", size, stats.mean_us(), "us",
                    {{"graph_size", sz}});
            }

            {
                auto bench = make_latency_bench(100, 0);
                bench.minEpochIterations(5);
                bench.run("get_nodes_by_type", [&] {
                    auto nodes = graph->get_nodes_by_type("test_node");
                    ankerl::nanobench::doNotOptimizeAway(nodes);
                });
                auto stats = nb_to_stats(bench);
                collector.record_scalability("get_nodes_by_type", size, stats.mean_us(), "us",
                    {{"graph_size", sz}});
            }

            {
                size_t idx = 0;
                auto bench = make_latency_bench(200, 0);
                bench.minEpochIterations(1000);
                bench.run("get_name_from_id", [&] {
                    auto name = graph->get_name_from_id(node_ids[idx++ % node_ids.size()]);
                    ankerl::nanobench::doNotOptimizeAway(name);
                });
                auto stats = nb_to_stats(bench);
                collector.record_scalability("get_name_from_id", size, stats.mean_ns, "ns",
                    {{"graph_size", sz}});
            }

            {
                size_t idx = 0;
                auto bench = make_latency_bench(200, 0);
                bench.minEpochIterations(1000);
                bench.run("get_id_from_name", [&] {
                    auto id = graph->get_id_from_name("query_node_" + std::to_string(idx++ % size));
                    ankerl::nanobench::doNotOptimizeAway(id);
                });
                auto stats = nb_to_stats(bench);
                collector.record_scalability("get_id_from_name", size, stats.mean_ns, "ns",
                    {{"graph_size", sz}});
            }

            INFO(size << " nodes — query ops measured");
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
            for (uint32_t i = 0; i < static_cast<uint32_t>(size); ++i) {
                auto node = GraphGenerator::create_test_node(
                    5000000 + i, graph->get_agent_id());
                auto res = graph->insert_node(node);
                REQUIRE(res.has_value());
            }

            auto bench = make_latency_bench(100);
            bench.run("get_all_nodes", [&] {
                auto nodes = graph->get_nodes();
                ankerl::nanobench::doNotOptimizeAway(nodes);
            });

            auto stats = nb_to_stats(bench);
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
                auto res = graph->insert_node(node);
                REQUIRE(res.has_value());
            }

            // ~28–41µs/op: 500 iters/epoch × 50 epochs ≈ 0.7–1.0 s
            uint64_t id_counter = 7000000 + static_cast<uint64_t>(target_size) * 100;
            auto bench = make_latency_bench(50);
            bench.minEpochIterations(500);
            bench.run("insert_under_pressure", [&] {
                auto node = GraphGenerator::create_test_node(
                    id_counter++, graph->get_agent_id());
                auto res = graph->insert_node(node);
                REQUIRE(res.has_value());
                ankerl::nanobench::doNotOptimizeAway(res);
            });

            auto stats = nb_to_stats(bench);
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
