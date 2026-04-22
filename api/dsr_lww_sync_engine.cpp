#include "dsr/api/dsr_lww_sync_engine.h"
#include "dsr/core/types/lww_io.h"
#include "dsr/core/types/lww_merge.h"

#include <algorithm>

using namespace DSR;
using DSR::LWW::edge_key;
using DSR::LWW::is_newer;
using DSR::LWW::version_of;

LWWSyncEngine::LWWSyncEngine(SyncEngineHost& host, uint64_t tombstone_window_ms)
    : host_(host),
      tombstone_window_ms_(tombstone_window_ms)
{
}

LWWSyncEngine::LWWSyncEngine(SyncEngineHost& host, const LWWSyncEngine& other)
    : host_(host),
      tombstone_window_ms_(other.tombstone_window_ms_),
      logical_clock_ms_(other.logical_clock_ms_),
      nodes_(other.nodes_),
      edges_(other.edges_),
      node_tombstones_(other.node_tombstones_),
      edge_tombstones_(other.edge_tombstones_)
{
    for (const auto& [key, _] : edges_) {
        idx_insert(key);
    }
}

SyncBackendInfo LWWSyncEngine::backend_info() const
{
    return SyncBackendInfo{SyncMode::LWW, DSR_PROTOCOL_VERSION};
}

std::unique_ptr<SyncEngine> LWWSyncEngine::clone(SyncEngineHost& host) const
{
    return std::make_unique<LWWSyncEngine>(host, *this);
}

void LWWSyncEngine::idx_insert(const EdgeKey& key)
{
    LWW::edge_index_insert(from_idx_, to_idx_, type_idx_, key);
}

void LWWSyncEngine::idx_erase(const EdgeKey& key)
{
    LWW::edge_index_erase(from_idx_, to_idx_, type_idx_, key);
}

uint64_t LWWSyncEngine::current_time_ms() const
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

uint64_t LWWSyncEngine::next_timestamp()
{
    logical_clock_ms_ = std::max(logical_clock_ms_ + 1, current_time_ms());
    return logical_clock_ms_;
}

void LWWSyncEngine::prune_tombstones(uint64_t now)
{
    std::erase_if(node_tombstones_, [now](const auto& item) { return item.second.expires_at_ms <= now; });
    std::erase_if(edge_tombstones_, [now](const auto& item) { return item.second.expires_at_ms <= now; });
}

void LWWSyncEngine::store_node_tombstone(uint64_t id, Version version, uint64_t now)
{
    node_tombstones_[id] = LWW::make_tombstone(version, now, tombstone_window_ms_);
}

void LWWSyncEngine::store_edge_tombstone(uint64_t from, uint64_t to, const std::string& type, Version version, uint64_t now)
{
    edge_tombstones_[edge_key(from, to, type)] = LWW::make_tombstone(version, now, tombstone_window_ms_);
}

void LWWSyncEngine::erase_related_edges(uint64_t node_id, Version version, uint64_t now, std::vector<Edge>* removed_edges)
{
    std::unordered_set<EdgeKey, hash_tuple> to_erase;
    if (auto it = from_idx_.find(node_id); it != from_idx_.end()) {
        to_erase.insert(it->second.begin(), it->second.end());
    }
    if (auto it = to_idx_.find(node_id); it != to_idx_.end()) {
        to_erase.insert(it->second.begin(), it->second.end());
    }
    for (const auto& key : to_erase) {
        auto eit = edges_.find(key);
        if (eit == edges_.end()) continue;
        host_.update_maps_edge_delete(eit->second.from, eit->second.to, eit->second.type);
        store_edge_tombstone(eit->second.from, eit->second.to, eit->second.type, version, now);
        if (removed_edges != nullptr) {
            removed_edges->emplace_back(LWW::to_user_edge(eit->second));
        }
        idx_erase(key);
        edges_.erase(eit);
    }
}

