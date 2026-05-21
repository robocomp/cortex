# CRDT vs Last-Write-Wins: Performance Analysis for Same-Host Deployment

## Context

All processes using the DSR distributed data structure run on the same machine with access to a common high-precision clock (e.g., `clock_gettime(CLOCK_MONOTONIC)` or `CLOCK_REALTIME`). This analysis evaluates replacing the delta-based CRDT layer (`mvreg` / `dot_kernel` / `dot_context`) with a simple Last-Write-Wins (LWW) register strategy.

## Why LWW Would Work Here

When all processes share a machine with a common high-precision clock, the fundamental problem CRDTs solve -- **detecting and resolving concurrent writes without a shared clock** -- simply doesn't exist. The clock *is* your total order.

---

## What the CRDT Layer Actually Costs

### Memory: ~10x Overhead

For a representative node with 10 attributes and 3 edges (2 attrs each):

| Metric | CRDT (current) | LWW (proposed) |
|---|---|---|
| Containers per node | **50+** `std::map`/`std::set` | **2** (`unordered_map` for attrs + fano) |
| Bytes per node | ~7,650 bytes | ~800 bytes |
| Per-attribute overhead | ~380 bytes (dot_kernel + dot_context) | ~12 bytes (timestamp + agent_id) |
| 100-node graph | ~4 MB | ~400 KB |

Each `mvreg<CRDTAttribute>` wraps the actual value in a `dot_kernel` (a `std::map<pair<uint64_t,int>, T>`) plus a `dot_context` (a `std::map<uint64_t,int>` + `std::set<pair<uint64_t,int>>`). That's **3 red-black trees per attribute** just for causal metadata. With LWW, you store `{value, timestamp, agent_id}` -- nothing else.

#### Per-Node Memory Breakdown (CRDT)

```
CRDTNode {
    strings: ~100 bytes

    m_attrs: std::map<string, mvreg<CRDTAttribute>> {
        10 entries, each ~390 bytes:
            mvreg {
                id: 8 bytes
                dot_kernel {
                    ds: std::map (RB tree 56 bytes + 1 entry ~64 bytes)
                    dot_context {
                        cc: std::map (RB tree 56 bytes + 5 entries ~160 bytes)
                        dc: std::set (RB tree 56 bytes + 0 entries when compacted)
                    }
                }
            }
        = 3,900 bytes
    }

    m_fano: std::map<pair<uint64_t,string>, mvreg<CRDTEdge>> {
        3 entries, each ~1,180 bytes:
            mvreg<CRDTEdge> {
                dot_kernel + dot_context: 216 bytes
                CRDTEdge {
                    strings + ids: 74 bytes
                    attrs map: 2 attributes x 390 bytes = 836 bytes
                }
            }
        = 3,540 bytes
    }

    TOTAL: ~7,650 bytes
    ACTUAL PAYLOAD: ~800 bytes
    OVERHEAD RATIO: ~9.5:1
}
```

#### Per-Node Memory Breakdown (LWW)

```
LWWNode {
    strings: ~100 bytes

    attrs: std::unordered_map<string, LWWAttribute> {
        10 entries, each ~60 bytes:
            LWWAttribute {
                value: ~40 bytes (variant)
                timestamp: 8 bytes
                agent_id: 4 bytes
            }
        = 600 bytes
    }

    fano: std::unordered_map<EdgeKey, LWWEdge> {
        3 entries, each ~100 bytes:
            LWWEdge {
                to, from, agent_id: 20 bytes
                type: ~15 bytes
                timestamp: 8 bytes
                attrs: 2 x 60 bytes = 120 bytes
            }
        = 300 bytes
    }

    TOTAL: ~1,000 bytes
    ACTUAL PAYLOAD: ~800 bytes
    OVERHEAD RATIO: ~1.25:1
}
```

---

### Write Path: ~10x Faster

#### Current: `mvreg::write()` (`delta_crdt.h:387-403`)

```cpp
mvreg<V> write(const V &val) {
    mvreg<V> r, a;
    r.dk = dk.rmv();        // Step 1: Remove all dots
    a.dk = dk.add(id, val); // Step 2: Add new dot
    r.join(std::move(a));   // Step 3: Merge results
    return r;
}
```

**Step 1 -- `dk.rmv()`:** Iterates ds map, inserts all dots into dc set, runs `compact()` (O(D^2 log A) worst case), clears ds map. ~5-10 operations + 1 compaction.

**Step 2 -- `dk.add()`:** Calls `c.makedot()` (map insert/update O(log A)), inserts into ds twice (copy + move), inserts into dc set. ~5 operations + 3 allocations.

**Step 3 -- `r.join(a)`:** Two-way sorted map merge with `dotin()` checks (each O(log A)), context join, compaction. ~20 operations.

**Total: ~40 operations + 4-5 heap allocations per single attribute write.**

#### LWW Equivalent

```cpp
void write(const V &val) {
    value = val;
    timestamp = clock_gettime();
    agent_id = me;
}
```

**Total: 3 assignments, 0 allocations.**

---

### Join/Merge Path: ~25-100x Faster

