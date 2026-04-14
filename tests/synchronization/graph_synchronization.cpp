//
// Created by jc on 5/11/24.
//
#include "dsr/api/dsr_api.h"
#include "../utils.h"
#include <thread>

#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators.hpp"

#include "dsr/core/topics/IDLGraph.hpp"

using namespace DSR;
using namespace std::chrono_literals;

namespace DSR
{
class DSRGraphTestAccess
{
public:
    static std::map<uint64_t, IDL::MvregNode> Map(DSRGraph& graph)
    {
        return graph.Map();
    }

    static void join_full_graph(DSRGraph& graph, IDL::OrMap&& full_graph)
    {
        graph.join_full_graph(std::move(full_graph));
    }
};
}

TEST_CASE("Connect and receive the graph from other agent", "[SYNCHRONIZATION][GRAPH]"){


    auto filename = GENERATE(make_edge_config_file, make_empty_config_file);
    auto ctx = filename();
    auto id1 = rand() % 1000;
    auto id2 = id1 + 1;
    DSRGraph G(random_string(10), id1, ctx);
    DSRGraph G2(random_string(11), id2);
    std::this_thread::sleep_for(200ms);
    REQUIRE(G2.size() == G.size());
    
}

TEST_CASE("Full graph join does not leave empty node registers after local deletion", "[SYNCHRONIZATION][GRAPH][REGRESSION]")
{
    auto ctx = make_empty_config_file();
    DSRGraph graph(random_string(10), static_cast<uint32_t>(rand() % 4000), ctx);
    const auto initial_size = graph.size();

    auto node = Node::create<testtype_node_type>("regression_node");
    node.id(1000);
    node.agent_id(graph.get_agent_id());

    REQUIRE(graph.insert_node_with_id(node).has_value());
    REQUIRE(graph.size() == initial_size + 1);

    IDL::OrMap full_graph;
    full_graph.id(graph.get_agent_id());
    full_graph.to_id(graph.get_agent_id());
    full_graph.m(DSRGraphTestAccess::Map(graph));

    REQUIRE(graph.delete_node(node.id()));
    REQUIRE(graph.size() == initial_size);
    REQUIRE_FALSE(graph.get_node(node.id()).has_value());

    DSRGraphTestAccess::join_full_graph(graph, std::move(full_graph));

    REQUIRE(graph.size() == initial_size);
    REQUIRE_FALSE(graph.get_node(node.id()).has_value());
}
