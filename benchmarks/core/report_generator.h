#ifndef DSR_REPORT_GENERATOR_H
#define DSR_REPORT_GENERATOR_H

#include <string>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include "metrics_collector.h"

namespace DSR::Benchmark {

class ReportGenerator {
public:
    explicit ReportGenerator(std::string output_directory = "results")
        : output_directory_(std::move(output_directory))
    {}

    // Export benchmark result to JSON
    bool export_json(const BenchmarkResult& result, const std::string& filename = "") {
        std::string filepath = generate_filepath(result, filename, ".json");
        std::ofstream out(filepath);
        if (!out.is_open()) {
            return false;
        }

        out << "{\n";
        out << "  \"benchmark_name\": " << quote(result.benchmark_name) << ",\n";
        out << "  \"timestamp\": " << quote(result.timestamp) << ",\n";
        out << "  \"total_duration_ms\": " << result.total_duration.count() << ",\n";

        // Metadata
        out << "  \"metadata\": {\n";
        bool first = true;
        for (const auto& [key, value] : result.metadata) {
            if (!first) out << ",\n";
            out << "    " << quote(key) << ": " << quote(value);
            first = false;
        }
        out << "\n  },\n";

        // Metrics
        out << "  \"metrics\": [\n";
        for (size_t i = 0; i < result.metrics.size(); ++i) {
            const auto& m = result.metrics[i];
            out << "    {\n";
            out << "      \"name\": " << quote(m.name) << ",\n";
            out << "      \"category\": " << quote(to_string(m.category)) << ",\n";
            out << "      \"value\": " << format_double(m.value) << ",\n";
            out << "      \"unit\": " << quote(m.unit);

            if (!m.additional_values.empty()) {
                out << ",\n      \"additional\": {\n";
                bool first_add = true;
                for (const auto& [key, value] : m.additional_values) {
                    if (!first_add) out << ",\n";
                    out << "        " << quote(key) << ": " << format_double(value);
                    first_add = false;
                }
                out << "\n      }";
            }

            if (!m.tags.empty()) {
                out << ",\n      \"tags\": {\n";
                bool first_tag = true;
                for (const auto& [key, value] : m.tags) {
                    if (!first_tag) out << ",\n";
                    out << "        " << quote(key) << ": " << quote(value);
                    first_tag = false;
                }
                out << "\n      }";
            }

            out << "\n    }";
            if (i < result.metrics.size() - 1) out << ",";
            out << "\n";
        }
        out << "  ]\n";
        out << "}\n";

        out.close();
        last_json_path_ = filepath;
        return true;
    }

    // Export benchmark result to CSV
    bool export_csv(const BenchmarkResult& result, const std::string& filename = "") {
        std::string filepath = generate_filepath(result, filename, ".csv");
        std::ofstream out(filepath);
        if (!out.is_open()) {
            return false;
        }

        // Header
        out << "benchmark_name,timestamp,metric_name,category,value,unit,"
            << "mean_ns,stddev_ns,min_ns,max_ns,p50_ns,p90_ns,p95_ns,p99_ns,count\n";

        // Data rows
        for (const auto& m : result.metrics) {
            out << quote_csv(result.benchmark_name) << ","
                << quote_csv(result.timestamp) << ","
                << quote_csv(m.name) << ","
                << quote_csv(to_string(m.category)) << ","
                << format_double(m.value) << ","
                << quote_csv(m.unit) << ",";

            // Additional values (latency-specific)
            auto get_add = [&m](const std::string& key) -> std::string {
                auto it = m.additional_values.find(key);
                if (it != m.additional_values.end()) {
                    return format_double(it->second);
                }
                return "";
            };

            out << get_add("mean_ns") << ","
                << get_add("stddev_ns") << ","
                << get_add("min_ns") << ","
                << get_add("max_ns") << ","
                << get_add("p50_ns") << ","
                << get_add("p90_ns") << ","
                << get_add("p95_ns") << ","
                << get_add("p99_ns") << ","
                << get_add("count") << "\n";
        }

        out.close();
        last_csv_path_ = filepath;
        return true;
    }

