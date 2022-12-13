//
// Created by jc on 19/11/22.
//

#include <iostream>
#include <optional>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>
#include "dsr/api/dsr_api.h"
#include <string>


using namespace std::literals;
using namespace DSR;

REGISTER_EDGE_TYPE(test_e);
REGISTER_NODE_TYPE(test_n);



REGISTER_TYPE(test_int, int32_t, false)
REGISTER_TYPE(test_float, float, false)
REGISTER_TYPE(test_bool, bool, false)
REGISTER_TYPE(test_uint, uint32_t, false)
REGISTER_TYPE(test_uint64, uint64_t, false)
REGISTER_TYPE(test_string, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(test_vec_byte, std::reference_wrapper<const std::vector<uint8_t>>, false)
REGISTER_TYPE(test_vec_float, std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(test_double, double, false)
REGISTER_TYPE(test_vec_u64, std::reference_wrapper<const std::vector<uint64_t>>, false)
REGISTER_TYPE(test_vec2, std::reference_wrapper<const vec2>, false)
REGISTER_TYPE(test_vec3, std::reference_wrapper<const vec3>, false)
REGISTER_TYPE(test_vec4, std::reference_wrapper<const vec4>, false)
REGISTER_TYPE(test_vec6, std::reference_wrapper<const vec6>, false)
REGISTER_TYPE(test_vec6_no_reference, vec6, false)



class TGraph
{
public:
    static TGraph &get() {
        static TGraph instance;
        return instance;
    }

    TGraph(TGraph const &) = delete;

    void operator=(TGraph const &) = delete;

    std::shared_ptr<DSRGraph> get_G() { return G; }

private:
    TGraph() {
        G = std::make_shared<DSRGraph>(0, "test", 1111, "/home/robocomp/robocomp/components/cortex/tests/testfiles/example.json");
    }

    std::shared_ptr<DSRGraph> G;
};


TEST_CASE("Node operations", "[NODE]") {

    std::shared_ptr<DSRGraph> G = TGraph::get().get_G();

    SECTION("Get a node that does not exists by id")
    {
        std::optional<Node> n_id = G->get_node(3333333);
        REQUIRE_FALSE(n_id.has_value());
    }

    SECTION("Get a node that does not exists by name")
    {
        std::optional<Node> n_name = G->get_node("nop");
        REQUIRE_FALSE(n_name.has_value());
    }

    SECTION("Insert a new node and get it by id and name")
    {
        auto n = Node::create<test_n_node_type>("nodename");
        std::optional<uint64_t> r = G->insert_node(n);
        REQUIRE(r.has_value());
        std::optional<Node> n_id = G->get_node(r.value());
        REQUIRE(n_id.has_value());
        std::optional<Node> n_name = G->get_node("nodename");
        REQUIRE(n_name.has_value());
    }

    SECTION("Create a node with an invalid type")
    {
        Node n;
        REQUIRE_THROWS(n.type("aaaaa"));
    }

    SECTION("Update existing node")
    {

        std::optional<Node> n_id = G->get_node("nodename");
        REQUIRE(n_id.has_value());
        G->add_attrib_local<level_att>(n_id.value(), 1);
        bool r = G->update_node(n_id.value());
        REQUIRE(r);

    }

    SECTION("Remove an attribute")
    {
        std::optional<Node> n_id = G->get_node("nodename");
        REQUIRE(n_id.has_value());
        REQUIRE(G->remove_attrib_local(n_id.value(), "level"));
        G->update_node(n_id.value());
        REQUIRE(n_id->attrs().find("level") == n_id->attrs().end());
        REQUIRE_FALSE(G->remove_attrib_local(n_id.value(), "level"));
    }


    SECTION("Update an existent node with different name")
    {
        std::optional<Node> n_ = G->get_node("nodename");
        REQUIRE(n_.has_value());
        n_->name("test2");
        REQUIRE_THROWS(G->update_node(n_.value()));
    }

    SECTION("Update an existent node with different id")
    {
        Node n;
        n.name("nodename");
        n.id(7500166);
        n.type("test_n");
        REQUIRE_THROWS(G->update_node(n));
    }

    SECTION("Delete existing node by id")
    {
        std::optional<Node> n_ = G->get_node("nodename");
        REQUIRE(n_.has_value());
        bool r = G->delete_node(n_->id());
        REQUIRE(r);
        REQUIRE(G->get_node(n_->name()) == std::nullopt);
    }


    SECTION("Delete existing node by name")
    {
        Node n;
        n.type("test_n");
        n.name("nodenewname");
        auto r = G->insert_node(n);
        REQUIRE(r.has_value());
        bool r2 = G->delete_node("nodenewname");
        REQUIRE(r2);
        REQUIRE(G->get_node(r.value()) == std::nullopt);

    }

    SECTION("Delete a node that does not exists by id")
    {
        bool r = G->delete_node(75000);
        REQUIRE_FALSE(r);
    }

    SECTION("Create a node with an user defined name")
    {
        Node n;
        n.id(1550);
        n.type("test_n");
        n.name("NODE_NAME");
        std::optional<uint64_t> r = G->insert_node(n);
        REQUIRE(r.has_value());
        REQUIRE(G->get_node("NODE_NAME").has_value());
    }
}


TEST_CASE("Edge operations", "[EDGE]") {

    std::shared_ptr<DSRGraph> G = TGraph::get().get_G();

    SECTION("Get an edge that does not exists by id")
    {
        std::optional<Edge> e_id = G->get_edge(666666, 77777, "K");
        REQUIRE_FALSE(e_id.has_value());
    }

    SECTION("Get an edge that does not exists by name")
    {
        std::optional<Edge> e_name = G->get_edge("no existe", "otro no existe", "K");
        REQUIRE_FALSE(e_name.has_value());
    }

    SECTION("Insert a new edge and get it by name and id")
    {
        auto n = Node::create<test_n_node_type>("nodeedge1");
        auto id1 = G->insert_node(n);
        REQUIRE(id1.has_value());

        auto n2 = Node::create<test_n_node_type>("nodeedge2");
        auto id2 = G->insert_node(n2);
        REQUIRE(id2.has_value());

        auto e = Edge::create<test_e_edge_type>(id1.value(), id2.value());
        bool r = G->insert_or_assign_edge(e);
        REQUIRE(r);

        std::optional<Edge> e_id = G->get_edge(id1.value(), id2.value(), "test_e");
        REQUIRE(e_id.has_value());

        std::optional<Edge> e_name = G->get_edge("nodeedge1", "nodeedge2", "test_e");
        REQUIRE(e_name.has_value());

        std::optional<Edge> e_node = G->get_edge(n, "nodeedge2", "test_e");
        REQUIRE(e_name.has_value());
    }

    SECTION("Update existing edge")
    {
        std::vector<Edge> edges = G->get_edges_by_type("test_e");
        REQUIRE(edges.size() == 1);
        Edge e = edges.at(0);

        G->add_attrib_local<test_string_att>(e, std::string("a"));
        bool r = G->insert_or_assign_edge(e);
        REQUIRE(r);

    }

    SECTION("Delete existing edge by id")
    {

        std::vector<Edge> edges = G->get_edges_by_type("test_e");
        REQUIRE(edges.size() == 1);
        Edge e = edges.at(0);
        bool r = G->delete_edge(e.from(), e.to(), "test_e");
        REQUIRE(r);
        REQUIRE(G->get_edge(e.from(), e.to(), "test_e") == std::nullopt);

//Reinsert for later.
        bool r2 = G->insert_or_assign_edge(e);
        REQUIRE(r2);
    }

    SECTION("Delete existing edge by name")
    {
        std::vector<Edge> edges = G->get_edges_by_type("test_e");
        REQUIRE(edges.size() == 1);
        Edge e = edges.at(0);

        bool r = G->delete_edge("nodeedge1", "nodeedge2", "test_e");
        REQUIRE(r);
        REQUIRE(G->get_edge("nodeedge1", "nodeedge2", "test_e") == std::nullopt);

    }

    SECTION("Delete an edge that does not exists")
    {
        bool r = G->delete_edge(4, 5, "test_e");
        REQUIRE_FALSE(r);
    }

}

/*
TEST_CASE("File operations (Utilities sub-api)", "[FILE]") {

    std::shared_ptr<DSRGraph> G = TGraph::get().get_G();
    //DSR::Utilities u (G.get());

    const std::string empty_file = "/home/robocomp/robocomp/components/dsr-TGraph/components/dsr_tests/src/unittests/testfiles/empty_file.json";
    const std::string wempty_file = "/home/robocomp/robocomp/components/dsr-TGraph/components/dsr_tests/src/unittests/testfiles/write_empty_file.json";

    const std::string test_file = "/home/robocomp/robocomp/components/dsr-TGraph/components/dsr_tests/src/unittests/testfiles/initial_dsr2.json";
    const std::string wtest_file = "/home/robocomp/robocomp/components/dsr-TGraph/components/dsr_tests/src/unittests/testfiles/write_initial_dsr2.json";



    SECTION("Load an empty file") {
        G->reset();
        REQUIRE(G->size() == 0);

        G->read_from_json_file(empty_file);
        REQUIRE(G->size() == 0);

    }

    SECTION("Write an empty file") {
        std::filesystem::remove(wempty_file);
        REQUIRE_FALSE(std::filesystem::exists(wempty_file));
        G->write_to_json_file(wempty_file);
        REQUIRE(std::filesystem::exists(wempty_file));
        std::ifstream file(wempty_file);
        const std::string c = "{\n"
                            "    \"DSRModel\": {\n"
                            "        \"symbols\": {\n"
                            "        }\n"
                            "    }\n"
                            "}\n";
        std::string content = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        REQUIRE( content ==  c);

    }

    SECTION("Load a non-empty file") {
        G->reset();
        REQUIRE(G->size() == 0);

        G->read_from_json_file(test_file);
        REQUIRE(G->size() == 75);
    }

    SECTION("Write a file") {
        std::filesystem::remove(wtest_file);
        REQUIRE_FALSE(std::filesystem::exists(wtest_file));
        G->write_to_json_file(wtest_file);
        REQUIRE(std::filesystem::exists(wtest_file));
        std::ifstream file(wtest_file);
        std::ifstream file2(test_file);

        QString content = QString::fromStdString(std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()));
        QString content2 =  QString::fromStdString(std::string((std::istreambuf_iterator<char>(file2)), std::istreambuf_iterator<char>()));

        REQUIRE( QJsonDocument::fromJson(content.toUtf8()) ==  QJsonDocument::fromJson(content2.toUtf8()));


    }
}*/

TEST_CASE("Maps operations", "[UTILS]") {
    std::shared_ptr<DSRGraph> G = TGraph::get().get_G();

    SECTION("Get id from a name and a name from an id")
    {
        auto n = Node::create<test_n_node_type>("nameformaptestnode");
        std::optional<uint64_t> r = G->insert_node(n);
        REQUIRE(r.has_value());

        std::optional<uint64_t> id = G->get_id_from_name("nameformaptestnode");
        REQUIRE(id.has_value());
        REQUIRE(id.value() == r.value());

        std::optional<std::string> name = G->get_name_from_id(r.value());
        REQUIRE(name.has_value());
        REQUIRE(name.value() == "nameformaptestnode");
    }


    SECTION("Get id from a name that does not exist")
    {
        std::optional<uint64_t> id = G->get_id_from_name("t_55");
        REQUIRE(!id.has_value());
    }


    SECTION("Get name from an id that does not exist")
    {
        std::optional<std::string> name = G->get_name_from_id(788987);
        REQUIRE(!name.has_value());
    }

    SECTION("Get nodes by type")
    {
        auto n = Node::create<test_n_node_type>("nameformaptestnode2");
        std::optional<uint64_t> r = G->insert_node(n);
        REQUIRE(r.has_value());

        std::vector<Node> ve = G->get_nodes_by_type("test_n");
        REQUIRE(!ve.empty());
        ve = G->get_nodes_by_type("no");
        REQUIRE(ve.empty());

    }SECTION("Get edges by type")
    {
        std::vector<Edge> ve = G->get_edges_by_type("RT");
        REQUIRE(!ve.empty());
        REQUIRE(std::all_of(ve.begin(), ve.end(), [](auto &a) { return "RT" == a.type(); }));
        ve = G->get_edges_by_type("no");
        REQUIRE(ve.empty());
    }SECTION("Get edges from/to a node (id)")
    {

        auto n = Node::create<test_n_node_type>("nameformaptestnode21");
        std::optional<uint64_t> r = G->insert_node(n);
        REQUIRE(r);

        auto n2 = Node::create<test_n_node_type>("nameformaptestnode22");
        std::optional<uint64_t> r2 = G->insert_node(n2);
        REQUIRE(r2);

        auto n3 = Node::create<test_n_node_type>("nameformaptestnode23");
        std::optional<uint64_t> r3 = G->insert_node(n3);
        REQUIRE(r3);


        auto e1 = Edge::create<test_e_edge_type>(*r2, *r);
        auto e2 = Edge::create<test_e_edge_type>(*r3, *r);

        auto b1 = G->insert_or_assign_edge(e1);
        auto b2 = G->insert_or_assign_edge(e2);

        REQUIRE((b1 && b2));

        std::vector<Edge> ve = G->get_edges_to_id(r.value());
        REQUIRE(( !ve.empty()  && ve.size() == 2 ));
        ve = G->get_edges_to_id(r2.value());
        REQUIRE(ve.empty());


        std::optional<std::map<std::pair<uint64_t, std::string>, DSR::Edge>> edges = G->get_edges(r.value());
        REQUIRE( (edges.has_value() && edges->empty() ) );
        edges = G->get_edges(9999999999);
        REQUIRE( !edges.has_value() );
        edges = G->get_edges(r2.value());
        REQUIRE(( edges.has_value() && edges->size() == 1));
        std::optional<Node> n4 = G->get_node(r2.value());
        REQUIRE(n4.has_value());
        REQUIRE(n4->fano() == edges.value());

    }

    SECTION("Get edges from a node that does not exist")
    {
        std::optional<std::map<std::pair<uint64_t, std::string>, DSR::Edge>> ve = G->get_edges(45550);
        REQUIRE(!ve.has_value());
    }

    SECTION("Get edges from a node that has no edges")
    {
        std::optional<std::map<std::pair<uint64_t, std::string>, DSR::Edge>> ve = G->get_edges(1000); //1000
        REQUIRE(ve.has_value());
        REQUIRE(ve->empty());
    }

}

TEST_CASE("Node and Edge type checking (RUNTIME)") {
    std::shared_ptr<DSRGraph> G = TGraph::get().get_G();

    SECTION("Node")
    {
        Node n;
        REQUIRE_NOTHROW(n.type("test_n"));
        REQUIRE_THROWS(n.type("invalidtype"));
    }

    SECTION("Edge")
    {
        Edge e;
        REQUIRE_NOTHROW(e.type("test_e"));
        REQUIRE_THROWS(e.type("invalidtype"));
    }
}

TEST_CASE("Attributes operations (Compile time type-check)", "[ATTRIBUTES]") {
    std::shared_ptr<DSRGraph> G = TGraph::get().get_G();

    SECTION("Insert attribute (node) and insert node in G")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_or_modify_attrib_local<test_int_att>(n.value(), 123);
        G->update_node(n.value());
        REQUIRE(n->attrs().find("test_int") != n->attrs().end());
    }

    SECTION("Insert an string attribute and update G")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_or_modify_attrib_local<test_string_att>(n.value(), std::string("string att"));
        G->update_node(n.value());
        REQUIRE(n->attrs().find("test_string") != n->attrs().end());
        bool r = G->update_node(n.value());
        REQUIRE(r);
    }

    SECTION("Insert attribute (edge) and insert in G")
    {
        std::optional<Edge> e = G->get_edge(1, 2, "RT");
        REQUIRE(e.has_value());
        G->add_or_modify_attrib_local<test_int_att>(e.value(), 123);
        G->insert_or_assign_edge(e.value());
        REQUIRE(e->attrs().find("test_int") != e->attrs().end());
        bool r = G->insert_or_assign_edge(e.value());
        REQUIRE(r);
    }

    SECTION("Update attribute and update G")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_or_modify_attrib_local<test_int_att>(n.value(), 125);
        G->update_node(n.value());
        REQUIRE(std::get<std::int32_t>(n->attrs()["test_int"].value()) == 125);
        bool r = G->update_node(n.value());
        REQUIRE(r);
    }

    SECTION("Get attribute by name")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<int> att = G->get_attrib_by_name<test_int_att>(n.value());
        REQUIRE(att.has_value());
        REQUIRE(att.value() == 125);

    }

}

