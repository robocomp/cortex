#ifndef DSR_NANOBENCH_ADAPTER_H
#define DSR_NANOBENCH_ADAPTER_H

// Bridge between ankerl::nanobench and the MetricsCollector/LatencyStats pipeline.
//
// Usage pattern:
//
//   auto bench = make_latency_bench(1000);          // 1000 samples, 100 warmup
//   bench.run("op_name", [&] {
//       auto result = graph->some_op();
//       ankerl::nanobench::doNotOptimizeAway(result);
//   });
//   collector.record_latency_stats("op_name", nb_to_stats(bench));
//   collector.record("op_name", MetricCategory::Throughput,
//                    nb_throughput(bench), "ops/sec", tags);
//
// make_latency_bench() is intended for steady-state operations and allows the
// call sites to raise minEpochIterations() for very fast paths. For destructive
// or state-mutating workloads, use make_single_op_latency_bench() so each epoch
// stays a single operation and the benchmarked state does not drift with
// nanobench's adaptive iteration counts.

#include <nanobench.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <filesystem>
#include "timing_utils.h"   // LatencyStats

namespace DSR::Benchmark {

// ---------------------------------------------------------------------------
// TeeBuf / nb_report_stream
//
// Writes nanobench table output to both stdout and results/nanobench_report.md
// so the full table is available for offline inspection.
// The file is created/truncated once on the first call; all test cases in the
// same process run append naturally via the shared static ofstream.
// ---------------------------------------------------------------------------
class TeeBuf : public std::streambuf {
public:
    TeeBuf(std::streambuf* a, std::streambuf* b) : a_(a), b_(b) {}
protected:
    int overflow(int c) override {
        if (c == traits_type::eof()) return traits_type::not_eof(c);
        if (a_->sputc(static_cast<char_type>(c)) == traits_type::eof()) return traits_type::eof();
        if (b_->sputc(static_cast<char_type>(c)) == traits_type::eof()) return traits_type::eof();
        return c;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        a_->sputn(s, n);
        return b_->sputn(s, n);
    }
private:
    std::streambuf *a_, *b_;
};

inline std::ostream& nb_report_stream() {
    static std::ofstream file = []() {
        std::filesystem::create_directories("results");
        return std::ofstream("results/nanobench_report.md");
    }();
    static TeeBuf tee(std::cout.rdbuf(), file.rdbuf());
    static std::ostream stream(&tee);
    return stream;
}

// ---------------------------------------------------------------------------
// nb_to_stats
//
// Extracts per-epoch elapsed times from the last benchmark run, sorts them,
// and returns a LatencyStats compatible with MetricsCollector::record_latency_stats().
// Note: nanobench stores elapsed as average time per iteration within each
// epoch, not total epoch time. If a benchmark uses minEpochIterations() > 1,
// the returned distribution is still useful for steady-state throughput/latency
// summaries, but it is not a raw single-operation percentile distribution.
// ---------------------------------------------------------------------------
inline LatencyStats nb_to_stats(const ankerl::nanobench::Bench& bench) {
    using Measure = ankerl::nanobench::Result::Measure;

    if (bench.results().empty()) return {};

    const auto& r = bench.results().back();
    const size_t n = r.size();
    if (n == 0) return {};

    // Collect per-epoch elapsed times in nanoseconds
    std::vector<double> ns(n);
    for (size_t i = 0; i < n; ++i)
        ns[i] = r.get(i, Measure::elapsed) * 1e9;

    std::sort(ns.begin(), ns.end());

    // Percentile helper: nearest-rank
    auto pct = [&](double p) -> uint64_t {
        const size_t idx = static_cast<size_t>(p / 100.0 * static_cast<double>(n - 1) + 0.5);
        return static_cast<uint64_t>(ns[std::min(idx, n - 1)]);
    };

    double sum = 0.0;
    for (double v : ns) sum += v;
    const double mean = sum / static_cast<double>(n);

    double var = 0.0;
    for (double v : ns) var += (v - mean) * (v - mean);

    LatencyStats s{};
    s.count     = n;
    s.mean_ns   = mean;
    s.stddev_ns = (n > 1) ? std::sqrt(var / static_cast<double>(n - 1)) : 0.0;
    s.min_ns    = static_cast<uint64_t>(ns.front());
    s.max_ns    = static_cast<uint64_t>(ns.back());
    s.p50_ns    = pct(50);
    s.p90_ns    = pct(90);
    s.p95_ns    = pct(95);
    s.p99_ns    = pct(99);
    return s;
}

// ---------------------------------------------------------------------------
// nb_throughput
//
// Derives single-operation throughput (ops/sec) from the mean latency of the
// last benchmark run.
// ---------------------------------------------------------------------------
inline double nb_throughput(const ankerl::nanobench::Bench& bench) {
    if (bench.results().empty()) return 0.0;
    using Measure = ankerl::nanobench::Result::Measure;
    const double mean_s = bench.results().back().average(Measure::elapsed);
    return (mean_s > 0.0) ? 1.0 / mean_s : 0.0;
}

// ---------------------------------------------------------------------------
// make_latency_bench
//
// Returns a Bench pre-configured for single-operation latency measurement:
//   epochIterations(1)  — one sample per epoch → full percentile resolution
//   epochs(n_samples)   — total independent latency samples to collect
//   warmup(n_warmup)    — thrown-away warm-up iterations before measurement
//   output(stream)      — tee to stdout + results/nanobench_report.md
// ---------------------------------------------------------------------------
inline ankerl::nanobench::Bench make_latency_bench(
    size_t n_samples  = 1000,
    size_t n_warmup   = 100)
{
    ankerl::nanobench::Bench b;
    b.warmup(n_warmup)
     .epochs(n_samples)
     .minEpochIterations(1)
     .minEpochTime(std::chrono::milliseconds(10))
     .performanceCounters(false)
     .output(&nb_report_stream());
    return b;
}

// Returns a Bench that keeps the measured workload fixed at one operation per
// epoch. This is for destructive or stateful benchmarks where adaptive
// iteration counts would otherwise change the graph shape during the run.
inline ankerl::nanobench::Bench make_single_op_latency_bench(
    size_t n_samples  = 1000,
    size_t n_warmup   = 100)
{
    ankerl::nanobench::Bench b;
    b.warmup(n_warmup)
     .epochs(n_samples)
     .epochIterations(1)
     .performanceCounters(false)
     .output(&nb_report_stream());
    return b;
}

}  // namespace DSR::Benchmark

#endif  // DSR_NANOBENCH_ADAPTER_H
