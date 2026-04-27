#ifndef DSR_METRICS_COLLECTOR_H
#define DSR_METRICS_COLLECTOR_H

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>
#include <memory>
#include "timing_utils.h"
#include "benchmark_config.h"

namespace DSR::Benchmark {

// Categories of benchmark metrics
enum class MetricCategory {
    Latency,
    Throughput,
    Scalability,
    Consistency
};

inline std::string to_string(MetricCategory cat) {
    switch (cat) {
        case MetricCategory::Latency: return "latency";
        case MetricCategory::Throughput: return "throughput";
        case MetricCategory::Scalability: return "scalability";
        case MetricCategory::Consistency: return "consistency";
    }
    return "unknown";
}


// Individual metric measurement
struct Metric {
    std::string name;
    MetricCategory category;
    std::string unit;
    double value;
    std::map<std::string, double> additional_values;  // For percentiles, etc.
    std::map<std::string, std::string> tags;          // For categorization
};


// Result of a complete benchmark run
struct BenchmarkResult {
    std::string benchmark_name;
    std::string timestamp;
    std::chrono::milliseconds total_duration;
    std::vector<Metric> metrics;
    std::map<std::string, std::string> metadata;
};


// Thread-safe collector for benchmark metrics
class MetricsCollector {
public:
    MetricsCollector() = default;

    explicit MetricsCollector(std::string benchmark_name)
        : benchmark_name_(std::move(benchmark_name))
        , start_time_(std::chrono::steady_clock::now())
    {}

    // Set benchmark name
    void set_benchmark_name(const std::string& name) {
        std::lock_guard lock(mutex_);
        benchmark_name_ = name;
    }

    // Add metadata
    void add_metadata(const std::string& key, const std::string& value) {
        std::lock_guard lock(mutex_);
        metadata_[key] = value;
    }

    // Record a simple metric
    void record(const std::string& name, MetricCategory category,
                double value, const std::string& unit = "") {
        Metric m;
        m.name = name;
        m.category = category;
        m.value = value;
        m.unit = unit;

        std::lock_guard lock(mutex_);
        metrics_.push_back(std::move(m));
    }

    // Record a metric with tags
    void record(const std::string& name, MetricCategory category,
                double value, const std::string& unit,
                const std::map<std::string, std::string>& tags) {
        Metric m;
        m.name = name;
        m.category = category;
        m.value = value;
        m.unit = unit;
        m.tags = tags;

        std::lock_guard lock(mutex_);
        metrics_.push_back(std::move(m));
    }

    // Record latency statistics from a LatencyTracker
    void record_latency_stats(const std::string& name, LatencyStats stats,
                              const std::map<std::string, std::string>& tags = {}) {
        Metric m;
        m.name = name;
        m.category = MetricCategory::Latency;
        m.value = stats.mean_ns;
        m.unit = "ns";
        m.tags = tags;
        m.additional_values["count"] = static_cast<double>(stats.count);
        m.additional_values["mean_ns"] = stats.mean_ns;
        m.additional_values["stddev_ns"] = stats.stddev_ns;
        m.additional_values["min_ns"] = static_cast<double>(stats.min_ns);
        m.additional_values["max_ns"] = static_cast<double>(stats.max_ns);
        m.additional_values["p50_ns"] = static_cast<double>(stats.p50_ns);
        m.additional_values["p90_ns"] = static_cast<double>(stats.p90_ns);
        m.additional_values["p95_ns"] = static_cast<double>(stats.p95_ns);
        m.additional_values["p99_ns"] = static_cast<double>(stats.p99_ns);

        std::lock_guard lock(mutex_);
        metrics_.push_back(std::move(m));
    }

    // Record throughput
    void record_throughput(const std::string& name, uint64_t operations,
                           std::chrono::milliseconds duration,
                           const std::map<std::string, std::string>& tags = {}) {
        double ops_per_sec = static_cast<double>(operations) /
                             (static_cast<double>(duration.count()) / 1000.0);

        Metric m;
        m.name = name;
        m.category = MetricCategory::Throughput;
        m.value = ops_per_sec;
        m.unit = "ops/sec";
        m.tags = tags;
        m.additional_values["total_operations"] = static_cast<double>(operations);
        m.additional_values["duration_ms"] = static_cast<double>(duration.count());

        std::lock_guard lock(mutex_);
        metrics_.push_back(std::move(m));
    }

    // Record scalability metric
    void record_scalability(const std::string& name, uint32_t scale_factor,
                            double metric_value, const std::string& unit,
                            const std::map<std::string, std::string>& tags = {}) {
        Metric m;
        m.name = name;
        m.category = MetricCategory::Scalability;
        m.value = metric_value;
        m.unit = unit;
        m.tags = tags;
        m.additional_values["scale_factor"] = static_cast<double>(scale_factor);

        std::lock_guard lock(mutex_);
        metrics_.push_back(std::move(m));
    }

    // Record consistency metric
    void record_consistency(const std::string& name, double value,
                            const std::string& unit,
                            const std::map<std::string, std::string>& tags = {}) {
        Metric m;
        m.name = name;
        m.category = MetricCategory::Consistency;
        m.value = value;
        m.unit = unit;
        m.tags = tags;

        std::lock_guard lock(mutex_);
        metrics_.push_back(std::move(m));
    }

    // Get all metrics by category
    [[nodiscard]] std::vector<Metric> get_metrics(MetricCategory category) const {
        std::lock_guard lock(mutex_);
        std::vector<Metric> result;
        for (const auto& m : metrics_) {
            if (m.category == category) {
                result.push_back(m);
            }
        }
        return result;
    }

    // Get all metrics
    [[nodiscard]] std::vector<Metric> get_all_metrics() const {
        std::lock_guard lock(mutex_);
        return metrics_;
    }

    // Generate final result
    [[nodiscard]] BenchmarkResult finalize() {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time_);

        // Generate timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        char timestamp_buf[64];
        std::strftime(timestamp_buf, sizeof(timestamp_buf), "%Y-%m-%dT%H:%M:%S",
                      std::localtime(&time_t_now));

        std::lock_guard lock(mutex_);
        BenchmarkResult result;
        result.benchmark_name = benchmark_name_;
        result.timestamp = timestamp_buf;
        result.total_duration = duration;
        result.metrics = metrics_;
        result.metadata = metadata_;

        return result;
    }

    // Clear all collected metrics
    void clear() {
        std::lock_guard lock(mutex_);
        metrics_.clear();
        metadata_.clear();
        start_time_ = std::chrono::steady_clock::now();
    }

private:
    mutable std::mutex mutex_;
    std::string benchmark_name_;
    std::chrono::steady_clock::time_point start_time_;
    std::vector<Metric> metrics_;
    std::map<std::string, std::string> metadata_;
};

}  // namespace DSR::Benchmark

#endif  // DSR_METRICS_COLLECTOR_H
