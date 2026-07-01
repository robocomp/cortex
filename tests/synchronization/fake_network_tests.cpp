#include <catch2/catch_test_macros.hpp>

#include "../transport/fake_network.h"
#include "dsr/core/types/user_types.h"
#include "dsr/core/types/internal_types.h"
#include "dsr/core/types/type_checking/dsr_edge_type.h"
#include "dsr/core/types/type_checking/dsr_node_type.h"

using namespace DSR;
using namespace DSR::Test;

// ──────────────────────────────────────────────────────────────────────────────
// Test helpers
// ──────────────────────────────────────────────────────────────────────────────

namespace {

Node make_robot(uint64_t id, uint32_t agent_id, const std::string& name, uint64_t ts = 100)
{
    auto n = Node::create<robot_node_type>(name);
    n.id(id);
    n.agent_id(agent_id);
    n.attrs()["level"] = Attribute(0, ts, agent_id);
    return n;
}

LWWNodeAttrVec make_attr_batch(uint64_t node_id, uint32_t agent_id, int level, uint64_t ts)
{
    LWWNodeAttrMsg msg;
    msg.node_id      = node_id;
    msg.attr_name    = "level";
    msg.value        = Attribute(level, ts, agent_id);
    msg.agent_id     = agent_id;
    msg.timestamp    = ts;
    msg.deleted      = false;
    msg.protocol_version = DSR_PROTOCOL_VERSION;
    msg.sync_mode    = sync_mode_wire_value(SyncMode::LWW);

    LWWNodeAttrVec batch;
    batch.vec.push_back(std::move(msg));
    return batch;
}

LWWNodeMsg make_insert_msg(uint64_t node_id, uint32_t agent_id, const std::string& name, uint64_t ts)
{
    LWWNodeMsg msg;
    msg.id           = node_id;
    msg.type         = robot_node_type::attr_name;
    msg.name         = name;
    msg.agent_id     = agent_id;
    msg.timestamp    = ts;
    msg.deleted      = false;
    msg.attrs["level"] = Attribute(0, ts, agent_id);
    msg.protocol_version = DSR_PROTOCOL_VERSION;
    msg.sync_mode    = sync_mode_wire_value(SyncMode::LWW);
    return msg;
}

LWWEdgeAttrVec make_edge_attr_batch(uint64_t from, uint64_t to, uint32_t agent_id, int weight, uint64_t ts)
{
    LWWEdgeAttrMsg msg;
    msg.from      = from;
    msg.to        = to;
    msg.type      = std::string(RT_edge_type::attr_name);
    msg.attr_name = "weight";
    msg.value     = Attribute(weight, ts, agent_id);
    msg.agent_id  = agent_id;
    msg.timestamp = ts;
    msg.deleted   = false;
    msg.protocol_version = DSR_PROTOCOL_VERSION;
    msg.sync_mode = sync_mode_wire_value(SyncMode::LWW);
    LWWEdgeAttrVec batch;
    batch.vec.push_back(std::move(msg));
    return batch;
}

LWWNodeMsg make_node_delete_msg(uint64_t node_id, uint32_t agent_id, uint64_t ts)
{
    LWWNodeMsg msg;
    msg.id       = node_id;
    msg.type     = robot_node_type::attr_name;
    msg.name     = "";
    msg.agent_id = agent_id;
    msg.timestamp = ts;
    msg.deleted  = true;
    msg.protocol_version = DSR_PROTOCOL_VERSION;
    msg.sync_mode = sync_mode_wire_value(SyncMode::LWW);
    return msg;
}

LWWEdgeMsg make_edge_msg(uint64_t from, uint64_t to, uint32_t agent_id, uint64_t ts, bool deleted = false)
{
    LWWEdgeMsg msg;
    msg.from     = from;
    msg.to       = to;
    msg.type     = std::string(RT_edge_type::attr_name);
    msg.agent_id = agent_id;
    msg.timestamp = ts;
    msg.deleted  = deleted;
    msg.protocol_version = DSR_PROTOCOL_VERSION;
    msg.sync_mode = sync_mode_wire_value(SyncMode::LWW);
    return msg;
}

// Extract the LWW tombstone timestamp from a node deletion effect.
// Returns 0 if the effect doesn't carry an LWW delta.
uint64_t tombstone_ts(const NodeMutationEffect& del_effect)
{
    if (!del_effect.node_delta.has_value()) return 0;
    const auto* lww = std::get_if<LWWNodeMsg>(&*del_effect.node_delta);
    return lww ? lww->timestamp : 0;
}

} // namespace

// ──────────────────────────────────────────────────────────────────────────────
// Basic delivery
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("FakeNetwork: LWW insert propagates to peer", "[FAKE_NET][LWW]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    auto effect = A.insert_node(make_robot(1000, 1, "robot_a"));
    REQUIRE(effect.applied);
    net.post(1, effect);
    net.deliver_all();

    REQUIRE(A.get_node(1000).has_value());
    REQUIRE(B.get_node(1000).has_value());
}

TEST_CASE("FakeNetwork: CRDT insert propagates to peer", "[FAKE_NET][CRDT]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::CRDT);
    auto& B = net.add_agent(2, SyncMode::CRDT);

    auto effect = A.insert_node(make_robot(1000, 1, "robot_a"));
    REQUIRE(effect.applied);
    net.post(1, effect);
    net.deliver_all();

    REQUIRE(A.get_node(1000).has_value());
    REQUIRE(B.get_node(1000).has_value());
}

