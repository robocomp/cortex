#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <chrono>
#include <atomic>

#include "../core/timing_utils.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/multi_agent_fixture.h"
#include "../fixtures/graph_generator.h"

using namespace DSR;
using namespace DSR::Benchmark;

// Each operation gets its own TEST_CASE so Catch2 doesn't re-run setup for
// every SECTION and overwrite the exported JSON with only the last result.

TEST_CASE("Node insertion throughput", "[THROUGHPUT][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("node_insert_throughput");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    constexpr auto TEST_DURATION = std::chrono::seconds(5);

    // Warmup — 500ms discard to prime caches, branch predictor, allocators
    {
        auto warmup_end = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (std::chrono::steady_clock::now() < warmup_end) {
            auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
            graph->insert_node(node);
        }
    }

    uint64_t operations = 0;
    auto start = std::chrono::steady_clock::now();
    auto end = start + TEST_DURATION;

    while (std::chrono::steady_clock::now() < end) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        graph->insert_node(node);
        operations++;
    }

    auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    collector.record_throughput("node_insert", operations, actual_duration);

    double ops_per_sec = static_cast<double>(operations) /
                        (static_cast<double>(actual_duration.count()) / 1000.0);
    INFO("Node insert throughput: " << ops_per_sec << " ops/sec");
    CHECK(ops_per_sec >= MIN_EXPECTED_THROUGHPUT_OPS);

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_insert_throughput");
}

TEST_CASE("Node read throughput", "[THROUGHPUT][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("node_read_throughput");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    constexpr auto TEST_DURATION = std::chrono::seconds(5);

    std::vector<uint64_t> node_ids;
    node_ids.reserve(1000);
    for (uint64_t i = 0; i < 1000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto result = graph->insert_node(node);
        if (result.has_value()) node_ids.push_back(result.value());
    }
    REQUIRE(!node_ids.empty());

    uint64_t operations = 0;
    auto start = std::chrono::steady_clock::now();
    auto end = start + TEST_DURATION;

    while (std::chrono::steady_clock::now() < end) {
        auto node = graph->get_node(node_ids[operations % node_ids.size()]);
        operations++;
    }

    auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    collector.record_throughput("node_read", operations, actual_duration);

    double ops_per_sec = static_cast<double>(operations) /
                        (static_cast<double>(actual_duration.count()) / 1000.0);
    INFO("Node read throughput: " << ops_per_sec << " ops/sec");

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_read_throughput");
}

TEST_CASE("Node update throughput", "[THROUGHPUT][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("node_update_throughput");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    constexpr auto TEST_DURATION = std::chrono::seconds(5);

    auto test_node = GraphGenerator::create_test_node(0, graph->get_agent_id(), "update_test");
    auto insert_result = graph->insert_node(test_node);
    REQUIRE(insert_result.has_value());
    uint64_t node_id = insert_result.value();

    // Warmup — 500ms discard
    {
        auto warmup_end = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        uint32_t w = 0;
        while (std::chrono::steady_clock::now() < warmup_end) {
            auto node = graph->get_node(node_id);
            if (node) {
                graph->add_or_modify_attrib_local<level_att>(*node, static_cast<int32_t>(w++ % 1000));
                graph->update_node(*node);
            }
        }
    }

    uint64_t operations = 0;
    auto start = std::chrono::steady_clock::now();
    auto end = start + TEST_DURATION;

    while (std::chrono::steady_clock::now() < end) {
        auto node = graph->get_node(node_id);
        if (node) {
            graph->add_or_modify_attrib_local<level_att>(
                *node, static_cast<int32_t>(operations % 1000));
            graph->update_node(*node);
            operations++;
        }
    }

    auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    collector.record_throughput("node_update", operations, actual_duration);

    double ops_per_sec = static_cast<double>(operations) /
                        (static_cast<double>(actual_duration.count()) / 1000.0);
    INFO("Node update throughput: " << ops_per_sec << " ops/sec");
    CHECK(ops_per_sec >= MIN_EXPECTED_THROUGHPUT_OPS);

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_update_throughput");
}

TEST_CASE("Edge insertion throughput", "[THROUGHPUT][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("edge_insert_throughput");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    constexpr auto TEST_DURATION = std::chrono::seconds(5);

    auto root = graph->get_node_root();
    REQUIRE(root.has_value());

    std::vector<uint64_t> target_ids;
    target_ids.reserve(10000);
    for (uint64_t i = 0; i < 10000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto result = graph->insert_node(node);
        if (result.has_value()) target_ids.push_back(result.value());
    }
    REQUIRE(!target_ids.empty());

    uint64_t operations = 0;
    auto start = std::chrono::steady_clock::now();
    auto end = start + TEST_DURATION;

    while (std::chrono::steady_clock::now() < end) {
        uint64_t target = target_ids[operations % target_ids.size()];
        auto edge = GraphGenerator::create_test_edge(root->id(), target, graph->get_agent_id());
        graph->insert_or_assign_edge(edge);
        operations++;
    }

    auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    collector.record_throughput("edge_insert", operations, actual_duration);

    double ops_per_sec = static_cast<double>(operations) /
                        (static_cast<double>(actual_duration.count()) / 1000.0);
    INFO("Edge insert throughput: " << ops_per_sec << " ops/sec");
    CHECK(ops_per_sec >= MIN_EXPECTED_THROUGHPUT_OPS);

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_insert_throughput");
}

