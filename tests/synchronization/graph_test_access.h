#pragma once
// Test-only access to DSRGraph internals (DSRGraph declares `friend class DSRGraphTestAccess`).
// Shared by every TU that needs it, so there is ONE definition of the class (ODR).
#include "dsr/api/dsr_api.h"
#include "catch2/catch_test_macros.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <utility>

namespace DSR
{
class DSRGraphTestAccess
{
public:
    static std::map<uint64_t, DSR::MvregNodeMsg> export_crdt_map(DSRGraph& graph)
    {
        auto full_graph = graph.engine_->export_full_graph();
        auto* payload = std::get_if<DSR::OrMap>(&full_graph);
        REQUIRE(payload != nullptr);
        return payload->m;
    }

    static void apply_node_delta(DSRGraph& graph, DSR::MvregNodeMsg&& delta)
    {
        graph.engine_->apply_remote_node_delta(DSR::NodeDeltaMessage{std::move(delta)});
    }

    static void import_full_graph(DSRGraph& graph, DSR::OrMap&& full_graph)
    {
        graph.engine_->import_full_graph(DSR::FullGraphMessage{std::move(full_graph)});
    }
    // The GRAPH_ANSWER reader callback and its state, to feed it samples without a network.
    using JoinState = DSRGraph::FullGraphJoinState;
    static std::shared_ptr<JoinState> make_join_state(bool synced, bool accept_server_answers)
    {
        auto state = std::make_shared<JoinState>();
        state->sync = synced;
        state->accept_server_answers = accept_server_answers;
        return state;
    }
    static void deliver_graph_answer(DSRGraph& graph, const std::shared_ptr<JoinState>& state, DSR::OrMap sample)
    {
        auto callback = graph.make_fullgraph_request_functor_impl<DSR::OrMap>("GRAPH_ANSWER", state);
        callback(std::move(sample), DSR::Transport::ReceivedSampleInfo{.valid_data = true});
    }
    static size_t early_pushes(const std::shared_ptr<JoinState>& state)
    {
        std::lock_guard<std::mutex> lck(state->mtx);
        return state->early_pushes.size();
    }
    // The host's edge cache maps for one node, to check they agree with the node's fano.
    static std::set<std::pair<uint64_t, std::string>> cached_out_edges(DSRGraph& graph, uint64_t from)
    {
        std::shared_lock<std::shared_mutex> lck(graph._mutex_cache_maps);
        std::set<std::pair<uint64_t, std::string>> out;
        for (const auto& [key, types] : graph.edges)
            if (key.first == from)
                for (const auto& t : types) out.emplace(key.second, t);
        return out;
    }

    static std::set<std::pair<uint64_t, std::string>> cached_in_edges(DSRGraph& graph, uint64_t to)
    {
        std::shared_lock<std::shared_mutex> lck(graph._mutex_cache_maps);
        std::set<std::pair<uint64_t, std::string>> in;
        if (auto it = graph.to_edges.find(to); it != graph.to_edges.end())
            for (const auto& [from, t] : it->second) in.emplace(from, t);
        return in;
    }
};
}
