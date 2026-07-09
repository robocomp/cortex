#include "dsr/api/dsr_crdt_sync_engine.h"

#include "dsr/api/dsr_logging.h"
#include "dsr/core/profiling.h"
#include "dsr/core/types/crdt_io.h"

#include <algorithm>
#include <iostream>
#include <utility>

using namespace DSR;

namespace {
template <typename Fano>
SyncEngineHost::EdgeKeyList collect_outgoing_edge_keys(const Fano& fano)
{
    SyncEngineHost::EdgeKeyList outgoing_edges;
    outgoing_edges.reserve(fano.size());
    for (const auto& [key, _] : fano) {
        outgoing_edges.emplace_back(key.first, key.second);
    }
    return outgoing_edges;
}

bool protocol_version_matches(
    DSR::GraphSettings::LOGLEVEL log_level,
    const char* channel,
    uint32_t remote_version)
{
    if (remote_version == DSR::DSR_PROTOCOL_VERSION) {
        return true;
    }

    DSR_LOG_ERROR(
        "[PROTOCOL] incompatible", channel,
        "remote:", remote_version,
        "local:", DSR::DSR_PROTOCOL_VERSION);
    return false;
}

class CRDTNodeAttrsView final : public SyncEngine::NodeAttrsView
{
public:
    explicit CRDTNodeAttrsView(const std::map<std::string, mvreg<Attribute>>& attrs)
        : attrs_(attrs) {}

    const Attribute* find(const std::string& name) const override
    {
        if (auto it = attrs_.find(name); it != attrs_.end() && !it->second.empty()) {
            return &it->second.read_reg();
        }
        return nullptr;
    }

private:
    const std::map<std::string, mvreg<Attribute>>& attrs_;
};

class CRDTNodeView final : public SyncEngine::NodeView
{
public:
    explicit CRDTNodeView(const CRDT::Node& node)
        : node_(node), attrs_(node.attrs) {}

    uint64_t id() const override { return node_.id; }
    const std::string& type() const override { return node_.type; }
    const std::string& name() const override { return node_.name; }
    const SyncEngine::NodeAttrsView& attrs() const override { return attrs_; }

private:
    const CRDT::Node& node_;
    CRDTNodeAttrsView attrs_;
};
}

CRDTSyncEngine::CRDTSyncEngine(SyncEngineHost& host)
    : host_(host)
{
}

CRDTSyncEngine::CRDTSyncEngine(SyncEngineHost& host, const CRDTSyncEngine& other)
    : host_(host),
      nodes_(other.nodes_),
      unprocessed_delta_node_att_(other.unprocessed_delta_node_att_),
      unprocessed_delta_edge_from_(other.unprocessed_delta_edge_from_),
      unprocessed_delta_edge_to_(other.unprocessed_delta_edge_to_),
      unprocessed_delta_edge_att_(other.unprocessed_delta_edge_att_),
      first_full_graph_(other.first_full_graph_)
{
}

SyncBackendInfo CRDTSyncEngine::backend_info() const
{
    return {};
}

std::unique_ptr<SyncEngine> CRDTSyncEngine::clone(SyncEngineHost& host) const
{
    CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::clone");
    return std::make_unique<CRDTSyncEngine>(host, *this);
}


std::optional<Node> CRDTSyncEngine::get_node(uint64_t id) const
{
    CORTEX_PROFILE_HOT_N("CRDTSyncEngine::get_node");
    if (const auto* node = get_node_ptr(id); node != nullptr) {
        return to_user_node(*node);
    }
    return {};
}

std::optional<Edge> CRDTSyncEngine::get_edge(uint64_t from, uint64_t to, const std::string& type) const
{
    CORTEX_PROFILE_HOT_N("CRDTSyncEngine::get_edge");
    if (const auto* edge = get_crdt_edge_ptr(from, to, type); edge != nullptr) {
        return to_user_edge(*edge);
    }
    return {};
}

bool CRDTSyncEngine::with_node_attrs(uint64_t id, const NodeAttrsVisitor& visitor) const
{
    CORTEX_PROFILE_HOT_N("CRDTSyncEngine::with_node_attrs");
    if (const auto* node = get_node_ptr(id); node != nullptr) {
        CRDTNodeAttrsView attrs(node->attrs);
        visitor(attrs);
        return true;
    }
    return false;
}

bool CRDTSyncEngine::with_node_view(uint64_t id, const NodeViewVisitor& visitor) const
{
    CORTEX_PROFILE_HOT_N("CRDTSyncEngine::with_node_view");
    if (const auto* node = get_node_ptr(id); node != nullptr) {
        CRDTNodeView view(*node);
        visitor(view);
        return true;
    }
    return false;
}

bool CRDTSyncEngine::for_each_edge_from(uint64_t from, const OutgoingEdgeVisitor& visitor) const
{
    CORTEX_PROFILE_HOT_N("CRDTSyncEngine::for_each_edge_from");
    if (const auto* node = get_node_ptr(from); node != nullptr) {
        for (const auto& [key, edge_reg] : node->fano) {
            if (!edge_reg.empty()) {
                Edge edge = to_user_edge(edge_reg.read_reg());
                visitor(key.first, key.second, edge);
            }
        }
        return true;
    }
    return false;
}

bool CRDTSyncEngine::for_each_edge_to(uint64_t to, const IncomingEdgeVisitor& visitor) const
{
    CORTEX_PROFILE_HOT_N("CRDTSyncEngine::for_each_edge_to");
    bool found = false;
    host_.for_each_incoming_edge(to, [&](uint64_t from, const std::string& type) {
        found = true;
        if (const auto* edge = get_crdt_edge_ptr(from, to, type); edge != nullptr) {
            Edge out = to_user_edge(*edge);
            visitor(from, type, out);
        }
    });
    return found;
}

void CRDTSyncEngine::for_each_edge_of_type(const std::string& type, const TypedEdgeVisitor& visitor) const
{
    CORTEX_PROFILE_HOT_N("CRDTSyncEngine::for_each_edge_of_type");
    host_.for_each_edge_of_type_cache(type, [&](uint64_t from, uint64_t to) {
        if (const auto* edge = get_crdt_edge_ptr(from, to, type); edge != nullptr) {
            Edge out = to_user_edge(*edge);
            visitor(from, to, out);
        }
    });
}

size_t CRDTSyncEngine::size() const
{
    return nodes_.size();
}

