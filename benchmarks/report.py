#!/usr/bin/env python3
"""
Generate a visual HTML report from benchmark results.

Single run:
    python report.py                            # latest run
    python report.py --run 20260314T153000

Compare two runs:
    python report.py --run 20260314T153000 --baseline 20260313T090000

List available runs:
    python report.py --list
"""

import json
import os
import sys
import glob
import argparse
from typing import Optional
from datetime import datetime

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_RESULTS_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "results"))
RUNS_INDEX = os.path.join(DEFAULT_RESULTS_ROOT, "runs.json")


# ── Data loading ──────────────────────────────────────────────────────────────

def load_runs_index() -> list:
    if not os.path.isfile(RUNS_INDEX):
        return []
    with open(RUNS_INDEX) as f:
        return json.load(f)


def load_run_metrics(run_dir: str) -> list:
    """Load all JSON metric files from a run directory.

    Scans two locations:
      - <run_dir>/*.json          Python benchmark output
      - <run_dir>/cpp/results/*.json   C++ benchmark output (written by dsr_benchmarks)
    """
    SKIP = {"run_info.json", "stability_summary.json"}
    search_paths = [
        (run_dir, "*.json"),
        (os.path.join(run_dir, "cpp", "results"), "*.json"),
    ]

    cpp_dir = os.path.join(run_dir, "cpp", "results")
    metrics = []
    for directory, pattern in search_paths:
        lang = "cpp" if os.path.abspath(directory) == os.path.abspath(cpp_dir) else "python"
        for path in sorted(glob.glob(os.path.join(directory, pattern))):
            if os.path.basename(path) in SKIP:
                continue
            try:
                with open(path) as f:
                    data = json.load(f)
                    data["_source_file"] = os.path.basename(path)
                    data["_lang"] = lang
                    metrics.append(data)
            except Exception as e:
                print(f"Warning: could not load {path}: {e}", file=sys.stderr)
    return metrics


def load_run_info(run_dir: str) -> dict:
    path = os.path.join(run_dir, "run_info.json")
    if os.path.isfile(path):
        with open(path) as f:
            return json.load(f)
    return {}


def resolve_run_dir(run_id: str, results_root: str) -> str:
    """Find the directory for a run_id (handles labelled dirs like 20260314T153000_label)."""
    # Direct match
    direct = os.path.join(results_root, run_id)
    if os.path.isdir(direct):
        return direct
    # Prefix match (labelled)
    for entry in os.listdir(results_root):
        if entry.startswith(run_id):
            candidate = os.path.join(results_root, entry)
            if os.path.isdir(candidate):
                return candidate
    # Look up in index
    for r in load_runs_index():
        if r["id"] == run_id:
            candidate = os.path.join(results_root, r["dir"])
            if os.path.isdir(candidate):
                return candidate
    raise FileNotFoundError(f"Run directory not found for id '{run_id}'")


_UNIT_TO_NS = {"ns": 1, "us": 1_000, "µs": 1_000, "ms": 1_000_000, "s": 1_000_000_000}


def _to_ns(value: float, unit: str) -> float:
    return value * _UNIT_TO_NS.get(unit.strip(), 1)


def infer_profile(bench: dict, metric: Optional[dict] = None) -> str:
    metadata = bench.get("metadata", {}) or {}
    meta_profile = str(metadata.get("profile", "")).strip().lower()
    if meta_profile in {"baseline", "extended", "other"}:
        return meta_profile

    tags = (metric or {}).get("tags", {}) or {}
    tag_values = {str(v).upper() for v in tags.values()}
    tag_keys = {str(k).upper() for k in tags.keys()}

    if "BASELINE" in tag_keys or "BASELINE" in tag_values:
        return "baseline"
    if "EXTENDED" in tag_keys or "EXTENDED" in tag_values:
        return "extended"

    source = bench.get("_source_file", "").lower()
    bench_name = bench.get("benchmark_name", "").lower()
    if "baseline" in source or "baseline" in bench_name:
        return "baseline"
    if "extended" in source or "extended" in bench_name:
        return "extended"
    if bench_name.startswith("crdt_") or source.startswith("crdt_"):
        return "baseline"
    return "other"


def flatten_metrics(bench_files: list) -> tuple[list, list]:
    """Return (latency_metrics, throughput_metrics) as flat lists."""
    latency, throughput = [], []
    latency_keys: set = set()   # (bench_name, metric_name) pairs with real latency data
    for bench in bench_files:
        bench_name = bench.get("benchmark_name", bench["_source_file"])
        lang = bench.get("_lang", "python")
        for m in bench.get("metrics", []):
            add = m.get("additional", {})
            tags = m.get("tags", {})
            unit = m.get("unit", "")
            category = m.get("category", "")
            profile = infer_profile(bench, m)

            # For scalability metrics with repeated names, append the tag that
            # differentiates them (e.g. graph_size) so each row is unique.
            metric_name = m["name"]
            if tags:
                tag_suffix = "_".join(f"{k}={v}" for k, v in tags.items()
                                      if k in ("graph_size", "num_threads", "threads", "scale_factor"))
                if tag_suffix:
                    metric_name = f"{metric_name}@{tag_suffix}"

            entry = {
                "benchmark": bench_name,
                "metric": metric_name,
                "lang": lang,
                "profile": profile,
                "value": m["value"],
                "unit": unit,
                "additional": add,
            }

            if category == "latency":
                entry.update({
                    "mean_ns": add.get("mean_ns", m["value"]),
                    "p50_ns": add.get("p50_ns", 0),
                    "p95_ns": add.get("p95_ns", 0),
                    "p99_ns": add.get("p99_ns", 0),
                    "min_ns": add.get("min_ns", 0),
                    "max_ns": add.get("max_ns", 0),
                    "count": int(add.get("count", 0)),
                    "has_percentiles": True,
                })
                latency.append(entry)
                latency_keys.add((bench_name, metric_name))
            elif category == "throughput":
                entry.update({
                    "ops_per_sec": m["value"],
                    "total_ops": add.get("total_operations", 0),
                    "duration_sec": add.get("duration_sec", add.get("duration_ms", 0) / 1000),
                })
                throughput.append(entry)
            elif category == "scalability" and unit in _UNIT_TO_NS:
                # Only promote scalability entries that have no proper latency
                # counterpart — avoids duplicates and preserves percentile data.
                if (bench_name, metric_name) in latency_keys:
                    continue
                mean_ns = _to_ns(m["value"], unit)
                entry.update({
                    "mean_ns": mean_ns,
                    "p50_ns": 0,
                    "p95_ns": 0,
                    "p99_ns": 0,
                    "min_ns": 0,
                    "max_ns": 0,
                    "count": int(add.get("count", 0)),
                    "has_percentiles": False,
                })
                latency.append(entry)
    return latency, throughput


# ── Scalability flattening ────────────────────────────────────────────────────

SCALE_DIMS = ("threads", "graph_size", "agents")


def flatten_scalability(bench_files: list) -> list:
    """Return a flat list of scalability data points.

    Any metric tagged with a recognised scale dimension (threads, graph_size,
    or agents) is included — regardless of category — so latency, throughput,
    and scalability records all contribute.
    """
    rows = []
    for bench in bench_files:
        lang = bench.get("_lang", "python")
        bench_name = bench.get("benchmark_name", bench["_source_file"])
        for m in bench.get("metrics", []):
            tags = m.get("tags", {})
            add = m.get("additional", {})
            scale_dim = next((d for d in SCALE_DIMS if d in tags), None)
            if scale_dim is None:
                continue
            try:
                scale_val = int(tags[scale_dim])
            except (ValueError, KeyError):
                continue
            cat = m.get("category", "")
            rows.append({
                "benchmark": bench_name,
                "operation": m["name"],
                "lang": lang,
                "profile": infer_profile(bench, m),
                "category": cat,
                "scale_dim": scale_dim,
                "scale_val": scale_val,
                "value": m["value"],
                "unit": m.get("unit", ""),
                "mean_ns": add.get("mean_ns", 0.0),
                "p99_ns": add.get("p99_ns", 0.0),
                "ops_per_sec": m["value"] if cat == "throughput" else 0.0,
            })
    return rows


