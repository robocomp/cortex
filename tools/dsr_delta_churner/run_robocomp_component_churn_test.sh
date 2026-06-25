#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPONENTS="$(cd "$ROOT/../components" && pwd)"
BUILD="$ROOT/build"
CHURNER_BIN="$ROOT/tools/dsr_delta_churner/dsr_delta_churner"
LOGDIR="${TMPDIR:-/tmp}/dsr_delta_churn_component_test"
GRAPH_FILE="$LOGDIR/empty_graph.json"
CONFIG_FILE="$LOGDIR/bullshit_publisher_graph_only.config"
COMPONENT="${COMPONENT:-bullshit_publisher}"

ITERATIONS="${ITERATIONS:-50}"
NODES_PER_RUN="${NODES_PER_RUN:-30}"
HOLD_MS="${HOLD_MS:-250}"
SETTLE_MS="${SETTLE_MS:-500}"
PARALLEL_CLIENTS="${PARALLEL_CLIENTS:-1}"
REBUILD_COMPONENT="${REBUILD_COMPONENT:-0}"

mkdir -p "$LOGDIR"

cat >"$GRAPH_FILE" <<'JSON'
{
  "DSRModel": {
    "symbols": {
      "100": {
        "attribute": {
          "level": {
            "type": 1,
            "value": 0
          }
        },
        "id": "100",
        "links": [],
        "name": "root",
        "type": "root"
      }
    }
  }
}
JSON

cat >"$CONFIG_FILE" <<CFG
Agent.id = 901
Agent.name = "$COMPONENT"
Agent.configFile = "$GRAPH_FILE"
ViewAgent.tree = false
ViewAgent.graph = true
ViewAgent.2d = false
ViewAgent.3d = false
Period.Compute = 100
Period.Emergency = 500
Ice.Warn.Connections = "0"
Ice.Trace.Network = "0"
Ice.Trace.Protocol = "0"
Ice.MessageSizeMax = "20004800"
CFG

g++ -std=c++23 -O2 -g -fPIC -no-pie \
  "$ROOT/tools/dsr_delta_churner/dsr_delta_churner.cpp" \
  -DQT_CORE_LIB \
  -I"$ROOT/api/include" \
  -I"$ROOT/core/include" \
  -I"$ROOT/qmat/include" \
  -I/usr/include/x86_64-linux-gnu/qt6 \
  -I/usr/include/x86_64-linux-gnu/qt6/QtCore \
  -L"$BUILD/api" \
  -L"$BUILD/core" \
  -Wl,-rpath,"$BUILD/api" \
  -Wl,-rpath,"$BUILD/core" \
  -ldsr_api -ldsr_core -lQt6Core -lfastdds -lfastcdr \
  -o "$CHURNER_BIN"

if [[ "$REBUILD_COMPONENT" == "1" ]]; then
  CPLUS_INCLUDE_PATH="$ROOT/api/include:$ROOT/gui/include:$ROOT/core/include:$ROOT/qmat/include" \
    cmake --build "$COMPONENTS/$COMPONENT/build" -j2
fi

export LD_LIBRARY_PATH="$BUILD/gui:$BUILD/api:$BUILD/core:${LD_LIBRARY_PATH:-}"

"$COMPONENTS/$COMPONENT/bin/$COMPONENT" --Ice.Config="$CONFIG_FILE" \
  >"$LOGDIR/$COMPONENT.log" 2>&1 &
LIVE_PID=$!

cleanup() {
  if kill -0 "$LIVE_PID" 2>/dev/null; then
    kill "$LIVE_PID" 2>/dev/null || true
    wait "$LIVE_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

sleep 4

for i in $(seq 1 "$ITERATIONS"); do
  if ! kill -0 "$LIVE_PID" 2>/dev/null; then
    echo "$COMPONENT died before iteration $i"
    exit 1
  fi

  pids=()
  for client in $(seq 1 "$PARALLEL_CLIENTS"); do
    agent_id=$((1000 + i * 10 + client))
    "$CHURNER_BIN" \
      --agent-id="$agent_id" \
      --tag="component_run_${i}_${client}" \
      --count="$NODES_PER_RUN" \
      --hold-ms="$HOLD_MS" \
      --settle-ms="$SETTLE_MS" \
      >"$LOGDIR/churn_${i}_${client}.log" 2>&1 &
    pids+=("$!")
  done

  failed=0
  for pid in "${pids[@]}"; do
    if ! wait "$pid"; then
      failed=1
    fi
  done
  if [[ "$failed" != "0" ]]; then
    echo "one or more churners failed in iteration $i"
    exit 1
  fi

  echo "iteration $i/$ITERATIONS ok ($PARALLEL_CLIENTS parallel clients)"
done

if ! kill -0 "$LIVE_PID" 2>/dev/null; then
  echo "$COMPONENT died after churn loop"
  exit 1
fi

echo "PASS: $COMPONENT survived $ITERATIONS waves with $PARALLEL_CLIENTS parallel churners"
echo "logs: $LOGDIR"