std::map<uint64_t, Node> CRDTSyncEngine::snapshot() const
{
    CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::snapshot");
    const auto log_level = host_.get_log_level();
    std::map<uint64_t, Node> out;
    for (const auto& [id, reg] : nodes_) {
        if (reg.empty()) {
            DSR_LOG_WARNING_L(log_level, "[CRDT] snapshot skipping empty node register", id);
            continue;
        }
        out.emplace(id, to_user_node(reg.read_reg()));
    }
    return out;
}

NodeMutationEffect CRDTSyncEngine::insert_node_local(Node&& node)
{
    CORTEX_PROFILE_ZONE_CS("CRDTSyncEngine::insert_node_local");
    NodeMutationEffect effect;
    auto [applied, delta] = insert_node_raw(user_node_to_crdt(std::move(node)));
    effect.applied = applied;
    if (applied && delta.has_value()) {
        effect.id = delta->id;
        effect.node_delta = NodeDeltaMessage{*delta};
        if (const auto* inserted = get_node_ptr(delta->id); inserted != nullptr) {
            effect.type = inserted->type;
        }
    }
    return effect;
}

NodeMutationEffect CRDTSyncEngine::update_node_local(Node&& node)
{
    CORTEX_PROFILE_ZONE_CS("CRDTSyncEngine::update_node_local");
    NodeMutationEffect effect;
    auto [applied, deltas] = update_node_raw(user_node_to_crdt(std::move(node)));
    effect.applied = applied;
    if (applied && deltas.has_value()) {
        effect.node_attr_batch = NodeAttrDeltaBatchMessage{*deltas};
        effect.changed_attributes.reserve(deltas->vec.size());
        for (const auto& item : deltas->vec) {
            effect.changed_attributes.emplace_back(item.attr_name);
        }
    }
    return effect;
}

NodeMutationEffect CRDTSyncEngine::delete_node_local(uint64_t id)
{
    CORTEX_PROFILE_ZONE_CS("CRDTSyncEngine::delete_node_local");
    NodeMutationEffect effect;
    if (auto node = get_crdt_node(id); node.has_value()) {
        auto [applied, deleted_edges, delta_node, delta_edges] = delete_node_raw(id, *node);
        effect.applied = applied;
        effect.id = id;
        effect.deleted_edges = std::move(deleted_edges);
        if (node.has_value()) {
            effect.deleted_node = to_user_node(*node);
        }
        if (delta_node.has_value()) {
            effect.node_delta = NodeDeltaMessage{*delta_node};
        }
        effect.edge_deltas.reserve(delta_edges.size());
        for (const auto& delta : delta_edges) {
            effect.edge_deltas.emplace_back(EdgeDeltaMessage{delta});
        }
    }
    return effect;
}

EdgeMutationEffect CRDTSyncEngine::insert_or_assign_edge_local(Edge&& edge)
{
    CORTEX_PROFILE_ZONE_CS("CRDTSyncEngine::insert_or_assign_edge_local");
    EdgeMutationEffect effect;
    auto from = edge.from();
    auto to = edge.to();
    auto type = edge.type();
    std::vector<std::string> edge_attr_names;
    edge_attr_names.reserve(edge.attrs().size());
    for (const auto& [name, _] : edge.attrs()) {
        edge_attr_names.emplace_back(name);
    }
    auto [applied, _edge_delta, attr_deltas] = insert_or_assign_edge_raw(user_edge_to_crdt(std::move(edge)), from, to);
    effect.applied = applied;
    effect.from = from;
    effect.to = to;
    effect.type = std::move(type);
    if (_edge_delta.has_value()) {
        effect.edge_delta = EdgeDeltaMessage{*_edge_delta};
        effect.changed_attributes = std::move(edge_attr_names);
    }
    if (attr_deltas.has_value()) {
        effect.edge_attr_batch = EdgeAttrDeltaBatchMessage{*attr_deltas};
        effect.changed_attributes.reserve(attr_deltas->vec.size());
        for (const auto& item : attr_deltas->vec) {
            effect.changed_attributes.emplace_back(item.attr_name);
        }
    }
    return effect;
}

EdgeMutationEffect CRDTSyncEngine::delete_edge_local(uint64_t from, uint64_t to, const std::string& type)
{
    CORTEX_PROFILE_ZONE_CS("CRDTSyncEngine::delete_edge_local");
    EdgeMutationEffect effect;
    effect.deleted_edge = get_edge(from, to, type);
    if (auto delta = delete_edge_raw(from, to, type); delta.has_value()) {
        effect.edge_delta = EdgeDeltaMessage{*delta};
        effect.applied = true;
    }
    effect.from = from;
    effect.to = to;
    effect.type = type;
    return effect;
}

void CRDTSyncEngine::apply_remote_node_delta(NodeDeltaMessage&& delta)
{
    CORTEX_PROFILE_ZONE_N("CRDTSyncEngine::apply_remote_node_delta");
    if (auto* payload = std::get_if<MvregNodeMsg>(&delta); payload != nullptr) {
        join_delta_node(std::move(*payload));
    }
}

void CRDTSyncEngine::apply_remote_edge_delta(EdgeDeltaMessage&& delta)
{
    CORTEX_PROFILE_ZONE_N("CRDTSyncEngine::apply_remote_edge_delta");
    if (auto* payload = std::get_if<MvregEdgeMsg>(&delta); payload != nullptr) {
        join_delta_edge(std::move(*payload));
    }
}

void CRDTSyncEngine::apply_remote_node_attr_batch(NodeAttrDeltaBatchMessage&& batch)
{
    CORTEX_PROFILE_ZONE_CS("CRDTSyncEngine::apply_remote_node_attr_batch");
    auto* payload = std::get_if<MvregNodeAttrVec>(&batch);
    if (payload == nullptr || payload->vec.empty()) {
        return;
    }

    const auto id = payload->vec.front().id;
    const auto sample_agent_id = payload->vec.front().agent_id;
    std::vector<std::string> changed_attributes;
    for (auto&& item : payload->vec) {
        if (host_.is_attribute_ignored(item.attr_name)) {
            continue;
        }
        if (auto changed = join_delta_node_attr(std::move(item)); changed.has_value()) {
            changed_attributes.emplace_back(std::move(*changed));
        }
    }

    if (!changed_attributes.empty()) {
        std::string type;
        if (const auto* node = get_node_ptr(id); node != nullptr) {
            type = node->type;
        }
        host_.on_remote_node_attrs_updated(id, type, changed_attributes, sample_agent_id);
    }
}

