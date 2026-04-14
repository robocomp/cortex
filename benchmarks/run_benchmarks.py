#!/usr/bin/env python3
"""
Top-level DSR benchmark runner — executes C++ and Python suites in one shot.

Usage:
    python run_benchmarks.py                         # run both suites
    python run_benchmarks.py --label "after-fix"     # named run
    python run_benchmarks.py --cpp-only              # skip Python
    python run_benchmarks.py --python-only           # skip C++
    python run_benchmarks.py --build                 # cmake build before running
    python run_benchmarks.py --all                   # include hidden tests ([.multi], [.extended])
    python run_benchmarks.py --cpp-filter "[LATENCY]"# pass filter to dsr_benchmarks
    python run_benchmarks.py --report                # open HTML report when done
    python run_benchmarks.py --compare <run-id>      # compare against a previous run
    python run_benchmarks.py --list                  # list recorded runs
    python run_benchmarks.py --delete <run-id>       # remove a run from the index
    python run_benchmarks.py --repeat 5              # run C++ 5× and report median
    python run_benchmarks.py --priority -10          # run with higher OS priority (requires root)
    python run_benchmarks.py --taskset 0,1           # pin C++ benchmarks to CPU cores 0 and 1
    python run_benchmarks.py --no-cpu-tune           # skip governor/turbo tuning (Linux)
"""

import sys
import os
import subprocess
import time
import json
import argparse
import platform
import shlex
import tempfile
from typing import Optional
from datetime import datetime

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PYTHON_DIR = os.path.join(SCRIPT_DIR, "python")
BUILD_DIR = os.path.join(SCRIPT_DIR, "build")
RESULTS_ROOT = os.path.join(SCRIPT_DIR, "results")
RUNS_INDEX = os.path.join(RESULTS_ROOT, "runs.json")
BASELINE_CPP_FILTER = "[BASELINE]~[.multi]"
# Catch2 v3 has no single spec that matches both visible and hidden tests.
# _run_cpp_once detects this sentinel and runs the binary twice:
#   1. no filter   → all visible tests
#   2. "[.]"        → all hidden tests (tags starting with '.')
ALL_CPP_FILTER = "__ALL_INCLUDING_HIDDEN__"
DEFAULT_STABILITY_WARN_PCT = 5.0


# ── Index helpers (mirrors python/run_all.py) ──────────────────────────────────

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
    os.makedirs(RESULTS_ROOT, exist_ok=True)
    try:
        fd, tmp_path = tempfile.mkstemp(prefix="runs.", suffix=".json.tmp", dir=RESULTS_ROOT)
        try:
            with os.fdopen(fd, "w") as f:
                json.dump(runs, f, indent=2)
            os.replace(tmp_path, RUNS_INDEX)
        except Exception:
            try:
                os.unlink(tmp_path)
            except OSError:
                pass
            raise
    except PermissionError:
        print(f"WARNING: cannot update benchmark index: {RUNS_INDEX} (permission denied)")


def register_run(run_info: dict):
    runs = load_runs()
    runs = [r for r in runs if r["id"] != run_info["id"]]
    runs.append(run_info)
    runs.sort(key=lambda r: r["id"])
    save_runs(runs)


# ── Locate C++ binary ─────────────────────────────────────────────────────────

def find_cpp_binary(override: Optional[str]) -> Optional[str]:
    if override:
        return override if os.path.isfile(override) else None
    candidate = os.path.join(BUILD_DIR, "dsr_benchmarks")
    return candidate if os.path.isfile(candidate) else None


def win_to_wsl(path: str) -> str:
    """Convert a Windows absolute path to a WSL /mnt/... path."""
    path = path.replace("\\", "/")
    if len(path) >= 2 and path[1] == ":":
        drive = path[0].lower()
        path = f"/mnt/{drive}{path[2:]}"
    return path


