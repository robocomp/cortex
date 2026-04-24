#ifndef DSR_MULTI_AGENT_FIXTURE_H
#define DSR_MULTI_AGENT_FIXTURE_H

#include <memory>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <functional>
#include <QCoreApplication>
#include <QTimer>
#include <QEventLoop>
#include <dsr/api/dsr_api.h>
#include <dsr/core/profiling.h>
#include <dsr/core/types/type_checking/type_checker.h>
#include "../core/benchmark_config.h"
#include "../core/timing_utils.h"

namespace DSR::Benchmark {

// Agent info for tracking
struct AgentInfo {
    uint32_t id;
    std::string name;
    std::unique_ptr<DSRGraph> graph;
    std::atomic<int> participants_matched{0};
};


// Forward declaration for type registration
class GraphGenerator;

// Reusable multi-agent test fixture
class MultiAgentFixture {
public:
    explicit MultiAgentFixture(const BenchmarkConfig& config = default_config())
        : config_(config)
    {
        // Ensure test types are registered before any DSR operations
        register_benchmark_types();
    }

    // Register node/edge types needed by benchmarks
    static void register_benchmark_types() {
        static bool registered = false;
        if (!registered) {
            node_types::register_type("test_node");
            edge_types::register_type("test_edge");
            registered = true;
        }
    }

    ~MultiAgentFixture() {
        cleanup();
    }

    // Disable copy
    MultiAgentFixture(const MultiAgentFixture&) = delete;
    MultiAgentFixture& operator=(const MultiAgentFixture&) = delete;

    // Create N agent instances with DSRGraph
    // First agent loads from config_file, others sync via DDS
    bool create_agents(uint32_t num_agents, const std::string& config_file) {
        CORTEX_PROFILE_MIN_N("MultiAgentFixture::create_agents");
        if (num_agents == 0 || num_agents > config_.max_agent_count) {
            qWarning("Can't create agents");
            return false;
        }
        if (config_file.empty()) {
            qWarning("create_agents: config_file is empty — graph generator likely failed to write to /tmp (check permissions)");
            return false;
        }

        // Keep agent IDs deterministic while remaining disjoint across fixture
        // instances in the same process.
        static std::atomic<uint32_t> next_base_agent_id{1000};
        base_agent_id_ = next_base_agent_id.fetch_add(config_.max_agent_count + 1,
                                                      std::memory_order_relaxed);

        agents_.clear();
        agents_.reserve(num_agents);

        // Create first agent with config file (it defines the initial graph)
        {
            auto agent = std::make_unique<AgentInfo>();
            agent->id = base_agent_id_;
            agent->name = "bench_agent_0";

            try {
                agent->graph = std::make_unique<DSRGraph>(
                    GraphSettings{
                        .agent_id = agent->id,
                        .graph_name = agent->name,
                        .input_file = config_file,
                        .same_host = true,
                        .sync_mode = config_.sync_mode
                    }
                );
                agents_.push_back(std::move(agent));
            } catch (const std::exception& e) {
                qWarning("Failed to create primary agent: %s", e.what());
                return false;
            }
        }

        // Small delay for DDS to initialize primary agent
        process_events(50);

        // Create additional agents WITHOUT config file - they sync via DDS
        for (uint32_t i = 1; i < num_agents; ++i) {
            auto agent = std::make_unique<AgentInfo>();
            agent->id = base_agent_id_ + i;
            agent->name = "bench_agent_" + std::to_string(i);

            try {
                // No config file - agent receives graph from DDS
                agent->graph = std::make_unique<DSRGraph>(
                    GraphSettings{
                        .agent_id = agent->id,
                        .graph_name = agent->name,
                        .input_file = std::string{},
                        .same_host = true,
                        .sync_mode = config_.sync_mode
                    }
                );
                agents_.push_back(std::move(agent));
            } catch (const std::exception& e) {
                qWarning("Failed to create agent %u: %s", i, e.what());
                return false;
            }

            // Process events after each agent creation
            process_events(20);
        }

        return true;
    }

    // Wait for DDS synchronization between agents
    // Actively processes events while waiting
    void wait_for_sync(std::chrono::milliseconds wait_time = std::chrono::milliseconds{0}) {
        if (wait_time.count() == 0) {
            wait_time = config_.sync_wait_time;
        }

        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < wait_time) {
            process_events(10);
        }
    }

    // Verify all agents have converged to same state
    bool verify_convergence(std::chrono::seconds timeout = std::chrono::seconds{0}) {
        if (timeout.count() == 0) {
            timeout = config_.max_convergence_timeout;
        }

        if (agents_.size() < 2) {
            return true;  // Single agent is always converged
        }

        auto start = std::chrono::steady_clock::now();

        while (std::chrono::steady_clock::now() - start < timeout) {
            if (check_node_convergence()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            process_events();
        }

        return false;
    }

    // Measure time to convergence
    std::chrono::milliseconds measure_convergence_time() {
        auto start = std::chrono::steady_clock::now();

        while (!check_node_convergence()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            process_events();

            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > config_.max_convergence_timeout) {
                return std::chrono::milliseconds{-1};  // Timeout
            }
        }

        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
    }

    // Get agent by index
    DSRGraph* get_agent(size_t index) {
        if (index < agents_.size()) {
            return agents_[index]->graph.get();
        }
        return nullptr;
    }

    // Get agent info by index
    AgentInfo* get_agent_info(size_t index) {
        if (index < agents_.size()) {
            return agents_[index].get();
        }
        return nullptr;
    }

    // Get number of agents
    [[nodiscard]] size_t agent_count() const {
        return agents_.size();
    }

    // Connect signal handler to all agents
    template<typename Signal, typename Slot>
    void connect_all(Signal signal, Slot slot) {
        for (auto& agent : agents_) {
            QObject::connect(agent->graph.get(), signal, slot, Qt::QueuedConnection);
        }
    }

    // Process Qt events (for signal delivery)
    void process_events(int timeout_ms = 10) {
        auto* app = QCoreApplication::instance();
        if (app) {
            app->processEvents(QEventLoop::AllEvents, timeout_ms);
        }
    }

    // Run event loop for specified duration
    void run_event_loop(std::chrono::milliseconds duration) {
        auto* app = QCoreApplication::instance();
        if (!app) return;

        QEventLoop loop;
        QTimer::singleShot(duration.count(), &loop, &QEventLoop::quit);
        loop.exec();
    }

    // Cleanup all agents
    void cleanup() {
        CORTEX_PROFILE_MIN_N("MultiAgentFixture::cleanup");
        agents_.clear();
    }

    // Get number of agents
    [[nodiscard]] size_t size() const {
        return agents_.size();
    }

private:
    bool check_node_convergence() {
        if (agents_.size() < 2) return true;

        auto& first_graph = agents_[0]->graph;
        auto first_nodes = first_graph->get_nodes();

        for (size_t i = 1; i < agents_.size(); ++i) {
            auto nodes = agents_[i]->graph->get_nodes();
            if (nodes.size() != first_nodes.size()) {
                return false;
            }

            // Check each node exists in the other graph
            for (const auto& node : first_nodes) {
                auto other_node = agents_[i]->graph->get_node(node.id());
                if (!other_node.has_value()) {
                    return false;
                }
            }
        }

        return true;
    }

    BenchmarkConfig config_;
    uint32_t base_agent_id_ = 0;
    std::vector<std::unique_ptr<AgentInfo>> agents_;
};

}  // namespace DSR::Benchmark

#endif  // DSR_MULTI_AGENT_FIXTURE_H
