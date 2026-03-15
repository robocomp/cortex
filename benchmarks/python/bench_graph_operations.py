#!/usr/bin/env python3
"""
Benchmark: Graph operations (CRUD) performance.

Measures insert, read, update, delete performance for nodes and edges.
"""

import sys
import os
import time

sys.path.insert(0, os.path.dirname(__file__))

from bench_utils import LatencyTracker, MetricsCollector, make_temp_config_file, warmup

try:
    import pydsr
except ImportError:
    print("Error: pydsr module not found.")
    sys.exit(1)


def benchmark_node_operations(graph: pydsr.DSRGraph, collector: MetricsCollector):
    """Benchmark node CRUD operations."""
    agent_id = graph.get_agent_id()

    # --- Insert ---
    tracker = LatencyTracker(500)
    base_id = 10000

    # Warmup
    for i in range(50):
        node = pydsr.Node(agent_id, "testtype", f"warmup_{i}")
        graph.insert_node(node)

    # Measure
    for i in range(500):
        node = pydsr.Node(agent_id, "testtype", f"bench_node_{i}")
        with tracker.measure():
            graph.insert_node(node)

    stats = tracker.stats()
    collector.record_latency_stats("node_insert", stats)
    print(f"Node insert: mean={stats.mean_us:.2f} us, p99={stats.p99_us:.2f} us")

    # --- Read by ID ---
    tracker = LatencyTracker(1000)
    nodes = graph.get_nodes()
    node_ids = [n.id for n in nodes[:100]]

    for _ in range(1000):
        node_id = node_ids[_ % len(node_ids)]
        with tracker.measure():
            _ = graph.get_node(node_id)

    stats = tracker.stats()
    collector.record_latency_stats("node_read_by_id", stats)
    print(f"Node read (by id): mean={stats.mean_us:.2f} us")

    # --- Read by name ---
    tracker = LatencyTracker(1000)
    node_names = [f"bench_node_{i}" for i in range(100)]

    for i in range(1000):
        name = node_names[i % len(node_names)]
        with tracker.measure():
            _ = graph.get_node(name)

    stats = tracker.stats()
    collector.record_latency_stats("node_read_by_name", stats)
    print(f"Node read (by name): mean={stats.mean_us:.2f} us")

    # --- Update ---
    tracker = LatencyTracker(500)
    test_node = graph.get_node("bench_node_0")

    for i in range(500):
        test_node.attrs["level"] = pydsr.Attribute(i)
        with tracker.measure():
            graph.update_node(test_node)

    stats = tracker.stats()
    collector.record_latency_stats("node_update", stats)
    print(f"Node update: mean={stats.mean_us:.2f} us")

    # --- Delete ---
    tracker = LatencyTracker(100)
    delete_nodes = [f"bench_node_{i}" for i in range(400, 500)]

    for name in delete_nodes:
        with tracker.measure():
            graph.delete_node(name)

    stats = tracker.stats()
    collector.record_latency_stats("node_delete", stats)
    print(f"Node delete: mean={stats.mean_us:.2f} us")


def benchmark_edge_operations(graph: pydsr.DSRGraph, collector: MetricsCollector):
    """Benchmark edge CRUD operations."""
    agent_id = graph.get_agent_id()

    # Get root node
    root = graph.get_node("root")
    if not root:
        print("No root node found")
        return

    # Create target nodes for edges
    for i in range(200):
        node = pydsr.Node(agent_id, "testtype", f"edge_target_{i}")
        graph.insert_node(node)

    time.sleep(0.1)

    # --- Insert edge ---
    tracker = LatencyTracker(200)

    for i in range(200):
        target = graph.get_node(f"edge_target_{i}")
        if target:
            edge = pydsr.Edge(target.id, root.id, "testtype_e", agent_id)
            with tracker.measure():
                graph.insert_or_assign_edge(edge)

    stats = tracker.stats()
    collector.record_latency_stats("edge_insert", stats)
    print(f"Edge insert: mean={stats.mean_us:.2f} us, p99={stats.p99_us:.2f} us")

    # --- Read edge ---
    tracker = LatencyTracker(500)

    for i in range(500):
        target = graph.get_node(f"edge_target_{i % 200}")
        if target:
            with tracker.measure():
                _ = graph.get_edge(root.id, target.id, "testtype_e")

    stats = tracker.stats()
    collector.record_latency_stats("edge_read", stats)
    print(f"Edge read: mean={stats.mean_us:.2f} us")

    # --- Delete edge ---
    tracker = LatencyTracker(100)

    for i in range(100, 200):
        target = graph.get_node(f"edge_target_{i}")
        if target:
            with tracker.measure():
                graph.delete_edge(root.id, target.id, "testtype_e")

    stats = tracker.stats()
    collector.record_latency_stats("edge_delete", stats)
    print(f"Edge delete: mean={stats.mean_us:.2f} us")


