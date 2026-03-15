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

// Each TEST_CASE measures both throughput (5-second window) and latency
// simultaneously, recording both to the same collector and exporting to a
// unique JSON file.  Tags {"threads","1","graph_size","0"} mark these as the
// single-thread, empty-graph baseline for the Scalability tab.

TEST_CASE("Node insert latency+throughput", "[THROUGHPUT][LATENCY][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("node_insert_lat_thr");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    LatencyTracker tracker(500000);
    uint64_t ops = 0;

    auto start = steady_clock::now();
    auto end = start + seconds(5);

    while (steady_clock::now() < end) {
        auto node = GraphGenerator::create_test_node(ops, graph->get_agent_id());
        {
            auto t = tracker.scoped_record();
            graph->insert_node(node);
        }
        ops++;
    }

    auto dur = duration_cast<milliseconds>(steady_clock::now() - start);
    collector.record_throughput("node_insert", ops, dur,
        {{"threads", "1"}, {"graph_size", "0"}});
    collector.record_latency_stats("node_insert", tracker.stats(),
        {{"threads", "1"}, {"graph_size", "0"}});

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_insert_lat_thr");
}

TEST_CASE("Node read latency+throughput", "[THROUGHPUT][LATENCY][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("node_read_lat_thr");

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
        if (res.has_value()) node_ids.push_back(res.value());
    }
    REQUIRE(!node_ids.empty());

    LatencyTracker tracker(500000);
    uint64_t ops = 0;

    auto start = steady_clock::now();
    auto end = start + seconds(5);

    while (steady_clock::now() < end) {
        uint64_t id = node_ids[ops % node_ids.size()];
        {
            auto t = tracker.scoped_record();
            auto node = graph->get_node(id);
        }
        ops++;
    }

    auto dur = duration_cast<milliseconds>(steady_clock::now() - start);
    collector.record_throughput("node_read", ops, dur,
        {{"threads", "1"}, {"graph_size", "0"}});
    collector.record_latency_stats("node_read", tracker.stats(),
        {{"threads", "1"}, {"graph_size", "0"}});

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_read_lat_thr");
}

TEST_CASE("Node update latency+throughput", "[THROUGHPUT][LATENCY][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("node_update_lat_thr");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    auto test_node = GraphGenerator::create_test_node(0, graph->get_agent_id(), "update_test");
    auto insert_result = graph->insert_node(test_node);
    REQUIRE(insert_result.has_value());
    uint64_t node_id = insert_result.value();

    LatencyTracker tracker(500000);
    uint64_t ops = 0;

    auto start = steady_clock::now();
    auto end = start + seconds(5);

    while (steady_clock::now() < end) {
        auto node = graph->get_node(node_id);
        if (node) {
            graph->add_or_modify_attrib_local<level_att>(
                *node, static_cast<int32_t>(ops % 1000));
            {
                auto t = tracker.scoped_record();
                graph->update_node(*node);
            }
            ops++;
        }
    }

    auto dur = duration_cast<milliseconds>(steady_clock::now() - start);
    collector.record_throughput("node_update", ops, dur,
        {{"threads", "1"}, {"graph_size", "0"}});
    collector.record_latency_stats("node_update", tracker.stats(),
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
        if (res.has_value()) target_ids.push_back(res.value());
    }
    REQUIRE(!target_ids.empty());

    LatencyTracker tracker(500000);
    uint64_t ops = 0;

    auto start = steady_clock::now();
    auto end = start + seconds(5);

    while (steady_clock::now() < end) {
        uint64_t target = target_ids[ops % target_ids.size()];
        auto edge = GraphGenerator::create_test_edge(root->id(), target, graph->get_agent_id());
        {
            auto t = tracker.scoped_record();
            graph->insert_or_assign_edge(edge);
        }
        ops++;
    }

    auto dur = duration_cast<milliseconds>(steady_clock::now() - start);
    collector.record_throughput("edge_insert", ops, dur,
        {{"threads", "1"}, {"graph_size", "0"}});
    collector.record_latency_stats("edge_insert", tracker.stats(),
        {{"threads", "1"}, {"graph_size", "0"}});

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_insert_lat_thr");
}

TEST_CASE("Edge read latency+throughput", "[THROUGHPUT][LATENCY][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("edge_read_lat_thr");

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
        if (res.has_value()) {
            target_ids.push_back(res.value());
            auto edge = GraphGenerator::create_test_edge(
                root->id(), res.value(), graph->get_agent_id());
            graph->insert_or_assign_edge(edge);
        }
    }
    REQUIRE(!target_ids.empty());

    LatencyTracker tracker(500000);
    uint64_t ops = 0;

    auto start = steady_clock::now();
    auto end = start + seconds(5);

    while (steady_clock::now() < end) {
        uint64_t target = target_ids[ops % target_ids.size()];
        {
            auto t = tracker.scoped_record();
            auto edge = graph->get_edge(root->id(), target, "test_edge");
        }
        ops++;
    }

    auto dur = duration_cast<milliseconds>(steady_clock::now() - start);
    collector.record_throughput("edge_read", ops, dur,
        {{"threads", "1"}, {"graph_size", "0"}});
    collector.record_latency_stats("edge_read", tracker.stats(),
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

    // Pre-populate a large pool to delete from
    std::vector<uint64_t> node_ids;
    node_ids.reserve(50000);
    for (uint64_t i = 0; i < 50000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        if (res.has_value()) node_ids.push_back(res.value());
    }
    REQUIRE(!node_ids.empty());

    LatencyTracker tracker(500000);
    uint64_t ops = 0;
    size_t pool_idx = 0;

    auto start = steady_clock::now();
    auto end = start + seconds(5);

    while (steady_clock::now() < end && pool_idx < node_ids.size()) {
        {
            auto t = tracker.scoped_record();
            graph->delete_node(node_ids[pool_idx++]);
        }
        ops++;
    }

    auto dur = duration_cast<milliseconds>(steady_clock::now() - start);
    collector.record_throughput("node_delete", ops, dur,
        {{"threads", "1"}, {"graph_size", "0"}});
    collector.record_latency_stats("node_delete", tracker.stats(),
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

    // Pre-populate 50K nodes with edges from root → node
    std::vector<uint64_t> target_ids;
    target_ids.reserve(50000);
    for (uint64_t i = 0; i < 50000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        if (res.has_value()) {
            auto edge = GraphGenerator::create_test_edge(
                root->id(), res.value(), graph->get_agent_id());
            graph->insert_or_assign_edge(edge);
            target_ids.push_back(res.value());
        }
    }
    REQUIRE(!target_ids.empty());

    LatencyTracker tracker(500000);
    uint64_t ops = 0;
    size_t pool_idx = 0;

    auto start = steady_clock::now();
    auto end = start + seconds(5);

    while (steady_clock::now() < end && pool_idx < target_ids.size()) {
        {
            auto t = tracker.scoped_record();
            graph->delete_edge(root->id(), target_ids[pool_idx++], "test_edge");
        }
        ops++;
    }

    auto dur = duration_cast<milliseconds>(steady_clock::now() - start);
    collector.record_throughput("edge_delete", ops, dur,
        {{"threads", "1"}, {"graph_size", "0"}});
    collector.record_latency_stats("edge_delete", tracker.stats(),
        {{"threads", "1"}, {"graph_size", "0"}});

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_delete_lat_thr");
}
