//
// Renaming a node through update_node: same id, new name, on this graph AND on a
// peer graph synced over DDS (remote apply path refreshes name_map/id_map).
//
#include "dsr/api/dsr_api.h"
#include "../utils.h"
#include <thread>
#include <memory>
#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QEventLoop>

#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators.hpp"

#include "dsr/core/types/type_checking/dsr_edge_type.h"
#include "dsr/core/types/type_checking/dsr_node_type.h"
#include "graph_test_access.h"
#include <set>

using namespace DSR;
using namespace std::chrono_literals;

namespace {

class ScopedQtApp
{
public:
    ScopedQtApp()
    {
        if (QCoreApplication::instance() == nullptr)
            app_ = std::make_unique<QCoreApplication>(argc_, nullptr);
    }
    void process()
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
private:
    int argc_{0};
    std::unique_ptr<QCoreApplication> app_;
};

template <typename Predicate>
bool wait_for(Predicate&& predicate, std::chrono::milliseconds timeout = 5000ms)
{
    ScopedQtApp qt;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        qt.process();
        if (predicate()) return true;
        std::this_thread::sleep_for(50ms);
    }
    qt.process();
    return predicate();
}

// Everything a renamed node must keep, checked on one graph.
void check_renamed(DSRGraph& G, uint64_t room_id, uint64_t root_id, uint64_t child_id)
{
    auto renamed = G.get_node("room_2");
    REQUIRE(renamed.has_value());
    REQUIRE(renamed->id() == room_id);
    REQUIRE(renamed->name() == "room_2");
    REQUIRE_FALSE(G.get_node("room_4").has_value());
    REQUIRE(G.get_name_from_id(room_id) == std::optional<std::string>{"room_2"});
    REQUIRE(G.get_id_from_name("room_2") == std::optional<uint64_t>{room_id});
    REQUIRE_FALSE(G.get_id_from_name("room_4").has_value());

    auto by_id = G.get_node(room_id);
    REQUIRE(by_id.has_value());
    REQUIRE(by_id->name() == "room_2");
    REQUIRE(G.get_attrib_by_name<level_att>(by_id.value()) == std::optional<int>{7});

    // incoming (root -> room) and outgoing (room -> child) edges survive
    REQUIRE(G.get_edge(root_id, room_id, "in").has_value());
    REQUIRE(G.get_edge(room_id, child_id, "in").has_value());
    REQUIRE(G.get_edge("root", "room_2", "in").has_value());
    REQUIRE(G.get_edge("room_2", "child_of_room", "in").has_value());
    REQUIRE(G.get_nodes_by_type("room").size() == 1);
}

}  // namespace

