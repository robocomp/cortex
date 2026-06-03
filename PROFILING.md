# Profiling

The repository supports multiple profiling backends through CMake and uses a
shared instrumentation surface across them.

The important design point is:

- the same `CORTEX_PROFILE_*` call sites are used for both Perfetto and Tracy
- backend choice changes collection and visualization, not where zones exist
- profiling detail is now an explicit runtime level instead of an implicit
  “everything is always on” model

## Backend Selection

Use one of:

```bash
-DCORTEX_PROFILING_BACKEND=NONE
-DCORTEX_PROFILING_BACKEND=TRACY
-DCORTEX_PROFILING_BACKEND=PERFETTO
```

### `NONE`

No profiling backend is enabled.

### `TRACY`

Uses the interactive Tracy workflow.

If `CORTEX_ENABLE_TRACY` is not explicitly configured, the build falls back to
the `DEFAULT` Tracy preset.

What you need:

- an instrumented build with `-DCORTEX_PROFILING_BACKEND=TRACY`
- the Tracy Profiler GUI
- a live connection between the running process and the profiler

Important:

- Tracy is mainly an interactive workflow
- traces are not written automatically to disk by the client
- if you want a saved trace, connect with the profiler and save the session
  from the UI

Typical usage:

```bash
cmake -S . -B build \
  -DWITH_TESTS=ON \
  -DCORTEX_PROFILING_BACKEND=TRACY \
  -DCORTEX_ENABLE_TRACY=DEFAULT
cmake --build build --target tests
./build/tests/tests "[SYNCHRONIZATION][GRAPH]"
```

Tracy presets are:

- `OFF`
- `ON`
- `DEFAULT`
- `MEDIUM`
- `EXTRA`
- `WSL`
- `WSL_EXTRA`

### `PERFETTO`

Uses Perfetto and writes an offline trace file automatically at process exit.

What you need:

- an instrumented build with `-DCORTEX_PROFILING_BACKEND=PERFETTO`
- a generated `.pftrace` file
- either the web UI at `https://ui.perfetto.dev/` or another
  Perfetto-compatible viewer

## Shared Instrumentation Model

Both Tracy and Perfetto use the same instrumentation macros from
[core/include/dsr/core/profiling.h](/abs/path/C:/Users/juanc/Projects/cortex/core/include/dsr/core/profiling.h).

The main macros are:

- `CORTEX_PROFILE_ZONE()`
- `CORTEX_PROFILE_ZONE_N("name")`
- `CORTEX_PROFILE_ZONE_CS("name")`

Those remain the default function-level surface.

There are also level-specific variants:

- `CORTEX_PROFILE_MIN_N("name")`
- `CORTEX_PROFILE_DETAIL_N("name")`
- `CORTEX_PROFILE_HOT_N("name")`
- `CORTEX_PROFILE_MIN_CS("name")`
- `CORTEX_PROFILE_DETAIL_CS("name")`
- `CORTEX_PROFILE_HOT_CS("name")`

The intent is:

- `min`: coarse lifecycle and benchmark-structure zones
- `default`: normal function-level zones
- `detail`: inner merge/apply/helper phases
- `hot`: very frequent per-call zones in hot paths

This lets long runs stay readable while still allowing focused deep traces.

## Profiling Detail Levels

Profiling detail is controlled at runtime, not configure time.

Accepted values:

- `off`
- `min`
- `default`
- `detail`
- `hot`

You can set it with either environment variable:

```bash
export CORTEX_PROFILE_DETAIL=min
export BENCH_PROFILE_DETAIL=detail
```

For `dsr_benchmarks`, `BENCH_PROFILE_DETAIL` is the most convenient form.

Examples:

```bash
BENCH_PROFILE_DETAIL=min ./build-perfetto/benchmarks/dsr_benchmarks "[PROFILE]"
BENCH_PROFILE_DETAIL=detail ./build-perfetto/benchmarks/dsr_benchmarks "[PROFILE]"
BENCH_PROFILE_DETAIL=hot ./build-perfetto/benchmarks/dsr_benchmarks "[PROFILE]"
```

Recommended usage:

- `min` for long benchmark suites where you care about setup, graph bootstrap,
  thread creation, and major phase boundaries
- `default` for ordinary development profiling
- `detail` for focused synchronization / merge / apply analysis
- `hot` only for short targeted runs, because it can generate a very large
  number of events

## Perfetto Callstack Mode (`CORTEX_PERFETTO_MODE`)

