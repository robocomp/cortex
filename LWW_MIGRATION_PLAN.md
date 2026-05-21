# LWW Migration Plan: Dual-Strategy Design

Both CRDT and LWW must coexist. The strategy is selected at graph construction time via `GraphSettings`. Existing CRDT code is preserved unchanged.

---

## Guiding Principles

1. **The CRDT path must remain untouched and fully functional.** Zero modifications to `delta_crdt.h`, existing `crdt_types.h`, or the current `dsr_api.cpp` logic.
2. **The public API (`Node`, `Edge`, `Attribute`, `DSRGraph`) must not change.** User agents compile without modification regardless of which strategy is active.
3. **Strategy is selected at construction time** via a new field in `GraphSettings`. All agents on the same network must use the same strategy (enforced by incompatible DDS topic types).
4. **Each step must leave the system compilable and testable.** The CRDT path passes all existing tests at every step.
5. **No code duplication of shared infrastructure.** Cache maps, signals, locks, DDS participant setup, subscription thread structure, and the full public API are shared.

---

## Architecture: Strategy Pattern

### The Problem

The CRDT-specific logic in `DSRGraph` is concentrated in ~15 internal methods that touch `mvreg` operations (`write`, `read_reg`, `empty`, `reset`, `join`). These methods also interact with:
- The `nodes` map (whose value type differs: `mvreg<CRDTNode>` vs `LWWEntry`)
- IDL types for serialization (different wire formats)
- DDS publishers (different message types)
- The unprocessed delta buffers (different stored types)

Everything else is shared: cache maps, signals, mutexes, user-facing API, DDS participant, subscription thread lifecycle.

### The Solution

Extract the 15 strategy-dependent methods into a virtual **`SyncEngine`** interface. `DSRGraph` holds a `unique_ptr<SyncEngine>` and delegates to it. Two implementations:

```
                    ┌──────────────────────────────────────────┐
                    │              DSRGraph                     │
                    │  (public API, signals, cache maps, locks, │
                    │   DDS participant, subscription threads)  │
                    │                                          │
                    │   unique_ptr<SyncEngine> engine_;        │
                    └──────────────┬───────────────────────────┘
                                   │
                          ┌────────┴────────┐
                          │   SyncEngine    │  (virtual interface)
                          │                │
                          │  get_node_()   │
                          │  insert_node_()│
                          │  update_node_()|
                          │  delete_node_()|
                          │  join_delta_*  │
                          │  ...           │
                          └───────┬────────┘
                        ┌─────────┴──────────┐
                        │                    │
               ┌────────┴───────┐   ┌───────┴────────┐
               │ CRDTSyncEngine │   │ LWWSyncEngine  │
               │                │   │                │
               │ nodes: map<id, │   │ nodes: map<id, │
               │  mvreg<Node>>  │   │  LWWEntry>     │
               │                │   │                │
               │ delta_crdt.h   │   │ lww_types.h    │
               │ translator.h   │   │ lww_translator │
               │ CRDT IDL types │   │ LWW IDL types  │
               └────────────────┘   └────────────────┘
```

### What the SyncEngine Owns

- **Node storage** (the `nodes` map with its strategy-specific value type)
- **Unprocessed delta buffers** (different stored types per strategy)
- **All 15 internal methods** that touch mvreg or LWW operations
- **IDL serialization** (strategy-specific translator functions)
- **DDS publishers** for strategy-specific message types

### What DSRGraph Keeps

- Public API methods (`get_node`, `insert_node`, `update_node`, `delete_node`, etc.)
- Cache maps (`name_map`, `id_map`, `nodeType`, `edgeType`, `edges`, `to_edges`, `deleted`)
- Mutexes (`_mutex`, `_mutex_cache_maps`)
- Signal emission infrastructure
- DDS participant, subscriber setup, subscription thread lifecycle
- ThreadPools
- User type conversions (these only touch `Node`, `Edge`, `Attribute`)
- JSON read/write
- Graph copy mechanism

---

## Step 0: Preparation

### 0.1 Create a feature branch

Branch from `development`. All work happens on `feature/lww-strategy`.

### 0.2 Establish baseline benchmarks

Run the existing benchmark suite and record CRDT results. These are the baseline.

### 0.3 Run all tests to confirm green baseline