std::optional<Node> LWWSyncEngine::get_node(uint64_t id) const
{
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return {};
    return LWW::to_user_node(it->second, edges_, from_idx_);
}

std::optional<Edge> LWWSyncEngine::get_edge(uint64_t from, uint64_t to, const std::string& type) const
{
    if (auto it = edges_.find(edge_key(from, to, type)); it != edges_.end()) {
        return LWW::to_user_edge(it->second);
    }
    return {};
}

bool LWWSyncEngine::for_each_edge_from(uint64_t from, const OutgoingEdgeVisitor& visitor) const
{
    if (!nodes_.contains(from)) {
        return false;
    }
    return LWW::for_each_edge_from(edges_, from_idx_, from, visitor);
}

bool LWWSyncEngine::for_each_edge_to(uint64_t to, const IncomingEdgeVisitor& visitor) const
{
    return LWW::for_each_edge_to(edges_, to_idx_, to, visitor);
}

void LWWSyncEngine::for_each_edge_of_type(const std::string& type, const TypedEdgeVisitor& visitor) const
{
    LWW::for_each_edge_of_type(edges_, type_idx_, type, visitor);
}

size_t LWWSyncEngine::size() const
{
    return nodes_.size();
}

std::map<uint64_t, Node> LWWSyncEngine::snapshot() const
{
    std::map<uint64_t, Node> out;
    for (const auto& [id, _] : nodes_) {
        out.emplace(id, *get_node(id));
    }
    return out;
}

NodeMutationEffect LWWSyncEngine::insert_node_local(Node&& node)
{
    auto now = next_timestamp();
    prune_tombstones(now);

    NodeMutationEffect effect;
    effect.id = node.id();
    effect.type = node.type();

    auto version = version_of(now, host_.local_agent_id());
    if (LWW::delta_is_stale(node_tombstones_, nodes_, node.id(), version)) {
        return effect;
    }

    NodeState state = LWW::to_node_state(node, version, host_.local_agent_id());
    for (const auto& [name, _] : node.attrs()) {
        effect.changed_attributes.emplace_back(name);
    }

    nodes_[node.id()] = std::move(state);
    node_tombstones_.erase(node.id());
    host_.update_maps_node_insert(node);
    effect.applied = true;
    effect.node_delta = NodeDeltaMessage{*export_node_delta(effect.id)};
    return effect;
}

NodeMutationEffect LWWSyncEngine::update_node_local(Node&& node)
{
    auto now = next_timestamp();
    prune_tombstones(now);

    NodeMutationEffect effect;
    effect.id = node.id();
    effect.type = node.type();

    auto it = nodes_.find(node.id());
    if (it == nodes_.end()) {
        return effect;
    }

    auto old_node = get_node(node.id());
    auto version = version_of(now, host_.local_agent_id());
    it->second.type = node.type();
    it->second.name = node.name();
    it->second.version = version;
    it->second.agent_id = host_.local_agent_id();

    std::map<std::string, AttrState> next_attrs = LWW::to_attr_state_map(node.attrs(), version);
    for (const auto& [name, _] : node.attrs()) {
        effect.changed_attributes.emplace_back(name);
    }
    for (const auto& [name, _] : it->second.attrs) {
        if (!next_attrs.contains(name)) {
            effect.changed_attributes.emplace_back(name);
        }
    }
    it->second.attrs = std::move(next_attrs);

    host_.update_maps_node_delete(node.id(), old_node);
    host_.update_maps_node_insert(node);
    effect.applied = true;
    effect.node_delta = NodeDeltaMessage{*export_node_delta(effect.id)};
    return effect;
}

