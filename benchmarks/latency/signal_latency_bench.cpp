#include <catch2/catch_test_macros.hpp>
#include <atomic>

#include "../core/nanobench_adapter.h"
#include "../core/timing_utils.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/multi_agent_fixture.h"
#include "../fixtures/graph_generator.h"

using namespace DSR;
using namespace DSR::Benchmark;

// For Qt::DirectConnection cases the signal fires synchronously within the
// graph operation, so nanobench's elapsed time equals the dispatch latency.
// For Qt::QueuedConnection the callback fires asynchronously via the Qt event
// loop — manual bench_now() timing with fixture.process_events() is required.

TEST_CASE("Node signal direct latency", "[LATENCY][signal][EXTENDED][.extended]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("signal_latency");
    collector.add_metadata("profile", "extended");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    std::atomic<bool> callback_called{false};

    QObject::connect(graph, &DSR::DSRGraph::update_node_signal, graph,
        [&](uint64_t, const std::string&, DSR::SignalInfo) {
            callback_called.store(true);
        }, Qt::DirectConnection);

    // ~40µs/op: 300 iters/epoch × 100 epochs ≈ 1.2 s
    auto bench = make_latency_bench(100, 50);
    bench.minEpochIterations(300);
    bench.run("node_signal_direct", [&] {
        callback_called.store(false);
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        graph->insert_node(node);
        REQUIRE(callback_called.load());
        ankerl::nanobench::doNotOptimizeAway(node);
    });

    auto stats = nb_to_stats(bench);
    collector.record_latency_stats("node_signal_direct", stats);
    INFO("Node signal (direct) - Mean: " << stats.mean_us() << " us, p99: " << stats.p99_us() << " us");

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "signal_node_direct");
}

TEST_CASE("Edge signal direct latency", "[LATENCY][signal][EXTENDED][.extended]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("signal_latency");
    collector.add_metadata("profile", "extended");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    auto root = graph->get_node_root();
    REQUIRE(root.has_value());

    // Pre-create enough nodes for warmup(50) + epochs(1000) = 1050
    std::vector<uint64_t> node_ids;
    node_ids.reserve(1060);
    for (int i = 0; i < 1060; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto id = graph->insert_node(node);
        REQUIRE(id.has_value());
        node_ids.push_back(*id);
    }

    std::atomic<bool> callback_called{false};
    std::atomic<uint64_t> target_to{0};

    QObject::connect(graph, &DSR::DSRGraph::update_edge_signal, graph,
        [&](uint64_t, uint64_t to, const std::string&, DSR::SignalInfo) {
            if (to == target_to.load()) {
                callback_called.store(true);
            }
        }, Qt::DirectConnection);

    // ~14µs/op: 600 iters/epoch × 100 epochs ≈ 0.84 s
    size_t idx = 0;
    auto bench = make_latency_bench(100, 50);
    bench.minEpochIterations(600);
    bench.run("edge_signal_direct", [&] {
        uint64_t target = node_ids[idx++ % node_ids.size()];
        target_to.store(target);
        callback_called.store(false);
        auto edge = GraphGenerator::create_test_edge(
            root->id(), target, graph->get_agent_id());
        graph->insert_or_assign_edge(edge);
        REQUIRE(callback_called.load());
        ankerl::nanobench::doNotOptimizeAway(edge);
    });

    auto stats = nb_to_stats(bench);
    collector.record_latency_stats("edge_signal_direct", stats);
    INFO("Edge signal (direct) - Mean: " << stats.mean_us() << " us, p99: " << stats.p99_us() << " us");

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "signal_edge_direct");
}