```bash
cmake -DWITH_TESTS=ON -DWITH_BENCHMARKS=ON ..
make -j && ctest --output-on-failure
```

---

## Step 1: Define the SyncEngine Interface

**Files to create:**
- `api/include/dsr/api/dsr_sync_engine.h`

### 1.1 Define the abstract interface

The interface must expose the 15 internal methods that DSRGraph currently calls directly. It must also expose a way for DSRGraph to read node data (for cache map updates, signal emission, and user-facing conversions).

Key methods to extract:

```
class SyncEngine {
public:
    virtual ~SyncEngine() = default;

    // --- Node storage access ---
    // Returns a CRDTNode copy (the common internal type) or nullopt
    virtual std::optional<CRDTNode> get_node(uint64_t id) = 0;
    virtual bool node_exists(uint64_t id) = 0;
    virtual size_t node_count() = 0;
    virtual void for_each_node(std::function<void(uint64_t, const CRDTNode&)>) = 0;

    // --- Edge access ---
    virtual std::optional<CRDTEdge> get_edge(uint64_t from, uint64_t to,
                                              const std::string& key) = 0;

    // --- Local write operations ---
    // Return IDL-agnostic results; publishing handled by DSRGraph
    virtual InsertNodeResult insert_node(CRDTNode&& node) = 0;
    virtual UpdateNodeResult update_node(CRDTNode&& node) = 0;
    virtual DeleteNodeResult delete_node(uint64_t id) = 0;
    virtual InsertEdgeResult insert_or_assign_edge(CRDTEdge&& edge,
                                                    uint64_t from, uint64_t to) = 0;
    virtual DeleteEdgeResult delete_edge(uint64_t from, uint64_t to,
                                          const std::string& key) = 0;

    // --- Network join operations ---
    // Called from subscription threads with raw DDS data
    virtual void join_delta_node(eprosima::fastdds::dds::DataReader* reader) = 0;
    virtual void join_delta_edge(eprosima::fastdds::dds::DataReader* reader) = 0;
    virtual void join_delta_node_attr(eprosima::fastdds::dds::DataReader* reader) = 0;
    virtual void join_delta_edge_attr(eprosima::fastdds::dds::DataReader* reader) = 0;

    // --- Full graph sync ---
    virtual void serve_full_graph(DSRPublisher& pub) = 0;
    virtual void join_full_graph(eprosima::fastdds::dds::DataReader* reader) = 0;

    // --- DDS topic setup ---
    virtual void register_topics(DSRParticipant& participant) = 0;
    virtual void publish_node_delta(/* strategy-specific delta */) = 0;
    // ... etc
};
```

### 1.2 Design the result types

The write operations need to return enough information for DSRGraph to:
1. Publish the delta over DDS (strategy-specific — so the engine publishes internally)
2. Emit signals (needs: node id, type, changed attribute names, edge from/to/type)
3. Update cache maps (needs: id, name, type, edge keys)

Since the delta format is strategy-specific, the engine should handle DDS publishing internally. The result types only carry signal/cache data:

```
struct InsertNodeResult {
    bool success;
    // Signal data (only if success):
    uint64_t id;
    std::string type;
    std::vector<std::pair<uint64_t, std::string>> edges; // fano keys
};

struct UpdateNodeResult {
    bool success;
    std::vector<std::string> changed_attrs;
};

struct DeleteNodeResult {
    bool success;
    std::vector<Edge> deleted_edges;       // for signal emission
    std::optional<Node> deleted_node;      // for deleted_node_signal
};

// ... similar for edge results
```

### 1.3 Design the callback interface

The engine needs to call back into DSRGraph for:
- Cache map updates (`update_maps_node_insert`, `update_maps_node_delete`, etc.)
- Signal emission
- Accessing the `deleted` set
- Acquiring locks

Define a `SyncEngineHost` interface that DSRGraph implements:

```
class SyncEngineHost {
public:
    virtual void update_maps_node_insert(uint64_t id, const CRDTNode& n) = 0;
    virtual void update_maps_node_delete(uint64_t id, const std::optional<CRDTNode>& n) = 0;
    virtual void update_maps_edge_insert(uint64_t from, uint64_t to, const std::string& key) = 0;
    virtual void update_maps_edge_delete(uint64_t from, uint64_t to, const std::string& key) = 0;

    virtual bool is_deleted(uint64_t id) = 0;
    virtual void mark_deleted(uint64_t id) = 0;

    virtual std::shared_mutex& mutex() = 0;
    virtual std::shared_mutex& cache_mutex() = 0;

    virtual signals_fns& emitter() = 0;
    virtual uint32_t agent_id() = 0;
    virtual bool is_copy() = 0;
};
```

