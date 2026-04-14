#!/usr/bin/env python3
"""
Benchmark: Python binding overhead vs C++.

Measures the overhead introduced by pybind11 bindings.  Pure Python object
creation (Node, Edge, Attribute) uses pyperf.Runner.bench_func() for
calibrated, multi-process timing.  Numpy array copy benchmarks use
bench_time_func() so the array setup happens outside the timed loop.

Graph creation overhead is measured separately with a LatencyTracker because
it is too expensive (~500 ms each) to repeat inside pyperf worker processes.
"""

import sys
import os
import time

sys.path.insert(0, os.path.dirname(__file__))

from bench_utils import (LatencyTracker, MetricsCollector, make_temp_config_file,
                         pyperf_to_latency_stats)

try:
    import pydsr
except ImportError:
    print("Error: pydsr module not found. Build with Python bindings enabled.")
    sys.exit(1)

try:
    import pyperf
except ImportError:
    print("Error: pyperf module not found.  Install with: pip install pyperf")
    sys.exit(1)


# ── Pure Python object creation (bench_func) ──────────────────────────────────

def _create_node():
    return pydsr.Node(1, "testtype", "bench_node")


def _create_edge():
    return pydsr.Edge(100, 200, "testtype_e", 1)


def _create_attr_str():
    return pydsr.Attribute("test_string")


def _create_attr_int():
    return pydsr.Attribute(42)


def _create_attr_float():
    return pydsr.Attribute(3.14159)


def _create_attr_list():
    return pydsr.Attribute([1.0, 2.0, 3.0])


# ── Numpy copy benchmarks (bench_time_func) ───────────────────────────────────

def _make_numpy_set_func(size: int):
    """Return a bench_time_func that times setting a numpy array attribute."""
    def time_func(loops):
        if not hasattr(time_func, "_data"):
            import numpy as np
            time_func._data = np.random.randint(0, 255, size, dtype=np.uint8)
            time_func._attr = pydsr.Attribute([0])
            for _ in range(10):  # warmup
                time_func._attr.value = time_func._data
        data = time_func._data
        attr = time_func._attr
        t1 = pyperf.perf_counter()
        for _ in range(loops):
            attr.value = data
        return pyperf.perf_counter() - t1
    time_func.__name__ = f"numpy_set_{size}"
    return time_func


def _make_numpy_get_func(size: int):
    """Return a bench_time_func that times getting a numpy array attribute."""
    def time_func(loops):
        if not hasattr(time_func, "_attr"):
            import numpy as np
            data = np.random.randint(0, 255, size, dtype=np.uint8)
            attr = pydsr.Attribute([0])
            attr.value = data
            for _ in range(10):  # warmup
                _ = attr.value
            time_func._attr = attr
        attr = time_func._attr
        t1 = pyperf.perf_counter()
        for _ in range(loops):
            _ = attr.value
        return pyperf.perf_counter() - t1
    time_func.__name__ = f"numpy_get_{size}"
    return time_func


# ── Graph creation (LatencyTracker — too expensive for pyperf workers) ─────────

def benchmark_graph_creation(collector: MetricsCollector):
    tracker = LatencyTracker(10)
    config_file = make_temp_config_file()

    for i in range(10):
        with tracker.measure():
            g = pydsr.DSRGraph(0, f"bench_graph_{i}", 100 + i, config_file)
        del g
        time.sleep(0.5)

    os.unlink(config_file)

    stats = tracker.stats()
    collector.record_latency_stats("graph_creation", stats)
    print(f"Graph creation: mean={stats.mean_ms:.2f} ms")


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    # Inject default pyperf tuning before Runner parses sys.argv.
    # Worker processes always receive --worker so they are skipped here.
    if "--worker" not in sys.argv:
        if "--values" not in sys.argv:
            sys.argv.extend(["--values", "20"])
        if "--warmups" not in sys.argv:
            sys.argv.extend(["--warmups", "5"])

    runner = pyperf.Runner()

    # runner.args may be None before the first bench_func call in some pyperf
    # versions; use sys.argv directly (worker processes always receive --worker).
    if "--worker" not in sys.argv:
        print("=" * 60)
        print("DSR Python Binding Overhead Benchmarks")
        print("=" * 60)

    # Pure Python object creation
    bm_node      = runner.bench_func("node_creation",      _create_node)
    bm_edge      = runner.bench_func("edge_creation",      _create_edge)
    bm_attr_str  = runner.bench_func("attribute_string",   _create_attr_str)
    bm_attr_int  = runner.bench_func("attribute_int",      _create_attr_int)
    bm_attr_float = runner.bench_func("attribute_float",   _create_attr_float)
    bm_attr_list = runner.bench_func("attribute_list",     _create_attr_list)

    # Numpy attribute benchmarks
    numpy_bms = {}
    try:
        import numpy  # noqa: F401 — check availability before spawning workers
        for size in [1000, 10000, 100000, 1000000]:
            numpy_bms[f"numpy_set_{size}"] = runner.bench_time_func(
                f"numpy_set_{size}", _make_numpy_set_func(size))
            numpy_bms[f"numpy_get_{size}"] = runner.bench_time_func(
                f"numpy_get_{size}", _make_numpy_get_func(size))
    except ImportError:
        print("Numpy not available, skipping numpy benchmarks")

    # Worker processes must not run the export code (stdout is not redirected,
    # so workers printing zeros would overwrite/corrupt the master's output).
    if "--worker" in sys.argv:
        return

    collector = MetricsCollector("binding_overhead")

    pyperf_items = [
        ("node_creation",    bm_node),
        ("edge_creation",    bm_edge),
        ("attribute_string", bm_attr_str),
        ("attribute_int",    bm_attr_int),
        ("attribute_float",  bm_attr_float),
        ("attribute_list",   bm_attr_list),
    ]
    for name, bm in pyperf_items:
        stats = pyperf_to_latency_stats(bm)
        collector.record_latency_stats(name, stats)
        print(f"{name}: mean={stats.mean_us:.3f} µs")

    for name, bm in numpy_bms.items():
        stats = pyperf_to_latency_stats(bm)
        collector.record_latency_stats(name, stats)
        print(f"{name}: mean={stats.mean_us:.2f} µs")

    print("\n--- Graph Creation ---")
    benchmark_graph_creation(collector)

    results_dir = os.environ.get(
        "BENCH_RESULTS_DIR",
        os.path.join(os.path.dirname(__file__), "..", "results"),
    )
    os.makedirs(results_dir, exist_ok=True)
    collector.export_json(os.path.join(results_dir, "python_binding_overhead.json"))
    collector.export_csv(os.path.join(results_dir, "python_binding_overhead.csv"))
    print(f"\nResults exported to {results_dir}")


if __name__ == "__main__":
    main()