void CRDTSyncEngine::apply_remote_edge_attr_batch(EdgeAttrDeltaBatchMessage&& batch)
{
    CORTEX_PROFILE_ZONE_CS("CRDTSyncEngine::apply_remote_edge_attr_batch");
    auto* payload = std::get_if<MvregEdgeAttrVec>(&batch);
    if (payload == nullptr || payload->vec.empty()) {
        return;
    }

    const auto from = payload->vec.front().from_node;
    const auto to = payload->vec.front().to_node;
    const auto type = payload->vec.front().type;
    const auto sample_agent_id = payload->vec.front().agent_id;
    std::vector<std::string> changed_attributes;
    for (auto&& item : payload->vec) {
        if (host_.is_attribute_ignored(item.attr_name)) {
            continue;
        }
        if (auto changed = join_delta_edge_attr(std::move(item)); changed.has_value()) {
            changed_attributes.emplace_back(std::move(*changed));
        }
    }

    if (!changed_attributes.empty()) {
        host_.on_remote_edge_attrs_updated(from, to, type, changed_attributes, sample_agent_id);
    }
}

void CRDTSyncEngine::import_full_graph(FullGraphMessage&& full_graph)
{
    if (auto* payload = std::get_if<OrMap>(&full_graph); payload != nullptr) {
        join_full_graph(std::move(*payload));
    }
}

FullGraphMessage CRDTSyncEngine::export_full_graph() const
{
    OrMap map;
    map.id = static_cast<int32_t>(host_.local_agent_id());
    map.protocol_version = DSR_PROTOCOL_VERSION;
    map.sync_mode = sync_mode_wire_value(host_.local_sync_mode());
    map.m = export_mvreg_map();
    return map;
}

const CRDT::Node* CRDTSyncEngine::get_node_ptr(uint64_t id) const
{
    auto it = nodes_.find(id);
    if (it != nodes_.end() && !it->second.empty()) {
        return &it->second.read_reg();
    }
    return nullptr;
}

std::optional<CRDT::Node> CRDTSyncEngine::get_crdt_node(uint64_t id) const
{
    if (const auto* node = get_node_ptr(id); node != nullptr) {
        return std::make_optional(*node);
    }
    return {};
}

const CRDT::Edge* CRDTSyncEngine::get_crdt_edge_ptr(uint64_t from, uint64_t to, const std::string& key) const
{
    auto from_it = nodes_.find(from);
    if (from_it == nodes_.end() || from_it->second.empty() || !nodes_.contains(to)) {
        return nullptr;
    }

    auto& fano = from_it->second.read_reg().fano;
    auto edge = fano.find({to, key});
    if (edge != fano.end() && !edge->second.empty()) {
        return &edge->second.read_reg();
    }

    return nullptr;
}

std::optional<CRDT::Edge> CRDTSyncEngine::get_crdt_edge(uint64_t from, uint64_t to, const std::string& key) const
{
    if (const auto* edge = get_crdt_edge_ptr(from, to, key); edge != nullptr) {
        return *edge;
    }
    return {};
}

std::tuple<bool, std::optional<MvregNodeMsg>> CRDTSyncEngine::insert_node_raw(CRDT::Node&& node)
{
    CORTEX_PROFILE_ZONE_CS("CRDTSyncEngine::insert_node_raw");
    if (!host_.is_node_deleted(node.id))
    {
        if (auto it = nodes_.find(node.id); it != nodes_.end() and not it->second.empty() and it->second.read_reg() == node)
        {
            return {true, {}};
        }

        uint64_t id = node.id;
        host_.update_maps_node_insert(id, node.name, node.type, collect_outgoing_edge_keys(node.fano));
        auto delta = nodes_[id].write(std::move(node));
        nodes_[id].join(mvreg<CRDT::Node>(delta));
        return {true, crdt_node_to_msg(host_.local_agent_id(), id, std::move(delta))};
    }
    return {false, {}};
}

std::tuple<bool, std::optional<MvregNodeAttrVec>> CRDTSyncEngine::update_node_raw(CRDT::Node&& node)
{
    CORTEX_PROFILE_ZONE_CS("CRDTSyncEngine::update_node_raw");
    if (!host_.is_node_deleted(node.id))
    {
        auto nit = nodes_.find(node.id);
        if (nit != nodes_.end() && !nit->second.empty())
        {
            MvregNodeAttrVec atts_deltas;
            auto& iter = nit->second.read_reg().attrs;
            for (auto& [k, att] : node.attrs) {
                auto& attr_reg = iter.try_emplace(k, mvreg<Attribute>()).first->second;
                if (attr_reg.empty() || att.read_reg() != attr_reg.read_reg()) {
                    auto delta = attr_reg.write(std::move(att.read_reg()));
                    attr_reg.join(mvreg<Attribute>(delta));
                    atts_deltas.vec.emplace_back(
                        crdt_node_attr_to_msg(host_.local_agent_id(), node.id, node.id, k, std::move(delta)));
                }
            }
            auto it_a = iter.begin();
            while (it_a != iter.end()) {
                const std::string& k = it_a->first;
                if (host_.is_attribute_ignored(k)) {
                    it_a = iter.erase(it_a);
                } else if (!node.attrs.contains(k)) {
                    auto delta = it_a->second.reset();
                    atts_deltas.vec.emplace_back(
                        crdt_node_attr_to_msg(node.agent_id, node.id, node.id, k, std::move(delta)));
                    it_a = iter.erase(it_a);
                } else {
                    ++it_a;
                }
            }

            return {true, std::move(atts_deltas)};
        }
    }

    return {false, {}};
}