TEST_CASE("FakeNetwork: LWW bidirectional inserts converge", "[FAKE_NET][LWW]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    auto ea = A.insert_node(make_robot(1000, 1, "robot_a"));
    auto eb = B.insert_node(make_robot(2000, 2, "robot_b"));
    REQUIRE(ea.applied);
    REQUIRE(eb.applied);
    net.post(1, ea);
    net.post(2, eb);
    net.deliver_all();

    REQUIRE(A.get_node(2000).has_value());
    REQUIRE(B.get_node(1000).has_value());
    REQUIRE(A.size() == B.size());
}

TEST_CASE("FakeNetwork: CRDT bidirectional inserts converge", "[FAKE_NET][CRDT]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::CRDT);
    auto& B = net.add_agent(2, SyncMode::CRDT);

    auto ea = A.insert_node(make_robot(1000, 1, "robot_a"));
    auto eb = B.insert_node(make_robot(2000, 2, "robot_b"));
    REQUIRE(ea.applied);
    REQUIRE(eb.applied);
    net.post(1, ea);
    net.post(2, eb);
    net.deliver_all();

    REQUIRE(A.get_node(2000).has_value());
    REQUIRE(B.get_node(1000).has_value());
    REQUIRE(A.size() == B.size());
}

TEST_CASE("FakeNetwork: LWW insert fans out to all agents in a three-node cluster",
          "[FAKE_NET][LWW]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);
    auto& C = net.add_agent(3, SyncMode::LWW);

    auto e = A.insert_node(make_robot(1000, 1, "robot_a"));
    REQUIRE(e.applied);
    net.post(1, e);
    net.deliver_all();

    REQUIRE(B.get_node(1000).has_value());
    REQUIRE(C.get_node(1000).has_value());
}

// ──────────────────────────────────────────────────────────────────────────────
// LWW attribute update ordering
//
// Timestamps must be anchored to the actual delta the engine emitted so that
// "newer" and "stale" are meaningful relative to the stored attr version.
// ──────────────────────────────────────────────────────────────────────────────

// Return the LWW version timestamp from a node insertion delta.
// The engine stores all attrs with this timestamp as their LWW version, so
// any attr-batch update must use a timestamp > this to be accepted.
static uint64_t node_version_ts(const NodeMutationEffect& effect)
{
    REQUIRE(effect.node_delta.has_value());
    const auto* lww = std::get_if<LWWNodeMsg>(&*effect.node_delta);
    REQUIRE(lww != nullptr);
    return lww->timestamp;
}

// Same for edge insertion deltas.
static uint64_t edge_version_ts(const EdgeMutationEffect& effect)
{
    REQUIRE(effect.edge_delta.has_value());
    const auto* lww = std::get_if<LWWEdgeMsg>(&*effect.edge_delta);
    REQUIRE(lww != nullptr);
    return lww->timestamp;
}

TEST_CASE("FakeNetwork: LWW stale attr update (older ts) is rejected", "[FAKE_NET][LWW][REORDER]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    auto effect = A.insert_node(make_robot(1000, 1, "robot_a"));
    REQUIRE(effect.applied);
    const uint64_t base = node_version_ts(effect);
    net.post(1, effect);
    net.deliver_all();

    // Apply newer update first (base+200 → level=9), then a stale one (base-1 → level=3).
    B.engine->apply_remote_node_attr_batch(
        NodeAttrDeltaBatchMessage{make_attr_batch(1000, 3, 9,  base + 200)});  // newer
    B.engine->apply_remote_node_attr_batch(
        NodeAttrDeltaBatchMessage{make_attr_batch(1000, 3, 3,  base - 1)});   // stale

    auto node = B.get_node(1000);
    REQUIRE(node.has_value());
    REQUIRE(node->attrs().at("level").dec() == 9);  // stale must not overwrite
}

TEST_CASE("FakeNetwork: LWW out-of-order attr delivery settles on highest timestamp",
          "[FAKE_NET][LWW][REORDER]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    auto effect = A.insert_node(make_robot(1000, 1, "robot_a"));
    REQUIRE(effect.applied);
    const uint64_t base = node_version_ts(effect);
    net.post(1, effect);
    net.deliver_all();

    // Deliver to each engine in ascending timestamp order (older first).
    // Both should end up with the value at base+300.
    for (auto* eng : {A.engine.get(), B.engine.get()}) {
        eng->apply_remote_node_attr_batch(
            NodeAttrDeltaBatchMessage{make_attr_batch(1000, 3, 42, base + 200)});
        eng->apply_remote_node_attr_batch(
            NodeAttrDeltaBatchMessage{make_attr_batch(1000, 3, 99, base + 300)});
    }

    REQUIRE(A.get_node(1000)->attrs().at("level").dec() == 99);
    REQUIRE(B.get_node(1000)->attrs().at("level").dec() == 99);
}

TEST_CASE("FakeNetwork: LWW reverse attr delivery still converges to highest timestamp",
          "[FAKE_NET][LWW][REORDER]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    auto effect = A.insert_node(make_robot(1000, 1, "robot_a"));
    REQUIRE(effect.applied);
    const uint64_t base = node_version_ts(effect);
    net.post(1, effect);
    net.deliver_all();

    // Newer batch arrives first at both engines, then the older one.
    // Both should still settle on base+300.
    for (auto* eng : {A.engine.get(), B.engine.get()}) {
        eng->apply_remote_node_attr_batch(
            NodeAttrDeltaBatchMessage{make_attr_batch(1000, 3, 99, base + 300)});  // newer first
        eng->apply_remote_node_attr_batch(
            NodeAttrDeltaBatchMessage{make_attr_batch(1000, 3, 42, base + 200)});  // older second
    }

    REQUIRE(A.get_node(1000)->attrs().at("level").dec() == 99);
    REQUIRE(B.get_node(1000)->attrs().at("level").dec() == 99);
}

