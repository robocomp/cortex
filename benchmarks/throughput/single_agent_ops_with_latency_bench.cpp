#include <catch2/catch_test_macros.hpp>

#include "../core/nanobench_adapter.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/multi_agent_fixture.h"
#include "../fixtures/graph_generator.h"

using namespace DSR;
using namespace DSR::Benchmark;

// Each TEST_CASE measures both latency and throughput simultaneously using
// nanobench. Steady-state read/update paths may raise minEpochIterations() to
// reduce timer noise. Destructive/stateful workloads use
// make_single_op_latency_bench() so graph growth does not drift across runs.
// Tags {"threads","1","graph_size","0"} mark these as the single-thread,
// empty-graph baseline for the Scalability tab.

TEST_CASE("Node insert latency+throughput", "[THROUGHPUT][LATENCY][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("node_insert_lat_thr");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    uint64_t id_counter = 0;
    auto sampled = run_sampled_benchmark(
        50,
        1000,
        [&] {
        auto node = GraphGenerator::create_test_node(id_counter++, graph->get_agent_id());
        auto res = graph->insert_node(node);
        REQUIRE(res.has_value());
        },
        [&] { fixture.process_events(1); },
        16);

    collector.record_latency_stats("node_insert", sampled.latency,
        {{"threads", "1"}, {"graph_size", "0"}});
    collector.record_throughput("node_insert", sampled.latency.count, sampled.wall_time,
        {{"threads", "1"}, {"graph_size", "0"}});

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_insert_lat_thr");
}

TEST_CASE("Node read latency+throughput", "[THROUGHPUT][LATENCY][single][EXTENDED][.extended]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("node_read_lat_thr");
    collector.add_metadata("profile", "extended");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    // Pre-populate 1000 nodes for round-robin reads
    std::vector<uint64_t> node_ids;
    node_ids.reserve(1000);
    for (uint64_t i = 0; i < 1000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        REQUIRE(res.has_value());
        node_ids.push_back(res.value());
    }

    // Cache warmup: touch every node once so the read loop is warm
    for (auto id : node_ids) { (void)graph->get_node(id); }

    // ~900 ns/op: 10 000 iters/epoch × 200 epochs ≈ 1.8 s
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
    collector.record_latency_stats("node_read", stats,
        {{"threads", "1"}, {"graph_size", "0"}});
    collector.record("node_read", MetricCategory::Throughput,
        nb_throughput(bench), "ops/sec",
        {{"threads", "1"}, {"graph_size", "0"}});

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_read_lat_thr");
}

TEST_CASE("Node update latency+throughput", "[THROUGHPUT][LATENCY][single][EXTENDED][.extended]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("node_update_lat_thr");
    collector.add_metadata("profile", "extended");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    auto test_node = GraphGenerator::create_test_node(0, graph->get_agent_id(), "update_test");
    auto insert_result = graph->insert_node(test_node);
    REQUIRE(insert_result.has_value());
    uint64_t node_id = insert_result.value();

    // ~38µs/op: 300 iters/epoch × 50 epochs ≈ 0.57 s
    uint64_t update_counter = 0;
    auto bench = make_latency_bench(50);
    bench.minEpochIterations(300);
    bench.run("node_update", [&] {
        auto node = graph->get_node(node_id);
        REQUIRE(node.has_value());
        graph->add_or_modify_attrib_local<level_att>(
            *node, static_cast<int32_t>(update_counter++ % 1000));
        bool ok = graph->update_node(*node);
        REQUIRE(ok);
        ankerl::nanobench::doNotOptimizeAway(ok);
    });

    auto stats = nb_to_stats(bench);
    collector.record_latency_stats("node_update", stats,
        {{"threads", "1"}, {"graph_size", "0"}});
    collector.record("node_update", MetricCategory::Throughput,
        nb_throughput(bench), "ops/sec",
        {{"threads", "1"}, {"graph_size", "0"}});

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_update_lat_thr");
}

