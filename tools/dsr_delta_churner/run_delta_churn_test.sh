#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPONENTS="$(cd "$ROOT/../components" && pwd)"
BUILD="$ROOT/build"
CHURNER_BIN="$ROOT/tools/dsr_delta_churner/dsr_delta_churner"
VIEWER_BIN="$ROOT/tools/dsr_delta_churner/dsr_live_viewer"
LOGDIR="${TMPDIR:-/tmp}/dsr_delta_churn_test"
GRAPH_FILE="$LOGDIR/empty_graph.json"

ITERATIONS="${ITERATIONS:-50}"
NODES_PER_RUN="${NODES_PER_RUN:-30}"
HOLD_MS="${HOLD_MS:-250}"
SETTLE_MS="${SETTLE_MS:-500}"

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

g++ -std=c++23 -O2 -g -fPIC -no-pie \
  "$ROOT/tools/dsr_delta_churner/dsr_live_viewer.cpp" \
  -DQT_CORE_LIB -DQT_GUI_LIB -DQT_WIDGETS_LIB -DQT_OPENGLWIDGETS_LIB \
  -I"$ROOT/api/include" \
  -I"$ROOT/gui/include" \
  -I"$ROOT/core/include" \
  -I"$ROOT/qmat/include" \
  -I/usr/include/x86_64-linux-gnu/qt6 \
  -I/usr/include/x86_64-linux-gnu/qt6/QtCore \
  -I/usr/include/x86_64-linux-gnu/qt6/QtGui \
  -I/usr/include/x86_64-linux-gnu/qt6/QtWidgets \
  -I/usr/include/x86_64-linux-gnu/qt6/QtOpenGLWidgets \
  -L"$BUILD/gui" \
  -L"$BUILD/api" \
  -L"$BUILD/core" \
  -Wl,-rpath,"$BUILD/gui" \
  -Wl,-rpath,"$BUILD/api" \
  -Wl,-rpath,"$BUILD/core" \
  -ldsr_gui -ldsr_api -ldsr_core \
  -lQt6OpenGLWidgets -lQt6Widgets -lQt6Gui -lQt6Core \
  -lgvc -lcgraph -lfastdds -lfastcdr \
  -o "$VIEWER_BIN"

export LD_LIBRARY_PATH="$BUILD/gui:$BUILD/api:$BUILD/core:${LD_LIBRARY_PATH:-}"

"$VIEWER_BIN" "$GRAPH_FILE" >"$LOGDIR/dsr_live_viewer.log" 2>&1 &
LIVE_PID=$!

cleanup() {
  if kill -0 "$LIVE_PID" 2>/dev/null; then
    kill "$LIVE_PID" 2>/dev/null || true
    wait "$LIVE_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

sleep 3

for i in $(seq 1 "$ITERATIONS"); do
  if ! kill -0 "$LIVE_PID" 2>/dev/null; then
    echo "dsr_live_viewer died before iteration $i"
    exit 1
  fi

  "$CHURNER_BIN" \
    --agent-id="$((1000 + i))" \
    --tag="run_$i" \
    --count="$NODES_PER_RUN" \
    --hold-ms="$HOLD_MS" \
    --settle-ms="$SETTLE_MS" \
    >"$LOGDIR/churn_$i.log" 2>&1

  echo "iteration $i/$ITERATIONS ok"
done

if ! kill -0 "$LIVE_PID" 2>/dev/null; then
  echo "dsr_live_viewer died after churn loop"
  exit 1
fi

echo "PASS: dsr_live_viewer survived $ITERATIONS churner launches"
echo "logs: $LOGDIR"