def compute_efficiency(rows: list) -> list:
    """Compute a normalised-performance series for each (benchmark, op, dim).

    threads / agents  →  parallel efficiency = thr_N / (N × thr_1) × 100
    graph_size        →  relative throughput  = thr_N / thr_min × 100
                         (100 % at smallest graph, declining as graph grows)

    Returns a list of {benchmark, operation, scale_dim, scale_val, efficiency,
    ops_per_sec} dicts.  The JS chart uses the same field regardless of which
    formula was applied; the label/title is updated per-dimension in JS.
    """
    from collections import defaultdict

    groups: dict = defaultdict(list)
    for r in rows:
        if r["category"] != "throughput":
            continue
        key = (r["benchmark"], r["operation"], r["scale_dim"])
        groups[key].append(r)

    result = []
    for (bench, op, dim), pts in groups.items():
        pts_sorted = sorted(pts, key=lambda p: p["scale_val"])

        if dim in ("threads", "agents"):
            baseline = next((p for p in pts_sorted if p["scale_val"] == 1), None)
            if baseline is None or baseline["ops_per_sec"] == 0:
                continue
            thr_1 = baseline["ops_per_sec"]
            for p in pts_sorted:
                N = p["scale_val"]
                if N == 0:
                    continue
                efficiency = (p["ops_per_sec"] / (N * thr_1)) * 100.0
                result.append({
                    "benchmark": bench, "operation": op, "scale_dim": dim,
                    "scale_val": N, "efficiency": round(efficiency, 2),
                    "ops_per_sec": p["ops_per_sec"],
                })

        elif dim == "graph_size":
            if not pts_sorted or pts_sorted[0]["ops_per_sec"] == 0:
                continue
            thr_min = pts_sorted[0]["ops_per_sec"]
            for p in pts_sorted:
                relative = (p["ops_per_sec"] / thr_min) * 100.0
                result.append({
                    "benchmark": bench, "operation": op, "scale_dim": dim,
                    "scale_val": p["scale_val"], "efficiency": round(relative, 2),
                    "ops_per_sec": p["ops_per_sec"],
                })

    return result


# ── HTML generation ───────────────────────────────────────────────────────────

def generate_html(
    run_info: dict,
    bench_files: list,
    output_path: str,
    baseline_info: Optional[dict] = None,
    baseline_files: Optional[list] = None,
):
    latency, throughput = flatten_metrics(bench_files)
    b_latency, b_throughput = (flatten_metrics(baseline_files) if baseline_files else ([], []))

    scl_rows = flatten_scalability(bench_files)
    eff_rows = compute_efficiency(scl_rows)
    b_scl_rows = flatten_scalability(baseline_files) if baseline_files else []

    run_id = run_info.get("id", "unknown")
    run_label = run_info.get("label") or run_id
    b_id = baseline_info.get("id", "") if baseline_info else ""
    b_label = (baseline_info.get("label") or b_id) if baseline_info else ""
    comparing = bool(baseline_files)
    generated_at = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    latency_json = json.dumps(latency)
    throughput_json = json.dumps(throughput)
    b_latency_json = json.dumps(b_latency)
    b_throughput_json = json.dumps(b_throughput)
    run_info_json = json.dumps(run_info)
    b_info_json = json.dumps(baseline_info or {})
    scl_json = json.dumps(scl_rows)
    eff_json = json.dumps(eff_rows)
    b_scl_json = json.dumps(b_scl_rows)

    # Summary rows
    summary = []
    for b in bench_files:
        summary.append({
            "benchmark": b.get("benchmark_name", b["_source_file"]),
            "profile": infer_profile(b),
            "timestamp": b.get("timestamp", ""),
            "duration": f"{b.get('total_duration_sec', 0):.1f}s",
            "metrics": len(b.get("metrics", [])),
            "source": b["_source_file"],
        })
    summary_json = json.dumps(summary)

    compare_tab = '<button onclick="showTab(\'compare\', this)">Compare</button>' if comparing else ""
    compare_panel = ""
    if comparing:
        compare_panel = '<div id="tab-compare" class="tab-panel"></div>'

    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Cortex Benchmark Report — {run_label}</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/hammerjs@2.0.8/hammer.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/chartjs-plugin-zoom@2.0.1/dist/chartjs-plugin-zoom.min.js"></script>