// ──────────────────────────────────────────────────────────────────────────────
// LWW delete vs. insert conflicts
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("FakeNetwork: LWW delete (newer ts) suppresses concurrent stale insert",
          "[FAKE_NET][LWW][CONFLICT]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    // Both agents have node 1000.
    {
        auto e = A.insert_node(make_robot(1000, 1, "robot_a"));
        REQUIRE(e.applied);
        net.post(1, e);
        net.deliver_all();
    }

    // A deletes node 1000 and retrieves the tombstone timestamp.
    auto del_effect = A.delete_node(1000);
    REQUIRE(del_effect.applied);
    REQUIRE(del_effect.node_delta.has_value());
    const uint64_t ts_del = tombstone_ts(del_effect);
    REQUIRE(ts_del > 0);

    net.post(1, del_effect);
    // A "remote" agent 3 sends a stale insert (ts < tombstone).
    net.post_raw(3, NodeDeltaMessage{make_insert_msg(1000, 3, "ghost", ts_del - 1)});
    net.deliver_all();

    REQUIRE_FALSE(A.get_node(1000).has_value());
    REQUIRE_FALSE(B.get_node(1000).has_value());
}

TEST_CASE("FakeNetwork: LWW newer remote insert (higher ts) overrides prior delete",
          "[FAKE_NET][LWW][CONFLICT]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    // Both agents have node 1000.
    {
        auto e = A.insert_node(make_robot(1000, 1, "robot_a"));
        REQUIRE(e.applied);
        net.post(1, e);
        net.deliver_all();
    }

    // A deletes node 1000 and retrieves the tombstone timestamp.
    auto del_effect = A.delete_node(1000);
    REQUIRE(del_effect.applied);
    const uint64_t ts_del = tombstone_ts(del_effect);
    REQUIRE(ts_del > 0);

    net.post(1, del_effect);
    // A "remote" agent 3 sends a fresher insert (ts > tombstone).
    net.post_raw(3, NodeDeltaMessage{make_insert_msg(1000, 3, "phoenix", ts_del + 10)});
    net.deliver_all();

    auto nodeA = A.get_node(1000);
    auto nodeB = B.get_node(1000);
    REQUIRE(nodeA.has_value());
    REQUIRE(nodeB.has_value());
    REQUIRE(nodeA->name() == "phoenix");
    REQUIRE(nodeB->name() == "phoenix");
}

// ──────────────────────────────────────────────────────────────────────────────
// Delivery controls: drop + full-graph recovery
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("FakeNetwork: dropped deltas are recovered by full-graph sync",
          "[FAKE_NET][LWW][RECOVERY]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    auto e1 = A.insert_node(make_robot(1000, 1, "robot_a"));
    auto e2 = A.insert_node(make_robot(2000, 1, "robot_b"));
    REQUIRE(e1.applied);
    REQUIRE(e2.applied);
    net.post(1, e1);
    net.post(1, e2);

    // Drop all queued deltas — B misses everything.
    net.drop_next(net.pending());
    REQUIRE(net.pending() == 0);

    REQUIRE_FALSE(B.get_node(1000).has_value());
    REQUIRE_FALSE(B.get_node(2000).has_value());

    // Full-graph sync recovers B.
    net.sync_full_graph(1, 2);

    REQUIRE(B.get_node(1000).has_value());
    REQUIRE(B.get_node(2000).has_value());
    REQUIRE(A.size() == B.size());
}

TEST_CASE("FakeNetwork: partial drop then full-graph sync", "[FAKE_NET][LWW][RECOVERY]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    // 4 inserts; drop the first 2.
    for (uint64_t id = 1000; id < 1004; ++id) {
        auto e = A.insert_node(make_robot(id, 1, "r" + std::to_string(id)));
        REQUIRE(e.applied);
        net.post(1, e);
    }
    net.drop_next(2);          // B misses nodes 1000 and 1001
    net.deliver_all();         // B gets nodes 1002 and 1003

    REQUIRE_FALSE(B.get_node(1000).has_value());
    REQUIRE_FALSE(B.get_node(1001).has_value());
    REQUIRE(B.get_node(1002).has_value());
    REQUIRE(B.get_node(1003).has_value());

    net.sync_full_graph(1, 2); // catch up
    REQUIRE(A.size() == B.size());
}

// ──────────────────────────────────────────────────────────────────────────────
// Partition / rejoin
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("FakeNetwork: partitioned agent catches up via buffered messages",
          "[FAKE_NET][LWW][PARTITION]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    // Initial shared state: both have node 1000.
    {
        auto e = A.insert_node(make_robot(1000, 1, "robot_a"));
        REQUIRE(e.applied);
        net.post(1, e);
        net.deliver_all();
    }
    REQUIRE(B.get_node(1000).has_value());

    // Partition B.
    net.partition(2);

    // A inserts nodes 2000 and 3000 while B is partitioned.
    for (uint64_t id = 2000; id < 2003; ++id) {
        auto e = A.insert_node(make_robot(id, 1, "r" + std::to_string(id)));
        REQUIRE(e.applied);
        net.post(1, e);
    }
    net.deliver_all();  // B's share goes to partition buffer

    // B is still missing the new nodes.
    REQUIRE_FALSE(B.get_node(2000).has_value());
    REQUIRE_FALSE(B.get_node(2001).has_value());
    REQUIRE_FALSE(B.get_node(2002).has_value());

    // Rejoin: buffer is flushed into B.
    net.unpartition(2);

    REQUIRE(B.get_node(2000).has_value());
    REQUIRE(B.get_node(2001).has_value());
    REQUIRE(B.get_node(2002).has_value());
    REQUIRE(A.size() == B.size());
}