TEST_CASE("Convenience methods", "[CONVENIENCE METHODS]") {
    std::shared_ptr<DSRGraph> G = TGraph::get().get_G();

    SECTION("Get node level")
    {
        std::optional<Node> n = G->get_node(2);
        REQUIRE(n.has_value());
        std::optional<int> level = G->get_node_level(n.value());
        REQUIRE(level.has_value() == true);
        REQUIRE(level.value() == 1);


        auto in = Node::create<test_n_node_type>();
        auto id = G->insert_node(in);
        REQUIRE(id.has_value());
        n = G->get_node(id.value());
        REQUIRE(n.has_value());
        level = G->get_node_level(n.value());
        REQUIRE(level.has_value() == false);
    }

    SECTION("Get parent id")
    {
        std::optional<Node> n = G->get_node(2);
        REQUIRE(n.has_value());
        std::optional<uint64_t> id = G->get_parent_id(n.value());
        REQUIRE(id.has_value());
        REQUIRE(id.value() == 1);
    }

    SECTION("Get parent node")
    {
        std::optional<Node> n = G->get_node(2);
        REQUIRE(n.has_value());
        std::optional<Node> p = G->get_parent_node(n.value());
        REQUIRE(p.has_value());
        std::optional<Node> parent = G->get_node(1);
        REQUIRE(parent.has_value());
        REQUIRE(p.value() == parent.value());

        std::optional<Node> parent_empty = G->get_parent_node(parent.value());
        REQUIRE_FALSE(parent_empty.has_value());
    }

    SECTION("get_node_root")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<Node> n2 = G->get_node_root();
        REQUIRE(n2.has_value());
        REQUIRE(n2.value() == n.value());
    }
}