TEST_CASE("Edge insert latency+throughput", "[THROUGHPUT][LATENCY][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("edge_insert_lat_thr");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    auto root = graph->get_node_root();
    REQUIRE(root.has_value());

    // Pre-populate target node pool
    std::vector<uint64_t> target_ids;
    target_ids.reserve(10000);
    for (uint64_t i = 0; i < 10000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        REQUIRE(res.has_value());
        target_ids.push_back(res.value());
    }

    size_t idx = 0;
    auto sampled = run_sampled_benchmark(
        50,
        1000,
        [&] {
        uint64_t target = target_ids[idx++ % target_ids.size()];
        auto edge = GraphGenerator::create_test_edge(root->id(), target, graph->get_agent_id());
        bool ok = graph->insert_or_assign_edge(edge);
        REQUIRE(ok);
        },
        [&] { fixture.process_events(1); },
        8);

    collector.record_latency_stats("edge_insert", sampled.latency,
        {{"threads", "1"}, {"graph_size", "0"}});
    collector.record_throughput("edge_insert", sampled.latency.count, sampled.wall_time,
        {{"threads", "1"}, {"graph_size", "0"}});

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_insert_lat_thr");
}

TEST_CASE("Edge read latency+throughput", "[THROUGHPUT][LATENCY][single][EXTENDED][.extended]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("edge_read_lat_thr");
    collector.add_metadata("profile", "extended");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    auto root = graph->get_node_root();
    REQUIRE(root.has_value());

    // Pre-populate 1000 nodes + edges
    std::vector<uint64_t> target_ids;
    target_ids.reserve(1000);
    for (uint64_t i = 0; i < 1000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        REQUIRE(res.has_value());
        target_ids.push_back(res.value());
        auto edge = GraphGenerator::create_test_edge(
            root->id(), res.value(), graph->get_agent_id());
        REQUIRE(graph->insert_or_assign_edge(edge));
    }

    // Cache warmup
    for (auto tid : target_ids) { (void)graph->get_edge(root->id(), tid, "test_edge"); }

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
    collector.record_latency_stats("edge_read", stats,
        {{"threads", "1"}, {"graph_size", "0"}});
    collector.record("edge_read", MetricCategory::Throughput,
        nb_throughput(bench), "ops/sec",
        {{"threads", "1"}, {"graph_size", "0"}});

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_read_lat_thr");
}

TEST_CASE("Node delete latency+throughput", "[THROUGHPUT][LATENCY][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("node_delete_lat_thr");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    // Pool is 3× the expected maximum (warmup + epochs) so nanobench auto-tuning
    // cannot exhaust it; the REQUIRE fires loudly if it somehow does.
    std::vector<uint64_t> node_ids;
    node_ids.reserve(3000);
    for (uint64_t i = 0; i < 3000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        REQUIRE(res.has_value());
        node_ids.push_back(res.value());
    }

    size_t pool_idx = 0;
    auto sampled = run_sampled_benchmark(
        50,
        1000,
        [&] {
        REQUIRE(pool_idx < node_ids.size());
        bool ok = graph->delete_node(node_ids[pool_idx++]);
        REQUIRE(ok);
        },
        [&] { fixture.process_events(1); },
        16);

    collector.record_latency_stats("node_delete", sampled.latency,
        {{"threads", "1"}, {"graph_size", "0"}});
    collector.record_throughput("node_delete", sampled.latency.count, sampled.wall_time,
        {{"threads", "1"}, {"graph_size", "0"}});

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_delete_lat_thr");
}

TEST_CASE("Edge delete latency+throughput", "[THROUGHPUT][LATENCY][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("edge_delete_lat_thr");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    auto root = graph->get_node_root();
    REQUIRE(root.has_value());

    // Pool is 3× the expected maximum (warmup + epochs) so nanobench auto-tuning
    // cannot exhaust it; the REQUIRE fires loudly if it somehow does.
    std::vector<uint64_t> target_ids;
    target_ids.reserve(3000);
    for (uint64_t i = 0; i < 3000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        REQUIRE(res.has_value());
        auto edge = GraphGenerator::create_test_edge(
            root->id(), res.value(), graph->get_agent_id());
        REQUIRE(graph->insert_or_assign_edge(edge));
        target_ids.push_back(res.value());
    }

    size_t pool_idx = 0;
    auto sampled = run_sampled_benchmark(
        50,
        1000,
        [&] {
        REQUIRE(pool_idx < target_ids.size());
        bool ok = graph->delete_edge(root->id(), target_ids[pool_idx++], "test_edge");
        REQUIRE(ok);
        },
        [&] { fixture.process_events(1); },
        8);

    collector.record_latency_stats("edge_delete", sampled.latency,
        {{"threads", "1"}, {"graph_size", "0"}});
    collector.record_throughput("edge_delete", sampled.latency.count, sampled.wall_time,
        {{"threads", "1"}, {"graph_size", "0"}});

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_delete_lat_thr");
}
