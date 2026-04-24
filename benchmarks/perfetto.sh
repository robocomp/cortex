#!/usr/bin/env bash
# perfetto.sh - generate one Perfetto trace per benchmark test case.
#
# Usage:
#   ./perfetto.sh [OPTIONS] [FILTER]
#
# Options:
#   -b BINARY     Path to dsr_benchmarks (default: ./build/dsr_benchmarks)
#   -d DETAIL     Profiling detail: off, min, default, detail, hot
#                 (default: use BENCH_PROFILE_DETAIL or binary default)
#   -g BACKEND    Graph backend for dsr_benchmarks: crdt or lww
#                 (default: use BENCH_SYNC_MODE or dsr_benchmarks default)
#   -o OUTPUT     Output root directory for run subdirectories
#                 (default: ./results/perfetto)
#   -l            List matching profile targets and exit
#   -p PRESET     Built-in preset: load, multiagent, profile
#   -r RUN_ID     Run directory name under OUTPUT
#                 (default: perfetto-YYYYMMDD-HHMMSS)
#   -k            Keep running after individual benchmark failures
#   -h            Show this help
#
# FILTER is forwarded to Catch2 as a tag expression or exact test name, e.g.:
#   ./perfetto.sh "Signal emission under load"
#   ./perfetto.sh "[PROFILE][LOAD]"
#   ./perfetto.sh -p multiagent
#   ./perfetto.sh -l -p profile
#
# The script intentionally does not default to "all benchmarks". Pass an exact
# benchmark name, a Catch2 tag expression, or a preset for scoped profiling.

set -euo pipefail

BINARY="./build/dsr_benchmarks"
OUTROOT="./results/perfetto"
LIST_ONLY=0
PRESET=""
FILTER=""
BACKEND="${BENCH_SYNC_MODE:-}"
DETAIL="${BENCH_PROFILE_DETAIL:-}"
RUN_ID="perfetto-$(date +%Y%m%d-%H%M%S)"
KEEP_GOING=0

while getopts "b:d:g:o:lp:r:kh" opt; do
    case "$opt" in
        b) BINARY="$OPTARG" ;;
        d) DETAIL="$OPTARG" ;;
        g) BACKEND="$OPTARG" ;;
        o) OUTROOT="$OPTARG" ;;
        l) LIST_ONLY=1 ;;
        p) PRESET="$OPTARG" ;;
        r) RUN_ID="$OPTARG" ;;
        k) KEEP_GOING=1 ;;
        h)
            sed -n '2,/^set -/p' "$0" | grep '^#' | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) echo "Unknown option: -$OPTARG" >&2; exit 1 ;;
    esac
done
shift $((OPTIND - 1))
FILTER="${1:-}"

preset_to_filter() {
    case "$1" in
        load) echo "[PROFILE][LOAD]" ;;
        multiagent) echo "[PROFILE][MULTIAGENT]" ;;
        profile) echo "[PROFILE]" ;;
        *)
            echo "ERROR: unknown preset '$1' (expected: load, multiagent, profile)" >&2
            exit 1
            ;;
    esac
}

if [[ -n "$PRESET" && -n "$FILTER" ]]; then
    echo "ERROR: use either -p PRESET or a FILTER argument, not both" >&2
    exit 1
fi

if [[ -n "$PRESET" ]]; then
    FILTER="$(preset_to_filter "$PRESET")"
fi

if [[ -z "$FILTER" ]]; then
    cat >&2 <<'EOF'
ERROR: a benchmark filter is required.
Examples:
  ./perfetto.sh "Signal emission under load"
  ./perfetto.sh "[PROFILE][LOAD]"
  ./perfetto.sh -p multiagent
  ./perfetto.sh -l -p profile
EOF
    exit 1
fi

if [[ -n "$BACKEND" ]]; then
    case "$BACKEND" in
        crdt|CRDT) BACKEND="crdt" ;;
        lww|LWW) BACKEND="lww" ;;
        *)
            echo "ERROR: unknown graph backend '$BACKEND' (expected: crdt or lww)" >&2
            exit 1
            ;;
    esac
fi

if [[ -n "$DETAIL" ]]; then
    case "$DETAIL" in
        off|min|default|detail|hot) ;;
        *)
            echo "ERROR: unknown profiling detail '$DETAIL' (expected: off, min, default, detail, hot)" >&2
            exit 1
            ;;
    esac
fi

[[ -x "$BINARY" ]] || { echo "ERROR: binary not found or not executable: $BINARY" >&2; exit 1; }

OUTDIR="${OUTROOT}/${RUN_ID}"
mkdir -p "$OUTDIR"

mapfile -t TEST_NAMES < <(
    BENCH_SYNC_MODE="$BACKEND" BENCH_PROFILE_DETAIL="$DETAIL" "$BINARY" --list-tests --verbosity quiet "$FILTER" 2>/dev/null \
    | sed 's/\r$//' \
    | grep -v '^[[:space:]]' \
    | grep -v '^All available test cases:' \
    | grep -v '^[0-9][0-9]* test cases$' \
    | grep -v '^$'
)

if [[ ${#TEST_NAMES[@]} -eq 0 ]]; then
    echo "No tests matched filter: '${FILTER}'" >&2
    echo "Run '$BINARY --list-tests' to see available tests." >&2
    exit 1
fi

if [[ $LIST_ONLY -eq 1 ]]; then
    printf '%s\n' "${TEST_NAMES[@]}"
    exit 0
fi

echo "Found ${#TEST_NAMES[@]} test(s) to trace."
echo "Output: $OUTDIR"
if [[ -n "$BACKEND" ]]; then
    echo "Backend: $BACKEND"
fi
if [[ -n "$DETAIL" ]]; then
    echo "Profile detail: $DETAIL"
fi
echo

PASS=0
FAIL=0

for name in "${TEST_NAMES[@]}"; do
    safe="$(echo "$name" | tr -cs 'A-Za-z0-9_-' '_' | sed 's/_\+/_/g; s/^_//; s/_$//')"
    trace_out="${OUTDIR}/${safe}.pftrace"
    trace_tmp="${trace_out}.tmp.$$"

    echo "-- $name"

    if env \
        BENCH_SYNC_MODE="$BACKEND" \
        BENCH_PROFILE_DETAIL="$DETAIL" \
        CORTEX_PERFETTO_TRACE_FILE="$trace_tmp" \
        "$BINARY" "$name" 2>/dev/null; then

        if [[ -s "$trace_tmp" ]]; then
            mv -f "$trace_tmp" "$trace_out"
            echo "   -> $trace_out"
            ((PASS++)) || true
        else
            echo "   x trace file missing or empty" >&2
            rm -f "$trace_tmp"
            ((FAIL++)) || true
            if [[ $KEEP_GOING -eq 0 ]]; then
                exit 1
            fi
        fi
    else
        echo "   x benchmark run failed" >&2
        rm -f "$trace_tmp"
        ((FAIL++)) || true
        if [[ $KEEP_GOING -eq 0 ]]; then
            exit 1
        fi
    fi
done

echo
echo "Done: $PASS succeeded, $FAIL failed."
[[ $FAIL -eq 0 ]]
