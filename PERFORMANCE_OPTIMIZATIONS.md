# CORTEX/DSR Performance Optimization Proposals

## 1. CRDT Data Structures: `std::map` -> `std::unordered_map` / flat containers

**Where:** `delta_crdt.h:17-21`, `crdt_types.h:121,247-248`

The entire CRDT layer uses **ordered `std::map`** and **`std::set`** for dot contexts, dot kernels, node attributes, and edge maps (fano). These are tree-based (`O(log n)` lookup, poor cache locality). Since the ordering is only exploited in `dot_context::join()` for merge-iteration, this can be restructured:

- **`dot_context::cc`** (`map<key_type, int>`) -> `std::unordered_map<key_type, int>`. The ordered merge in `join()` can be replaced by iterating one map and probing the other -- still `O(n)` but with better constants.
- **`dot_context::dc`** (`set<pair<key_type, int>>`) -> `std::unordered_set` with a pair hash. The `compact()` method iterates linearly anyway.
- **`dot_kernel::ds`** (`map<pair<key_type,int>, T>`) -> `std::unordered_map`. The `join_replace_conflict` merge-iteration is only used on deltas, which are typically size 1.
- **`CRDTNode::m_attrs`** and **`CRDTEdge::m_attrs`** (`map<string, mvreg<CRDTAttribute>>`) -> `std::unordered_map`. Attribute lookups by name are the hottest path, done on every `get_attrib_by_name` call.
- **`CRDTNode::m_fano`** (`map<pair<uint64_t, string>, mvreg<CRDTEdge>>`) -> `std::unordered_map` with a pair hash. Edge lookups by `{to, type}` are very frequent.

**Expected impact:** 2-4x speedup on attribute lookups and CRDT join operations due to `O(1)` amortized access and better cache locality.

---

## 2. Eliminate Unnecessary Copies in `get_()` and `get_node()`

**Where:** `dsr_api.cpp:859-867` and `dsr_api.cpp:149-168`

```cpp
std::optional<CRDTNode> DSRGraph::get_(uint64_t id) {
    // Returns a COPY of the entire CRDTNode (attrs + fano deep copy)
    return std::make_optional(it->second.read_reg());
}
```

Every `get_node()` call deep-copies the entire node including all attributes and edges. Then the caller often only reads one attribute and discards the rest. This is the **single most expensive pattern** in the codebase.

**Proposal:** Add a `get_node_ref()` or `get_()` that returns a `const CRDTNode*` (pointer, not optional of a copy) for internal use while the lock is held. The public `get_node()` can keep returning copies for safety, but internal methods (`get_nodes_by_type`, `get_edges_by_type`, `update_node_`, `delete_node_`) should work with references. Many internal call sites already hold `_mutex` and could use a pointer directly.

Additionally, consider a **`get_attrib_by_name(uint64_t id)` overload** (line 208-226) that avoids creating the intermediate `CRDTNode` copy -- it currently calls `get_(id)` which copies the whole node just to read one attribute:

```cpp
template <typename name>
inline std::optional<...> get_attrib_by_name(uint64_t id) {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::optional<CRDTNode> n = get_(id);  // FULL NODE COPY just for one attribute!
```

**Expected impact:** Eliminates the dominant allocation overhead in read-heavy workloads. Could be 3-10x improvement for attribute-heavy agents.

---

## 3. Lock Granularity: Merge `_mutex` and `_mutex_cache_maps`

**Where:** `dsr_api.h:579-580`, every method that acquires both locks

Currently there are two `shared_mutex`es: one for the node map, one for cache maps. In practice, **almost every write operation acquires both**, and many read operations (e.g., `get_nodes_by_type`, `get_edges_by_type`, `get_edges_to_id`) also acquire both. The dual-lock pattern:
- Adds overhead (two atomic CAS operations per lock/unlock)
- Creates lock-ordering complexity
- The cache maps are always consistent with the node map, so separating them adds no meaningful concurrency

**Proposal:** Merge into a single `shared_mutex`. Alternatively, if the intent is to allow concurrent reads to cache maps while a write to nodes is in progress, consider a **read-copy-update (RCU)** pattern or `std::atomic<shared_ptr>` for the cache maps.

**Expected impact:** ~10-20% improvement in lock-heavy workloads by halving atomic operations.

---

## 4. ThreadPool: Heap Allocation Per Task

**Where:** `threadpool.h:171-177`