std::tuple<bool, std::vector<Edge>, std::optional<MvregNodeMsg>, std::vector<MvregEdgeMsg>>
CRDTSyncEngine::delete_node_raw(uint64_t id, const CRDT::Node& node)
{
    CORTEX_PROFILE_ZONE_CS("CRDTSyncEngine::delete_node_raw");

    std::vector<Edge> deleted_edges;
    std::vector<MvregEdgeMsg> delta_vec;

    for (const auto& v : node.fano) {
        deleted_edges.emplace_back(to_user_edge(v.second.read_reg()));
    }
    auto delta = nodes_[id].reset();
    nodes_.erase(id);
    MvregNodeMsg delta_remove = crdt_node_to_msg(host_.local_agent_id(), id, std::move(delta));
    {
        std::vector<std::pair<uint64_t, std::string>> incoming;
        host_.for_each_incoming_edge(id, [&](uint64_t from, const std::string& type) {
            incoming.emplace_back(from, type);
        });
        for (const auto& [from, type] : incoming)
        {
            if (!nodes_.contains(from)) continue;
            auto& visited_node = nodes_.at(from).read_reg();
            deleted_edges.emplace_back(to_user_edge(visited_node.fano.at({id, type}).read_reg()));
            auto delta_fano = visited_node.fano.at({id, type}).reset();
            delta_vec.emplace_back(crdt_edge_to_msg(host_.local_agent_id(), from, id, type, std::move(delta_fano)));
            visited_node.fano.erase({id, type});
            host_.update_maps_edge_delete(from, id, type);
        }
    }

    host_.update_maps_node_delete(id, node.type, collect_outgoing_edge_keys(node.fano));

    return {true, std::move(deleted_edges), std::move(delta_remove), std::move(delta_vec)};
}

std::optional<MvregEdgeMsg> CRDTSyncEngine::delete_edge_raw(uint64_t from, uint64_t to, const std::string& key)
{
    if (nodes_.contains(from)) {
        auto& node = nodes_.at(from).read_reg();
        if (node.fano.contains({to, key})) {
            auto delta = node.fano.at({to, key}).reset();
            node.fano.erase({to, key});
            host_.update_maps_edge_delete(from, to, key);
            return crdt_edge_to_msg(host_.local_agent_id(), from, to, key, std::move(delta));
        }
    }
    return {};
}

std::tuple<bool, std::optional<MvregEdgeMsg>, std::optional<MvregEdgeAttrVec>>
CRDTSyncEngine::insert_or_assign_edge_raw(CRDT::Edge&& attrs, uint64_t from, uint64_t to)
{
    CORTEX_PROFILE_ZONE_CS("CRDTSyncEngine::insert_or_assign_edge_raw");
    std::optional<MvregEdgeMsg> delta_edge;
    std::optional<MvregEdgeAttrVec> delta_attrs;

    if (nodes_.contains(from))
    {
        auto& node = nodes_.at(from).read_reg();
        auto fano_it = node.fano.find({to, attrs.type});
        if (fano_it != node.fano.end())
        {
            MvregEdgeAttrVec atts_deltas;
            auto& iter_edge = fano_it->second.read_reg().attrs;
            for (auto& [k, att] : attrs.attrs) {
                auto& attr_reg = iter_edge.try_emplace(k, mvreg<Attribute>()).first->second;
                if (attr_reg.empty() || att.read_reg() != attr_reg.read_reg()) {
                    auto delta = attr_reg.write(std::move(att.read_reg()));
                    attr_reg.join(mvreg<Attribute>(delta));
                    atts_deltas.vec.emplace_back(
                        crdt_edge_attr_to_msg(host_.local_agent_id(), from, from, to, attrs.type, k, std::move(delta)));
                }
            }
            auto it = iter_edge.begin();
            while (it != iter_edge.end()) {
                if (!attrs.attrs.contains(it->first)) {
                    std::string att = it->first;
                    auto delta = it->second.reset();
                    it = iter_edge.erase(it);
                    atts_deltas.vec.emplace_back(
                        crdt_edge_attr_to_msg(host_.local_agent_id(), from, from, to, attrs.type, std::move(att), std::move(delta)));
                } else {
                    ++it;
                }
            }
            return {true, {}, std::move(atts_deltas)};
        } else
        {
            std::string att_type = attrs.type;
            auto& edge_reg = node.fano[{to, attrs.type}];
            auto delta = edge_reg.write(std::move(attrs));
            edge_reg.join(mvreg<CRDT::Edge>(delta));
            host_.update_maps_edge_insert(from, to, att_type);
            return {true, crdt_edge_to_msg(host_.local_agent_id(), from, to, std::move(att_type), std::move(delta)), {}};
        }
    }
    return {false, {}, {}};
}

bool CRDTSyncEngine::process_delta_edge(uint64_t from, uint64_t to, const std::string& type, mvreg<CRDT::Edge>&& delta)
{
    CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::process_delta_edge");
    bool signal = false;
    auto& node = nodes_.at(from).read_reg();
    auto& fanout = node.fano;
    std::optional<CRDT::Edge> prev = {};
    if (auto it = fanout.find({to, type}); it != fanout.end() && !it->second.empty()) {
        prev = it->second.read_reg();
    }
    auto& edge_reg = fanout[{to, type}];
    auto d_empty = delta.empty();
    edge_reg.join(std::move(delta));
    if (edge_reg.empty() || d_empty) {
        fanout.erase({to, type});
        host_.update_maps_edge_delete(from, to, type);
        signal = false;
    } else {
        host_.update_maps_edge_insert(from, to, type);
        signal = !prev.has_value() || prev.value() != edge_reg.read_reg();
    }
    return signal;
}

void CRDTSyncEngine::process_delta_node_attr(uint64_t id, const std::string& att_name, mvreg<Attribute>&& attr)
{
    CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::process_delta_node_attr");
    auto& n = nodes_.at(id).read_reg();
    auto& attrs = n.attrs;
    auto& attr_reg = attrs[att_name];
    auto d_empty = attr.empty();
    attr_reg.join(std::move(attr));
    if (attr_reg.empty() || d_empty) {
        attrs.erase(att_name);
    }
}

void CRDTSyncEngine::process_delta_edge_attr(uint64_t from, uint64_t to, const std::string& type, const std::string& att_name, mvreg<Attribute>&& attr)
{
    CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::process_delta_edge_attr");
    auto& n = nodes_.at(from).read_reg().fano.at({to, type}).read_reg();
    auto& attrs = n.attrs;
    auto& attr_reg = attrs[att_name];
    auto d_empty = attr.empty();
    attr_reg.join(std::move(attr));
    if (attr_reg.empty() || d_empty) {
        attrs.erase(att_name);
    }
}