TEST_CASE("Attributes operations II (Runtime time type-check)", "[RUNTIME ATTRIBUTES]") {
    std::shared_ptr<DSRGraph> G = TGraph::get().get_G();

    SECTION("Insert an attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->runtime_checked_add_or_modify_attrib_local(n.value(), "test_int", 133);
        REQUIRE(std::get<std::int32_t>(n->attrs()["test_int"].value()) == 133);
        bool r = G->update_node(n.value());
        REQUIRE(r);
        REQUIRE_THROWS(G->runtime_checked_add_or_modify_attrib_local( n.value(), "test_int", 133.0f));
    }

    SECTION("Modify an attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        REQUIRE(G->runtime_checked_modify_attrib_local(n.value(), "test_int", 111));
        REQUIRE(std::get<std::int32_t>(n->attrs()["test_int"].value()) == 111);
        bool r = G->update_node(n.value());
        REQUIRE(r);
    }

    SECTION("Update attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        REQUIRE(G->runtime_checked_modify_attrib_local(n.value(), "test_int", 177));
        G->update_node(n.value());
        REQUIRE(std::get<std::int32_t>(n->attrs()["test_int"].value()) == 177);
        REQUIRE(std::get<std::int32_t>(n->attrs()["test_int"].value()) == G->get_attrib_by_name<test_int_att>(n.value()));
    }

    SECTION("Remove an attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        REQUIRE(G->remove_attrib_local(n.value(), "test_int"));
        REQUIRE(n->attrs().find("test_int") == n->attrs().end());
        bool r = G->update_node(n.value());
        REQUIRE(r);
    }

    SECTION("Insert attribute (edge) and insert in G")
    {
        std::optional<Edge> e = G->get_edge(1, 2, "RT");
        REQUIRE(e.has_value());
        REQUIRE(G->runtime_checked_add_attrib_local(e.value(), "new_int", 123));
        G->insert_or_assign_edge(e.value());
        REQUIRE(e->attrs().find("new_int") != e->attrs().end());
        bool r = G->insert_or_assign_edge(e.value());
        REQUIRE(r);
    }

}

