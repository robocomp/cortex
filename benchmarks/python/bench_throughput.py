#!/usr/bin/env python3
"""
Benchmark: Single-agent throughput + latency for node/edge operations.

Runs a 5-second measurement window per operation while tracking per-op
latency via LatencyTracker.measure().  Exports to python_throughput.json.
"""

import sys
import os
import time

sys.path.insert(0, os.path.dirname(__file__))

from bench_utils import LatencyTracker, MetricsCollector, make_temp_config_file

try:
    import pydsr
except ImportError:
    print("Error: pydsr module not found.")
    sys.exit(1)

_DURATION = 5.0  # seconds per benchmark


def benchmark_node_insert(graph: pydsr.DSRGraph, collector: MetricsCollector):
    agent_id = graph.get_agent_id()
    tracker = LatencyTracker()
    ops = 0
    t_end = time.perf_counter() + _DURATION
    while time.perf_counter() < t_end:
        node = pydsr.Node(agent_id, "testtype", f"thr_ins_{ops}")
        with tracker.measure():
            graph.insert_node(node)
        ops += 1
    collector.record_throughput("node_insert", ops, _DURATION)
    collector.record_latency_stats("node_insert", tracker.stats())
    stats = tracker.stats()
    print(f"Node insert: {ops / _DURATION:.0f} ops/sec, mean {stats.mean_us:.2f} µs")


def benchmark_node_read(graph: pydsr.DSRGraph, collector: MetricsCollector):
    agent_id = graph.get_agent_id()

    # Pre-populate 1000 nodes for round-robin reads
    node_ids = []
    for i in range(1000):
        node = pydsr.Node(agent_id, "testtype", f"thr_rd_{i}")
        result = graph.insert_node(node)
        if result is not None:
            node_ids.append(result)
    if not node_ids:
        print("Node read: no nodes to read, skipping")
        return

    tracker = LatencyTracker()
    ops = 0
    pool = len(node_ids)
    t_end = time.perf_counter() + _DURATION
    while time.perf_counter() < t_end:
        nid = node_ids[ops % pool]
        with tracker.measure():
            graph.get_node(nid)
        ops += 1
    collector.record_throughput("node_read", ops, _DURATION)
    collector.record_latency_stats("node_read", tracker.stats())
    stats = tracker.stats()
    print(f"Node read:   {ops / _DURATION:.0f} ops/sec, mean {stats.mean_us:.2f} µs")


def benchmark_node_update(graph: pydsr.DSRGraph, collector: MetricsCollector):
    agent_id = graph.get_agent_id()

    node = pydsr.Node(agent_id, "testtype", "thr_upd_target")
    graph.insert_node(node)
    target = graph.get_node("thr_upd_target")
    if not target:
        print("Node update: could not retrieve target node, skipping")
        return

    tracker = LatencyTracker()
    ops = 0
    t_end = time.perf_counter() + _DURATION
    while time.perf_counter() < t_end:
        target.attrs["level"] = pydsr.Attribute(ops % 1000)
        with tracker.measure():
            graph.update_node(target)
        ops += 1
    collector.record_throughput("node_update", ops, _DURATION)
    collector.record_latency_stats("node_update", tracker.stats())
    stats = tracker.stats()
    print(f"Node update: {ops / _DURATION:.0f} ops/sec, mean {stats.mean_us:.2f} µs")


def benchmark_edge_insert(graph: pydsr.DSRGraph, collector: MetricsCollector):
    agent_id = graph.get_agent_id()

    root = graph.get_node("root")
    if not root:
        print("Edge insert: no root node, skipping")
        return

    # Pre-populate 1000 target nodes
    targets = []
    for i in range(1000):
        node = pydsr.Node(agent_id, "testtype", f"thr_etgt_{i}")
        graph.insert_node(node)
        n = graph.get_node(f"thr_etgt_{i}")
        if n:
            targets.append(n.id)
    if not targets:
        print("Edge insert: no target nodes, skipping")
        return

    tracker = LatencyTracker()
    ops = 0
    pool = len(targets)
    t_end = time.perf_counter() + _DURATION
    while time.perf_counter() < t_end:
        tid = targets[ops % pool]
        edge = pydsr.Edge(tid, root.id, "testtype_e", agent_id)
        with tracker.measure():
            graph.insert_or_assign_edge(edge)
        ops += 1
    collector.record_throughput("edge_insert", ops, _DURATION)
    collector.record_latency_stats("edge_insert", tracker.stats())
    stats = tracker.stats()
    print(f"Edge insert: {ops / _DURATION:.0f} ops/sec, mean {stats.mean_us:.2f} µs")


def benchmark_edge_read(graph: pydsr.DSRGraph, collector: MetricsCollector):
    agent_id = graph.get_agent_id()

    root = graph.get_node("root")
    if not root:
        print("Edge read: no root node, skipping")
        return

    # Pre-populate 1000 target nodes + edges
    targets = []
    for i in range(1000):
        node = pydsr.Node(agent_id, "testtype", f"thr_erd_{i}")
        graph.insert_node(node)
        n = graph.get_node(f"thr_erd_{i}")
        if n:
            targets.append(n.id)
            edge = pydsr.Edge(n.id, root.id, "testtype_e", agent_id)
            graph.insert_or_assign_edge(edge)
    if not targets:
        print("Edge read: no target edges, skipping")
        return

    tracker = LatencyTracker()
    ops = 0
    pool = len(targets)
    t_end = time.perf_counter() + _DURATION
    while time.perf_counter() < t_end:
        tid = targets[ops % pool]
        with tracker.measure():
            graph.get_edge(root.id, tid, "testtype_e")
        ops += 1
    collector.record_throughput("edge_read", ops, _DURATION)
    collector.record_latency_stats("edge_read", tracker.stats())
    stats = tracker.stats()
    print(f"Edge read:   {ops / _DURATION:.0f} ops/sec, mean {stats.mean_us:.2f} µs")


def main():
    print("=" * 60)
    print("DSR Python Throughput + Latency Benchmarks")
    print("=" * 60)
    print()

    collector = MetricsCollector("python_throughput")
    config_file = make_temp_config_file()

    graph = pydsr.DSRGraph(0, "bench_throughput", 43, config_file)
    time.sleep(0.5)

    print("--- Node operations ---")
    benchmark_node_insert(graph, collector)
    benchmark_node_read(graph, collector)
    benchmark_node_update(graph, collector)

    print("\n--- Edge operations ---")
    benchmark_edge_insert(graph, collector)
    benchmark_edge_read(graph, collector)

    del graph
    os.unlink(config_file)

    results_dir = os.environ.get(
        "BENCH_RESULTS_DIR",
        os.path.join(os.path.dirname(__file__), "..", "results"),
    )
    os.makedirs(results_dir, exist_ok=True)
    collector.export_json(os.path.join(results_dir, "python_throughput.json"))
    collector.export_csv(os.path.join(results_dir, "python_throughput.csv"))
    print(f"\nResults exported to {results_dir}")


if __name__ == "__main__":
    main()
