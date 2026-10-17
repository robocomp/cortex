# The Full-Graph Dangling-Atomic Bug (DSR core)

*Found 2026-07-21 · `DSRGraph::fullgraph_request_thread()` in `api/dsr_api.cpp` · fixed, no ABI break*

## TL;DR

A DDS callback in the DSR graph-sync code kept a **pointer to a local variable that had
already gone out of scope**. Every time a peer joined the graph afterwards, that callback
wrote one byte through the stale pointer — into whatever now happened to live at that spot on
the stack. Usually harmless; occasionally it landed on the internals of a live `cv::Mat` (or an
ONNX buffer, or the graph-viewer) and the process died with a wild, un-reproducible SIGSEGV.

This is almost certainly the single cause behind the long tail of "random crashes in
`cv::Mat` / ONNX / `paintAndFlush` under participant churn" we've been chasing for weeks.

---

## The intuitive version

Imagine you leave a **sticky note on your desk**: *"put a ✓ here when the full graph arrives."*
You point a courier at that exact spot on the desk and walk away.

Now the desk is a shared table. After you leave, other people come and go and put their own
papers, laptops, and coffee cups on that same spot. The courier finally shows up (a new robot
joined the network and sent the graph) and — following your months-old instruction — slaps a
✓ **right where your note used to be**. But your note is long gone; the ✓ lands on top of
whatever is there now.

- If a scrap of paper is there → nothing bad happens (you never notice).
- If someone's **open laptop** is there → the ✓ pokes a key and the laptop does something insane
  and crashes.

The "desk spot" is a fixed **stack address**. The "✓" is `sync = true`. The "laptop" is a
`cv::Mat` header (or an ONNX tensor) that the main thread happened to place at that address.
The courier is a **FastDDS reader thread**. And "you walked away" is the function **returning**
while a permanent subscription still holds the address of its now-dead local variable.

---

## The code

```cpp
std::pair<bool, bool> DSRGraph::fullgraph_request_thread()
{
    std::atomic<bool> sync{false};        // <-- LOCAL, lives on this function's stack
    std::atomic<bool> repeated{false};    // <-- LOCAL

    // Register a PERMANENT DDS subscription whose callback captures &sync / &repeated:
    subscribe_graph_answers(make_fullgraph_request_functor_impl("GRAPH_ANSWER", sync, repeated));
    //                                                   the lambda does:  sync = true;

    ... request the graph, wait for it ...

    return { sync, repeated };             // <-- FUNCTION RETURNS: sync/repeated destroyed,
}                                          //     but the subscription + callback live on
```

The subscription is never torn down. So long after this function returns, **any peer joining**
sends a `GRAPH_ANSWER`, the FastDDS reader thread runs the callback, and it executes
`sync = true` — writing through `&sync`, which now points at **freed, reused stack memory**.

`std::atomic<bool>::operator=` writes exactly **one byte** (`0x01`). If the stale address is
byte 6 of an 8-byte word, the word reads back as `0x0001_0000_0000_0000` — a pointer with
**bit 48 flipped**. That's the fingerprint we saw in every crash.

---

## Why it was a nightmare to find (a textbook heisenbug)

It only kills the process when **two independent things line up**:

1. **A peer joins after startup** (sends `GRAPH_ANSWER`) → fires the stray write. No join, no bug.
2. **Something important sits at that exact dead stack address** at that instant. This depends on
   the *current stack layout*, which shifts with what the code is doing.

Because of #2 the victim kept moving: one day it was the Ricoh compositor's `cv::Mat`, another
day the graph viewer, most recently the semantic-map `cv::Mat` in the voxelizer's compute loop.
Same bullet, different bystander each time — so it looked like a different bug every time, and
tools that change memory layout (like ASan) made it vanish. It's also why enabling an unrelated
feature (`publish_node`) "caused" the crash: it merely reshaped the stack so the bullet hit a
`cv::Mat` header instead of a harmless slot.

---

## How it was finally pinned

Not by reading code (three separate reviews called the surrounding code memory-safe), but by
**catching the writer in the act**:

1. The corruption always landed at the **same offset** within the victim (`cv::Mat::step.p`,
   with bit 48 set) — a deterministic target.
2. Ran voxelizer under gdb, computed that exact stack address at runtime, and set a **hardware
   watchpoint** on those 8 bytes, conditioned on "bit 48 got set." Near-zero overhead, so it
   didn't perturb the timing the way ASan did.
3. Provoked a peer join (started `table_agent`). The watchpoint fired instantly with the
   writer's backtrace:

```
#0 std::atomic<bool>::operator=
#1 DSRGraph::make_fullgraph_request_functor_impl<…>::{lambda}   dsr_api.cpp:525
#2 std::function<…>::operator()
#3 DSRSubscriber::SubListener::on_data_available                (FastDDS reader thread)
   … FastDDS StatefulReader / MessageReceiver / SharedMemChannelResource …
```

Caller confirmed: the lambda captured `&sync`/`&repeated`, references to stack locals of a
function that had long since returned.

---

## The fix (no ABI break)

Give the flags a lifetime that matches the subscription, without changing `DSRGraph`'s layout
(which would force every agent to recompile). Make them `shared_ptr` and have the callback
**capture them by value**:

```cpp
auto sync     = std::make_shared<std::atomic<bool>>(false);
auto repeated = std::make_shared<std::atomic<bool>>(false);
// functor signature: (…, std::shared_ptr<std::atomic<bool>> sync, … repeated)
// lambda capture:    [this, channel, sync, repeated]   // BY VALUE -> keeps them alive
//   body:  *sync = true;  /  !*sync  /  *repeated = true;
// return { sync->load(), repeated->load() };
```

Now the callback owns the flags; the late `GRAPH_ANSWER` writes into a live, heap-owned atomic.
No dangling pointer, no wild write. Only a private method's signature + bodies changed →
`DSRGraph`'s ABI is unchanged, so agents just relink the rebuilt `libdsr`.

---

## Blast radius & follow-up

- **Every DSR agent** is affected — the bug is in `DSRGraph` core, on the join path. It fires on
  *any* post-startup participant join.
- **Rebuild all agents** against the fixed cortex. Until an agent is rebuilt, it still carries the
  bug and will keep crashing intermittently under churn. (We saw exactly this: after fixing
  cortex + rebuilding only voxelizer, the *un-rebuilt* `robot_concept` / `table_concept` kept
  SIGSEGV-ing on churn.)
- Expect the whole family of "random SEGV in `cv::Mat` / ONNX / `paintAndFlush` when peers join"
  crashes to disappear once every agent is on the fixed build.

## One-line lesson

> Never capture a reference to a local variable into a callback that outlives the function —
> especially a callback owned by a long-lived subscription on another thread. Capture by value,
> or give the state the same lifetime as the subscription.