Three modes are available, selected at configure time:

| Mode | CMake flag | What you get |
|------|-----------|--------------|
| `DEFAULT` | _(omit)_ | Zone events only, no callstacks |
| `STACKFRAME` | `-DCORTEX_PERFETTO_MODE=STACKFRAME` | Callstacks attached to `CORTEX_PROFILE_ZONE_CS` zones |
| `LINUX_PERF` | `-DCORTEX_PERFETTO_MODE=LINUX_PERF` | Kernel CPU sampling correlated with zone events |

### `STACKFRAME`

Captures a callstack at every `CORTEX_PROFILE_ZONE_CS` call site using
`backtrace()`, resolves symbols with `dladdr` + demangling, and emits proper
Perfetto `Callstack` / `Frame` / `InternedString` data.

Important:

- `STACKFRAME` is only useful on `_CS` zones
- if most important coarse zones are plain `_N` zones, you will not get much
  extra value from `STACKFRAME`
- if work happens inside lambdas or unnamed helpers, wrap those bodies with
  explicit named zones if you want readable callsites

`-fno-omit-frame-pointer` is added automatically.

### `LINUX_PERF`

Adds a `linux.perf` data source alongside track events. The kernel interrupts
the process at 1 kHz and records sampled CPU stacks independently of
instrumentation.

This is useful for:

- hotspots in uninstrumented code
- inner work that would be too expensive to zone explicitly
- correlating coarse zones with sampled stacks

`-fno-omit-frame-pointer` is added automatically.

Requirements for `LINUX_PERF`:

- `traced`, `traced_probes`, and `traced_perf` must be running
- `perf_event_paranoid <= 0`:
  ```bash
  sudo sysctl -w kernel.perf_event_paranoid=0
  ```
- the real `tracebox` binary needs `CAP_PERFMON` and often `CAP_SYS_PTRACE`:
  ```bash
  ls -la $(which tracebox)
  sudo setcap cap_perfmon,cap_sys_ptrace+ep /path/to/real/tracebox
  ```
- if `setcap` is not an option, run `traced_perf` as root:
  ```bash
  tracebox traced --background
  tracebox traced_probes --background
  sudo tracebox traced_perf
  ```

Important:

- `LINUX_PERF` does not replace track events, it complements them
- zone events and sampled stacks land in the same `.pftrace`
- sampled stacks show up separately in the UI, typically under
  `Process callstacks cpu-clock`

## Perfetto Buffer Behavior

Perfetto currently uses separate buffers for:

- `track_event`
- `linux.perf` (when `LINUX_PERF` mode is enabled)

Current implementation details are in
[core/profiling.cpp](/abs/path/C:/Users/juanc/Projects/cortex/core/profiling.cpp):

- `track_event` buffer: `64 MB`
- `linux.perf` buffer: `64 MB`
- flush period: `1000 ms`
- incremental state clear period: `1000 ms`
- `track_event` incremental timestamps are disabled
- `track_event` buffer uses `DISCARD`

That last point matters:

- `DISCARD` preserves early setup / bootstrap zones once the event buffer fills
- later track events are dropped instead of overwriting the beginning of the
  run

This was chosen deliberately because long benchmark suites were saturating
`track_event` and hiding graph setup / agent bootstrap / subscription-thread
setup.

Tradeoff:

- `DISCARD` is better when you care about early setup visibility
- ring-buffer overwrite is better when you care about the tail of the run

For long benchmark suites, `DISCARD` is usually the better debugging default.

## Why Perfetto and `linux.perf` Behave Differently

It is normal for `track_event` and `linux.perf` to fail differently.

`track_event`:

- event-based
- records every emitted zone
- can saturate quickly if hot functions emit one zone per call

`linux.perf`:

- sampled
- records periodic interrupts rather than every function call
- can lose samples due to kernel ring-buffer overruns
- still often remains useful even when some samples are lost

So these are different failure modes:

- `track_event` overflow can hide setup or tail phases depending on fill policy
- `linux.perf` lost records show up as missing samples, not missing zones

## Typical Usage

### Perfetto `DEFAULT`

```bash
cmake -S . -B build-perfetto \
  -DWITH_TESTS=ON \
  -DCORTEX_PROFILING_BACKEND=PERFETTO
cmake --build build-perfetto --target tests
./build-perfetto/tests/tests "[SYNCHRONIZATION][GRAPH]"
```

### Perfetto `STACKFRAME`