<style>
  :root {{
    --bg: #0f1117; --surface: #1a1d27; --border: #2a2d3a;
    --accent: #7c6af7; --accent2: #4fc3f7; --accent3: #81c995; --accent4: #f28b82;
    --text: #e0e0e0; --muted: #8b8fa8;
    --font: 'Segoe UI', system-ui, sans-serif;
    --mono: 'Cascadia Code', 'Consolas', monospace;
  }}
  * {{ box-sizing: border-box; margin: 0; padding: 0; }}
  body {{ background: var(--bg); color: var(--text); font-family: var(--font); }}

  header {{
    border-bottom: 1px solid var(--border);
    padding: 20px 40px;
    background: var(--surface);
    display: flex;
    align-items: baseline;
    gap: 16px;
  }}
  header h1 {{ font-size: 1.35rem; font-weight: 600; }}
  header .run-id {{ color: var(--accent); font-family: var(--mono); font-size: 0.85rem; }}
  header .meta {{ margin-left: auto; color: var(--muted); font-size: 0.8rem; text-align: right; line-height: 1.6; }}

  nav {{
    background: var(--surface); border-bottom: 1px solid var(--border);
    padding: 0 40px; display: flex; gap: 4px;
  }}
  nav button {{
    background: none; border: none; border-bottom: 3px solid transparent;
    color: var(--muted); cursor: pointer; font-family: var(--font);
    font-size: 0.9rem; padding: 12px 16px;
    transition: color 0.15s, border-color 0.15s;
  }}
  nav button:hover {{ color: var(--text); }}
  nav button.active {{ color: var(--accent); border-color: var(--accent); }}

  main {{ padding: 32px 40px; max-width: 1400px; margin: 0 auto; }}
  .tab-panel {{ display: none; }}
  .tab-panel.active {{ display: block; }}

  h2 {{ font-size: 1.1rem; font-weight: 600; margin-bottom: 20px; }}
  h3 {{ font-size: 0.85rem; font-weight: 600; color: var(--muted); text-transform: uppercase;
       letter-spacing: 0.05em; margin-bottom: 12px; }}

  .grid-2 {{ display: grid; grid-template-columns: 1fr 1fr; gap: 24px; }}
  .grid-3 {{ display: grid; grid-template-columns: repeat(3, 1fr); gap: 16px; }}
  @media (max-width: 900px) {{ .grid-2, .grid-3 {{ grid-template-columns: 1fr; }} }}

  .card {{ background: var(--surface); border: 1px solid var(--border); border-radius: 8px; padding: 24px; }}
  .stat-card {{
    background: var(--surface); border: 1px solid var(--border);
    border-radius: 8px; padding: 20px; text-align: center;
  }}
  .stat-card .val {{ font-size: 1.9rem; font-weight: 700; color: var(--accent); font-family: var(--mono); }}
  .stat-card .lbl {{ color: var(--muted); font-size: 0.78rem; margin-top: 4px; }}

  .chart-wrap {{ position: relative; height: 320px; }}
  .chart-wrap.tall {{ height: 420px; }}

  table {{ width: 100%; border-collapse: collapse; font-size: 0.875rem; }}
  th {{
    text-align: left; padding: 10px 14px; color: var(--muted); font-weight: 500;
    border-bottom: 1px solid var(--border); font-size: 0.78rem;
    text-transform: uppercase; letter-spacing: 0.04em;
  }}
  td {{ padding: 10px 14px; border-bottom: 1px solid var(--border); font-family: var(--mono); font-size: 0.85rem; }}
  tr:last-child td {{ border-bottom: none; }}
  tr:hover td {{ background: rgba(124,106,247,0.04); }}

  .badge {{
    display: inline-block; padding: 2px 8px; border-radius: 4px;
    font-size: 0.72rem; font-weight: 600;
  }}
  .badge-latency {{ background: rgba(124,106,247,0.18); color: var(--accent); }}
  .badge-throughput {{ background: rgba(79,195,247,0.18); color: var(--accent2); }}
  .badge-cpp {{ background: rgba(251,146,60,0.18); color: #fb923c; }}
  .badge-python {{ background: rgba(96,165,250,0.18); color: #60a5fa; }}
  .badge-baseline {{ background: rgba(129,201,149,0.18); color: var(--accent3); }}
  .badge-extended {{ background: rgba(255,183,77,0.18); color: #ffb74d; }}
  .badge-other {{ background: rgba(139,143,168,0.18); color: var(--muted); }}
  .lang-toggle {{ display: flex; gap: 0; }}
  .lang-toggle button {{
    background: var(--surface); border: 1px solid var(--border);
    color: var(--muted); cursor: pointer; font-family: var(--font);
    font-size: 0.82rem; padding: 5px 12px; transition: all 0.15s;
  }}
  .lang-toggle button:first-child {{ border-radius: 6px 0 0 6px; }}
  .lang-toggle button:last-child  {{ border-radius: 0 6px 6px 0; border-left: none; }}
  .lang-toggle button:not(:first-child):not(:last-child) {{ border-left: none; }}
  .lang-toggle button.active {{ background: rgba(124,106,247,0.15); color: var(--accent); border-color: var(--accent); }}

  .delta {{ font-weight: 600; }}
  .delta-good {{ color: var(--accent3); }}
  .delta-bad {{ color: var(--accent4); }}
  .delta-neutral {{ color: var(--muted); }}

  .lang-section-title {{
    display: flex; align-items: center; gap: 10px;
    margin: 32px 0 16px; font-size: 1.05rem; font-weight: 600;
    border-bottom: 1px solid var(--border); padding-bottom: 10px;
  }}
  .cmp-chips {{
    display: flex; flex-wrap: wrap; gap: 6px; margin-top: 12px;
  }}
  .cmp-chip {{
    display: inline-flex; align-items: center; gap: 4px;
    padding: 3px 9px; border-radius: 20px; font-size: 0.72rem; font-weight: 500;
    cursor: pointer; border: 1px solid var(--border);
    background: var(--surface); color: var(--text);
    transition: all 0.15s; user-select: none;
  }}
  .cmp-chip:hover {{ border-color: var(--accent); color: var(--accent); }}
  .cmp-chip.hidden-chip {{
    opacity: 0.35; text-decoration: line-through; color: var(--muted);
  }}
  .btn-sm {{
    background: var(--surface); border: 1px solid var(--border); border-radius: 6px;
    color: var(--muted); cursor: pointer; font-family: var(--font);
    font-size: 0.78rem; padding: 4px 10px; transition: all 0.15s;
  }}
  .btn-sm:hover {{ color: var(--text); border-color: var(--accent); }}

  .section {{ margin-bottom: 36px; }}
  .subsection-title {{
    display:flex; align-items:center; justify-content:space-between;
    margin: 20px 0 10px; font-size: 0.95rem; font-weight: 600;
  }}
  .filter-bar {{ display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 14px; }}
  .filter-bar select {{
    background: var(--surface); border: 1px solid var(--border); border-radius: 6px;
    color: var(--text); font-family: var(--font); font-size: 0.85rem; padding: 6px 12px; cursor: pointer;
  }}
  .empty {{ color: var(--muted); font-style: italic; padding: 24px 0; text-align: center; }}

  .run-pill {{
    display: inline-flex; align-items: center; gap: 6px;
    background: var(--surface); border: 1px solid var(--border);
    border-radius: 20px; padding: 4px 12px; font-size: 0.8rem;
  }}
  .run-pill .dot {{ width: 8px; height: 8px; border-radius: 50%; }}
</style>
</head>
<body>

<header>
  <div>
    <h1>Cortex Benchmark Report</h1>
    <span class="run-id">{run_label}</span>
    {f'<span style="color:var(--muted);font-size:0.8rem"> vs baseline: <span style="color:var(--accent2)">{b_label}</span></span>' if comparing else ""}
  </div>
  <div class="meta">
    Generated: {generated_at}<br>
    {run_info.get("git_hash") and f"git: {run_info['git_hash']}" or ""}
  </div>
</header>

<nav>
  <button class="active" onclick="showTab('overview', this)">Overview</button>
  <button onclick="showTab('latency', this)">Latency</button>
  <button onclick="showTab('throughput', this)">Throughput</button>
  <button onclick="showTab('scalability', this)">Scalability</button>
  {compare_tab}
  <button onclick="showTab('raw', this)">Raw Data</button>
</nav>

<main>

<!-- OVERVIEW -->
<div id="tab-overview" class="tab-panel active">
  <div class="section"><div class="grid-3" id="stat-cards"></div></div>
  <div class="section grid-2">
    <div class="card">
      <h3>Latency — Mean (µs)</h3>
      <div class="chart-wrap"><canvas id="ov-latency"></canvas></div>
    </div>
    <div class="card">
      <h3>Throughput (ops/sec)</h3>
      <div class="chart-wrap"><canvas id="ov-throughput"></canvas></div>
    </div>
  </div>
  <div class="section">
    <div class="card">
      <h3>Run Info</h3>
      <table><tbody id="run-info-table"></tbody></table>
    </div>
  </div>
  <div class="section">
    <div class="card">
      <h3>Benchmark Files</h3>
      <table>
        <thead><tr><th>Benchmark</th><th>Timestamp</th><th>Duration</th><th>Metrics</th><th>File</th></tr></thead>
        <tbody id="summary-table"></tbody>
      </table>
    </div>
  </div>
</div>

<!-- LATENCY -->
<div id="tab-latency" class="tab-panel">
  <div class="section">
    <div class="filter-bar">
      <select id="lat-filter" onchange="renderLatency()"><option value="">All benchmarks</option></select>
      <select id="lat-profile-filter" onchange="renderLatency()">
        <option value="">All profiles</option>
        <option value="baseline">Baseline</option>
        <option value="extended">Extended</option>
        <option value="other">Other</option>
      </select>
      <div class="lang-toggle">
        <button class="active" onclick="setLangFilter('lat','',this)">All</button>
        <button onclick="setLangFilter('lat','cpp',this)">C++</button>
        <button onclick="setLangFilter('lat','python',this)">Python</button>
      </div>
      <button onclick="if(latChart) latChart.resetZoom()" style="background:var(--surface);border:1px solid var(--border);border-radius:6px;color:var(--muted);cursor:pointer;font-family:var(--font);font-size:0.85rem;padding:6px 12px;">Reset zoom</button>
    </div>
    <div style="color:var(--muted);font-size:0.78rem;margin-bottom:8px;">Scroll to zoom · Click &amp; drag to pan · Double-click to reset</div>
    <div class="card" style="margin-bottom:24px;">
      <h3>Latency Distribution — Mean / p50 / p95 / p99</h3>
      <div class="chart-wrap tall"><canvas id="lat-dist"></canvas></div>
    </div>
    <div class="card">
      <h3>Latency Detail</h3>
      <div id="lat-detail-sections"></div>
    </div>
  </div>
</div>

<!-- THROUGHPUT -->
<div id="tab-throughput" class="tab-panel">
  <div class="section">
    <div class="filter-bar">
      <select id="thr-filter" onchange="renderThroughput()"><option value="">All benchmarks</option></select>
      <select id="thr-profile-filter" onchange="renderThroughput()">
        <option value="">All profiles</option>
        <option value="baseline">Baseline</option>
        <option value="extended">Extended</option>
        <option value="other">Other</option>
      </select>
      <div class="lang-toggle">
        <button class="active" onclick="setLangFilter('thr','',this)">All</button>
        <button onclick="setLangFilter('thr','cpp',this)">C++</button>
        <button onclick="setLangFilter('thr','python',this)">Python</button>
      </div>
    </div>
    <div class="card" style="margin-bottom:24px;">
      <h3>Operations per Second</h3>
      <div class="chart-wrap tall"><canvas id="thr-bar"></canvas></div>
    </div>
    <div class="card">
      <h3>Throughput Detail</h3>
      <div id="thr-detail-sections"></div>
    </div>
  </div>
</div>

<!-- SCALABILITY -->
<div id="tab-scalability" class="tab-panel">
  <div class="section">
    <div class="filter-bar">
      <select id="scl-dim" onchange="renderScalability()">
        <option value="threads">Threads</option>
        <option value="graph_size">Graph size</option>
        <option value="agents">Agents</option>
      </select>
      <select id="scl-op" onchange="renderScalability()"><option value="">All operations</option></select>
    </div>
    <div class="grid-2 section">
      <div class="card">
        <h3>Throughput (ops/sec)</h3>
        <div class="chart-wrap"><canvas id="scl-thr-chart"></canvas></div>
      </div>
      <div class="card">
        <h3>Mean Latency (µs)</h3>
        <div class="chart-wrap"><canvas id="scl-lat-chart"></canvas></div>
      </div>
    </div>
    <div class="card section">
      <h3 id="scl-eff-title">Scaling Efficiency (% of ideal linear)</h3>
      <div class="chart-wrap"><canvas id="scl-eff-chart"></canvas></div>
    </div>
    <div class="card">
      <h3>Scalability Detail</h3>
      <table>
        <thead><tr>
          <th>Benchmark</th><th>Operation</th><th>Dimension</th><th>Scale</th>
          <th>Throughput</th><th>Mean Latency</th><th>Efficiency %</th>
        </tr></thead>
        <tbody id="scl-table"></tbody>
      </table>
    </div>
  </div>
</div>

<!-- COMPARE (injected if comparing) -->
{compare_panel}

<!-- RAW -->
<div id="tab-raw" class="tab-panel">
  <div class="section" id="raw-content"></div>
</div>

</main>

<script>
// ── Data ─────────────────────────────────────────────────────────────────────
const LAT   = {latency_json};
const THR   = {throughput_json};
const B_LAT = {b_latency_json};
const B_THR = {b_throughput_json};
const RUN_INFO = {run_info_json};
const B_INFO   = {b_info_json};
const SUMMARY  = {summary_json};
const COMPARING = {json.dumps(comparing)};
const SCL = {scl_json};
const EFF = {eff_json};
const B_SCL = {b_scl_json};

const RUN_COLOR  = '#7c6af7';
const BASE_COLOR = '#4fc3f7';
const PALETTE = ['#7c6af7','#4fc3f7','#81c995','#f28b82','#ffb74d','#e879f9','#34d399','#fb923c'];

// ── Formatters ───────────────────────────────────────────────────────────────
function fmtNs(ns) {{
  if (ns >= 1e9) return (ns/1e9).toFixed(2) + ' s';
  if (ns >= 1e6) return (ns/1e6).toFixed(2) + ' ms';
  if (ns >= 1e3) return (ns/1e3).toFixed(2) + ' µs';
  return ns.toFixed(1) + ' ns';
}}
function fmtOps(v) {{
  if (v >= 1e6) return (v/1e6).toFixed(2) + ' M ops/s';
  if (v >= 1e3) return (v/1e3).toFixed(2) + ' K ops/s';
  return v.toFixed(1) + ' ops/s';
}}
function deltaClass(pct, higherIsBetter) {{
  if (Math.abs(pct) < 1) return 'delta-neutral';
  return (pct > 0) === higherIsBetter ? 'delta-good' : 'delta-bad';
}}
function fmtDelta(pct, higherIsBetter) {{
  const sign = pct > 0 ? '+' : '';
  const cls = deltaClass(pct, higherIsBetter);
  return `<span class="delta ${{cls}}">${{sign}}${{pct.toFixed(1)}}%</span>`;
}}
function profileLabel(profile) {{
  if (profile === 'baseline') return 'Baseline';
  if (profile === 'extended') return 'Extended';
  return 'Other';
}}
function profileBadge(profile) {{
  return `<span class="badge badge-${{profile}}">${{profileLabel(profile)}}</span>`;
}}
function profileOrder(profile) {{
  return profile === 'baseline' ? 0 : profile === 'extended' ? 1 : 2;
}}

// ── Chart defaults ────────────────────────────────────────────────────────────
const CD = {{
  responsive: true, maintainAspectRatio: false,
  plugins: {{
    legend: {{ labels: {{ color: '#8b8fa8', font: {{ size: 11 }} }} }},
    tooltip: {{ backgroundColor: '#1a1d27', borderColor: '#2a2d3a', borderWidth: 1,
               titleColor: '#e0e0e0', bodyColor: '#8b8fa8' }},
  }},
  scales: {{
    x: {{ ticks: {{ color: '#8b8fa8', font: {{ size: 11 }} }}, grid: {{ color: '#2a2d3a' }} }},
    y: {{ ticks: {{ color: '#8b8fa8', font: {{ size: 11 }} }}, grid: {{ color: '#2a2d3a' }} }},
  }},
}};

// ── Tab navigation ────────────────────────────────────────────────────────────
let compareRendered = false;
let sclRendered = false;
function showTab(name, btn) {{
  document.querySelectorAll('.tab-panel').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('nav button').forEach(b => b.classList.remove('active'));
  document.getElementById('tab-' + name).classList.add('active');
  if (btn) btn.classList.add('active');
  // Lazy-render compare tab the first time it becomes visible so Chart.js
  // gets correct canvas dimensions (canvas in a hidden div has width=0).
  if (name === 'compare' && COMPARING && !compareRendered) {{
    compareRendered = true;
    renderCompare();
  }}
  // Lazy-render scalability tab the first time it becomes visible.
  if (name === 'scalability' && !sclRendered) {{
    sclRendered = true;
    const sclOpSel = document.getElementById('scl-op');
    [...new Set(SCL.map(r => r.operation))].forEach(op => {{
      const o = document.createElement('option'); o.value = op; o.textContent = op;
      sclOpSel.appendChild(o);
    }});
    renderScalability();
  }}
  // Resize latency/throughput charts when their tabs become visible.
  if (name === 'latency' && latChart) latChart.resize();
  if (name === 'throughput' && thrChart) thrChart.resize();
}}

// ── Overview ──────────────────────────────────────────────────────────────────
function renderOverview() {{
  // Stat cards
  const total = LAT.length + THR.length;
  const benchNames = [...new Set([...LAT.map(m=>m.benchmark), ...THR.map(m=>m.benchmark)])];
  const avgMean = LAT.length ? LAT.reduce((s,m)=>s+m.mean_ns,0)/LAT.length : 0;
  const baselineCount = new Set([...LAT.filter(m=>m.profile==='baseline').map(m=>m.benchmark), ...THR.filter(m=>m.profile==='baseline').map(m=>m.benchmark)]).size;
  const extendedCount = new Set([...LAT.filter(m=>m.profile==='extended').map(m=>m.benchmark), ...THR.filter(m=>m.profile==='extended').map(m=>m.benchmark)]).size;
  document.getElementById('stat-cards').innerHTML = `
    <div class="stat-card"><div class="val">${{benchNames.length}}</div><div class="lbl">Benchmark Suites</div></div>
    <div class="stat-card"><div class="val">${{total}}</div><div class="lbl">Total Metrics</div></div>
    <div class="stat-card"><div class="val">${{LAT.length ? fmtNs(avgMean) : 'N/A'}}</div><div class="lbl">Avg Mean Latency</div></div>
    <div class="stat-card"><div class="val">${{baselineCount}}</div><div class="lbl">Baseline Suites</div></div>
    <div class="stat-card"><div class="val">${{extendedCount}}</div><div class="lbl">Extended Suites</div></div>
  `;

  // Run info table
  const riRows = Object.entries(RUN_INFO)
    .filter(([k]) => !k.startsWith('_'))
    .map(([k,v]) => `<tr><td style="color:var(--muted);width:160px">${{k}}</td><td>${{v}}</td></tr>`)
    .join('');
  document.getElementById('run-info-table').innerHTML = riRows || '<tr><td class="empty">No metadata</td></tr>';

  // Summary table
  document.getElementById('summary-table').innerHTML = SUMMARY.map(r => `
    <tr>
      <td>${{r.benchmark}} ${{profileBadge(r.profile)}}</td><td>${{r.timestamp}}</td>
      <td>${{r.duration}}</td><td>${{r.metrics}}</td>
      <td style="color:var(--muted)">${{r.source}}</td>
    </tr>`).join('');

  // Overview latency chart
  if (LAT.length) {{
    const labels = LAT.map(m => m.metric);
    const datasets = [{{ label: 'Mean (µs)', data: LAT.map(m => m.mean_ns/1000), backgroundColor: RUN_COLOR+'cc', borderColor: RUN_COLOR, borderWidth: 1 }}];
    if (COMPARING && B_LAT.length) {{
      const bMap = Object.fromEntries(B_LAT.map(m => [m.benchmark+'/'+m.metric, m]));
      datasets.push({{ label: 'Baseline Mean (µs)', data: LAT.map(m => (bMap[m.benchmark+'/'+m.metric]?.mean_ns||0)/1000), backgroundColor: BASE_COLOR+'88', borderColor: BASE_COLOR, borderWidth: 1 }});
    }}
    new Chart(document.getElementById('ov-latency'), {{
      type: 'bar', data: {{ labels, datasets }},
      options: {{ ...CD, plugins: {{ ...CD.plugins, tooltip: {{ ...CD.plugins.tooltip,
        callbacks: {{ label: ctx => ` ${{ctx.dataset.label}}: ${{fmtNs(ctx.raw*1000)}}` }} }} }} }},
    }});
  }} else document.getElementById('ov-latency').parentElement.innerHTML = '<p class="empty">No latency data</p>';

  // Overview throughput chart
  if (THR.length) {{
    const labels = THR.map(m => m.metric);
    const datasets = [{{ label: 'Ops/sec', data: THR.map(m => m.ops_per_sec), backgroundColor: PALETTE[1]+'cc', borderColor: PALETTE[1], borderWidth: 1 }}];
    if (COMPARING && B_THR.length) {{
      const bMap = Object.fromEntries(B_THR.map(m => [m.benchmark+'/'+m.metric, m]));
      datasets.push({{ label: 'Baseline', data: THR.map(m => bMap[m.benchmark+'/'+m.metric]?.ops_per_sec||0), backgroundColor: BASE_COLOR+'88', borderColor: BASE_COLOR, borderWidth: 1 }});
    }}
    new Chart(document.getElementById('ov-throughput'), {{
      type: 'bar', data: {{ labels, datasets }},
      options: {{ ...CD, plugins: {{ ...CD.plugins, tooltip: {{ ...CD.plugins.tooltip,
        callbacks: {{ label: ctx => ` ${{fmtOps(ctx.raw)}}` }} }} }} }},
    }});
  }} else document.getElementById('ov-throughput').parentElement.innerHTML += '<p class="empty">No throughput data</p>';
}}

// ── Latency ───────────────────────────────────────────────────────────────────
let latChart = null;
function populateFilter(selId, data) {{
  const sel = document.getElementById(selId);
  [...new Set(data.map(m => m.benchmark))].forEach(n => {{
    const o = document.createElement('option'); o.value = n; o.textContent = n; sel.appendChild(o);
  }});
}}

// ── Lang filter state ─────────────────────────────────────────────────────────
const langFilter = {{ lat: '', thr: '' }};
function setLangFilter(tab, lang, btn) {{
  langFilter[tab] = lang;
  btn.closest('.lang-toggle').querySelectorAll('button').forEach(b => b.classList.remove('active'));
  btn.classList.add('active');
  if (tab === 'lat') renderLatency();
  else renderThroughput();
}}
function langBadge(lang) {{
  return `<span class="badge badge-${{lang}}">${{lang}}</span>`;
}}

function renderLatency() {{
  let data = LAT;
  const benchF = document.getElementById('lat-filter').value;
  const profileF = document.getElementById('lat-profile-filter').value;
  if (benchF) data = data.filter(m => m.benchmark === benchF);
  if (profileF) data = data.filter(m => m.profile === profileF);
  if (langFilter.lat) data = data.filter(m => m.lang === langFilter.lat);
  const bMap = COMPARING ? Object.fromEntries(B_LAT.map(m => [m.benchmark+'/'+m.metric, m])) : {{}};

  const labels = data.map(m => m.metric);
  const toUs = ns => ns/1000;

  if (latChart) {{ latChart.destroy(); latChart = null; }}
  if (data.length === 0) {{
    const otherLang = langFilter.lat === 'python' ? 'C++' : langFilter.lat === 'cpp' ? 'Python' : '';
    const hint = otherLang ? ` Try switching to the ${{otherLang}} filter.` : '';
    const canvas = document.getElementById('lat-dist');
    canvas.style.display = 'none';
    let p = canvas.parentElement.querySelector('.lat-empty-msg');
    if (!p) {{ p = document.createElement('p'); p.className = 'empty lat-empty-msg'; canvas.parentElement.appendChild(p); }}
    p.textContent = `No latency data for current filter.${{hint}}`;
    document.getElementById('lat-detail-sections').innerHTML = '<p class="empty">No data</p>';
    return;
  }}
  // Restore canvas if previously hidden
  const latDistCanvas = document.getElementById('lat-dist');
  latDistCanvas.style.display = '';
  const latEmptyMsg = latDistCanvas.parentElement.querySelector('.lat-empty-msg');
  if (latEmptyMsg) latEmptyMsg.remove();
  const datasets = [
    {{ label: 'Mean',  data: data.map(m=>toUs(m.mean_ns)), backgroundColor: RUN_COLOR+'aa' }},
    {{ label: 'p50',   data: data.map(m=>toUs(m.p50_ns)),  backgroundColor: PALETTE[2]+'aa' }},
    {{ label: 'p95',   data: data.map(m=>toUs(m.p95_ns)),  backgroundColor: PALETTE[1]+'aa' }},
    {{ label: 'p99',   data: data.map(m=>toUs(m.p99_ns)),  backgroundColor: PALETTE[3]+'aa' }},
  ];
  if (COMPARING && B_LAT.length) {{
    datasets.push({{ label: 'Baseline Mean', data: data.map(m=>toUs(bMap[m.benchmark+'/'+m.metric]?.mean_ns||0)), backgroundColor: BASE_COLOR+'55', borderColor: BASE_COLOR, borderWidth: 1, borderDash: [4,2] }});
  }}
  latChart = new Chart(document.getElementById('lat-dist'), {{
    type: 'bar', data: {{ labels, datasets }},
    options: {{ ...CD,
      scales: {{ ...CD.scales, y: {{ ...CD.scales.y, title: {{ display:true, text:'µs', color:'#8b8fa8' }} }} }},
      plugins: {{ ...CD.plugins,
        tooltip: {{ ...CD.plugins.tooltip,
          callbacks: {{ label: ctx => ` ${{ctx.dataset.label}}: ${{fmtNs(ctx.raw*1000)}}` }} }},
        zoom: {{
          pan:  {{ enabled: true, mode: 'xy' }},
          zoom: {{
            wheel:  {{ enabled: true }},
            pinch:  {{ enabled: true }},
            mode:   'xy',
            onZoomComplete: ({{ chart }}) => chart.update('none'),
          }},
          limits: {{ y: {{ min: 0 }} }},
        }},
      }},
    }},
  }});

  const renderLatencyRows = rows => rows.map(m => {{
    const b = bMap[m.benchmark+'/'+m.metric];
    const deltaCell = b ? fmtDelta(((m.mean_ns - b.mean_ns) / b.mean_ns)*100, false) : '';
    const stddev = m.additional?.stddev_ns ?? 0;
    const cv = m.mean_ns > 0 ? (stddev / m.mean_ns) * 100 : 0;
    const cvColor = cv < 10 ? 'var(--accent3)' : cv < 30 ? '#ffb74d' : 'var(--accent4)';
    const cvCell = m.count > 1 && m.has_percentiles ? `<span style="color:${{cvColor}};font-weight:600">${{cv.toFixed(1)}}%</span>` : '—';
    const na = `<span style="color:var(--muted)">—</span>`;
    const p50  = m.has_percentiles ? fmtNs(m.p50_ns) : na;
    const p95  = m.has_percentiles ? fmtNs(m.p95_ns) : na;
    const p99  = m.has_percentiles ? `<span style="color:var(--accent4)">${{fmtNs(m.p99_ns)}}</span>` : na;
    const pmin = m.has_percentiles ? fmtNs(m.min_ns) : na;
    const pmax = m.has_percentiles ? fmtNs(m.max_ns) : na;
    return `<tr>
      <td>${{langBadge(m.lang)}}</td>
      <td><span class="badge badge-latency">${{m.benchmark}}</span></td>
      <td>${{m.metric}}</td>
      <td style="color:var(--muted)">${{m.count > 0 ? m.count.toLocaleString() : na}}</td>
      <td>${{fmtNs(m.mean_ns)}}${{deltaCell ? ' ' + deltaCell : ''}}</td>
      <td>${{p50}}</td>
      <td>${{p95}}</td>
      <td>${{p99}}</td>
      <td style="color:var(--muted)">${{pmin}}</td>
      <td style="color:var(--muted)">${{pmax}}</td>
      <td>${{cvCell}}</td>
    </tr>`;
  }}).join('');

  const grouped = ['baseline', 'extended', 'other'].map(profile => {{
    const rows = data.filter(m => m.profile === profile);
    if (!rows.length) return '';
    return `
      <div class="subsection-title">
        <span>${{profileLabel(profile)}} ${{profileBadge(profile)}}</span>
        <span style="color:var(--muted);font-size:0.78rem">${{rows.length}} metric(s)</span>
      </div>
      <table style="margin-bottom:18px;">
        <thead><tr>
          <th>Lang</th><th>Benchmark</th><th>Operation</th><th>n</th>
          <th>Mean</th><th>p50</th><th>p95</th><th>p99</th><th>Min</th><th>Max</th>
          <th title="Coefficient of Variation = stddev/mean. Green &lt;10%, Yellow 10-30%, Red &gt;30%">CV%</th>
        </tr></thead>
        <tbody>${{renderLatencyRows(rows) || '<tr><td colspan="11" class="empty">No data</td></tr>'}}</tbody>
      </table>`;
  }}).join('');
  document.getElementById('lat-detail-sections').innerHTML = grouped || '<p class="empty">No data</p>';
}}

// ── Throughput ────────────────────────────────────────────────────────────────
let thrChart = null;
function renderThroughput() {{
  let data = THR;
  const benchF = document.getElementById('thr-filter').value;
  const profileF = document.getElementById('thr-profile-filter').value;
  if (benchF) data = data.filter(m => m.benchmark === benchF);
  if (profileF) data = data.filter(m => m.profile === profileF);
  if (langFilter.thr) data = data.filter(m => m.lang === langFilter.thr);
  const bMap = COMPARING ? Object.fromEntries(B_THR.map(m => [m.benchmark+'/'+m.metric, m])) : {{}};

  if (thrChart) thrChart.destroy();
  const datasets = [{{ label: 'Ops/sec', data: data.map(m=>m.ops_per_sec), backgroundColor: PALETTE[1]+'cc', borderColor: PALETTE[1], borderWidth: 1 }}];
  if (COMPARING && B_THR.length) {{
    datasets.push({{ label: 'Baseline', data: data.map(m=>bMap[m.benchmark+'/'+m.metric]?.ops_per_sec||0), backgroundColor: BASE_COLOR+'55', borderColor: BASE_COLOR, borderWidth: 1 }});
  }}
  thrChart = new Chart(document.getElementById('thr-bar'), {{
    type: 'bar', data: {{ labels: data.map(m=>m.metric), datasets }},
    options: {{ ...CD, indexAxis: 'y',
      scales: {{ ...CD.scales, x: {{ ...CD.scales.x, title: {{ display:true, text:'ops/sec', color:'#8b8fa8' }} }} }},
      plugins: {{ ...CD.plugins, tooltip: {{ ...CD.plugins.tooltip,
        callbacks: {{ label: ctx => ' ' + fmtOps(ctx.raw) }} }} }},
    }},
  }});

  const renderThroughputRows = rows => rows.map(m => {{
    const b = bMap[m.benchmark+'/'+m.metric];
    const deltaCell = b ? fmtDelta(((m.ops_per_sec - b.ops_per_sec) / b.ops_per_sec)*100, true) : '';
    return `<tr>
      <td>${{langBadge(m.lang)}}</td>
      <td><span class="badge badge-throughput">${{m.benchmark}}</span> ${{profileBadge(m.profile)}}</td>
      <td>${{m.metric}}</td>
      <td style="color:var(--accent2)">${{fmtOps(m.ops_per_sec)}}${{deltaCell ? ' ' + deltaCell : ''}}</td>
      <td style="color:var(--muted)">${{m.total_ops.toLocaleString()}}</td>
      <td style="color:var(--muted)">${{m.duration_sec.toFixed(2)}}s</td>
    </tr>`;
  }}).join('');

  const grouped = ['baseline', 'extended', 'other'].map(profile => {{
    const rows = data.filter(m => m.profile === profile);
    if (!rows.length) return '';
    return `
      <div class="subsection-title">
        <span>${{profileLabel(profile)}} ${{profileBadge(profile)}}</span>
        <span style="color:var(--muted);font-size:0.78rem">${{rows.length}} metric(s)</span>
      </div>
      <table style="margin-bottom:18px;">
        <thead><tr>
          <th>Lang</th><th>Benchmark</th><th>Operation</th><th>Ops/sec</th><th>Total Ops</th><th>Duration</th>
        </tr></thead>
        <tbody>${{renderThroughputRows(rows) || '<tr><td colspan="6" class="empty">No data</td></tr>'}}</tbody>
      </table>`;
  }}).join('');
  document.getElementById('thr-detail-sections').innerHTML = grouped || '<p class="empty">No data</p>';
}}

// ── Scalability Tab ───────────────────────────────────────────────────────────
let sclThrChart = null, sclLatChart = null, sclEffChart = null;

// Show or hide an empty-state message on a canvas card.
// Hides/shows the <canvas> and adds/removes a sibling <p class="empty">.
function sclShowEmpty(canvasId, isEmpty, msg) {{
  const canvas = document.getElementById(canvasId);
  if (!canvas) return;
  canvas.style.display = isEmpty ? 'none' : '';
  let p = canvas.parentElement.querySelector('.scl-empty-msg');
  if (isEmpty) {{
    if (!p) {{ p = document.createElement('p'); p.className = 'empty scl-empty-msg'; canvas.parentElement.appendChild(p); }}
    p.innerHTML = msg;
  }} else {{
    if (p) p.remove();
  }}
}}

function renderScalability() {{
  const dim    = document.getElementById('scl-dim').value;
  const opSel  = document.getElementById('scl-op').value;

  let rows = SCL.filter(r => r.scale_dim === dim);
  if (opSel) rows = rows.filter(r => r.operation === opSel);

  // Baseline rows for this dimension (only relevant when COMPARING)
  let bRows = COMPARING ? B_SCL.filter(r => r.scale_dim === dim) : [];
  if (opSel) bRows = bRows.filter(r => r.operation === opSel);

  const ops = [...new Set(rows.map(r => r.operation))];

  // ── Empty state ───────────────────────────────────────────────────────────
  const noDataMsg = dim === 'agents'
    ? 'No agent-scaling data — run with <code>[SCALABILITY][agents]</code> filter to generate it.'
    : `No scalability data for dimension: <strong>${{dim}}</strong>`;

  // ── Throughput line chart ──────────────────────────────────────────────────
  const thrData = ops.map((op, i) => {{
    const pts = rows.filter(r => r.operation === op && r.category === 'throughput')
                    .sort((a, b) => a.scale_val - b.scale_val);
    return {{
      label: op,
      data: pts.map(p => ({{x: p.scale_val, y: p.ops_per_sec}})),
      borderColor: PALETTE[i % PALETTE.length],
      backgroundColor: PALETTE[i % PALETTE.length] + '33',
      tension: 0.3, parsing: false,
    }};
  }}).filter(ds => ds.data.length > 0);

  // Add dashed baseline series when comparing
  if (COMPARING && bRows.length) {{
    const bOps = [...new Set(bRows.map(r => r.operation))];
    bOps.forEach((op, i) => {{
      const pts = bRows.filter(r => r.operation === op && r.category === 'throughput')
                       .sort((a, b) => a.scale_val - b.scale_val);
      if (pts.length === 0) return;
      thrData.push({{
        label: op + ' (baseline)',
        data: pts.map(p => ({{x: p.scale_val, y: p.ops_per_sec}})),
        borderColor: PALETTE[i % PALETTE.length] + '88',
        backgroundColor: 'transparent',
        borderDash: [5, 3],
        tension: 0.3, parsing: false,
      }});
    }});
  }}

  if (sclThrChart) {{ sclThrChart.destroy(); sclThrChart = null; }}
  sclShowEmpty('scl-thr-chart', thrData.length === 0, noDataMsg);
  if (thrData.length > 0) {{
    sclThrChart = new Chart(document.getElementById('scl-thr-chart'), {{
      type: 'line', data: {{ datasets: thrData }},
      options: {{ ...CD,
        scales: {{ ...CD.scales,
          x: {{ ...CD.scales.x, type: 'linear', title: {{ display: true, text: dim, color: '#8b8fa8' }} }},
          y: {{ ...CD.scales.y, title: {{ display: true, text: 'ops/sec', color: '#8b8fa8' }} }},
        }},
        plugins: {{ ...CD.plugins,
          tooltip: {{ ...CD.plugins.tooltip,
            callbacks: {{ label: ctx => ` ${{ctx.dataset.label}}: ${{fmtOps(ctx.parsed.y)}}` }} }},
        }},
      }},
    }});
  }}

  // ── Latency line chart ────────────────────────────────────────────────────
  const latData = ops.map((op, i) => {{
    const pts = rows.filter(r => r.operation === op && r.category === 'latency')
                    .sort((a, b) => a.scale_val - b.scale_val);
    return {{
      label: op,
      data: pts.map(p => ({{x: p.scale_val, y: p.mean_ns / 1000}})),
      borderColor: PALETTE[i % PALETTE.length],
      backgroundColor: PALETTE[i % PALETTE.length] + '33',
      tension: 0.3, parsing: false,
    }};
  }}).filter(ds => ds.data.length > 0);

  // Add dashed baseline latency series when comparing
  if (COMPARING && bRows.length) {{
    const bOps = [...new Set(bRows.map(r => r.operation))];
    bOps.forEach((op, i) => {{
      const pts = bRows.filter(r => r.operation === op && r.category === 'latency')
                       .sort((a, b) => a.scale_val - b.scale_val);
      if (pts.length === 0) return;
      latData.push({{
        label: op + ' (baseline)',
        data: pts.map(p => ({{x: p.scale_val, y: p.mean_ns / 1000}})),
        borderColor: PALETTE[i % PALETTE.length] + '88',
        backgroundColor: 'transparent',
        borderDash: [5, 3],
        tension: 0.3, parsing: false,
      }});
    }});
  }}

  if (sclLatChart) {{ sclLatChart.destroy(); sclLatChart = null; }}
  sclShowEmpty('scl-lat-chart', latData.length === 0, noDataMsg);
  if (latData.length > 0) {{
    sclLatChart = new Chart(document.getElementById('scl-lat-chart'), {{
      type: 'line', data: {{ datasets: latData }},
      options: {{ ...CD,
        scales: {{ ...CD.scales,
          x: {{ ...CD.scales.x, type: 'linear', title: {{ display: true, text: dim, color: '#8b8fa8' }} }},
          y: {{ ...CD.scales.y, title: {{ display: true, text: 'µs', color: '#8b8fa8' }} }},
        }},
        plugins: {{ ...CD.plugins,
          tooltip: {{ ...CD.plugins.tooltip,
            callbacks: {{ label: ctx => ` ${{ctx.dataset.label}}: ${{fmtNs(ctx.parsed.y * 1000)}}` }} }},
        }},
      }},
    }});
  }}

  // ── Efficiency line chart ─────────────────────────────────────────────────
  // Update title based on dimension
  const effTitleEl = document.getElementById('scl-eff-title');
  if (effTitleEl) {{
    effTitleEl.textContent = dim === 'graph_size'
      ? 'Relative Throughput (% vs smallest graph)'
      : 'Scaling Efficiency (% of ideal linear)';
  }}

  let effRows = EFF.filter(r => r.scale_dim === dim);
  if (opSel) effRows = effRows.filter(r => r.operation === opSel);
  const effOps = [...new Set(effRows.map(r => r.operation))];

  const effData = effOps.map((op, i) => {{
    const pts = effRows.filter(r => r.operation === op)
                       .sort((a, b) => a.scale_val - b.scale_val);
    return {{
      label: op,
      data: pts.map(p => ({{x: p.scale_val, y: p.efficiency}})),
      borderColor: PALETTE[i % PALETTE.length],
      backgroundColor: 'transparent',
      tension: 0.3, parsing: false,
    }};
  }});

  // Add ideal 100% reference line
  const allScales = [...new Set(effRows.map(r => r.scale_val))].sort((a,b)=>a-b);
  if (allScales.length >= 2) {{
    effData.push({{
      label: dim === 'graph_size' ? 'Reference (100%)' : 'Ideal linear (100%)',
      data: allScales.map(x => ({{x, y: 100}})),
      borderColor: '#8b8fa855',
      backgroundColor: 'transparent',
      borderDash: [6, 3],
      pointRadius: 0,
      tension: 0, parsing: false,
    }});
  }}

  if (sclEffChart) {{ sclEffChart.destroy(); sclEffChart = null; }}
  sclShowEmpty('scl-eff-chart', effData.length === 0, noDataMsg);
  if (effData.length > 0) {{
    sclEffChart = new Chart(document.getElementById('scl-eff-chart'), {{
      type: 'line', data: {{ datasets: effData }},
      options: {{ ...CD,
        scales: {{ ...CD.scales,
          x: {{ ...CD.scales.x, type: 'linear', title: {{ display: true, text: dim, color: '#8b8fa8' }} }},
          y: {{ ...CD.scales.y,
            title: {{ display: true, text: 'Efficiency %', color: '#8b8fa8' }},
            ticks: {{ callback: v => v + '%', color: '#8b8fa8', font: {{size: 11}} }},
          }},
        }},
        plugins: {{ ...CD.plugins,
          tooltip: {{ ...CD.plugins.tooltip,
            callbacks: {{ label: ctx => ` ${{ctx.dataset.label}}: ${{ctx.parsed.y.toFixed(1)}}%` }} }},
        }},
      }},
    }});
  }}

  // ── Detail table ──────────────────────────────────────────────────────────
  const effMap = {{}};
  EFF.forEach(r => {{ effMap[r.operation + '/' + r.scale_dim + '/' + r.scale_val] = r.efficiency; }});

  const tableRows = rows.map(r => {{
    const effKey = r.operation + '/' + r.scale_dim + '/' + r.scale_val;
    const eff = effMap[effKey];
    const effCell = eff !== undefined ? eff.toFixed(1) + '%' : '—';
    const thrCell = r.category === 'throughput' ? fmtOps(r.ops_per_sec) : '—';
    const latCell = r.mean_ns > 0 ? fmtNs(r.mean_ns) : '—';
    return `<tr>
      <td style="color:var(--muted)">${{r.benchmark}}</td>
      <td>${{r.operation}}</td>
      <td style="color:var(--accent2)">${{r.scale_dim}}</td>
      <td style="font-weight:600">${{r.scale_val}}</td>
      <td style="color:var(--accent2)">${{thrCell}}</td>
      <td>${{latCell}}</td>
      <td style="color:var(--accent3)">${{effCell}}</td>
    </tr>`;
  }}).join('') || `<tr><td colspan="7" class="empty">${{noDataMsg}}</td></tr>`;

  document.getElementById('scl-table').innerHTML = tableRows;
}}

// ── Compare Tab ───────────────────────────────────────────────────────────────
const cmpHidden = {{ python: new Set(), cpp: new Set() }};
const cmpCharts = {{ python: null, cpp: null }};

function cmpToggle(lang, key) {{
  const h = cmpHidden[lang];
  if (h.has(key)) h.delete(key); else h.add(key);
  updateCmpChart(lang);
}}

function cmpShowAll(lang) {{
  cmpHidden[lang].clear();
  updateCmpChart(lang);
}}

function updateCmpChart(lang) {{
  const chart = cmpCharts[lang];
  if (!chart) return;
  const h = cmpHidden[lang];
  chart.data.datasets[0].data = chart._cmpMeta.map(d => h.has(d.key) ? null : d.pct);
  chart.data.datasets[0].backgroundColor = chart._cmpMeta.map(d => {{
    if (h.has(d.key)) return 'transparent';
    return d.pct > 5 ? '#f28b8288' : d.pct < -5 ? '#81c99588' : '#8b8fa855';
  }});
  chart.update('none');
  document.querySelectorAll(`#cmp-chips-${{lang}} .cmp-chip`).forEach(chip => {{
    chip.classList.toggle('hidden-chip', h.has(chip.dataset.key));
  }});
}}

function buildCmpDeltaChart(lang, items) {{
  const canvasId = `cmp-delta-${{lang}}`;
  const canvas = document.getElementById(canvasId);
  if (!canvas || !items.length) return;
  if (cmpCharts[lang]) cmpCharts[lang].destroy();
  const h = cmpHidden[lang];
  const labels = items.map(d => d.label);
  const data   = items.map(d => h.has(d.key) ? null : d.pct);
  const colors = items.map(d => h.has(d.key) ? 'transparent' : (d.pct > 5 ? '#f28b8288' : d.pct < -5 ? '#81c99588' : '#8b8fa855'));
  const chart = new Chart(canvas, {{
    type: 'bar',
    data: {{ labels, datasets: [{{ label: 'Δ Mean Latency (%)', data, backgroundColor: colors, borderWidth: 0 }}] }},
    options: {{ ...CD, indexAxis: 'y',
      scales: {{ ...CD.scales,
        x: {{ ...CD.scales.x,
          title: {{ display:true, text:'% change (positive = slower)', color:'#8b8fa8' }},
          ticks: {{ callback: v => v + '%', color:'#8b8fa8', font:{{size:11}} }},
        }},
        y: {{ ...CD.scales.y, ticks: {{ ...CD.scales.y.ticks, font:{{size:11}} }} }},
      }},
      plugins: {{ ...CD.plugins,
        tooltip: {{ ...CD.plugins.tooltip, callbacks: {{ label: ctx => ctx.raw !== null ? ` ${{ctx.raw.toFixed(1)}}%` : ' (hidden)' }} }},
      }},
      onClick: (evt) => {{
        const pts = chart.getElementsAtEventForMode(evt, 'index', {{intersect: false}}, false);
        if (pts.length) cmpToggle(lang, items[pts[0].index].key);
      }},
    }},
  }});
  chart._cmpMeta = items;
  cmpCharts[lang] = chart;

  // Render chips
  const chipsEl = document.getElementById(`cmp-chips-${{lang}}`);
  if (chipsEl) {{
    chipsEl.innerHTML = items.map(d => {{
      const col = d.pct > 5 ? '#f28b82' : d.pct < -5 ? '#81c995' : '#8b8fa8';
      return `<span class="cmp-chip" data-key="${{d.key}}" onclick="cmpToggle('${{lang}}', '${{d.key}}')">`
           + `<span style="width:8px;height:8px;border-radius:50%;background:${{col}};display:inline-block"></span>`
           + `${{d.label}}</span>`;
    }}).join('');
  }}
}}

function buildLangSection(lang, lat, bLatMap, thr, bThrMap) {{
  const langLat = lat.filter(m => m.lang === lang);
  const langThr = thr.filter(m => m.lang === lang);
  if (!langLat.length && !langThr.length) return {{ html: '', items: [] }};

  const badgeCls  = lang === 'cpp' ? 'badge-cpp' : 'badge-python';
  const langLabel = lang === 'cpp' ? 'C++' : 'Python';

  // Deduplicate for chart (first-seen per key)
  const seenChart = new Set(), chartItems = [];
  for (const m of langLat) {{
    const key = m.benchmark + '/' + m.metric;
    const b = bLatMap[key];
    if (b && !seenChart.has(key)) {{
      seenChart.add(key);
      const pct = ((m.mean_ns - b.mean_ns) / b.mean_ns) * 100;
      chartItems.push({{ key, label: m.metric + '  [' + m.benchmark + ']', pct }});
    }}
  }}

  const chartH = Math.max(240, chartItems.length * 26);
  const chartSection = chartItems.length ? `
    <div class="card section">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;">
        <h3>Latency Change vs Baseline (%)</h3>
        <button class="btn-sm" onclick="cmpShowAll('${{lang}}')">Show All</button>
      </div>
      <p style="font-size:0.75rem;color:var(--muted);margin-bottom:10px;">Click a bar or chip to hide/show it</p>
      <div class="chart-wrap" style="height:${{chartH}}px"><canvas id="cmp-delta-${{lang}}"></canvas></div>
      <div id="cmp-chips-${{lang}}" class="cmp-chips"></div>
    </div>` : '';

  const latRows = langLat.map(m => {{
    const b = bLatMap[m.benchmark+'/'+m.metric];
    if (!b) return `<tr><td><span class="badge badge-latency">${{m.benchmark}}</span></td><td>${{m.metric}}</td><td colspan="4" style="color:var(--muted)">no baseline</td></tr>`;
    const pct    = ((m.mean_ns - b.mean_ns) / b.mean_ns) * 100;
    const p99pct = (m.has_percentiles && b.has_percentiles && b.p99_ns) ? ((m.p99_ns - b.p99_ns) / b.p99_ns) * 100 : null;
    return `<tr>
      <td><span class="badge badge-latency">${{m.benchmark}}</span></td>
      <td>${{m.metric}}</td>
      <td>${{fmtNs(b.mean_ns)}}</td>
      <td>${{fmtNs(m.mean_ns)}}</td>
      <td>${{fmtDelta(pct, false)}}</td>
      <td>${{p99pct !== null ? fmtDelta(p99pct, false) : '<span class="delta delta-neutral">—</span>'}}</td>
    </tr>`;
  }}).join('');

  const thrRows = langThr.map(m => {{
    const b = bThrMap[m.benchmark+'/'+m.metric];
    if (!b) return `<tr><td><span class="badge badge-throughput">${{m.benchmark}}</span></td><td>${{m.metric}}</td><td colspan="3" style="color:var(--muted)">no baseline</td></tr>`;
    const pct = ((m.ops_per_sec - b.ops_per_sec) / b.ops_per_sec) * 100;
    return `<tr>
      <td><span class="badge badge-throughput">${{m.benchmark}}</span></td>
      <td>${{m.metric}}</td>
      <td style="color:var(--muted)">${{fmtOps(b.ops_per_sec)}}</td>
      <td style="color:var(--accent2)">${{fmtOps(m.ops_per_sec)}}</td>
      <td>${{fmtDelta(pct, true)}}</td>
    </tr>`;
  }}).join('');

  const latSection = langLat.length ? `
    <div class="card section">
      <h3>Latency Comparison</h3>
      <table>
        <thead><tr>
          <th>Benchmark</th><th>Operation</th>
          <th>Baseline Mean</th><th>Current Mean</th><th>Δ Mean</th><th>Δ p99</th>
        </tr></thead>
        <tbody>${{latRows}}</tbody>
      </table>
    </div>` : '';

  const thrSection = langThr.length ? `
    <div class="card section">
      <h3>Throughput Comparison</h3>
      <table>
        <thead><tr>
          <th>Benchmark</th><th>Operation</th>
          <th>Baseline</th><th>Current</th><th>Δ</th>
        </tr></thead>
        <tbody>${{thrRows}}</tbody>
      </table>
    </div>` : '';

  const html = `
    <div class="lang-section-title">
      <span class="badge ${{badgeCls}}" style="font-size:0.88rem;padding:4px 14px;">${{langLabel}}</span>
    </div>
    ${{chartSection}}
    ${{latSection}}
    ${{thrSection}}`;

  return {{ html, items: chartItems }};
}}

function renderCompare() {{
  const el = document.getElementById('tab-compare');
  if (!el || !COMPARING) return;

  const bLatMap = Object.fromEntries(B_LAT.map(m => [m.benchmark+'/'+m.metric, m]));
  const bThrMap = Object.fromEntries(B_THR.map(m => [m.benchmark+'/'+m.metric, m]));

  const pySection  = buildLangSection('python', LAT, bLatMap, THR, bThrMap);
  const cppSection = buildLangSection('cpp',    LAT, bLatMap, THR, bThrMap);

  el.innerHTML = `
    <div class="section">
      <div style="display:flex;gap:16px;align-items:center;margin-bottom:20px;flex-wrap:wrap;">
        <div class="run-pill"><span class="dot" style="background:${{RUN_COLOR}}"></span>{run_label}</div>
        <span style="color:var(--muted)">vs baseline</span>
        <div class="run-pill"><span class="dot" style="background:${{BASE_COLOR}}"></span>{b_label}</div>
      </div>
      ${{pySection.html}}
      ${{cppSection.html}}
    </div>`;

  buildCmpDeltaChart('python', pySection.items);
  buildCmpDeltaChart('cpp',    cppSection.items);
}}

// ── Raw ───────────────────────────────────────────────────────────────────────
function renderRaw() {{
  const all = [...LAT.map(m=>({{'type':'latency',...m}})), ...THR.map(m=>({{'type':'throughput',...m}}))];
  const byProfile = ['baseline', 'extended', 'other'].sort((a,b)=>profileOrder(a)-profileOrder(b)).map(profile => {{
    const rows = all.filter(m => m.profile === profile);
    if (!rows.length) return '';
    return `
      <div class="card" style="margin-bottom:18px;">
        <div class="subsection-title" style="margin-top:0;">
          <span>${{profileLabel(profile)}} ${{profileBadge(profile)}}</span>
          <span style="color:var(--muted);font-size:0.78rem">${{rows.length}} metric(s)</span>
        </div>
        <pre style="overflow:auto;font-family:var(--mono);font-size:0.78rem;color:var(--muted);max-height:600px;">${{JSON.stringify(rows, null, 2)}}</pre>
      </div>`;
  }}).join('');
  document.getElementById('raw-content').innerHTML = byProfile || `
    <div class="card">
      <p class="empty">No data</p>
    </div>`;
}}

// ── Init ──────────────────────────────────────────────────────────────────────
populateFilter('lat-filter', LAT);
populateFilter('thr-filter', THR);
// scl-op is populated lazily on first tab activation (needs SCL data)
renderOverview();
renderLatency();
renderThroughput();
renderRaw();
</script>
</body>
</html>
"""

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(html)
    print(f"Report written to: {os.path.abspath(output_path)}")


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Generate visual HTML benchmark report")
    parser.add_argument("--run", "-r", help="Run ID to report on (default: latest)")
    parser.add_argument("--baseline", "-b", help="Run ID to compare against")
    parser.add_argument("--results-root", default=DEFAULT_RESULTS_ROOT)
    parser.add_argument("--output", "-o", help="Output HTML file (default: <run_dir>/report.html)")
    parser.add_argument("--list", action="store_true", help="List available runs")
    args = parser.parse_args()

    runs = load_runs_index()

    if args.list:
        if not runs:
            print("No runs recorded. Run 'python run_all.py' first.")
            return
        print(f"{'ID':<22}  {'Label':<20}  Dir")
        print("-" * 70)
        for r in runs:
            print(f"{r['id']:<22}  {(r.get('label') or '-'):<20}  {r['dir']}")
        return

    # Resolve target run
    if args.run:
        run_dir = resolve_run_dir(args.run, args.results_root)
    elif runs:
        # Latest run
        latest = runs[-1]
        run_dir = os.path.join(args.results_root, latest["dir"])
        print(f"Using latest run: {latest['id']}")
    else:
        # Fallback: flat results directory (old layout)
        run_dir = args.results_root
        print(f"No runs index found, reading from: {run_dir}")

    run_info = load_run_info(run_dir)
    bench_files = load_run_metrics(run_dir)
    if not bench_files:
        print(f"No metric JSON files found in: {run_dir}", file=sys.stderr)
        sys.exit(1)
    print(f"Loaded {len(bench_files)} metric file(s) from run '{run_info.get('id', run_dir)}'")

    # Resolve baseline
    baseline_info, baseline_files = None, None
    if args.baseline:
        b_dir = resolve_run_dir(args.baseline, args.results_root)
        baseline_info = load_run_info(b_dir)
        baseline_files = load_run_metrics(b_dir)
        print(f"Baseline: {len(baseline_files)} file(s) from run '{baseline_info.get('id', b_dir)}'")

    output_path = args.output or os.path.join(run_dir, "report.html")
    generate_html(run_info, bench_files, output_path, baseline_info, baseline_files)


if __name__ == "__main__":
    main()