NodeMutationEffect LWWSyncEngine::delete_node_local(uint64_t id)
{
    auto now = next_timestamp();
    prune_tombstones(now);

    NodeMutationEffect effect;
    effect.id = id;
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return effect;
    }

    auto version = version_of(now, host_.local_agent_id());
    auto deleted_node = get_node(id);
    effect.deleted_node = deleted_node;
    effect.deleted_edges.clear();
    erase_related_edges(id, version, now, &effect.deleted_edges);
    host_.update_maps_node_delete(id, deleted_node);
    store_node_tombstone(id, version, now);
    nodes_.erase(it);
    effect.applied = true;
    if (auto delta = export_node_delta(id); delta.has_value()) {
        effect.node_delta = NodeDeltaMessage{*delta};
    }
    effect.edge_deltas.reserve(effect.deleted_edges.size());
    for (const auto& edge : effect.deleted_edges) {
        if (auto delta = export_edge_delta(edge.from(), edge.to(), edge.type()); delta.has_value()) {
            effect.edge_deltas.emplace_back(EdgeDeltaMessage{*delta});
        }
    }
    return effect;
}

EdgeMutationEffect LWWSyncEngine::insert_or_assign_edge_local(Edge&& edge)
{
    auto now = next_timestamp();
    prune_tombstones(now);

    EdgeMutationEffect effect;
    effect.from = edge.from();
    effect.to = edge.to();
    effect.type = edge.type();

    auto version = version_of(now, host_.local_agent_id());
    if (!nodes_.contains(edge.from()) || !nodes_.contains(edge.to()) ||
        LWW::delta_is_stale(edge_tombstones_, edges_, edge_key(edge.from(), edge.to(), edge.type()), version)) {
        return effect;
    }

    auto key = edge_key(edge.from(), edge.to(), edge.type());
    edges_[key] = LWW::to_edge_state(edge, version, host_.local_agent_id());
    idx_insert(key);  // idempotent for updates
    for (const auto& [name, _] : edge.attrs()) {
        effect.changed_attributes.emplace_back(name);
    }
    edge_tombstones_.erase(key);
    host_.update_maps_edge_insert(edge.from(), edge.to(), edge.type());
    effect.applied = true;
    effect.edge_delta = EdgeDeltaMessage{*export_edge_delta(effect.from, effect.to, effect.type)};
    return effect;
}

EdgeMutationEffect LWWSyncEngine::delete_edge_local(uint64_t from, uint64_t to, const std::string& type)
{
    auto now = next_timestamp();
    prune_tombstones(now);

    EdgeMutationEffect effect;
    effect.from = from;
    effect.to = to;
    effect.type = type;

    auto key = edge_key(from, to, type);
    auto it = edges_.find(key);
    if (it == edges_.end()) {
        return effect;
    }

    auto version = version_of(now, host_.local_agent_id());
    effect.deleted_edge = LWW::to_user_edge(it->second);
    host_.update_maps_edge_delete(from, to, type);
    store_edge_tombstone(from, to, type, version, now);
    idx_erase(key);
    edges_.erase(it);
    effect.applied = true;
    if (auto delta = export_edge_delta(from, to, type); delta.has_value()) {
        effect.edge_delta = EdgeDeltaMessage{*delta};
    }
    return effect;
}

void LWWSyncEngine::apply_remote_node_delta(NodeDeltaMessage&& delta)
{
    auto* payload = std::get_if<LWWNodeMsg>(&delta);
    if (payload == nullptr) {
        return;
    }

    auto now = std::max(current_time_ms(), payload->timestamp);
    logical_clock_ms_ = std::max(logical_clock_ms_, now);
    prune_tombstones(now);

    auto version = version_of(payload->timestamp, payload->agent_id);
    if (LWW::delta_is_stale(node_tombstones_, nodes_, payload->id, version)) {
        return;
    }

    if (payload->deleted) {
        std::optional<Node> deleted_node;
        std::vector<Edge> deleted_edges;
        if (auto it = nodes_.find(payload->id); it != nodes_.end()) {
            deleted_node = get_node(payload->id);
            erase_related_edges(payload->id, version, now, &deleted_edges);
            host_.update_maps_node_delete(payload->id, deleted_node);
            nodes_.erase(it);
        }
        store_node_tombstone(payload->id, version, now);
        host_.on_remote_node_deleted(payload->id, deleted_node, deleted_edges, payload->agent_id);
        return;
    }

    NodeState state = LWW::to_node_state(*payload);

    auto maybe_old = get_node(payload->id);
    if (maybe_old.has_value()) {
        host_.update_maps_node_delete(payload->id, maybe_old);
    }
    nodes_[payload->id] = std::move(state);
    node_tombstones_.erase(payload->id);
    host_.update_maps_node_insert(*get_node(payload->id));
    host_.on_remote_node_updated(payload->id, payload->type, payload->agent_id);
}