def is_wsl_needed() -> bool:
    """Return True if we're on Windows and wsl.exe is available (ELF binary)."""
    if platform.system() != "Windows":
        return False
    try:
        subprocess.run(["wsl", "--version"], capture_output=True, timeout=3)
        return True
    except Exception:
        return False


# ── Build step ────────────────────────────────────────────────────────────────

def build_cpp() -> bool:
    if not os.path.isdir(BUILD_DIR):
        print(f"Build directory not found: {BUILD_DIR}")
        return False
    print("Building C++ benchmarks...")
    if is_wsl_needed():
        wsl_build = win_to_wsl(BUILD_DIR)
        result = subprocess.run(
            ["wsl", "-e", "bash", "-c", f"cmake --build {wsl_build} --parallel"],
            cwd=SCRIPT_DIR,
        )
    else:
        result = subprocess.run(
            ["cmake", "--build", BUILD_DIR, "--parallel"],
            cwd=SCRIPT_DIR,
        )
    return result.returncode == 0


# ── Median merge ──────────────────────────────────────────────────────────────

def _median(values: list) -> float:
    """Return the median of a list of numbers (handles even-length lists)."""
    import statistics
    return statistics.median(values) if values else 0.0


def _summarize_repeat_stability(src_dirs: list[str], dest_dir: str,
                                warn_pct: Optional[float] = DEFAULT_STABILITY_WARN_PCT):
    import statistics

    summaries = []
    all_files: set[str] = set()
    for d in src_dirs:
        results_d = os.path.join(d, "results")
        if os.path.isdir(results_d):
            for f in os.listdir(results_d):
                if f.endswith(".json"):
                    all_files.add(f)

    def metric_key(m: dict) -> str:
        tags = m.get("tags", {})
        tag_str = ",".join(f"{k}={v}" for k, v in sorted(tags.items()))
        return f"{m.get('category', '')}|{m['name']}|{m.get('unit', '')}|{tag_str}"

    for basename in sorted(all_files):
        loaded = []
        for d in src_dirs:
            path = os.path.join(d, "results", basename)
            if os.path.isfile(path):
                with open(path) as fh:
                    loaded.append(json.load(fh))

        metric_runs: dict[str, list[dict]] = {}
        for run_data in loaded:
            for m in run_data.get("metrics", []):
                metric_runs.setdefault(metric_key(m), []).append(m)

        for key, peers in sorted(metric_runs.items()):
            values = [p["value"] for p in peers if isinstance(p.get("value"), (int, float))]
            if len(values) < 2:
                continue
            median = statistics.median(values)
            min_v = min(values)
            max_v = max(values)
            spread_pct = ((max_v - min_v) / median * 100.0) if median else 0.0
            stdev_pct = ((statistics.stdev(values) / median) * 100.0) if len(values) > 1 and median else 0.0
            exemplar = peers[0]
            summaries.append({
                "source_file": basename,
                "name": exemplar["name"],
                "category": exemplar.get("category", ""),
                "unit": exemplar.get("unit", ""),
                "tags": exemplar.get("tags", {}),
                "repeat_values": values,
                "median": median,
                "min": min_v,
                "max": max_v,
                "spread_pct": round(spread_pct, 2),
                "stdev_pct": round(stdev_pct, 2),
            })

    os.makedirs(dest_dir, exist_ok=True)
    out_path = os.path.join(dest_dir, "stability_summary.json")
    with open(out_path, "w") as fh:
        json.dump({"metrics": summaries}, fh, indent=2)

    warnings = []
    if summaries:
        print("\nRepeat stability summary:")
        for s in summaries:
            print(f"  {s['category']}/{s['name']}: median={s['median']:.3f} {s['unit']} "
                  f"spread={s['spread_pct']:.2f}% stdev={s['stdev_pct']:.2f}%")
            if warn_pct is not None and s["spread_pct"] > warn_pct:
                warnings.append(s)

    if warnings:
        print(f"\nStability warnings (spread > {warn_pct:.2f}%):")
        for s in warnings:
            print(f"  {s['category']}/{s['name']} tags={s['tags']} spread={s['spread_pct']:.2f}%")

    return {
        "warn_threshold_pct": warn_pct,
        "warning_count": len(warnings),
        "warnings": warnings,
        "metrics": summaries,
    }