Every task submitted to the threadpool allocates a `function_wrapper` on the heap via `new`:

```cpp
auto tmp_ptr = std::unique_ptr<function_wrapper_base>(
    new function_wrapper<Function, Arguments...>(...));
tasks.emplace(std::move(tmp_ptr));
```

With 4 DDS subscription threads feeding the pool continuously, this creates significant allocation pressure.

**Proposal:**
- Use a **fixed-size ring buffer** instead of `std::queue<unique_ptr>` to avoid heap allocations per task
- Or use a **small-object optimization**: `std::function`-like inline storage for small callables (most DSR lambdas are small)
- Consider `std::move_only_function` (C++23) which you already have access to
- Alternatively, use a lock-free queue (e.g., `moodycamel::ConcurrentQueue`) to also eliminate the mutex contention between DDS threads and worker threads

**Expected impact:** Reduces allocation pressure and GC-like fragmentation under high delta rates.

---

## 5. `dot_context::compact()` Is Called Too Eagerly

**Where:** `delta_crdt.h:95-101`

`insertdot()` calls `compact()` by default, and `compact()` iterates the entire dot cloud `dc` in a loop until convergence. During `join()`, `insertdot` is called **for every dot in the other's dc** with `compactnow=false`, then `compact()` is called once -- this is correct. But individual `insertdot()` calls (e.g., from `dot_kernel::add()` at line 278) trigger a full compact each time.

**Proposal:** Defer compaction. Use lazy compaction: set a dirty flag and compact only when `dotin()` is called or during `join()`. Most insert sequences are followed by a join anyway.

**Expected impact:** Reduces CPU time in delta creation, especially for multi-attribute updates.

---

## 6. `dotin()` Has a Subtle Bug and Performance Issue

**Where:** `delta_crdt.h:43-49`

```cpp
bool dotin(const pair<key_type, int> &d) const {
    const auto itm = cc.find(d.first);
    if (itm != cc.end() && d.second <= itm->second) return true;
    if (not dc.empty() and d.second < dc.rbegin()->second) return true; // BUG?
    if (dc.count(d) != 0) return true;
    return false;
}
```

The middle check `d.second < dc.rbegin()->second` compares the dot's counter against the **maximum counter of any key** in the dot cloud -- this seems incorrect for a multi-key system. A dot `(agent_X, 5)` would be considered "in" if any other agent has a counter > 5 in the dot cloud. This might cause **false positives** leading to premature delta pruning and lost updates.

If this is intentional as a heuristic, it should be documented. If not, fixing it would improve **correctness** (which is the ultimate performance optimization -- no wasted retransmissions).

---

## 7. Batch Delta Publishing

**Where:** `dsr_api.cpp:337-348` (update_node), `dsr_api.cpp:679-691` (insert_or_assign_edge)

When updating a node, each attribute delta is serialized into `IDL::MvregNodeAttr` and then published as a vector. However, the update pattern is:
1. Acquire lock
2. Compute all attribute deltas
3. Release lock
4. Publish all deltas
5. Emit signals

Step 4 serializes each delta separately via FastCDR.

**Proposal:** For bulk attribute updates, consider **coalescing** multiple attribute deltas into a single DDS message with a batched payload. This reduces DDS overhead (fewer writes, fewer network packets). The receiver already processes attribute vectors.

**Expected impact:** Reduces network I/O and serialization overhead for nodes with many attributes being updated simultaneously.

---

## 8. `get_connected_agents()` Pre-allocates Wrong Size

**Where:** `dsr_api.h:555-564`

```cpp
std::vector<std::string> ret_vec(participant_set.size()); // Creates N empty strings
for (auto &[k, _]: participant_set) {
    ret_vec.emplace_back(k);  // Appends AFTER the N empty strings
}
```

This creates `N` default-constructed strings, then appends `N` more via `emplace_back`. The returned vector has `2N` elements, the first `N` being empty strings.

**Fix:** Change to `reserve()` instead of sized construction:
```cpp
std::vector<std::string> ret_vec;
ret_vec.reserve(participant_set.size());
```

---

## 9. Unprocessed Delta Maps Can Grow Unbounded

**Where:** `dsr_api.h:675-678`

The four `unprocessed_delta_*` multimaps accumulate deltas for nodes/edges that haven't arrived yet. If a node is permanently unreachable (e.g., an agent crashes), these maps grow forever.

