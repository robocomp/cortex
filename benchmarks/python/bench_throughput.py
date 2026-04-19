#!/usr/bin/env python3
"""
Benchmark: Single-agent throughput + latency for node/edge operations.

Uses pyperf.Runner with bench_time_func so that pyperf calibrates the
iteration count and runs multiple worker processes for noise reduction.
Each bench_time_func performs lazy setup (graph creation) outside the timed
loop on the first call; subsequent calls in the same worker reuse the graph.

The master process collects all Benchmark objects, converts them to
LatencyStats, and exports to python_throughput.json.
"""

import sys
import os
import time

sys.path.insert(0, os.path.dirname(__file__))

from bench_utils import MetricsCollector, make_temp_config_file, pyperf_to_latency_stats

try:
    import pydsr
except ImportError:
    print("Error: pydsr module not found.")
    sys.exit(1)

try:
    import pyperf
except ImportError:
    print("Error: pyperf module not found.  Install with: pip install pyperf")
    sys.exit(1)


# ── Lazy graph initialisation (runs once per worker process) ──────────────────

def _init_graph(tag: str, agent_id_hint: int = 43):
    """Create a DSRGraph and return (graph, config_path, agent_id)."""
    config = make_temp_config_file()
    graph = pydsr.DSRGraph(0, f"bench_throughput_{tag}", agent_id_hint, config)
    time.sleep(0.2)
    return graph, config, graph.get_agent_id()


# ── bench_time_func implementations ──────────────────────────────────────────
# Each function signature is (loops,) -> float (elapsed seconds).
# pyperf calls time_func(loops) — use pyperf.perf_counter() directly.
# State is stored as function attributes so setup only happens once per worker.

def _bench_node_insert(loops):
    if not hasattr(_bench_node_insert, "_graph"):
        graph, config, agent_id = _init_graph("insert")
        _bench_node_insert._graph = graph
        _bench_node_insert._config = config
        _bench_node_insert._agent_id = agent_id
        _bench_node_insert._counter = 0

    graph = _bench_node_insert._graph
    agent_id = _bench_node_insert._agent_id

    t1 = pyperf.perf_counter()
    for _ in range(loops):
        node = pydsr.Node(agent_id, "testtype", f"thr_ins_{_bench_node_insert._counter}")
        _bench_node_insert._counter += 1
        graph.insert_node(node)
    return pyperf.perf_counter() - t1


def _bench_node_read(loops):
    if not hasattr(_bench_node_read, "_graph"):
        graph, config, agent_id = _init_graph("read")
        node_ids = []
        for i in range(1000):
            node = pydsr.Node(agent_id, "testtype", f"thr_rd_{i}")
            nid = graph.insert_node(node)
            assert nid is not None
            node_ids.append(nid)
        for nid in node_ids:
            graph.get_node(nid)  # cache warmup
        _bench_node_read._graph = graph
        _bench_node_read._config = config
        _bench_node_read._node_ids = node_ids
        _bench_node_read._idx = 0

    graph = _bench_node_read._graph
    node_ids = _bench_node_read._node_ids
    idx = _bench_node_read._idx

    t1 = pyperf.perf_counter()
    for _ in range(loops):
        graph.get_node(node_ids[idx % len(node_ids)])
        idx += 1
    _bench_node_read._idx = idx
    return pyperf.perf_counter() - t1


def _bench_node_update(loops):
    if not hasattr(_bench_node_update, "_graph"):
        graph, config, agent_id = _init_graph("update")
        node = pydsr.Node(agent_id, "testtype", "thr_upd_target")
        nid = graph.insert_node(node)
        assert nid is not None
        target = graph.get_node("thr_upd_target")
        assert target is not None
        _bench_node_update._graph = graph
        _bench_node_update._config = config
        _bench_node_update._target = target
        _bench_node_update._counter = 0

    graph = _bench_node_update._graph
    target = _bench_node_update._target

    t1 = pyperf.perf_counter()
    for _ in range(loops):
        target.attrs["level"] = pydsr.Attribute(_bench_node_update._counter % 1000)
        _bench_node_update._counter += 1
        graph.update_node(target)
    return pyperf.perf_counter() - t1


