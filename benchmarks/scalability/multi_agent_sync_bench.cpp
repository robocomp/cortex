#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <atomic>
#include <vector>

#include "../core/timing_utils.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/multi_agent_fixture.h"
#include "../fixtures/graph_generator.h"

using namespace DSR;
using namespace DSR::Benchmark;

TEST_CASE("Multi-agent synchronization benchmarks", "[SCALABILITY][sync][.multi][PROFILE][MULTIAGENT]") {
    GraphGenerator generator;
    MetricsCollector collector("multi_agent_sync");

    SECTION("Initial sync time vs agent count") {
        for (uint32_t num_agents : {2, 4, 8, 16}) {
            auto config_file = generator.generate_empty_graph();

            LatencyTracker tracker(10);

            for (int trial = 0; trial < 10; ++trial) {
                MultiAgentFixture fixture;

                uint64_t start = get_unix_timestamp();
                bool created = fixture.create_agents(num_agents, config_file);
                if (!created) {
                    WARN("Could not create " << num_agents << " agents");
                    break;
                }

                fixture.wait_for_sync();
                bool converged = fixture.verify_convergence();
                uint64_t elapsed = get_unix_timestamp() - start;

                if (converged) {
                    tracker.record(elapsed);
                }

                // Cleanup before next trial
            }

            if (tracker.count() > 0) {
                auto stats = tracker.stats();
                collector.record_scalability(
                    "initial_sync_time",
                    num_agents,
                    stats.mean_ms(),
                    "ms",
                    {{"num_agents", std::to_string(num_agents)}});

                INFO(num_agents << " agents - Initial sync: " << stats.mean_ms() << " ms");
            }
        }
    }

    SECTION("Convergence time after operation") {
        for (uint32_t num_agents : {2, 4, 8}) {
            MultiAgentFixture fixture;
            auto config_file = generator.generate_empty_graph();

            if (!fixture.create_agents(num_agents, config_file)) {
                WARN("Could not create " << num_agents << " agents");
                continue;
            }
            fixture.wait_for_sync();

            LatencyTracker tracker(50);

            // Measure convergence time after node insertion
            for (int i = 0; i < 50; ++i) {
                auto* sender = fixture.get_agent(0);
                auto node = GraphGenerator::create_test_node(
                    700000 + i, sender->get_agent_id(),
                    "sync_node_" + std::to_string(i));

                uint64_t start = get_unix_timestamp();
                sender->insert_node(node);

                auto conv_time = fixture.measure_convergence_time();
                if (conv_time.count() >= 0) {
                    tracker.record(static_cast<uint64_t>(conv_time.count()) * 1'000'000);  // ms to ns
                }
            }

            if (tracker.count() > 0) {
                auto stats = tracker.stats();
                collector.record_scalability(
                    "convergence_after_insert",
                    num_agents,
                    stats.mean_ms(),
                    "ms",
                    {{"num_agents", std::to_string(num_agents)}});

                INFO(num_agents << " agents - Convergence time: " << stats.mean_ms() << " ms");
            }
        }
    }

    SECTION("Broadcast time to all agents") {
        for (uint32_t num_agents : {2, 4, 8}) {
            MultiAgentFixture fixture;
            auto config_file = generator.generate_empty_graph();

            if (!fixture.create_agents(num_agents, config_file)) {
                WARN("Could not create " << num_agents << " agents");
                continue;
            }
            fixture.wait_for_sync();

            LatencyTracker tracker(50);

            // Track when each agent receives the update
            std::vector<std::atomic<uint64_t>> receive_times(num_agents - 1);
            std::vector<std::atomic<bool>> received(num_agents - 1);

            for (size_t i = 1; i < num_agents; ++i) {
                auto* receiver = fixture.get_agent(i);
                QObject::connect(receiver, &DSR::DSRGraph::update_node_signal, receiver,
                    [&, idx = i - 1](uint64_t id, const std::string& type, DSR::SignalInfo) {
                        if (id >= 800000 && id < 900000 && !received[idx].load()) {
                            receive_times[idx].store(get_unix_timestamp());
                            received[idx].store(true);
                        }
                    }, Qt::DirectConnection);
            }

            auto* sender = fixture.get_agent(0);

            for (int i = 0; i < 50; ++i) {
                // Reset tracking
                for (size_t j = 0; j < num_agents - 1; ++j) {
                    receive_times[j].store(0);
                    received[j].store(false);
                }

                auto node = GraphGenerator::create_test_node(
                    800000 + i, sender->get_agent_id(),
                    "broadcast_node_" + std::to_string(i));

                uint64_t send_time = get_unix_timestamp();
                sender->insert_node(node);

                // Wait for all receivers
                auto start = std::chrono::steady_clock::now();
                while (true) {
                    bool all_received = true;
                    for (size_t j = 0; j < num_agents - 1; ++j) {
                        if (!received[j].load()) {
                            all_received = false;
                            break;
                        }
                    }

                    if (all_received) break;

                    fixture.process_events(1);

                    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
                        break;
                    }
                }

                // Find max receive time (last agent to receive)
                uint64_t max_time = 0;
                for (size_t j = 0; j < num_agents - 1; ++j) {
                    if (received[j].load()) {
                        max_time = std::max(max_time, receive_times[j].load());
                    }
                }

                if (max_time > send_time) {
                    tracker.record(max_time - send_time);
                }
            }

            if (tracker.count() > 0) {
                auto stats = tracker.stats();
                collector.record_scalability(
                    "broadcast_to_all",
                    num_agents,
                    stats.mean_us(),
                    "us",
                    {{"num_agents", std::to_string(num_agents)}});

                INFO(num_agents << " agents - Broadcast time: " << stats.mean_us() << " us");
            }
        }
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "multi_agent_sync");
}