TEST_CASE("update_node renames a node, keeping its id, locally and on a DDS peer", "[SYNCHRONIZATION][RENAME][DDS]")
{
    ScopedQtApp qt;
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    CAPTURE(sync_mode_label(sync_mode));

    constexpr int8_t kDomain = 77;  // private test domain, away from the agents' 0 and media 7
    auto ctx = make_empty_config_file();
    const auto id_a = static_cast<uint32_t>(rand() % 1000 + 3000);
    DSRGraph A(make_test_graph_settings(random_string(10), id_a, ctx, true, kDomain, SignalMode::QT, sync_mode));
    DSRGraph B(make_test_graph_settings(random_string(11), id_a + 1, std::string{}, true, kDomain, SignalMode::QT, sync_mode));
    REQUIRE(wait_for([&] { return B.size() == A.size() and B.get_node("root").has_value(); }));

    const auto root_id = A.get_node("root")->id();

    auto room = Node::create<room_node_type>("room_4");
    A.add_or_modify_attrib_local<level_att>(room, 7);
    const auto room_id_opt = A.insert_node(room);
    REQUIRE(room_id_opt.has_value());
    const auto room_id = room_id_opt.value();

    auto child = Node::create<testtype_node_type>("child_of_room");
    const auto child_id_opt = A.insert_node(child);
    REQUIRE(child_id_opt.has_value());
    const auto child_id = child_id_opt.value();

    REQUIRE(A.insert_or_assign_edge(Edge::create<in_edge_type>(root_id, room_id)));
    REQUIRE(A.insert_or_assign_edge(Edge::create<in_edge_type>(room_id, child_id)));

    REQUIRE(wait_for([&] {
        return B.get_node("room_4").has_value() and B.get_node("child_of_room").has_value()
           and B.get_edge(root_id, room_id, "in").has_value()
           and B.get_edge(room_id, child_id, "in").has_value();
    }));

    SECTION("rename to a free name succeeds and propagates")
    {
        auto n = A.get_node("room_4");
        REQUIRE(n.has_value());
        n->name("room_2");
        REQUIRE(A.update_node(n.value()));

        check_renamed(A, room_id, root_id, child_id);

        REQUIRE(wait_for([&] { return B.get_node("room_2").has_value() and not B.get_node("room_4").has_value(); }));
        check_renamed(B, room_id, root_id, child_id);

        // an attribute update after the rename still works and propagates under the new name
        auto again = A.get_node("room_2");
        REQUIRE(again.has_value());
        A.add_or_modify_attrib_local<level_att>(again.value(), 9);
        REQUIRE(A.update_node(again.value()));
        REQUIRE(wait_for([&] {
            auto b = B.get_node("room_2");
            return b.has_value() and B.get_attrib_by_name<level_att>(b.value()) == std::optional<int>{9};
        }));
        REQUIRE(B.get_node(room_id)->name() == "room_2");

        SECTION("renaming another node onto a used name returns false, no throw")
        {
            auto c = A.get_node("child_of_room");
            REQUIRE(c.has_value());
            c->name("room_2");
            bool r = true;
            REQUIRE_NOTHROW(r = A.update_node(c.value()));
            REQUIRE_FALSE(r);
            REQUIRE(A.get_node("child_of_room").has_value());
            REQUIRE(A.get_node("room_2")->id() == room_id);
        }
    }

    SECTION("renaming onto an existing node's name returns false, no throw")
    {
        auto c = A.get_node("child_of_room");
        REQUIRE(c.has_value());
        c->name("room_4");
        bool r = true;
        REQUIRE_NOTHROW(r = A.update_node(c.value()));
        REQUIRE_FALSE(r);
        REQUIRE(A.get_node("room_4")->id() == room_id);
        REQUIRE(A.get_node("child_of_room")->id() == child_id);
    }
}