void LWWSyncEngine::apply_remote_edge_delta(EdgeDeltaMessage&& delta)
{
    auto* payload = std::get_if<LWWEdgeMsg>(&delta);
    if (payload == nullptr) {
        return;
    }

    auto now = std::max(current_time_ms(), payload->timestamp);
    logical_clock_ms_ = std::max(logical_clock_ms_, now);
    prune_tombstones(now);

    auto version = version_of(payload->timestamp, payload->agent_id);
    if (LWW::delta_is_stale(edge_tombstones_, edges_, edge_key(payload->from, payload->to, payload->type), version)) {
        return;
    }
    if (!nodes_.contains(payload->from) || !nodes_.contains(payload->to)) {
        return;
    }

    auto key = edge_key(payload->from, payload->to, payload->type);
    if (payload->deleted) {
        std::optional<Edge> deleted_edge;
        if (auto it = edges_.find(key); it != edges_.end()) {
            deleted_edge = LWW::to_user_edge(it->second);
            host_.update_maps_edge_delete(payload->from, payload->to, payload->type);
            idx_erase(key);
            edges_.erase(it);
        }
        store_edge_tombstone(payload->from, payload->to, payload->type, version, now);
        host_.on_remote_edge_deleted(payload->from, payload->to, payload->type, deleted_edge, payload->agent_id);
        return;
    }

    EdgeState state = LWW::to_edge_state(*payload);
    edges_[key] = std::move(state);
    idx_insert(key);
    edge_tombstones_.erase(key);
    host_.update_maps_edge_insert(payload->from, payload->to, payload->type);
    host_.on_remote_edge_updated(payload->from, payload->to, payload->type, payload->agent_id);
}

void LWWSyncEngine::apply_remote_node_attr_batch(NodeAttrDeltaBatchMessage&& batch)
{
    auto* payload = std::get_if<LWWNodeAttrVec>(&batch);
    if (payload == nullptr) {
        return;
    }

    struct NodeAttrBatchChange
    {
        std::string type;
        std::vector<std::string> attrs;
        uint32_t agent_id{0};
    };
    std::unordered_map<uint64_t, NodeAttrBatchChange> changes;
    for (const auto& item : payload->vec) {
        auto it = nodes_.find(item.node_id);
        if (it == nodes_.end()) {
            continue;
        }
        auto version = version_of(item.timestamp, item.agent_id);
        auto attr_it = it->second.attrs.find(item.attr_name);
        if (attr_it != it->second.attrs.end() && !is_newer(version, attr_it->second.version)) {
            continue;
        }
        if (item.deleted) {
            it->second.attrs.erase(item.attr_name);
        } else {
            it->second.attrs[item.attr_name] = AttrState{item.value, version};
        }
        auto& change = changes[item.node_id];
        if (change.type.empty()) {
            change.type = it->second.type;
        }
        change.agent_id = item.agent_id;
        change.attrs.push_back(item.attr_name);
    }

    for (const auto& [id, change] : changes) {
        host_.on_remote_node_attrs_updated(id, change.type, change.attrs, change.agent_id);
    }
}