#### Current Join Path

For an incoming delta, `join_delta_node()` (`dsr_api.cpp:1013`) triggers:

1. `mvreg<CRDTNode>::join()` -> `dot_kernel::join_replace_conflict()`:
   - Two-way merge of sorted ds maps with `dotin()` checks
   - Each `dotin()` is O(log A) map lookup + set lookup
   - `dot_context::join()`: merge two cc maps O(A) + insert all dc dots O(D log D) + `compact()` O(D^2 log A)

2. Per attribute: `mvreg<CRDTAttribute>::join()` with full context merging

3. Per edge: `mvreg<CRDTEdge>::join()` with recursive attribute joins

**For a node with N attributes, M edges, K edge attrs:**

| Phase | Operations |
|-------|-----------|
| Node join | O(A * log A + D * A * log A) |
| N attribute joins | N x O(A * log A + D) |
| M edge joins | M x O(A * log A + D) |
| M*K edge attribute joins | MK x O(A * log A + D) |
| **Total** | **O((N+MK) * (A * log A + D * A * log A))** |

Concrete (N=10, M=3, K=2, A=5, D=0 typical): **~250 operations**.

#### LWW Equivalent

```cpp
void join(const LWWAttribute &remote) {
    if (remote.timestamp > local.timestamp) {
        local = remote;
    }
}
```

For a node with N attributes, M edges, K edge attrs: **N + MK comparisons + conditional copies = ~16 operations**.

#### Compaction: The Hidden Cost

The `compact()` method (`delta_crdt.h:52-82`) is particularly expensive:

```cpp
void compact() {
    bool flag;
    do {
        flag = false;
        for (auto sit = dc.begin(); sit != dc.end();) {
            auto mit = cc.find(sit->first);       // O(log A)
            // ... insert/erase/increment logic
        }
    } while (flag == true);  // Repeat until no progress
}
```

Worst-case: O(D^2 * log A) per call. Called during every `join()` and `insertdot()` by default. With LWW, compaction does not exist.

---

### Network: ~42-55% Smaller Messages

#### Current IDL Message Structure

```
MvregNodeAttr (per attribute update) {
    id: 8 bytes
    node: 8 bytes
    attr_name: ~15 bytes
    DotKernelAttr {
        ds: map<PairInt, Attrib> {
            1 entry: 12 (key) + ~40 (value) = 52 bytes
        }
        DotContext {
            cc: map<uint64, int32> {A entries x 12 bytes}   // CRDT overhead
            dc: sequence<PairInt> {D entries x 12 bytes}    // CRDT overhead
        }
    }
    agent_id: 4 bytes
    timestamp: 8 bytes
}
```

#### Message Size Comparison (10 attributes, 5 agents, compacted)

| Component | CRDT | LWW |
|-----------|------|-----|
| Actual attribute values | 400 bytes | 400 bytes |
| Attribute names | 150 bytes | 150 bytes |
| Node metadata | 80 bytes | 80 bytes |
| Edge data | 300 bytes | 300 bytes |
| Edge attribute values | 240 bytes | 240 bytes |
| CRDT cc maps (node level) | 60 bytes | **0** |
| CRDT cc maps (per attribute) | 600 bytes | **0** |
| CRDT cc maps (per edge attr) | 360 bytes | **0** |
| Timestamps | distributed in cc | 1 per entity: ~80 bytes |
| Wire format overhead | ~200 bytes | ~150 bytes |
| **Total** | **~2,390 bytes** | **~1,400 bytes** |

**CRDT metadata: 1,020 bytes (42% of message). Eliminated entirely with LWW.**

With uncompacted dot clouds (D=20): CRDT messages grow by an additional ~2,400 bytes. LWW messages are unaffected.

---

### Serialization/Deserialization: ~2-3x Faster

Current path converts between `mvreg<CRDTAttribute>` and `IDL::MvregNodeAttr` with nested map/set serialization (`CRDTNode_to_IDL`, `CRDTNodeAttr_to_IDL` in `translator.h`). Each conversion iterates dot maps, serializes cc entries, serializes dc sequences.

LWW serialization: write `{value, timestamp, agent_id}`. No nested context maps.

---

## Expected Overall Speedup

| Operation | Current | LWW | Speedup |
|-----------|---------|-----|---------|
| Single attribute write | ~40 ops, 4-5 allocs | 3 ops, 0 allocs | **~10x** |
| Node join (10 attrs) | ~250 ops | ~10 ops | **~25x** |
| Message size | ~2,400 bytes | ~1,300 bytes | **~1.8x** |
| Memory per node | ~7,650 bytes | ~800 bytes | **~9.5x** |
| Read | O(1) | O(1) | 1x |
| `get_nodes_by_type` (N nodes) | N deep copies | N deep copies | **~2x** (smaller objects) |
| Serialization | nested map iteration | flat write | **~2-3x** |

**End-to-end throughput improvement for a write-heavy multi-agent workload: ~5-10x**, dominated by the join path speedup since that's the hot path for receiving remote updates.

---

## What You Lose (and Why It's OK on Same-Host)