### Checkpoint: interface header compiles. No other files changed. All existing tests pass.

---

## Step 2: Add `SyncMode` to `GraphSettings`

**Files to modify:**
- `api/include/dsr/api/dsr_graph_settings.h`

### 2.1 Add the enum

```cpp
enum struct SyncMode : uint8_t {
    CRDT = 0,   // Default: delta-based CRDT (current behavior)
    LWW  = 1,   // Last-Write-Wins (same-host only)
};
```

### 2.2 Add the field

```cpp
struct GraphSettings {
    // ... existing fields ...
    SyncMode sync_mode = SyncMode::CRDT;  // default preserves current behavior
};
```

### Checkpoint: compiles, no behavioral change. All existing tests pass.

---

## Step 3: Extract CRDTSyncEngine from DSRGraph

This is a **refactor-only** step. No new functionality. Move the 15 internal methods and the `nodes` map from `DSRGraph` into `CRDTSyncEngine`, which implements `SyncEngine`.

**Files to create:**
- `api/include/dsr/api/dsr_crdt_sync_engine.h`
- `api/dsr_crdt_sync_engine.cpp`

**Files to modify:**
- `api/include/dsr/api/dsr_api.h` (remove moved members, add `unique_ptr<SyncEngine>`)
- `api/dsr_api.cpp` (remove moved method bodies, delegate to engine)

### 3.1 Move the `nodes` map

Move from `DSRGraph`:
```cpp
Nodes nodes;  // unordered_map<uint64_t, mvreg<CRDTNode>>
```

Into `CRDTSyncEngine`:
```cpp
class CRDTSyncEngine : public SyncEngine {
    Nodes nodes;
    // ... unprocessed delta maps ...
};
```

### 3.2 Move the 15 internal methods

Move these method bodies from `dsr_api.cpp` into `dsr_crdt_sync_engine.cpp`:
- `get_()` → `CRDTSyncEngine::get_node()`
- `get_edge_()` → `CRDTSyncEngine::get_edge()`
- `insert_node_()` → `CRDTSyncEngine::insert_node()`
- `update_node_()` → `CRDTSyncEngine::update_node()`
- `delete_node_()` → `CRDTSyncEngine::delete_node()`
- `insert_or_assign_edge_()` → `CRDTSyncEngine::insert_or_assign_edge()`
- `delete_edge_()` → `CRDTSyncEngine::delete_edge()`
- `join_delta_node()` → `CRDTSyncEngine::join_delta_node()`
- `join_delta_edge()` → `CRDTSyncEngine::join_delta_edge()`
- `join_delta_node_attr()` → `CRDTSyncEngine::join_delta_node_attr()`
- `join_delta_edge_attr()` → `CRDTSyncEngine::join_delta_edge_attr()`
- `process_delta_edge()` → stays private in CRDTSyncEngine
- `process_delta_node_attr()` → stays private in CRDTSyncEngine
- `process_delta_edge_attr()` → stays private in CRDTSyncEngine
- `join_full_graph()` → `CRDTSyncEngine::join_full_graph()`
- `Map()` → `CRDTSyncEngine::serialize_graph()`

### 3.3 Move the unprocessed delta maps

These four maps move from `DSRGraph` to `CRDTSyncEngine`:
```cpp
std::unordered_multimap<uint64_t, std::tuple<std::string, mvreg<CRDTAttribute>, uint64_t>>
    unprocessed_delta_node_att;
std::unordered_multimap<uint64_t, std::tuple<uint64_t, std::string, mvreg<CRDTEdge>, uint64_t>>
    unprocessed_delta_edge_from;
std::unordered_multimap<uint64_t, std::tuple<uint64_t, std::string, mvreg<CRDTEdge>, uint64_t>>
    unprocessed_delta_edge_to;
std::unordered_multimap<std::tuple<uint64_t, uint64_t, std::string>,
    std::tuple<std::string, mvreg<CRDTAttribute>, uint64_t>, hash_tuple>
    unprocessed_delta_edge_att;
```