void LWWSyncEngine::apply_remote_edge_attr_batch(EdgeAttrDeltaBatchMessage&& batch)
{
    auto* payload = std::get_if<LWWEdgeAttrVec>(&batch);
    if (payload == nullptr) {
        return;
    }

    struct EdgeAttrBatchChange
    {
        std::vector<std::string> attrs;
        uint32_t agent_id{0};
    };
    std::map<EdgeKey, EdgeAttrBatchChange> changes;
    for (const auto& item : payload->vec) {
        auto it = edges_.find(edge_key(item.from, item.to, item.type));
        if (it == edges_.end()) {
            continue;
        }
        auto version = version_of(item.timestamp, item.agent_id);
        auto attr_it = it->second.attrs.find(item.attr_name);
        if (attr_it != it->second.attrs.end() && !is_newer(version, attr_it->second.version)) {
            continue;
        }
        if (item.deleted) {
            it->second.attrs.erase(item.attr_name);
        } else {
            it->second.attrs[item.attr_name] = AttrState{item.value, version};
        }
        auto& change = changes[edge_key(item.from, item.to, item.type)];
        change.agent_id = item.agent_id;
        change.attrs.push_back(item.attr_name);
    }

    for (const auto& [key, change] : changes) {
        host_.on_remote_edge_attrs_updated(std::get<0>(key), std::get<1>(key), std::get<2>(key), change.attrs, change.agent_id);
    }
}

void LWWSyncEngine::import_full_graph(FullGraphMessage&& full_graph)
{
    auto* payload = std::get_if<LWWGraphSnapshot>(&full_graph);
    if (payload == nullptr) {
        return;
    }

    tombstone_window_ms_ = payload->tombstone_window_ms == 0 ? tombstone_window_ms_ : payload->tombstone_window_ms;
    for (const auto& node : payload->nodes) {
        apply_remote_node_delta(NodeDeltaMessage{node});
    }
    for (const auto& edge : payload->edges) {
        apply_remote_edge_delta(EdgeDeltaMessage{edge});
    }
}

FullGraphMessage LWWSyncEngine::export_full_graph() const
{
    LWWGraphSnapshot snapshot;
    snapshot.id = static_cast<int32_t>(host_.local_agent_id());
    snapshot.protocol_version = DSR_PROTOCOL_VERSION;
    snapshot.sync_mode = sync_mode_wire_value(SyncMode::LWW);
    snapshot.tombstone_window_ms = tombstone_window_ms_;

    for (const auto& [id, node] : nodes_) {
        snapshot.nodes.emplace_back(LWW::to_node_msg(node));
    }

    for (const auto& [key, edge] : edges_) {
        snapshot.edges.emplace_back(LWW::to_edge_msg(edge));
    }

    return snapshot;
}

std::optional<LWWSyncEngine::Tombstone> LWWSyncEngine::node_tombstone(uint64_t id) const
{
    if (auto it = node_tombstones_.find(id); it != node_tombstones_.end()) {
        return it->second;
    }
    return {};
}

std::optional<LWWSyncEngine::Tombstone> LWWSyncEngine::edge_tombstone(uint64_t from, uint64_t to, const std::string& type) const
{
    if (auto it = edge_tombstones_.find(edge_key(from, to, type)); it != edge_tombstones_.end()) {
        return it->second;
    }
    return {};
}

std::optional<LWWNodeMsg> LWWSyncEngine::export_node_delta(uint64_t id) const
{
    if (auto it = nodes_.find(id); it != nodes_.end()) {
        return LWW::to_node_msg(it->second);
    }
    if (auto it = node_tombstones_.find(id); it != node_tombstones_.end()) {
        return LWW::to_node_tombstone_msg(id, it->second);
    }
    return {};
}

std::optional<LWWEdgeMsg> LWWSyncEngine::export_edge_delta(uint64_t from, uint64_t to, const std::string& type) const
{
    auto key = edge_key(from, to, type);
    if (auto it = edges_.find(key); it != edges_.end()) {
        return LWW::to_edge_msg(it->second);
    }
    if (auto it = edge_tombstones_.find(key); it != edge_tombstones_.end()) {
        return LWW::to_edge_tombstone_msg(from, to, type, it->second);
    }
    return {};
}
