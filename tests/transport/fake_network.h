#pragma once

#include "dsr/api/dsr_crdt_sync_engine.h"
#include "dsr/api/dsr_lww_sync_engine.h"
#include "dsr/api/dsr_sync_engine.h"
#include "dsr/core/types/internal_types.h"
#include "dsr/core/types/user_types.h"

#include <algorithm>
#include <deque>
#include <memory>
#include <optional>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace DSR::Test {

// Minimal SyncEngineHost for use outside DSRGraph.
// update_maps_* are no-ops; convergence is verified by querying the engine directly.
struct FakeSyncHost final : DSR::SyncEngineHost
{
    uint32_t agent{};
    DSR::SyncMode mode{DSR::SyncMode::LWW};

    FakeSyncHost() = default;
    FakeSyncHost(uint32_t a, DSR::SyncMode m) : agent(a), mode(m) {}

    uint32_t local_agent_id() const override { return agent; }
    DSR::SyncMode local_sync_mode() const override { return mode; }
    bool is_copy_graph() const override { return false; }

    void update_maps_node_insert(uint64_t, std::string_view, std::string_view, const EdgeKeyList&) override {}
    void update_maps_node_delete(uint64_t, std::optional<std::string_view>, const EdgeKeyList&) override {}
    void update_maps_edge_insert(uint64_t, uint64_t, const std::string&) override {}
    void update_maps_edge_delete(uint64_t, uint64_t, const std::string&) override {}
};

// Envelope for an in-flight delta between agents.
struct PendingMessage
{
    uint32_t from_agent_id;
    using Payload = std::variant<
        DSR::NodeDeltaMessage,
        DSR::EdgeDeltaMessage,
        DSR::NodeAttrDeltaBatchMessage,
        DSR::EdgeAttrDeltaBatchMessage,
        DSR::FullGraphMessage>;
    Payload payload;
};

// One in-process agent: a host + a sync engine.
// FakeAgent is non-copyable and non-movable so that the engine's reference
// to host remains stable as long as the FakeAgent lives on the heap.
struct FakeAgent
{
    uint32_t id;
    DSR::SyncMode mode;
    FakeSyncHost host;
    std::unique_ptr<DSR::SyncEngine> engine;

    explicit FakeAgent(uint32_t id_, DSR::SyncMode mode_)
        : id(id_), mode(mode_), host{id_, mode_}
    {
        if (mode == DSR::SyncMode::LWW)
            engine = std::make_unique<DSR::LWWSyncEngine>(host);
        else
            engine = std::make_unique<DSR::CRDTSyncEngine>(host);
    }

    FakeAgent(const FakeAgent&)            = delete;
    FakeAgent& operator=(const FakeAgent&) = delete;
    FakeAgent(FakeAgent&&)                 = delete;
    FakeAgent& operator=(FakeAgent&&)      = delete;

    // ── Local mutations ───────────────────────────────────────────────────────

    DSR::NodeMutationEffect insert_node(DSR::Node n)
    {
        return engine->insert_node_local(std::move(n));
    }

    DSR::NodeMutationEffect update_node(DSR::Node n)
    {
        return engine->update_node_local(std::move(n));
    }

    DSR::NodeMutationEffect delete_node(uint64_t node_id)
    {
        return engine->delete_node_local(node_id);
    }

    DSR::EdgeMutationEffect insert_edge(DSR::Edge e)
    {
        return engine->insert_or_assign_edge_local(std::move(e));
    }

    DSR::EdgeMutationEffect delete_edge(uint64_t from, uint64_t to, const std::string& type)
    {
        return engine->delete_edge_local(from, to, type);
    }

    // ── Query ─────────────────────────────────────────────────────────────────

    std::optional<DSR::Node> get_node(uint64_t node_id) const
    {
        return engine->get_node(node_id);
    }

    std::optional<DSR::Edge> get_edge(uint64_t from, uint64_t to, const std::string& type) const
    {
        return engine->get_edge(from, to, type);
    }

    size_t size() const { return engine->size(); }
    std::map<uint64_t, DSR::Node> snapshot() const { return engine->snapshot(); }
};

// ──────────────────────────────────────────────────────────────────────────────
// FakeNetwork
//
// Owns a collection of FakeAgents and an ordered queue of PendingMessages.
// Tests control delivery explicitly: deliver_all(), deliver_next(), drop_next(),
// shuffle(). Partition/unpartition simulates network splits.
// ──────────────────────────────────────────────────────────────────────────────
class FakeNetwork
{
public:
    // Add a new agent to the network. Returns a stable reference.
    FakeAgent& add_agent(uint32_t id, DSR::SyncMode mode = DSR::SyncMode::LWW)
    {
        agents_.push_back(std::make_unique<FakeAgent>(id, mode));
        return *agents_.back();
    }

    FakeAgent* get_agent(uint32_t id)
    {
        for (auto& a : agents_)
            if (a->id == id) return a.get();
        return nullptr;
    }

    // ── Queue outgoing deltas from mutation effects ───────────────────────────

