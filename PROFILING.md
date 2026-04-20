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

#### Callstack mode (`CORTEX_PERFETTO_MODE`)

Three modes are available, selected at configure time:

| Mode | CMake flag | What you get |
|------|-----------|--------------|
| `DEFAULT` | _(omit)_ | Zone events only, no callstacks |
| `STACKFRAME` | `-DCORTEX_PERFETTO_MODE=STACKFRAME` | Callstacks attached to `CORTEX_PROFILE_ZONE_CS` zones via `callstack_iid` + `InternedData` (native flamechart in the UI) |
| `LINUX_PERF` | `-DCORTEX_PERFETTO_MODE=LINUX_PERF` | Kernel-driven CPU sampling correlated to zone events by timestamp |

**STACKFRAME** captures a callstack at every `CORTEX_PROFILE_ZONE_CS` call site using `backtrace()`, resolves symbols with `dladdr` + demangling, and emits them as proper `Callstack` / `Frame` / `InternedString` proto entries. The Perfetto UI renders these as an expandable flamechart on the event. `-fno-omit-frame-pointer` is added automatically.

**LINUX_PERF** adds a `linux.perf` data source alongside track events. The kernel interrupts the process at 1 kHz and records the CPU stack independently of instrumentation — useful for finding hotspots in uninstrumented code. `-fno-omit-frame-pointer` is added automatically.

Requirements for `LINUX_PERF`:

- `traced`, `traced_probes`, and `traced_perf` must be running (all three)
- `perf_event_paranoid <= 0`:
  ```bash
  sudo sysctl -w kernel.perf_event_paranoid=0
  ```
- The `tracebox` binary (not its symlink) needs `CAP_SYS_PTRACE` for DWARF stack unwinding. `readlink -f` may not resolve the real path — locate it manually if needed:
  ```bash
  # find the actual binary (example path — yours may differ)
  ls -la $(which tracebox)          # shows where symlink points
  sudo setcap cap_perfmon,cap_sys_ptrace+ep /home/jc/.local/share/perfetto/prebuilts/tracebox
  ```
- Run `traced_perf` as root if `setcap` is not an option (root can open `/proc/<pid>/mem` and connect to user-owned sockets):
  ```bash
  tracebox traced --background        # as your user
  tracebox traced_probes --background # as your user
  sudo tracebox traced_perf           # as root
  ```

**Important — LINUX_PERF vs DEFAULT/STACKFRAME modes:**

LINUX_PERF mode registers the process as a system-backend producer only. It does **not** write a `.pftrace` file itself — the trace is owned by `traced` and read back at process exit via the consumer API. Zone events and CPU samples land in the same file and are visible as separate track groups in the UI ("Process callstacks cpu-clock" for samples, thread rows for zones). To correlate them, expand both groups and select an area that spans both.

#### Typical usage

DEFAULT (zone events only, no daemons needed):

```bash
cmake -S . -B build-perfetto \
  -DWITH_TESTS=ON \
  -DCORTEX_PROFILING_BACKEND=PERFETTO
cmake --build build-perfetto --target tests
./build-perfetto/tests/tests "[SYNCHRONIZATION][GRAPH]"
```

STACKFRAME (callstacks on `_CS` zones, no daemons needed):

```bash
cmake -S . -B build-perfetto \
  -DWITH_TESTS=ON \
  -DCORTEX_PROFILING_BACKEND=PERFETTO \
  -DCORTEX_PERFETTO_MODE=STACKFRAME
cmake --build build-perfetto --target tests
./build-perfetto/tests/tests "[SYNCHRONIZATION][GRAPH]"
```

LINUX_PERF (kernel CPU sampling correlated with zone events):

```bash
# One-time setup:
sudo sysctl -w kernel.perf_event_paranoid=0
sudo setcap cap_perfmon,cap_sys_ptrace+ep /path/to/real/tracebox

# Start daemons (traced_perf as root OR with setcap applied):
tracebox traced --background
tracebox traced_probes --background
tracebox traced_perf --background   # or: sudo tracebox traced_perf

cmake -S . -B build-pf-lp \
  -DWITH_TESTS=ON \
  -DCORTEX_PROFILING_BACKEND=PERFETTO \
  -DCORTEX_PERFETTO_MODE=LINUX_PERF
cmake --build build-pf-lp -j$(nproc)
./build-pf-lp/bin/your_component
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
