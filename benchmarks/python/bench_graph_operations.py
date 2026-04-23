#!/usr/bin/env python3
"""
Benchmark: Graph operations (CRUD) performance.

Measures insert, read, update, delete performance for nodes and edges.

pyperf is intentionally not used here: the benchmark functions share a single
DSRGraph instance and depend on each other's side-effects (e.g. edge
benchmarks rely on nodes inserted by node benchmarks).  pyperf's per-worker
subprocess model would require re-running the full setup chain in each worker,
and the shared-state dependency makes clean isolation impractical.
"""

import sys
import os
import time

sys.path.insert(0, os.path.dirname(__file__))

from bench_utils import LatencyTracker, MetricsCollector, make_temp_config_file, warmup, sync_modes, create_graph

try:
    import pydsr
except ImportError:
    print("Error: pydsr module not found.")
    sys.exit(1)


def benchmark_node_operations(graph: pydsr.DSRGraph, collector: MetricsCollector):
    """Benchmark node CRUD operations."""
    agent_id = graph.get_agent_id()

    # --- Insert ---
    tracker = LatencyTracker(2000)
    base_id = 10000

    # Warmup
    for i in range(100):
        node = pydsr.Node(agent_id, "testtype", f"warmup_{i}")
        result = graph.insert_node(node)
        assert result is not None, f"Warmup insert_node failed at i={i}"

    # Measure
    for i in range(2000):
        node = pydsr.Node(agent_id, "testtype", f"bench_node_{i}")
        with tracker.measure():
            result = graph.insert_node(node)
        assert result is not None, f"insert_node failed at i={i}"

    stats = tracker.stats()
    collector.record_latency_stats("node_insert", stats)
    print(f"Node insert: mean={stats.mean_us:.2f} us, p99={stats.p99_us:.2f} us")

    # --- Read by ID ---
    tracker = LatencyTracker(3000)
    nodes = graph.get_nodes()
    node_ids = [n.id for n in nodes[:100]]
    # Warmup: touch all IDs to bring them into cache
    for node_id in node_ids:
        node = graph.get_node(node_id)
        assert node is not None, f"Warmup get_node({node_id}) returned None"

    for i in range(3000):
        node_id = node_ids[i % len(node_ids)]
        with tracker.measure():
            node = graph.get_node(node_id)
        assert node is not None, f"get_node({node_id}) returned None"

    stats = tracker.stats()
    collector.record_latency_stats("node_read_by_id", stats)
    print(f"Node read (by id): mean={stats.mean_us:.2f} us")

    # --- Read by name ---
    tracker = LatencyTracker(3000)
    node_names = [f"bench_node_{i}" for i in range(100)]
    for name in node_names:
        node = graph.get_node(name)
        assert node is not None, f"Warmup get_node('{name}') returned None"

    for i in range(3000):
        name = node_names[i % len(node_names)]
        with tracker.measure():
            node = graph.get_node(name)
        assert node is not None, f"get_node('{name}') returned None"

    stats = tracker.stats()
    collector.record_latency_stats("node_read_by_name", stats)
    print(f"Node read (by name): mean={stats.mean_us:.2f} us")

    # --- Update ---
    tracker = LatencyTracker(2000)
    test_node = graph.get_node("bench_node_0")
    assert test_node is not None, "bench_node_0 not found for update benchmark"

    for i in range(2000):
        test_node.attrs["level"] = pydsr.Attribute(i)
        with tracker.measure():
            result = graph.update_node(test_node)
        assert result, f"update_node failed at i={i}"

    stats = tracker.stats()
    collector.record_latency_stats("node_update", stats)
    print(f"Node update: mean={stats.mean_us:.2f} us")

    # --- Delete ---
    tracker = LatencyTracker(500)
    delete_nodes = [f"bench_node_{i}" for i in range(1500, 2000)]

    for name in delete_nodes:
        with tracker.measure():
            result = graph.delete_node(name)
        assert result, f"delete_node('{name}') failed"

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
        result = graph.insert_node(node)
        assert result is not None, f"insert_node failed for edge_target_{i}"

    time.sleep(0.1)

    # --- Insert edge ---
    tracker = LatencyTracker(200)

    for i in range(200):
        target = graph.get_node(f"edge_target_{i}")
        assert target is not None, f"edge_target_{i} not found for edge insert"
        edge = pydsr.Edge(target.id, root.id, "testtype_e", agent_id)
        with tracker.measure():
            result = graph.insert_or_assign_edge(edge)
        assert result, f"insert_or_assign_edge failed for edge_target_{i}"

    stats = tracker.stats()
    collector.record_latency_stats("edge_insert", stats)
    print(f"Edge insert: mean={stats.mean_us:.2f} us, p99={stats.p99_us:.2f} us")

    # --- Read edge ---
    tracker = LatencyTracker(500)

    for i in range(500):
        target = graph.get_node(f"edge_target_{i % 200}")
        assert target is not None, f"edge_target_{i % 200} not found for edge read"
        with tracker.measure():
            edge = graph.get_edge(root.id, target.id, "testtype_e")
        assert edge is not None, f"get_edge returned None for edge_target_{i % 200}"

    stats = tracker.stats()
    collector.record_latency_stats("edge_read", stats)
    print(f"Edge read: mean={stats.mean_us:.2f} us")

    # --- Delete edge ---
    tracker = LatencyTracker(100)

    for i in range(100, 200):
        target = graph.get_node(f"edge_target_{i}")
        assert target is not None, f"edge_target_{i} not found for edge delete"
        with tracker.measure():
            result = graph.delete_edge(root.id, target.id, "testtype_e")
        assert result, f"delete_edge failed for edge_target_{i}"

    stats = tracker.stats()
    collector.record_latency_stats("edge_delete", stats)
    print(f"Edge delete: mean={stats.mean_us:.2f} us")


