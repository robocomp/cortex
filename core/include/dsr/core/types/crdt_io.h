//
// CRDT conversion helpers kept separate from backend engines.
//

#pragma once

#include "dsr/core/types/user_types.h"
#include "dsr/core/types/crdt_types.h"
#include "dsr/core/types/internal_types.h"

namespace DSR {

inline MvregNodeMsg crdt_node_to_msg(uint32_t agent_id, uint64_t id, mvreg<CRDTNode>&& data)
{
    MvregNodeMsg msg;
    msg.dk = std::move(data);
    msg.id = id;
    msg.agent_id = agent_id;
    msg.timestamp = get_unix_timestamp();
    msg.protocol_version = DSR_PROTOCOL_VERSION;
    return msg;
}

template<typename S>
inline MvregEdgeMsg crdt_edge_to_msg(uint32_t agent_id, uint64_t from, uint64_t to, S&& type, mvreg<CRDTEdge>&& data)
{
    MvregEdgeMsg msg;
    msg.dk = std::move(data);
    msg.id = from;
    msg.to = to;
    msg.from = from;
    msg.type = std::forward<S>(type);
    msg.agent_id = agent_id;
    msg.timestamp = get_unix_timestamp();
    msg.protocol_version = DSR_PROTOCOL_VERSION;
    return msg;
}

template<typename S>
inline MvregNodeAttrMsg crdt_node_attr_to_msg(uint32_t agent_id, uint64_t id, uint64_t node, S&& attr, mvreg<CRDTAttribute>&& data)
{
    MvregNodeAttrMsg msg;
    msg.dk = std::move(data);
    msg.id = id;
    msg.node = node;
    msg.attr_name = std::forward<S>(attr);
    msg.agent_id = agent_id;
    msg.timestamp = get_unix_timestamp();
    msg.protocol_version = DSR_PROTOCOL_VERSION;
    return msg;
}

template<typename TS, typename AS>
inline MvregEdgeAttrMsg crdt_edge_attr_to_msg(uint32_t agent_id, uint64_t id, uint64_t from, uint64_t to,
                                              TS&& type, AS&& attr, mvreg<CRDTAttribute>&& data)
{
    MvregEdgeAttrMsg msg;
    msg.dk = std::move(data);
    msg.id = id;
    msg.from_node = from;
    msg.to_node = to;
    msg.type = std::forward<TS>(type);
    msg.attr_name = std::forward<AS>(attr);
    msg.agent_id = agent_id;
    msg.timestamp = get_unix_timestamp();
    msg.protocol_version = DSR_PROTOCOL_VERSION;
    return msg;
}

inline CRDTEdge user_edge_to_crdt(Edge&& edge)
{
    CRDTEdge crdt_edge;

    crdt_edge.agent_id(edge.agent_id());
    crdt_edge.from(edge.from());
    crdt_edge.to(edge.to());
    crdt_edge.type(std::move(edge.type()));
    for (auto&& [k, v] : edge.attrs()) {
        mvreg<CRDTAttribute> mv;
        mv.write(std::move(v));
        crdt_edge.attrs().emplace(k, std::move(mv));
    }

    return crdt_edge;
}

inline CRDTEdge user_edge_to_crdt(const Edge& edge)
{
    CRDTEdge crdt_edge;

    crdt_edge.agent_id(edge.agent_id());
    crdt_edge.from(edge.from());
    crdt_edge.to(edge.to());
    crdt_edge.type(edge.type());
    for (auto& [k, v] : edge.attrs()) {
        mvreg<CRDTAttribute> mv;
        mv.write(v);
        crdt_edge.attrs().emplace(k, std::move(mv));
    }

    return crdt_edge;
}

inline Edge to_user_edge(const CRDTEdge& edge)
{
    Edge out(edge.to(), edge.from(), edge.type(), {}, edge.agent_id());
    auto& attrs = out.attrs();
    for (const auto& [name, attr] : edge.attrs()) {
        if (!attr.empty()) {
            attrs.emplace(name, attr.read_reg());
        }
    }
    return out;
}

inline Node to_user_node(const CRDTNode& node)
{
    Node out(node.agent_id(), node.type());
    out.id(node.id());
    out.name(node.name());

    auto& attrs = out.attrs();
    for (const auto& [name, attr] : node.attrs()) {
        if (!attr.empty()) {
            attrs.emplace(name, attr.read_reg());
        }
    }

    auto& fano = out.fano();
    for (const auto& [key, edge] : node.fano()) {
        if (!edge.empty()) {
            fano.emplace(key, to_user_edge(edge.read_reg()));
        }
    }

    return out;
}

inline CRDTNode user_node_to_crdt(Node&& node)
{
    CRDTNode crdt_node;

    crdt_node.agent_id(node.agent_id());
    crdt_node.id(node.id());
    crdt_node.type(std::move(node.type()));
    crdt_node.name(std::move(node.name()));

    for (auto&& [k, val] : node.attrs()) {
        mvreg<CRDTAttribute> mv;
        mv.write(std::move(val));
        crdt_node.attrs().emplace(k, std::move(mv));
    }

    for (auto& [k, v] : node.fano()) {
        mvreg<CRDTEdge> mv;
        mv.write(user_edge_to_crdt(std::move(v)));
        crdt_node.fano().emplace(k, std::move(mv));
    }

    return crdt_node;
}

inline CRDTNode user_node_to_crdt(const Node& node)
{
    CRDTNode crdt_node;

    crdt_node.agent_id(node.agent_id());
    crdt_node.id(node.id());
    crdt_node.type(node.type());
    crdt_node.name(node.name());
    for (auto& [k, v] : node.attrs()) {
        mvreg<CRDTAttribute> mv;
        mv.write(v);
        crdt_node.attrs().emplace(k, std::move(mv));
    }

    for (auto& [k, v] : node.fano()) {
        mvreg<CRDTEdge> mv;
        mv.write(user_edge_to_crdt(v));
        crdt_node.fano().emplace(k, std::move(mv));
    }

    return crdt_node;
}

} // namespace DSR
