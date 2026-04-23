// DSR Benchmarking Suite
// Main entry point using Catch2

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>
#include <QCoreApplication>
#include <QtGlobal>
#include "core/benchmark_config.h"
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <type_traits>

// Custom Qt message handler to filter debug output during benchmarks
static bool g_verbose = false;

namespace {

bool hasCliFlag(int argc, char* argv[], const char* flag) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == flag) {
            return true;
        }
    }
    return false;
}

bool shouldPrintBenchmarkPreamble(int argc, char* argv[]) {
    return !hasCliFlag(argc, argv, "--help")
        && !hasCliFlag(argc, argv, "-?")
        && !hasCliFlag(argc, argv, "--list-tests")
        && !hasCliFlag(argc, argv, "--list-tags")
        && !hasCliFlag(argc, argv, "--list-reporters")
        && !hasCliFlag(argc, argv, "--list-listeners");
}

std::optional<std::string> getenv_string(const char* name) {
    if (const char* value = std::getenv(name); value != nullptr && *value != '\0') {
        return std::string(value);
    }
    return std::nullopt;
}

template <typename T>
void load_env_value(const char* name, T& target) {
    if (const auto value = getenv_string(name); value.has_value()) {
        if constexpr (std::is_same_v<T, uint32_t>) {
            target = static_cast<uint32_t>(std::stoul(*value));
        } else if constexpr (std::is_same_v<T, std::chrono::milliseconds>) {
            target = std::chrono::milliseconds(std::stoull(*value));
        } else if constexpr (std::is_same_v<T, std::chrono::seconds>) {
            target = std::chrono::seconds(std::stoull(*value));
        } else {
            target = *value;
        }
    }
}

void configureBenchmarkDefaultsFromEnv() {
    using namespace DSR::Benchmark;

    auto& config = default_config();

    if (const auto sync_mode = getenv_string("BENCH_SYNC_MODE"); sync_mode.has_value()) {
        config.sync_mode = parse_sync_mode(*sync_mode);
    }

    load_env_value("BENCH_WARMUP_ITERATIONS", config.warmup_iterations);
    load_env_value("BENCH_MEASUREMENT_ITERATIONS", config.measurement_iterations);
    load_env_value("BENCH_SYNC_WAIT_MS", config.sync_wait_time);
    load_env_value("BENCH_MAX_CONVERGENCE_TIMEOUT_S", config.max_convergence_timeout);
    load_env_value("BENCH_DEFAULT_AGENT_COUNT", config.default_agent_count);
    load_env_value("BENCH_MAX_AGENT_COUNT", config.max_agent_count);
    load_env_value("BENCH_CONCURRENT_WRITER_THREADS", config.concurrent_writer_threads);
    load_env_value("BENCH_RESULTS_DIRECTORY", config.results_directory);
}

}  // namespace

void benchmarkMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    // In non-verbose mode, only show warnings and above
    if (!g_verbose) {
        switch (type) {
            case QtDebugMsg:
            case QtInfoMsg:
                return;  // Suppress debug and info messages
            default:
                break;
        }
    }

    // Format and output remaining messages
    QByteArray localMsg = msg.toLocal8Bit();
    switch (type) {
        case QtDebugMsg:
            std::cout << "[DEBUG] " << localMsg.constData() << std::endl;
            break;
        case QtInfoMsg:
            std::cout << "[INFO] " << localMsg.constData() << std::endl;
            break;
        case QtWarningMsg:
            std::cout << "[WARNING] " << localMsg.constData() << std::endl;
            break;
        case QtCriticalMsg:
            std::cout << "[CRITICAL] " << localMsg.constData() << std::endl;
            break;
        case QtFatalMsg:
            std::cout << "[FATAL] " << localMsg.constData() << std::endl;
            // Throw instead of abort() so the fixture's try/catch can catch it,
            // mark the test as failed, and let Catch2 continue to the next test.
            throw std::runtime_error(localMsg.constData());
    }
}

