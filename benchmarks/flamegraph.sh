#!/usr/bin/env bash
# flamegraph.sh - generate a per-benchmark flamegraph SVG using perf.
#
# Usage:
#   ./flamegraph.sh [OPTIONS] [FILTER]
#
# Options:
#   -b BINARY     Path to dsr_benchmarks (default: ./build/dsr_benchmarks)
#   -o OUTPUT     Output root directory for run subdirectories
#                 (default: ./results/flamegraphs)
#   -F FREQ       perf sampling frequency in Hz (default: 999)
#   -k            Keep raw perf.data files (deleted by default)
#   -l            List matching profile targets and exit
#   -p PRESET     Built-in preset: load, multiagent, profile
#   -r RUN_ID     Run directory name under OUTPUT
#                 (default: flamegraph-YYYYMMDD-HHMMSS)
#   -h            Show this help
#
# FILTER is forwarded to Catch2 as a tag expression or exact test name, e.g.:
#   ./flamegraph.sh "Signal emission under load"
#   ./flamegraph.sh "[PROFILE][LOAD]"
#   ./flamegraph.sh -p multiagent
#   ./flamegraph.sh -l -p profile
#
# The script intentionally does not default to "all benchmarks". Pass an exact
# benchmark name, a Catch2 tag expression, or a preset for scoped profiling.
#
# Requirements:
#   1. perf
#        sudo apt install linux-tools-common linux-tools-$(uname -r)
#
#   2. FlameGraph scripts (flamegraph.pl + stackcollapse-perf.pl)
#        git clone https://github.com/brendangregg/FlameGraph /opt/FlameGraph
#        export FG_DIR=/opt/FlameGraph
#      Either add them to PATH or set FG_DIR before running this script.
#
#   3. perf_event_paranoia - perf needs read access to kernel symbols.
#      If perf says "Permission denied" or produces empty stacks, lower the
#      paranoia level (resets on reboot):
#        echo 1 | sudo tee /proc/sys/kernel/perf_event_paranoia
#      To make it permanent:
#        echo 'kernel.perf_event_paranoia = 1' | sudo tee /etc/sysctl.d/99-perf.conf
#        sudo sysctl --system
#
#   4. Debug symbols - for meaningful stack frames the binary should be built
#      with frame pointers or DWARF info. The CMake target already passes -g.

set -euo pipefail

BINARY="./build/dsr_benchmarks"
OUTROOT="./results/flamegraphs"
FREQ=999
KEEP_DATA=0
LIST_ONLY=0
PRESET=""
FILTER=""
RUN_ID="flamegraph-$(date +%Y%m%d-%H%M%S)"

while getopts "b:o:F:klp:r:h" opt; do
    case "$opt" in
        b) BINARY="$OPTARG" ;;
        o) OUTROOT="$OPTARG" ;;
        F) FREQ="$OPTARG" ;;
        k) KEEP_DATA=1 ;;
        l) LIST_ONLY=1 ;;
        p) PRESET="$OPTARG" ;;
        r) RUN_ID="$OPTARG" ;;
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

find_tool() {
    local name="$1"

    if [[ -n "${FG_DIR:-}" && -x "${FG_DIR}/${name}" ]]; then
        echo "${FG_DIR}/${name}"
        return
    fi

    if command -v "$name" >/dev/null 2>&1; then
        command -v "$name"
        return
    fi

    for p in /usr/share/FlameGraph /opt/FlameGraph "$HOME/FlameGraph"; do
        if [[ -x "${p}/${name}" ]]; then
            echo "${p}/${name}"
            return
        fi
    done

    echo ""
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
  ./flamegraph.sh "Signal emission under load"
  ./flamegraph.sh "[PROFILE][LOAD]"
  ./flamegraph.sh -p multiagent
  ./flamegraph.sh -l -p profile
EOF
    exit 1
fi

[[ -x "$BINARY" ]] || { echo "ERROR: binary not found or not executable: $BINARY" >&2; exit 1; }
command -v perf >/dev/null 2>&1 || { echo "ERROR: perf not found" >&2; exit 1; }

OUTDIR="${OUTROOT}/${RUN_ID}"
mkdir -p "$OUTDIR"

mapfile -t TEST_NAMES < <(
    "$BINARY" --list-tests --verbosity quiet "$FILTER" 2>/dev/null \
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

COLLAPSE="$(find_tool stackcollapse-perf.pl)"
FLAMEGRAPH="$(find_tool flamegraph.pl)"

if [[ -z "$COLLAPSE" || -z "$FLAMEGRAPH" ]]; then
    cat >&2 <<'EOF'
ERROR: FlameGraph tools not found.
Install Brendan Gregg's FlameGraph scripts:
  git clone https://github.com/brendangregg/FlameGraph /opt/FlameGraph
  export FG_DIR=/opt/FlameGraph
or set FG_DIR to the directory containing flamegraph.pl and stackcollapse-perf.pl.
EOF
    exit 1
fi

echo "Found ${#TEST_NAMES[@]} test(s) to profile."
echo "Output: $OUTDIR"
echo

PASS=0
FAIL=0

for name in "${TEST_NAMES[@]}"; do
    safe="$(echo "$name" | tr -cs 'A-Za-z0-9_-' '_' | sed 's/_\+/_/g; s/^_//; s/_$//')"

    perf_data="${OUTDIR}/${safe}.perf.data"
    svg_out="${OUTDIR}/${safe}.svg"
    perf_tmp="${perf_data}.tmp.$$"
    svg_tmp="${svg_out}.tmp.$$"

    echo "-- $name"

    if perf record \
        -F "$FREQ" \
        -g \
        --call-graph dwarf \
        -o "$perf_tmp" \
        -- "$BINARY" "$name" 2>/dev/null; then

        perf script -i "$perf_tmp" 2>/dev/null \
            | perl "$COLLAPSE" --inline \
            | perl "$FLAMEGRAPH" --title "$name" \
            > "$svg_tmp"

        mv -f "$svg_tmp" "$svg_out"

        echo "   -> $svg_out"
        ((PASS++)) || true
    else
        echo "   x perf record failed" >&2
        rm -f "$perf_tmp" "$svg_tmp"
        ((FAIL++)) || true
        continue
    fi

    if [[ $KEEP_DATA -eq 0 && -f "$perf_tmp" ]]; then
        rm -f "$perf_tmp"
    elif [[ -f "$perf_tmp" ]]; then
        mv -f "$perf_tmp" "$perf_data"
    fi
done

echo
echo "Done: $PASS succeeded, $FAIL failed."
[[ $FAIL -eq 0 ]]
