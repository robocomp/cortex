#pragma once

#include "dsr/core/types/lww_io.h"

#include <unordered_map>
#include <unordered_set>

namespace DSR::LWW {

using EdgePtrSet = std::unordered_set<const EdgeState*>;
using FromIndex = std::unordered_map<uint64_t, EdgePtrSet>;
using ToIndex = std::unordered_map<uint64_t, EdgePtrSet>;
using TypeIndex = std::unordered_map<std::string, EdgePtrSet>;

inline void edge_index_insert(FromIndex& from_idx, ToIndex& to_idx, TypeIndex& type_idx, const EdgeState& edge)
{
    from_idx[edge.from].insert(&edge);
    to_idx[edge.to].insert(&edge);
    type_idx[edge.type].insert(&edge);
}

inline void edge_index_erase(FromIndex& from_idx, ToIndex& to_idx, TypeIndex& type_idx, const EdgeState& edge)
{
    if (auto it = from_idx.find(edge.from); it != from_idx.end()) {
        it->second.erase(&edge);
        if (it->second.empty()) {
            from_idx.erase(it);
        }
    }
    if (auto it = to_idx.find(edge.to); it != to_idx.end()) {
        it->second.erase(&edge);
        if (it->second.empty()) {
            to_idx.erase(it);
        }
    }
    if (auto it = type_idx.find(edge.type); it != type_idx.end()) {
        it->second.erase(&edge);
        if (it->second.empty()) {
            type_idx.erase(it);
        }
    }
}

inline Node to_user_node(const NodeState& state, const FromIndex& from_idx)
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
    for (const auto* edge : fit->second) {
        fano.emplace(std::pair{edge->to, edge->type}, to_user_edge(*edge));
    }
    return out;
}

template <typename Visitor>
bool for_each_edge_from(const FromIndex& from_idx, uint64_t from, Visitor&& visitor)
{
    auto it = from_idx.find(from);
    if (it == from_idx.end()) {
        return false;
    }
    for (const auto* edge : it->second) {
        visitor(edge->to, edge->type, to_user_edge(*edge));
    }
    return true;
}

template <typename Visitor>
bool for_each_edge_to(const ToIndex& to_idx, uint64_t to, Visitor&& visitor)
{
    auto it = to_idx.find(to);
    if (it == to_idx.end()) {
        return false;
    }
    for (const auto* edge : it->second) {
        visitor(edge->from, edge->type, to_user_edge(*edge));
    }
    return true;
}

template <typename Visitor>
void for_each_edge_of_type(const TypeIndex& type_idx, const std::string& type, Visitor&& visitor)
{
    auto it = type_idx.find(type);
    if (it == type_idx.end()) {
        return;
    }
    for (const auto* edge : it->second) {
        visitor(edge->from, edge->to, to_user_edge(*edge));
    }
}

} // namespace DSR::LWW