### 3.4 Move DDS publishers for strategy-specific topics

The four delta publishers move into the engine (node, edge, node_attrs, edge_attrs). The graph_request/answer publishers stay in DSRGraph (they are strategy-independent).

### 3.5 Update DSRGraph to delegate

Replace direct method calls with engine delegation:
```cpp
// Before:
std::optional<CRDTNode> DSRGraph::get_(uint64_t id) {
    auto it = nodes.find(id);
    if (it != nodes.end() and !it->second.empty())
        return std::make_optional(it->second.read_reg());
    return {};
}

// After:
std::optional<CRDTNode> DSRGraph::get_(uint64_t id) {
    return engine_->get_node(id);
}
```

### 3.6 Verify: pure refactor

The CRDTSyncEngine method bodies must be **identical** to the original code, just receiving `host` callbacks instead of accessing `DSRGraph` members directly. The engine accesses cache maps and signals through the `SyncEngineHost` interface.

### Checkpoint: all existing tests pass with CRDTSyncEngine. Behavior is identical. This is the most critical step — if tests pass here, the abstraction is correct.

---

## Step 4: Define LWW Internal Types

**Files to create:**
- `core/include/dsr/core/types/lww_types.h`

### 4.1 `LWWNode`

```cpp
class LWWNode {
    std::string m_type, m_name;
    uint64_t m_id;
    uint32_t m_agent_id;
    uint64_t m_timestamp;    // node-level timestamp
    std::unordered_map<std::string, Attribute> m_attrs;
    std::unordered_map<std::pair<uint64_t, std::string>, LWWEdge, hash_tuple> m_fano;
};
```

Provide the same accessor API as `CRDTNode` (`type()`, `name()`, `id()`, `attrs()`, `fano()`) so that:
- User type constructors (`Node(const LWWNode&)`) can work
- Cache map update functions can work
- The SyncEngine interface can return `CRDTNode` by converting from `LWWNode` (or we generalize the interface — see 4.3)

### 4.2 `LWWEdge`

```cpp
class LWWEdge {
    uint64_t m_to, m_from;
    std::string m_type;
    uint32_t m_agent_id;
    uint64_t m_timestamp;
    std::unordered_map<std::string, Attribute> m_attrs;
};
```

### 4.3 Interface return type decision

**Option A**: SyncEngine always returns `CRDTNode`/`CRDTEdge`, and LWWSyncEngine converts internally. Simple but adds a conversion cost.

**Option B**: SyncEngine returns `Node`/`Edge` (user types). These are strategy-independent. This means the engine does the internal→user conversion, not DSRGraph.

**Option C**: Template SyncEngine on the internal node type. Adds compile-time complexity but zero runtime cost.

**Recommended: Option B.** The SyncEngine returns user-facing `Node`/`Edge` objects. DSRGraph already wants to return `Node`/`Edge` to the user, so the conversion happens once inside the engine. The cache map update functions only need `id`, `name`, `type`, and edge keys — these can be passed as arguments or extracted from `Node`.

This means the SyncEngine interface from Step 1 changes slightly:
```
virtual std::optional<Node> get_node(uint64_t id) = 0;
virtual std::optional<Edge> get_edge(uint64_t from, uint64_t to, const std::string& key) = 0;
```

For internal operations that need the raw node (cache map updates), the engine calls the host callbacks with the data directly.

### 4.4 LWW timestamp comparison function

```cpp
struct LWWClock {
    static bool wins(uint64_t ts_a, uint32_t agent_a,
                     uint64_t ts_b, uint32_t agent_b) {
        if (ts_a != ts_b) return ts_a > ts_b;
        return agent_a > agent_b;   // deterministic tiebreak
    }
};
```

### Checkpoint: new types compile. No code uses them yet. All existing tests pass.

---

## Step 5: Add LWW IDL Types

**Files to modify:**
- `core/topics/IDLGraph.idl` — **append** new types, do NOT modify existing ones

### 5.1 Add LWW delta types alongside existing CRDT types

Append to the end of `IDLGraph.idl`:

