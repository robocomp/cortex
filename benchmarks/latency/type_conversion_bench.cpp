#include <catch2/catch_test_macros.hpp>

#include <dsr/core/types/crdt_io.h>
#include <dsr/core/types/lww_io.h>
#include <dsr/core/types/lww_index.h>
#include <dsr/core/types/user_types.h>
#include <dsr/core/types/crdt_types.h>

#include "../core/nanobench_adapter.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/graph_generator.h"

using namespace DSR;
using namespace DSR::Benchmark;

// ── Helpers ──────────────────────────────────────────────────────────────────

static Attribute make_attr(int32_t v) {
    return Attribute(v, 0, 1);
}

static Node make_node(uint32_t n_attrs, uint32_t n_edges) {
    GraphGenerator::register_test_types();
    Node node;
    node.id(1000);
    node.name("conv_test_node");
    node.type("test_node");
    node.agent_id(1);

    auto& attrs = node.attrs();
    for (uint32_t i = 0; i < n_attrs; ++i) {
        switch (i % 5) {
            case 0: attrs.emplace("attr_int_"   + std::to_string(i), Attribute(static_cast<int32_t>(i),    0, 1)); break;
            case 1: attrs.emplace("attr_float_" + std::to_string(i), Attribute(static_cast<float>(i) * 0.1f, 0, 1)); break;
            case 2: attrs.emplace("attr_bool_"  + std::to_string(i), Attribute(static_cast<bool>(i % 2),    0, 1)); break;
            case 3: attrs.emplace("attr_str_"   + std::to_string(i), Attribute(std::string("val_" + std::to_string(i)), 0, 1)); break;
            case 4: attrs.emplace("attr_u64_"   + std::to_string(i), Attribute(static_cast<uint64_t>(i * 1000), 0, 1)); break;
        }
    }

    auto& fano = node.fano();
    for (uint32_t e = 0; e < n_edges; ++e) {
        uint64_t dst = 2000 + e;
        Edge edge(dst, 1000, "test_edge", {}, 1);
        auto& eattrs = edge.attrs();
        eattrs.emplace("weight", Attribute(static_cast<float>(e) * 0.5f, 0, 1));
        fano.emplace(std::make_pair(dst, std::string("test_edge")), std::move(edge));
    }

    return node;
}

static Edge make_edge(uint32_t n_attrs) {
    GraphGenerator::register_test_types();
    Edge edge(2000, 1000, "test_edge", {}, 1);
    auto& attrs = edge.attrs();
    for (uint32_t i = 0; i < n_attrs; ++i) {
        attrs.emplace("ea_" + std::to_string(i), Attribute(static_cast<int32_t>(i), 0, 1));
    }
    return edge;
}

// ── CRDT node/edge conversions ────────────────────────────────────────────────

TEST_CASE("CRDT type conversion latency", "[LATENCY][CONVERSION][BASELINE]") {
    MetricsCollector collector("crdt_type_conversion");
    collector.add_metadata("profile", "baseline");

    for (auto [n_attrs, n_edges, label] : std::initializer_list<std::tuple<uint32_t, uint32_t, const char*>>{
            {0,  0,  "bare"},
            {5,  0,  "light"},
            {20, 0,  "heavy_attrs"},
            {5,  5,  "light_with_edges"},
            {20, 20, "heavy"},
        })
    {
        Node node = make_node(n_attrs, n_edges);

        {
            auto bench = make_latency_bench();
            bench.run(std::string("user_node_to_crdt_") + label, [&] {
                auto crdt = user_node_to_crdt(node);
                ankerl::nanobench::doNotOptimizeAway(crdt);
            });
            collector.record_latency_stats("user_node_to_crdt",
                nb_to_stats(bench), {{"config", label}});
        }

        {
            CRDT::Node crdt_node = user_node_to_crdt(node);

            auto bench = make_latency_bench();
            bench.run(std::string("crdt_node_to_user_") + label, [&] {
                auto out = to_user_node(crdt_node);
                ankerl::nanobench::doNotOptimizeAway(out);
            });
            collector.record_latency_stats("crdt_node_to_user",
                nb_to_stats(bench), {{"config", label}});
        }

        Edge edge = make_edge(n_attrs);

        {
            auto bench = make_latency_bench();
            bench.run(std::string("user_edge_to_crdt_") + label, [&] {
                auto crdt = user_edge_to_crdt(edge);
                ankerl::nanobench::doNotOptimizeAway(crdt);
            });
            collector.record_latency_stats("user_edge_to_crdt",
                nb_to_stats(bench), {{"config", label}});
        }

        {
            CRDT::Edge crdt_edge = user_edge_to_crdt(edge);

            auto bench = make_latency_bench();
            bench.run(std::string("crdt_edge_to_user_") + label, [&] {
                auto out = to_user_edge(crdt_edge);
                ankerl::nanobench::doNotOptimizeAway(out);
            });
            collector.record_latency_stats("crdt_edge_to_user",
                nb_to_stats(bench), {{"config", label}});
        }
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "crdt_type_conversion");
}

