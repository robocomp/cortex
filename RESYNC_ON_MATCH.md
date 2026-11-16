# Resync on match — the lost-insert fix

Branch `feat/resync-on-match`, on top of `1d05a8d` (kept). Local commits only: nothing pushed, nothing
merged, nothing installed.

Legend: **[V]** verified by running it here · **[I]** inferred from reading the code, not exercised.

## READ FIRST

1. **Every agent must be rebuilt after the reinstall.** `sizeof(DSR::DSRGraph)` changes (new members
   in `DSRGraph`, `DSRParticipant`, `DSRPublisher`), and agents allocate the graph from their own
   translation units. An agent built against the old headers and run against the new `libdsr_api.so`
   writes past its allocation. This is not the "new attribute" kind of reinstall. [I]
2. **No wire-format change, `DSR_PROTOCOL_VERSION` untouched.** But a *mixed* fleet is not safe: an
   agent with the OLD library imports every addressed push it sees (it ignores `to_id`) and takes it
   for the server's answer, so an old joiner can end its join on a peer's partial graph. Run the
   fleet all-new or all-old. [I]
3. **Commit 2 goes beyond the approved design**, because the design alone left a hole (below). It is a
   separate commit so it can be dropped: `git revert 509c214` leaves the rest working, with the
   limitation described in "What the design alone did not heal".
4. **The existing test suite runs on DDS domain 0** — the live fleet's domain. `bin/robot_concept` was
   running, so I did not run it as it stands; see "How the existing suite was run".

## Commits

| commit | what |
|---|---|
| `e0eff99` | resync on match: push the full graph to a peer once our writers have paired with it |
| `509c214` | full-graph import: an edge into a node that arrives for the first time comes with it |
| last commit | this report, and test 3 rewritten on the real fleet layout (one file loader) |

## Files changed

| file | change |
|---|---|
| `core/include/dsr/core/transport/transport_crtp.h` | `ParticipantGuidPrefix`, `guid_prefix` in `ParticipantDiscoveryInfo`, `BuiltinWriter`, `PublicationMatchInfo` |
| `core/include/dsr/core/rtps/dsrpublisher.h`, `core/rtps/dsrpublisher.cpp` | match callback on the writer listener; remote GUID prefix from `last_subscription_handle`; own-participant matches skipped; **inverted matched/unmatched test fixed** |
| `core/include/dsr/core/rtps/dsrparticipant.h`, `core/rtps/dsrparticipant.cpp` | discovery passes the GUID prefix; `set_publication_match_callback()`; each built-in writer reports under its `BuiltinWriter` kind |
| `api/include/dsr/api/dsr_api.h`, `api/dsr_api.cpp` | the mechanism: per-peer pairing state, the queued drain, the addressed push, the receive rules, the file-loaded agent's `GRAPH_ANSWER` reader |
| `api/include/dsr/api/dsr_sync_engine.h`, `dsr_crdt_sync_engine.h` | `mark_initial_state_loaded()` (a file-loaded graph has no silent first import to come) |
| `api/dsr_crdt_sync_engine.cpp` | commit 2: edges into a newly imported node |
| `tests/synchronization/resync_on_match.cpp`, `graph_test_access.h`, `tests/CMakeLists.txt` | the tests |

## How each requirement is met

**1. Who matched.** `on_publication_matched` converts `info.last_subscription_handle` to a GUID
(`iHandle2GUID`) and reports its 12-byte prefix. `ParticipantDiscoveryInfo` now carries the
participant's prefix too, and `DSRGraph::on_peer_discovery` parses the agent id out of
`"Participant_<id> ( <name> )"` with `std::from_chars`. The two callbacks arrive on FastDDS threads
in either order: the per-peer entry is keyed by prefix and a push that is due before the id is known
stays pending until discovery brings it. The transport layer grew by three small types and one
callback setter; nothing went through the CRTP interface. [V: the pushes in the test logs name the
right agent ids]

