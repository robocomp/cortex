#!/usr/bin/env python3
"""
Benchmark: Python binding overhead vs C++.

Measures the overhead introduced by pybind11 bindings by comparing
Python API operations with baseline measurements.
"""

import sys
import os
import time

# Add parent to path for imports
sys.path.insert(0, os.path.dirname(__file__))

from bench_utils import LatencyTracker, MetricsCollector, make_temp_config_file, warmup

try:
    import pydsr
except ImportError:
    print("Error: pydsr module not found. Build with Python bindings enabled.")
    sys.exit(1)


def benchmark_node_creation():
    """Benchmark Node object creation overhead."""
    collector = MetricsCollector("binding_overhead_node_creation")
    tracker = LatencyTracker(1000)

    # Warmup
    for i in range(100):
        _ = pydsr.Node(1, "testtype", f"warmup_{i}")

    # Measure
    for i in range(1000):
        with tracker.measure():
            node = pydsr.Node(1, "testtype", f"node_{i}")

    stats = tracker.stats()
    collector.record_latency_stats("node_creation", stats)
    print(f"Node creation: mean={stats.mean_us:.2f} us, p99={stats.p99_us:.2f} us")

    return collector


def benchmark_edge_creation():
    """Benchmark Edge object creation overhead."""
    collector = MetricsCollector("binding_overhead_edge_creation")
    tracker = LatencyTracker(1000)

    # Warmup
    for i in range(100):
        _ = pydsr.Edge(100, 200, "testtype_e", 1)

    # Measure
    for i in range(1000):
        with tracker.measure():
            edge = pydsr.Edge(100, 200 + i, "testtype_e", 1)

    stats = tracker.stats()
    collector.record_latency_stats("edge_creation", stats)
    print(f"Edge creation: mean={stats.mean_us:.2f} us, p99={stats.p99_us:.2f} us")

    return collector


def benchmark_attribute_creation():
    """Benchmark Attribute creation with different types."""
    collector = MetricsCollector("binding_overhead_attribute")

    # String attribute
    tracker = LatencyTracker(1000)
    warmup(lambda: pydsr.Attribute("test"))
    for _ in range(1000):
        with tracker.measure():
            _ = pydsr.Attribute("test_string")
    stats = tracker.stats()
    collector.record_latency_stats("attribute_string", stats)
    print(f"Attribute(string): mean={stats.mean_us:.2f} us")

    # Int attribute
    tracker = LatencyTracker(1000)
    warmup(lambda: pydsr.Attribute(42))
    for _ in range(1000):
        with tracker.measure():
            _ = pydsr.Attribute(42)
    stats = tracker.stats()
    collector.record_latency_stats("attribute_int", stats)
    print(f"Attribute(int): mean={stats.mean_us:.2f} us")

    # Float attribute
    tracker = LatencyTracker(1000)
    warmup(lambda: pydsr.Attribute(3.14))
    for _ in range(1000):
        with tracker.measure():
            _ = pydsr.Attribute(3.14159)
    stats = tracker.stats()
    collector.record_latency_stats("attribute_float", stats)
    print(f"Attribute(float): mean={stats.mean_us:.2f} us")

    # List attribute
    tracker = LatencyTracker(1000)
    test_list = [1.0, 2.0, 3.0]
    warmup(lambda: pydsr.Attribute(test_list))
    for _ in range(1000):
        with tracker.measure():
            _ = pydsr.Attribute(test_list)
    stats = tracker.stats()
    collector.record_latency_stats("attribute_list", stats)
    print(f"Attribute(list[3]): mean={stats.mean_us:.2f} us")

    return collector


def benchmark_attribute_numpy():
    """Benchmark Attribute with numpy arrays (large data)."""
    try:
        import numpy as np
    except ImportError:
        print("Numpy not available, skipping numpy benchmarks")
        return None

    collector = MetricsCollector("binding_overhead_numpy")

    for size in [1000, 10000, 100000, 1000000]:
        tracker = LatencyTracker(100)
        data = np.random.randint(0, 255, size, dtype=np.uint8)

        # Warmup
        for _ in range(10):
            attr = pydsr.Attribute([0])
            attr.value = data

        # Measure set
        for _ in range(100):
            attr = pydsr.Attribute([0])
            with tracker.measure():
                attr.value = data

        stats = tracker.stats()
        collector.record_latency_stats(f"numpy_set_{size}", stats,
                                        tags={"size": str(size)})
        print(f"Numpy set ({size} bytes): mean={stats.mean_us:.2f} us")

        # Measure get
        tracker = LatencyTracker(100)
        attr = pydsr.Attribute([0])
        attr.value = data
        for _ in range(100):
            with tracker.measure():
                _ = attr.value

        stats = tracker.stats()
        collector.record_latency_stats(f"numpy_get_{size}", stats,
                                        tags={"size": str(size)})
        print(f"Numpy get ({size} bytes): mean={stats.mean_us:.2f} us")

    return collector


def benchmark_graph_creation():
    """Benchmark DSRGraph creation overhead."""
    collector = MetricsCollector("binding_overhead_graph")
    tracker = LatencyTracker(10)

    config_file = make_temp_config_file()

    # This is expensive, only do a few iterations
    for i in range(10):
        with tracker.measure():
            g = pydsr.DSRGraph(0, f"bench_graph_{i}", 100 + i, config_file)
        del g
        time.sleep(0.5)  # Allow cleanup

    stats = tracker.stats()
    collector.record_latency_stats("graph_creation", stats)
    print(f"Graph creation: mean={stats.mean_ms:.2f} ms")

    os.unlink(config_file)
    return collector


def main():
    print("=" * 60)
    print("DSR Python Binding Overhead Benchmarks")
    print("=" * 60)
    print()

    collectors = []

    print("--- Node/Edge/Attribute Creation ---")
    collectors.append(benchmark_node_creation())
    collectors.append(benchmark_edge_creation())
    collectors.append(benchmark_attribute_creation())

    print("\n--- Numpy Array Operations ---")
    numpy_collector = benchmark_attribute_numpy()
    if numpy_collector:
        collectors.append(numpy_collector)

    print("\n--- Graph Creation ---")
    collectors.append(benchmark_graph_creation())

    # Export results
    print("\n--- Exporting Results ---")
    results_dir = os.environ.get(
        "BENCH_RESULTS_DIR",
        os.path.join(os.path.dirname(__file__), "..", "results"),
    )
    os.makedirs(results_dir, exist_ok=True)

    for c in collectors:
        if c:
            c.export_json(os.path.join(results_dir, f"python_{c.benchmark_name}.json"))
            c.export_csv(os.path.join(results_dir, f"python_{c.benchmark_name}.csv"))

    print(f"Results exported to {results_dir}")


if __name__ == "__main__":
    main()
