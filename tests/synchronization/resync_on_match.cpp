//
// Resync on match: what an agent writes BEFORE its writers have matched a running peer's readers is
// never delivered to that peer (the writers are RELIABLE + VOLATILE). The peer must still end up
// with it: when the pairing completes, the writer pushes its full graph to that peer.
//
// The loss needs a write that happens within the few milliseconds discovery takes. An agent that
// JOINS spends more than a second inside its constructor (the full-graph request), by which time
// every writer has matched, so a joiner cannot show it from a single thread. An agent that loads the
// graph from a FILE returns from its constructor at once: its first insert is written to a DSR_NODE
// writer that no peer has matched yet. That is the same loss, made repeatable.
//
#include "dsr/api/dsr_api.h"
#include "../utils.h"
#include <atomic>
#include <cstdlib>
#include <memory>
#include <thread>
#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QEventLoop>
#include <QtCore/QObject>

#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators.hpp"

#include "dsr/core/types/type_checking/dsr_edge_type.h"
#include "dsr/core/types/type_checking/dsr_node_type.h"
#include "graph_test_access.h"

using namespace DSR;
using namespace std::chrono_literals;

namespace {

constexpr int8_t kDomain = 78;  // private test domain, away from the agents' 0, media 7 and rename's 77

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
        std::this_thread::sleep_for(20ms);
    }
    qt.process();
    return predicate();
}

void settle(std::chrono::milliseconds time)
{
    wait_for([] { return false; }, time);
}

uint32_t fresh_agent_id()
{
    static uint32_t next = static_cast<uint32_t>(rand() % 400 + 3500);
    return next++;
}

}  // namespace

TEST_CASE("A node inserted before the writer matched a running peer still reaches that peer", "[SYNCHRONIZATION][RESYNC][DDS]")
{
    ScopedQtApp qt;
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    CAPTURE(sync_mode_label(sync_mode));
    auto ctx = make_empty_config_file();

    // R is up and running, with every reader in place.
    DSRGraph R(make_test_graph_settings(random_string(10), fresh_agent_id(), ctx, true, kDomain, SignalMode::QT, sync_mode));
    settle(300ms);

    std::atomic<int> signals_for_early_node{0};
    std::atomic<uint64_t> early_id{0};
    QObject::connect(&R, &DSRGraph::update_node_signal, &R,
                     [&](uint64_t id, const std::string&, DSR::SignalInfo) {
                         if (id == early_id.load()) ++signals_for_early_node;
                     });

    // W starts and inserts at once: its DSR_NODE writer has not matched R's reader yet.
    DSRGraph W(make_test_graph_settings(random_string(11), fresh_agent_id(), ctx, true, kDomain, SignalMode::QT, sync_mode));
    auto early = Node::create<testtype_node_type>("early_node");
    const auto inserted = W.insert_node(early);
    REQUIRE(inserted.has_value());
    early_id = inserted.value();

    const bool reached = wait_for([&] { return R.get_node("early_node").has_value(); });
    if (std::getenv("DSR_RESYNC_TEST_REPORT") != nullptr) {
        // For counting how often the loss shows up over many runs (see RESYNC_ON_MATCH.md).
        std::printf("RESYNC_TEST early_node_reached_peer=%d\n", reached ? 1 : 0);
    }
    REQUIRE(reached);
    REQUIRE(R.get_node("early_node")->id() == inserted.value());

    // R loaded its graph from a file: the push is a re-sync for it, so it must signal the new node.
    REQUIRE(wait_for([&] { return signals_for_early_node.load() > 0; }));

    // From here on the node lives on both sides: a later update travels as an attribute delta and
    // is applied by R instead of being parked for a node it never got.
    auto again = W.get_node("early_node");
    REQUIRE(again.has_value());
    W.add_or_modify_attrib_local<level_att>(again.value(), 5);
    REQUIRE(W.update_node(again.value()));
    REQUIRE(wait_for([&] {
        auto r = R.get_node("early_node");
        return r.has_value() and R.get_attrib_by_name<level_att>(r.value()) == std::optional<int>{5};
    }));
}

TEST_CASE("The join handshake is unchanged by the pushes between peers", "[SYNCHRONIZATION][RESYNC][DDS]")
{
    ScopedQtApp qt;
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    CAPTURE(sync_mode_label(sync_mode));
    auto ctx = make_empty_config_file();

    DSRGraph S(make_test_graph_settings(random_string(10), fresh_agent_id(), ctx, true, kDomain, SignalMode::QT, sync_mode));
    DSRGraph A(make_test_graph_settings(random_string(11), fresh_agent_id(), std::string{}, true, kDomain, SignalMode::QT, sync_mode));
    REQUIRE(wait_for([&] { return A.size() == S.size() and A.get_node("root").has_value(); }));

    // A joiner inserts as soon as its constructor returns.
    DSRGraph B(make_test_graph_settings(random_string(12), fresh_agent_id(), std::string{}, true, kDomain, SignalMode::QT, sync_mode));
    auto node = Node::create<testtype_node_type>("joiner_node");
    const auto inserted = B.insert_node(node);
    REQUIRE(inserted.has_value());
    REQUIRE(wait_for([&] {
        return S.get_node("joiner_node").has_value() and A.get_node("joiner_node").has_value()
           and B.get_node("root").has_value();
    }));
    REQUIRE(S.size() == A.size());
    REQUIRE(S.size() == B.size());

    // A third joiner: in CRDT mode its first import (the server's answer) is silent, and the graphs
    // its peers push to it afterwards hold nothing new, so they are silent too.
    DSRGraph C(make_test_graph_settings(random_string(13), fresh_agent_id(), std::string{}, true, kDomain, SignalMode::QT, sync_mode));
    std::atomic<int> emitted{0};
    QObject::connect(&C, &DSRGraph::update_node_signal, &C,
                     [&](uint64_t, const std::string&, DSR::SignalInfo) { ++emitted; });
    REQUIRE(wait_for([&] { return C.size() == S.size() and C.get_node("joiner_node").has_value(); }));
    settle(1000ms);
    if (sync_mode == SyncMode::CRDT) {
        // (LWW has no silent first import: it signals every node of the initial graph, with or
        // without the pushes.)
        REQUIRE(emitted.load() == 0);
    }
    REQUIRE(C.size() == S.size());
    REQUIRE(A.size() == S.size());
    REQUIRE(B.size() == S.size());
}