```idl
// ============================================================
// LWW (Last-Write-Wins) message types — same-host deployment
// ============================================================

struct LWWNodeAttrDelta {
    unsigned long long node_id;
    string attr_name;
    Attrib value;
    unsigned long agent_id;
    unsigned long long timestamp;
    boolean is_delete;
};

struct LWWEdgeAttrDelta {
    unsigned long long from;
    unsigned long long to;
    string edge_type;
    string attr_name;
    Attrib value;
    unsigned long agent_id;
    unsigned long long timestamp;
    boolean is_delete;
};

struct LWWEdgeDelta {
    unsigned long long from;
    unsigned long long to;
    string edge_type;
    map<string, Attrib> attrs;
    unsigned long agent_id;
    unsigned long long timestamp;
    boolean is_delete;
};

struct LWWNodeDelta {
    unsigned long long id;
    string type;
    string name;
    unsigned long agent_id;
    unsigned long long timestamp;
    boolean is_delete;
    map<string, Attrib> attrs;
    map<EdgeKey, LWWEdgeDelta> fano;
};

struct LWWFullGraph {
    map<unsigned long long, LWWNodeDelta> nodes;
};
```

### 5.2 Regenerate IDL code

```bash
fastddsgen -replace core/topics/IDLGraph.idl
```

The existing CRDT types (`DotContext`, `DotKernel`, `MvregNode`, etc.) are unchanged. New LWW types are added alongside them.

### 5.3 Register new PubSubTypes

The generated code will include new PubSubType classes for each LWW struct. These will be registered on **separate DDS topics** by `LWWSyncEngine` (not the same topics as CRDT).

### Checkpoint: IDL regenerated. Old CRDT types unchanged. New LWW types available. All existing tests pass.

---

## Step 6: Create LWW Translator

**Files to create:**
- `core/include/dsr/core/types/lww_translator.h`

### 6.1 LWWNode ↔ IDL conversions

These are trivial field copies — no dot context, no dot kernel:

```
LWWNodeDelta node_to_lww_idl(const LWWNode& node);
LWWNode lww_idl_to_node(LWWNodeDelta&& delta);
```

### 6.2 LWWNode ↔ User type conversions

```
Node lww_node_to_user(const LWWNode& node);
LWWNode user_to_lww_node(const Node& node, uint64_t timestamp, uint32_t agent_id);
```

### 6.3 Attribute / Edge variants

Same trivial pattern for edges and attributes.

### Checkpoint: translator compiles. No code uses it yet. All existing tests pass.

---

## Step 7: Implement LWWSyncEngine

**Files to create:**
- `api/include/dsr/api/dsr_lww_sync_engine.h`
- `api/dsr_lww_sync_engine.cpp`

This is the core implementation step. Each method is dramatically simpler than its CRDT counterpart.

### 7.1 Node storage

```cpp
class LWWSyncEngine : public SyncEngine {
    std::unordered_map<uint64_t, LWWNode> nodes;

    // Unprocessed delta buffers (same structure, no mvreg)
    std::unordered_multimap<uint64_t,
        std::tuple<std::string, Attribute, uint64_t>> unprocessed_delta_node_att;
    // ... similar for edges ...
};
```

### 7.2 `get_node(uint64_t id)`

```
auto it = nodes.find(id);
if (it != nodes.end())
    return lww_node_to_user(it->second);
return {};
```

### 7.3 `insert_node(LWWNode&& node)` (called via user→LWW conversion)

```
1. Check host->is_deleted(id) — if deleted with newer timestamp, reject
2. nodes[id] = std::move(node)
3. host->update_maps_node_insert(id, ...)
4. Create LWWNodeDelta, publish via DDS
5. Return InsertNodeResult with signal data
```

### 7.4 `update_node(LWWNode&& node)`

```
1. Find existing in nodes map
2. For each incoming attribute:
   - If newer timestamp: replace
   - Add to changed list
3. For each existing attribute not in incoming: remove
4. Create vector<LWWNodeAttrDelta> for changes
5. Publish via DDS
6. Return UpdateNodeResult with changed attr names
```

### 7.5 `delete_node(uint64_t id)`

```
1. Snapshot node for signals
2. Collect outgoing + incoming edges for cascade delete
3. Remove from nodes map
4. host->mark_deleted(id)
5. host->update_maps_node_delete(id, ...)
6. Create LWWNodeDelta{is_delete=true}, publish
7. Create LWWEdgeDelta{is_delete=true} for each cascade, publish
8. Return DeleteNodeResult
```