TEST_CASE("FakeNetwork: cold-start agent synced via full-graph import",
          "[FAKE_NET][LWW][PARTITION]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    // A accumulates nodes without B ever receiving deltas.
    for (uint64_t id = 1000; id < 1005; ++id) {
        auto e = A.insert_node(make_robot(id, 1, "r" + std::to_string(id)));
        REQUIRE(e.applied);
    }

    REQUIRE(A.size() == 5);
    REQUIRE(B.size() == 0);

    net.sync_full_graph(1, 2);

    REQUIRE(B.size() == 5);
    REQUIRE(A.size() == B.size());
}

// ──────────────────────────────────────────────────────────────────────────────
// Edge synchronisation
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("FakeNetwork: LWW edge insert propagates", "[FAKE_NET][LWW][EDGE]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    // Sync endpoint nodes.
    for (uint64_t id : {uint64_t{10}, uint64_t{11}}) {
        auto e = A.insert_node(make_robot(id, 1, "r" + std::to_string(id)));
        REQUIRE(e.applied);
        net.post(1, e);
    }
    net.deliver_all();

    // Insert edge on A and sync.
    auto edge = Edge::create<RT_edge_type>(10, 11);
    edge.agent_id(1);
    auto ee = A.insert_edge(std::move(edge));
    REQUIRE(ee.applied);
    net.post(1, ee);
    net.deliver_all();

    REQUIRE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());
}

TEST_CASE("FakeNetwork: LWW edge delete propagates", "[FAKE_NET][LWW][EDGE]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    // Setup: both agents have the edge.
    for (uint64_t id : {uint64_t{10}, uint64_t{11}}) {
        auto e = A.insert_node(make_robot(id, 1, "r" + std::to_string(id)));
        REQUIRE(e.applied);
        net.post(1, e);
    }
    {
        auto edge = Edge::create<RT_edge_type>(10, 11);
        edge.agent_id(1);
        auto ee = A.insert_edge(std::move(edge));
        REQUIRE(ee.applied);
        net.post(1, ee);
    }
    net.deliver_all();
    REQUIRE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // A deletes edge and syncs.
    auto de = A.delete_edge(10, 11, std::string(RT_edge_type::attr_name));
    REQUIRE(de.applied);
    net.post(1, de);
    net.deliver_all();

    REQUIRE_FALSE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());
}

// ──────────────────────────────────────────────────────────────────────────────
// Out-of-order delivery: edge delta before endpoint nodes
//
// apply_remote_edge_delta buffers the delta when either endpoint node is absent.
// The edge materializes automatically when the missing node arrives.
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("FakeNetwork: LWW edge delta arrives before 'to' node: buffered and applied",
          "[FAKE_NET][LWW][REORDER][EDGE]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    auto e_from = A.insert_node(make_robot(10, 1, "r_from"));
    auto e_to   = A.insert_node(make_robot(11, 1, "r_to"));
    auto edge   = Edge::create<RT_edge_type>(10, 11);
    edge.agent_id(1);
    auto e_edge = A.insert_edge(std::move(edge));
    REQUIRE(e_from.applied);
    REQUIRE(e_to.applied);
    REQUIRE(e_edge.applied);

    // Deliver the 'from' node so B knows about it.
    net.post(1, e_from);
    net.deliver_all();
    REQUIRE(B.get_node(10).has_value());

    // Queue: edge first, then 'to' node.
    net.post(1, e_edge);
    net.post(1, e_to);

    // Edge arrives: B has 'from' but not 'to' → buffered, not yet materialized.
    net.deliver_next(1);
    REQUIRE_FALSE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // 'to' node arrives: both endpoints now present, pending edge materializes.
    net.deliver_next(1);
    REQUIRE(B.get_node(11).has_value());
    REQUIRE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());
}

TEST_CASE("FakeNetwork: LWW edge delta arrives before 'from' node: buffered and applied",
          "[FAKE_NET][LWW][REORDER][EDGE]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    auto e_from = A.insert_node(make_robot(10, 1, "r_from"));
    auto e_to   = A.insert_node(make_robot(11, 1, "r_to"));
    auto edge   = Edge::create<RT_edge_type>(10, 11);
    edge.agent_id(1);
    auto e_edge = A.insert_edge(std::move(edge));
    REQUIRE(e_from.applied);
    REQUIRE(e_to.applied);
    REQUIRE(e_edge.applied);

    // Deliver the 'to' node so B has the target but not the source.
    net.post(1, e_to);
    net.deliver_all();
    REQUIRE(B.get_node(11).has_value());
    REQUIRE_FALSE(B.get_node(10).has_value());

    // Queue: edge first, then 'from' node.
    net.post(1, e_edge);
    net.post(1, e_from);

    // Edge arrives: B has 'to' but not 'from' → buffered, not yet materialized.
    net.deliver_next(1);
    REQUIRE_FALSE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // 'from' node arrives: both endpoints now present, pending edge materializes.
    net.deliver_next(1);
    REQUIRE(B.get_node(10).has_value());
    REQUIRE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());
}

// ──────────────────────────────────────────────────────────────────────────────
// Out-of-order delivery: attr batch before parent creation
//
// apply_remote_node_attr_batch and apply_remote_edge_attr_batch buffer items
// whose parent entity does not yet exist. The buffered updates are applied
// automatically when the parent materializes.
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("FakeNetwork: LWW node attr batch arrives before node creation: buffered and applied",
          "[FAKE_NET][LWW][REORDER]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    // A inserts the node (level=0), then bumps level to 99.
    auto e_ins = A.insert_node(make_robot(1000, 1, "robot_a"));
    REQUIRE(e_ins.applied);
    const uint64_t base = node_version_ts(e_ins);

    auto updated = make_robot(1000, 1, "robot_a");
    updated.attrs()["level"] = Attribute(99, base, 1);
    auto e_upd = A.update_node(std::move(updated));
    REQUIRE(e_upd.applied);
    REQUIRE(e_upd.node_attr_batch.has_value());  // metadata unchanged → attr-batch only

    // B receives the attr batch FIRST; node doesn't exist → buffered.
    net.post_raw(1, *e_upd.node_attr_batch);
    net.deliver_all();
    REQUIRE_FALSE(B.get_node(1000).has_value());

    // B then receives the original node insertion delta (level=0).
    // On arrival the buffered attr batch is applied: level becomes 99.
    net.post(1, e_ins);
    net.deliver_all();

    auto node = B.get_node(1000);
    REQUIRE(node.has_value());
    REQUIRE(node->attrs().at("level").dec() == 99);  // buffered attr applied on node arrival
}

