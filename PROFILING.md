# Profiling Backends

The repository supports multiple profiling backends through CMake.

## Backend Selection

Use:

```bash
-DCORTEX_PROFILING_BACKEND=NONE
-DCORTEX_PROFILING_BACKEND=TRACY
-DCORTEX_PROFILING_BACKEND=PERFETTO
```

### `NONE`

No profiling backend is enabled.

### `TRACY`

Uses the interactive Tracy client/profiler flow.

If `CORTEX_ENABLE_TRACY` is not explicitly configured, the build falls back to the `DEFAULT` Tracy preset.

What you need:

- an instrumented build with `-DCORTEX_PROFILING_BACKEND=TRACY`
- the Tracy Profiler GUI
- a live connection between the running process and the profiler

Important:

- Tracy is mainly an interactive workflow
- traces are not written automatically to disk by the client
- if you want a saved trace, connect with the profiler and save the session from the UI

Typical usage:

```bash
cmake -S . -B build \
  -DWITH_TESTS=ON \
  -DCORTEX_PROFILING_BACKEND=TRACY \
  -DCORTEX_ENABLE_TRACY=DEFAULT
cmake --build build --target tests
./build/tests/tests "[SYNCHRONIZATION][GRAPH]"
```

Tracy options are OFF, ON, DEFAULT, MEDIUM, EXTRA, WSL, WSL_EXTRA

### `PERFETTO`

Uses Perfetto's in-process backend and writes an offline trace file automatically at process exit.

What you need:

- an instrumented build with `-DCORTEX_PROFILING_BACKEND=PERFETTO`
- a generated `.pftrace` file
- either the web UI at `https://ui.perfetto.dev/` or another Perfetto-compatible viewer

Typical usage:

```bash
cmake -S . -B build-perfetto \
  -DWITH_TESTS=ON \
  -DCORTEX_PROFILING_BACKEND=PERFETTO
cmake --build build-perfetto --target tests
export CORTEX_PERFETTO_TRACE_FILE=.artifacts/output.pftrace
./build-perfetto/tests/tests "[SYNCHRONIZATION][GRAPH]"
```

The trace file path can be overridden with:

```bash
export CORTEX_PERFETTO_TRACE_FILE=/path/to/output.pftrace
```

If not set, the default output path is:

```text
.artifacts/perfetto/<executable>-<pid>-<timestamp>.pftrace
```

relative to the process working directory.

The generated trace file can be opened with:

- https://ui.perfetto.dev/
- the standalone Perfetto UI / trace processor tools

Recommended flow on this repository:

1. Run the instrumented binary or test.
2. Wait for the process to exit cleanly.
3. Open `https://ui.perfetto.dev/`.
4. Use `Open trace file` and load the `.pftrace`.

Notes:

- Perfetto is the better fit for offline captures that you want to inspect later.
- The trace is flushed on shutdown, so if the process is killed hard you may lose the file.
- The current instrumentation covers graph, RT, CRDT, bootstrap, and synchronization paths.