def merge_cpp_results(src_dirs: list[str], dest_dir: str):
    """
    Load the same JSON result files from N run directories and write a merged
    copy to dest_dir where each metric's numerical fields are replaced by the
    median across all N runs.  Non-numeric fields (name, unit, tags, category)
    are taken from the first run.

    This cancels OS-scheduler noise: a single run that was preempted by a
    Windows background process no longer inflates the reported mean.
    """
    import statistics as _stats

    os.makedirs(dest_dir, exist_ok=True)

    # Collect all JSON basenames present in any source directory
    all_files: set[str] = set()
    for d in src_dirs:
        results_d = os.path.join(d, "results")
        if os.path.isdir(results_d):
            for f in os.listdir(results_d):
                if f.endswith(".json"):
                    all_files.add(f)

    merged_count = 0
    for basename in sorted(all_files):
        # Load this file from every run that has it
        loaded = []
        for d in src_dirs:
            path = os.path.join(d, "results", basename)
            if os.path.isfile(path):
                try:
                    with open(path) as fh:
                        loaded.append(json.load(fh))
                except Exception as e:
                    print(f"  Warning: could not load {path}: {e}", file=sys.stderr)

        if not loaded:
            continue

        if len(loaded) == 1:
            # Only one run has this file — copy as-is
            import shutil
            shutil.copy(os.path.join(src_dirs[0], "results", basename),
                        os.path.join(dest_dir, basename))
            continue

        # Build merged result: start from first run's structure
        merged = json.loads(json.dumps(loaded[0]))  # deep copy

        # Index metrics by category+name+unit+tags so latency/throughput records
        # for the same operation do not get merged into each other.
        def metric_key(m: dict) -> str:
            tags = m.get("tags", {})
            tag_str = ",".join(f"{k}={v}" for k, v in sorted(tags.items()))
            return f"{m.get('category', '')}|{m['name']}|{m.get('unit', '')}|{tag_str}"

        per_run_metrics: dict[str, list[dict]] = {}
        for run_data in loaded:
            for m in run_data.get("metrics", []):
                k = metric_key(m)
                per_run_metrics.setdefault(k, []).append(m)

        merged_metrics = []
        for m in merged.get("metrics", []):
            k = metric_key(m)
            peers = per_run_metrics.get(k, [m])
            if len(peers) < 2:
                merged_metrics.append(m)
                continue

            merged_m = json.loads(json.dumps(m))  # deep copy
            # Median the top-level value
            values = [p["value"] for p in peers if isinstance(p.get("value"), (int, float))]
            if values:
                merged_m["value"] = _median(values)

            # Median all additional numeric fields
            all_add_keys: set[str] = set()
            for p in peers:
                all_add_keys.update(p.get("additional", {}).keys())
            for key in all_add_keys:
                vals = [p.get("additional", {}).get(key)
                        for p in peers if isinstance(p.get("additional", {}).get(key), (int, float))]
                if vals:
                    merged_m.setdefault("additional", {})[key] = _median(vals)

            merged_metrics.append(merged_m)

        merged["metrics"] = merged_metrics
        merged.setdefault("metadata", {})["repeat_runs"] = str(len(loaded))
        merged["metadata"]["aggregation"] = "median"

        out_path = os.path.join(dest_dir, basename)
        with open(out_path, "w") as fh:
            json.dump(merged, fh, indent=2)
        merged_count += 1

    print(f"  Merged {merged_count} result file(s) from {len(src_dirs)} runs (median)")


# ── CPU tuning ────────────────────────────────────────────────────────────────

def _cpu_count() -> int:
    try:
        import multiprocessing
        return multiprocessing.cpu_count()
    except Exception:
        return 1


