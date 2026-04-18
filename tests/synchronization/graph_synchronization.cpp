//
// Created by jc on 5/11/24.
//
#include "dsr/api/dsr_api.h"
#include "../utils.h"
#include <thread>

#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators.hpp"

#include "dsr/core/types/internal_types.h"

using namespace DSR;
using namespace std::chrono_literals;

namespace DSR
{
class DSRGraphTestAccess
{
public:
    static std::map<uint64_t, DSR::MvregNodeMsg> Map(DSRGraph& graph)
    {
        return graph.Map();
    }

    static void join_delta_node(DSRGraph& graph, DSR::MvregNodeMsg&& delta)
    {
        graph.join_delta_node(std::move(delta));
    }

    static void join_full_graph(DSRGraph& graph, DSR::OrMap&& full_graph)
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

TEST_CASE("Same-process agents discover each other and exchange updates", "[SYNCHRONIZATION][GRAPH][REGRESSION][DDS]")
{
    const auto same_host = GENERATE(true, false);
    auto ctx = make_edge_config_file();
    auto id1 = static_cast<uint32_t>(rand() % 1000 + 1000);
    auto id2 = id1 + 1;

    DSRGraph loader(random_string(10), id1, ctx, same_host);
    DSRGraph follower(random_string(11), id2, std::string{}, same_host);

    auto wait_until = [](auto&& predicate, std::chrono::milliseconds timeout = 2000ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
                return true;
            std::this_thread::sleep_for(50ms);
        }
        return predicate();
    };

    REQUIRE(wait_until([&] { return follower.size() == loader.size(); }));
    REQUIRE(wait_until([&] { return !loader.get_connected_agents().empty(); }));
    REQUIRE(wait_until([&] { return !follower.get_connected_agents().empty(); }));

    auto root_loader = loader.get_node("root");
    REQUIRE(root_loader.has_value());
    root_loader->attrs()["same_process_loader_" + std::to_string(same_host)] =
        Attribute(std::string("loader"), get_unix_timestamp(), loader.get_agent_id());
    REQUIRE(loader.update_node(root_loader.value()));

    REQUIRE(wait_until([&] {
        auto root_follower = follower.get_node("root");
        return root_follower.has_value() &&
               root_follower->attrs().contains("same_process_loader_" + std::to_string(same_host));
    }));

    auto root_follower = follower.get_node("root");
    REQUIRE(root_follower.has_value());
    root_follower->attrs()["same_process_follower_" + std::to_string(same_host)] =
        Attribute(std::string("follower"), get_unix_timestamp(), follower.get_agent_id());
    REQUIRE(follower.update_node(root_follower.value()));

    REQUIRE(wait_until([&] {
        auto updated_root_loader = loader.get_node("root");
        return updated_root_loader.has_value() &&
               updated_root_loader->attrs().contains("same_process_follower_" + std::to_string(same_host));
    }));
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

    DSR::OrMap full_graph;
    full_graph.id = graph.get_agent_id();
    full_graph.to_id = graph.get_agent_id();
    full_graph.m = DSRGraphTestAccess::Map(graph);

    REQUIRE(graph.delete_node(node.id()));
    REQUIRE(graph.size() == initial_size);
    REQUIRE_FALSE(graph.get_node(node.id()).has_value());

    DSRGraphTestAccess::join_full_graph(graph, std::move(full_graph));

    REQUIRE(graph.size() == initial_size);
    REQUIRE_FALSE(graph.get_node(node.id()).has_value());
}

TEST_CASE("Full graph join rejects incompatible protocol versions", "[SYNCHRONIZATION][GRAPH][VERSION]")
{
    auto ctx = make_empty_config_file();
    DSRGraph sender(random_string(10), static_cast<uint32_t>(rand() % 2000 + 1000), ctx);
    DSRGraph receiver(random_string(10), static_cast<uint32_t>(rand() % 1000 + 3000), ctx);

    auto node = Node::create<testtype_node_type>("version_mismatch_node");
    node.id(2000);
    node.agent_id(sender.get_agent_id());

    REQUIRE(sender.insert_node_with_id(node).has_value());
    REQUIRE_FALSE(receiver.get_node(node.id()).has_value());

    DSR::OrMap full_graph;
    full_graph.id = static_cast<int32_t>(sender.get_agent_id());
    full_graph.to_id = receiver.get_agent_id();
    full_graph.protocol_version = DSR::DSR_PROTOCOL_VERSION + 1;
    full_graph.m = DSRGraphTestAccess::Map(sender);

    DSRGraphTestAccess::join_full_graph(receiver, std::move(full_graph));

    REQUIRE_FALSE(receiver.get_node(node.id()).has_value());
}

TEST_CASE("Node delta join rejects incompatible protocol versions", "[SYNCHRONIZATION][GRAPH][VERSION]")
{
    auto ctx = make_empty_config_file();
    DSRGraph sender(random_string(10), static_cast<uint32_t>(rand() % 2000 + 1000), ctx);
    DSRGraph receiver(random_string(10), static_cast<uint32_t>(rand() % 1000 + 3000), ctx);

    auto node = Node::create<testtype_node_type>("delta_version_mismatch_node");
    node.id(3000);
    node.agent_id(sender.get_agent_id());

    REQUIRE(sender.insert_node_with_id(node).has_value());
    REQUIRE_FALSE(receiver.get_node(node.id()).has_value());

    auto map = DSRGraphTestAccess::Map(sender);
    auto it = map.find(node.id());
    REQUIRE(it != map.end());

    auto delta = std::move(it->second);
    delta.protocol_version = DSR::DSR_PROTOCOL_VERSION + 1;
    DSRGraphTestAccess::join_delta_node(receiver, std::move(delta));

    REQUIRE_FALSE(receiver.get_node(node.id()).has_value());
}