def _bench_edge_insert(loops):
    if not hasattr(_bench_edge_insert, "_graph"):
        graph, config, agent_id = _init_graph("edge_insert", 44)
        root = graph.get_node("root")
        assert root is not None, "no root node"
        targets = []
        for i in range(1000):
            node = pydsr.Node(agent_id, "testtype", f"thr_etgt_{i}")
            ins = graph.insert_node(node)
            assert ins is not None
            n = graph.get_node(f"thr_etgt_{i}")
            assert n is not None
            targets.append(n.id)
        _bench_edge_insert._graph = graph
        _bench_edge_insert._config = config
        _bench_edge_insert._agent_id = agent_id
        _bench_edge_insert._root_id = root.id
        _bench_edge_insert._targets = targets
        _bench_edge_insert._idx = 0

    graph = _bench_edge_insert._graph
    agent_id = _bench_edge_insert._agent_id
    root_id = _bench_edge_insert._root_id
    targets = _bench_edge_insert._targets
    idx = _bench_edge_insert._idx

    t1 = pyperf.perf_counter()
    for _ in range(loops):
        tid = targets[idx % len(targets)]
        edge = pydsr.Edge(tid, root_id, "testtype_e", agent_id)
        graph.insert_or_assign_edge(edge)
        idx += 1
    _bench_edge_insert._idx = idx
    return pyperf.perf_counter() - t1


def _bench_edge_read(loops):
    if not hasattr(_bench_edge_read, "_graph"):
        graph, config, agent_id = _init_graph("edge_read", 45)
        root = graph.get_node("root")
        assert root is not None, "no root node"
        targets = []
        for i in range(1000):
            node = pydsr.Node(agent_id, "testtype", f"thr_erd_{i}")
            ins = graph.insert_node(node)
            assert ins is not None
            n = graph.get_node(f"thr_erd_{i}")
            assert n is not None
            targets.append(n.id)
            edge = pydsr.Edge(n.id, root.id, "testtype_e", agent_id)
            graph.insert_or_assign_edge(edge)
        for tid in targets:
            graph.get_edge(root.id, tid, "testtype_e")  # cache warmup
        _bench_edge_read._graph = graph
        _bench_edge_read._config = config
        _bench_edge_read._root_id = root.id
        _bench_edge_read._targets = targets
        _bench_edge_read._idx = 0

    graph = _bench_edge_read._graph
    root_id = _bench_edge_read._root_id
    targets = _bench_edge_read._targets
    idx = _bench_edge_read._idx

    t1 = pyperf.perf_counter()
    for _ in range(loops):
        graph.get_edge(root_id, targets[idx % len(targets)], "testtype_e")
        idx += 1
    _bench_edge_read._idx = idx
    return pyperf.perf_counter() - t1


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

    bm_node_insert = runner.bench_time_func("node_insert", _bench_node_insert)
    bm_node_read   = runner.bench_time_func("node_read",   _bench_node_read)
    bm_node_update = runner.bench_time_func("node_update", _bench_node_update)
    bm_edge_insert = runner.bench_time_func("edge_insert", _bench_edge_insert)
    bm_edge_read   = runner.bench_time_func("edge_read",   _bench_edge_read)

    # Worker processes must not run the export code (stdout is not redirected,
    # so workers printing zeros would overwrite/corrupt the master's output).
    if "--worker" in sys.argv:
        return
    collector = MetricsCollector("python_throughput")

    benchmarks = [
        ("node_insert", bm_node_insert),
        ("node_read",   bm_node_read),
        ("node_update", bm_node_update),
        ("edge_insert", bm_edge_insert),
        ("edge_read",   bm_edge_read),
    ]
    for name, bm in benchmarks:
        stats = pyperf_to_latency_stats(bm)
        collector.record_latency_stats(name, stats)
        if stats.mean_ns > 0:
            collector.record_throughput(name, 1, stats.mean_ns / 1e9)
        print(f"{name}: mean={stats.mean_us:.2f} µs  stddev={stats.stddev_ns/1000:.2f} µs")

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