def _read_sysfs(path: str) -> Optional[str]:
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return None


def _write_sysfs(path: str, value: str) -> bool:
    try:
        with open(path, "w") as f:
            f.write(value + "\n")
        return True
    except OSError:
        return False


def setup_cpu_for_benchmarking() -> dict:
    """
    Configure the CPU for stable benchmarking:
      - Set scaling governor to 'performance' on all CPUs
      - Disable turbo boost (Intel pstate or generic cpufreq boost)

    Returns a dict of original settings so restore_cpu_settings() can revert them.
    Prints a warning and returns an empty dict if the process lacks write permission.
    """
    if platform.system() != "Linux":
        return {}

    saved = {"governors": {}, "intel_no_turbo": None, "amd_boost": None}
    any_written = False
    permission_error = False

    n_cpus = _cpu_count()
    for i in range(n_cpus):
        gov_path = f"/sys/devices/system/cpu/cpu{i}/cpufreq/scaling_governor"
        current = _read_sysfs(gov_path)
        if current is None:
            continue
        saved["governors"][gov_path] = current
        if current != "performance":
            if _write_sysfs(gov_path, "performance"):
                any_written = True
            else:
                permission_error = True

    # Intel pstate: write "1" to disable turbo
    intel_path = "/sys/devices/system/cpu/intel_pstate/no_turbo"
    val = _read_sysfs(intel_path)
    if val is not None:
        saved["intel_no_turbo"] = val
        if val != "1":
            if _write_sysfs(intel_path, "1"):
                any_written = True
            else:
                permission_error = True

    # AMD / generic: write "0" to disable boost
    amd_path = "/sys/devices/system/cpu/cpufreq/boost"
    val = _read_sysfs(amd_path)
    if val is not None:
        saved["amd_boost"] = val
        if val != "0":
            if _write_sysfs(amd_path, "0"):
                any_written = True
            else:
                permission_error = True

    if permission_error:
        print(
            "\nWARNING: Could not set CPU governor/turbo (permission denied).\n"
            "  Run with sudo, or manually run:  sudo pyperf system tune\n"
            "  Benchmarks may show instability due to frequency scaling.\n"
        )
        return {}

    if any_written:
        print("  CPU tuning: governor=performance, turbo disabled")

    return saved


def restore_cpu_settings(saved: dict):
    """Revert CPU governor and turbo settings to the values captured by setup_cpu_for_benchmarking()."""
    if not saved:
        return

    for path, value in saved.get("governors", {}).items():
        _write_sysfs(path, value)

    if saved.get("intel_no_turbo") is not None:
        _write_sysfs("/sys/devices/system/cpu/intel_pstate/no_turbo", saved["intel_no_turbo"])

    if saved.get("amd_boost") is not None:
        _write_sysfs("/sys/devices/system/cpu/cpufreq/boost", saved["amd_boost"])

    print("  CPU settings restored")


# ── Run C++ suite ─────────────────────────────────────────────────────────────

def _build_cpp_cmd(binary: str, catch2_filter: Optional[str], verbose: bool,
                   priority: Optional[int], taskset: Optional[str]) -> str:
    """Build the shell command string for one C++ benchmark invocation."""
    parts = []
    if taskset:
        parts += [f"taskset -c {shlex.quote(taskset)}"]
    if priority is not None:
        parts += [f"nice -n {priority}"]
    wsl_binary = win_to_wsl(binary) if is_wsl_needed() else binary
    parts.append(shlex.quote(wsl_binary))
    if catch2_filter:
        parts.append(shlex.quote(catch2_filter))
    if verbose:
        parts.append("--verbose")
    return " ".join(parts)