// ── LWW node/edge conversions ─────────────────────────────────────────────────

TEST_CASE("LWW type conversion latency", "[LATENCY][CONVERSION][BASELINE]") {
    MetricsCollector collector("lww_type_conversion");
    collector.add_metadata("profile", "baseline");

    const LWW::Version ver{12345678ULL, 1};

    for (auto [n_attrs, n_edges, label] : std::initializer_list<std::tuple<uint32_t, uint32_t, const char*>>{
            {0,  0,  "bare"},
            {5,  0,  "light"},
            {20, 0,  "heavy_attrs"},
            {5,  5,  "light_with_edges"},
            {20, 20, "heavy"},
        })
    {
        Node node = make_node(n_attrs, n_edges);

        {
            auto bench = make_latency_bench();
            bench.run(std::string("to_node_state_") + label, [&] {
                auto state = LWW::to_node_state(node, ver, 1);
                ankerl::nanobench::doNotOptimizeAway(state);
            });
            collector.record_latency_stats("to_node_state",
                nb_to_stats(bench), {{"config", label}});
        }

        {
            LWW::NodeState state = LWW::to_node_state(node, ver, 1);

            // Build a FromIndex with the node's outgoing edges.
            LWW::FromIndex from_idx;
            std::vector<LWW::EdgeState> edge_states;
            edge_states.reserve(n_edges);
            for (const auto& [key, edge] : node.fano()) {
                Edge e(key.first, node.id(), key.second, edge.attrs(), 1);
                edge_states.push_back(LWW::to_edge_state(e, ver, 1));
            }
            for (const auto& es : edge_states) {
                from_idx[es.from].insert(&es);
            }

            auto bench = make_latency_bench();
            bench.run(std::string("lww_node_to_user_") + label, [&] {
                auto out = LWW::to_user_node(state, from_idx);
                ankerl::nanobench::doNotOptimizeAway(out);
            });
            collector.record_latency_stats("lww_node_to_user",
                nb_to_stats(bench), {{"config", label}});
        }

        Edge edge = make_edge(n_attrs);

        {
            auto bench = make_latency_bench();
            bench.run(std::string("to_edge_state_") + label, [&] {
                auto state = LWW::to_edge_state(edge, ver, 1);
                ankerl::nanobench::doNotOptimizeAway(state);
            });
            collector.record_latency_stats("to_edge_state",
                nb_to_stats(bench), {{"config", label}});
        }

        {
            LWW::EdgeState state = LWW::to_edge_state(edge, ver, 1);

            auto bench = make_latency_bench();
            bench.run(std::string("lww_edge_to_user_") + label, [&] {
                auto out = LWW::to_user_edge(state);
                ankerl::nanobench::doNotOptimizeAway(out);
            });
            collector.record_latency_stats("lww_edge_to_user",
                nb_to_stats(bench), {{"config", label}});
        }
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "lww_type_conversion");
}

// ── Wire message serialization ────────────────────────────────────────────────

TEST_CASE("Wire message conversion latency", "[LATENCY][CONVERSION][BASELINE]") {
    MetricsCollector collector("wire_conversion");
    collector.add_metadata("profile", "baseline");

    const LWW::Version ver{12345678ULL, 1};

    for (auto [n_attrs, label] : std::initializer_list<std::pair<uint32_t, const char*>>{
            {0,  "bare"},
            {5,  "light"},
            {20, "heavy"},
        })
    {
        Node node = make_node(n_attrs, 0);
        Edge edge = make_edge(n_attrs);

        {
            LWW::NodeState state = LWW::to_node_state(node, ver, 1);

            auto bench = make_latency_bench();
            bench.run(std::string("lww_node_to_msg_") + label, [&] {
                auto msg = LWW::to_node_msg(state);
                ankerl::nanobench::doNotOptimizeAway(msg);
            });
            collector.record_latency_stats("lww_node_to_msg",
                nb_to_stats(bench), {{"config", label}});
        }

        {
            LWW::EdgeState state = LWW::to_edge_state(edge, ver, 1);

            auto bench = make_latency_bench();
            bench.run(std::string("lww_edge_to_msg_") + label, [&] {
                auto msg = LWW::to_edge_msg(state);
                ankerl::nanobench::doNotOptimizeAway(msg);
            });
            collector.record_latency_stats("lww_edge_to_msg",
                nb_to_stats(bench), {{"config", label}});
        }
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "wire_conversion");
}
