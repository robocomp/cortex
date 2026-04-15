#!/usr/bin/env python3
import argparse
import json
import os
import sys
import time
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run one DSR agent worker process")
    parser.add_argument("--agent-name", required=True)
    parser.add_argument("--agent-id", required=True, type=int)
    parser.add_argument("--domain-id", required=True, type=int)
    parser.add_argument("--same-host", required=True, choices=("true", "false"))
    parser.add_argument("--graph-file", default="")
    parser.add_argument("--artifacts-dir", required=True)
    parser.add_argument("--local-attr", required=True)
    parser.add_argument("--local-value", required=True)
    parser.add_argument("--remote-attr", required=True)
    parser.add_argument("--remote-value", required=True)
    parser.add_argument("--startup-delay", default=0.0, type=float)
    parser.add_argument("--sync-timeout", default=30.0, type=float)
    parser.add_argument("--hold-seconds", default=0.0, type=float)
    return parser.parse_args()


def wait_for(predicate, timeout_s: float, interval_s: float = 0.1, error: str = "timeout"):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(interval_s)
    raise TimeoutError(error)


def read_root_attr(graph, attr_name: str):
    root = graph.get_node("root")
    if root is None:
        return None
    if attr_name not in root.attrs:
        return None
    return root.attrs[attr_name].value


def main() -> int:
    args = parse_args()
    artifacts_dir = Path(args.artifacts_dir)
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    result_path = artifacts_dir / f"{args.agent_name}.json"

    build_python_wrapper = Path(__file__).resolve().parents[2] / "build" / "python-wrapper"
    sys.path.insert(0, str(build_python_wrapper))

    import pydsr

    time.sleep(args.startup_delay)

    graph = pydsr.DSRGraph(
        0,
        args.agent_name,
        args.agent_id,
        args.graph_file,
        args.same_host == "true",
        args.domain_id,
    )

    result = {
        "agent_name": args.agent_name,
        "agent_id": args.agent_id,
        "domain_id": args.domain_id,
        "same_host": args.same_host == "true",
        "graph_file_loaded": bool(args.graph_file),
    }

    try:
        initial_nodes = wait_for(
            lambda: len(graph.get_nodes()) if graph.get_node("root") is not None else 0,
            timeout_s=args.sync_timeout,
            error="graph root never became available",
        )
        result["initial_node_count"] = initial_nodes

        root = wait_for(
            lambda: graph.get_node("root"),
            timeout_s=args.sync_timeout,
            error="root node not available",
        )
        root.attrs[args.local_attr] = pydsr.Attribute(args.local_value)
        update_ok = graph.update_node(root)
        if not update_ok:
            raise RuntimeError(f"failed to update root with {args.local_attr}")

        observed_remote = wait_for(
            lambda: read_root_attr(graph, args.remote_attr),
            timeout_s=args.sync_timeout,
            error=f"remote attribute {args.remote_attr} not observed",
        )
        if observed_remote != args.remote_value:
            raise RuntimeError(
                f"unexpected value for {args.remote_attr}: {observed_remote!r} != {args.remote_value!r}"
            )

        final_root = graph.get_node("root")
        result["final_node_count"] = len(graph.get_nodes())
        result["local_attr_value"] = final_root.attrs[args.local_attr].value
        result["remote_attr_value"] = final_root.attrs[args.remote_attr].value
        if args.hold_seconds > 0:
            time.sleep(args.hold_seconds)
        result["status"] = "ok"
    except Exception as exc:
        result["status"] = "error"
        result["error"] = str(exc)
    finally:
        result_path.write_text(json.dumps(result, indent=2), encoding="utf-8")

    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
