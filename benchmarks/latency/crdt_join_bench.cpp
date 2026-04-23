#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <dsr/core/crdt/delta_crdt.h>
#include <dsr/core/types/crdt_types.h>
#include "../core/nanobench_adapter.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"

using namespace DSR::Benchmark;

// Create a test attribute
static DSR::CRDTAttribute make_test_attribute(uint32_t agent_id, int32_t value) {
    DSR::CRDTAttribute attr;
    attr.value(value);
    attr.timestamp(bench_now());
    attr.agent_id(agent_id);
    return attr;
}

// All four mvreg operations in a single TEST_CASE so they export together
// to one JSON file.
TEST_CASE("CRDT mvreg operations", "[CRDT][MVREG][BASELINE]") {
    if (strcmp(sync_mode_name(default_config().sync_mode),"lww")) return;
    MetricsCollector collector("crdt_mvreg");
    collector.add_metadata("profile", "baseline");

    // ── mvreg write ───────────────────────────────────────────────────────────
    {
        mvreg<DSR::CRDTAttribute> reg;
        reg.id = 100;
        int i = 0;

        auto bench = make_latency_bench();
        bench.run("mvreg_write", [&] {
            auto attr = make_test_attribute(100, i++);
            auto delta = reg.write(attr);
            ankerl::nanobench::doNotOptimizeAway(attr);
            ankerl::nanobench::doNotOptimizeAway(delta);
        });
        collector.record_latency_stats("mvreg_write", nb_to_stats(bench));
    }

    // ── mvreg join (same agent) ───────────────────────────────────────────────
    {
        mvreg<DSR::CRDTAttribute> reg;
        reg.id = 100;
        auto init_attr = make_test_attribute(100, 0);
        reg.write(init_attr);
        int i = 0;

        auto bench = make_latency_bench();
        bench.run("mvreg_join_same_agent", [&] {
            mvreg<DSR::CRDTAttribute> delta_reg;
            delta_reg.id = 100;
            auto new_attr = make_test_attribute(100, i++);
            auto delta = delta_reg.write(new_attr);
            reg.join(std::move(delta));
            ankerl::nanobench::doNotOptimizeAway(reg);
        });
        collector.record_latency_stats("mvreg_join_same_agent", nb_to_stats(bench));
    }

    // ── mvreg join (different agents) ────────────────────────────────────────
    {
        int i = 0;

        auto bench = make_latency_bench();
        bench.run("mvreg_join_different_agent", [&] {
            mvreg<DSR::CRDTAttribute> reg;
            reg.id = 100;
            auto attr = make_test_attribute(100, 0);
            auto delta = reg.write(attr);

            uint32_t other_agent = 200 + (i % 10);
            mvreg<DSR::CRDTAttribute> delta_reg;
            delta_reg.id = other_agent;
            delta_reg.join(std::move(delta));
            auto new_attr = make_test_attribute(other_agent, i * 2);
            delta = delta_reg.write(new_attr);

            reg.join(std::move(delta));
            ankerl::nanobench::doNotOptimizeAway(reg);
            ++i;
        });
        collector.record_latency_stats("mvreg_join_different_agent", nb_to_stats(bench));
    }

    // ── mvreg read ────────────────────────────────────────────────────────────
    {
        mvreg<DSR::CRDTAttribute> reg;
        reg.id = 100;
        auto attr = make_test_attribute(100, 42);
        reg.write(attr);

        // Read is pure — no warmup needed (cache already warm after write)
        auto bench = make_latency_bench(1000, 0);
        bench.minEpochIterations(10);
        bench.run("mvreg_read", [&] {
            const auto& value = reg.read_reg();
            ankerl::nanobench::doNotOptimizeAway(value);
        });
        collector.record_latency_stats("mvreg_read", nb_to_stats(bench));
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "crdt_mvreg");
}

TEST_CASE("CRDT dot_context operations", "[CRDT][DOT_CONTEXT][BASELINE]") {
    if (strcmp(sync_mode_name(default_config().sync_mode),"lww")) return;
    MetricsCollector collector("crdt_dot_context");
    collector.add_metadata("profile", "baseline");

    // ── makedot ───────────────────────────────────────────────────────────────
    {
        dot_context ctx;
        int i = 0;

        auto bench = make_latency_bench();
        bench.minEpochIterations(10);
        bench.run("dot_context_makedot", [&] {
            auto dot = ctx.makedot(100 + (i++ % 10));
            ankerl::nanobench::doNotOptimizeAway(dot);
        });
        collector.record_latency_stats("dot_context_makedot", nb_to_stats(bench));
    }

    // ── dotin ─────────────────────────────────────────────────────────────────
    {
        dot_context ctx;
        for (int i = 0; i < 100; ++i) ctx.makedot(100 + (i % 10));
        int i = 0;

        auto bench = make_latency_bench(1000, 0);
        bench.minEpochIterations(10);
        bench.run("dot_context_dotin", [&] {
            std::pair<key_type, int> dot{100 + (i++ % 10), i % 50};
            bool r = ctx.dotin(dot);
            ankerl::nanobench::doNotOptimizeAway(r);
        });
        collector.record_latency_stats("dot_context_dotin", nb_to_stats(bench));
    }

    // ── join ──────────────────────────────────────────────────────────────────
    {
        auto bench = make_latency_bench();
        bench.run("dot_context_join", [&] {
            dot_context ctx1;
            dot_context ctx2;
            for (int j = 0; j < 10; ++j) {
                ctx1.makedot(100);
                ctx2.makedot(200);
            }
            ctx1.join(ctx2);
            ankerl::nanobench::doNotOptimizeAway(ctx1);
        });
        collector.record_latency_stats("dot_context_join", nb_to_stats(bench));
    }

    // ── compact ───────────────────────────────────────────────────────────────
    {
        auto bench = make_latency_bench();
        bench.run("dot_context_compact", [&] {
            dot_context ctx;
            for (int j = 0; j < 50; ++j) ctx.insertdot({100, j * 2}, false);
            ctx.compact();
            ankerl::nanobench::doNotOptimizeAway(ctx);
        });
        collector.record_latency_stats("dot_context_compact", nb_to_stats(bench));
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "crdt_dot_context");
}