**2. The push is not itself lost.** I did not use "push on every match, coalesce". The trigger is
the moment the **last of our five writers** (node, edge, node attrs, edge attrs, graph answer) has
matched the peer. At that moment (a) the `GRAPH_ANSWER` writer has matched, so the push is tracked
for that reader and delivered reliably; (b) no later delta can be lost to that peer; (c) everything
written earlier is already in our state, because a local change is applied under `_mutex` before it
is published. So exactly one push per pairing is enough, and it is always sent after all of the
peer's readers are matched. An unmatch clears the bit; a re-match completes the pairing again and
pushes again. Nothing runs on the listener thread except a mutex-guarded bitmask update and one
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)`; pairings that complete before the queued call
runs share it and share one export. No timer, no threshold. [V: test logs show one push per ordered
pair; I: the re-match path]

**3. Addressing, on the existing fields.**

| `id` | `to_id` | meaning | who imports |
|---|---|---|---|
| `-1` | requester | "repeated" (duplicate agent id) | nobody; the joiner aborts — unchanged |
| sender | `0` | the server's answer to a `GRAPH_REQUEST` | every agent that joined through a server — unchanged |
| sender | `!= 0` | addressed push | only the agent whose id is `to_id` |

- (a) Join handshake: a push that reaches a joiner **before** the server's answer is kept (latest per
  sender) and imported right after the answer. It does not set `sync` — its sender may be another
  joiner with an empty graph — and it is never the silent first import. [V: unit test feeds the
  reader callback by hand; V: a CRDT joiner still gets zero `update_node_signal` from its join]
- (b) Duplicate id: same condition as before (`id == -1`, not yet synced, `to_id` is ours). [V: unit
  test]
- (c) LWW: same code path — the functor is the same template, the push goes through
  `std::visit` on the engine's `FullGraphMessage`. [V: every DDS test runs in CRDT and LWW]
- The agent that loaded the file had no `GRAPH_ANSWER` reader. It has one now, accepting addressed
  pushes only (`accept_server_answers = false`), and its engine is told the initial state is loaded
  so the push emits signals. Without this the server itself could never be healed. [V]
- **Limit:** `to_id == 0` means "not addressed" and agent id 0 is legal (ids are 0..4095). An agent
  with id 0 is never pushed to. [I]

**4. Locking.** Export: `std::shared_lock(_mutex)` around `engine_->export_full_graph()`, as in the
server path; the write happens after the lock is released (the message is a copy; the server path
writes while holding it). Import: `queue_full_graph_import()` is the existing answer path, factored
out — queued to the Qt thread, `unique_lock(_mutex)`, `RemoteApplyScope`. [V by the tests; I for
contention under load]

**5. Cost.** One export + one addressed import per ordered pair of agents, per pairing; nothing in
steady state. Eight agents starting together: 8 × 7 = **56** pushes in total, i.e. each agent sends
7 and imports 7 full graphs during start-up, spread over the seconds the fleet takes to pair. For
scale, the join already makes every joined agent import every server answer (up to 7 + 6 + … ≈ 28
imports fleet-wide), and each joiner sends at least two requests. Every agent's reader also
deserialises the pushes addressed to others before dropping them on `to_id` (8 × 7 × 6 = 336
discarded deserialisations at start). A joiner's first push is usually its still-empty graph (the
drain is queued ahead of its own initial import): cheap, and still correct, since its own writes are
always in its state. I did not measure wall-clock cost on a real graph. [I — arithmetic, plus V for
"one push per ordered pair" in the logs]

`DSR_RESYNC_ON_MATCH=0` in an agent's environment switches its **sending** side off.

## What the design alone did not heal (why commit 2 exists)

A full-graph import is **not** a full merge. For a node the receiver already holds with the same
register, `mvreg::join` keeps the local copy and nothing nested is merged. Measured with commit 1
only [V]: the pushed node arrived, with its own outgoing edges, but the edge
`known parent -> new node` did not — it lives in the parent's fano, and its delta was lost in the
same window as the node. Later edge-attribute deltas would then be parked for ever, the same disease
one level down.

Commit 2: when an import inserts a node that was unknown here, edges **into** it found in the
sender's copy of nodes we already hold are taken as well. Such an edge cannot be one we deleted (we
never held its target). Edges between two nodes we already hold are deliberately left alone, because
there a missing edge may be a deleted one and a stale snapshot would resurrect it.

Still **not** healed, by either commit [I, from the join code]:
- an edge inserted early between two nodes the peer already holds;
- an attribute written early on a node the peer already holds (heals itself only if that attribute is
  written again);
- an early **delete**. A full graph cannot say "this is gone".

## Tests

`tests/synchronization/resync_on_match.cpp`, private DDS domain 78 (and 79), tag `[RESYNC]`.

1. *A node inserted before the writer matched a running peer still reaches that peer.* Two agents
   that both load the file; the second inserts the instant its constructor returns. This is the raw
   race (a write inside the few ms discovery takes) and it also covers the file-loaded agent as the
   receiver. Two file loaders is not a real fleet layout — it is the only way to write that early
   from one thread, since a joiner sits >1 s in its constructor.
2. *The join handshake is unchanged by the pushes between peers.*
3. *A peer that joins while a node is being inserted still gets the node and the edge to it.* The
   real layout: one file loader S, joiners W and R. W inserts node + edges, R is constructed at once
   with no Qt events processed in between, so S answers R from a state that does not have the node
   yet and W wrote before R existed. Deterministic, no race involved.
4. *An addressed full graph is imported only by its target, and never as the initial graph.* The
   reader callback fed by hand: addressing, the kept early push, the duplicate-id answer, the
   file-loaded agent.

### Unpatched vs patched  [V]

Unpatched = `1d05a8d` exported to `build-baseline/src` with tests 1–3 added (test 4 needs the new
types). Each run does CRDT and LWW.

| test | unpatched `1d05a8d` | patched |
|---|---|---|
| 1. early insert, two file loaders (a race) | node **lost in 17 of 20** (10 runs × 2 modes); an earlier batch with the same scenario: 38 of 40 | reached in **20 of 20** (and 40 of 40 in the earlier batch) |
| 2. join handshake | passes | passes |
| 3. joiner + node + edge, one file loader (deterministic) | node **lost in 20 of 20** | node and edge reached in **20 of 20** |
| 4. receive rules | (not buildable) | passes |
| all `[RESYNC]` | 1 of 3 test cases pass | 4 of 4, 99 assertions, 10 runs out of 10 |

Two further checks on the patched build:
- `DSR_RESYNC_ON_MATCH=0`: tests 1 and 3 fail again in both modes (one run). The push is what heals.
- Commit 1 without commit 2 (engine file taken from `e0eff99`): test 3 fails in CRDT at the edge
  `root -> late_child` only — the node is there, 22 of 23 assertions pass. LWW passes (its import
  applies edges on their own).

Test 1 is a race and is not deterministic on the unpatched code (the node gets through when
discovery happens to win, 5 times out of 60 here). Test 3 is deterministic.

### How the existing suite was run

The suite uses DDS domain 0. With `robot_concept` live on domain 0, a file-loaded test graph there
would answer the fleet's `GRAPH_REQUEST`s and inject test nodes. Unprivileged namespaces are disabled
on this box, so I built two throw-away copies under `build-private/` (git-ignored) in which
`DSRParticipant::init_impl` adds 40 to the domain id — the only difference from the real sources —
and ran the whole `tests` binary from each, one after the other.

| build | test cases | passed | failed |
|---|---|---|---|
| unpatched + tests 1–3 | 91 | 78 | 13 = 11 old + the 2 resync tests |
| patched (final tree) | 92 | 81 | 11 old |

The 11 old failures are the same assertion sites in both builds (7 in `graph/inner_gaussian.cpp`, 10
in `graph/rt_timestamp.cpp`): RT/covariance tests, nothing to do with synchronisation, failing before
this branch. Every `[SYNCHRONIZATION]`, `[CRDT]`, `[LWW]`, `[FAKE_NET]`, `[SIGNALS]` and `[RENAME]`
case passes in the patched build. The whole of `cortex` (core, api, gui, pydsr, tests) also builds
from the branch in `build-resync/`. [V]

Not run: benchmarks, the Python wrapper tests, TSAN/ASAN builds, and **no live fleet run**.

## Risks

- **ABI** — see READ FIRST 1.
- **Mixed versions** — see READ FIRST 2.
- **More imports of other agents' snapshots.** Until now a joined agent imported only the server's
  graph; now it also imports each peer's. A peer's snapshot can be a few queued deltas behind. For
  nodes held on both sides that is a no-op. For a **delete racing a join** it is a new way to get a
  zombie: the delete is lost to the newcomer (not matched yet), the server's answer no longer has the
  node, but a peer that has not applied the delete yet pushes a graph that still has it. Narrow
  window; the same weakness already existed with the server's answer. [I]
- **Two agents that both load a JSON file.** Their registers for the same node ids differ, and now
  they import each other's pushes. I saw this bite in an earlier version of test 3 (the second
  loader's own `root -> child` edge disappeared from its view after importing the first loader's
  root). Before this change file loaders never imported anything, though joiners in such a fleet
  already had the problem. If any deployment starts two file loaders, tell me — the fix would be to
  not address pushes between them. [V that it happens; I that no fleet here does it]
- **The log line** `Subscriber [topic] matched/unmatched` now says the truth; it used to be inverted
  (`current_count == MATCHED_MATCHING` compared a count with an enum equal to 0). Anything grepping
  those lines sees them swap. `[resync] full graph ( N elements ) pushed to agent X after its readers
  matched` is new, one `qInfo` line per push.
- **A peer that never offers all five readers** is never pushed to. Every `DSRGraph` creates all
  five, so this only concerns foreign tools on the domain. [I]
- **Qt event loop.** Like every remote apply, the push and its import run in queued calls on the
  thread that owns the `DSRGraph`. An agent whose owning thread does not spin an event loop neither
  pushes nor imports (it does not import the initial graph either, today). [I]

## Build and install (owner)

Nothing below was run by me except the build in `build-resync/`.

```bash
# 1. review, then bring the branch into the main checkout (your call how)
cd /home/pbustos/robocomp/components/cortex
git merge --no-ff feat/resync-on-match        # or cherry-pick the three commits

# 2. build (check nobody else is compiling first)
pgrep -a cc1plus
cmake -B build
make -C build -j8

# 3. install
sudo make -C build install
sudo ldconfig

# 4. rebuild EVERY agent (cbuild in each), then restart the fleet
```

To run the new tests without touching domain 0:

```bash
cd /home/pbustos/robocomp/components/cortex.worktrees/resync-on-match
cmake -S . -B build-resync -DWITH_TESTS=ON \
      -DFETCHCONTENT_SOURCE_DIR_CATCH2=/tmp/cortex-base-build/_deps/catch2-src -DFETCHCONTENT_FULLY_DISCONNECTED=ON
make -C build-resync -j8 tests
./build-resync/tests/tests "[RESYNC]"
```

What to look for in a live run: one `[resync] full graph ... pushed to agent <id>` line per peer in
each agent's log, and `ltsm_topology` present in `door_concept`'s graph.