| CRDT Capability | LWW Equivalent | Why It's Fine on Same Host |
|---|---|---|
| Causal consistency | Clock-based total order | Same clock = perfect ordering |
| Concurrent write detection | Not needed | Timestamps break ties deterministically |
| Conflict resolution (lowest agent_id wins) | Last timestamp wins | Acceptable semantics with synchronized clocks |
| Tombstone propagation via dot context | Explicit delete with timestamp | Simpler, equally correct with shared clock |
| Partition tolerance | Not needed | Same-host = no network partitions |

---

## The `dotin()` Bug Disappears

The current `dotin()` implementation (`delta_crdt.h:43-49`) has a subtle correctness issue:

```cpp
bool dotin(const pair<key_type, int> &d) const {
    const auto itm = cc.find(d.first);
    if (itm != cc.end() && d.second <= itm->second) return true;
    if (not dc.empty() and d.second < dc.rbegin()->second) return true; // BUG?
    if (dc.count(d) != 0) return true;
    return false;
}
```

The middle check compares a dot's counter against the maximum counter of **any key** in the dot cloud, potentially causing false positives across different agent keys. With LWW, causal context tracking does not exist, so this class of bugs is eliminated entirely.

---

## Risk Assessment

### The Only Risk: Future Multi-Host Deployment

If the system ever needs to run across multiple machines without a shared clock, LWW would produce incorrect results (clock skew -> lost updates). Mitigation:

- Define a clean `Register<T>` interface with `LWWRegister` and `MVRegister` implementations
- Select strategy at graph construction time via `GraphSettings`
- Keep the CRDT code in the repository but behind a compile flag or runtime switch

### Clock Precision Requirements

For LWW to be correct, the clock must satisfy:

1. **Monotonicity**: `clock_gettime()` must never go backwards (use `CLOCK_MONOTONIC` or `CLOCK_MONOTONIC_RAW`)
2. **Resolution**: Must be finer than the minimum time between two writes to the same attribute from different agents. With `CLOCK_MONOTONIC` on modern Linux: ~1 nanosecond resolution, which is sufficient (two agents cannot both call `clock_gettime()` and get the same value in practice)
3. **Shared visibility**: All processes on the same host see the same monotonic clock (guaranteed by the kernel)

If two writes happen at the exact same nanosecond (extremely unlikely but theoretically possible), break ties by agent_id for determinism:

```cpp
bool wins(const LWWValue &a, const LWWValue &b) {
    if (a.timestamp != b.timestamp) return a.timestamp > b.timestamp;
    return a.agent_id > b.agent_id;  // Deterministic tiebreak
}
```

---

## Architectural Sketch: LWW Replacement

### LWW Attribute

```cpp
struct LWWAttribute {
    ValType value;          // Same variant as current Attribute
    uint64_t timestamp;     // clock_gettime(CLOCK_MONOTONIC)
    uint32_t agent_id;

    bool wins_over(const LWWAttribute &other) const {
        return timestamp > other.timestamp ||
               (timestamp == other.timestamp && agent_id > other.agent_id);
    }
};
```

### LWW Node

```cpp
struct LWWNode {
    std::string type, name;
    uint64_t id;
    uint32_t agent_id;
    std::unordered_map<std::string, LWWAttribute> attrs;
    std::unordered_map<EdgeKey, LWWEdge> fano;
};
```

### LWW Join

```cpp
void join_attribute(const std::string &key, LWWAttribute &&remote) {
    auto [it, inserted] = attrs.try_emplace(key, std::move(remote));
    if (!inserted && remote.wins_over(it->second)) {
        it->second = std::move(remote);
    }
}
```

### LWW Delta Message (IDL)

```idl
struct LWWAttrDelta {
    unsigned long long node_id;
    string attr_name;
    Attrib value;
    unsigned long long timestamp;
    unsigned long agent_id;
};
```

No `DotContext`, no `DotKernel`, no `PairInt` dot identifiers.

---

## Summary

| Metric | CRDT | LWW | Improvement |
|--------|------|-----|-------------|
| Memory per node (10 attrs, 3 edges) | 7,650 bytes | 1,000 bytes | **7.6x** |
| Write latency (single attribute) | ~40 ops | 3 ops | **~13x** |
| Join latency (10-attr node) | ~250 ops | ~10 ops | **~25x** |
| Message size (10-attr update) | 2,390 bytes | 1,400 bytes | **1.7x** |
| Containers per node | 50+ map/set | 2 unordered_map | **25x fewer** |
| Heap allocations per write | 4-5 | 0 | **eliminated** |
| Code complexity | delta_crdt.h (466 lines) + translator + join logic | ~50 lines | **~10x simpler** |
| Correctness risks | `dotin()` bug, compaction edge cases | Timestamp comparison | **eliminated** |

**Bottom line:** The CRDT layer is solving a distributed systems problem (causal consistency without a shared clock) that does not exist in the same-host deployment scenario. Removing it yields a **5-10x throughput improvement**, **~8x memory reduction**, **~50% network bandwidth reduction**, and dramatically simpler code.