### 7.6 `join_delta_node(DataReader* reader)`

```
1. Deserialize LWWNodeDelta from DDS
2. Acquire unique_lock via host->mutex()
3. If is_delete:
   - If node exists and delete timestamp > node.timestamp: delete + signal
   - Else: ignore
4. If not is_delete:
   - If deleted with newer timestamp: ignore
   - If deleted with older timestamp: resurrect
   - Apply LWW merge per attribute (timestamp comparison)
   - host->update_maps_*
   - Consume unprocessed deltas
5. Release lock
6. Emit signals via host->emitter()
```

### 7.7 Join methods for edges and attributes

Same pattern: deserialize, timestamp comparison, apply if newer.

### 7.8 `join_full_graph(DataReader* reader)`

```
1. Deserialize LWWFullGraph
2. For each node:
   - Apply same logic as join_delta_node but in batch
   - No publishing (we're receiving)
3. Consume unprocessed deltas
```

### 7.9 DDS topic setup

LWWSyncEngine uses **different DDS topic names** than CRDTSyncEngine to prevent cross-talk:

```
CRDT topics: "DSR_NODE", "DSR_EDGE", "DSR_NODE_ATTRS", "DSR_EDGE_ATTRS"
LWW topics:  "DSR_LWW_NODE", "DSR_LWW_EDGE", "DSR_LWW_NODE_ATTRS", "DSR_LWW_EDGE_ATTRS"
```

If a CRDT agent and an LWW agent accidentally end up on the same DDS domain, they simply don't see each other's messages — safe failure mode.

### Checkpoint: LWWSyncEngine compiles. Unit tests for LWW logic can be written against it directly.

---

## Step 8: Wire DSRGraph to Select Engine

**Files to modify:**
- `api/include/dsr/api/dsr_api.h`
- `api/dsr_api.cpp`

### 8.1 Add engine member

```cpp
class DSRGraph : public QObject, public SyncEngineHost {
    // ...
    std::unique_ptr<SyncEngine> engine_;
};
```

### 8.2 Construct the right engine in DSRGraph constructor

```cpp
DSRGraph::DSRGraph(GraphSettings settings) : /* ... */ {
    if (settings.sync_mode == SyncMode::LWW) {
        engine_ = std::make_unique<LWWSyncEngine>(this);
    } else {
        engine_ = std::make_unique<CRDTSyncEngine>(this);
    }
    // ... rest of constructor (DDS setup, file load, threads) ...
}
```

### 8.3 Implement SyncEngineHost in DSRGraph

DSRGraph implements the host interface, exposing cache maps and signals to the engine:

```cpp
void DSRGraph::update_maps_node_insert(uint64_t id, const CRDTNode& n) {
    // Existing code, unchanged
}
// ... etc ...
```

These methods are already implemented in DSRGraph. They just need to be exposed via the `SyncEngineHost` interface.

### 8.4 Update subscription thread callbacks

The subscription threads currently call hardcoded CRDT join methods. Change them to delegate to the engine:

```cpp
void DSRGraph::node_subscription_thread() {
    // ... DDS reader setup ...
    // Callback:
    engine_->join_delta_node(reader);
}
```

### 8.5 Update the public API methods

Public methods like `get_node()`, `insert_node()`, etc. delegate to the engine:

```cpp
std::optional<Node> DSRGraph::get_node(uint64_t id) {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return engine_->get_node(id);
}
```

### Checkpoint: DSRGraph compiles with both engines. `SyncMode::CRDT` produces identical behavior to before. `SyncMode::LWW` is functional.

---

## Step 9: Update the Clock for LWW

**Files to modify:**
- `core/include/dsr/core/utils.h`

### 9.1 Add a monotonic clock function (don't modify existing)

Keep `get_unix_timestamp()` unchanged (CRDT path uses it). Add a new function:

```cpp
[[maybe_unused]] static uint64_t get_monotonic_timestamp() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL
         + static_cast<uint64_t>(ts.tv_nsec);
}
```

LWWSyncEngine uses `get_monotonic_timestamp()`. CRDTSyncEngine keeps using `get_unix_timestamp()`.

### Checkpoint: compiles, no behavioral change. All tests pass.

