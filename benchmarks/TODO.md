# Benchmarks - Pending Items

## Working Tests

The following benchmarks run by default (no filter needed):

### Single-agent (C++)
- Node/edge insert, read, update, delete throughput `[THROUGHPUT][single]`
- Node/edge insert, read, update, delete latency+throughput `[THROUGHPUT][LATENCY][single]`
- Concurrent writers throughput `[THROUGHPUT][concurrent]`
- Signal emission latency `[LATENCY][signal]`
- Signal emission under load `[LATENCY][signal][stress]`
- CRDT mvreg operations `[CRDT][mvreg]`
- CRDT dot_context operations `[CRDT][dot_context]`

### Scalability (C++)
- Thread scaling per operation `[SCALABILITY][threads]`
- Graph size impact per operation `[SCALABILITY][graphsize]`
- Graph size impact on performance `[SCALABILITY][memory]`

### Python
- `bench_graph_operations.py` — node/edge CRUD
- `bench_throughput.py` — 5-second throughput+latency windows
- `bench_signals.py` — signal callback latency and throughput
- `bench_binding_overhead.py` — pydsr binding overhead

Run all: `./dsr_benchmarks '~[.multi]'`
Run specific: `./dsr_benchmarks '[CRDT]'`, `./dsr_benchmarks '[THROUGHPUT]'`

## Known Issues

### Multi-agent tests disabled (tag: `.multi`)
- DDS synchronization not working in test environment
- Signals from agent A not propagating to agent B handlers
- Agents discover each other (DDS participant matching works) but data doesn't sync
- Run with `./dsr_benchmarks "[.multi]"` to explicitly test

### API note: insert_node auto-generates IDs
- `DSRGraph::insert_node()` ignores the ID set on the node and generates a new one
- Use `insert_node_with_id` to use a provided ID; check return value to confirm
- The returned `std::optional<uint64_t>` contains the actual assigned ID


## Python Benchmarks
- [ ] Add RT_API transform benchmarks
- [ ] Add InnerEigenAPI spatial transform benchmarks
- [ ] Benchmark Python ↔ C++ data conversion overhead (Eigen matrices, large arrays)
- [ ] Add multi-agent Python benchmarks (multiple DSRGraph instances)

## C++ Benchmarks
- [ ] Add DDS-specific latency benchmarks (network layer)
- [ ] Benchmark different QoS settings impact
- [ ] Add RT_API benchmarks
- [ ] Add Eigen api benchmarks

## Profiling
- [ ] Add Tracy profiler instrumentation (zones for delta propagation, CRDT joins, DDS pub/sub)
- [ ] Create Tracy build option (`WITH_TRACY`)
- [ ] Document Tracy vs perf usage

## Infrastructure (DON'T DO UNLESS EXPLICIT REQUEST)
- [ ] CI integration (run benchmarks on PR, compare with baseline)
- [ ] Historical results tracking
- [ ] Regression detection with configurable thresholds
- [ ] Grafana/dashboard export format

## Documentation
- [ ] Benchmark interpretation guide
- [ ] Performance tuning recommendations based on results
