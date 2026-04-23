#ifndef DSR_BENCHMARK_CONFIG_H
#define DSR_BENCHMARK_CONFIG_H

#include <cstdint>
#include <string>
#include <chrono>
#include <dsr/api/dsr_graph_settings.h>

namespace DSR::Benchmark {

struct BenchmarkConfig {
    // Timing configuration
    uint32_t warmup_iterations = 10;
    uint32_t measurement_iterations = 100;
    std::chrono::milliseconds sync_wait_time{200};
    std::chrono::seconds max_convergence_timeout{10};

    // Multi-agent configuration
    uint32_t default_agent_count = 2;
    uint32_t max_agent_count = 16;

    // Graph generation
    uint32_t small_graph_nodes = 100;
    uint32_t medium_graph_nodes = 1000;
    uint32_t large_graph_nodes = 10000;

    // Throughput settings
    uint32_t throughput_duration_seconds = 5;
    uint32_t concurrent_writer_threads = 4;

    // Output settings
    std::string results_directory = "results";
    bool export_json = true;
    bool export_csv = true;
    bool verbose = false;
    DSR::SyncMode sync_mode = DSR::SyncMode::CRDT;
};

inline const char* sync_mode_name(DSR::SyncMode mode) {
    switch (mode) {
        case DSR::SyncMode::CRDT: return "crdt";
        case DSR::SyncMode::LWW: return "lww";
    }
    return "unknown";
}

inline DSR::SyncMode parse_sync_mode(const std::string& value) {
    if (value == "lww" || value == "LWW") {
        return DSR::SyncMode::LWW;
    }
    return DSR::SyncMode::CRDT;
}

inline std::string sync_mode_suffix(DSR::SyncMode mode) {
    return std::string("_") + sync_mode_name(mode);
}

// Default configuration singleton
inline BenchmarkConfig& default_config() {
    static BenchmarkConfig config;
    return config;
}

// Percentile levels for latency statistics
constexpr double PERCENTILE_P50 = 0.50;
constexpr double PERCENTILE_P90 = 0.90;
constexpr double PERCENTILE_P95 = 0.95;
constexpr double PERCENTILE_P99 = 0.99;

// Threshold constants for validation
constexpr uint64_t MAX_EXPECTED_LATENCY_NS = 100'000'000;  // 100ms
constexpr uint64_t MIN_EXPECTED_THROUGHPUT_OPS = 1000;     // 1000 ops/sec

}  // namespace DSR::Benchmark

#endif  // DSR_BENCHMARK_CONFIG_H
