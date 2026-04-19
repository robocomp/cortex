"""
Utility functions for DSR Python benchmarks.
"""

import time
import statistics
import json
import csv
import os
from dataclasses import dataclass, field
from typing import Callable, List, Dict, Any, Optional
from contextlib import contextmanager


@dataclass
class LatencyStats:
    """Statistics from latency measurements."""
    count: int = 0
    mean_ns: float = 0.0
    stddev_ns: float = 0.0
    min_ns: float = 0.0
    max_ns: float = 0.0
    p50_ns: float = 0.0
    p90_ns: float = 0.0
    p95_ns: float = 0.0
    p99_ns: float = 0.0

    @property
    def mean_us(self) -> float:
        return self.mean_ns / 1000.0

    @property
    def mean_ms(self) -> float:
        return self.mean_ns / 1_000_000.0

    @property
    def p99_us(self) -> float:
        return self.p99_ns / 1000.0

    @property
    def p99_ms(self) -> float:
        return self.p99_ns / 1_000_000.0


class LatencyTracker:
    """Collects latency samples and computes statistics."""

    def __init__(self, expected_samples: int = 100):
        self.samples: List[float] = []

    def record(self, latency_ns: float):
        """Record a latency sample in nanoseconds."""
        self.samples.append(latency_ns)

    def record_seconds(self, latency_sec: float):
        """Record a latency sample in seconds."""
        self.samples.append(latency_sec * 1_000_000_000)

    @contextmanager
    def measure(self):
        """Context manager for measuring latency."""
        start = time.perf_counter_ns()
        yield
        self.samples.append(time.perf_counter_ns() - start)

    def clear(self):
        self.samples.clear()

    @property
    def count(self) -> int:
        return len(self.samples)

    def stats(self) -> LatencyStats:
        """Compute and return statistics."""
        if not self.samples:
            return LatencyStats()

        sorted_samples = sorted(self.samples)
        n = len(sorted_samples)

        def percentile(p: float) -> float:
            idx = p * (n - 1)
            lower = int(idx)
            upper = min(lower + 1, n - 1)
            frac = idx - lower
            return sorted_samples[lower] * (1 - frac) + sorted_samples[upper] * frac

        return LatencyStats(
            count=n,
            mean_ns=statistics.mean(sorted_samples),
            stddev_ns=statistics.stdev(sorted_samples) if n > 1 else 0.0,
            min_ns=sorted_samples[0],
            max_ns=sorted_samples[-1],
            p50_ns=percentile(0.50),
            p90_ns=percentile(0.90),
            p95_ns=percentile(0.95),
            p99_ns=percentile(0.99),
        )


@dataclass
class Metric:
    """Individual metric measurement."""
    name: str
    category: str
    value: float
    unit: str = ""
    additional: Dict[str, float] = field(default_factory=dict)
    tags: Dict[str, str] = field(default_factory=dict)