TEST_CASE("Attribute signal direct latency", "[LATENCY][signal][EXTENDED][.extended]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("signal_latency");
    collector.add_metadata("profile", "extended");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    auto test_node = GraphGenerator::create_test_node(0, graph->get_agent_id(), "attr_signal_test");
    auto node_id = graph->insert_node(test_node);
    REQUIRE(node_id.has_value());

    std::atomic<bool> callback_called{false};

    QObject::connect(graph, &DSR::DSRGraph::update_node_attr_signal, graph,
        [&](uint64_t id, const std::vector<std::string>&, DSR::SignalInfo) {
            if (id == *node_id) {
                callback_called.store(true);
            }
        }, Qt::DirectConnection);

    uint64_t counter = 0;
    auto bench = make_latency_bench(1000, 50);
    bench.run("attr_signal_direct", [&] {
        callback_called.store(false);
        auto node = graph->get_node(*node_id);
        REQUIRE(node.has_value());
        graph->add_or_modify_attrib_local<level_att>(
            *node, static_cast<int32_t>(100 + counter++));
        graph->update_node(*node);
        REQUIRE(callback_called.load());
        ankerl::nanobench::doNotOptimizeAway(node);
    });

    auto stats = nb_to_stats(bench);
    collector.record_latency_stats("attr_signal_direct", stats);
    INFO("Attr signal (direct) - Mean: " << stats.mean_us() << " us, p99: " << stats.p99_us() << " us");

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "signal_attr_direct");
}

TEST_CASE("Node signal queued latency", "[LATENCY][signal]") {
    // Qt::QueuedConnection dispatches via the event loop, so the callback
    // fires asynchronously.  nanobench cannot model the poll-wait pattern;
    // manual bench_now() + fixture.process_events() is used instead.
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("signal_latency");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    LatencyTracker tracker(1000);
    std::atomic<uint64_t> callback_time{0};
    std::atomic<bool> callback_called{false};

    QObject::connect(graph, &DSR::DSRGraph::update_node_signal, graph,
        [&](uint64_t, const std::string&, DSR::SignalInfo) {
            callback_time.store(bench_now());
            callback_called.store(true);
        }, Qt::QueuedConnection);

    // Warmup
    for (int i = 0; i < 50; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        REQUIRE(res.has_value());
        fixture.process_events();
    }

    // Measurement
    for (int i = 0; i < 1000; ++i) {
        callback_called.store(false);
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        uint64_t pre_insert = bench_now();
        auto res = graph->insert_node(node);
        REQUIRE(res.has_value());

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        while (!callback_called.load() && std::chrono::steady_clock::now() < deadline) {
            fixture.process_events(1);
        }

        if (callback_called.load()) {
            tracker.record(callback_time.load() - pre_insert);
        }
    }

    auto stats = tracker.stats();
    collector.record_latency_stats("node_signal_queued", stats);
    INFO("Node signal (queued) - Mean: " << stats.mean_us() << " us, p99: " << stats.p99_us() << " us");

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "signal_node_queued");
}

TEST_CASE("Signal emission under load", "[LATENCY][signal][stress][PROFILE][LOAD]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("signal_latency_stress");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));
    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    // Pre-populate graph with 1000 nodes
    for (int i = 0; i < 1000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto res = graph->insert_node(node);
        REQUIRE(res.has_value());
    }
    fixture.process_events();

    std::atomic<bool> callback_called{false};

    QObject::connect(graph, &DSR::DSRGraph::update_node_signal, graph,
        [&](uint64_t, const std::string&, DSR::SignalInfo) {
            callback_called.store(true);
        }, Qt::DirectConnection);

    auto bench = make_latency_bench(1000, 50);
    bench.minEpochIterations(10);
    bench.run("signal_under_load", [&] {
        callback_called.store(false);
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        graph->insert_node(node);
        REQUIRE(callback_called.load());
        ankerl::nanobench::doNotOptimizeAway(node);
    });

    auto stats = nb_to_stats(bench);
    collector.record_latency_stats("signal_with_1000_nodes", stats, {{"existing_nodes", "1000"}});
    INFO("Signal with 1000 nodes - Mean: " << stats.mean_us() << " us, p99: " << stats.p99_us() << " us");

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "signal_latency_stress");
}