---

## Step 10: Add LWW Tests

**Files to create:**
- `tests/lww/lww_operations.cpp` — unit tests for LWW register semantics
- `tests/lww/lww_sync.cpp` — two-agent synchronization tests
- `tests/lww/lww_deletion.cpp` — tombstone and delete-insert ordering tests

### 10.1 LWW register tests

- Two writes: newer timestamp wins
- Tiebreak: same timestamp, higher agent_id wins
- Stale delta ignored
- Attribute-level LWW resolution

### 10.2 Two-agent sync tests

- Agent A inserts node, Agent B receives it
- Agent A updates attribute, Agent B converges
- Agent A deletes node, Agent B removes it
- Concurrent updates from A and B resolve by timestamp
- Full graph sync: new Agent C joins and gets current state

### 10.3 Deletion tests

- Delete at T1, insert at T2>T1: node exists
- Insert at T1, delete at T2>T1: node deleted
- Full graph sync doesn't resurrect deleted nodes (timestamped tombstones)
- `clear_deleted()` works

### 10.4 Run existing CRDT tests

All existing CRDT tests must still pass unchanged. They test `SyncMode::CRDT`.

### Checkpoint: all old tests pass + new LWW tests pass.

---

## Step 11: Update Python Bindings

**Files to modify:**
- `python-wrapper/python_api.cpp`

### 11.1 Expose `SyncMode` to Python

```python
pydsr.SyncMode.CRDT
pydsr.SyncMode.LWW
```

### 11.2 Update DSRGraph constructor binding

Add `sync_mode` parameter to the Python DSRGraph constructor, defaulting to `SyncMode.CRDT`.

### 11.3 No other changes needed

Python bindings only expose `Node`, `Edge`, `Attribute` — no CRDT types. Both engines produce the same user-facing types.

### Checkpoint: Python works with both modes.

---

## Step 12: Benchmarks — Comparative

**Files to create:**
- `benchmarks/strategy/crdt_vs_lww_bench.cpp`

### 12.1 Run identical workloads under both modes

For each benchmark (throughput, latency, convergence, memory):
1. Run with `SyncMode::CRDT`
2. Run with `SyncMode::LWW`
3. Report side-by-side

### 12.2 Expected results

| Metric | CRDT | LWW | Speedup |
|--------|------|-----|---------|
| Write throughput | baseline | ~10x | |
| Join latency | baseline | ~25x | |
| Memory per node | baseline | ~8x smaller | |
| Message size | baseline | ~1.7x smaller | |

### Checkpoint: benchmark results confirm the analysis from `LWW_ANALYSIS.md`.

---

## Step 13: Documentation and Cleanup

### 13.1 Document the `sync_mode` setting

In README or a dedicated doc, explain:
- `SyncMode::CRDT` — default, safe for multi-host deployment
- `SyncMode::LWW` — same-host only, ~10x faster, requires shared monotonic clock
- All agents on the network must use the same mode (different DDS topics enforce this)

### 13.2 Update CMakeLists.txt

Add new source files to the build:
- `dsr_crdt_sync_engine.cpp`
- `dsr_lww_sync_engine.cpp`
- New test files
- New benchmark files

### 13.3 No code deletion

`delta_crdt.h`, `crdt_types.h`, `translator.h`, existing IDL types — all stay. The CRDT path is fully preserved.

---

## File Inventory

### New files (created from scratch)

| File | Purpose |
|------|---------|
| `api/include/dsr/api/dsr_sync_engine.h` | Abstract SyncEngine + SyncEngineHost interfaces |
| `api/include/dsr/api/dsr_crdt_sync_engine.h` | CRDT engine header |
| `api/dsr_crdt_sync_engine.cpp` | CRDT engine impl (code moved from dsr_api.cpp) |
| `api/include/dsr/api/dsr_lww_sync_engine.h` | LWW engine header |
| `api/dsr_lww_sync_engine.cpp` | LWW engine impl (new code) |
| `core/include/dsr/core/types/lww_types.h` | LWWNode, LWWEdge, LWWClock |
| `core/include/dsr/core/types/lww_translator.h` | LWW ↔ IDL ↔ User conversions |
| `tests/lww/lww_operations.cpp` | LWW unit tests |
| `tests/lww/lww_sync.cpp` | LWW sync tests |
| `tests/lww/lww_deletion.cpp` | LWW deletion tests |
| `benchmarks/strategy/crdt_vs_lww_bench.cpp` | Comparative benchmark |

