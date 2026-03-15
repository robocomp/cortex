#!/usr/bin/env python3
"""
Benchmark: Signal/callback performance.

Measures signal connection, emission, and callback invocation overhead.
"""

import sys
import os
import time
import threading

sys.path.insert(0, os.path.dirname(__file__))

from bench_utils import LatencyTracker, MetricsCollector, make_temp_config_file

try:
    import pydsr
except ImportError:
    print("Error: pydsr module not found.")
    sys.exit(1)


def benchmark_signal_callback_latency(graph: pydsr.DSRGraph, collector: MetricsCollector):
    """Measure signal callback invocation latency."""
    agent_id = graph.get_agent_id()
    tracker = LatencyTracker(100)

    callback_time = [0]
    callback_received = threading.Event()
    expected_id = [0]

    def on_node_update(node_id: int, node_type: str):
        if node_id == expected_id[0]:
            callback_time[0] = time.perf_counter_ns()
            callback_received.set()

    # Connect signal
    pydsr.signals.connect(graph, pydsr.signals.UPDATE_NODE, on_node_update)

    # Warmup
    for i in range(20):
        node = pydsr.Node(agent_id, "testtype", f"warmup_sig_{i}")
        graph.insert_node(node)
        time.sleep(0.05)

    # Measure
    for i in range(100):
        callback_received.clear()
        node = pydsr.Node(agent_id, "testtype", f"signal_node_{i}")

        send_time = time.perf_counter_ns()
        expected_id[0] = graph.insert_node(node)

        # Wait for callback
        if callback_received.wait(timeout=2.0):
            latency = callback_time[0] - send_time
            tracker.record(latency)

    stats = tracker.stats()
    collector.record_latency_stats("signal_callback_latency", stats)
    print(f"Signal callback latency: mean={stats.mean_us:.2f} us, p99={stats.p99_us:.2f} us")
    print(f"  (received {tracker.count}/100 callbacks)")


def benchmark_signal_throughput(graph: pydsr.DSRGraph, collector: MetricsCollector):
    """Measure how many signals can be processed per second.

    Uses a fixed insert count instead of a time-based loop to keep the
    callback backlog bounded.  An unbounded loop (e.g. 3 s × 40K inserts/sec)
    creates a queue that outlasts the benchmark and blocks graph teardown.
    """
    agent_id = graph.get_agent_id()

    callback_count = [0]

    def on_node_update(node_id: int, node_type: str):
        callback_count[0] += 1

    pydsr.signals.connect(graph, pydsr.signals.UPDATE_NODE, on_node_update)

    INSERT_COUNT = 3000
    print("Generating signals...")
    start = time.perf_counter()

    for i in range(INSERT_COUNT):
        node = pydsr.Node(agent_id, "testtype", f"sig_tp_{i}")
        graph.insert_node(node)

    # Wait for callbacks to drain, but give up after a timeout so teardown
    # isn't blocked indefinitely if the callback rate is very slow.
    drain_deadline = time.perf_counter() + 5.0
    prev = -1
    while time.perf_counter() < drain_deadline:
        time.sleep(0.1)
        cur = callback_count[0]
        if cur == prev:          # no new callbacks — queue is drained
            break
        prev = cur

    duration = time.perf_counter() - start
    callbacks_per_sec = callback_count[0] / duration

    collector.record_throughput("signal_callbacks", callback_count[0], duration)
    print(f"Signal throughput: {callbacks_per_sec:.0f} callbacks/sec")
    print(f"  ({callback_count[0]} callbacks for {INSERT_COUNT} inserts)")


def benchmark_multiple_handlers(graph: pydsr.DSRGraph, collector: MetricsCollector):
    """Measure impact of multiple signal handlers."""
    agent_id = graph.get_agent_id()

    for num_handlers in [1, 5, 10]:
        callback_counts = [0] * num_handlers

        def make_handler(idx):
            def handler(node_id: int, node_type: str):
                callback_counts[idx] += 1
            return handler

        # Connect multiple handlers
        handlers = [make_handler(i) for i in range(num_handlers)]
        for h in handlers:
            pydsr.signals.connect(graph, pydsr.signals.UPDATE_NODE, h)

        # Generate updates
        insert_count = 100
        start = time.perf_counter()

        for i in range(insert_count):
            node = pydsr.Node(agent_id, "testtype", f"mh_{num_handlers}_{i}")
            graph.insert_node(node)

        time.sleep(0.3)  # Let callbacks process
        duration = time.perf_counter() - start

        total_callbacks = sum(callback_counts)
        collector.record("callbacks_with_handlers", "throughput",
                        total_callbacks / duration,
                        "callbacks/sec",
                        tags={"num_handlers": str(num_handlers)})

        print(f"{num_handlers} handlers: {total_callbacks} callbacks in {duration:.2f}s")


def main():
    print("=" * 60)
    print("DSR Python Signal Benchmarks")
    print("=" * 60)
    print()

    collector = MetricsCollector("signals")

    config_file = make_temp_config_file()
    graph = pydsr.DSRGraph(0, "bench_signals", 42, config_file)
    time.sleep(0.5)

    print("--- Signal Callback Latency ---")
    benchmark_signal_callback_latency(graph, collector)

    print("\n--- Signal Throughput ---")
    benchmark_signal_throughput(graph, collector)

    print("\n--- Multiple Handlers Impact ---")
    benchmark_multiple_handlers(graph, collector)

    del graph
    os.unlink(config_file)

    # Export
    results_dir = os.environ.get(
        "BENCH_RESULTS_DIR",
        os.path.join(os.path.dirname(__file__), "..", "results"),
    )
    os.makedirs(results_dir, exist_ok=True)
    collector.export_json(os.path.join(results_dir, "python_signals.json"))
    collector.export_csv(os.path.join(results_dir, "python_signals.csv"))
    print(f"\nResults exported to {results_dir}")


if __name__ == "__main__":
    main()