TEST_CASE("FakeNetwork: LWW edge attr batch arrives before edge creation: buffered and applied",
          "[FAKE_NET][LWW][REORDER][EDGE]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    // Sync endpoint nodes to both agents.
    for (uint64_t id : {uint64_t{10}, uint64_t{11}}) {
        auto e = A.insert_node(make_robot(id, 1, "r" + std::to_string(id)));
        REQUIRE(e.applied);
        net.post(1, e);
    }
    net.deliver_all();

    // A inserts the edge (no attrs), then adds a 'weight' attr.
    auto edge1 = Edge::create<RT_edge_type>(10, 11);
    edge1.agent_id(1);
    auto e_edge = A.insert_edge(std::move(edge1));
    REQUIRE(e_edge.applied);
    REQUIRE(e_edge.edge_delta.has_value());
    const uint64_t base = edge_version_ts(e_edge);

    auto edge2 = Edge::create<RT_edge_type>(10, 11);
    edge2.agent_id(1);
    edge2.attrs()["weight"] = Attribute(99, base, 1);
    auto e_upd = A.insert_edge(std::move(edge2));  // update: edge already exists in A
    REQUIRE(e_upd.applied);
    REQUIRE(e_upd.edge_attr_batch.has_value());  // existing edge → attr-batch only

    // B receives the attr batch FIRST; edge doesn't exist → buffered.
    net.post_raw(1, *e_upd.edge_attr_batch);
    net.deliver_all();
    REQUIRE_FALSE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // B then receives the original edge insertion delta (no weight attr).
    // On arrival the buffered attr batch is applied: weight becomes 99.
    net.post(1, e_edge);
    net.deliver_all();

    auto e = B.get_edge(10, 11, std::string(RT_edge_type::attr_name));
    REQUIRE(e.has_value());
    REQUIRE(e->attrs().contains("weight"));  // buffered attr applied on edge arrival
    REQUIRE(e->attrs().at("weight").dec() == 99);
}

// ──────────────────────────────────────────────────────────────────────────────
// Attr updates targeting deleted entities
//
// A stale or reordered attr batch that arrives after the target node/edge has
// been deleted must be silently discarded. It must not recreate the entity.
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("FakeNetwork: LWW node attr batch for deleted node is silently ignored",
          "[FAKE_NET][LWW][REORDER]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    // Both agents have node 1000.
    auto e_ins = A.insert_node(make_robot(1000, 1, "robot_a"));
    REQUIRE(e_ins.applied);
    const uint64_t base = node_version_ts(e_ins);
    net.post(1, e_ins);
    net.deliver_all();
    REQUIRE(B.get_node(1000).has_value());

    // A deletes node 1000; deletion propagates to B.
    auto e_del = A.delete_node(1000);
    REQUIRE(e_del.applied);
    net.post(1, e_del);
    net.deliver_all();
    REQUIRE_FALSE(B.get_node(1000).has_value());

    // A stale/reordered attr batch for the now-deleted node arrives at B.
    B.engine->apply_remote_node_attr_batch(
        NodeAttrDeltaBatchMessage{make_attr_batch(1000, 3, 99, base + 100)});

    // Node must remain absent — the attr batch must not resurrect it.
    REQUIRE_FALSE(B.get_node(1000).has_value());
}

TEST_CASE("FakeNetwork: LWW edge attr batch for deleted edge is silently ignored",
          "[FAKE_NET][LWW][REORDER][EDGE]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::LWW);
    auto& B = net.add_agent(2, SyncMode::LWW);

    // Both agents have endpoint nodes and the edge.
    for (uint64_t id : {uint64_t{10}, uint64_t{11}}) {
        auto e = A.insert_node(make_robot(id, 1, "r" + std::to_string(id)));
        REQUIRE(e.applied);
        net.post(1, e);
    }
    auto edge = Edge::create<RT_edge_type>(10, 11);
    edge.agent_id(1);
    auto e_edge = A.insert_edge(std::move(edge));
    REQUIRE(e_edge.applied);
    const uint64_t base = edge_version_ts(e_edge);
    net.post(1, e_edge);
    net.deliver_all();
    REQUIRE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // A deletes the edge; deletion propagates to B.
    auto e_del = A.delete_edge(10, 11, std::string(RT_edge_type::attr_name));
    REQUIRE(e_del.applied);
    net.post(1, e_del);
    net.deliver_all();
    REQUIRE_FALSE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // A stale/reordered attr batch for the deleted edge arrives at B.
    B.engine->apply_remote_edge_attr_batch(
        EdgeAttrDeltaBatchMessage{make_edge_attr_batch(10, 11, 3, 99, base + 100)});

    // Edge must remain absent — the attr batch must not recreate it.
    REQUIRE_FALSE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());
}