    void post(uint32_t from_id, const DSR::NodeMutationEffect& effect)
    {
        if (effect.node_delta.has_value())
            queue_.push_back({from_id, *effect.node_delta});
        if (effect.node_attr_batch.has_value())
            queue_.push_back({from_id, *effect.node_attr_batch});
        for (const auto& ed : effect.edge_deltas)
            queue_.push_back({from_id, ed});
    }

    void post(uint32_t from_id, const DSR::EdgeMutationEffect& effect)
    {
        if (effect.edge_delta.has_value())
            queue_.push_back({from_id, *effect.edge_delta});
        if (effect.edge_attr_batch.has_value())
            queue_.push_back({from_id, *effect.edge_attr_batch});
    }

    // Queue a manually crafted delta (for conflict tests with explicit timestamps).
    void post_raw(uint32_t from_id, DSR::NodeDeltaMessage msg)
    {
        queue_.push_back({from_id, std::move(msg)});
    }
    void post_raw(uint32_t from_id, DSR::EdgeDeltaMessage msg)
    {
        queue_.push_back({from_id, std::move(msg)});
    }
    void post_raw(uint32_t from_id, DSR::NodeAttrDeltaBatchMessage msg)
    {
        queue_.push_back({from_id, std::move(msg)});
    }
    void post_raw(uint32_t from_id, DSR::EdgeAttrDeltaBatchMessage msg)
    {
        queue_.push_back({from_id, std::move(msg)});
    }

    // ── Delivery controls ─────────────────────────────────────────────────────

    size_t pending() const { return queue_.size(); }

    // Deliver all queued messages in order.
    void deliver_all()
    {
        while (!queue_.empty())
            deliver_front();
    }

    // Deliver the next n messages in queue order.
    void deliver_next(size_t n = 1)
    {
        for (size_t i = 0; i < n && !queue_.empty(); ++i)
            deliver_front();
    }

    // Discard the next n messages without delivering.
    void drop_next(size_t n = 1)
    {
        for (size_t i = 0; i < n && !queue_.empty(); ++i)
            queue_.pop_front();
    }

    // Randomise the queue order. Pass an rng for reproducibility.
    void shuffle(std::mt19937& rng)
    {
        std::shuffle(queue_.begin(), queue_.end(), rng);
    }

    void shuffle()
    {
        std::mt19937 rng{std::random_device{}()};
        shuffle(rng);
    }

    // Stop delivering messages TO agent_id; buffer them instead.
    void partition(uint32_t agent_id)
    {
        partitioned_.insert(agent_id);
        partition_buffer_.emplace(agent_id, std::deque<PendingMessage>{});
    }

    // Resume delivery to agent_id and immediately flush its buffer.
    void unpartition(uint32_t agent_id)
    {
        partitioned_.erase(agent_id);
        if (auto it = partition_buffer_.find(agent_id); it != partition_buffer_.end()) {
            FakeAgent* target = get_agent(agent_id);
            if (target) {
                for (const auto& msg : it->second)
                    apply_to(*target, msg);
            }
            partition_buffer_.erase(it);
        }
    }

    // Directly import agent `from_id`'s full graph into agent `to_id`.
    // Simulates the graph-request / graph-answer handshake.
    void sync_full_graph(uint32_t from_id, uint32_t to_id)
    {
        FakeAgent* src = get_agent(from_id);
        FakeAgent* dst = get_agent(to_id);
        if (!src || !dst) return;
        dst->engine->import_full_graph(src->engine->export_full_graph());
    }

private:
    std::vector<std::unique_ptr<FakeAgent>> agents_;
    std::deque<PendingMessage> queue_;
    std::unordered_set<uint32_t> partitioned_;
    std::unordered_map<uint32_t, std::deque<PendingMessage>> partition_buffer_;

    void deliver_front()
    {
        PendingMessage msg = std::move(queue_.front());
        queue_.pop_front();
        for (auto& agent : agents_) {
            if (agent->id == msg.from_agent_id) continue;
            if (partitioned_.count(agent->id)) {
                partition_buffer_[agent->id].push_back(msg);
            } else {
                apply_to(*agent, msg);
            }
        }
    }

    static void apply_to(FakeAgent& agent, const PendingMessage& msg)
    {
        std::visit([&](const auto& payload) {
            using T = std::remove_cvref_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, DSR::NodeDeltaMessage>)
                agent.engine->apply_remote_node_delta(T{payload});
            else if constexpr (std::is_same_v<T, DSR::EdgeDeltaMessage>)
                agent.engine->apply_remote_edge_delta(T{payload});
            else if constexpr (std::is_same_v<T, DSR::NodeAttrDeltaBatchMessage>)
                agent.engine->apply_remote_node_attr_batch(T{payload});
            else if constexpr (std::is_same_v<T, DSR::EdgeAttrDeltaBatchMessage>)
                agent.engine->apply_remote_edge_attr_batch(T{payload});
            else if constexpr (std::is_same_v<T, DSR::FullGraphMessage>)
                agent.engine->import_full_graph(T{payload});
        }, msg.payload);
    }
};

} // namespace DSR::Test