class MetricsCollector:
    """Collects benchmark metrics."""

    def __init__(self, benchmark_name: str = ""):
        self.benchmark_name = benchmark_name
        self.metrics: List[Metric] = []
        self.metadata: Dict[str, str] = {}
        self.start_time = time.time()

    def record(self, name: str, category: str, value: float,
               unit: str = "", tags: Optional[Dict[str, str]] = None):
        self.metrics.append(Metric(
            name=name,
            category=category,
            value=value,
            unit=unit,
            tags=tags or {},
        ))

    def record_latency_stats(self, name: str, stats: LatencyStats,
                             tags: Optional[Dict[str, str]] = None):
        m = Metric(
            name=name,
            category="latency",
            value=stats.mean_ns,
            unit="ns",
            tags=tags or {},
            additional={
                "count": stats.count,
                "mean_ns": stats.mean_ns,
                "stddev_ns": stats.stddev_ns,
                "min_ns": stats.min_ns,
                "max_ns": stats.max_ns,
                "p50_ns": stats.p50_ns,
                "p90_ns": stats.p90_ns,
                "p95_ns": stats.p95_ns,
                "p99_ns": stats.p99_ns,
            }
        )
        self.metrics.append(m)

    def record_scalability(self, name: str, scale_factor: int, value: float,
                           unit: str = "", tags: Optional[Dict[str, str]] = None):
        m = Metric(name=name, category="scalability", value=value, unit=unit,
                   tags=tags or {}, additional={"scale_factor": float(scale_factor)})
        self.metrics.append(m)

    def record_throughput(self, name: str, operations: int,
                          duration_sec: float, tags: Optional[Dict[str, str]] = None):
        ops_per_sec = operations / duration_sec if duration_sec > 0 else 0
        m = Metric(
            name=name,
            category="throughput",
            value=ops_per_sec,
            unit="ops/sec",
            tags=tags or {},
            additional={
                "total_operations": operations,
                "duration_sec": duration_sec,
            }
        )
        self.metrics.append(m)

    def export_json(self, filepath: str):
        """Export metrics to JSON."""
        os.makedirs(os.path.dirname(filepath) or ".", exist_ok=True)
        result = {
            "benchmark_name": self.benchmark_name,
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "total_duration_sec": time.time() - self.start_time,
            "metadata": self.metadata,
            "metrics": [
                {
                    "name": m.name,
                    "category": m.category,
                    "value": m.value,
                    "unit": m.unit,
                    "additional": m.additional,
                    "tags": m.tags,
                }
                for m in self.metrics
            ]
        }
        with open(filepath, "w") as f:
            json.dump(result, f, indent=2)

    def export_csv(self, filepath: str):
        """Export metrics to CSV."""
        os.makedirs(os.path.dirname(filepath) or ".", exist_ok=True)
        with open(filepath, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow([
                "benchmark_name", "metric_name", "category", "value", "unit",
                "mean_ns", "p50_ns", "p95_ns", "p99_ns", "count"
            ])
            for m in self.metrics:
                writer.writerow([
                    self.benchmark_name, m.name, m.category, m.value, m.unit,
                    m.additional.get("mean_ns", ""),
                    m.additional.get("p50_ns", ""),
                    m.additional.get("p95_ns", ""),
                    m.additional.get("p99_ns", ""),
                    m.additional.get("count", ""),
                ])


def make_temp_config_file() -> str:
    """Create a minimal DSR config file."""
    import tempfile
    config = {
        "DSRModel": {
            "symbols": {
                "100": {
                    "attribute": {
                        "level": {"type": 1, "value": 0}
                    },
                    "id": "100",
                    "links": [],
                    "name": "root",
                    "type": "root"
                }
            }
        }
    }
    fd, path = tempfile.mkstemp(suffix=".json", prefix="dsr_bench_")
    with os.fdopen(fd, "w") as f:
        json.dump(config, f)
    return path


def warmup(func: Callable, iterations: int = 10):
    """Run warmup iterations."""
    for _ in range(iterations):
        func()


# ── pyperf integration ────────────────────────────────────────────────────────

try:
    import pyperf as _pyperf  # type: ignore
    HAS_PYPERF = True
except ImportError:
    _pyperf = None  # type: ignore
    HAS_PYPERF = False


def pyperf_to_latency_stats(bm) -> LatencyStats:
    """Convert a pyperf Benchmark to LatencyStats.

    pyperf 'values' are mean elapsed time per operation (in seconds) for each
    run.  With the default 3 processes × 5 values we get ~15 data points.
    Note: these are per-run averages, not individual-op samples, so percentiles
    reflect variability across runs rather than per-op tail latency.
    """
    if bm is None:
        return LatencyStats()
    try:
        values_ns = [v * 1e9 for v in bm.get_values()]
    except Exception:
        return LatencyStats()
    if not values_ns:
        return LatencyStats()

    sorted_v = sorted(values_ns)
    n = len(sorted_v)

    def pct(p: float) -> float:
        idx = p * (n - 1)
        lo = int(idx)
        hi = min(lo + 1, n - 1)
        f = idx - lo
        return sorted_v[lo] * (1 - f) + sorted_v[hi] * f

    return LatencyStats(
        count=n,
        mean_ns=statistics.mean(sorted_v),
        stddev_ns=statistics.stdev(sorted_v) if n > 1 else 0.0,
        min_ns=sorted_v[0],
        max_ns=sorted_v[-1],
        p50_ns=pct(0.50),
        p90_ns=pct(0.90),
        p95_ns=pct(0.95),
        p99_ns=pct(0.99),
    )
