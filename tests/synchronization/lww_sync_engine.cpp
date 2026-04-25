#include <catch2/catch_test_macros.hpp>

#include <thread>

#include "../utils.h"
#include "../transport/fake_network.h"
#include "dsr/api/dsr_api.h"
#include "dsr/api/dsr_lww_sync_engine.h"
#include "dsr/core/types/type_checking/dsr_attr_name.h"
#include "dsr/core/types/type_checking/dsr_edge_type.h"
#include "dsr/core/types/type_checking/dsr_node_type.h"

using namespace DSR;
using namespace DSR::Test;
using namespace std::chrono_literals;

namespace {

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

TEST_CASE("LWW local node updates emit attr batches for attr-only changes", "[LWW][ENGINE]")
{
    FakeSyncHost host;
    LWWSyncEngine engine(host);

    auto node = make_robot(4, "robot_4", 1);
    node.attrs()["parent"] = Attribute(7, 1, 17);
    REQUIRE(engine.insert_node_local(std::move(node)).applied);

    auto replacement = make_robot(4, "robot_4", 5);
    auto effect = engine.update_node_local(std::move(replacement));

    REQUIRE(effect.applied);
    REQUIRE_FALSE(effect.node_delta.has_value());
    REQUIRE(effect.node_attr_batch.has_value());
    REQUIRE(effect.changed_attributes == std::vector<std::string>{"level", "parent"});

    const auto* batch = std::get_if<LWWNodeAttrVec>(&*effect.node_attr_batch);
    REQUIRE(batch != nullptr);
    REQUIRE(batch->vec.size() == 2);
    REQUIRE(batch->vec[0].attr_name == "level");
    REQUIRE_FALSE(batch->vec[0].deleted);
    REQUIRE(batch->vec[1].attr_name == "parent");
    REQUIRE(batch->vec[1].deleted);
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

TEST_CASE("LWW local edge updates emit attr batches for attr-only changes", "[LWW][ENGINE]")
{
    FakeSyncHost host;
    LWWSyncEngine engine(host);

    REQUIRE(engine.insert_node_local(make_robot(12, "robot_c", 1)).applied);
    REQUIRE(engine.insert_node_local(make_robot(13, "robot_d", 1)).applied);

    auto edge = Edge::create<RT_edge_type>(12, 13);
    edge.attrs()["weight"] = Attribute(1, 1, 17);
    REQUIRE(engine.insert_or_assign_edge_local(std::move(edge)).applied);

    auto updated = Edge::create<RT_edge_type>(12, 13);
    updated.attrs()["capacity"] = Attribute(9, 1, 17);
    auto effect = engine.insert_or_assign_edge_local(std::move(updated));

    REQUIRE(effect.applied);
    REQUIRE_FALSE(effect.edge_delta.has_value());
    REQUIRE(effect.edge_attr_batch.has_value());
    REQUIRE(effect.changed_attributes == std::vector<std::string>{"capacity", "weight"});

    const auto* batch = std::get_if<LWWEdgeAttrVec>(&*effect.edge_attr_batch);
    REQUIRE(batch != nullptr);
    REQUIRE(batch->vec.size() == 2);
    REQUIRE(batch->vec[0].attr_name == "capacity");
    REQUIRE_FALSE(batch->vec[0].deleted);
    REQUIRE(batch->vec[1].attr_name == "weight");
    REQUIRE(batch->vec[1].deleted);
}

TEST_CASE("Same-process LWW agents synchronize over DDS", "[LWW][DDS]")
{
    auto ctx = make_edge_config_file();

    GraphSettings loader_settings;
    loader_settings.agent_id = static_cast<uint32_t>(rand() % 1000 + 2500);
    loader_settings.graph_name = random_string(10);
    loader_settings.input_file = ctx;
    loader_settings.same_host = true;
    loader_settings.sync_mode = SyncMode::LWW;

    GraphSettings follower_settings = loader_settings;
    follower_settings.agent_id += 1;
    follower_settings.graph_name = random_string(11);
    follower_settings.input_file.clear();

    DSRGraph loader(loader_settings);
    DSRGraph follower(follower_settings);

    auto wait_until = [](auto&& predicate, std::chrono::milliseconds timeout = 3000ms)
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

    auto root_loader = loader.get_node("root");
    REQUIRE(root_loader.has_value());
    root_loader->attrs()["lww_loader_sync"] =
        Attribute(std::string("loader"), get_unix_timestamp(), loader.get_agent_id());
    REQUIRE(loader.update_node(root_loader.value()));

    REQUIRE(wait_until([&] {
        auto root_follower = follower.get_node("root");
        return root_follower.has_value() &&
               root_follower->attrs().contains("lww_loader_sync");
    }));
}
