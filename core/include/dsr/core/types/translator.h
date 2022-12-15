//
// Created by juancarlos on 1/7/20.
//

#pragma once

#include "crdt_types.h"
#include "user_types.h"

#include <cassert>

namespace DSR
{

    // Translators
    inline static CRDTEdge user_edge_to_crdt(Edge &&edge)
    {
        CRDTEdge crdt_edge;

        crdt_edge.agent_id(edge.agent_id());
        crdt_edge.from(edge.from());
        crdt_edge.to(edge.to());
        crdt_edge.type(std::move(edge.type()));
        for (auto &&[k, v] : edge.attrs())
        {
            mvreg<CRDTAttribute> mv;
            mv.write(std::move(v));
            crdt_edge.attrs().emplace(k, std::move(mv));
        }

        return crdt_edge;
    }

    inline static CRDTEdge user_edge_to_crdt(const Edge &edge)
    {
        CRDTEdge crdt_edge;

        crdt_edge.agent_id(edge.agent_id());
        crdt_edge.from(edge.from());
        crdt_edge.to(edge.to());
        crdt_edge.type(edge.type());
        for (auto &[k, v] : edge.attrs())
        {
            mvreg<CRDTAttribute> mv;
            mv.write(v);
            crdt_edge.attrs().emplace(k, std::move(mv));
        }

        return crdt_edge;
    }

    inline static CRDTNode user_node_to_crdt(Node &&node)
    {
        CRDTNode crdt_node;

        crdt_node.agent_id(node.agent_id());
        crdt_node.id(node.id());
        crdt_node.type(std::move(node.type()));
        crdt_node.name(std::move(node.name()));

        for (auto &&[k, val] : node.attrs())
        {
            mvreg<CRDTAttribute> mv;
            mv.write(std::move(val));
            crdt_node.attrs().emplace(k, std::move(mv));
        }

        for (auto &[k, v] : node.fano())
        {
            mvreg<CRDTEdge> mv;
            mv.write(user_edge_to_crdt(std::move(v)));
            crdt_node.fano().emplace(k, std::move(mv));
        }

        return crdt_node;
    }

    inline static CRDTNode user_node_to_crdt(const Node &node)
    {
        CRDTNode crdt_node;

        crdt_node.agent_id(node.agent_id());
        crdt_node.id(node.id());
        crdt_node.type(node.type());
        crdt_node.name(node.name());
        for (auto &[k, v] : node.attrs())
        {
            mvreg<CRDTAttribute> mv;
            mv.write(v);
            crdt_node.attrs().emplace(k, std::move(mv));
        }

        for (auto &[k, v] : node.fano())
        {
            mvreg<CRDTEdge> mv;
            mv.write(user_edge_to_crdt(v));
            crdt_node.fano().emplace(k, std::move(mv));
        }

        return crdt_node;
    }
    
}  // namespace DSR
