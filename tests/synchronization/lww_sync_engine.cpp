#include <catch2/catch_test_macros.hpp>

#include "dsr/api/dsr_api.h"
#include "dsr/api/dsr_lww_sync_engine.h"
#include "dsr/core/types/type_checking/dsr_attr_name.h"
#include "dsr/core/types/type_checking/dsr_edge_type.h"
#include "dsr/core/types/type_checking/dsr_node_type.h"

using namespace DSR;

namespace {

struct FakeSyncHost final : SyncEngineHost
{
    uint32_t agent{17};
    SyncMode mode{SyncMode::LWW};
    bool copy{false};
    std::unordered_map<uint64_t, Node> nodes;
    std::unordered_map<std::tuple<uint64_t, uint64_t, std::string>, Edge, hash_tuple> edges;

    uint32_t local_agent_id() const override { return agent; }
    SyncMode local_sync_mode() const override { return mode; }
    bool is_copy_graph() const override { return copy; }

    void update_maps_node_insert(const Node& node) override { nodes[node.id()] = node; }

    void update_maps_node_delete(uint64_t id, const std::optional<Node>&) override
    {
        nodes.erase(id);
        for (auto it = edges.begin(); it != edges.end(); ) {
            if (std::get<0>(it->first) == id || std::get<1>(it->first) == id) {
                it = edges.erase(it);
            } else {
                ++it;
            }
        }
    }

    void update_maps_edge_insert(uint64_t from, uint64_t to, const std::string& type) override
    {
        edges[std::tuple{from, to, type}] = Edge(to, from, type, {}, agent);
    }

    void update_maps_edge_delete(uint64_t from, uint64_t to, const std::string& type) override
    {
        edges.erase(std::tuple{from, to, type});
    }
};

Node make_robot(uint64_t id, std::string name, int level)
{
    auto node = Node::create<robot_node_type>(name);
    node.id(id);
    node.agent_id(17);
    node.attrs()["level"] = Attribute(level, 1, 17);
    return node;
}

} // namespace

TEST_CASE("LWW tombstones suppress older remote node recreation", "[LWW][ENGINE]")
{
    FakeSyncHost host;
    LWWSyncEngine engine(host);

    REQUIRE(engine.insert_node_local(make_robot(1, "robot_1", 1)).applied);
    auto deleted = engine.delete_node_local(1);
    REQUIRE(deleted.applied);
    REQUIRE_FALSE(engine.get_node(1).has_value());
    REQUIRE(engine.node_tombstone(1).has_value());

    LWWNodeMsg older_recreate;
    older_recreate.id = 1;
    older_recreate.type = robot_node_type::attr_name;
    older_recreate.name = "robot_1";
    older_recreate.agent_id = 3;
    older_recreate.timestamp = deleted.deleted_node->attrs().at("level").timestamp();
    older_recreate.protocol_version = DSR_PROTOCOL_VERSION;
    older_recreate.sync_mode = sync_mode_wire_value(SyncMode::LWW);
    older_recreate.attrs.emplace("level", Attribute(2, older_recreate.timestamp, older_recreate.agent_id));
    engine.apply_remote_node_delta(NodeDeltaMessage{older_recreate});

    REQUIRE_FALSE(engine.get_node(1).has_value());
}

TEST_CASE("LWW newer remote node recreation beats tombstone", "[LWW][ENGINE]")
{
    FakeSyncHost host;
    LWWSyncEngine engine(host);

    REQUIRE(engine.insert_node_local(make_robot(2, "robot_2", 1)).applied);
    REQUIRE(engine.delete_node_local(2).applied);
    auto tombstone = engine.node_tombstone(2);
    REQUIRE(tombstone.has_value());

    LWWNodeMsg newer_recreate;
    newer_recreate.id = 2;
    newer_recreate.type = robot_node_type::attr_name;
    newer_recreate.name = "robot_2b";
    newer_recreate.agent_id = 8;
    newer_recreate.timestamp = tombstone->version.timestamp + 1;
    newer_recreate.deleted = false;
    newer_recreate.protocol_version = DSR_PROTOCOL_VERSION;
    newer_recreate.sync_mode = sync_mode_wire_value(SyncMode::LWW);
    newer_recreate.attrs.emplace("level", Attribute(9, newer_recreate.timestamp, newer_recreate.agent_id));
    engine.apply_remote_node_delta(NodeDeltaMessage{newer_recreate});

    auto node = engine.get_node(2);
    REQUIRE(node.has_value());
    REQUIRE(node->name() == "robot_2b");
    REQUIRE(node->attrs().at("level").dec() == 9);
}

