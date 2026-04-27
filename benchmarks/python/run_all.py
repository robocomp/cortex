#!/usr/bin/env python3
"""
Run all DSR Python benchmarks and record the results as a named run.

Usage:
    python run_all.py                       # auto-timestamped run
    python run_all.py --label "after-fix"   # labelled run
    python run_all.py --list                # list previous runs
    python run_all.py --delete <run-id>     # remove a run from the index
"""

import sys
import os
import subprocess
import time
import json
import argparse
import platform
from datetime import datetime

ALL_BENCHMARKS = [
    "bench_binding_overhead.py",
    "bench_baseline_graph.py",
    "bench_graph_operations.py",
    "bench_throughput.py",
    "bench_signals.py",
]

BASELINE_BENCHMARKS = [
    "bench_baseline_graph.py",
]

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_RESULTS_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "results"))
RUNS_INDEX = os.path.join(DEFAULT_RESULTS_ROOT, "runs.json")


# ── Index helpers ─────────────────────────────────────────────────────────────

def load_runs() -> list:
    if not os.path.isfile(RUNS_INDEX):
        return []
    try:
        with open(RUNS_INDEX) as f:
            return json.load(f)
    except PermissionError:
        print(f"WARNING: cannot read benchmark index: {RUNS_INDEX} (permission denied)")
        return []


def save_runs(runs: list):
    os.makedirs(DEFAULT_RESULTS_ROOT, exist_ok=True)
    try:
        with open(RUNS_INDEX, "w") as f:
            json.dump(runs, f, indent=2)
    except PermissionError:
        print(f"WARNING: cannot update benchmark index: {RUNS_INDEX} (permission denied)")


def register_run(run_info: dict):
    runs = load_runs()
    runs = [r for r in runs if r["id"] != run_info["id"]]
    runs.append(run_info)
    runs.sort(key=lambda r: r["id"])
    save_runs(runs)


# ── Commands ──────────────────────────────────────────────────────────────────

def cmd_list():
    runs = load_runs()
    if not runs:
        print("No runs recorded yet.")
        return
    print(f"{'ID':<22}  {'Label':<20}  {'Pass/Total':>10}  {'Duration':>9}")
    print("-" * 70)
    for r in runs:
        ratio = f"{r.get('benchmarks_passed', 0)}/{r.get('benchmarks_run', 0)}"
        dur = f"{r.get('total_duration_sec', 0):.1f}s"
        label = r.get("label") or "-"
        print(f"{r['id']:<22}  {label:<20}  {ratio:>10}  {dur:>9}")


def cmd_delete(run_id: str):
    runs = load_runs()
    before = len(runs)
    runs = [r for r in runs if r["id"] != run_id]
    if len(runs) == before:
        print(f"Run '{run_id}' not found in index.")
        return
    save_runs(runs)
    print(f"Removed run '{run_id}' from index (result files kept on disk).")


def cmd_run_direct(benchmarks) -> int:
    """Run benchmarks using BENCH_RESULTS_DIR already set in the environment.

    Called by the top-level run_benchmarks.py wrapper so it can manage the
    run directory and index registration itself.
    """
    results_dir = os.environ.get("BENCH_RESULTS_DIR", ".")
    print("=" * 70)
    print("  DSR Python Benchmark Suite")
    print(f"  Output : {results_dir}")
    print("=" * 70)
    print()

    env = dict(os.environ)
    results = []
    suite_start = time.time()

    for bench in benchmarks:
        bench_path = os.path.join(SCRIPT_DIR, bench)
        print(f"\n{'=' * 70}")
        print(f"Running: {bench}")
        print("=" * 70)
        try:
            proc = subprocess.run([sys.executable, bench_path], cwd=SCRIPT_DIR, env=env, timeout=300)
            results.append((bench, proc.returncode == 0))
        except subprocess.TimeoutExpired:
            print(f"TIMEOUT: {bench}")
            results.append((bench, False))
        except Exception as e:
            print(f"ERROR: {bench}: {e}")
            results.append((bench, False))

    total_duration = time.time() - suite_start
    passed = sum(1 for _, ok in results if ok)
    print(f"\n  {passed}/{len(results)} benchmarks completed in {total_duration:.1f}s")
    return 0 if all(ok for _, ok in results) else 1