TEST_CASE("Other attribute operations and checks", "[ATTRIBUTES]") {
    std::shared_ptr<DSRGraph> G = TGraph::get().get_G();

    SECTION("Get attribute timestamp")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_attrib_local<test_string_att>(n.value(), std::string("string att"));
        G->update_node(n.value());
        REQUIRE(G->get_attrib_timestamp<test_string_att>(n.value()).has_value());
        REQUIRE(G->get_attrib_timestamp_by_name(n.value(), "test_string").has_value());
    }

    SECTION("Get attribute by name using only the id (no references)")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_or_modify_attrib_local<test_string_att>(n.value(), std::string("string att"));
        G->update_node(n.value());
        std::string str2{G->get_attrib_by_name<test_string_att>(n.value()).value()};
        std::string str1 = G->get_attrib_by_name<test_string_att>(1).value();

        REQUIRE(str1 == str2);
    }

    SECTION("In add_or_modify_attrib_local, we can pass a reference or move the value")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::string stratt("string att lvalue");
        std::string strattmove("string att rvalue");
        G->add_or_modify_attrib_local<test_string_att>(n.value(), stratt);
        G->update_node(n.value());
        std::string str1 = G->get_attrib_by_name<test_string_att>(1).value();
        REQUIRE(str1 == stratt);
        G->add_or_modify_attrib_local<test_string_att>(n.value(), std::move(strattmove));
        G->update_node(n.value());
        str1 = G->get_attrib_by_name<test_string_att>(1).value();
        REQUIRE(str1 == "string att rvalue");
    }

    SECTION("runtime_checked_add_or_modify_attrib_local is equivalent to add_or_modify_attrib_local")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::string stratt("string att lvalue");
        G->add_or_modify_attrib_local<test_string_att>(n.value(), stratt);
        G->update_node(n.value());
        std::string str1 = G->get_attrib_by_name<test_string_att>(1).value();
        REQUIRE(str1 == stratt);
        REQUIRE_NOTHROW(G->runtime_checked_add_or_modify_attrib_local(n.value(), std::string{test_string_str}, stratt));
        G->update_node(n.value());
        str1 = G->get_attrib_by_name<test_string_att>(1).value();
        REQUIRE(str1 == stratt);
    }

    SECTION("add_attrib_local is equivalent to runtime_checked_add_attrib_local")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::string stratt("string att");
        G->remove_attrib_local<test_string_att>(n.value());
        G->update_node(n.value());
        REQUIRE_FALSE(G->get_attrib_by_name<test_string_att>(1).has_value());
        REQUIRE(G->add_attrib_local<test_string_att>(n.value(), stratt));
        REQUIRE_FALSE(G->add_attrib_local<test_string_att>(n.value(), stratt));
        G->update_node(n.value());
        std::optional<std::string> value1 = G->get_attrib_by_name<test_string_att>(1);
        REQUIRE(value1.has_value());

        G->remove_attrib_local<test_string_att>(n.value());
        G->update_node(n.value());
        REQUIRE_FALSE(G->get_attrib_by_name<test_string_att>(1).has_value());
        REQUIRE(G->runtime_checked_add_attrib_local(n.value(), std::string{test_string_str}, stratt));
        REQUIRE_FALSE(G->runtime_checked_add_attrib_local(n.value(), std::string{test_string_str}, stratt));
        G->update_node(n.value());

        std::optional<std::string> value2 = G->get_attrib_by_name<test_string_att>(1);
        REQUIRE(value2.has_value());
        REQUIRE(value1.value() == value2.value());
    }

    SECTION("modify_attrib_local is equivalent to runtime_checked_modify_attrib_local")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::string stratt("string att");
        G->remove_attrib_local<test_string_att>(n.value());
        G->update_node(n.value());
        REQUIRE_FALSE(G->get_attrib_by_name<test_string_att>(1).has_value());

        REQUIRE_FALSE(G->modify_attrib_local<test_string_att>(n.value(), stratt));
        REQUIRE(G->add_attrib_local<test_string_att>(n.value(), std::string{"initial_value"}));
        REQUIRE(G->get_attrib_by_name<test_string_att>(n.value()).value().get() == std::string{"initial_value"});
        REQUIRE(G->modify_attrib_local<test_string_att>(n.value(), stratt));
        REQUIRE(G->get_attrib_by_name<test_string_att>(n.value()).value().get() == stratt);

        G->update_node(n.value());
        std::optional<std::string> value1 = G->get_attrib_by_name<test_string_att>(1);
        REQUIRE(value1.has_value());

        G->remove_attrib_local<test_string_att>(n.value());
        G->update_node(n.value());
        REQUIRE_FALSE(G->get_attrib_by_name<test_string_att>(1).has_value());

        REQUIRE_FALSE(G->runtime_checked_modify_attrib_local(n.value(), std::string{test_string_str}, stratt));
        REQUIRE(G->add_attrib_local<test_string_att>(n.value(), std::string{"initial_value"}));
        REQUIRE(G->get_attrib_by_name<test_string_att>(n.value()).value().get() == std::string{"initial_value"});
        REQUIRE(G->runtime_checked_modify_attrib_local(n.value(), std::string{test_string_str}, stratt));
        REQUIRE(G->get_attrib_by_name<test_string_att>(n.value()).value().get() == stratt);

        G->update_node(n.value());
        std::optional<std::string> value2 = G->get_attrib_by_name<test_string_att>(1);
        REQUIRE(value2.has_value());
        REQUIRE(value1.value() == value2.value());
    }
}