TEST_CASE("LWW update_node keeps full replacement API semantics", "[LWW][ENGINE]")
{
    FakeSyncHost host;
    LWWSyncEngine engine(host);

    auto node = make_robot(3, "robot_3", 1);
    node.attrs()["parent"] = Attribute(7, 1, 17);
    REQUIRE(engine.insert_node_local(std::move(node)).applied);

    auto replacement = make_robot(3, "robot_3", 5);
    auto effect = engine.update_node_local(std::move(replacement));
    REQUIRE(effect.applied);

    auto stored = engine.get_node(3);
    REQUIRE(stored.has_value());
    REQUIRE(stored->attrs().contains("level"));
    REQUIRE_FALSE(stored->attrs().contains("parent"));
}

TEST_CASE("LWW edge tombstones allow newer recreation", "[LWW][ENGINE]")
{
    FakeSyncHost host;
    LWWSyncEngine engine(host);

    REQUIRE(engine.insert_node_local(make_robot(10, "robot_a", 1)).applied);
    REQUIRE(engine.insert_node_local(make_robot(11, "robot_b", 1)).applied);

    auto edge = Edge::create<RT_edge_type>(10, 11);
    edge.attrs()["weight"] = Attribute(1, 1, 17);
    REQUIRE(engine.insert_or_assign_edge_local(std::move(edge)).applied);
    REQUIRE(engine.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());

    REQUIRE(engine.delete_edge_local(10, 11, std::string(RT_edge_type::attr_name)).applied);
    REQUIRE_FALSE(engine.get_edge(10, 11, std::string(RT_edge_type::attr_name)).has_value());
    auto tombstone = engine.edge_tombstone(10, 11, std::string(RT_edge_type::attr_name));
    REQUIRE(tombstone.has_value());

    LWWEdgeMsg newer_edge;
    newer_edge.from = 10;
    newer_edge.to = 11;
    newer_edge.type = RT_edge_type::attr_name;
    newer_edge.agent_id = 22;
    newer_edge.timestamp = tombstone->version.timestamp + 1;
    newer_edge.deleted = false;
    newer_edge.protocol_version = DSR_PROTOCOL_VERSION;
    newer_edge.sync_mode = sync_mode_wire_value(SyncMode::LWW);
    newer_edge.attrs.emplace("weight", Attribute(4, newer_edge.timestamp, newer_edge.agent_id));
    engine.apply_remote_edge_delta(EdgeDeltaMessage{newer_edge});

    auto stored = engine.get_edge(10, 11, std::string(RT_edge_type::attr_name));
    REQUIRE(stored.has_value());
    REQUIRE(stored->attrs().at("weight").dec() == 4);
}

TEST_CASE("DSRGraph LWW mode supports local core mutations without DDS", "[LWW][GRAPH]")
{
    GraphSettings settings;
    settings.agent_id = 31;
    settings.graph_name = "lww_local_graph";
    settings.sync_mode = SyncMode::LWW;
    settings.same_host = true;

    DSRGraph graph(settings);

    auto parent = make_robot(100, "parent", 1);
    auto child = make_robot(101, "child", 2);

    REQUIRE(graph.insert_node_with_id(parent).has_value());
    REQUIRE(graph.insert_node_with_id(child).has_value());
    REQUIRE(graph.size() == 2);

    auto edge = Edge::create<RT_edge_type>(100, 101);
    edge.attrs()["weight"] = Attribute(3, 1, settings.agent_id);
    REQUIRE(graph.insert_or_assign_edge(edge));

    auto stored_parent = graph.get_node(100);
    REQUIRE(stored_parent.has_value());
    REQUIRE(stored_parent->name() == "parent");

    auto stored_edge = graph.get_edge(100, 101, std::string(RT_edge_type::attr_name));
    REQUIRE(stored_edge.has_value());
    REQUIRE(stored_edge->attrs().at("weight").dec() == 3);

    auto replacement = make_robot(100, "parent", 7);
    REQUIRE(graph.update_node(replacement));
    stored_parent = graph.get_node(100);
    REQUIRE(stored_parent.has_value());
    REQUIRE(stored_parent->attrs().at("level").dec() == 7);

    REQUIRE(graph.delete_edge(100, 101, std::string(RT_edge_type::attr_name)));
    REQUIRE_FALSE(graph.get_edge(100, 101, std::string(RT_edge_type::attr_name)).has_value());

    REQUIRE(graph.delete_node(100));
    REQUIRE_FALSE(graph.get_node(100).has_value());
    REQUIRE(graph.size() == 1);
}