// ──────────────────────────────────────────────────────────────────────────────
// Scenario: pending items evicted by tombstone before parent materializes
//
// A buffered attr that arrives before its parent node/edge must be discarded
// if the parent is subsequently tombstoned before it ever materializes.
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("FakeNetwork: LWW pending node attr discarded when node tombstoned before creation",
          "[FAKE_NET][LWW][REORDER]")
{
    FakeNetwork net;
    auto& agent = net.add_agent(1, SyncMode::LWW);
    auto& eng = *agent.engine;

    // T=200: attr for node 1000 → buffered (node absent, no tombstone).
    eng.apply_remote_node_attr_batch(
        NodeAttrDeltaBatchMessage{make_attr_batch(1000, 2, 99, 200)});
    REQUIRE_FALSE(eng.get_node(1000).has_value());

    // T=300: delete node 1000 → tombstone evicts the pending attr.
    eng.apply_remote_node_delta(NodeDeltaMessage{make_node_delete_msg(1000, 2, 300)});
    REQUIRE_FALSE(eng.get_node(1000).has_value());

    // T=100: stale node insertion → rejected by tombstone (100 < 300).
    eng.apply_remote_node_delta(NodeDeltaMessage{make_insert_msg(1000, 2, "robot_a", 100)});
    REQUIRE_FALSE(eng.get_node(1000).has_value());
}

