#pragma once

#include "dsr/core/types/internal_types.h"
#include "dsr/core/types/lww_types.h"

namespace DSR::LWW {

inline std::map<std::string, AttrState> to_attr_state_map(const std::map<std::string, Attribute>& attrs, const Version& version)
{
    std::map<std::string, AttrState> out;
    for (const auto& [name, attr] : attrs) {
        out.emplace(name, AttrState{attr, version});
    }
    return out;
}

inline NodeState to_node_state(const Node& node, const Version& version, uint32_t agent_id)
{
    NodeState state;
    state.id = node.id();
    state.type = node.type();
    state.name = node.name();
    state.agent_id = agent_id;
    state.version = version;
    state.attrs = to_attr_state_map(node.attrs(), version);
    return state;
}

inline EdgeState to_edge_state(const Edge& edge, const Version& version, uint32_t agent_id)
{
    EdgeState state;
    state.from = edge.from();
    state.to = edge.to();
    state.type = edge.type();
    state.agent_id = agent_id;
    state.version = version;
    state.attrs = to_attr_state_map(edge.attrs(), version);
    return state;
}

inline NodeState to_node_state(const LWWNodeMsg& msg)
{
    const auto version = version_of(msg.timestamp, msg.agent_id);
    NodeState state;
    state.id = msg.id;
    state.type = msg.type;
    state.name = msg.name;
    state.agent_id = msg.agent_id;
    state.version = version;
    state.attrs = to_attr_state_map(msg.attrs, version);
    return state;
}

inline EdgeState to_edge_state(const LWWEdgeMsg& msg)
{
    const auto version = version_of(msg.timestamp, msg.agent_id);
    EdgeState state;
    state.from = msg.from;
    state.to = msg.to;
    state.type = msg.type;
    state.agent_id = msg.agent_id;
    state.version = version;
    state.attrs = to_attr_state_map(msg.attrs, version);
    return state;
}

inline Edge to_user_edge(const EdgeState& state)
{
    Edge out(state.to, state.from, state.type, {}, state.agent_id);
    auto& attrs = out.attrs();
    for (const auto& [name, attr] : state.attrs) {
        attrs.emplace(name, attr.value);
    }
    return out;
}

template <typename EdgeMap>
Node to_user_node(const NodeState& state, const EdgeMap& edges)
{
    Node out(state.agent_id, state.type);
    out.id(state.id);
    out.name(state.name);
    auto& attrs = out.attrs();
    for (const auto& [name, attr] : state.attrs) {
        attrs.emplace(name, attr.value);
    }
    auto& fano = out.fano();
    for (const auto& [key, edge] : edges) {
        if (edge.from != state.id) {
            continue;
        }
        fano.emplace(std::pair{edge.to, edge.type}, to_user_edge(edge));
    }
    return out;
}

inline LWWNodeMsg to_node_msg(const NodeState& state)
{
    LWWNodeMsg msg;
    msg.id = state.id;
    msg.type = state.type;
    msg.name = state.name;
    msg.agent_id = state.agent_id;
    msg.timestamp = state.version.timestamp;
    msg.deleted = false;
    msg.protocol_version = DSR_PROTOCOL_VERSION;
    msg.sync_mode = sync_mode_wire_value(SyncMode::LWW);
    for (const auto& [name, attr] : state.attrs) {
        msg.attrs.emplace(name, attr.value);
    }
    return msg;
}

inline LWWNodeMsg to_node_tombstone_msg(uint64_t id, const Tombstone& tombstone)
{
    LWWNodeMsg msg;
    msg.id = id;
    msg.agent_id = tombstone.version.agent_id;
    msg.timestamp = tombstone.version.timestamp;
    msg.deleted = true;
    msg.protocol_version = DSR_PROTOCOL_VERSION;
    msg.sync_mode = sync_mode_wire_value(SyncMode::LWW);
    return msg;
}

inline LWWEdgeMsg to_edge_msg(const EdgeState& state)
{
    LWWEdgeMsg msg;
    msg.from = state.from;
    msg.to = state.to;
    msg.type = state.type;
    msg.agent_id = state.agent_id;
    msg.timestamp = state.version.timestamp;
    msg.deleted = false;
    msg.protocol_version = DSR_PROTOCOL_VERSION;
    msg.sync_mode = sync_mode_wire_value(SyncMode::LWW);
    for (const auto& [name, attr] : state.attrs) {
        msg.attrs.emplace(name, attr.value);
    }
    return msg;
}

inline LWWEdgeMsg to_edge_tombstone_msg(uint64_t from, uint64_t to, const std::string& type, const Tombstone& tombstone)
{
    LWWEdgeMsg msg;
    msg.from = from;
    msg.to = to;
    msg.type = type;
    msg.agent_id = tombstone.version.agent_id;
    msg.timestamp = tombstone.version.timestamp;
    msg.deleted = true;
    msg.protocol_version = DSR_PROTOCOL_VERSION;
    msg.sync_mode = sync_mode_wire_value(SyncMode::LWW);
    return msg;
}

} // namespace DSR::LWW
