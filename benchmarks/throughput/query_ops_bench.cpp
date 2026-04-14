#include <catch2/catch_test_macros.hpp>

#include "../core/nanobench_adapter.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/multi_agent_fixture.h"
#include "../fixtures/graph_generator.h"

using namespace DSR;
using namespace DSR::Benchmark;

TEST_CASE("Graph query convenience operations", "[EXTENDED][LATENCY][THROUGHPUT][query][single][.extended]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("graph_query_baseline");
    collector.add_metadata("profile", "extended");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    auto root = graph->get_node_root();
    REQUIRE(root.has_value());

    std::vector<uint64_t> node_ids;
    node_ids.reserve(1000);
    for (uint64_t i = 0; i < 1000; ++i) {
        auto node = GraphGenerator::create_test_node(i, graph->get_agent_id(), "query_node_" + std::to_string(i));
        auto inserted = graph->insert_node(node);
        REQUIRE(inserted.has_value());
        node_ids.push_back(*inserted);

        auto edge = GraphGenerator::create_test_edge(root->id(), *inserted, graph->get_agent_id());
        REQUIRE(graph->insert_or_assign_edge(edge));
    }

    for (auto id : node_ids) {
        (void)graph->get_node(id);
    }
    (void)graph->get_nodes();
    (void)graph->get_nodes_by_type("test_node");
    (void)graph->get_edges(root->id());
    (void)graph->get_edges_to_id(root->id());
    (void)graph->get_edges_by_type("test_edge");

    {
        auto bench = make_latency_bench(1000, 0);
        bench.minEpochIterations(10);
        bench.run("get_nodes", [&] {
            auto nodes = graph->get_nodes();
            ankerl::nanobench::doNotOptimizeAway(nodes);
        });
        collector.record_latency_stats("get_nodes", nb_to_stats(bench));
        collector.record("get_nodes", MetricCategory::Throughput, nb_throughput(bench), "ops/sec");
    }

    {
        auto bench = make_latency_bench(1000, 0);
        bench.minEpochIterations(10);
        bench.run("get_nodes_by_type", [&] {
            auto nodes = graph->get_nodes_by_type("test_node");
            ankerl::nanobench::doNotOptimizeAway(nodes);
        });
        collector.record_latency_stats("get_nodes_by_type", nb_to_stats(bench));
        collector.record("get_nodes_by_type", MetricCategory::Throughput, nb_throughput(bench), "ops/sec");
    }

    {
        auto bench = make_latency_bench(1000, 0);
        bench.minEpochIterations(20);
        bench.run("get_edges_from_root", [&] {
            auto edges = graph->get_edges(root->id());
            ankerl::nanobench::doNotOptimizeAway(edges);
        });
        collector.record_latency_stats("get_edges_from_root", nb_to_stats(bench));
        collector.record("get_edges_from_root", MetricCategory::Throughput, nb_throughput(bench), "ops/sec");
    }

    {
        auto bench = make_latency_bench(1000, 0);
        bench.minEpochIterations(20);
        bench.run("get_edges_to_root", [&] {
            auto edges = graph->get_edges_to_id(root->id());
            ankerl::nanobench::doNotOptimizeAway(edges);
        });
        collector.record_latency_stats("get_edges_to_root", nb_to_stats(bench));
        collector.record("get_edges_to_root", MetricCategory::Throughput, nb_throughput(bench), "ops/sec");
    }

    {
        auto bench = make_latency_bench(1000, 0);
        bench.minEpochIterations(20);
        bench.run("get_edges_by_type", [&] {
            auto edges = graph->get_edges_by_type("test_edge");
            ankerl::nanobench::doNotOptimizeAway(edges);
        });
        collector.record_latency_stats("get_edges_by_type", nb_to_stats(bench));
        collector.record("get_edges_by_type", MetricCategory::Throughput, nb_throughput(bench), "ops/sec");
    }

    {
        size_t idx = 0;
        auto bench = make_latency_bench(1000, 0);
        bench.minEpochIterations(5000);
        bench.run("get_name_from_id", [&] {
            auto name = graph->get_name_from_id(node_ids[idx++ % node_ids.size()]);
            REQUIRE(name.has_value());
            ankerl::nanobench::doNotOptimizeAway(name);
        });
        collector.record_latency_stats("get_name_from_id", nb_to_stats(bench));
        collector.record("get_name_from_id", MetricCategory::Throughput, nb_throughput(bench), "ops/sec");
    }

    {
        size_t idx = 0;
        auto bench = make_latency_bench(1000, 0);
        bench.minEpochIterations(5000);
        bench.run("get_id_from_name", [&] {
            auto id = graph->get_id_from_name("query_node_" + std::to_string(idx++ % node_ids.size()));
            REQUIRE(id.has_value());
            ankerl::nanobench::doNotOptimizeAway(id);
        });
        collector.record_latency_stats("get_id_from_name", nb_to_stats(bench));
        collector.record("get_id_from_name", MetricCategory::Throughput, nb_throughput(bench), "ops/sec");
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "graph_query_baseline");
}