TEST_CASE("Scaling efficiency", "[SCALABILITY][efficiency][.multi][PROFILE][MULTIAGENT]") {
    GraphGenerator generator;
    MetricsCollector collector("scaling_efficiency");

    std::map<uint32_t, double> throughputs;

    SECTION("Throughput scaling with agents") {
        for (uint32_t num_agents : {1, 2, 4, 8}) {
            MultiAgentFixture fixture;
            auto config_file = generator.generate_empty_graph();

            if (!fixture.create_agents(num_agents, config_file)) {
                WARN("Could not create " << num_agents << " agents");
                continue;
            }
            fixture.wait_for_sync();

            constexpr auto TEST_DURATION = std::chrono::seconds(3);
            std::atomic<uint64_t> total_ops{0};
            std::atomic<bool> stop_flag{false};

            std::vector<std::thread> threads;
            threads.reserve(num_agents);

            auto start = std::chrono::steady_clock::now();

            for (size_t i = 0; i < num_agents; ++i) {
                threads.emplace_back([&, agent_idx = i]() {
                    auto* graph = fixture.get_agent(agent_idx);
                    uint64_t base_id = 900000 + agent_idx * 50000;
                    uint64_t local_ops = 0;

                    while (!stop_flag.load(std::memory_order_relaxed)) {
                        auto node = GraphGenerator::create_test_node(
                            base_id + local_ops, graph->get_agent_id());
                        graph->insert_node(node);
                        local_ops++;
                    }

                    total_ops.fetch_add(local_ops, std::memory_order_relaxed);
                });
            }

            std::this_thread::sleep_for(TEST_DURATION);
            stop_flag.store(true);

            for (auto& t : threads) {
                t.join();
            }

            auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);

            double ops_per_sec = static_cast<double>(total_ops.load()) /
                                (static_cast<double>(actual_duration.count()) / 1000.0);

            throughputs[num_agents] = ops_per_sec;

            collector.record_scalability(
                "throughput_scaling",
                num_agents,
                ops_per_sec,
                "ops/sec",
                {{"num_agents", std::to_string(num_agents)}});

            INFO(num_agents << " agents - Throughput: " << ops_per_sec << " ops/sec");
        }

        // Calculate scaling efficiency
        if (throughputs.count(1) > 0 && throughputs.count(2) > 0) {
            double efficiency_2 = throughputs[2] / (2 * throughputs[1]) * 100;
            collector.record("scaling_efficiency_2_agents", MetricCategory::Scalability,
                efficiency_2, "%");
            INFO("Scaling efficiency (2 agents): " << efficiency_2 << "%");
        }

        if (throughputs.count(1) > 0 && throughputs.count(4) > 0) {
            double efficiency_4 = throughputs[4] / (4 * throughputs[1]) * 100;
            collector.record("scaling_efficiency_4_agents", MetricCategory::Scalability,
                efficiency_4, "%");
            INFO("Scaling efficiency (4 agents): " << efficiency_4 << "%");
        }
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "scaling_efficiency");
}
