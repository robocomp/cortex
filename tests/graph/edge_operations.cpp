//
// Created by jc on 5/11/24.
//

#include "catch2/catch_test_macros.hpp"

#include "dsr/core/types/type_checking/dsr_edge_type.h"
#include "dsr/core/types/user_types.h"

#include "dsr/api/dsr_api.h"
#include "../utils.h"
#include <optional>


using namespace DSR;



TEST_CASE("Graph edge operations", "[EDGE]") {

    auto filename = make_empty_config_file();
    DSRGraph G(random_string(10), rand() % 1200, filename);

    SECTION("Get an edge that does not exists by id") {
        std::optional<Edge> e_id = G.get_edge(random_number(), random_number(), random_string());
        REQUIRE_FALSE(e_id.has_value());
    }

    SECTION("Get an edge that does not exists by name") {
        std::optional<Edge> e_name = G.get_edge(random_string(), random_string(), random_string());
        REQUIRE_FALSE(e_name.has_value());
    }

    SECTION("Insert a new edge and get it by name and id") {
        auto n = Node::create<testtype_node_type>();
        auto id1 = G.insert_node(n);
        REQUIRE(id1.has_value());

        auto n2 = Node::create<testtype_node_type>();
        auto id2 = G.insert_node(n2);
        REQUIRE(id2.has_value());

        auto e = Edge::create<in_edge_type>(id1.value(), id2.value());
        bool r  = G.insert_or_assign_edge(e);
        REQUIRE(r);

        std::optional<Edge> e_id = G.get_edge(id1.value(), id2.value(), std::string(in_edge_type::attr_name));
        REQUIRE(e_id.has_value());

        auto name_ori = G.get_name_from_id(*id1);
        REQUIRE(name_ori.has_value());

        auto name_dst = G.get_name_from_id(*id2);
        REQUIRE(name_dst.has_value());


        std::optional<Edge> e_name = G.get_edge(*name_ori, *name_dst, std::string(in_edge_type::attr_name));
        REQUIRE(e_name.has_value());

        auto new_n = G.get_node(*name_ori);
        REQUIRE(new_n.has_value());
        std::optional<Edge> e_node = G.get_edge(*new_n, *name_dst, std::string(in_edge_type::attr_name));
        REQUIRE(e_name.has_value());
    }

    SECTION("Update existing edge") {
        auto n = Node::create<testtype_node_type>();
        auto id1 = G.insert_node(n);
        REQUIRE(id1.has_value());

        auto n2 = Node::create<testtype_node_type>();
        auto id2 = G.insert_node(n2);
        REQUIRE(id2.has_value());

        auto e = Edge::create<in_edge_type>(id1.value(), id2.value());
        bool r  = G.insert_or_assign_edge(e);
        REQUIRE(r);

        std::vector<Edge> edges = G.get_edges_by_type(std::string(in_edge_type::attr_name));
        REQUIRE(edges.size() > 0);
        e = edges.at(0);

        G.add_attrib_local<name_att>(e,  random_string());
        r = G.insert_or_assign_edge(e);
        REQUIRE(r);
    }

    SECTION("Delete existing edge by id and name") {
auto n = Node::create<testtype_node_type>();
        auto id1 = G.insert_node(n);
        REQUIRE(id1.has_value());

        auto n2 = Node::create<testtype_node_type>();
        auto id2 = G.insert_node(n2);
        REQUIRE(id2.has_value());

        auto e = Edge::create<in_edge_type>(id1.value(), id2.value());
        bool r  = G.insert_or_assign_edge(e);
        REQUIRE(r);

        std::vector<Edge> edges = G.get_edges_by_type(std::string(in_edge_type::attr_name));
        REQUIRE(edges.size() == 1);
        e = edges.at(0);
        r = G.delete_edge(e.from(), e.to(), std::string(in_edge_type::attr_name));
        REQUIRE(r);
        REQUIRE(G.get_edge(e.from(), e.to(), std::string(in_edge_type::attr_name)) == std::nullopt);

        //Reinsert it.
        bool r2  = G.insert_or_assign_edge(e);
        REQUIRE(r2);

        auto name_ori = G.get_name_from_id(e.from());
        REQUIRE(name_ori.has_value());

        auto name_dst = G.get_name_from_id(e.to());
        REQUIRE(name_dst.has_value());


        r = G.delete_edge(*name_ori, *name_dst, std::string(in_edge_type::attr_name));
        REQUIRE(r);
        REQUIRE(G.get_edge(*name_ori, *name_dst, std::string(in_edge_type::attr_name)) == std::nullopt);

    }


    SECTION("Delete an edge that does not exists") {
        std::vector<Edge> edges = G.get_edges_by_type(std::string(in_edge_type::attr_name));
        REQUIRE(edges.size() == 0);
        bool r = G.delete_edge(random_number(),random_number(), std::string(in_edge_type::attr_name));
        REQUIRE_FALSE(r);
    }

    SECTION("Deleting one edge type between a pair leaves other types intact") {
        auto n1 = Node::create<testtype_node_type>();
        auto id1 = G.insert_node(n1);
        REQUIRE(id1.has_value());

        auto n2 = Node::create<testtype_node_type>();
        auto id2 = G.insert_node(n2);
        REQUIRE(id2.has_value());

        // Two different edge types between the same node pair
        auto e_in    = Edge::create<in_edge_type>(*id1, *id2);
        auto e_knows = Edge::create<knows_edge_type>(*id1, *id2);
        REQUIRE(G.insert_or_assign_edge(e_in));
        REQUIRE(G.insert_or_assign_edge(e_knows));

        // Delete only the "in" edge
        REQUIRE(G.delete_edge(*id1, *id2, std::string(in_edge_type::attr_name)));

        // "in" must be gone
        REQUIRE_FALSE(G.get_edge(*id1, *id2, std::string(in_edge_type::attr_name)).has_value());

        // "knows" must still be visible
        REQUIRE(G.get_edge(*id1, *id2, std::string(knows_edge_type::attr_name)).has_value());
        auto remaining = G.get_edges_by_type(std::string(knows_edge_type::attr_name));
        REQUIRE(remaining.size() == 1);
    }

}


TEST_CASE("Edge creation", 
          "[edge]") {


    SECTION("Runtime set of an edge type") {
        Edge e;
        REQUIRE_NOTHROW(e.type("in"));
        REQUIRE_THROWS(e.type("invalidtype"));
    }
}