TEST_CASE("Native types in attributes", "[ATTRIBUTES]") {
    std::shared_ptr<DSRGraph> G = TGraph::get().get_G();

    SECTION("Insert a string attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_attrib_local<test_string_att>(n.value(), std::string("string att"));
        G->update_node(n.value());

        REQUIRE(n->attrs().find("test_string") != n->attrs().end());
        std::optional<Node> n2 = G->get_node(1);
        REQUIRE(n2.has_value());
        REQUIRE(n.value() == n2.value());
    }SECTION("Get a string attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<std::string> st = G->get_attrib_by_name<test_string_att>(n.value());
        REQUIRE(st.has_value());
    }

    SECTION("Insert an int32 attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_attrib_local<test_int_att>(n.value(), 11);
        G->update_node(n.value());

        REQUIRE(n->attrs().find("test_int") != n->attrs().end());
        std::optional<Node> n2 = G->get_node(1);
        REQUIRE(n2.has_value());
        REQUIRE(G->get_attrib_by_name<test_int_att>(n.value()) == G->get_attrib_by_name<test_int_att>(n2.value()));
    }SECTION("Get an int32 attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<int> st = G->get_attrib_by_name<test_int_att>(n.value());
        REQUIRE(st.has_value());
    }

    SECTION("Insert an uint32 attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_attrib_local<test_uint_att>(n.value(), 11u);
        G->update_node(n.value());

        REQUIRE(n->attrs().find("test_uint") != n->attrs().end());
        std::optional<Node> n2 = G->get_node(1);
        REQUIRE(n2.has_value());
        REQUIRE(G->get_attrib_by_name<test_uint_att>(n.value()) == G->get_attrib_by_name<test_uint_att>(n2.value()));
    }SECTION("Get an uint32 attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<uint32_t> st = G->get_attrib_by_name<test_uint_att>(n.value());
        REQUIRE(st.has_value());
    }

    SECTION("Insert an uint64 attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_attrib_local<test_uint64_att>(n.value(), static_cast<uint64_t>(1));
        G->update_node(n.value());

        REQUIRE(n->attrs().find("test_uint64") != n->attrs().end());
        std::optional<Node> n2 = G->get_node(1);
        REQUIRE(n2.has_value());
        REQUIRE(G->get_attrib_by_name<test_uint64_att>(n.value()) == G->get_attrib_by_name<test_uint64_att>(n2.value()));
    }SECTION("Get an uint64 attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<uint64_t> st = G->get_attrib_by_name<test_uint64_att>(n.value());
        REQUIRE(st.has_value());
    }

    SECTION("Insert a float attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_attrib_local<test_float_att>(n.value(), static_cast<float>(11.0));
        G->update_node(n.value());

        REQUIRE(n->attrs().find("test_float") != n->attrs().end());
        std::optional<Node> n2 = G->get_node(1);
        REQUIRE(n2.has_value());
        REQUIRE(G->get_attrib_by_name<test_float_att>(n.value()) == G->get_attrib_by_name<test_float_att>(n2.value()));

    }SECTION("Get a float attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<float> st = G->get_attrib_by_name<test_float_att>(n.value());
        REQUIRE(st.has_value());
    }

    SECTION("Insert a float_vector attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_attrib_local<test_vec_float_att>(n.value(), std::vector<float>{11.0, 167.23, 55.66});
        G->update_node(n.value());

        REQUIRE(n->attrs().find("test_vec_float") != n->attrs().end());
        std::optional<Node> n2 = G->get_node(1);
        REQUIRE(n2.has_value());
        REQUIRE(G->get_attrib_by_name<test_vec_float_att>(n.value()).value().get() ==
                G->get_attrib_by_name<test_vec_float_att>(n2.value()).value().get());
    }SECTION("Get a float_vector attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<std::vector<float>> st = G->get_attrib_by_name<test_vec_float_att>(n.value());
        REQUIRE(st.has_value());
    }

    SECTION("Insert a bool attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_attrib_local<test_bool_att>(n.value(), true);
        G->update_node(n.value());
        REQUIRE(n->attrs().find("test_bool") != n->attrs().end());
        std::optional<Node> n2 = G->get_node(1);
        REQUIRE(n2.has_value());
        REQUIRE(G->get_attrib_by_name<test_bool_att>(n.value()) == G->get_attrib_by_name<test_bool_att>(n2.value()));
    }SECTION("Get a bool attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<bool> st = G->get_attrib_by_name<test_bool_att>(n.value());
        REQUIRE(st.has_value());
    }

    SECTION("Insert a byte_vector attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_attrib_local<test_vec_byte_att>(n.value(), std::vector<uint8_t>{11, 167, 55});
        G->update_node(n.value());

        REQUIRE(n->attrs().find("test_vec_byte") != n->attrs().end());
        std::optional<Node> n2 = G->get_node(1);
        REQUIRE(n2.has_value());
        REQUIRE(G->get_attrib_by_name<test_vec_byte_att>(n.value()).value().get() ==
                G->get_attrib_by_name<test_vec_byte_att>(n2.value()).value().get());
    }SECTION("Get a byte_vector attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<std::vector<uint8_t>> st = G->get_attrib_by_name<test_vec_byte_att>(n.value());
        REQUIRE(st.has_value());
    }

    SECTION("Insert a double attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_attrib_local<test_double_att>(n.value(), double{33.3});
        G->update_node(n.value());
        REQUIRE(n->attrs().find("test_double") != n->attrs().end());
        std::optional<Node> n2 = G->get_node(1);
        REQUIRE(n2.has_value());
        REQUIRE(G->get_attrib_by_name<test_double_att>(n.value()) == G->get_attrib_by_name<test_double_att>(n2.value()));
    }SECTION("Get a double attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<double> st = G->get_attrib_by_name<test_double_att>(n.value());
        REQUIRE(st.has_value());
    }


    SECTION("Insert a byte_vector attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_attrib_local<test_vec_u64_att>(n.value(), std::vector<uint64_t>{11, 167, 55});
        G->update_node(n.value());

        REQUIRE(n->attrs().find("test_vec_u64") != n->attrs().end());
        std::optional<Node> n2 = G->get_node(1);
        REQUIRE(n2.has_value());
        REQUIRE(G->get_attrib_by_name<test_vec_u64_att>(n.value()).value().get() ==
                G->get_attrib_by_name<test_vec_u64_att>(n2.value()).value().get());
    }SECTION("Get a byte_vector attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<std::reference_wrapper<const std::vector<uint64_t>>> st = G->get_attrib_by_name<test_vec_u64_att>(
                n.value());
        REQUIRE(st.has_value());
    }

    SECTION("Insert an array<float, 2> attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_attrib_local<test_vec2_att>(n.value(), std::array<float, 2>{11.0, 167.0});
        G->update_node(n.value());

        REQUIRE(n->attrs().find("test_vec2") != n->attrs().end());
        std::optional<Node> n2 = G->get_node(1);
        REQUIRE(n2.has_value());
        REQUIRE(G->get_attrib_by_name<test_vec2_att>(n.value()).value().get() ==
                G->get_attrib_by_name<test_vec2_att>(n2.value()).value().get());
    }SECTION("Get a array<float, 2> attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<std::reference_wrapper<const std::array<float, 2>>> st = G->get_attrib_by_name<test_vec2_att>(
                n.value());
        REQUIRE(st.has_value());
    }

    SECTION("Insert an array<float, 3> attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_attrib_local<test_vec3_att>(n.value(), std::array<float, 3>{11.0, 167.0, 0.0});
        G->update_node(n.value());

        REQUIRE(n->attrs().find("test_vec3") != n->attrs().end());
        std::optional<Node> n2 = G->get_node(1);
        REQUIRE(n2.has_value());
        REQUIRE(G->get_attrib_by_name<test_vec3_att>(n.value()).value().get() ==
                G->get_attrib_by_name<test_vec3_att>(n2.value()).value().get());
    }SECTION("Get a array<float, 3> attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<std::reference_wrapper<const std::array<float, 3>>> st = G->get_attrib_by_name<test_vec3_att>(
                n.value());
        REQUIRE(st.has_value());
    }

    SECTION("Insert an array<float, 4> attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_attrib_local<test_vec4_att>(n.value(), std::array<float, 4>{11.0, 167.0, 1.0, 0.0});
        G->update_node(n.value());

        REQUIRE(n->attrs().find("test_vec4") != n->attrs().end());
        std::optional<Node> n2 = G->get_node(1);
        REQUIRE(n2.has_value());
        REQUIRE(G->get_attrib_by_name<test_vec4_att>(n.value()).value().get() ==
                G->get_attrib_by_name<test_vec4_att>(n2.value()).value().get());
    }SECTION("Get a array<float, 4> attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<std::reference_wrapper<const std::array<float, 4>>> st = G->get_attrib_by_name<test_vec4_att>(
                n.value());
        REQUIRE(st.has_value());
    }

    SECTION("Insert an array<float, 6> attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_attrib_local<test_vec6_att>(n.value(), std::array<float, 6>{0.0, 0.0, 11.0, 167.0, 1.0, 1.0});
        G->update_node(n.value());

        REQUIRE(n->attrs().find("test_vec6") != n->attrs().end());
        std::optional<Node> n2 = G->get_node(1);
        REQUIRE(n2.has_value());
        REQUIRE(G->get_attrib_by_name<test_vec6_att>(n.value()).value().get() ==
                G->get_attrib_by_name<test_vec6_att>(n2.value()).value().get());
    }SECTION("Get a array<float, 6> attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<std::reference_wrapper<const std::array<float, 6>>> st = G->get_attrib_by_name<test_vec6_att>(
                n.value());
        REQUIRE(st.has_value());
    }

    SECTION("Insert an array<float, 6> attribute without reference wrapper")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        G->add_attrib_local<test_vec6_no_reference_att>(n.value(), std::array<float, 6>{0.0, 0.0, 11.0, 167.0, 1.0, 0.0});
        G->update_node(n.value());

        REQUIRE(n->attrs().find("test_vec6_no_reference") != n->attrs().end());
        std::optional<Node> n2 = G->get_node(1);
        REQUIRE(n2.has_value());
        REQUIRE(G->get_attrib_by_name<test_vec6_no_reference_att>(n.value()).value() ==
                G->get_attrib_by_name<test_vec6_no_reference_att>(n2.value()).value());
    }SECTION("Get a array<float, 6> attribute")
    {
        std::optional<Node> n = G->get_node(1);
        REQUIRE(n.has_value());
        std::optional<std::array<float, 6>> st = G->get_attrib_by_name<test_vec6_no_reference_att>(n.value());
        REQUIRE(st.has_value());
    }
}


