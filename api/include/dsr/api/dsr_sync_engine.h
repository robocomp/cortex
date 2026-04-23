#pragma once

#include "dsr/api/dsr_graph_settings.h"
#include "dsr/core/types/internal_types.h"
#include "dsr/core/types/user_types.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace DSR {

// Backend-agnostic message envelopes. Stage 4 extends these variants with LWW
// message types while keeping the engine interface typed and decoupled from DDS
// reader/writer plumbing.
using NodeDeltaMessage = std::variant<MvregNodeMsg, LWWNodeMsg>;
using EdgeDeltaMessage = std::variant<MvregEdgeMsg, LWWEdgeMsg>;
using NodeAttrDeltaBatchMessage = std::variant<MvregNodeAttrVec, LWWNodeAttrVec>;
using EdgeAttrDeltaBatchMessage = std::variant<MvregEdgeAttrVec, LWWEdgeAttrVec>;
using FullGraphMessage = std::variant<OrMap, LWWGraphSnapshot>;

struct SyncBackendInfo
{
    SyncMode mode{SyncMode::CRDT};
    uint32_t protocol_version{DSR_PROTOCOL_VERSION};
};

struct NodeMutationEffect
{
    bool applied{false};
    uint64_t id{};
    std::string type;
    std::vector<std::string> changed_attributes;
    std::vector<std::pair<uint64_t, std::string>> related_edge_keys;
    std::vector<Edge> deleted_edges;
    std::optional<Node> deleted_node;
    std::optional<NodeDeltaMessage> node_delta;
    std::optional<NodeAttrDeltaBatchMessage> node_attr_batch;
    std::vector<EdgeDeltaMessage> edge_deltas;
};

struct EdgeMutationEffect
{
    bool applied{false};
    uint64_t from{};
    uint64_t to{};
    std::string type;
    std::vector<std::string> changed_attributes;
    std::optional<Edge> deleted_edge;
    std::optional<EdgeDeltaMessage> edge_delta;
    std::optional<EdgeAttrDeltaBatchMessage> edge_attr_batch;
};

class SyncEngineHost
{
public:
    using EdgeKeyList = std::vector<std::pair<uint64_t, std::string>>;

    virtual ~SyncEngineHost() = default;

    virtual uint32_t local_agent_id() const = 0;
    virtual SyncMode local_sync_mode() const = 0;
    virtual bool is_copy_graph() const = 0;

    virtual void update_maps_node_insert(uint64_t id, std::string_view name, std::string_view type, const EdgeKeyList& outgoing_edges) = 0;
    virtual void update_maps_node_delete(uint64_t id, std::optional<std::string_view> type, const EdgeKeyList& outgoing_edges) = 0;
    virtual void update_maps_edge_insert(uint64_t from, uint64_t to, const std::string& type) = 0;
    virtual void update_maps_edge_delete(uint64_t from, uint64_t to, const std::string& type) = 0;

    // Graph configuration queries — default impls safe for unit-test hosts.
    virtual GraphSettings::LOGLEVEL get_log_level() const { return GraphSettings::LOGLEVEL::INFOL; }
    virtual bool is_attribute_ignored(const std::string& /*name*/) const { return false; }
    virtual bool is_node_deleted(uint64_t /*id*/) const { return false; }

    // Secondary-index traversal — default no-ops; DSRGraph overrides with cache-map reads.
    virtual void for_each_incoming_edge(uint64_t /*to*/, std::function<void(uint64_t from, const std::string& type)> /*visitor*/) const {}
    virtual void for_each_edge_of_type_cache(const std::string& /*type*/, std::function<void(uint64_t from, uint64_t to)> /*visitor*/) const {}

