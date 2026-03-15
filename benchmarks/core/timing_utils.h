#ifndef DSR_TIMING_UTILS_H
#define DSR_TIMING_UTILS_H

#include <chrono>
#include <functional>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <stdexcept>
#include <dsr/core/utils.h>

namespace DSR::Benchmark {

// Monotonic nanosecond counter for benchmark measurements.
// Uses steady_clock (CLOCK_MONOTONIC on Linux) instead of system_clock so
// that NTP adjustments and settimeofday() cannot produce negative intervals
// or artificially inflate latency samples.
inline uint64_t bench_now() noexcept {
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

// RAII timer that calls a callback with elapsed nanoseconds on destruction
class ScopedTimer {
public:
    using Callback = std::function<void(uint64_t)>;

    explicit ScopedTimer(Callback on_complete)
        : callback_(std::move(on_complete))
        , start_time_(bench_now())
    {}

    ~ScopedTimer() {
        if (callback_) {
            uint64_t elapsed = bench_now() - start_time_;
            callback_(elapsed);
        }
    }

    // Disable copy
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    // Allow move
    ScopedTimer(ScopedTimer&& other) noexcept
        : callback_(std::move(other.callback_))
        , start_time_(other.start_time_)
    {
        other.callback_ = nullptr;
    }

    ScopedTimer& operator=(ScopedTimer&& other) noexcept {
        if (this != &other) {
            callback_ = std::move(other.callback_);
            start_time_ = other.start_time_;
            other.callback_ = nullptr;
        }
        return *this;
    }

    // Get elapsed time without stopping
    [[nodiscard]] uint64_t elapsed_ns() const {
        return bench_now() - start_time_;
    }

    // Cancel the callback
    void cancel() {
        callback_ = nullptr;
    }

private:
    Callback callback_;
    uint64_t start_time_;
};


// Statistics from latency measurements
struct LatencyStats {
    uint64_t count = 0;
    double mean_ns = 0.0;
    double stddev_ns = 0.0;
    uint64_t min_ns = 0;
    uint64_t max_ns = 0;
    uint64_t p50_ns = 0;
    uint64_t p90_ns = 0;
    uint64_t p95_ns = 0;
    uint64_t p99_ns = 0;

    // Convenience methods for different units
    [[nodiscard]] double mean_us() const { return mean_ns / 1000.0; }
    [[nodiscard]] double mean_ms() const { return mean_ns / 1'000'000.0; }
    [[nodiscard]] double stddev_us() const { return stddev_ns / 1000.0; }
    [[nodiscard]] double stddev_ms() const { return stddev_ns / 1'000'000.0; }
    [[nodiscard]] double min_us() const { return min_ns / 1000.0; }
    [[nodiscard]] double max_us() const { return max_ns / 1000.0; }
    [[nodiscard]] double p50_us() const { return p50_ns / 1000.0; }
    [[nodiscard]] double p90_us() const { return p90_ns / 1000.0; }
    [[nodiscard]] double p95_us() const { return p95_ns / 1000.0; }
    [[nodiscard]] double p99_us() const { return p99_ns / 1000.0; }
    [[nodiscard]] double min_ms() const { return min_ns / 1'000'000.0; }
    [[nodiscard]] double max_ms() const { return max_ns / 1'000'000.0; }
    [[nodiscard]] double p50_ms() const { return p50_ns / 1'000'000.0; }
    [[nodiscard]] double p90_ms() const { return p90_ns / 1'000'000.0; }
    [[nodiscard]] double p95_ms() const { return p95_ns / 1'000'000.0; }
    [[nodiscard]] double p99_ms() const { return p99_ns / 1'000'000.0; }
};


// Collects latency samples and computes statistics
class LatencyTracker {
public:
    LatencyTracker() = default;

    // Reserve space for expected samples
    explicit LatencyTracker(size_t expected_samples) {
        samples_.reserve(expected_samples);
    }

    // Record a latency sample in nanoseconds
    void record(uint64_t latency_ns) {
        samples_.push_back(latency_ns);
        stats_valid_ = false;
    }

    // Record using ScopedTimer callback pattern
    [[nodiscard]] auto recorder() {
        return [this](uint64_t latency_ns) {
            this->record(latency_ns);
        };
    }

    // Create a ScopedTimer that records to this tracker
    [[nodiscard]] ScopedTimer scoped_record() {
        return ScopedTimer(recorder());
    }

    // Get number of recorded samples
    [[nodiscard]] size_t count() const {
        return samples_.size();
    }

    // Check if tracker has samples
    [[nodiscard]] bool empty() const {
        return samples_.empty();
    }

    // Clear all samples
    void clear() {
        samples_.clear();
        stats_valid_ = false;
    }

    // Get raw samples (for export)
    [[nodiscard]] const std::vector<uint64_t>& samples() const {
        return samples_;
    }

    // Compute and return statistics
    [[nodiscard]] LatencyStats stats() {
        if (stats_valid_) {
            return cached_stats_;
        }

        if (samples_.empty()) {
            return LatencyStats{};
        }

        // Sort samples for percentile calculation
        std::vector<uint64_t> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());

        LatencyStats result;
        result.count = sorted.size();
        result.min_ns = sorted.front();
        result.max_ns = sorted.back();

        // Calculate mean
        double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
        result.mean_ns = sum / static_cast<double>(result.count);

        // Calculate standard deviation
        double sq_sum = std::accumulate(sorted.begin(), sorted.end(), 0.0,
            [mean = result.mean_ns](double acc, uint64_t val) {
                double diff = static_cast<double>(val) - mean;
                return acc + diff * diff;
            });
        result.stddev_ns = std::sqrt(sq_sum / static_cast<double>(result.count));

        // Calculate percentiles
        result.p50_ns = percentile(sorted, 0.50);
        result.p90_ns = percentile(sorted, 0.90);
        result.p95_ns = percentile(sorted, 0.95);
        result.p99_ns = percentile(sorted, 0.99);

        cached_stats_ = result;
        stats_valid_ = true;
        return result;
    }

private:
    static uint64_t percentile(const std::vector<uint64_t>& sorted, double p) {
        if (sorted.empty()) return 0;
        if (sorted.size() == 1) return sorted[0];

        double index = p * static_cast<double>(sorted.size() - 1);
        size_t lower = static_cast<size_t>(std::floor(index));
        size_t upper = static_cast<size_t>(std::ceil(index));

        if (lower == upper) {
            return sorted[lower];
        }

        double fraction = index - static_cast<double>(lower);
        return static_cast<uint64_t>(
            static_cast<double>(sorted[lower]) * (1.0 - fraction) +
            static_cast<double>(sorted[upper]) * fraction
        );
    }

    std::vector<uint64_t> samples_;
    LatencyStats cached_stats_;
    bool stats_valid_ = false;
};


// Utility function to measure a single operation
template<typename Func>
uint64_t measure_ns(Func&& func) {
    uint64_t start = bench_now();
    std::forward<Func>(func)();
    return bench_now() - start;
}

// Utility function to run warmup iterations
template<typename Func>
void warmup(Func&& func, uint32_t iterations) {
    for (uint32_t i = 0; i < iterations; ++i) {
        std::forward<Func>(func)();
    }
}

}  // namespace DSR::Benchmark

#endif  // DSR_TIMING_UTILS_H
