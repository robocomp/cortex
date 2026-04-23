#!/usr/bin/env python3
"""
Stable Python baseline benchmarks on a fixed graph.

This intentionally avoids graph growth during measurement. The goal is to
provide a low-noise Python baseline for lookup/query/update paths and binding
costs, not to model end-to-end insertion throughput.
"""

import sys
import os
import time

sys.path.insert(0, os.path.dirname(__file__))

from bench_utils import LatencyTracker, MetricsCollector, make_temp_config_file, sync_modes, create_graph

try:
    import pydsr
except ImportError:
    print("Error: pydsr module not found.")
    sys.exit(1)


def benchmark_fixed_graph(graph: pydsr.DSRGraph, collector: MetricsCollector):
    agent_id = graph.get_agent_id()
    root = graph.get_node("root")
    assert root is not None, "root node missing"

    # Keep the Python baseline bounded so the top-level default run stays usable.
    node_ids = []
    for i in range(300):
        node = pydsr.Node(agent_id, "testtype", f"baseline_node_{i}")
        inserted = graph.insert_node(node)
        assert inserted is not None, f"insert_node failed for baseline_node_{i}"
        node_ids.append(inserted)
        edge = pydsr.Edge(inserted, root.id, "testtype_e", agent_id)
        assert graph.insert_or_assign_edge(edge), f"insert edge failed for baseline_node_{i}"

    for node_id in node_ids:
        assert graph.get_node(node_id) is not None
    graph.get_nodes()
    graph.get_nodes_by_type("testtype")
    graph.get_edges(root.id)
    graph.get_edges_by_type("testtype_e")

    tracker = LatencyTracker(1000)
    for i in range(1000):
        node_id = node_ids[i % len(node_ids)]
        with tracker.measure():
            node = graph.get_node(node_id)
        assert node is not None
    collector.record_latency_stats("node_read_by_id", tracker.stats())

    tracker = LatencyTracker(500)
    for i in range(500):
        name = f"baseline_node_{i % len(node_ids)}"
        with tracker.measure():
            node = graph.get_node(name)
        assert node is not None
    collector.record_latency_stats("node_read_by_name", tracker.stats())

    tracker = LatencyTracker(500)
    target = graph.get_node("baseline_node_0")
    assert target is not None
    for i in range(500):
        target.attrs["level"] = pydsr.Attribute(i)
        with tracker.measure():
            ok = graph.update_node(target)
        assert ok
    collector.record_latency_stats("node_update", tracker.stats())

    tracker = LatencyTracker(100)
    for _ in range(100):
        with tracker.measure():
            nodes = graph.get_nodes()
        assert nodes
    collector.record_latency_stats("get_nodes", tracker.stats())

    tracker = LatencyTracker(100)
    for _ in range(100):
        with tracker.measure():
            nodes = graph.get_nodes_by_type("testtype")
        assert nodes
    collector.record_latency_stats("get_nodes_by_type", tracker.stats())

    tracker = LatencyTracker(300)
    for i in range(300):
        node_id = node_ids[i % len(node_ids)]
        with tracker.measure():
            edge = graph.get_edge(root.id, node_id, "testtype_e")
        assert edge is not None
    collector.record_latency_stats("edge_read", tracker.stats())

    tracker = LatencyTracker(100)
    for _ in range(100):
        with tracker.measure():
            edges = graph.get_edges_by_type("testtype_e")
        assert edges
    collector.record_latency_stats("get_edges_by_type", tracker.stats())


def main():
    print("=" * 60)
    print("DSR Python Baseline Graph Benchmarks")
    print("=" * 60)
    print()

    results_dir = os.environ.get(
        "BENCH_RESULTS_DIR",
        os.path.join(os.path.dirname(__file__), "..", "results"),
    )
    os.makedirs(results_dir, exist_ok=True)

    for sync_label, sync_mode in sync_modes(pydsr):
        collector = MetricsCollector(f"python_baseline_graph_{sync_label}")
        collector.metadata["profile"] = "baseline"
        collector.metadata["sync_mode"] = sync_label

        config_file = make_temp_config_file()
        graph = create_graph(pydsr, f"python_baseline_graph_{sync_label}", 84, config_file, sync_mode)
        time.sleep(0.3)

        benchmark_fixed_graph(graph, collector)

        del graph
        os.unlink(config_file)

        collector.export_json(os.path.join(results_dir, f"python_baseline_graph_{sync_label}.json"))
        collector.export_csv(os.path.join(results_dir, f"python_baseline_graph_{sync_label}.csv"))
    print(f"\nResults exported to {results_dir}")


if __name__ == "__main__":
    main()