    // Signal hooks for remote apply — called by the engine after state is updated.
    // Default no-ops; DSRGraph overrides to emit Qt signals.
    virtual void on_remote_node_updated(uint64_t /*id*/, const std::string& /*type*/, uint32_t /*agent_id*/) {}
    virtual void on_remote_node_deleted(uint64_t /*id*/, const std::optional<Node>& /*node*/, const std::vector<Edge>& /*edges*/, uint32_t /*agent_id*/) {}
    virtual void on_remote_edge_updated(uint64_t /*from*/, uint64_t /*to*/, const std::string& /*type*/, uint32_t /*agent_id*/) {}
    virtual void on_remote_edge_deleted(uint64_t /*from*/, uint64_t /*to*/, const std::string& /*type*/, const std::optional<Edge>& /*edge*/, uint32_t /*agent_id*/) {}
    virtual void on_remote_node_attrs_updated(uint64_t /*id*/, const std::string& /*type*/, const std::vector<std::string>& /*attrs*/, uint32_t /*agent_id*/) {}
    virtual void on_remote_edge_attrs_updated(uint64_t /*from*/, uint64_t /*to*/, const std::string& /*type*/, const std::vector<std::string>& /*attrs*/, uint32_t /*agent_id*/) {}
};

class SyncEngine
{
public:
    class NodeAttrsView
    {
    public:
        virtual ~NodeAttrsView() = default;
        virtual const Attribute* find(const std::string& name) const = 0;
    };

    class NodeView
    {
    public:
        virtual ~NodeView() = default;
        virtual uint64_t id() const = 0;
        virtual const std::string& type() const = 0;
        virtual const std::string& name() const = 0;
        virtual const NodeAttrsView& attrs() const = 0;
    };

    using OutgoingEdgeVisitor = std::function<void(uint64_t to, const std::string& type, const Edge& edge)>;
    using IncomingEdgeVisitor = std::function<void(uint64_t from, const std::string& type, const Edge& edge)>;
    using TypedEdgeVisitor = std::function<void(uint64_t from, uint64_t to, const Edge& edge)>;
    using NodeAttrsVisitor = std::function<void(const NodeAttrsView&)>;
    using NodeViewVisitor = std::function<void(const NodeView&)>;

    virtual ~SyncEngine() = default;

    virtual SyncBackendInfo backend_info() const = 0;
    virtual std::unique_ptr<SyncEngine> clone(SyncEngineHost& host) const = 0;

    virtual std::optional<Node> get_node(uint64_t id) const = 0;
    virtual std::optional<Edge> get_edge(uint64_t from, uint64_t to, const std::string& type) const = 0;
    virtual bool with_node_attrs(uint64_t id, const NodeAttrsVisitor& visitor) const = 0;
    virtual bool with_node_view(uint64_t id, const NodeViewVisitor& visitor) const = 0;
    virtual bool for_each_edge_from(uint64_t from, const OutgoingEdgeVisitor& visitor) const = 0;
    virtual bool for_each_edge_to(uint64_t to, const IncomingEdgeVisitor& visitor) const = 0;
    virtual void for_each_edge_of_type(const std::string& type, const TypedEdgeVisitor& visitor) const = 0;
    virtual size_t size() const = 0;
    virtual std::map<uint64_t, Node> snapshot() const = 0;

    virtual NodeMutationEffect insert_node_local(Node&& node) = 0;
    virtual NodeMutationEffect update_node_local(Node&& node) = 0;
    virtual NodeMutationEffect delete_node_local(uint64_t id) = 0;

    virtual EdgeMutationEffect insert_or_assign_edge_local(Edge&& edge) = 0;
    virtual EdgeMutationEffect delete_edge_local(uint64_t from, uint64_t to, const std::string& type) = 0;

    virtual void apply_remote_node_delta(NodeDeltaMessage&& delta) = 0;
    virtual void apply_remote_edge_delta(EdgeDeltaMessage&& delta) = 0;
    virtual void apply_remote_node_attr_batch(NodeAttrDeltaBatchMessage&& batch) = 0;
    virtual void apply_remote_edge_attr_batch(EdgeAttrDeltaBatchMessage&& batch) = 0;
    virtual void import_full_graph(FullGraphMessage&& full_graph) = 0;
    virtual FullGraphMessage export_full_graph() const = 0;
};

using SyncEnginePtr = std::unique_ptr<SyncEngine>;

} // namespace DSR