### Modified files (existing)

| File | Change |
|------|--------|
| `api/include/dsr/api/dsr_graph_settings.h` | Add `SyncMode` enum + field |
| `api/include/dsr/api/dsr_api.h` | Add `unique_ptr<SyncEngine>`, implement SyncEngineHost, remove moved members |
| `api/dsr_api.cpp` | Remove moved method bodies, delegate to engine |
| `core/topics/IDLGraph.idl` | Append LWW IDL types (no modification to existing) |
| `core/include/dsr/core/utils.h` | Add `get_monotonic_timestamp()` |
| `python-wrapper/python_api.cpp` | Expose SyncMode, add constructor parameter |
| `CMakeLists.txt` (api, core, tests, benchmarks) | Add new source files |

### Untouched files

| File | Why |
|------|-----|
| `core/include/dsr/core/crdt/delta_crdt.h` | CRDT library stays |
| `core/include/dsr/core/types/crdt_types.h` | CRDT types stay |
| `core/include/dsr/core/types/translator.h` | CRDT translator stays |
| `core/include/dsr/core/types/user_types.h` | User types unchanged |
| `core/include/dsr/core/types/common_types.h` | Attribute unchanged |
| `core/rtps/dsrpublisher.h` | Base publisher unchanged |
| `core/rtps/dsrsubscriber.h` | Base subscriber unchanged |
| All existing test files | CRDT tests unchanged |
| All existing benchmark files | CRDT benchmarks unchanged |

---

## Dependency Graph

```
Step 0  (baseline, verify green)
  │
  ├──► Step 1  (SyncEngine interface)
  │       │
  │       ▼
  │    Step 3  (Extract CRDTSyncEngine — refactor only, critical step)
  │       │
  │       │     ┌──────────────────────────────────────────┐
  │       │     │  In parallel after Step 3:               │
  │       ├────►│  Step 4  (LWW types)                     │
  ├──►    │     │  Step 5  (LWW IDL types)                 │
  │  Step 2     │  Step 6  (LWW translator)                │
  │  (settings) │  Step 9  (monotonic clock)               │
  │       │     └──────────────────────────────────────────┘
  │       │                    │
  │       │                    ▼
  │       └──────────►  Step 7  (LWWSyncEngine implementation)
  │                         │
  │                         ▼
  │                    Step 8  (Wire DSRGraph to select engine)
  │                         │
  │                    ┌────┴────┐
  │                    ▼         ▼
  │              Step 10     Step 11
  │              (tests)     (python)
  │                    │         │
  │                    └────┬────┘
  │                         ▼
  │                    Step 12 (benchmarks)
  │                         │
  │                         ▼
  └───────────────►   Step 13 (docs + cleanup)
```

**Critical path**: Steps 0 → 1 → 3 → 7 → 8 → 10

**Step 3 is the riskiest**: extracting CRDTSyncEngine without changing behavior. If existing tests pass after Step 3, the abstraction is correct and everything else is additive.

---

## Estimated Scope

| Step | New Lines | Modified Lines | Risk |
|------|-----------|---------------|------|
| 1. SyncEngine interface | ~120 | 0 | Low |
| 2. GraphSettings | ~5 | ~3 | Trivial |
| 3. Extract CRDTSyncEngine | ~800 (moved) | ~200 (DSRGraph delegation) | **High** |
| 4. LWW types | ~200 | 0 | Low |
| 5. LWW IDL types | ~60 | ~0 (append only) | Low |
| 6. LWW translator | ~150 | 0 | Low |
| 7. LWWSyncEngine | ~600 | 0 | Medium |
| 8. Wire DSRGraph | ~30 | ~80 | Medium |
| 9. Monotonic clock | ~8 | 0 | Trivial |
| 10. LWW tests | ~400 | 0 | Low |
| 11. Python bindings | ~15 | ~10 | Low |
| 12. Benchmarks | ~200 | 0 | Low |
| 13. Docs + cleanup | ~50 | ~20 | Trivial |
| **Total** | **~2,600 new** | **~300 modified** | |

No existing code is deleted. The CRDT path is preserved in full.