void CRDTSyncEngine::join_delta_node(MvregNodeMsg&& mvreg)
{
    CORTEX_PROFILE_ZONE_CS("CRDTSyncEngine::join_delta_node");
    const auto log_level = host_.get_log_level();

    std::optional<CRDT::Node> maybe_deleted_node = {};
    try {
        if (!protocol_version_matches(log_level, "DSR_NODE", mvreg.protocol_version)) {
            return;
        }
        bool signal = false, joined = false;
        auto id = mvreg.id;
        auto timestamp = mvreg.timestamp;
        auto crdt_delta = std::move(mvreg.dk);
        auto d_empty = crdt_delta.empty();
        std::unordered_set<std::pair<uint64_t, std::string>,hash_tuple> map_new_to_edges = {};
        std::unordered_set<std::tuple<uint64_t, uint64_t, std::string>,hash_tuple> map_new_from_edges = {};
        std::vector<std::pair<uint64_t, std::string>> incoming_edge_cache;
        std::string current_type;

        auto delete_unprocessed_deltas = [&](){
            CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::join_delta_node delete unprocessed deltas");
            unprocessed_delta_node_att_.erase(id);
            decltype(unprocessed_delta_edge_from_)::node_type node_handle = unprocessed_delta_edge_from_.extract(id);
            while (!node_handle.empty())
            {
                unprocessed_delta_edge_att_.erase(std::tuple{id, std::get<0>(node_handle.mapped()), std::get<1>(node_handle.mapped())});
                node_handle = unprocessed_delta_edge_from_.extract(id);
            }
            std::erase_if(unprocessed_delta_edge_to_,
                          [&](auto &it){ return std::get<0>(it.second) == id;});
            std::erase_if(unprocessed_delta_edge_att_,
                          [&](auto &it){ return std::get<0>(it.first) == id || std::get<1>(it.first) == id;});
        };

        auto consume_unprocessed_deltas = [&]() {
            CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::join_delta_node consume unprocessed deltas");
            decltype(unprocessed_delta_node_att_)::node_type node_handle_node_att = unprocessed_delta_node_att_.extract(id);
            while (!node_handle_node_att.empty())
            {
                auto &[att_name, delta, timestamp_node_att] = node_handle_node_att.mapped();
                if (timestamp < timestamp_node_att) {
                    process_delta_node_attr(id, att_name,std::move(delta));
                }
                node_handle_node_att = unprocessed_delta_node_att_.extract(id);
            }

            decltype(unprocessed_delta_edge_from_)::node_type node_handle_edge = unprocessed_delta_edge_from_.extract(id);
            while (!node_handle_edge.empty()) {
                auto &[to, type, delta, timestamp_edge] = node_handle_edge.mapped();
                auto att_key = std::tuple{id, to, type};
                DSR_LOG_DEBUG("[JOIN_NODE] unprocessed_delta_edge_from", id, to, type, (timestamp < timestamp_edge));
                if (timestamp < timestamp_edge) {
                    if (process_delta_edge(id, to, type, std::move(delta))) map_new_to_edges.emplace(to, type);
                }
                if (nodes_.contains(id) and nodes_.at(id).read_reg().fano.contains({to, type})) {
                    decltype(unprocessed_delta_edge_att_)::node_type node_handle_edge_att =  unprocessed_delta_edge_att_.extract(att_key);
                    while (!node_handle_edge_att.empty()) {
                        auto &[att_name, delta, timestamp_edge_att] = node_handle_edge_att.mapped();
                        DSR_LOG_DEBUG("[JOIN_NODE] edge_att", id, to, type, att_name, (timestamp < timestamp_edge));
                        if (timestamp < timestamp_edge_att) {
                            process_delta_edge_attr(id, to, type, att_name, std::move(delta));
                        }
                        node_handle_edge_att = unprocessed_delta_edge_att_.extract(att_key);
                    }
                }
                std::erase_if(unprocessed_delta_edge_to_,
                              [to = to, id = id, type = type](auto& it) { return it.first == to && std::get<0>(it.second) == id && std::get<1>(it.second) == type;});
                node_handle_edge = unprocessed_delta_edge_from_.extract(id);
            }

            node_handle_edge = unprocessed_delta_edge_to_.extract(id);
            while (!node_handle_edge.empty()) {
                auto &[from, type, delta, timestamp_edge] = node_handle_edge.mapped();
                auto att_key = std::tuple{from, id, type};
                DSR_LOG_DEBUG("[JOIN_NODE] unprocessed_delta_edge_to", from, id, type, (timestamp < timestamp_edge));
                if (timestamp < timestamp_edge) {
                    if (process_delta_edge(from, id, type, std::move(delta))) map_new_from_edges.emplace(from, id, type);
                }
                if (nodes_.contains(from) and nodes_.at(from).read_reg().fano.contains({id, type})) {
                    decltype(unprocessed_delta_edge_att_)::node_type node_handle_edge_att =  unprocessed_delta_edge_att_.extract(att_key);
                    while (!node_handle_edge_att.empty()) {
                        auto &[att_name, delta, timestamp_edge_att] = node_handle_edge_att.mapped();
                        DSR_LOG_DEBUG("[JOIN_NODE] edge_att", from, id, type, att_name, (timestamp < timestamp_edge));
                        if (timestamp < timestamp_edge_att) {
                            process_delta_edge_attr(from, id, type, att_name, std::move(delta));
                        }
                        node_handle_edge_att = unprocessed_delta_edge_att_.extract(att_key);
                    }
                }

                node_handle_edge = unprocessed_delta_edge_to_.extract(id);
            }
        };

        {
            CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::join_delta_node crdt merge");
            if (!host_.is_node_deleted(id)) {
                if (auto it = nodes_.find(id); it != nodes_.end() && !it->second.empty()) {
                    maybe_deleted_node = it->second.read_reg();
                }
                nodes_[id].join(std::move(crdt_delta));
                if (nodes_.at(id).empty() || d_empty) {
                    if (maybe_deleted_node.has_value()) {
                        host_.for_each_incoming_edge(id, [&](uint64_t from, const std::string& type) {
                            incoming_edge_cache.emplace_back(from, type);
                        });
                    }
                    host_.update_maps_node_delete(
                        id,
                        maybe_deleted_node.has_value() ? std::optional<std::string_view>{maybe_deleted_node->type} : std::nullopt,
                        maybe_deleted_node.has_value() ? collect_outgoing_edge_keys(maybe_deleted_node->fano) : SyncEngineHost::EdgeKeyList{});
                    delete_unprocessed_deltas();
                    nodes_.erase(id);
                } else {
                    const auto& reg = nodes_.at(id).read_reg();
                    current_type = reg.type;
                    host_.update_maps_node_insert(reg.id, reg.name, reg.type, collect_outgoing_edge_keys(reg.fano));
                    consume_unprocessed_deltas();
                }
                signal = !d_empty;
                joined = true;
            }
        }

        if (joined) {
            CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::join_delta_node signal emit");
            if (signal) {
                host_.on_remote_node_updated(id, current_type, mvreg.agent_id);
                if (const auto* current = get_node_ptr(id); current != nullptr) {
                    for (const auto& [k, v] : current->fano) {
                        host_.on_remote_edge_updated(id, k.first, k.second, mvreg.agent_id);
                    }
                }
                for (const auto& [to, type] : map_new_to_edges) {
                    host_.on_remote_edge_updated(id, to, type, mvreg.agent_id);
                }
            } else {
                std::optional<Node> deleted_node_user = maybe_deleted_node.has_value()
                    ? std::make_optional(to_user_node(*maybe_deleted_node)) : std::nullopt;
                std::vector<Edge> deleted_edges_user;
                if (maybe_deleted_node.has_value()) {
                    for (const auto& [key, mvreg_edge] : maybe_deleted_node->fano) {
                        deleted_edges_user.emplace_back(to_user_edge(mvreg_edge.read_reg()));
                    }
                }
                host_.on_remote_node_deleted(id, deleted_node_user, deleted_edges_user, mvreg.agent_id);
                for (const auto& [from, type] : incoming_edge_cache) {
                    host_.on_remote_edge_deleted(from, id, type, std::nullopt, mvreg.agent_id);
                }
            }
        }
    } catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
}