def cmd_run(label, results_root, benchmarks):
    ts = datetime.now()
    run_id = ts.strftime("%Y%m%dT%H%M%S")
    dir_name = run_id if not label else f"{run_id}_{label.replace(' ', '-')}"
    run_dir = os.path.join(results_root, dir_name)
    os.makedirs(run_dir, exist_ok=True)

    print("=" * 70)
    print(f"  DSR Python Benchmark Suite")
    print(f"  Run ID : {run_id}")
    if label:
        print(f"  Label  : {label}")
    print(f"  Output : {run_dir}")
    print("=" * 70)
    print()

    env = {**os.environ, "BENCH_RESULTS_DIR": run_dir}

    results = []
    suite_start = time.time()

    for bench in benchmarks:
        bench_path = os.path.join(SCRIPT_DIR, bench)
        print(f"\n{'=' * 70}")
        print(f"Running: {bench}")
        print("=" * 70)

        try:
            proc = subprocess.run(
                [sys.executable, bench_path],
                cwd=SCRIPT_DIR,
                env=env,
                timeout=300,
            )
            results.append((bench, proc.returncode == 0))
        except subprocess.TimeoutExpired:
            print(f"TIMEOUT: {bench}")
            results.append((bench, False))
        except Exception as e:
            print(f"ERROR: {bench}: {e}")
            results.append((bench, False))

    total_duration = time.time() - suite_start

    try:
        git_hash = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=SCRIPT_DIR, stderr=subprocess.DEVNULL,
        ).decode().strip()
    except Exception:
        git_hash = ""

    run_info = {
        "id": run_id,
        "label": label or "",
        "dir": dir_name,
        "timestamp": ts.isoformat(),
        "total_duration_sec": round(total_duration, 2),
        "benchmarks_run": len(results),
        "benchmarks_passed": sum(1 for _, ok in results if ok),
        "git_hash": git_hash,
        "platform": platform.platform(),
        "python": sys.version.split()[0],
    }

    with open(os.path.join(run_dir, "run_info.json"), "w") as f:
        json.dump(run_info, f, indent=2)

    register_run(run_info)

    print("\n" + "=" * 70)
    print("  Summary")
    print("=" * 70)
    for bench, ok in results:
        print(f"  [{'PASS' if ok else 'FAIL'}] {bench}")

    passed = sum(1 for _, ok in results if ok)
    print(f"\n  {passed}/{len(results)} benchmarks completed in {total_duration:.1f}s")
    print(f"  Run ID  : {run_id}")
    print(f"  Results : {run_dir}")
    print(f"  Index   : {RUNS_INDEX}")

    return 0 if all(ok for _, ok in results) else 1


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Run DSR benchmarks and track results")
    parser.add_argument("--label", "-l", help="Human-readable label for this run")
    parser.add_argument("--results-root", default=DEFAULT_RESULTS_ROOT,
                        help="Root directory for all run results")
    parser.add_argument("--list", action="store_true", help="List all recorded runs")
    parser.add_argument("--delete", metavar="RUN_ID", help="Remove a run from the index")
    parser.add_argument("--direct", action="store_true",
                        help="Run benchmarks using BENCH_RESULTS_DIR from env, skip index registration")
    parser.add_argument("--baseline", action="store_true",
                        help="Run only the curated low-noise Python baseline set")
    args = parser.parse_args()

    benchmarks = BASELINE_BENCHMARKS if args.baseline else ALL_BENCHMARKS

    if args.list:
        cmd_list()
        return 0

    if args.delete:
        cmd_delete(args.delete)
        return 0

    if args.direct:
        return cmd_run_direct(benchmarks)

    return cmd_run(args.label, args.results_root, benchmarks)


if __name__ == "__main__":
    sys.exit(main())
