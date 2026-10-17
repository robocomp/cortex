#ifndef DSR_PROC_METRICS_H
#define DSR_PROC_METRICS_H

// Process metrics for AgentInfoAPI, read straight from /proc/self (2026-10-17).
//
// Replaces `top -p <pid> -b -n1 | awk` + std::stoi / std::stof, which had three faults:
//   * LOCALE: the agents are Qt programs (setlocale(LC_ALL, "") under es_ES); std::stof reads through LC_NUMERIC,
//     so "12.5" parsed as 12 — the ','→'.' replace made it worse, not better. Every cpu_usage was truncated.
//   * UNITS: top prints RES with a suffix once it is large ("1,2g"), and std::stoi read that as 1 (kB).
//   * MEANING: the first frame of `top -n1` is the average since the process started, not current use.
// Here: integers from /proc parsed with std::from_chars (locale-independent), CPU % from the tick delta between two
// reads, no subprocess. Header-only and pure apart from read_self(), so it can be checked without a graph.

#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace DSR::proc
{
    /// utime + stime (clock ticks) from the text of /proc/<pid>/stat. The command name is in parentheses and may
    /// itself contain spaces and ')', so fields are counted after the LAST ')': field 3 (state) is the first there,
    /// utime is field 14 and stime field 15.
    [[nodiscard]] inline std::optional<std::uint64_t> cpu_ticks_from_stat(std::string_view stat)
    {
        const auto close = stat.rfind(')');
        if (close == std::string_view::npos) return std::nullopt;
        std::string_view rest = stat.substr(close + 1);
        std::uint64_t utime = 0, stime = 0;
        int field = 2;   // the ')' ends field 2
        while (not rest.empty())
        {
            const auto b = rest.find_first_not_of(' ');
            if (b == std::string_view::npos) break;
            rest.remove_prefix(b);
            const auto e = std::min(rest.find(' '), rest.size());
            const std::string_view tok = rest.substr(0, e);
            ++field;
            if (field == 14 or field == 15)
            {
                std::uint64_t v = 0;
                const auto r = std::from_chars(tok.data(), tok.data() + tok.size(), v);
                if (r.ec != std::errc{} or r.ptr != tok.data() + tok.size()) return std::nullopt;
                (field == 14 ? utime : stime) = v;
                if (field == 15) return utime + stime;
            }
            rest.remove_prefix(e);
        }
        return std::nullopt;
    }

    /// VmRSS (kB) from the text of /proc/<pid>/status.
    [[nodiscard]] inline std::optional<std::int64_t> rss_kb_from_status(std::string_view status)
    {
        const auto at = status.find("VmRSS:");
        if (at == std::string_view::npos) return std::nullopt;
        std::string_view v = status.substr(at + 6);
        const auto b = v.find_first_not_of(" \t");
        if (b == std::string_view::npos) return std::nullopt;
        v.remove_prefix(b);
        std::int64_t kb = 0;
        if (std::from_chars(v.data(), v.data() + v.size(), kb).ec != std::errc{}) return std::nullopt;
        return kb;
    }

    /// Percent of ONE core used over an interval (2 busy cores = 200 %). Negative = unknown (no interval).
    [[nodiscard]] inline float cpu_percent(std::uint64_t delta_ticks, double interval_s, long ticks_per_s)
    {
        if (interval_s <= 0.0 or ticks_per_s <= 0) return -1.0f;
        return static_cast<float>(100.0 * (static_cast<double>(delta_ticks) / static_cast<double>(ticks_per_s)) / interval_s);
    }

    struct Sample
    {
        std::uint64_t cpu_ticks = 0;
        std::int64_t rss_kb = 0;
        std::chrono::steady_clock::time_point when{};
    };

    /// This process's CPU ticks and resident memory now; nullopt if /proc cannot be read or parsed.
    [[nodiscard]] inline std::optional<Sample> read_self()
    {
        const auto slurp = [](const char* path) -> std::optional<std::string>
        {
            std::ifstream f(path);
            if (not f) return std::nullopt;
            std::ostringstream ss;
            ss << f.rdbuf();
            return ss.str();
        };
        const auto stat = slurp("/proc/self/stat");
        const auto status = slurp("/proc/self/status");
        if (not stat or not status) return std::nullopt;
        const auto ticks = cpu_ticks_from_stat(*stat);
        const auto rss = rss_kb_from_status(*status);
        if (not ticks or not rss) return std::nullopt;
        return Sample{.cpu_ticks = *ticks, .rss_kb = *rss, .when = std::chrono::steady_clock::now()};
    }
}

#endif //DSR_PROC_METRICS_H