void CRDTSyncEngine::join_delta_edge(MvregEdgeMsg&& mvreg)
{
    CORTEX_PROFILE_ZONE_CS("CRDTSyncEngine::join_delta_edge");
    const auto log_level = host_.get_log_level();
    try {
        if (!protocol_version_matches(log_level, "DSR_EDGE", mvreg.protocol_version)) {
            return;
        }
        bool signal = false, joined = false;
        auto from = mvreg.from;
        auto to = mvreg.to;
        auto type = mvreg.type;
        auto id = mvreg.id;
        auto timestamp = mvreg.timestamp;
        auto crdt_delta = std::move(mvreg.dk);
        auto d_empty = crdt_delta.empty();
        std::optional<Edge> deleted_edge;

        auto delete_unprocessed_deltas = [&](){
            CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::join_delta_edge delete unprocessed deltas");
            unprocessed_delta_edge_att_.erase(std::tuple{from, to, type});
            std::erase_if(unprocessed_delta_edge_to_,
                          [&](auto &it){ return std::get<0>(it.second) == from || std::get<0>(it.second) == to;});
            std::erase_if(unprocessed_delta_edge_from_,
                          [&](auto &it){ return std::get<0>(it.second) == from || std::get<0>(it.second) == to;});
        };

        auto consume_unprocessed_deltas = [&](){
            CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::join_delta_edge consume unprocessed deltas");
            auto att_key = std::tuple{from, to, type};
            decltype(unprocessed_delta_edge_att_)::node_type node_handle_edge_att = unprocessed_delta_edge_att_.extract(att_key);
            while (!node_handle_edge_att.empty()) {
                auto &[att_name, delta, timestamp_edge_att] = node_handle_edge_att.mapped();
                DSR_LOG_DEBUG("[JOIN_EDGE] edge_att", from, to, type, att_name, (timestamp < timestamp_edge_att));
                if (timestamp < timestamp_edge_att) {
                    process_delta_edge_attr(from, to, type, att_name,std::move(delta));
                }
                node_handle_edge_att = unprocessed_delta_edge_att_.extract(att_key);
            }
            std::erase_if(unprocessed_delta_edge_to_,
                          [from = from, to = to, type = type](auto& it) { return it.first == to && std::get<0>(it.second) == from && std::get<1>(it.second) == type;});
            std::erase_if(unprocessed_delta_edge_from_,
                          [from = from, to = to, type = type](auto& it) { return it.first == from && std::get<0>(it.second) == to && std::get<1>(it.second) == type;});
        };

        {
            CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::join_delta_edge crdt merge");
            deleted_edge = get_edge(from, to, type);
            bool cfrom{nodes_.contains(from)}, cto{nodes_.contains(to)};
            bool dfrom{host_.is_node_deleted(from)}, dto{host_.is_node_deleted(to)};

            if (cfrom && cto) {
                signal = process_delta_edge(from, to, type, std::move(crdt_delta));
                consume_unprocessed_deltas();
                joined = true;
            } else if (!dfrom && !dto) {
                bool find = false;
                for (auto [begin, end] = unprocessed_delta_edge_from_.equal_range(from); begin != end; ++begin) {
                    if (std::get<0>(begin->second) == to && std::get<1>(begin->second) == type){
                        find = true;
                        break;
                    }
                }
                for (auto [begin, end] = unprocessed_delta_edge_to_.equal_range(to); begin != end; ++begin) {
                    if (std::get<0>(begin->second) == from && std::get<1>(begin->second) == type){
                        find = true;
                        break;
                    }
                }

                if (!find) {
                    if (!cfrom) {
                        DSR_LOG_DEBUG("[JOIN_EDGE] INSERT UNPROCESSED, no from", from, "unprocessed_delta_edge_from");
                        unprocessed_delta_edge_from_.emplace(from, std::tuple{to, type, crdt_delta, timestamp});
                    }
                    if (cfrom && !cto) {
                        DSR_LOG_DEBUG("[JOIN_EDGE] INSERT UNPROCESSED, no to", to, "unprocessed_delta_edge_to");
                        unprocessed_delta_edge_to_.emplace(to, std::tuple{from, type, std::move(crdt_delta), timestamp});
                    }
                }
            } else {
                if (d_empty) {
                    delete_unprocessed_deltas();
                    joined = true;
                }
            }
        }

        if (joined) {
            CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::join_delta_edge signal emit");
            if (signal) {
                host_.on_remote_edge_updated(from, to, type, mvreg.agent_id);
            } else {
                host_.on_remote_edge_deleted(from, to, type, deleted_edge, mvreg.agent_id);
            }
        }
    } catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
}