def benchmark_query_operations(graph: pydsr.DSRGraph, collector: MetricsCollector):
    """Benchmark query operations."""

    # --- get_nodes ---
    tracker = LatencyTracker(100)

    for _ in range(100):
        with tracker.measure():
            _ = graph.get_nodes()

    stats = tracker.stats()
    collector.record_latency_stats("get_all_nodes", stats)
    print(f"get_nodes(): mean={stats.mean_us:.2f} us")

    # --- get_nodes_by_type ---
    tracker = LatencyTracker(100)

    for _ in range(100):
        with tracker.measure():
            _ = graph.get_nodes_by_type("testtype")

    stats = tracker.stats()
    collector.record_latency_stats("get_nodes_by_type", stats)
    print(f"get_nodes_by_type(): mean={stats.mean_us:.2f} us")

    # --- get_edges (from node) ---
    root = graph.get_node("root")
    if root:
        tracker = LatencyTracker(100)

        for _ in range(100):
            with tracker.measure():
                _ = graph.get_edges(root.id)

        stats = tracker.stats()
        collector.record_latency_stats("get_edges_from_node", stats)
        print(f"get_edges(id): mean={stats.mean_us:.2f} us")

    # --- get_edges_to_id ---
    if root:
        tracker = LatencyTracker(100)

        for _ in range(100):
            with tracker.measure():
                _ = graph.get_edges_to_id(root.id)

        stats = tracker.stats()
        collector.record_latency_stats("get_edges_to_id", stats)
        print(f"get_edges_to_id(id): mean={stats.mean_us:.2f} us")

    # --- get_edges_by_type ---
    tracker = LatencyTracker(100)

    for _ in range(100):
        with tracker.measure():
            _ = graph.get_edges_by_type("testtype_e")

    stats = tracker.stats()
    collector.record_latency_stats("get_edges_by_type", stats)
    print(f"get_edges_by_type(): mean={stats.mean_us:.2f} us")


def main():
    print("=" * 60)
    print("DSR Python Graph Operations Benchmarks")
    print("=" * 60)
    print()

    collector = MetricsCollector("graph_operations")

    # Create graph
    config_file = make_temp_config_file()
    graph = pydsr.DSRGraph(0, "bench_graph_ops", 42, config_file)
    time.sleep(0.5)

    print("--- Node Operations ---")
    benchmark_node_operations(graph, collector)

    print("\n--- Edge Operations ---")
    benchmark_edge_operations(graph, collector)

    print("\n--- Query Operations ---")
    benchmark_query_operations(graph, collector)

    # Cleanup
    del graph
    os.unlink(config_file)

    # Export
    results_dir = os.environ.get(
        "BENCH_RESULTS_DIR",
        os.path.join(os.path.dirname(__file__), "..", "results"),
    )
    os.makedirs(results_dir, exist_ok=True)
    collector.export_json(os.path.join(results_dir, "python_graph_operations.json"))
    collector.export_csv(os.path.join(results_dir, "python_graph_operations.csv"))
    print(f"\nResults exported to {results_dir}")


if __name__ == "__main__":
    main()
