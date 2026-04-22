//
// Legacy compatibility shim. Prefer including `crdt_io.h` directly in new code.
//

#ifndef CONVERTER_H
#define CONVERTER_H

#include "dsr/core/types/crdt_io.h"

namespace DSR {

inline MvregNodeMsg CRDTNode_to_Msg(uint32_t agent_id, uint64_t id, mvreg<CRDTNode>&& data)
{
    return crdt_node_to_msg(agent_id, id, std::move(data));
}

template<typename S>
inline MvregEdgeMsg CRDTEdge_to_Msg(uint32_t agent_id, uint64_t from, uint64_t to, S&& type, mvreg<CRDTEdge>&& data)
{
    return crdt_edge_to_msg(agent_id, from, to, std::forward<S>(type), std::move(data));
}

template<typename S>
inline MvregNodeAttrMsg CRDTNodeAttr_to_Msg(uint32_t agent_id, uint64_t id, uint64_t node, S&& attr, mvreg<CRDTAttribute>&& data)
{
    return crdt_node_attr_to_msg(agent_id, id, node, std::forward<S>(attr), std::move(data));
}

template<typename TS, typename AS>
inline MvregEdgeAttrMsg CRDTEdgeAttr_to_Msg(uint32_t agent_id, uint64_t id, uint64_t from, uint64_t to,
                                            TS&& type, AS&& attr, mvreg<CRDTAttribute>&& data)
{
    return crdt_edge_attr_to_msg(agent_id, id, from, to, std::forward<TS>(type), std::forward<AS>(attr), std::move(data));
}

} // namespace DSR

#endif // CONVERTER_H