TEST_CASE("Edge read throughput", "[THROUGHPUT][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("edge_read_throughput");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    constexpr auto TEST_DURATION = std::chrono::seconds(5);

    auto root = graph->get_node_root();
    REQUIRE(root.has_value());

    std::vector<uint64_t> target_ids;
    target_ids.reserve(1000);
    for (uint64_t i = 0; i < 1000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto result = graph->insert_node(node);
        if (result.has_value()) {
            target_ids.push_back(result.value());
            auto edge = GraphGenerator::create_test_edge(
                root->id(), result.value(), graph->get_agent_id());
            graph->insert_or_assign_edge(edge);
        }
    }
    REQUIRE(!target_ids.empty());

    uint64_t operations = 0;
    auto start = std::chrono::steady_clock::now();
    auto end = start + TEST_DURATION;

    while (std::chrono::steady_clock::now() < end) {
        uint64_t target = target_ids[operations % target_ids.size()];
        auto edge = graph->get_edge(root->id(), target, "test_edge");
        operations++;
    }

    auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    collector.record_throughput("edge_read", operations, actual_duration);

    double ops_per_sec = static_cast<double>(operations) /
                        (static_cast<double>(actual_duration.count()) / 1000.0);
    INFO("Edge read throughput: " << ops_per_sec << " ops/sec");

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_read_throughput");
}

TEST_CASE("Mixed operations throughput", "[THROUGHPUT][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("mixed_ops_throughput");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    constexpr auto TEST_DURATION = std::chrono::seconds(5);

    auto root = graph->get_node_root();
    REQUIRE(root.has_value());

    std::vector<uint64_t> node_ids;
    node_ids.reserve(500);
    for (uint64_t i = 0; i < 500; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto result = graph->insert_node(node);
        if (result.has_value()) node_ids.push_back(result.value());
    }
    REQUIRE(!node_ids.empty());

    uint64_t operations = 0;
    auto start = std::chrono::steady_clock::now();
    auto end = start + TEST_DURATION;

    while (std::chrono::steady_clock::now() < end) {
        int op_type = operations % 10;
        if (op_type < 4) {
            auto node = graph->get_node(node_ids[operations % node_ids.size()]);
        } else if (op_type < 7) {
            auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
            auto result = graph->insert_node(node);
            if (result.has_value()) node_ids.push_back(result.value());
        } else {
            auto node = graph->get_node(node_ids[operations % node_ids.size()]);
            if (node) {
                graph->add_or_modify_attrib_local<level_att>(
                    *node, static_cast<int32_t>(operations));
                graph->update_node(*node);
            }
        }
        operations++;
    }

    auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    collector.record_throughput("mixed_ops", operations, actual_duration);

    double ops_per_sec = static_cast<double>(operations) /
                        (static_cast<double>(actual_duration.count()) / 1000.0);
    INFO("Mixed ops throughput: " << ops_per_sec << " ops/sec");
    CHECK(ops_per_sec >= MIN_EXPECTED_THROUGHPUT_OPS);

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "mixed_ops_throughput");
}

TEST_CASE("Node deletion throughput", "[THROUGHPUT][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("node_delete_throughput");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    constexpr auto TEST_DURATION = std::chrono::seconds(5);

    // Pre-populate a large pool so we can delete without running out.
    // We refill the pool when it drops below a threshold.
    std::vector<uint64_t> node_ids;
    node_ids.reserve(50000);
    for (uint64_t i = 0; i < 50000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        if (res.has_value()) node_ids.push_back(res.value());
    }
    REQUIRE(!node_ids.empty());

    uint64_t operations = 0;
    size_t pool_idx = 0;
    auto start = std::chrono::steady_clock::now();
    auto end = start + TEST_DURATION;

    while (std::chrono::steady_clock::now() < end) {
        if (pool_idx >= node_ids.size()) break;
        graph->delete_node(node_ids[pool_idx++]);
        operations++;
    }

    auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    collector.record_throughput("node_delete", operations, actual_duration);

    double ops_per_sec = static_cast<double>(operations) /
                        (static_cast<double>(actual_duration.count()) / 1000.0);
    INFO("Node delete throughput: " << ops_per_sec << " ops/sec");

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "node_delete_throughput");
}

TEST_CASE("Edge deletion throughput", "[THROUGHPUT][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("edge_delete_throughput");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    constexpr auto TEST_DURATION = std::chrono::seconds(5);

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

    uint64_t operations = 0;
    size_t pool_idx = 0;
    auto start = std::chrono::steady_clock::now();
    auto end = start + TEST_DURATION;

    while (std::chrono::steady_clock::now() < end) {
        if (pool_idx >= target_ids.size()) break;
        graph->delete_edge(root->id(), target_ids[pool_idx++], "test_edge");
        operations++;
    }

    auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    collector.record_throughput("edge_delete", operations, actual_duration);

    double ops_per_sec = static_cast<double>(operations) /
                        (static_cast<double>(actual_duration.count()) / 1000.0);
    INFO("Edge delete throughput: " << ops_per_sec << " ops/sec");

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_delete_throughput");
}

// Catch2 BENCHMARK macros (microbenchmark mode, run with [!benchmark])
TEST_CASE("Single agent operations (Catch2 BENCHMARK)", "[THROUGHPUT][single][!benchmark]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));

    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    BENCHMARK("Node insert") {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        return graph->insert_node(node);
    };

    auto read_node = GraphGenerator::create_test_node(0, graph->get_agent_id());
    auto read_id_opt = graph->insert_node(read_node);
    REQUIRE(read_id_opt.has_value());
    uint64_t read_id = read_id_opt.value();

    BENCHMARK("Node read") {
        return graph->get_node(read_id);
    };

    BENCHMARK("Node update") {
        auto node = graph->get_node(read_id);
        if (node) {
            graph->add_or_modify_attrib_local<level_att>(*node, 42);
            return graph->update_node(*node);
        }
        return false;
    };
}
