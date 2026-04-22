#pragma once

#include "dsr/core/types/lww_io.h"

#include <unordered_map>
#include <unordered_set>

namespace DSR::LWW {

using EdgeSet = std::unordered_set<EdgeKey, hash_tuple>;
using FromIndex = std::unordered_map<uint64_t, EdgeSet>;
using ToIndex = std::unordered_map<uint64_t, EdgeSet>;
using TypeIndex = std::unordered_map<std::string, EdgeSet>;

inline void edge_index_insert(FromIndex& from_idx, ToIndex& to_idx, TypeIndex& type_idx, const EdgeKey& key)
{
    from_idx[std::get<0>(key)].insert(key);
    to_idx[std::get<1>(key)].insert(key);
    type_idx[std::get<2>(key)].insert(key);
}

inline void edge_index_erase(FromIndex& from_idx, ToIndex& to_idx, TypeIndex& type_idx, const EdgeKey& key)
{
    if (auto it = from_idx.find(std::get<0>(key)); it != from_idx.end()) {
        it->second.erase(key);
        if (it->second.empty()) {
            from_idx.erase(it);
        }
    }
    if (auto it = to_idx.find(std::get<1>(key)); it != to_idx.end()) {
        it->second.erase(key);
        if (it->second.empty()) {
            to_idx.erase(it);
        }
    }
    if (auto it = type_idx.find(std::get<2>(key)); it != type_idx.end()) {
        it->second.erase(key);
        if (it->second.empty()) {
            type_idx.erase(it);
        }
    }
}

template <typename EdgeMap>
Node to_user_node(const NodeState& state, const EdgeMap& edges, const FromIndex& from_idx)
{
    Node out(state.agent_id, state.type);
    out.id(state.id);
    out.name(state.name);
    auto& attrs = out.attrs();
    for (const auto& [name, attr] : state.attrs) {
        attrs.emplace(name, attr.value);
    }
    auto fit = from_idx.find(state.id);
    if (fit == from_idx.end()) {
        return out;
    }
    auto& fano = out.fano();
    for (const auto& key : fit->second) {
        if (auto eit = edges.find(key); eit != edges.end()) {
            fano.emplace(std::pair{eit->second.to, eit->second.type}, to_user_edge(eit->second));
        }
    }
    return out;
}

template <typename EdgeMap, typename Visitor>
bool for_each_edge_from(const EdgeMap& edges, const FromIndex& from_idx, uint64_t from, Visitor&& visitor)
{
    auto it = from_idx.find(from);
    if (it == from_idx.end()) {
        return false;
    }
    for (const auto& key : it->second) {
        const auto& edge = edges.at(key);
        visitor(edge.to, edge.type, to_user_edge(edge));
    }
    return true;
}

template <typename EdgeMap, typename Visitor>
bool for_each_edge_to(const EdgeMap& edges, const ToIndex& to_idx, uint64_t to, Visitor&& visitor)
{
    auto it = to_idx.find(to);
    if (it == to_idx.end()) {
        return false;
    }
    for (const auto& key : it->second) {
        const auto& edge = edges.at(key);
        visitor(edge.from, edge.type, to_user_edge(edge));
    }
    return true;
}

template <typename EdgeMap, typename Visitor>
void for_each_edge_of_type(const EdgeMap& edges, const TypeIndex& type_idx, const std::string& type, Visitor&& visitor)
{
    auto it = type_idx.find(type);
    if (it == type_idx.end()) {
        return;
    }
    for (const auto& key : it->second) {
        const auto& edge = edges.at(key);
        visitor(edge.from, edge.to, to_user_edge(edge));
    }
}

} // namespace DSR::LWW