TEST_CASE("FakeNetwork: LWW pending edge attr discarded when edge tombstoned before creation",
          "[FAKE_NET][LWW][REORDER][EDGE]")
{
    FakeNetwork net;
    auto& agent = net.add_agent(1, SyncMode::LWW);
    auto& eng = *agent.engine;

    // Put endpoint nodes in place.
    eng.apply_remote_node_delta(NodeDeltaMessage{make_insert_msg(10, 1, "r_from", 50)});
    eng.apply_remote_node_delta(NodeDeltaMessage{make_insert_msg(11, 1, "r_to", 50)});

    // T=300: edge attr → buffered (edge absent, no tombstone).
    eng.apply_remote_edge_attr_batch(
        EdgeAttrDeltaBatchMessage{make_edge_attr_batch(10, 11, 2, 99, 300)});
    REQUIRE_FALSE(eng.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // T=400: delete edge → tombstone at 400. Pending attr (T=300 < 400) evicted.
    eng.apply_remote_edge_delta(EdgeDeltaMessage{make_edge_msg(10, 11, 2, 400, true)});
    REQUIRE_FALSE(eng.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // T=200: stale edge insertion → rejected by tombstone (200 < 400).
    eng.apply_remote_edge_delta(EdgeDeltaMessage{make_edge_msg(10, 11, 2, 200)});
    REQUIRE_FALSE(eng.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());
}

// ──────────────────────────────────────────────────────────────────────────────
// Scenario 5: stale edge delete + newer pending edge attr + edge creation
//
// A pending edge attr with a newer timestamp must survive an older edge delete
// tombstone and be applied once the edge materializes.
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("FakeNetwork: LWW newer edge attr survives older delete tombstone",
          "[FAKE_NET][LWW][REORDER][EDGE]")
{
    FakeNetwork net;
    auto& agent = net.add_agent(1, SyncMode::LWW);
    auto& eng = *agent.engine;

    // Endpoint nodes exist.
    eng.apply_remote_node_delta(NodeDeltaMessage{make_insert_msg(10, 1, "r_from", 50)});
    eng.apply_remote_node_delta(NodeDeltaMessage{make_insert_msg(11, 1, "r_to", 50)});

    // T=300: edge attr arrives first — buffered (no edge, no tombstone).
    eng.apply_remote_edge_attr_batch(
        EdgeAttrDeltaBatchMessage{make_edge_attr_batch(10, 11, 2, 99, 300)});
    REQUIRE_FALSE(eng.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // T=100: stale edge delete → tombstone at 100.
    //        Pending attr (T=300 > 100) is newer → KEPT in buffer.
    eng.apply_remote_edge_delta(EdgeDeltaMessage{make_edge_msg(10, 11, 2, 100, true)});
    REQUIRE_FALSE(eng.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // T=200: edge insert → tombstone check: 200 > 100 → materializes.
    //        Pending attr (T=300) flushed and applied.
    eng.apply_remote_edge_delta(EdgeDeltaMessage{make_edge_msg(10, 11, 2, 200)});
    auto edge = eng.get_edge(10, 11, std::string(RT_edge_type::attr_name));
    REQUIRE(edge.has_value());
    REQUIRE(edge->attrs().contains("weight"));
    REQUIRE(edge->attrs().at("weight").dec() == 99);
}

// ──────────────────────────────────────────────────────────────────────────────
// Scenario 6: chained out-of-order: edge attr → edge → node A → node B
//
// All four messages arrive in reverse dependency order. Eventual consistency
// is reached purely through pending buffers — no full-graph sync needed.
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("FakeNetwork: LWW chained out-of-order edge attr then edge then nodes",
          "[FAKE_NET][LWW][REORDER][EDGE]")
{
    FakeNetwork net;
    auto& agent = net.add_agent(1, SyncMode::LWW);
    auto& eng = *agent.engine;

    // Arrival order (all out-of-order relative to dependency graph):
    // 1) edge attr (weight=42) → no edge yet → buffered in pending_edge_attrs_
    eng.apply_remote_edge_attr_batch(
        EdgeAttrDeltaBatchMessage{make_edge_attr_batch(10, 11, 2, 42, 400)});
    REQUIRE_FALSE(eng.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // 2) edge (from=10, to=11) → nodes 10 and 11 absent → buffered in pending_edges_
    eng.apply_remote_edge_delta(EdgeDeltaMessage{make_edge_msg(10, 11, 2, 200)});
    REQUIRE_FALSE(eng.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // 3) node 10 arrives → flush_pending_edges: edge still waiting for node 11.
    eng.apply_remote_node_delta(NodeDeltaMessage{make_insert_msg(10, 2, "r_from", 100)});
    REQUIRE(eng.get_node(10).has_value());
    REQUIRE_FALSE(eng.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // 4) node 11 arrives → flush_pending_edges: both nodes present → edge materializes
    //    → pending_edge_attrs_ flushed → weight=42 applied.
    eng.apply_remote_node_delta(NodeDeltaMessage{make_insert_msg(11, 2, "r_to", 100)});
    REQUIRE(eng.get_node(11).has_value());

    auto edge = eng.get_edge(10, 11, std::string(RT_edge_type::attr_name));
    REQUIRE(edge.has_value());
    REQUIRE(edge->attrs().contains("weight"));
    REQUIRE(edge->attrs().at("weight").dec() == 42);
}

// ══════════════════════════════════════════════════════════════════════════════
// CRDT backend — out-of-order delivery scenarios
//
// The CRDT engine buffers deltas in unprocessed_delta_* maps keyed on the
// missing parent. Buffered items are replayed automatically once the parent
// materialises (consume_unprocessed_deltas). The check applied is a timestamp
// comparison: buffered items are applied only when their timestamp is strictly
// greater than the materialising parent's timestamp.
//
// Scenario 5 (stale delete + newer pending attr) is NOT tested here because
// the CRDT engine evicts ALL pending edge attrs on any edge delete regardless
// of per-item timestamps — its delete semantics differ from LWW.
// ══════════════════════════════════════════════════════════════════════════════

TEST_CASE("FakeNetwork: CRDT edge delta arrives before 'to' node: buffered and applied",
          "[FAKE_NET][CRDT][REORDER][EDGE]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::CRDT);
    auto& B = net.add_agent(2, SyncMode::CRDT);

    auto e_from = A.insert_node(make_robot(10, 1, "r_from"));
    auto e_to   = A.insert_node(make_robot(11, 1, "r_to"));
    auto edge   = Edge::create<RT_edge_type>(10, 11);
    edge.agent_id(1);
    auto e_edge = A.insert_edge(std::move(edge));
    REQUIRE(e_from.applied);
    REQUIRE(e_to.applied);
    REQUIRE(e_edge.applied);

    // Deliver the 'from' node so B knows about it.
    net.post(1, e_from);
    net.deliver_all();
    REQUIRE(B.get_node(10).has_value());

    // Queue: edge first, then 'to' node.
    net.post(1, e_edge);
    net.post(1, e_to);

    // Edge arrives: 'to' missing → buffered in unprocessed_delta_edge_to_.
    net.deliver_next(1);
    REQUIRE_FALSE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // 'to' node arrives → consume_unprocessed_deltas materialises the edge.
    net.deliver_next(1);
    REQUIRE(B.get_node(11).has_value());
    REQUIRE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());
}

TEST_CASE("FakeNetwork: CRDT edge delta arrives before 'from' node: buffered and applied",
          "[FAKE_NET][CRDT][REORDER][EDGE]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::CRDT);
    auto& B = net.add_agent(2, SyncMode::CRDT);

    auto e_from = A.insert_node(make_robot(10, 1, "r_from"));
    auto e_to   = A.insert_node(make_robot(11, 1, "r_to"));
    auto edge   = Edge::create<RT_edge_type>(10, 11);
    edge.agent_id(1);
    auto e_edge = A.insert_edge(std::move(edge));
    REQUIRE(e_from.applied);
    REQUIRE(e_to.applied);
    REQUIRE(e_edge.applied);

    // Deliver the 'to' node so B has the target but not the source.
    net.post(1, e_to);
    net.deliver_all();
    REQUIRE(B.get_node(11).has_value());
    REQUIRE_FALSE(B.get_node(10).has_value());

    // Queue: edge first, then 'from' node.
    net.post(1, e_edge);
    net.post(1, e_from);

    // Edge arrives: 'from' missing → buffered in unprocessed_delta_edge_from_.
    net.deliver_next(1);
    REQUIRE_FALSE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // 'from' node arrives → consume_unprocessed_deltas materialises the edge.
    net.deliver_next(1);
    REQUIRE(B.get_node(10).has_value());
    REQUIRE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());
}

TEST_CASE("FakeNetwork: CRDT pending edge update signal keeps original direction when 'from' node arrives",
          "[FAKE_NET][CRDT][REORDER][EDGE][SIGNAL]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::CRDT);
    auto& B = net.add_agent(2, SyncMode::CRDT);

    auto e_from = A.insert_node(make_robot(10, 1, "r_from"));
    auto e_to   = A.insert_node(make_robot(11, 1, "r_to"));
    auto edge   = Edge::create<RT_edge_type>(10, 11);
    edge.agent_id(1);
    auto e_edge = A.insert_edge(std::move(edge));
    REQUIRE(e_from.applied);
    REQUIRE(e_to.applied);
    REQUIRE(e_edge.applied);

    net.post(1, e_to);
    net.deliver_all();
    REQUIRE(B.get_node(11).has_value());
    B.host.edge_updates.clear();

    net.post(1, e_edge);
    net.post(1, e_from);

    net.deliver_next(1);
    REQUIRE(B.host.edge_updates.empty());
    REQUIRE_FALSE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    net.deliver_next(1);
    REQUIRE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());
    REQUIRE_FALSE(B.host.edge_updates.empty());
    const auto& update = B.host.edge_updates.back();
    REQUIRE(update.from == 10);
    REQUIRE(update.to == 11);
    REQUIRE(update.type == std::string(RT_edge_type::attr_name));
    REQUIRE(update.agent_id == 1);
}

TEST_CASE("FakeNetwork: CRDT node attr batch arrives before node creation: buffered and applied",
          "[FAKE_NET][CRDT][REORDER]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::CRDT);
    auto& B = net.add_agent(2, SyncMode::CRDT);

    // A inserts node (level=0), then bumps level to 99.
    auto e_ins = A.insert_node(make_robot(1000, 1, "robot_a"));
    REQUIRE(e_ins.applied);

    auto updated = make_robot(1000, 1, "robot_a");
    updated.attrs()["level"] = Attribute(99, 100, 1);
    auto e_upd = A.update_node(std::move(updated));
    REQUIRE(e_upd.applied);
    REQUIRE(e_upd.node_attr_batch.has_value());  // update_node → attr-batch only, no node_delta

    // B receives the attr batch FIRST; node doesn't exist → buffered in
    // unprocessed_delta_node_att_. post() of a NodeMutationEffect with no
    // node_delta queues only the attr batch.
    net.post(1, e_upd);
    net.deliver_all();
    REQUIRE_FALSE(B.get_node(1000).has_value());

    // B then receives the original node insertion delta.
    // On arrival, consume_unprocessed_deltas applies the buffered level=99 attr
    // (provided the attr's timestamp is strictly newer than the node's timestamp).
    net.post(1, e_ins);
    net.deliver_all();

    auto node = B.get_node(1000);
    REQUIRE(node.has_value());
    REQUIRE(node->attrs().at("level").dec() == 99);
}

TEST_CASE("FakeNetwork: CRDT edge attr batch arrives before edge creation: buffered and applied",
          "[FAKE_NET][CRDT][REORDER][EDGE]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::CRDT);
    auto& B = net.add_agent(2, SyncMode::CRDT);

    // Sync endpoint nodes to both agents.
    for (uint64_t id : {uint64_t{10}, uint64_t{11}}) {
        auto e = A.insert_node(make_robot(id, 1, "r" + std::to_string(id)));
        REQUIRE(e.applied);
        net.post(1, e);
    }
    net.deliver_all();

    // A inserts the edge (no attrs), then adds a 'weight' attr.
    auto edge1 = Edge::create<RT_edge_type>(10, 11);
    edge1.agent_id(1);
    auto e_edge = A.insert_edge(std::move(edge1));
    REQUIRE(e_edge.applied);
    REQUIRE(e_edge.edge_delta.has_value());

    auto edge2 = Edge::create<RT_edge_type>(10, 11);
    edge2.agent_id(1);
    edge2.attrs()["weight"] = Attribute(99, 100, 1);
    auto e_upd = A.insert_edge(std::move(edge2));  // update: edge exists in A
    REQUIRE(e_upd.applied);
    REQUIRE(e_upd.edge_attr_batch.has_value());  // existing edge → attr-batch only, no edge_delta

    // B receives the attr batch FIRST (post() of EdgeMutationEffect with no edge_delta
    // queues only the attr batch). Edge absent → buffered in unprocessed_delta_edge_att_.
    net.post(1, e_upd);
    net.deliver_all();
    REQUIRE_FALSE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // B then receives the original edge insertion delta.
    // On arrival, consume_unprocessed_deltas applies the buffered weight=99 attr.
    net.post_raw(1, *e_edge.edge_delta);
    net.deliver_all();

    auto e = B.get_edge(10, 11, std::string(RT_edge_type::attr_name));
    REQUIRE(e.has_value());
    REQUIRE(e->attrs().contains("weight"));
    REQUIRE(e->attrs().at("weight").dec() == 99);
}

TEST_CASE("FakeNetwork: CRDT chained out-of-order edge attr then edge then nodes",
          "[FAKE_NET][CRDT][REORDER][EDGE]")
{
    FakeNetwork net;
    auto& A = net.add_agent(1, SyncMode::CRDT);
    auto& B = net.add_agent(2, SyncMode::CRDT);

    // Build all state on A first.
    auto e_from = A.insert_node(make_robot(10, 1, "r_from"));
    auto e_to   = A.insert_node(make_robot(11, 1, "r_to"));
    REQUIRE(e_from.applied);
    REQUIRE(e_to.applied);

    auto edge1 = Edge::create<RT_edge_type>(10, 11);
    edge1.agent_id(1);
    auto e_edge = A.insert_edge(std::move(edge1));
    REQUIRE(e_edge.applied);
    REQUIRE(e_edge.edge_delta.has_value());

    auto edge2 = Edge::create<RT_edge_type>(10, 11);
    edge2.agent_id(1);
    edge2.attrs()["weight"] = Attribute(42, 100, 1);
    auto e_upd = A.insert_edge(std::move(edge2));
    REQUIRE(e_upd.applied);
    REQUIRE(e_upd.edge_attr_batch.has_value());

    // Arrival order at B (all out-of-order):
    // 1) edge attr batch → no edge yet → buffered in unprocessed_delta_edge_att_
    net.post(1, e_upd);
    net.deliver_all();
    REQUIRE_FALSE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // 2) edge delta → 'from' node (10) absent → buffered in unprocessed_delta_edge_from_[10]
    net.post_raw(1, *e_edge.edge_delta);
    net.deliver_all();
    REQUIRE_FALSE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // 3) node 10 arrives → consume_unprocessed_deltas:
    //    - edge is stored in nodes_[10].fano[{11, type}]
    //    - edge attr (weight=42) is flushed from unprocessed_delta_edge_att_ and applied
    //    - get_edge still returns null because nodes_.contains(11) is false
    net.post(1, e_from);
    net.deliver_all();
    REQUIRE(B.get_node(10).has_value());
    REQUIRE_FALSE(B.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    // 4) node 11 arrives → get_crdt_edge_ptr now finds both endpoints.
    //    The edge (with weight=42 already merged) is visible.
    net.post(1, e_to);
    net.deliver_all();
    REQUIRE(B.get_node(11).has_value());

    auto edge = B.get_edge(10, 11, std::string(RT_edge_type::attr_name));
    REQUIRE(edge.has_value());
    REQUIRE(edge->attrs().contains("weight"));
    REQUIRE(edge->attrs().at("weight").dec() == 42);
}