std::optional<std::string> CRDTSyncEngine::join_delta_node_attr(MvregNodeAttrMsg&& mvreg)
{
    CORTEX_PROFILE_ZONE_CS("CRDTSyncEngine::join_delta_node_attr");
    try {
        if (!protocol_version_matches(host_.get_log_level(), "DSR_NODE_ATTS", mvreg.protocol_version)) {
            return {};
        }
        auto id = mvreg.node;
        auto att_name = mvreg.attr_name;
        auto timestamp = mvreg.timestamp;
        auto crdt_delta = std::move(mvreg.dk);
        auto d_empty = crdt_delta.empty();
        {
            CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::join_delta_node_attr crdt merge");
            if (nodes_.contains(id)) {
                process_delta_node_attr(id, att_name, std::move(crdt_delta));
                std::erase_if(unprocessed_delta_node_att_,
                              [id = id, att_name = att_name](auto &it){ return it.first == id && std::get<0>(it.second) == att_name;});
                return att_name;
            } else if (!host_.is_node_deleted(id)) {
                bool find = false;
                for (auto [begin, end] = unprocessed_delta_node_att_.equal_range(id); begin != end; ++begin) {
                    if (std::get<0>(begin->second) == att_name){
                        find = true;
                        break;
                    }
                }
                if (!find) {
                    unprocessed_delta_node_att_.emplace(id, std::tuple{att_name, std::move(crdt_delta), timestamp});
                }
            } else if (d_empty) {
                unprocessed_delta_edge_from_.erase(id);
                std::erase_if(unprocessed_delta_edge_to_,
                              [id = id](auto &it){ return std::get<0>(it.second) == id;});
                unprocessed_delta_node_att_.erase(id);
                std::erase_if(unprocessed_delta_edge_att_,
                              [id = id](auto &it){ return std::get<0>(it.first) == id || std::get<1>(it.first) == id;});
            }
        }
    } catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
    return {};
}

std::optional<std::string> CRDTSyncEngine::join_delta_edge_attr(MvregEdgeAttrMsg&& mvreg)
{
    CORTEX_PROFILE_ZONE_CS("CRDTSyncEngine::join_delta_edge_attr");
    try {
        if (!protocol_version_matches(host_.get_log_level(), "DSR_EDGE_ATTS", mvreg.protocol_version)) {
            return {};
        }
        auto from = mvreg.from_node;
        auto to = mvreg.to_node;
        auto type = mvreg.type;
        auto att_name = mvreg.attr_name;
        auto timestamp = mvreg.timestamp;
        auto crdt_delta = std::move(mvreg.dk);
        auto d_empty = crdt_delta.empty();
        {
            CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::join_delta_edge_attr crdt merge");
            if (nodes_.contains(from)  and nodes_.at(from).read_reg().fano.contains({to, type}))
            {
                process_delta_edge_attr(from, to, type, att_name, std::move(crdt_delta));
                std::erase_if(unprocessed_delta_edge_att_,
                              [from = from, to = to, type = type, att_name = att_name](auto &it){ return it.first == std::tuple{from, to, type} && std::get<0>(it.second) == att_name;});
                return att_name;
            } else if (!host_.is_node_deleted(from) && !host_.is_node_deleted(to)) {
                bool find = false;
                for (auto [begin, end] = unprocessed_delta_edge_att_.equal_range(std::tuple{from, to, type}); begin != end; ++begin) {
                    if (std::get<0>(begin->second) == att_name){
                        find = true;
                        break;
                    }
                }
                if (!find) {
                    unprocessed_delta_edge_att_.emplace(std::tuple{from, to, type},  std::tuple{att_name, std::move(crdt_delta), timestamp});
                }
            } else if (d_empty) {
                unprocessed_delta_edge_from_.erase(from);
                std::erase_if(unprocessed_delta_edge_to_,
                              [from = from](auto &it){ return std::get<0>(it.second) == from;});
                unprocessed_delta_node_att_.erase(from);
                std::erase_if(unprocessed_delta_edge_att_,
                              [from = from](auto &it){ return std::get<0>(it.first) == from || std::get<1>(it.first) == from;});
            }
        }
    } catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
    return {};
}

