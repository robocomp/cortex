#!/usr/bin/env bash
# flamegraph.sh — generate a per-benchmark flamegraph SVG using perf.
#
# Usage:
#   ./flamegraph.sh [OPTIONS] [FILTER]
#
# Options:
#   -b BINARY     Path to dsr_benchmarks (default: ./build/dsr_benchmarks)
#   -o OUTPUT     Output directory for SVGs and perf.data files
#                 (default: ./results/flamegraphs)
#   -F FREQ       perf sampling frequency in Hz (default: 999)
#   -k            Keep raw perf.data files (deleted by default)
#   -h            Show this help
#
# FILTER is forwarded to Catch2 as a tag or test-name substring, e.g.:
#   ./flamegraph.sh "[LATENCY]"      # only latency tests
#   ./flamegraph.sh "Node insertion" # only that one test
#   ./flamegraph.sh                  # all tests (skips .multi by default)
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
#   3. perf_event_paranoia — perf needs read access to kernel symbols.
#      If perf says "Permission denied" or produces empty stacks, lower the
#      paranoia level (resets on reboot):
#        echo 1 | sudo tee /proc/sys/kernel/perf_event_paranoia
#      To make it permanent:
#        echo 'kernel.perf_event_paranoia = 1' | sudo tee /etc/sysctl.d/99-perf.conf
#        sudo sysctl --system
#      Alternatively run this script with sudo (not recommended for daily use).
#
#   4. Debug symbols — for meaningful stack frames the binary should be built
#      with frame pointers or DWARF info.  The CMake target already passes
#      -g, which is sufficient.  For deeper library frames install:
#        sudo apt install linux-tools-$(uname -r) libc6-dbg

set -euo pipefail

# ── defaults ────────────────────────────────────────────────────────────────
BINARY="./build/dsr_benchmarks"
OUTDIR="./results/flamegraphs"
FREQ=999
KEEP_DATA=0
FILTER="${1:-}"

# ── argument parsing ─────────────────────────────────────────────────────────
while getopts "b:o:F:kh" opt; do
    case $opt in
        b) BINARY="$OPTARG" ;;
        o) OUTDIR="$OPTARG" ;;
        F) FREQ="$OPTARG" ;;
        k) KEEP_DATA=1 ;;
        h)
            sed -n '2,/^set -/p' "$0" | grep '^#' | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) echo "Unknown option: -$OPTARG" >&2; exit 1 ;;
    esac
done
shift $((OPTIND - 1))
FILTER="${1:-}"   # remaining positional arg is the Catch2 filter

# ── locate FlameGraph tools ──────────────────────────────────────────────────
FG_DIR="${FG_DIR:-}"
find_tool() {
    local name="$1"
    if [[ -n "$FG_DIR" && -x "$FG_DIR/$name" ]]; then
        echo "$FG_DIR/$name"; return
    fi
    if command -v "$name" &>/dev/null; then
        command -v "$name"; return
    fi
    # common install paths
    for p in /usr/share/FlameGraph /opt/FlameGraph ~/FlameGraph; do
        [[ -x "$p/$name" ]] && { echo "$p/$name"; return; }
    done
    echo ""
}

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

# ── sanity checks ────────────────────────────────────────────────────────────
[[ -x "$BINARY" ]] || { echo "ERROR: binary not found or not executable: $BINARY" >&2; exit 1; }
command -v perf &>/dev/null || { echo "ERROR: perf not found" >&2; exit 1; }

mkdir -p "$OUTDIR"

# ── list test cases ──────────────────────────────────────────────────────────
# Catch2 v3: `--list-tests` prints indented names; test names are flush-left
# lines that do NOT start with whitespace, the tags line follows indented.
# We capture names by looking for lines not starting with spaces/brackets and
# not being the header line.
list_args=(--list-tests --verbosity quiet)
[[ -n "$FILTER" ]] && list_args+=("$FILTER")

mapfile -t TEST_NAMES < <(
    "$BINARY" "${list_args[@]}" 2>/dev/null \
    | grep -v '^\s' \
    | grep -v '^All available' \
    | grep -v '^$' \
    | sed 's/^[[:space:]]*//' \
    | grep -v '^\['
)

if [[ ${#TEST_NAMES[@]} -eq 0 ]]; then
    echo "No tests matched filter: '${FILTER}'" >&2
    echo "Run '$BINARY --list-tests' to see available tests." >&2
    exit 1
fi

echo "Found ${#TEST_NAMES[@]} test(s) to profile."
echo "Output: $OUTDIR"
echo

# ── profile each test ────────────────────────────────────────────────────────
PASS=0
FAIL=0

for name in "${TEST_NAMES[@]}"; do
    # derive a filesystem-safe name
    safe="$(echo "$name" | tr -cs 'A-Za-z0-9_-' '_' | sed 's/_\+/_/g; s/^_//; s/_$//')"

    perf_data="$OUTDIR/${safe}.perf.data"
    svg_out="$OUTDIR/${safe}.svg"

    echo "── $name"

    # Record. perf writes to the data file; the binary runs single-shot.
    if perf record \
            -F "$FREQ" \
            -g \
            --call-graph dwarf \
            -o "$perf_data" \
            -- "$BINARY" "$name" \
            2>/dev/null; then

        # Convert to flamegraph
        perf script -i "$perf_data" 2>/dev/null \
            | perl "$COLLAPSE" --inline \
            | perl "$FLAMEGRAPH" --title "$name" \
            > "$svg_out"

        echo "   → $svg_out"
        (( PASS++ )) || true
    else
        echo "   ✗ perf record failed (exit $?)" >&2
        (( FAIL++ )) || true
    fi

    [[ $KEEP_DATA -eq 0 && -f "$perf_data" ]] && rm -f "$perf_data"
done

echo
echo "Done: $PASS succeeded, $FAIL failed."
[[ $FAIL -eq 0 ]]