**Proposal:** Add a TTL-based eviction or a maximum size cap. When the map exceeds a threshold, evict the oldest entries (using the stored timestamps).

**Expected impact:** Prevents memory leaks and reduces map lookup time in degraded scenarios.

---

## 10. `delete_node(string)` Acquires Lock Twice

**Where:** `dsr_api.cpp:407-445`

```cpp
bool DSRGraph::delete_node(const std::string &name) {
    id = get_id_from_name(name);          // Acquires shared_lock on _mutex_cache_maps
    if (id.has_value()) {
        node_signal = get_(*id);           // Needs shared_lock on _mutex (NOT held!)
        std::unique_lock<std::shared_mutex> lock(_mutex);  // Then acquires exclusive
        ...
    }
```

`get_(*id)` at line 419 is called **without** holding `_mutex`, so it reads `nodes` unsynchronized -- this is a **data race**. Compare with `delete_node(uint64_t)` at line 447-458 which has the same issue.

**Fix:** Move the `get_()` call inside the exclusive lock scope, or snapshot the node data after acquiring the lock.

**Expected impact:** Fixes a correctness bug that could cause crashes under concurrent access.

---

## 11. Signal Emission Under Lock vs. Outside Lock

**Where:** Throughout `join_delta_node`, `join_delta_edge`, etc.

The current pattern correctly emits signals **outside** the lock, which is good. But the signal emission itself (`emitter.update_node_signal(...)`) could invoke Qt signal-slot connections that re-enter the graph API (e.g., a slot that calls `get_node()`). This re-entrance could cause deadlocks with `shared_mutex` if a thread holds a shared lock and then tries to acquire it again (shared_mutex is not recursive).

**Proposal:** Consider using a **deferred signal queue** -- collect all signals to emit, release the lock, then emit them in batch. This is partially done already but not consistently. A consistent pattern would also enable **signal coalescing** (e.g., if the same node is updated 10 times in rapid succession, emit one signal instead of 10).

**Expected impact:** Prevents potential deadlocks and reduces signal storm overhead.

---

## 12. Replace `std::unordered_map` with Open-Addressing Hash Maps for Cache Maps

**Where:** `dsr_api.h:631-637`

The cache maps use `std::unordered_map` which uses chaining (linked lists for collision resolution). For the access patterns in DSR (frequent small lookups, moderate size), **open-addressing** hash maps like `absl::flat_hash_map` or `ankerl::unordered_dense::map` offer:
- Better cache locality (no pointer chasing)
- Lower memory overhead
- 2-3x faster lookups

**Expected impact:** 20-50% faster cache map lookups.

---

## 13. InnerEigenAPI: Cache Transform Chains

**Where:** `api/include/dsr/api/dsr_inner_eigen_api.h`

The InnerEigenAPI computes transforms between arbitrary nodes by walking the tree up to the common ancestor. This involves multiple `get_node()` calls (each a copy + lock), RT attribute extraction, and matrix multiplications.

**Proposal:** Cache computed transforms with an invalidation strategy based on the RT edge update signals. When an RT edge is updated, only invalidate transforms that pass through that edge. This is a classic **kinematic chain cache** used in robotics frameworks.

**Expected impact:** Dramatic speedup for agents that frequently query transforms between the same node pairs (common in perception and navigation).

---

## Summary: Priority-Ordered Recommendations

| Priority | Optimization | Effort | Impact |
|----------|-------------|--------|--------|
| **P0** | Fix data races in `delete_node(string)` (#10) and `dotin()` (#6) | Low | Correctness |
| **P0** | Fix `get_connected_agents()` bug (#8) | Trivial | Correctness |
| **P1** | Return const refs instead of copies in `get_()` (#2) | Medium | High |
| **P1** | `std::map` -> `std::unordered_map` in CRDT types (#1) | Medium | High |
| **P2** | Merge dual mutex into single mutex (#3) | Low | Medium |
| **P2** | Open-addressing hash maps for cache (#12) | Low | Medium |
| **P2** | Threadpool allocation reduction (#4) | Medium | Medium |
| **P3** | Lazy dot context compaction (#5) | Low | Low-Medium |
| **P3** | Unprocessed delta eviction (#9) | Low | Robustness |
| **P3** | Transform chain caching (#13) | High | High (use-case dependent) |
| **P3** | Batch delta publishing (#7) | Medium | Medium |
| **P3** | Signal coalescing (#11) | Medium | Medium |