void CRDTSyncEngine::join_full_graph(OrMap&& full_graph)
{
    CORTEX_PROFILE_ZONE_CS("CRDTSyncEngine::join_full_graph");
    const auto log_level = host_.get_log_level();
    if (!protocol_version_matches(log_level, "GRAPH_ANSWER", full_graph.protocol_version)) {
        return;
    }

    // The FIRST full-graph import builds the local copy from scratch (join). There is no change to
    // notify, so emit NO signals — the whole-graph signal cascade from this (non-Qt) sync thread is
    // the churn heap-corruption crash. Later imports (re-sync) still emit their deltas.
    const bool emit_signals = !first_full_graph_;
    first_full_graph_ = false;

    std::vector<std::tuple<bool, uint64_t, std::string, std::optional<CRDT::Node>, std::optional<CRDT::Node>>> updates;

    uint64_t id{0}, timestamp{0};
    uint32_t agent_id_ch{0};
    auto delete_unprocessed_deltas = [&](){
        CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::join_full_graph delete unprocessed deltas");
        unprocessed_delta_node_att_.erase(id);
        decltype(unprocessed_delta_edge_from_)::node_type node_handle = unprocessed_delta_edge_from_.extract(id);
        while (!node_handle.empty())
        {
            unprocessed_delta_edge_att_.erase(std::tuple{id, std::get<0>(node_handle.mapped()), std::get<1>(node_handle.mapped())});
            node_handle = unprocessed_delta_edge_from_.extract(id);
        }
        std::erase_if(unprocessed_delta_edge_to_,
                      [&](auto &it){ return std::get<0>(it.second) == id;});
    };

    auto consume_unprocessed_deltas = [&](){
        CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::join_full_graph consume unprocessed deltas");
        decltype(unprocessed_delta_node_att_)::node_type node_handle_node_att = unprocessed_delta_node_att_.extract(id);
        while (!node_handle_node_att.empty())
        {
            auto &[att_name, delta, timestamp_node_att] = node_handle_node_att.mapped();
            if (timestamp < timestamp_node_att) {
                process_delta_node_attr(id, att_name,std::move(delta));
            }
            node_handle_node_att = unprocessed_delta_node_att_.extract(id);
        }

        decltype(unprocessed_delta_edge_from_)::node_type node_handle_edge = unprocessed_delta_edge_from_.extract(id);
        while (!node_handle_edge.empty()) {
            auto &[to, type, delta, timestamp_edge] = node_handle_edge.mapped();
            auto att_key = std::tuple{id, to, type};
            DSR_LOG_DEBUG("[JOIN_FULL] unprocessed_delta_edge_from", id, to, type, (timestamp < timestamp_edge));
            if (timestamp < timestamp_edge) {
                process_delta_edge(id, to, type, std::move(delta));
            }
            if (nodes_.contains(id) and nodes_.at(id).read_reg().fano.contains({to, type})) {
                decltype(unprocessed_delta_edge_att_)::node_type node_handle_edge_att =  unprocessed_delta_edge_att_.extract(att_key);
                while (!node_handle_edge_att.empty()) {
                    auto &[att_name, delta, timestamp_edge_att] = node_handle_edge_att.mapped();
                    DSR_LOG_DEBUG("[JOIN_FULL] edge_att", id, to, type, att_name, (timestamp < timestamp_edge));
                    if (timestamp < timestamp_edge_att) {
                        process_delta_edge_attr(id, to, type, att_name, std::move(delta));
                    }
                    node_handle_edge_att = unprocessed_delta_edge_att_.extract(att_key);
                }
            }
            std::erase_if(unprocessed_delta_edge_to_,
                          [to = to, id = id, type = type](auto& it) { return it.first == to && std::get<0>(it.second) == id && std::get<1>(it.second) == type;});
            node_handle_edge = unprocessed_delta_edge_from_.extract(id);
        }

        node_handle_edge = unprocessed_delta_edge_to_.extract(id);
        while (!node_handle_edge.empty()) {
            auto &[from, type, delta, timestamp_edge] = node_handle_edge.mapped();
            auto att_key = std::tuple{from, id, type};
            DSR_LOG_DEBUG("[JOIN_FULL] unprocessed_delta_edge_to", from, id, type, (timestamp < timestamp_edge));
            if (timestamp < timestamp_edge) {
                process_delta_edge(from, id, type, std::move(delta));
            }
            if (nodes_.contains(from) and nodes_.at(from).read_reg().fano.contains({id, type})) {
                decltype(unprocessed_delta_edge_att_)::node_type node_handle_edge_att =  unprocessed_delta_edge_att_.extract(att_key);
                while (!node_handle_edge_att.empty()) {
                    auto &[att_name, delta, timestamp_edge_att] = node_handle_edge_att.mapped();
                    DSR_LOG_DEBUG("[JOIN_FULL] edge_att", from, id, type, att_name, (timestamp < timestamp_edge));
                    if (timestamp < timestamp_edge_att) {
                        process_delta_edge_attr(from, id, type, att_name, std::move(delta));
                    }
                    node_handle_edge_att = unprocessed_delta_edge_att_.extract(att_key);
                }
            }

            node_handle_edge = unprocessed_delta_edge_to_.extract(id);
        }
    };

    {
        CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::join_full_graph crdt");
        for (auto &[k, val] : full_graph.m) {
            auto mv = std::move(val.dk);
            bool mv_empty = mv.empty();
            agent_id_ch = val.agent_id;
            auto it = nodes_.find(k);
            std::optional<CRDT::Node> nd =
                    (it != nodes_.end() and !it->second.empty()) ? std::make_optional(it->second.read_reg()) : std::nullopt;
            id = k;
            if (host_.is_node_deleted(k)) {
                nodes_.erase(k);
                delete_unprocessed_deltas();
                continue;
            }
            if (it == nodes_.end()) {
                it = nodes_.emplace(k, mvreg<CRDT::Node>{}).first;
            }
            it->second.join(std::move(mv));
            if (mv_empty or it->second.empty()) {
                host_.update_maps_node_delete(
                    k,
                    nd.has_value() ? std::optional<std::string_view>{nd->type} : std::nullopt,
                    nd.has_value() ? collect_outgoing_edge_keys(nd->fano) : SyncEngineHost::EdgeKeyList{});
                updates.emplace_back(false, k, "", std::nullopt, std::nullopt);
                delete_unprocessed_deltas();
            } else {
                const auto& reg = it->second.read_reg();
                host_.update_maps_node_insert(reg.id, reg.name, reg.type, collect_outgoing_edge_keys(reg.fano));
                updates.emplace_back(true, k, reg.type, nd, reg);
                consume_unprocessed_deltas();
            }
        }
    }
    {
        CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::join_full_graph emit phase");
        if (emit_signals)
        for (auto &[signal, node_id, type, nd, current_nd] : updates) {
            if (signal) {
                if (!nd.has_value() || nd->attrs != current_nd->attrs) {
                    host_.on_remote_node_updated(node_id, type, agent_id_ch);
                } else if (nd.value() != *current_nd) {
                    const auto& iter = current_nd->fano;
                    for (const auto &[k, v] : nd->fano) {
                        if (!iter.contains(k)) {
                            std::optional<Edge> del_edge = (v.dk.ds.size() > 0)
                                ? std::make_optional(to_user_edge(v.read_reg())) : std::nullopt;
                            host_.on_remote_edge_deleted(node_id, k.first, k.second, del_edge, agent_id_ch);
                        }
                    }
                    for (const auto &[k, v] : iter) {
                        if (auto it = nd->fano.find(k); it == nd->fano.end() or it->second != v)
                            host_.on_remote_edge_updated(node_id, k.first, k.second, agent_id_ch);
                    }
                }
            } else {
                std::optional<Node> nd_user = nd.has_value() ? std::make_optional(to_user_node(*nd)) : std::nullopt;
                host_.on_remote_node_deleted(node_id, nd_user, {}, agent_id_ch);
            }
        }
    }

    std::erase_if(nodes_, [](const auto& item) {
        return item.second.empty();
    });
}

std::map<uint64_t, MvregNodeMsg> CRDTSyncEngine::export_mvreg_map() const
{
    CORTEX_PROFILE_DETAIL_N("CRDTSyncEngine::export_mvreg_map");
    const auto log_level = host_.get_log_level();
    std::map<uint64_t, MvregNodeMsg> m;
    for (const auto& kv : nodes_) {
        if (kv.second.empty()) {
            DSR_LOG_WARNING_L(log_level, "[CRDT] export skipping empty node register", kv.first);
            continue;
        }
        MvregNodeMsg msg;
        msg.dk = kv.second;
        msg.id = kv.first;
        msg.agent_id = host_.local_agent_id();
        msg.timestamp = get_unix_timestamp();
        msg.protocol_version = DSR_PROTOCOL_VERSION;
        msg.sync_mode = sync_mode_wire_value(host_.local_sync_mode());
        m.emplace(kv.first, std::move(msg));
    }
    return m;
}