// The receiving side, fed by hand: who imports what, and when.
TEST_CASE("An addressed full graph is imported only by its target, and never as the initial graph", "[SYNCHRONIZATION][RESYNC]")
{
    ScopedQtApp qt;
    auto ctx = make_empty_config_file();
    // Two graphs that never see each other (different domains): H only supplies graph content.
    DSRGraph G(make_test_graph_settings(random_string(10), fresh_agent_id(), ctx, true, kDomain));
    DSRGraph H(make_test_graph_settings(random_string(11), fresh_agent_id(), ctx, true, static_cast<int8_t>(kDomain + 1)));
    const auto me = G.get_agent_id();
    const auto other = me + 1000 > 4095 ? me - 1000 : me + 1000;

    auto snapshot_with = [&](const char* name, uint32_t to_id) {
        auto node = Node::create<testtype_node_type>(name);
        REQUIRE(H.insert_node(node).has_value());
        DSR::OrMap sample;
        sample.id = static_cast<int32_t>(H.get_agent_id());
        sample.to_id = to_id;
        sample.m = DSRGraphTestAccess::export_crdt_map(H);
        return sample;
    };
    auto has = [&](const char* name) { qt.process(); return G.get_node(name).has_value(); };

    SECTION("while joining: a push waits for the server's answer")
    {
        auto state = DSRGraphTestAccess::make_join_state(false, true);

        DSRGraphTestAccess::deliver_graph_answer(G, state, snapshot_with("for_someone_else", other));
        REQUIRE_FALSE(state->sync.load());
        REQUIRE(DSRGraphTestAccess::early_pushes(state) == 0);

        DSRGraphTestAccess::deliver_graph_answer(G, state, snapshot_with("pushed_early", me));
        REQUIRE_FALSE(state->sync.load());      // a push does not end the wait for the server
        REQUIRE(DSRGraphTestAccess::early_pushes(state) == 1);
        REQUIRE_FALSE(has("pushed_early"));     // and is not imported before the initial graph

        DSR::OrMap answer;                       // the server's (broadcast) answer
        answer.id = static_cast<int32_t>(other);
        DSRGraphTestAccess::deliver_graph_answer(G, state, std::move(answer));
        REQUIRE(state->sync.load());
        REQUIRE(DSRGraphTestAccess::early_pushes(state) == 0);
        REQUIRE(has("pushed_early"));
        REQUIRE(has("for_someone_else"));        // it was in H's graph by the time of the kept push
    }

    SECTION("while joining: the duplicate-id answer is still recognised")
    {
        auto state = DSRGraphTestAccess::make_join_state(false, true);
        DSR::OrMap repeated;
        repeated.id = -1;
        repeated.to_id = other;
        DSRGraphTestAccess::deliver_graph_answer(G, state, repeated);
        REQUIRE_FALSE(state->repeated.load());
        repeated.to_id = me;
        DSRGraphTestAccess::deliver_graph_answer(G, state, repeated);
        REQUIRE(state->repeated.load());
        REQUIRE_FALSE(state->sync.load());
    }

    SECTION("once joined: only what is addressed here, or the server's answer")
    {
        auto state = DSRGraphTestAccess::make_join_state(true, true);
        DSRGraphTestAccess::deliver_graph_answer(G, state, snapshot_with("not_mine", other));
        REQUIRE_FALSE(has("not_mine"));
        auto own = snapshot_with("my_own_echo", me);
        own.id = static_cast<int32_t>(me);
        DSRGraphTestAccess::deliver_graph_answer(G, state, std::move(own));
        REQUIRE_FALSE(has("my_own_echo"));
        DSRGraphTestAccess::deliver_graph_answer(G, state, snapshot_with("mine", me));
        REQUIRE(has("mine"));
        DSRGraphTestAccess::deliver_graph_answer(G, state, snapshot_with("from_server", 0));
        REQUIRE(has("from_server"));
    }

    SECTION("an agent that loaded the graph from a file takes addressed pushes only")
    {
        auto state = DSRGraphTestAccess::make_join_state(true, false);
        DSRGraphTestAccess::deliver_graph_answer(G, state, snapshot_with("another_servers_answer", 0));
        REQUIRE_FALSE(has("another_servers_answer"));
        DSRGraphTestAccess::deliver_graph_answer(G, state, snapshot_with("addressed", me));
        REQUIRE(has("addressed"));
    }
}
