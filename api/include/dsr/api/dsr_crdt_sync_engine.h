#pragma once

#include "dsr/api/dsr_sync_engine.h"
#include "dsr/core/crdt/delta_crdt.h"
#include "dsr/core/types/crdt_types.h"

#include <map>
#include <optional>
#include <tuple>
#include <unordered_map>

namespace DSR {

class CRDTSyncEngine final : public SyncEngine
{
public:
    using Nodes = std::unordered_map<uint64_t, mvreg<CRDT::Node>>;
    friend class DSRGraph;
    friend class RT_API;

    explicit CRDTSyncEngine(SyncEngineHost& host);
    CRDTSyncEngine(SyncEngineHost& host, const CRDTSyncEngine& other);
    ~CRDTSyncEngine() override = default;

    SyncBackendInfo backend_info() const override;
    std::unique_ptr<SyncEngine> clone(SyncEngineHost& host) const override;

    std::optional<Node> get_node(uint64_t id) const override;
    std::optional<Edge> get_edge(uint64_t from, uint64_t to, const std::string& type) const override;
    bool with_node_attrs(uint64_t id, const NodeAttrsVisitor& visitor) const override;
    bool with_node_view(uint64_t id, const NodeViewVisitor& visitor) const override;
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

private:
    const CRDT::Node* get_node_ptr(uint64_t id) const;
    std::optional<CRDT::Node> get_crdt_node(uint64_t id) const;
    const CRDT::Edge* get_crdt_edge_ptr(uint64_t from, uint64_t to, const std::string& key) const;
    std::optional<CRDT::Edge> get_crdt_edge(uint64_t from, uint64_t to, const std::string& key) const;

    std::tuple<bool, std::optional<MvregNodeMsg>> insert_node_raw(CRDT::Node&& node);
    std::tuple<bool, std::optional<MvregNodeAttrVec>> update_node_raw(CRDT::Node&& node);
    std::tuple<bool, std::vector<Edge>, std::optional<MvregNodeMsg>, std::vector<MvregEdgeMsg>>
    delete_node_raw(uint64_t id, const CRDT::Node& node);
    std::optional<MvregEdgeMsg> delete_edge_raw(uint64_t from, uint64_t to, const std::string& key);
    std::tuple<bool, std::optional<MvregEdgeMsg>, std::optional<MvregEdgeAttrVec>>
    insert_or_assign_edge_raw(CRDT::Edge&& attrs, uint64_t from, uint64_t to);

    void join_delta_node(MvregNodeMsg&& mvreg);
    void join_delta_edge(MvregEdgeMsg&& mvreg);
    std::optional<std::string> join_delta_node_attr(MvregNodeAttrMsg&& mvreg);
    std::optional<std::string> join_delta_edge_attr(MvregEdgeAttrMsg&& mvreg);
    void join_full_graph(OrMap&& full_graph);

    std::map<uint64_t, MvregNodeMsg> export_mvreg_map() const;

    bool process_delta_edge(uint64_t from, uint64_t to, const std::string& type, mvreg<CRDT::Edge>&& delta);
    void process_delta_node_attr(uint64_t id, const std::string& att_name, mvreg<Attribute>&& attr);
    void process_delta_edge_attr(uint64_t from, uint64_t to, const std::string& type, const std::string& att_name, mvreg<Attribute>&& attr);

    SyncEngineHost& host_;
    Nodes nodes_;
    std::unordered_multimap<uint64_t, std::tuple<std::string, mvreg<Attribute>, uint64_t>> unprocessed_delta_node_att_;
    std::unordered_multimap<uint64_t, std::tuple<uint64_t, std::string, mvreg<CRDT::Edge>, uint64_t>> unprocessed_delta_edge_from_;
    std::unordered_multimap<uint64_t, std::tuple<uint64_t, std::string, mvreg<CRDT::Edge>, uint64_t>> unprocessed_delta_edge_to_;
    std::unordered_multimap<std::tuple<uint64_t, uint64_t, std::string>, std::tuple<std::string, mvreg<Attribute>, uint64_t>, hash_tuple> unprocessed_delta_edge_att_;
};

} // namespace DSR