// A renames while C's concurrent edits to the same node are still in flight: C adds an
// edge room->x, deletes the edge room->y and bumps an attribute, and A renames BEFORE
// it has applied any of that. All remote applies are queued onto this (single) main
// thread, so "A has not seen C's change" is deterministic: nothing is applied until
// wait_for() pumps the event loop. C is then guaranteed to receive A's rename AFTER its
// own edits — the case where a whole-register rename would wipe/resurrect them.
TEST_CASE("rename concurrent with another agent's edge and attribute edits loses nothing", "[SYNCHRONIZATION][RENAME][DDS][CONCURRENT]")
{
    ScopedQtApp qt;
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    CAPTURE(sync_mode_label(sync_mode));

    constexpr int8_t kDomain = 77;
    auto ctx = make_empty_config_file();
    const auto id_a = static_cast<uint32_t>(rand() % 1000 + 3000);
    DSRGraph A(make_test_graph_settings(random_string(10), id_a, ctx, true, kDomain, SignalMode::QT, sync_mode));
    DSRGraph B(make_test_graph_settings(random_string(11), id_a + 1, std::string{}, true, kDomain, SignalMode::QT, sync_mode));
    DSRGraph C(make_test_graph_settings(random_string(12), id_a + 2, std::string{}, true, kDomain, SignalMode::QT, sync_mode));
    REQUIRE(wait_for([&] { return B.get_node("root").has_value() and C.get_node("root").has_value(); }));

    auto room = Node::create<room_node_type>("room_4");
    A.add_or_modify_attrib_local<level_att>(room, 7);
    const auto room_id = A.insert_node(room).value();
    auto x_node = Node::create<testtype_node_type>("x_new_target");
    auto y_node = Node::create<testtype_node_type>("y_old_target");
    const auto x_id = A.insert_node(x_node).value();
    const auto y_id = A.insert_node(y_node).value();
    REQUIRE(A.insert_or_assign_edge(Edge::create<in_edge_type>(room_id, y_id)));

    auto synced = [&](DSRGraph& G) {
        auto r = G.get_node("room_4");
        return r.has_value() and G.get_node("x_new_target").has_value()
           and G.get_edge(room_id, y_id, "in").has_value()
           and G.get_attrib_by_name<level_att>(r.value()) == std::optional<int>{7};
    };
    REQUIRE(wait_for([&] { return synced(A) and synced(B) and synced(C); }));

    // ---- concurrent section: no event processing between these calls ----
    REQUIRE(C.insert_or_assign_edge(Edge::create<in_edge_type>(room_id, x_id)));
    REQUIRE(C.delete_edge(room_id, y_id, "in"));
    {
        auto rc = C.get_node(room_id);
        REQUIRE(rc.has_value());
        C.add_or_modify_attrib_local<level_att>(rc.value(), 11);
        REQUIRE(C.update_node(rc.value()));
    }
    {
        auto ra = A.get_node(room_id);
        REQUIRE(ra.has_value());
        REQUIRE(A.get_edge(room_id, y_id, "in").has_value());      // A really has NOT seen C's edits
        REQUIRE_FALSE(A.get_edge(room_id, x_id, "in").has_value());
        ra->name("room_2");
        REQUIRE(A.update_node(ra.value()));
    }
    // ---- end concurrent section ----

    auto converged = [&](DSRGraph& G) {
        auto r = G.get_node(room_id);
        return r.has_value() and r->name() == "room_2"
           and G.get_edge(room_id, x_id, "in").has_value()
           and not G.get_edge(room_id, y_id, "in").has_value()
           and G.get_attrib_by_name<level_att>(r.value()) == std::optional<int>{11};
    };
    wait_for([&] { return converged(A) and converged(B) and converged(C); });
    // settle: give any late (wrong) delta the chance to arrive and break convergence
    wait_for([] { return false; }, 1000ms);

    for (auto [role, G] : {std::pair{"A (renamer)", &A}, std::pair{"B (bystander)", &B}, std::pair{"C (concurrent editor)", &C}})
    {
        CAPTURE(role);
        auto r = G->get_node(room_id);
        REQUIRE(r.has_value());
        CHECK(r->name() == "room_2");
        CHECK_FALSE(G->get_node("room_4").has_value());
        CHECK(G->get_edge(room_id, x_id, "in").has_value());          // C's added edge survives
        CHECK_FALSE(G->get_edge(room_id, y_id, "in").has_value());    // C's deleted edge stays deleted
        CHECK(G->get_attrib_by_name<level_att>(r.value()) == std::optional<int>{11});  // C's attr survives

        // the host's edge cache maps agree with the node's fano
        std::set<std::pair<uint64_t, std::string>> fano_keys;
        for (const auto& [k, e] : r->fano()) fano_keys.emplace(k.first, k.second);
        CHECK(DSRGraphTestAccess::cached_out_edges(*G, room_id) == fano_keys);
        CHECK(DSRGraphTestAccess::cached_in_edges(*G, x_id).contains({room_id, "in"}));
        CHECK_FALSE(DSRGraphTestAccess::cached_in_edges(*G, y_id).contains({room_id, "in"}));
    }
}
