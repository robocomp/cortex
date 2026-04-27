#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ARTIFACT_ROOT="${ROOT_DIR}/.artifacts/same_host_transport"
GRAPH_FILE="${ROOT_DIR}/python-wrapper/etc/autonomyLab_objects.simscene.json"
WORKER="${ROOT_DIR}/tools/same_host_smoke/agent_worker.py"

export PYTHONPATH="${ROOT_DIR}/build/python-wrapper${PYTHONPATH:+:${PYTHONPATH}}"
export LD_LIBRARY_PATH="${ROOT_DIR}/build/api:${ROOT_DIR}/build/core${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

mkdir -p "${ARTIFACT_ROOT}"

snapshot_case() {
  local case_dir="$1"
  local pid_a="$2"
  local pid_b="$3"

  {
    echo "loader_pid=${pid_a}"
    echo "follower_pid=${pid_b}"
  } > "${case_dir}/pids.txt"

  lsof -p "${pid_a}" > "${case_dir}/loader.lsof" || true
  lsof -p "${pid_b}" > "${case_dir}/follower.lsof" || true
  ss -uapn > "${case_dir}/ss.txt" || true
  ip maddr show > "${case_dir}/ip_maddr.txt" || true
}

verify_case() {
  local same_host="$1"
  local case_dir="${ARTIFACT_ROOT}/same_host_${same_host}"
  local domain_id loader_id follower_id

  if [[ "${same_host}" == "true" ]]; then
    domain_id=51
    loader_id=1501
    follower_id=1502
  else
    domain_id=52
    loader_id=1511
    follower_id=1512
  fi

  mkdir -p "${case_dir}"

  python3 "${WORKER}" \
    --agent-name "transport_${same_host}_loader" \
    --agent-id "${loader_id}" \
    --domain-id "${domain_id}" \
    --same-host "${same_host}" \
    --graph-file "${GRAPH_FILE}" \
    --artifacts-dir "${case_dir}" \
    --local-attr "transport_loader_${same_host}" \
    --local-value "loader_${same_host}" \
    --remote-attr "transport_follower_${same_host}" \
    --remote-value "follower_${same_host}" \
    --hold-seconds 12 \
    > "${case_dir}/loader.log" 2>&1 &
  local pid_a=$!

  python3 "${WORKER}" \
    --agent-name "transport_${same_host}_follower" \
    --agent-id "${follower_id}" \
    --domain-id "${domain_id}" \
    --same-host "${same_host}" \
    --artifacts-dir "${case_dir}" \
    --local-attr "transport_follower_${same_host}" \
    --local-value "follower_${same_host}" \
    --remote-attr "transport_loader_${same_host}" \
    --remote-value "loader_${same_host}" \
    --startup-delay 1 \
    --hold-seconds 12 \
    > "${case_dir}/follower.log" 2>&1 &
  local pid_b=$!

  sleep 4
  snapshot_case "${case_dir}" "${pid_a}" "${pid_b}"

  wait "${pid_a}"
  wait "${pid_b}"

  python3 - "${case_dir}" "${same_host}" <<'PY'
import json
import sys
from pathlib import Path

case_dir = Path(sys.argv[1])
same_host = sys.argv[2]

loader = json.loads(next(case_dir.glob("*loader.json")).read_text(encoding="utf-8"))
follower = json.loads(next(case_dir.glob("*follower.json")).read_text(encoding="utf-8"))
def read_if_exists(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="ignore") if path.exists() else ""

lsof_loader = read_if_exists(case_dir / "loader.lsof")
lsof_follower = read_if_exists(case_dir / "follower.lsof")
ss_txt = read_if_exists(case_dir / "ss.txt")
ip_maddr = read_if_exists(case_dir / "ip_maddr.txt")

for result in (loader, follower):
    if result["status"] != "ok":
        raise SystemExit(f"{result['agent_name']} failed: {result.get('error', 'unknown error')}")

combined_lsof = lsof_loader + "\n" + lsof_follower

evidence_lines = []
for line in combined_lsof.splitlines():
    if "/dev/shm/fastdds_" in line or "239.255." in line:
        evidence_lines.append(line.strip())

uses_multicast = any(marker in (combined_lsof + "\n" + ss_txt + "\n" + ip_maddr) for marker in (
    "239.255.0.1",
    "239.255.0.53",
))
uses_shm = "/dev/shm" in combined_lsof

summary = {
    "same_host": same_host == "true",
    "uses_multicast": uses_multicast,
    "uses_shm": uses_shm,
    "evidence_lines": evidence_lines[:12],
}
(case_dir / "transport_summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")

if same_host == "true" and not uses_shm:
    raise SystemExit("shared-memory evidence not found for same_host=true")

# Multicast is the discovery mechanism for cross-host (same_host=false).
# For same_host=true, DSR uses SHM + loopback-UDP unicast — no multicast
# group is joined, so absence of 239.255.x.x evidence is expected and correct.
if same_host == "false" and not uses_multicast:
    raise SystemExit("multicast evidence not found for same_host=false")

if same_host == "false" and uses_shm:
    raise SystemExit("unexpected shared-memory evidence found for same_host=false")

print(json.dumps(summary))
print("evidence:")
for line in summary["evidence_lines"]:
    print(f"  {line}")
PY
}

verify_case true
verify_case false

echo "Transport artifacts written to ${ARTIFACT_ROOT}"
