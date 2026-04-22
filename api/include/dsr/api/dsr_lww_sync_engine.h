#pragma once

#include "dsr/api/dsr_sync_engine.h"
#include "dsr/core/types/lww_types.h"

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>

namespace DSR {

class LWWSyncEngine final : public SyncEngine
{
public:
    using Version = LWW::Version;
    using Tombstone = LWW::Tombstone;

    explicit LWWSyncEngine(
        SyncEngineHost& host,
        uint64_t tombstone_window_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::minutes(5)).count());
    LWWSyncEngine(SyncEngineHost& host, const LWWSyncEngine& other);
    ~LWWSyncEngine() override = default;

    SyncBackendInfo backend_info() const override;
    std::unique_ptr<SyncEngine> clone(SyncEngineHost& host) const override;

    std::optional<Node> get_node(uint64_t id) const override;
    std::optional<Edge> get_edge(uint64_t from, uint64_t to, const std::string& type) const override;
    bool for_each_edge_from(uint64_t from, const OutgoingEdgeVisitor& visitor) const override;
    bool for_each_edge_to(uint64_t to, const IncomingEdgeVisitor& visitor) const override;
    void for_each_edge_of_type(const std::string& type, const TypedEdgeVisitor& visitor) const override;
    size_t size() const override;
    std::map<uint64_t, Node> snapshot() const override;

    NodeMutationEffect insert_node_local(Node&& node) override;
    NodeMutationEffect update_node_local(Node&& node) override;
    NodeMutationEffect delete_node_local(uint64_t id) override;

    EdgeMutationEffect insert_or_assign_edge_local(Edge&& edge) override;
    EdgeMutationEffect delete_edge_local(uint64_t from, uint64_t to, const std::string& type) override;

    void apply_remote_node_delta(NodeDeltaMessage&& delta) override;
    void apply_remote_edge_delta(EdgeDeltaMessage&& delta) override;
    void apply_remote_node_attr_batch(NodeAttrDeltaBatchMessage&& batch) override;
    void apply_remote_edge_attr_batch(EdgeAttrDeltaBatchMessage&& batch) override;
    void import_full_graph(FullGraphMessage&& full_graph) override;
    FullGraphMessage export_full_graph() const override;

    std::optional<Tombstone> node_tombstone(uint64_t id) const;
    std::optional<Tombstone> edge_tombstone(uint64_t from, uint64_t to, const std::string& type) const;
    std::optional<LWWNodeMsg> export_node_delta(uint64_t id) const;
    std::optional<LWWEdgeMsg> export_edge_delta(uint64_t from, uint64_t to, const std::string& type) const;

private:
    using AttrState = LWW::AttrState;
    using NodeState = LWW::NodeState;
    using EdgeState = LWW::EdgeState;
    using EdgeKey = LWW::EdgeKey;

    uint64_t current_time_ms() const;
    uint64_t next_timestamp();
    void prune_tombstones(uint64_t now);

    Node to_node(const NodeState& state) const;
    Edge to_edge(const EdgeState& state) const;

    void store_node_tombstone(uint64_t id, Version version, uint64_t now);
    void store_edge_tombstone(uint64_t from, uint64_t to, const std::string& type, Version version, uint64_t now);
    void erase_related_edges(uint64_t node_id, Version version, uint64_t now, std::vector<Edge>* removed_edges = nullptr);

    bool node_delta_is_stale(uint64_t id, const Version& version) const;
    bool edge_delta_is_stale(uint64_t from, uint64_t to, const std::string& type, const Version& version) const;

    SyncEngineHost& host_;
    uint64_t tombstone_window_ms_;
    uint64_t logical_clock_ms_{0};
    std::unordered_map<uint64_t, NodeState> nodes_;
    std::unordered_map<EdgeKey, EdgeState, hash_tuple> edges_;
    std::unordered_map<uint64_t, Tombstone> node_tombstones_;
    std::unordered_map<EdgeKey, Tombstone, hash_tuple> edge_tombstones_;
};

} // namespace DSR
