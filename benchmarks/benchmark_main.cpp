// DSR Benchmarking Suite
// Main entry point using Catch2

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>
#include <QCoreApplication>
#include <QtGlobal>
#include <iostream>
#include <stdexcept>

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
