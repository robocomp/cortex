#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <dsr/core/crdt/delta_crdt.h>
#include <dsr/core/types/crdt_types.h>
#include "../core/timing_utils.h"
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
// to one JSON file.  No Catch2 SECTIONs — each measurement block runs
// sequentially so all metrics accumulate in one collector.
TEST_CASE("CRDT mvreg operations", "[CRDT][mvreg]") {
    MetricsCollector collector("crdt_mvreg");

    // ── mvreg write ───────────────────────────────────────────────────────────
    {
        LatencyTracker tracker(1000);
        mvreg<DSR::CRDTAttribute> reg;
        reg.id = 100;

        for (int i = 0; i < 1000; ++i) {
            auto attr = make_test_attribute(100, i);
            uint64_t start = bench_now();
            auto delta = reg.write(attr);
            tracker.record(bench_now() - start);
        }
        collector.record_latency_stats("mvreg_write", tracker.stats());
        INFO("mvreg::write mean: " << tracker.stats().mean_ns << " ns");
    }

    // ── mvreg join (same agent) ───────────────────────────────────────────────
    {
        LatencyTracker tracker(1000);
        mvreg<DSR::CRDTAttribute> reg;
        reg.id = 100;

        auto init_attr = make_test_attribute(100, 0);
        reg.write(init_attr);

        for (int i = 0; i < 1000; ++i) {
            mvreg<DSR::CRDTAttribute> delta_reg;
            delta_reg.id = 100;
            auto new_attr = make_test_attribute(100, i);
            auto delta = delta_reg.write(new_attr);

            uint64_t start = bench_now();
            reg.join(std::move(delta));
            tracker.record(bench_now() - start);
        }
        collector.record_latency_stats("mvreg_join_same_agent", tracker.stats());
        INFO("mvreg::join (same agent) mean: " << tracker.stats().mean_ns << " ns");
    }

    // ── mvreg join (different agents) ────────────────────────────────────────
    {
        LatencyTracker tracker(1000);

        for (int i = 0; i < 1000; ++i) {
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

            uint64_t start = bench_now();
            reg.join(std::move(delta));
            tracker.record(bench_now() - start);
        }
        collector.record_latency_stats("mvreg_join_different_agent", tracker.stats());
        INFO("mvreg::join (different agent) mean: " << tracker.stats().mean_ns << " ns");
    }

    // ── mvreg read ────────────────────────────────────────────────────────────
    {
        LatencyTracker tracker(1000);
        mvreg<DSR::CRDTAttribute> reg;
        reg.id = 100;

        auto attr = make_test_attribute(100, 42);
        reg.write(attr);

        for (int i = 0; i < 1000; ++i) {
            uint64_t start = bench_now();
            [[maybe_unused]] const auto& value = reg.read_reg();
            tracker.record(bench_now() - start);
        }
        collector.record_latency_stats("mvreg_read", tracker.stats());
        INFO("mvreg::read mean: " << tracker.stats().mean_ns << " ns");
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "crdt_mvreg");
}

TEST_CASE("CRDT dot_context operations", "[CRDT][dot_context]") {
    MetricsCollector collector("crdt_dot_context");

    // ── makedot ───────────────────────────────────────────────────────────────
    {
        LatencyTracker tracker(1000);
        dot_context ctx;

        for (int i = 0; i < 1000; ++i) {
            uint64_t start = bench_now();
            auto dot = ctx.makedot(100 + (i % 10));
            tracker.record(bench_now() - start);
        }
        collector.record_latency_stats("dot_context_makedot", tracker.stats());
        INFO("dot_context::makedot mean: " << tracker.stats().mean_ns << " ns");
    }

    // ── dotin ─────────────────────────────────────────────────────────────────
    {
        dot_context ctx;
        for (int i = 0; i < 100; ++i) ctx.makedot(100 + (i % 10));

        LatencyTracker tracker(1000);
        for (int i = 0; i < 1000; ++i) {
            std::pair<key_type, int> dot{100 + (i % 10), i % 50};
            uint64_t start = bench_now();
            [[maybe_unused]] bool r = ctx.dotin(dot);
            tracker.record(bench_now() - start);
        }
        collector.record_latency_stats("dot_context_dotin", tracker.stats());
        INFO("dot_context::dotin mean: " << tracker.stats().mean_ns << " ns");
    }

    // ── join ──────────────────────────────────────────────────────────────────
    {
        LatencyTracker tracker(1000);

        for (int i = 0; i < 1000; ++i) {
            dot_context ctx1;
            dot_context ctx2;
            for (int j = 0; j < 10; ++j) {
                ctx1.makedot(100);
                ctx2.makedot(200);
            }
            uint64_t start = bench_now();
            ctx1.join(ctx2);
            tracker.record(bench_now() - start);
        }
        collector.record_latency_stats("dot_context_join", tracker.stats());
        INFO("dot_context::join mean: " << tracker.stats().mean_ns << " ns");
    }

    // ── compact ───────────────────────────────────────────────────────────────
    {
        LatencyTracker tracker(1000);

        for (int i = 0; i < 1000; ++i) {
            dot_context ctx;
            for (int j = 0; j < 50; ++j) ctx.insertdot({100, j * 2}, false);

            uint64_t start = bench_now();
            ctx.compact();
            tracker.record(bench_now() - start);
        }
        collector.record_latency_stats("dot_context_compact", tracker.stats());
        INFO("dot_context::compact mean: " << tracker.stats().mean_ns << " ns");
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "crdt_dot_context");
}

// Catch2 BENCHMARK macros — kept hidden; run with [!benchmark] to activate.
TEST_CASE("CRDT micro-benchmarks (Catch2 BENCHMARK)", "[.][crdt][!benchmark]") {

    BENCHMARK("mvreg write") {
        mvreg<DSR::CRDTAttribute> reg;
        reg.id = 100;
        auto attr = make_test_attribute(100, 42);
        return reg.write(attr);
    };

    BENCHMARK("mvreg join") {
        mvreg<DSR::CRDTAttribute> reg;
        reg.id = 100;
        auto attr1 = make_test_attribute(100, 1);
        auto delta = reg.write(attr1);

        mvreg<DSR::CRDTAttribute> delta_reg;
        delta_reg.id = 200;
        delta_reg.join(std::move(delta));
        auto attr2 = make_test_attribute(200, 2);
        delta = delta_reg.write(attr2);

        reg.join(std::move(delta));
        return reg.read_reg();
    };

    BENCHMARK("dot_context makedot") {
        dot_context ctx;
        return ctx.makedot(100);
    };

    BENCHMARK("dot_context join") {
        dot_context ctx1;
        dot_context ctx2;
        for (int i = 0; i < 10; ++i) {
            ctx1.makedot(100);
            ctx2.makedot(200);
        }
        ctx1.join(ctx2);
        return ctx1.cc.size();
    };
}
