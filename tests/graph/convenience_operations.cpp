
#include "dsr/api/dsr_api.h"
#include "../utils.h"
#include <optional>

#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators.hpp"
#include "dsr/core/types/type_checking/dsr_node_type.h"

using namespace DSR;




TEST_CASE("Maps operations", "[CONVENIENCE METHODS]") {
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    CAPTURE(sync_mode_label(sync_mode));

    auto filename = make_edge_config_file();
    DSRGraph G(make_test_graph_settings(random_string(10), rand() % 1200, filename, true, 0, SignalMode::QT, sync_mode));


    SECTION("Get id from a name and a name from an id") {
        auto file_name = random_string();
        auto n = Node::create<testtype_node_type>(file_name);
        std::optional<uint64_t> r = G.insert_node(n);
        REQUIRE(r.has_value());

        std::optional<uint64_t> id = G.get_id_from_name(file_name);
        REQUIRE(id.has_value());
        REQUIRE(id.value() == r.value());

        std::optional<std::string> name = G.get_name_from_id(r.value());
        REQUIRE(name.has_value());
        REQUIRE(name.value() == file_name);
    }


    SECTION("Get id from a name that does not exist") {
        std::optional<uint64_t> id = G.get_id_from_name(random_string());
        REQUIRE(!id.has_value());
    }


    SECTION("Get name from an id that does not exist") {
        std::optional<std::string> name = G.get_name_from_id(random_number() + 101);
        REQUIRE(!name.has_value());
    }

    SECTION("Get nodes by type") {
        auto n = Node::create<testtype_node_type>();
        std::optional<uint64_t> r = G.insert_node(n);
        REQUIRE(r.has_value());

        std::vector<Node> ve = G.get_nodes_by_type(std::string(testtype_node_type::attr_name));
        REQUIRE(!ve.empty());
        ve = G.get_nodes_by_type(random_string());
        REQUIRE(ve.empty());

    }
    SECTION("Get edges by type") {
        std::vector<Edge> ve = G.get_edges_by_type("RT");
        REQUIRE(!ve.empty());
        REQUIRE(std::all_of(ve.begin(), ve.end(),  [](auto& a){ return  "RT" == a.type(); }));
        ve = G.get_edges_by_type(random_string());
        REQUIRE(ve.empty());
    }
    SECTION("Get edges to a node (id)") {
        std::vector<Edge> ve = G.get_edges_to_id(100);
        REQUIRE(ve.empty());
        ve = G.get_edges_to_id(200);
        REQUIRE(ve.size() == 1);
    }

    SECTION("Get edges from a node") {
        std::optional<std::map<std::pair<uint64_t , std::string>, DSR::Edge>> ve = G.get_edges(100);
        REQUIRE(ve.has_value() == true);
        REQUIRE(ve->size() == 1);
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        REQUIRE(n->fano() == ve.value());
    }

    SECTION("Get edges from a node that does not exist") {
        std::optional<std::map<std::pair<uint64_t, std::string>, DSR::Edge>> ve = G.get_edges(random_number());
        REQUIRE(!ve.has_value());
    }

    SECTION("Get edges from a node that has no edges") {
        std::optional<std::map<std::pair<uint64_t, std::string>, DSR::Edge>> ve = G.get_edges(200);
        REQUIRE(ve.has_value());
        REQUIRE(ve->empty());
    }

}



TEST_CASE("Convenience methods", "[CONVENIENCE METHODS]") {
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    CAPTURE(sync_mode_label(sync_mode));

    auto filename = make_edge_config_file();
    DSRGraph G(make_test_graph_settings(random_string(10), rand() % 1200, filename, true, 0, SignalMode::QT, sync_mode));


    SECTION("Get node level") {
        std::optional<Node> n = G.get_node("root");
        REQUIRE(n.has_value());
        std::optional<int> level = G.get_node_level(n.value());
        REQUIRE(level.has_value() == true);
        REQUIRE(level.value() == 0);


        auto in = Node::create<testtype_node_type>();
        auto id = G.insert_node(in);
        REQUIRE(id.has_value());
        n = G.get_node(id.value());
        REQUIRE(n.has_value());
        level = G.get_node_level(n.value());
        REQUIRE(level.has_value() == false);
    }

    SECTION("Get parent id") {
        std::optional<Node> n = G.get_node(150);
        REQUIRE(n.has_value());
        std::optional<uint64_t> id = G.get_parent_id(n.value());
        REQUIRE(id.has_value());
        REQUIRE(id.value() == 100);
    }

    SECTION("Get parent node") {
        std::optional<Node> n = G.get_node(150);
        REQUIRE(n.has_value());
        std::optional<Node> p = G.get_parent_node(n.value());
        REQUIRE(p.has_value());
        std::optional<Node> parent =  G.get_node(100);
        REQUIRE(parent.has_value());
        REQUIRE(p.value() == parent.value());

        std::optional<Node> parent_empty = G.get_parent_node(parent.value());
        REQUIRE_FALSE(parent_empty.has_value());
    }

    SECTION("get_node_root") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        std::optional<Node> n2 = G.get_node_root();
        REQUIRE(n2.has_value());
        REQUIRE(n2.value() == n.value());
    }
}
