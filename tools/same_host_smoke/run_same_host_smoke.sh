#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ARTIFACT_ROOT="${ROOT_DIR}/.artifacts/same_host_smoke"
GRAPH_FILE="${ROOT_DIR}/python-wrapper/etc/autonomyLab_objects.simscene.json"
WORKER="${ROOT_DIR}/tools/same_host_smoke/agent_worker.py"

export PYTHONPATH="${ROOT_DIR}/build/python-wrapper${PYTHONPATH:+:${PYTHONPATH}}"
export LD_LIBRARY_PATH="${ROOT_DIR}/build/api:${ROOT_DIR}/build/core${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

mkdir -p "${ARTIFACT_ROOT}"

run_case() {
  local same_host="$1"
  local domain_id="$2"
  local case_dir="${ARTIFACT_ROOT}/same_host_${same_host}"

  rm -rf "${case_dir}"
  mkdir -p "${case_dir}"

  python3 "${WORKER}" \
    --agent-name "same_host_${same_host}_loader" \
    --agent-id $((domain_id * 10 + 1)) \
    --domain-id "${domain_id}" \
    --same-host "${same_host}" \
    --graph-file "${GRAPH_FILE}" \
    --artifacts-dir "${case_dir}" \
    --local-attr "sync_from_loader_${same_host}" \
    --local-value "loader_${same_host}" \
    --remote-attr "sync_from_follower_${same_host}" \
    --remote-value "follower_${same_host}" \
    > "${case_dir}/loader.log" 2>&1 &
  local pid_a=$!

  python3 "${WORKER}" \
    --agent-name "same_host_${same_host}_follower" \
    --agent-id $((domain_id * 10 + 2)) \
    --domain-id "${domain_id}" \
    --same-host "${same_host}" \
    --artifacts-dir "${case_dir}" \
    --local-attr "sync_from_follower_${same_host}" \
    --local-value "follower_${same_host}" \
    --remote-attr "sync_from_loader_${same_host}" \
    --remote-value "loader_${same_host}" \
    --startup-delay 1.0 \
    > "${case_dir}/follower.log" 2>&1 &
  local pid_b=$!

  local rc=0
  wait "${pid_a}" || rc=1
  wait "${pid_b}" || rc=1

  if [[ "${rc}" -ne 0 ]]; then
    echo "Scenario same_host=${same_host} failed. See ${case_dir}" >&2
    return "${rc}"
  fi

  python3 - "${case_dir}" "${same_host}" <<'PY'
import json
import sys
from pathlib import Path

case_dir = Path(sys.argv[1])
same_host = sys.argv[2]
loader = json.loads((case_dir / f"same_host_{same_host}_loader.json").read_text(encoding="utf-8"))
follower = json.loads((case_dir / f"same_host_{same_host}_follower.json").read_text(encoding="utf-8"))

for result in (loader, follower):
    if result["status"] != "ok":
        raise SystemExit(f"{result['agent_name']} failed: {result.get('error', 'unknown error')}")

if follower["initial_node_count"] <= 0:
    raise SystemExit("Follower did not receive the initial graph")

if loader["remote_attr_value"] != f"follower_{same_host}":
    raise SystemExit("Loader did not observe follower mutation")

if follower["remote_attr_value"] != f"loader_{same_host}":
    raise SystemExit("Follower did not observe loader mutation")

print(f"same_host={same_host}: PASS")
PY
}

run_case true 41
run_case false 42

echo "Artifacts written to ${ARTIFACT_ROOT}"