def benchmark_query_operations(graph: pydsr.DSRGraph, collector: MetricsCollector):
    """Benchmark query operations."""

    # --- get_nodes ---
    tracker = LatencyTracker(500)

    for _ in range(500):
        with tracker.measure():
            graph.get_nodes()

    stats = tracker.stats()
    collector.record_latency_stats("get_all_nodes", stats)
    print(f"get_nodes(): mean={stats.mean_us:.2f} us")

    # --- get_nodes_by_type ---
    tracker = LatencyTracker(500)

    for _ in range(500):
        with tracker.measure():
            graph.get_nodes_by_type("testtype")

    stats = tracker.stats()
    collector.record_latency_stats("get_nodes_by_type", stats)
    print(f"get_nodes_by_type(): mean={stats.mean_us:.2f} us")

    # --- get_edges (from node) ---
    root = graph.get_node("root")
    if root:
        tracker = LatencyTracker(500)

        for _ in range(500):
            with tracker.measure():
                graph.get_edges(root.id)

        stats = tracker.stats()
        collector.record_latency_stats("get_edges_from_node", stats)
        print(f"get_edges(id): mean={stats.mean_us:.2f} us")

    # --- get_edges_to_id ---
    if root:
        tracker = LatencyTracker(500)

        for _ in range(500):
            with tracker.measure():
                graph.get_edges_to_id(root.id)

        stats = tracker.stats()
        collector.record_latency_stats("get_edges_to_id", stats)
        print(f"get_edges_to_id(id): mean={stats.mean_us:.2f} us")

    # --- get_edges_by_type ---
    tracker = LatencyTracker(500)

    for _ in range(500):
        with tracker.measure():
            graph.get_edges_by_type("testtype_e")

    stats = tracker.stats()
    collector.record_latency_stats("get_edges_by_type", stats)
    print(f"get_edges_by_type(): mean={stats.mean_us:.2f} us")


def main():
    print("=" * 60)
    print("DSR Python Graph Operations Benchmarks")
    print("=" * 60)
    print()

    # Export
    results_dir = os.environ.get(
        "BENCH_RESULTS_DIR",
        os.path.join(os.path.dirname(__file__), "..", "results"),
    )
    os.makedirs(results_dir, exist_ok=True)

    for sync_label, sync_mode in sync_modes(pydsr):
        print(f"--- Backend: {sync_label} ---")
        collector = MetricsCollector(f"graph_operations_{sync_label}")
        collector.metadata["sync_mode"] = sync_label

        config_file = make_temp_config_file()
        graph = create_graph(pydsr, f"bench_graph_ops_{sync_label}", 42, config_file, sync_mode)
        time.sleep(0.5)

        print("--- Node Operations ---")
        benchmark_node_operations(graph, collector)

        print("\n--- Edge Operations ---")
        benchmark_edge_operations(graph, collector)

        print("\n--- Query Operations ---")
        benchmark_query_operations(graph, collector)

        del graph
        os.unlink(config_file)

        collector.export_json(os.path.join(results_dir, f"python_graph_operations_{sync_label}.json"))
        collector.export_csv(os.path.join(results_dir, f"python_graph_operations_{sync_label}.csv"))
    print(f"\nResults exported to {results_dir}")


if __name__ == "__main__":
    main()