```bash
cmake -S . -B build-perfetto \
  -DWITH_TESTS=ON \
  -DCORTEX_PROFILING_BACKEND=PERFETTO \
  -DCORTEX_PERFETTO_MODE=STACKFRAME
cmake --build build-perfetto --target tests
./build-perfetto/tests/tests "[SYNCHRONIZATION][GRAPH]"
```

### Perfetto `LINUX_PERF`

```bash
sudo sysctl -w kernel.perf_event_paranoid=0
sudo setcap cap_perfmon,cap_sys_ptrace+ep /path/to/real/tracebox

tracebox traced --background
tracebox traced_probes --background
tracebox traced_perf --background

cmake -S . -B build-pf-lp \
  -DWITH_TESTS=ON \
  -DCORTEX_PROFILING_BACKEND=PERFETTO \
  -DCORTEX_PERFETTO_MODE=LINUX_PERF
cmake --build build-pf-lp -j$(nproc)
./build-pf-lp/bin/your_component
```

### Tracy

```bash
cmake -S . -B build-tracy \
  -DWITH_TESTS=ON \
  -DCORTEX_PROFILING_BACKEND=TRACY \
  -DCORTEX_ENABLE_TRACY=DEFAULT
cmake --build build-tracy --target tests
./build-tracy/tests/tests "[SYNCHRONIZATION][GRAPH]"
```

## Benchmark Workflow

`dsr_benchmarks` starts profiling before `Catch::Session::run()`, so benchmark
fixture setup and graph/bootstrap phases can appear in the trace if the active
detail level includes them.

Useful examples:

```bash
BENCH_PROFILE_DETAIL=min ./build-perfetto/benchmarks/dsr_benchmarks "[PROFILE]"
BENCH_PROFILE_DETAIL=default ./build-perfetto/benchmarks/dsr_benchmarks "[PROFILE]"
BENCH_PROFILE_DETAIL=hot ./build-perfetto/benchmarks/dsr_benchmarks "[PROFILE]"
```

For one trace file per benchmark case, use
[benchmarks/perfetto.sh](/abs/path/C:/Users/juanc/Projects/cortex/benchmarks/perfetto.sh):

```bash
bash benchmarks/perfetto.sh -b ./build-perfetto/benchmarks/dsr_benchmarks -p profile
bash benchmarks/perfetto.sh -b ./build-perfetto/benchmarks/dsr_benchmarks -g lww -d min "[PROFILE][LOAD]"
```

It mirrors the flamegraph workflow:

- discovers matching Catch2 benchmarks first
- runs each benchmark case separately
- writes one `.pftrace` per case
- stores traces under `results/perfetto/<run-id>/`

There is also a matching CMake target:

```bash
cmake --build build-perfetto --target perfetto_traces -j1 -- BENCH_FILTER=[PROFILE]
```

Recommended workflow:

1. Start with `min` for long benchmark suites.
2. Use `default` or `detail` for medium focused runs.
3. Use `hot` only on short targeted runs, ideally one benchmark or one small
   benchmark group at a time.

## Trace File Path

The trace file path can be overridden with:

```bash
export CORTEX_PERFETTO_TRACE_FILE=/path/to/output.pftrace
```

If not set, the default output path is:

```text
.artifacts/perfetto/<executable>-<pid>-<timestamp>.pftrace
```

relative to the process working directory.

## Viewing Traces

Open the generated `.pftrace` with:

- https://ui.perfetto.dev/
- Perfetto standalone UI / trace processor tools

Recommended flow:

1. Run the instrumented binary or benchmark.
2. Wait for the process to exit cleanly.
3. Open `https://ui.perfetto.dev/`.
4. Load the `.pftrace`.

## Practical Guidance

Use Tracy when:

- you want live interactive iteration
- you want streaming-style inspection while the process is running

Use Perfetto when:

- you want an artifact to inspect later
- you want timeline zones plus `linux.perf` sampled callstacks in one trace
- you want easier offline sharing / postmortem inspection

Use `STACKFRAME` when:

- you have deliberately marked important zones as `_CS`
- you want per-zone callstacks instead of sampled CPU stacks

Use `LINUX_PERF` when:

- you want coarse zones plus cheap sampled hotspots
- you care about uninstrumented code paths

Notes:

- Perfetto traces are flushed on shutdown, so hard-killing the process may lose
  the trace
- current instrumentation now separates coarse lifecycle/setup visibility from
  hot per-call zones through runtime detail levels
