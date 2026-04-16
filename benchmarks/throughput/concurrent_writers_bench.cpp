#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <atomic>
#include <barrier>
#include <vector>
#include <chrono>
#include <iostream>

#include <nanobench.h>
#include "../core/nanobench_adapter.h"
#include "../core/timing_utils.h"
#include "../core/metrics_collector.h"
#include "../core/report_generator.h"
#include "../fixtures/multi_agent_fixture.h"
#include "../fixtures/graph_generator.h"

using namespace DSR;
using namespace DSR::Benchmark;

TEST_CASE("Concurrent writers throughput", "[THROUGHPUT][concurrent][PROFILE][LOAD]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("concurrent_writers");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));

    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    constexpr auto TEST_DURATION = std::chrono::seconds(5);

    ankerl::nanobench::Bench bench;
    bench.output(&nb_report_stream()).warmup(0).epochs(1).epochIterations(1);

    auto run_concurrent_test = [&](uint32_t num_threads, const std::string& test_name) {
        std::atomic<uint64_t> total_operations{0};
        std::atomic<uint64_t> failed_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(num_threads);

        std::vector<std::thread> threads;
        threads.reserve(num_threads);

        auto start = std::chrono::steady_clock::now();

        bench.run(test_name, [&] {
            for (uint32_t t = 0; t < num_threads; ++t) {
                threads.emplace_back([&, thread_id = t]() {
                    uint64_t base_id = 100000 + thread_id * 100000;
                    uint64_t local_ops = 0;

                    sync_point.arrive_and_wait();

                    while (!stop_flag.load(std::memory_order_relaxed)) {
                        auto node = GraphGenerator::create_test_node(
                            base_id + local_ops, graph->get_agent_id(),
                            "thread_" + std::to_string(thread_id) + "_node_" + std::to_string(local_ops));
                        if (!graph->insert_node(node).has_value())
                            failed_ops.fetch_add(1, std::memory_order_relaxed);
                        local_ops++;
                    }

                    total_operations.fetch_add(local_ops, std::memory_order_relaxed);
                });
            }

            std::this_thread::sleep_for(TEST_DURATION);
            stop_flag.store(true, std::memory_order_relaxed);
            for (auto& t : threads) t.join();

            bench.batch(total_operations.load());
            ankerl::nanobench::doNotOptimizeAway(total_operations.load());
        });

        if (failed_ops.load() > 0)
            std::cerr << "[BENCH " << test_name << "] "
                      << failed_ops.load() << " insert_node calls failed\n";

        auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        collector.record_throughput(test_name, total_operations.load(), actual_duration,
            {{"num_threads", std::to_string(num_threads)}});

        double ops_per_sec = static_cast<double>(total_operations.load()) /
                            (static_cast<double>(actual_duration.count()) / 1000.0);

        return ops_per_sec;
    };

    SECTION("2 concurrent writers") {
        double ops = run_concurrent_test(2, "concurrent_insert_2t");
        INFO("2 threads: " << ops << " ops/sec");
        CHECK(ops >= MIN_EXPECTED_THROUGHPUT_OPS);
    }

    SECTION("4 concurrent writers") {
        double ops = run_concurrent_test(4, "concurrent_insert_4t");
        INFO("4 threads: " << ops << " ops/sec");
    }

    SECTION("8 concurrent writers") {
        double ops = run_concurrent_test(8, "concurrent_insert_8t");
        INFO("8 threads: " << ops << " ops/sec");
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "concurrent_writers");
}