    // Export both JSON and CSV
    bool export_all(const BenchmarkResult& result, const std::string& base_filename = "") {
        bool json_ok = export_json(result, base_filename);
        bool csv_ok = export_csv(result, base_filename);
        return json_ok && csv_ok;
    }

    // Compare with baseline and generate comparison report
    bool compare_with_baseline(const BenchmarkResult& current,
                               const std::string& baseline_json_path,
                               double regression_threshold_percent = 10.0) {
        // Read baseline JSON (simplified parsing)
        std::ifstream baseline_file(baseline_json_path);
        if (!baseline_file.is_open()) {
            return false;
        }

        // For now, just note that comparison is requested
        // Full JSON parsing would require nlohmann/json
        comparison_requested_ = true;
        baseline_path_ = baseline_json_path;
        regression_threshold_ = regression_threshold_percent;

        return true;
    }

    // Get last generated file paths
    [[nodiscard]] const std::string& last_json_path() const { return last_json_path_; }
    [[nodiscard]] const std::string& last_csv_path() const { return last_csv_path_; }

    // Set output directory
    void set_output_directory(const std::string& dir) {
        output_directory_ = dir;
    }

private:
    std::string generate_filepath(const BenchmarkResult& result,
                                  const std::string& filename,
                                  const std::string& extension) {
        // Ensure directory exists
        std::filesystem::create_directories(output_directory_);

        std::string name = filename;
        if (name.empty()) {
            // Generate filename from benchmark name and timestamp
            name = "benchmark_" + sanitize_filename(result.benchmark_name) +
                   "_" + sanitize_filename(result.timestamp);
        }

        // Remove extension if present
        if (name.size() > extension.size() &&
            name.substr(name.size() - extension.size()) == extension) {
            name = name.substr(0, name.size() - extension.size());
        }

        return output_directory_ + "/" + name + extension;
    }

    static std::string sanitize_filename(const std::string& name) {
        std::string result;
        for (char c : name) {
            if (std::isalnum(c) || c == '_' || c == '-') {
                result += c;
            } else if (c == ' ' || c == ':' || c == '/') {
                result += '_';
            }
        }
        return result;
    }

    static std::string quote(const std::string& s) {
        std::string result = "\"";
        for (char c : s) {
            if (c == '"') result += "\\\"";
            else if (c == '\\') result += "\\\\";
            else if (c == '\n') result += "\\n";
            else result += c;
        }
        result += "\"";
        return result;
    }

    static std::string quote_csv(const std::string& s) {
        if (s.find(',') != std::string::npos ||
            s.find('"') != std::string::npos ||
            s.find('\n') != std::string::npos) {
            std::string escaped;
            for (char c : s) {
                if (c == '"') escaped += "\"\"";
                else escaped += c;
            }
            return "\"" + escaped + "\"";
        }
        return s;
    }

    static std::string format_double(double value) {
        std::ostringstream oss;
        oss << std::setprecision(6) << std::fixed << value;
        std::string str = oss.str();
        // Remove trailing zeros
        size_t dot_pos = str.find('.');
        if (dot_pos != std::string::npos) {
            size_t last_non_zero = str.find_last_not_of('0');
            if (last_non_zero > dot_pos) {
                str = str.substr(0, last_non_zero + 1);
            } else {
                str = str.substr(0, dot_pos);
            }
        }
        return str;
    }

    std::string output_directory_;
    std::string last_json_path_;
    std::string last_csv_path_;
    bool comparison_requested_ = false;
    std::string baseline_path_;
    double regression_threshold_ = 10.0;
};

}  // namespace DSR::Benchmark

#endif  // DSR_REPORT_GENERATOR_H