def _run_cpp_once(binary: str, cpp_cwd: str, catch2_filter: Optional[str],
                  verbose: bool, priority: Optional[int], taskset: Optional[str]) -> tuple[bool, float]:
    # Catch2 v3 has no single-spec "run everything including hidden".
    # Handle the sentinel by running visible tests then hidden tests in the same cwd.
    if catch2_filter == ALL_CPP_FILTER:
        ok1, dur1 = _run_cpp_once(binary, cpp_cwd, None,  verbose, priority, taskset)
        ok2, dur2 = _run_cpp_once(binary, cpp_cwd, "[.]", verbose, priority, taskset)
        return ok1 and ok2, dur1 + dur2

    os.makedirs(cpp_cwd, exist_ok=True)
    start = time.time()
    if is_wsl_needed():
        wsl_cwd = win_to_wsl(cpp_cwd)
        cmd_str = _build_cpp_cmd(binary, catch2_filter, verbose, priority, taskset)
        bash_cmd = f"cd {wsl_cwd} && {cmd_str}"
        result = subprocess.run(["wsl", "-e", "bash", "-c", bash_cmd])
    else:
        cmd = []
        if taskset:
            cmd += ["taskset", "-c", taskset]
        if priority is not None:
            cmd += ["nice", "-n", str(priority)]
        cmd.append(binary)
        if catch2_filter:
            cmd.append(catch2_filter)
        if verbose:
            cmd.append("--verbose")
        result = subprocess.run(cmd, cwd=cpp_cwd)
    duration = time.time() - start
    return result.returncode == 0, duration


def run_cpp(binary: str, run_dir: str, catch2_filter: Optional[str], verbose: bool,
            repeat: int = 1, priority: Optional[int] = None, taskset: Optional[str] = None,
            stability_warn_pct: Optional[float] = DEFAULT_STABILITY_WARN_PCT):
    """
    Run dsr_benchmarks 'repeat' times.  If repeat > 1, each invocation writes
    to a separate cpp_N/ subdirectory; results are then median-merged into
    cpp/results/ so the rest of the pipeline sees a single stable result set.
    """
    print(f"\n{'=' * 70}")
    print(f"Running: C++ benchmarks ({os.path.basename(binary)})")
    if catch2_filter == ALL_CPP_FILTER:
        print("Filter : (all — visible + hidden)")
    elif catch2_filter:
        print(f"Filter : {catch2_filter}")
    if repeat > 1:
        print(f"Repeat : {repeat}× (median aggregation)")
    if priority is not None:
        print(f"Priority: nice {priority:+d}")
    if taskset:
        print(f"CPU affinity: {taskset}")
    print("=" * 70)

    total_start = time.time()
    all_ok = True
    stability = None

    if repeat <= 1:
        # Single run — original behaviour
        cpp_cwd = os.path.join(run_dir, "cpp")
        print(f"Output : {cpp_cwd}/results/")
        ok, dur = _run_cpp_once(binary, cpp_cwd, catch2_filter, verbose, priority, taskset)
        all_ok = ok
    else:
        # Multiple runs → median merge
        run_cwds = []
        for r in range(1, repeat + 1):
            cpp_cwd = os.path.join(run_dir, f"cpp_{r}")
            print(f"\n--- Run {r}/{repeat} → {cpp_cwd}/results/ ---")
            ok, dur = _run_cpp_once(binary, cpp_cwd, catch2_filter, verbose, priority, taskset)
            if not ok:
                print(f"  Warning: run {r} exited non-zero")
                all_ok = False
            run_cwds.append(cpp_cwd)

        # Merge into canonical cpp/results/
        dest = os.path.join(run_dir, "cpp", "results")
        print(f"\nMerging {repeat} runs → {dest}")
        merge_cpp_results(run_cwds, dest)
        stability = _summarize_repeat_stability(run_cwds, dest, warn_pct=stability_warn_pct)

    total_dur = time.time() - total_start
    print(f"\nC++ suite {'PASSED' if all_ok else 'FAILED'} in {total_dur:.1f}s")
    return all_ok, total_dur, stability


# ── Run Python suite ──────────────────────────────────────────────────────────