TEST_CASE("Concurrent read-write throughput", "[THROUGHPUT][concurrent][PROFILE][LOAD]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("concurrent_read_write");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(1, config_file));

    auto* graph = fixture.get_agent(0);
    REQUIRE(graph != nullptr);

    // Pre-populate graph and store actual IDs
    std::vector<uint64_t> pre_node_ids;
    pre_node_ids.reserve(1000);
    for (uint64_t i = 0; i < 1000; ++i) {
        auto node = GraphGenerator::create_test_node(0, graph->get_agent_id());
        auto result = graph->insert_node(node);
        REQUIRE(result.has_value());
        pre_node_ids.push_back(result.value());
    }

    constexpr auto TEST_DURATION = std::chrono::seconds(5);

    ankerl::nanobench::Bench bench;
    bench.output(&nb_report_stream()).warmup(0).epochs(1).epochIterations(1);

    SECTION("Mixed read-write workload") {
        constexpr uint32_t NUM_READERS = 4;
        constexpr uint32_t NUM_WRITERS = 2;
        constexpr uint32_t TOTAL_THREADS = NUM_READERS + NUM_WRITERS;

        std::atomic<uint64_t> read_ops{0};
        std::atomic<uint64_t> write_ops{0};
        std::atomic<uint64_t> write_failures{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(TOTAL_THREADS);

        std::vector<std::thread> threads;
        threads.reserve(TOTAL_THREADS);

        auto start = std::chrono::steady_clock::now();

        bench.run("mixed_read_write", [&] {
            // Reader threads
            for (uint32_t t = 0; t < NUM_READERS; ++t) {
                threads.emplace_back([&, thread_id = t]() {
                    uint64_t local_ops = 0;
                    sync_point.arrive_and_wait();

                    while (!stop_flag.load(std::memory_order_relaxed)) {
                        uint64_t id = pre_node_ids[local_ops % pre_node_ids.size()];
                        auto node = graph->get_node(id);
                        ankerl::nanobench::doNotOptimizeAway(node);
                        local_ops++;
                    }

                    read_ops.fetch_add(local_ops, std::memory_order_relaxed);
                });
            }

            // Writer threads
            for (uint32_t t = 0; t < NUM_WRITERS; ++t) {
                threads.emplace_back([&, thread_id = t]() {
                    uint64_t base_id = 300000 + thread_id * 100000;
                    uint64_t local_ops = 0;
                    sync_point.arrive_and_wait();

                    while (!stop_flag.load(std::memory_order_relaxed)) {
                        auto node = GraphGenerator::create_test_node(
                            base_id + local_ops, graph->get_agent_id());
                        if (!graph->insert_node(node).has_value())
                            write_failures.fetch_add(1, std::memory_order_relaxed);
                        local_ops++;
                    }

                    write_ops.fetch_add(local_ops, std::memory_order_relaxed);
                });
            }

            std::this_thread::sleep_for(TEST_DURATION);
            stop_flag.store(true, std::memory_order_relaxed);
            for (auto& t : threads) t.join();

            bench.batch(read_ops.load() + write_ops.load());
            ankerl::nanobench::doNotOptimizeAway(read_ops.load());
        });

        if (write_failures.load() > 0)
            std::cerr << "[BENCH concurrent_read_write] "
                      << write_failures.load() << " insert_node calls failed\n";

        auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        collector.record_throughput("concurrent_reads", read_ops.load(), actual_duration,
            {{"num_readers", std::to_string(NUM_READERS)}});
        collector.record_throughput("concurrent_writes", write_ops.load(), actual_duration,
            {{"num_writers", std::to_string(NUM_WRITERS)}});

        double read_ops_sec = static_cast<double>(read_ops.load()) /
                             (static_cast<double>(actual_duration.count()) / 1000.0);
        double write_ops_sec = static_cast<double>(write_ops.load()) /
                              (static_cast<double>(actual_duration.count()) / 1000.0);

        INFO("Read throughput: " << read_ops_sec << " ops/sec");
        INFO("Write throughput: " << write_ops_sec << " ops/sec");
    }

    SECTION("Update contention test") {
        constexpr uint32_t NUM_THREADS = 4;

        // All threads update the same node
        auto test_node = GraphGenerator::create_test_node(
            0, graph->get_agent_id(), "contention_test");
        auto contention_id_opt = graph->insert_node(test_node);
        REQUIRE(contention_id_opt.has_value());
        uint64_t contention_node_id = contention_id_opt.value();

        std::atomic<uint64_t> total_ops{0};
        std::atomic<uint64_t> successful_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(NUM_THREADS);

        std::vector<std::thread> threads;
        threads.reserve(NUM_THREADS);

        auto start = std::chrono::steady_clock::now();

        bench.run("update_contention", [&] {
            for (uint32_t t = 0; t < NUM_THREADS; ++t) {
                threads.emplace_back([&, thread_id = t, node_id = contention_node_id]() {
                    uint64_t local_total = 0;
                    uint64_t local_success = 0;
                    sync_point.arrive_and_wait();

                    while (!stop_flag.load(std::memory_order_relaxed)) {
                        auto node = graph->get_node(node_id);
                        if (node) {
                            graph->add_or_modify_attrib_local<level_att>(
                                *node, static_cast<int32_t>(thread_id * 1000 + local_total));
                            if (graph->update_node(*node)) {
                                local_success++;
                            }
                        }
                        local_total++;
                    }

                    total_ops.fetch_add(local_total, std::memory_order_relaxed);
                    successful_ops.fetch_add(local_success, std::memory_order_relaxed);
                });
            }

            std::this_thread::sleep_for(TEST_DURATION);
            stop_flag.store(true, std::memory_order_relaxed);
            for (auto& t : threads) t.join();

            bench.batch(total_ops.load());
            ankerl::nanobench::doNotOptimizeAway(total_ops.load());
        });

        auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        double success_rate = static_cast<double>(successful_ops.load()) /
                             static_cast<double>(total_ops.load()) * 100.0;

        collector.record("update_contention_total", MetricCategory::Throughput,
            static_cast<double>(total_ops.load()), "ops",
            {{"num_threads", std::to_string(NUM_THREADS)}});
        collector.record("update_contention_success_rate", MetricCategory::Throughput,
            success_rate, "%",
            {{"num_threads", std::to_string(NUM_THREADS)}});

        INFO("Total attempts: " << total_ops.load());
        INFO("Successful updates: " << successful_ops.load());
        INFO("Success rate: " << success_rate << "%");
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "concurrent_read_write");
}

TEST_CASE("Multi-agent concurrent operations", "[THROUGHPUT][concurrent][multiagent][.multi][PROFILE][LOAD][MULTIAGENT]") {
    MultiAgentFixture fixture;
    GraphGenerator generator;
    MetricsCollector collector("multiagent_concurrent");

    auto config_file = generator.generate_empty_graph();
    REQUIRE(fixture.create_agents(4, config_file));
    fixture.wait_for_sync();

    constexpr auto TEST_DURATION = std::chrono::seconds(5);

    SECTION("Each agent writes independently") {
        std::atomic<uint64_t> total_ops{0};
        std::atomic<uint64_t> failed_ops{0};
        std::atomic<bool> stop_flag{false};
        std::barrier sync_point(fixture.agent_count());

        std::vector<std::thread> threads;
        threads.reserve(fixture.agent_count());

        auto start = std::chrono::steady_clock::now();

        for (size_t i = 0; i < fixture.agent_count(); ++i) {
            threads.emplace_back([&, agent_idx = i]() {
                auto* graph = fixture.get_agent(agent_idx);
                uint64_t base_id = 600000 + agent_idx * 100000;
                uint64_t local_ops = 0;

                sync_point.arrive_and_wait();

                while (!stop_flag.load(std::memory_order_relaxed)) {
                    auto node = GraphGenerator::create_test_node(
                        base_id + local_ops, graph->get_agent_id(),
                        "agent_" + std::to_string(agent_idx) + "_node_" + std::to_string(local_ops));
                    if (!graph->insert_node(node).has_value())
                        failed_ops.fetch_add(1, std::memory_order_relaxed);
                    local_ops++;
                }

                total_ops.fetch_add(local_ops, std::memory_order_relaxed);
            });
        }

        std::this_thread::sleep_for(TEST_DURATION);
        stop_flag.store(true, std::memory_order_relaxed);

        for (auto& t : threads) {
            t.join();
        }

        if (failed_ops.load() > 0)
            std::cerr << "[BENCH multiagent_concurrent] "
                      << failed_ops.load() << " insert_node calls failed\n";

        auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        collector.record_throughput("multiagent_concurrent_insert",
            total_ops.load(), actual_duration,
            {{"num_agents", std::to_string(fixture.agent_count())}});

        double ops_per_sec = static_cast<double>(total_ops.load()) /
                            (static_cast<double>(actual_duration.count()) / 1000.0);

        INFO("Multi-agent concurrent throughput: " << ops_per_sec << " ops/sec");
    }

    auto result = collector.finalize();
    ReportGenerator reporter("results");
    reporter.export_all(result, "multiagent_concurrent");
}
