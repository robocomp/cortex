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
};

struct EdgeMutationEffect
{
    bool applied{false};
    uint64_t from{};
    uint64_t to{};
    std::string type;
    std::vector<std::string> changed_attributes;
    std::optional<Edge> deleted_edge;
};

class SyncEngineHost
{
public:
    virtual ~SyncEngineHost() = default;

    virtual uint32_t local_agent_id() const = 0;
    virtual SyncMode local_sync_mode() const = 0;
    virtual bool is_copy_graph() const = 0;

    virtual void update_maps_node_insert(const Node& node) = 0;
    virtual void update_maps_node_delete(uint64_t id, const std::optional<Node>& node) = 0;
    virtual void update_maps_edge_insert(uint64_t from, uint64_t to, const std::string& type) = 0;
    virtual void update_maps_edge_delete(uint64_t from, uint64_t to, const std::string& type) = 0;
};

class SyncEngine
{
public:
    using OutgoingEdgeVisitor = std::function<void(uint64_t to, const std::string& type, const Edge& edge)>;
    using IncomingEdgeVisitor = std::function<void(uint64_t from, const std::string& type, const Edge& edge)>;
    using TypedEdgeVisitor = std::function<void(uint64_t from, uint64_t to, const Edge& edge)>;

    virtual ~SyncEngine() = default;

    virtual SyncBackendInfo backend_info() const = 0;

    virtual std::optional<Node> get_node(uint64_t id) const = 0;
    virtual std::optional<Edge> get_edge(uint64_t from, uint64_t to, const std::string& type) const = 0;
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