def run_python(run_dir: str, label: Optional[str], baseline: bool = False):
    """
    Delegate to python/run_all.py passing BENCH_RESULTS_DIR so Python files
    land directly in <run_dir>/ (not a subdirectory).
    """
    print(f"\n{'=' * 70}")
    print("Running: Python benchmarks")
    print(f"Output : {run_dir}/")
    print("=" * 70)

    env = {**os.environ, "BENCH_RESULTS_DIR": run_dir}
    cmd = [sys.executable, os.path.join(PYTHON_DIR, "run_all.py"), "--direct"]
    if baseline:
        cmd.append("--baseline")
    # --direct: benchmarks write to BENCH_RESULTS_DIR, skip run_all.py's own
    # index registration so run_benchmarks.py stays the single source of truth.

    start = time.time()
    result = subprocess.run(cmd, cwd=PYTHON_DIR, env=env)
    duration = time.time() - start

    ok = result.returncode == 0
    print(f"\nPython suite {'PASSED' if ok else 'FAILED'} in {duration:.1f}s")
    return ok, duration


# ── Ownership / permission helpers ───────────────────────────────────────────

def _fix_run_permissions(run_dir: str):
    """
    When the script is run via sudo, chown the run directory and the shared
    results index back to the original user so they remain accessible without
    root.  Falls back to world-readable permissions when the original user
    cannot be determined (e.g. direct root login).
    """
    if os.getuid() != 0:
        return  # Not running as root — nothing to do.

    sudo_uid_str = os.environ.get("SUDO_UID")
    sudo_gid_str = os.environ.get("SUDO_GID")

    if sudo_uid_str:
        uid = int(sudo_uid_str)
        gid = int(sudo_gid_str) if sudo_gid_str else uid

        def _chown_tree(path: str):
            for dirpath, dirnames, filenames in os.walk(path, topdown=False):
                for name in filenames:
                    try:
                        os.chown(os.path.join(dirpath, name), uid, gid)
                    except OSError:
                        pass
                try:
                    os.chown(dirpath, uid, gid)
                except OSError:
                    pass

        _chown_tree(run_dir)

        # Also fix the shared index file and RESULTS_ROOT itself so the user
        # can write new runs later without sudo.
        for path in (RUNS_INDEX, RESULTS_ROOT):
            try:
                os.chown(path, uid, gid)
            except OSError:
                pass

        try:
            import pwd as _pwd
            username = _pwd.getpwuid(uid).pw_name
            print(f"  Ownership transferred to {username} (uid={uid}, gid={gid})")
        except Exception:
            print(f"  Ownership transferred to uid={uid}, gid={gid}")
    else:
        # Direct root login — make results world-readable as a fallback.
        import stat
        _file_mode = (stat.S_IRUSR | stat.S_IWUSR |
                      stat.S_IRGRP |
                      stat.S_IROTH)
        _dir_mode  = _file_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH

        for dirpath, dirnames, filenames in os.walk(run_dir, topdown=False):
            for name in filenames:
                try:
                    os.chmod(os.path.join(dirpath, name), _file_mode)
                except OSError:
                    pass
            try:
                os.chmod(dirpath, _dir_mode)
            except OSError:
                pass

        print("  Results made world-readable (root without sudo; SUDO_UID not set)")


# ── Commands ──────────────────────────────────────────────────────────────────

def cmd_list():
    runs = load_runs()
    if not runs:
        print("No runs recorded yet.")
        return
    print(f"{'ID':<22}  {'Label':<20}  {'Suites':<12}  {'Duration':>9}")
    print("-" * 70)
    for r in runs:
        suites = ", ".join(r.get("suites_run", [])) or "-"
        dur = f"{r.get('total_duration_sec', 0):.1f}s"
        label = r.get("label") or "-"
        print(f"{r['id']:<22}  {label:<20}  {suites:<12}  {dur:>9}")