//Scenarios
SCENARIO("Node insertions, updates and removals", "[NODE]") {
    std::shared_ptr<DSRGraph> G = TGraph::get().get_G();

    GIVEN("A new Node")
    {
        Node n;
        REQUIRE_NOTHROW(n.type("robot"));
        n.name("robot_8");
        n.agent_id(0);
        G->add_attrib_local<test_string_att>(n, std::string("value"));

        WHEN("When we insert a new node")
        {
            THEN("The node is inserted and the graph is bigger")
            {
                auto size = G->size();
                auto r = G->insert_node(n);
                REQUIRE(r);
                REQUIRE(size + 1 == G->size());
            }THEN("You can get the node")
            {
                auto no = G->get_node("robot_8");
                REQUIRE(no.has_value());
                G->add_attrib_local<test_string_att>(no.value(), std::string("value"));
                std::optional<Node> node = G->get_node("robot_8");
                REQUIRE(node.has_value());
                THEN("The requested node is equal to the inserted node")
                {
                    REQUIRE(node.value() == no);
                }
            }
        }

        AND_WHEN("The node is updated")
        {
            auto size = G->size();
            REQUIRE_NOTHROW(G->get_node("robot_8").value());
            n = G->get_node("robot_8").value();
            G->add_attrib_local<test_int_att>(n, 11);
            bool r = G->update_node(n);
            REQUIRE(r);

            THEN("The graph size is equal")
            {
                REQUIRE(size == G->size());
            }THEN("You can get the node")
            {
                std::optional<Node> node = G->get_node("robot_8");
                REQUIRE(node.has_value());
                THEN("The requested node has the inserted attribute")
                {
                    REQUIRE(G->get_attrib_by_name<test_int_att>(node.value()) == 11);
                }
            }
        }

        AND_WHEN("The node is deleted")
        {
            auto node = G->get_node("robot_8");
            G->delete_node("robot_8");
            THEN("You can't get or update the node")
            {
                REQUIRE_THROWS(G->update_node(node.value()));
                std::optional<Node> node2 = G->get_node("robot_8");
                REQUIRE(!node2.has_value());
            }
        }
    }GIVEN("A deleted node")
    {
        std::optional<Node> node = G->get_node("robot_8");
        THEN("Optional value is empty")
        {
            REQUIRE_FALSE(node.has_value());
        }
    }
}