int main(int argc, char* argv[]) {
    // Install custom message handler before QCoreApplication
    qInstallMessageHandler(benchmarkMessageHandler);

    // Check for verbose flag
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--verbose" || std::string(argv[i]) == "-v") {
            g_verbose = true;
            break;
        }
    }

    // Initialize Qt (required for signals/slots)
    QCoreApplication app(argc, argv);
    configureBenchmarkDefaultsFromEnv();
    // Initialize Catch2
    Catch::Session session;

    // Set default reporter to console with colors
    session.configData().showDurations = Catch::ShowDurations::Always;

    // Apply command line arguments
    int returnCode = session.applyCommandLine(argc, argv);
    if (returnCode != 0) {
        return returnCode;
    }

    if (shouldPrintBenchmarkPreamble(argc, argv)) {
        std::cout << "=================================\n";
        std::cout << " DSR Benchmarking Suite\n";
        std::cout << "=================================\n\n";
        std::cout << "Benchmark config:\n";
        std::cout << "  sync_mode      = " << DSR::Benchmark::sync_mode_name(DSR::Benchmark::default_config().sync_mode) << "\n";
        std::cout << "  warmup         = " << DSR::Benchmark::default_config().warmup_iterations << "\n";
        std::cout << "  measurements   = " << DSR::Benchmark::default_config().measurement_iterations << "\n";
        std::cout << "  sync_wait_ms   = " << DSR::Benchmark::default_config().sync_wait_time.count() << "\n";
        std::cout << "  max_conv_s     = " << DSR::Benchmark::default_config().max_convergence_timeout.count() << "\n";
        std::cout << "  agent_count    = " << DSR::Benchmark::default_config().default_agent_count << "\n";
        std::cout << "  writer_threads = " << DSR::Benchmark::default_config().concurrent_writer_threads << "\n\n";
        std::cout << "Available benchmark categories:\n";
        std::cout << "  [BASELINE]     - Curated low-noise regression baseline\n";
        std::cout << "  [EXTENDED]     - Slower supplementary baseline coverage\n";
        std::cout << "  [LATENCY]      - Signal emission, CRDT operations\n";
        std::cout << "  [THROUGHPUT]   - Single agent insert/read/update/delete, concurrent writers\n";
        std::cout << "  [CRDT]         - mvreg and dot_context micro-benchmarks\n";
        std::cout << "  [SCALABILITY]  - Thread scaling, graph size impact\n";
        std::cout << "  [CONSISTENCY]  - Convergence time, conflict rates\n";
        std::cout << "  [PROFILE]      - Expensive profiling-focused cases\n";
        std::cout << "  [LOAD]         - Work-under-load and concurrency-heavy cases\n";
        std::cout << "  [MULTIAGENT]   - Multi-agent synchronization/consistency cases\n";
        std::cout << "\n";
        std::cout << "Usage examples:\n";
        std::cout << "  ./dsr_benchmarks                    # Run all non-hidden benchmarks\n";
        std::cout << "  ./dsr_benchmarks \"[BASELINE]\"       # Run curated baseline benchmarks\n";
        std::cout << "  ./dsr_benchmarks \"[EXTENDED]\"       # Run slower supplementary coverage\n";
        std::cout << "  ./dsr_benchmarks \"[LATENCY]\"        # Run latency benchmarks\n";
        std::cout << "  ./dsr_benchmarks \"[THROUGHPUT]\"     # Run throughput benchmarks\n";
        std::cout << "  ./dsr_benchmarks \"[CRDT]\"           # Run CRDT micro-benchmarks\n";
        std::cout << "  ./dsr_benchmarks \"[PROFILE][LOAD]\"  # Run long load-heavy cases\n";
        std::cout << "  ./dsr_benchmarks \"[PROFILE][MULTIAGENT]\" # Run multi-agent profiling cases\n";
        std::cout << "  ./dsr_benchmarks \"[.multi]\"         # Run multi-agent tests (may timeout)\n";
        std::cout << "  ./dsr_benchmarks -r json::out=x.json # Export to JSON\n";
        std::cout << "  ./dsr_benchmarks --verbose          # Show Qt debug messages\n";
        std::cout << "\n";
        std::cout << "Note: [.multi] and [.extended] tests are hidden by default.\n";
        std::cout << "\n";
    }

    return session.run();
}
