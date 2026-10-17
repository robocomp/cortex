//
// Created by juancarlos on 18/8/20.
//

#ifndef DSR_EDGE_TYPE_H
#define DSR_EDGE_TYPE_H

#include <string_view>
#include <array>
#include <experimental/array>
#include "type_checker.h"
using namespace std::literals;



template<const std::string_view& n>
struct EdgeType {
    static constexpr bool edge_type = true;
    static constexpr std::string_view attr_name = std::string_view(n);
};


#define REGISTER_EDGE_TYPE(x) \
                                static constexpr auto    x ##_edge_type_str = std::string_view(#x ); \
                                [[maybe_unused]] inline bool x ##__b =  edge_types::register_type( x##_edge_type_str);     \
                                using x##_edge_type = EdgeType< x##_edge_type_str >;                      \
                                \


inline std::unordered_set<std::string_view> edge_types::set_type_;

REGISTER_EDGE_TYPE(RT)
// Consumer -> affordance: "I am executing this, and here is which proposal and which pose."
REGISTER_EDGE_TYPE(executing)
REGISTER_EDGE_TYPE(VRT)
REGISTER_EDGE_TYPE(reachable)
REGISTER_EDGE_TYPE(in)
REGISTER_EDGE_TYPE(knows)
REGISTER_EDGE_TYPE(transitable)
REGISTER_EDGE_TYPE(visible)
REGISTER_EDGE_TYPE(accessible)
REGISTER_EDGE_TYPE(graspable)
REGISTER_EDGE_TYPE(talking)
REGISTER_EDGE_TYPE(looking_at)
REGISTER_EDGE_TYPE(sitting)
REGISTER_EDGE_TYPE(standing)
REGISTER_EDGE_TYPE(close_to)
REGISTER_EDGE_TYPE(has)
REGISTER_EDGE_TYPE(blocked)
REGISTER_EDGE_TYPE(is_blocking)
REGISTER_EDGE_TYPE(is_near)
REGISTER_EDGE_TYPE(front)
REGISTER_EDGE_TYPE(interacting)
REGISTER_EDGE_TYPE(interactive)
REGISTER_EDGE_TYPE(thinks)
REGISTER_EDGE_TYPE(goto_action)
REGISTER_EDGE_TYPE(attention_action)
REGISTER_EDGE_TYPE(following_action)
REGISTER_EDGE_TYPE(lost)
REGISTER_EDGE_TYPE(has_intention)
REGISTER_EDGE_TYPE(collision)
REGISTER_EDGE_TYPE(TARGET)
REGISTER_EDGE_TYPE(testtype_e)
REGISTER_EDGE_TYPE(current)
REGISTER_EDGE_TYPE(exit)
REGISTER_EDGE_TYPE(match)

// ── level-2 meta-concept membership (ring_metaconcept) ─ registered 2026-07-26 ───────────────────
//    NON-RT edge rig→member (dining_set_* → table_*/chair_*). It is BOTH the structural link and the
//    top-down message channel: the arrangement's per-member empirical prior rides on this edge's
//    attributes (rig_yaw_prior / rig_yaw_kappa / …, see dsr_attr_name.h). Non-RT on purpose — the
//    members are already room→member RT children, and a second RT parent would double-parent them
//    in the transform tree.
REGISTER_EDGE_TYPE(group_member)

// ── PROVISIONAL ROOM ─ registered 2026-09-14 ────────────────────────────────────────────────────
//    SELF-EDGE room->room, written by room_concept while a room is still being estimated and removed
//    when the layout is committed. Presence means "this room node exists so that something can be
//    planned against it, but its boundary is a lower bound and its walls are not yet walls".
//
//    An edge rather than an attribute, for two reasons. update_edge_signal carries the edge TYPE, so
//    a consumer filters on it directly, where update_node_signal fires for every attribute a node
//    has. And update_node DELETES attributes absent from the copy it is handed, which has bitten this
//    codebase before; an edge cannot be erased by an unrelated writer's partial update.
//
//    ⚠ THIS EDGE IS A SIGNAL, NOT A SAFETY MECHANISM. controller_world_model.cpp takes
//    get_nodes_by_type("room").front() with no filter, so the controller will plan against a
//    provisional room exactly as if it were surveyed, and will keep doing so until someone adds the
//    check. The provisional room must therefore be safe BY CONSTRUCTION -- sized from confirmed free
//    space, never from sensor range plus an outward margin -- and the robot's own footprint test
//    against the live grid stays the authority on collision.
//
//    ⚠ PROMOTION REMOVES THE EDGE AND KEEPS THE NODE. The node id is load-bearing: residual_concept
//    binds its field to it (ResidualFitter::ensure_instance stores room_node_id_) and the controller
//    latches room_name until the node disappears. Deleting and recreating the room to "publish the
//    real one" would silently break both.
REGISTER_EDGE_TYPE(proto)

#endif //DSR_EDGE_TYPE_H
