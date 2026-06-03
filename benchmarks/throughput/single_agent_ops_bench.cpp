#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "../core/nanobench_adapter.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/multi_agent_fixture.h"
#include "../fixtures/graph_generator.h"

#include <memory>
#include <tuple>

using namespace DSR;
using namespace DSR::Benchmark;

// Each operation gets its own TEST_CASE.  nanobench replaces the manual
// 5-second time-window loops: it auto-tunes warmup and iteration count,
// and derives throughput from the mean latency (nb_throughput()).

TEST_CASE("Node insertion throughput", "[THROUGHPUT][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("node_insert_throughput");

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

    collector.record_latency_stats("node_insert", sampled.latency);
    collector.record_throughput("node_insert", sampled.latency.count, sampled.wall_time);

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

    std::vector<uint64_t> node_ids;
    node_ids.reserve(1000);
    for (uint64_t i = 0; i < 1000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto result = graph->insert_node(node);
        REQUIRE(result.has_value());
        node_ids.push_back(result.value());
    }

    // Cache warmup
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

    collector.record_latency_stats("node_read", nb_to_stats(bench));
    collector.record("node_read", MetricCategory::Throughput,
                     nb_throughput(bench), "ops/sec");

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

    collector.record_latency_stats("node_update", nb_to_stats(bench));
    collector.record("node_update", MetricCategory::Throughput,
                     nb_throughput(bench), "ops/sec");

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

    auto root = graph->get_node_root();
    REQUIRE(root.has_value());

    std::vector<uint64_t> target_ids;
    target_ids.reserve(10000);
    for (uint64_t i = 0; i < 10000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto result = graph->insert_node(node);
        REQUIRE(result.has_value());
        target_ids.push_back(result.value());
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

    collector.record_latency_stats("edge_insert", sampled.latency);
    collector.record_throughput("edge_insert", sampled.latency.count, sampled.wall_time);

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_insert_throughput");
}

TEST_CASE("Edge upsert mode diagnostics", "[THROUGHPUT][LATENCY][diagnostic][edge]") {
    GraphGenerator generator;
    MetricsCollector collector("edge_upsert_modes");

    auto make_graph_with_targets = [&](uint64_t target_count, bool preinsert_edges) {
        auto fixture = std::make_unique<MultiAgentFixture>();
        auto config_file = generator.generate_empty_graph();
        REQUIRE(fixture->create_agents(1, config_file));
        auto* graph = fixture->get_agent(0);
        REQUIRE(graph != nullptr);

        auto root = graph->get_node_root();
        REQUIRE(root.has_value());

        std::vector<uint64_t> targets;
        targets.reserve(target_count);
        for (uint64_t i = 0; i < target_count; ++i) {
            auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
            auto result = graph->insert_node(node);
            REQUIRE(result.has_value());
            targets.push_back(result.value());
            if (preinsert_edges) {
                auto edge = GraphGenerator::create_test_edge(root->id(), result.value(), graph->get_agent_id());
                REQUIRE(graph->insert_or_assign_edge(edge));
            }
        }

        return std::tuple{std::move(fixture), root->id(), std::move(targets)};
    };

    {
        auto [fixture, root_id, targets] = make_graph_with_targets(12000, false);
        auto* graph = fixture->get_agent(0);
        size_t idx = 0;
        auto sampled = run_sampled_benchmark(
            50,
            10000,
            [&] {
                auto edge = GraphGenerator::create_test_edge(root_id, targets[idx++], graph->get_agent_id());
                REQUIRE(graph->insert_or_assign_edge(edge));
            },
            [&] { fixture->process_events(1); },
            8);
        collector.record_latency_stats("edge_fresh_insert", sampled.latency);
        collector.record_throughput("edge_fresh_insert", sampled.latency.count, sampled.wall_time);
    }

    {
        auto [fixture, root_id, targets] = make_graph_with_targets(10000, true);
        auto* graph = fixture->get_agent(0);
        size_t idx = 0;
        auto sampled = run_sampled_benchmark(
            50,
            10000,
            [&] {
                auto edge = GraphGenerator::create_test_edge(root_id, targets[idx++ % targets.size()], graph->get_agent_id());
                REQUIRE(graph->insert_or_assign_edge(edge));
            },
            [&] { fixture->process_events(1); },
            8);
        collector.record_latency_stats("edge_reassign_same_empty", sampled.latency);
        collector.record_throughput("edge_reassign_same_empty", sampled.latency.count, sampled.wall_time);
    }

    {
        auto [fixture, root_id, targets] = make_graph_with_targets(10000, true);
        auto* graph = fixture->get_agent(0);
        size_t idx = 0;
        int32_t value = 0;
        auto sampled = run_sampled_benchmark(
            50,
            10000,
            [&] {
                auto edge = GraphGenerator::create_test_edge(root_id, targets[idx++ % targets.size()], graph->get_agent_id());
                edge.attrs().insert_or_assign("diagnostic_value",
                    Attribute(static_cast<int32_t>(value++), 0, graph->get_agent_id()));
                REQUIRE(graph->insert_or_assign_edge(edge));
            },
            [&] { fixture->process_events(1); },
            8);
        collector.record_latency_stats("edge_reassign_changed_attr", sampled.latency);
        collector.record_throughput("edge_reassign_changed_attr", sampled.latency.count, sampled.wall_time);
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "edge_upsert_modes");
}

TEST_CASE("Edge read throughput", "[THROUGHPUT][single]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("edge_read_throughput");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    auto root = graph->get_node_root();
    REQUIRE(root.has_value());

    std::vector<uint64_t> target_ids;
    target_ids.reserve(1000);
    for (uint64_t i = 0; i < 1000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto result = graph->insert_node(node);
        REQUIRE(result.has_value());
        target_ids.push_back(result.value());
        auto edge = GraphGenerator::create_test_edge(
            root->id(), result.value(), graph->get_agent_id());
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

    collector.record_latency_stats("edge_read", nb_to_stats(bench));
    collector.record("edge_read", MetricCategory::Throughput,
                     nb_throughput(bench), "ops/sec");

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

    auto root = graph->get_node_root();
    REQUIRE(root.has_value());

    std::vector<uint64_t> node_ids;
    node_ids.reserve(600); // 500 initial + up to ~100 inserts from 30% insert rate × 1100 calls
    for (uint64_t i = 0; i < 500; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto result = graph->insert_node(node);
        REQUIRE(result.has_value());
        node_ids.push_back(result.value());
    }

    uint64_t ops = 0;
    auto sampled = run_sampled_benchmark(
        50,
        1000,
        [&] {
        int op_type = static_cast<int>(ops % 10);
        if (op_type < 4) {
            auto node = graph->get_node(node_ids[ops % node_ids.size()]);
            ankerl::nanobench::doNotOptimizeAway(node);
        } else if (op_type < 7) {
            auto node = GraphGenerator::create_test_node(ops, graph->get_agent_id());
            auto result = graph->insert_node(node);
            REQUIRE(result.has_value());
            node_ids.push_back(result.value());
        } else {
            auto node = graph->get_node(node_ids[ops % node_ids.size()]);
            REQUIRE(node.has_value());
            graph->add_or_modify_attrib_local<level_att>(
                *node, static_cast<int32_t>(ops));
            bool ok = graph->update_node(*node);
            REQUIRE(ok);
        }
        ++ops;
        },
        [&] { fixture.process_events(1); },
        16);

    collector.record_latency_stats("mixed_ops", sampled.latency);
    collector.record_throughput("mixed_ops", sampled.latency.count, sampled.wall_time);

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

    collector.record_latency_stats("node_delete", sampled.latency);
    collector.record_throughput("node_delete", sampled.latency.count, sampled.wall_time);

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

    collector.record_latency_stats("edge_delete", sampled.latency);
    collector.record_throughput("edge_delete", sampled.latency.count, sampled.wall_time);

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