def cmd_delete(run_id: str):
    runs = load_runs()
    before = len(runs)
    runs = [r for r in runs if r["id"] != run_id]
    if len(runs) == before:
        print(f"Run '{run_id}' not found in index.")
        return
    save_runs(runs)
    print(f"Removed run '{run_id}' from index (files kept on disk).")


def cmd_run(args):
    ts = datetime.now()
    run_id = ts.strftime("%Y%m%dT%H%M%S%f")
    dir_name = run_id if not args.label else f"{run_id}_{args.label.replace(' ', '-')}"
    run_dir = os.path.join(RESULTS_ROOT, dir_name)
    os.makedirs(run_dir, exist_ok=True)

    print("=" * 70)
    print("  DSR Benchmark Suite (C++ + Python)")
    print(f"  Run ID : {run_id}")
    if args.label:
        print(f"  Label  : {args.label}")
    print(f"  Output : {run_dir}")
    print("=" * 70)

    effective_cpp_filter = args.cpp_filter
    if args.all and not effective_cpp_filter:
        effective_cpp_filter = ALL_CPP_FILTER
    elif args.baseline and not effective_cpp_filter:
        effective_cpp_filter = BASELINE_CPP_FILTER

    # Optionally build C++
    if args.build:
        if not build_cpp():
            print("Build failed — aborting.")
            return 1

    suites_run = []
    results = {}
    total_start = time.time()

    # CPU tuning (Linux only, skipped with --no-cpu-tune or when Python-only)
    cpu_saved = {}
    if not getattr(args, "no_cpu_tune", False) and not args.python_only:
        cpu_saved = setup_cpu_for_benchmarking()

    try:
        # C++ suite
        if not args.python_only:
            binary = find_cpp_binary(args.cpp_binary)
            if binary:
                ok, dur, stability = run_cpp(
                    binary, run_dir, effective_cpp_filter, args.verbose,
                    repeat=args.repeat, priority=args.priority, taskset=args.taskset,
                    stability_warn_pct=args.stability_warn_pct,
                )
                results["cpp"] = {"ok": ok, "duration_sec": dur, "stability": stability}
                suites_run.append("cpp")
            else:
                print("\nWARNING: C++ binary not found. Use --cpp-binary or --build.")
                print(f"  Searched: {os.path.join(BUILD_DIR, 'dsr_benchmarks')}")
                results["cpp"] = {"ok": False, "duration_sec": 0, "skipped": True}

        # Python suite
        if not args.cpp_only:
            ok, dur = run_python(run_dir, args.label, baseline=args.baseline)
            results["python"] = {"ok": ok, "duration_sec": dur}
            suites_run.append("python")

    finally:
        restore_cpu_settings(cpu_saved)

    total_duration = time.time() - total_start

    # Gather git hash
    try:
        git_hash = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=SCRIPT_DIR, stderr=subprocess.DEVNULL,
        ).decode().strip()
    except Exception:
        git_hash = ""

    run_info = {
        "id": run_id,
        "label": args.label or "",
        "dir": dir_name,
        "timestamp": ts.isoformat(),
        "total_duration_sec": round(total_duration, 2),
        "suites_run": suites_run,
        "suites_passed": [s for s in suites_run if results.get(s, {}).get("ok")],
        "git_hash": git_hash,
        "platform": platform.platform(),
        "python": sys.version.split()[0],
    }

    cpp_stability = results.get("cpp", {}).get("stability")
    if cpp_stability:
        run_info["cpp_stability"] = {
            "warn_threshold_pct": cpp_stability.get("warn_threshold_pct"),
            "warning_count": cpp_stability.get("warning_count", 0),
        }

    with open(os.path.join(run_dir, "run_info.json"), "w") as f:
        json.dump(run_info, f, indent=2)

    register_run(run_info)

    # Summary
    print("\n" + "=" * 70)
    print("  Summary")
    print("=" * 70)
    all_ok = True
    for suite in ["cpp", "python"]:
        if suite not in results:
            continue
        r = results[suite]
        if r.get("skipped"):
            print(f"  [SKIP] {suite}")
        else:
            status = "PASS" if r["ok"] else "FAIL"
            print(f"  [{status}] {suite} ({r['duration_sec']:.1f}s)")
            if not r["ok"]:
                all_ok = False

    print(f"\n  Run ID  : {run_id}")
    print(f"  Results : {run_dir}")
    print(f"  Index   : {RUNS_INDEX}")

    # Generate report
    if args.report or args.compare:
        report_args = ["--run", run_id, "--results-root", RESULTS_ROOT]
        if args.compare:
            report_args += ["--baseline", args.compare]
        report_path = os.path.join(run_dir, "report.html")
        report_args += ["--output", report_path]

        print(f"\nGenerating report...")
        subprocess.run(
            [sys.executable, os.path.join(SCRIPT_DIR, "report.py")] + report_args,
            cwd=SCRIPT_DIR,
        )

        if args.open_report and os.path.isfile(report_path):
            import webbrowser
            webbrowser.open(f"file://{report_path}")

    _fix_run_permissions(run_dir)
    return 0 if all_ok else 1


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Run DSR C++ and Python benchmarks together",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--label", "-l", help="Human-readable label for this run")
    parser.add_argument("--cpp-binary", metavar="PATH",
                        help=f"Path to dsr_benchmarks binary (default: {os.path.join(BUILD_DIR, 'dsr_benchmarks')})")
    parser.add_argument("--cpp-filter", metavar="FILTER",
                        help='Catch2 test filter, e.g. "[LATENCY]" or "[THROUGHPUT]"')
    parser.add_argument("--build", action="store_true",
                        help="Build C++ benchmarks before running")
    parser.add_argument("--cpp-only", action="store_true", help="Skip Python suite")
    parser.add_argument("--python-only", action="store_true", help="Skip C++ suite")
    parser.add_argument("--all", action="store_true",
                        help="Run all C++ tests including hidden ones ([.multi], [.extended])")
    parser.add_argument("--baseline", action="store_true",
                        help="Run only the curated low-noise baseline benchmark set")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Pass --verbose to C++ binary (shows Qt debug messages)")
    parser.add_argument("--report", action="store_true",
                        help="Generate HTML report after the run")
    parser.add_argument("--open", dest="open_report", action="store_true",
                        help="Open the HTML report in a browser after generation")
    parser.add_argument("--compare", metavar="RUN_ID",
                        help="Generate a comparison report against this baseline run")
    parser.add_argument("--list", action="store_true", help="List all recorded runs")
    parser.add_argument("--delete", metavar="RUN_ID",
                        help="Remove a run from the index")
    parser.add_argument("--repeat", "-r", type=int, default=1, metavar="N",
                        help="Run C++ benchmarks N times and report the median (reduces OS noise)")
    parser.add_argument("--priority", type=int, default=None, metavar="NICE",
                        help="Set process nice level (e.g. -10); values < 0 require root/sudo")
    parser.add_argument("--taskset", metavar="CPULIST",
                        help="Pin C++ benchmarks to CPU cores via taskset (e.g. '0,1')")
    parser.add_argument("--no-cpu-tune", action="store_true",
                        help="Skip automatic CPU governor/turbo configuration (Linux only)")
    parser.add_argument("--stability-warn-pct", type=float, default=DEFAULT_STABILITY_WARN_PCT,
                        metavar="PCT",
                        help="Warn when repeated C++ metrics exceed this spread percentage")

    args = parser.parse_args()

    if args.list:
        cmd_list()
        return 0

    if args.delete:
        cmd_delete(args.delete)
        return 0

    if args.all and args.baseline:
        print("Error: --all and --baseline are mutually exclusive.")
        return 1

    if args.cpp_only and args.python_only:
        print("Error: --cpp-only and --python-only are mutually exclusive.")
        return 1

    return cmd_run(args)


if __name__ == "__main__":
    sys.exit(main())
