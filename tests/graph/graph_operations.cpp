//
// Created on 5/11/24.
//


#include "dsr/api/dsr_api.h"
#include "../utils.h"
#include <optional>

#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators.hpp"
#include "dsr/core/types/type_checking/dsr_edge_type.h"
#include "dsr/core/types/type_checking/dsr_node_type.h"


using namespace DSR;




REGISTER_TYPE(att, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(int_, int32_t, false)
REGISTER_TYPE(float_, float, false)
REGISTER_TYPE(bool_, bool, false)
REGISTER_TYPE(uint_, uint32_t, false)
REGISTER_TYPE(uint64_, uint64_t , false)
REGISTER_TYPE(string_, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(vec_byte, std::reference_wrapper<const std::vector<uint8_t>>, false)
REGISTER_TYPE(vec_float, std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(doub, double, false)
REGISTER_TYPE(vec_u64, std::reference_wrapper<const std::vector<uint64_t>>, false)
REGISTER_TYPE(vec2_, std::reference_wrapper<const vec2>, false)
REGISTER_TYPE(vec3_, std::reference_wrapper<const vec3>, false)
REGISTER_TYPE(vec4_, std::reference_wrapper<const vec4>, false)
REGISTER_TYPE(vec6_, std::reference_wrapper<const vec6>, false)
REGISTER_TYPE(att_no_reference, vec6, false)



SCENARIO( "Node insertions, updates and removals", "[NODE]" ) {
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    CAPTURE(sync_mode_label(sync_mode));

    auto filename = make_edge_config_file();
    DSRGraph G(make_test_graph_settings(random_string(10), rand() % 1200, filename, true, 0, SignalMode::QT, sync_mode));

    GIVEN("A new Node")
    {
        Node n;
        REQUIRE_NOTHROW(n.type("robot"));
        n.name("robot_8");
        n.agent_id(0);
        G.add_attrib_local<att_att>(n, std::string("value"));

        WHEN("When we insert a new node")
        {
            THEN("The node is inserted and the graph is bigger")
            {
                auto size = G.size();
                auto r = G.insert_node(n);
                REQUIRE(r);
                REQUIRE(size + 1 == G.size());
                AND_THEN("You can get the node")
                {
                    auto no = G.get_node("robot_8");
                    REQUIRE(no.has_value());
                    G.add_attrib_local<att_att>(no.value(), std::string("value"));
                    std::optional<Node> node = G.get_node("robot_8");
                    REQUIRE(node.has_value());
                    THEN("The requested node is equal to the inserted node") {
                        REQUIRE(node.value() == no);
                    }
                }
                AND_WHEN("The node is updated")
                {
                    auto size = G.size();
                    REQUIRE_NOTHROW(G.get_node("robot_8").value());
                    n = G.get_node("robot_8").value();
                    G.add_attrib_local<int__att>(n,  11);
                    bool r = G.update_node(n);
                    REQUIRE(r);

                    THEN("The graph size is equal")
                    {
                        REQUIRE(size == G.size());
                        AND_THEN("You can get the node")
                        {
                            std::optional<Node> node = G.get_node("robot_8");
                            REQUIRE(node.has_value());
                            THEN("The requested node has the inserted attribute") {
                                REQUIRE(G.get_attrib_by_name<int__att>(node.value()) == 11);
                            }
                        }
                    }
                
                    AND_WHEN("The node is deleted")
                    {
                        auto node = G.get_node("robot_8");
                        G.delete_node("robot_8");
                        AND_THEN("You can't get or update the node")
                        {
                            REQUIRE_THROWS(G.update_node(node.value()));
                            std::optional<Node> node2 = G.get_node("robot_8");
                            REQUIRE(!node2.has_value());
                        }
                        AND_GIVEN("A deleted node")
                        {
                            std::optional<Node> node = G.get_node("robot_8");
                            THEN("Optional value is empty")
                            {
                                REQUIRE_FALSE(node.has_value());
                            }
                        }  
                    }

                }
            }

            
        }      
    }

}


