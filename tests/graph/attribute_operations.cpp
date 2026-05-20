

#include "dsr/api/dsr_api.h"
#include "../utils.h"
#include <optional>

#include "catch2/catch_test_macros.hpp"
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


TEST_CASE("Attributes operations (Compile time type-checked)", "[ATTRIBUTES]") {

    auto filename = make_edge_config_file();
    DSRGraph G(random_string(10), rand() % 1200, filename);


    SECTION("Insert attribute (node) and insert node in G") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_or_modify_attrib_local<int__att>(n.value(), (int)random_number());
        G.update_node(n.value());
        REQUIRE(n->attrs().find("int_") != n->attrs().end());
    }

    SECTION("Insert an string attribute and update G") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_or_modify_attrib_local<string__att>(n.value(),  std::string("string att"));
        G.update_node(n.value());
        REQUIRE(n->attrs().find("string_") != n->attrs().end());
        bool r = G.update_node(n.value());
        REQUIRE(r);
    }

    SECTION("Insert attribute (edge) and insert in G") {
        std::optional<Edge> e = G.get_edge(100, 150, "RT");
        REQUIRE(e.has_value());
        G.add_or_modify_attrib_local<int__att>(e.value(), 123);
        G.insert_or_assign_edge(e.value());
        REQUIRE(e->attrs().find("int_") != e->attrs().end());
        bool r = G.insert_or_assign_edge(e.value());
        REQUIRE(r);
    }

    SECTION("Insert rt_covariance attribute on RT edge") {
        std::optional<Edge> e = G.get_edge(100, 150, "RT");
        REQUIRE(e.has_value());

        const std::vector<float> covariance(36, 0.5f);
        G.add_or_modify_attrib_local<rt_covariance_att>(e.value(), covariance);
        REQUIRE(G.insert_or_assign_edge(e.value()));

        auto stored_edge = G.get_edge(100, 150, "RT");
        REQUIRE(stored_edge.has_value());

        auto stored_covariance = G.get_attrib_by_name<rt_covariance_att>(stored_edge.value());
        REQUIRE(stored_covariance.has_value());
        REQUIRE(stored_covariance.value().get() == covariance);
    }

    SECTION("Update attribute and update G") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_or_modify_attrib_local<int__att>(n.value(), 125);
        G.update_node(n.value());
        REQUIRE(std::get<std::int32_t>(n->attrs()["int_"].value()) == 125);
        bool r = G.update_node(n.value());
        REQUIRE(r);
    }

    SECTION("Get attribute by name") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_or_modify_attrib_local<int__att>(n.value(), 125);
        G.update_node(n.value());
        std::optional<int> att = G.get_attrib_by_name<int__att>(n.value());
        REQUIRE(att.has_value());
        REQUIRE(att.value() == 125);
    }

}

TEST_CASE("Attributes operations II (Runtime time type-checked)", "[RUNTIME ATTRIBUTES]") {

    auto filename = make_edge_config_file();
    DSRGraph G(random_string(10), rand() % 1200, filename);

    SECTION("Insert an attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.runtime_checked_add_or_modify_attrib_local(n.value(), "int_", 133);
        REQUIRE(std::get<std::int32_t>(n->attrs()["int_"].value()) == 133);
        bool r = G.update_node(n.value());
        REQUIRE(r);
        REQUIRE_THROWS(G.runtime_checked_add_or_modify_attrib_local(n.value(), "int_", 133.0f));
    }

    SECTION("Modify an attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.runtime_checked_add_or_modify_attrib_local(n.value(), "int_", 133);
        REQUIRE(std::get<std::int32_t>(n->attrs()["int_"].value()) == 133);
        REQUIRE(G.runtime_checked_modify_attrib_local(n.value(), "int_", 111));
        REQUIRE(std::get<std::int32_t>(n->attrs()["int_"].value()) == 111);
        bool r = G.update_node(n.value());
        REQUIRE(r);
    }

    SECTION("Update attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.runtime_checked_add_or_modify_attrib_local(n.value(), "int_", 133);
        G.update_node(n.value());
        REQUIRE(std::get<std::int32_t>(n->attrs()["int_"].value()) == 133);
        REQUIRE(G.runtime_checked_modify_attrib_local(n.value(), "int_", 177));
        G.update_node(n.value());
        REQUIRE(std::get<std::int32_t>(n->attrs()["int_"].value()) == 177);
        REQUIRE(std::get<std::int32_t>(n->attrs()["int_"].value()) == G.get_attrib_by_name<int__att>(n.value()));
    }

    SECTION("Remove an attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.runtime_checked_add_or_modify_attrib_local(n.value(), "int_", 133);
        G.update_node(n.value());
        REQUIRE(n->attrs().find("int_") != n->attrs().end());
        REQUIRE(G.remove_attrib_local(n.value(), "int_"));
        REQUIRE(n->attrs().find("int_") == n->attrs().end());
        bool r = G.update_node(n.value());
        REQUIRE(r);
    }

    SECTION("Insert attribute (edge) and insert in G") {
        std::optional<Edge> e = G.get_edge(100, 150, "RT");
        REQUIRE(e.has_value());
        REQUIRE(G.runtime_checked_add_attrib_local(e.value(), "new_int", 123));
        G.insert_or_assign_edge(e.value());
        REQUIRE(e->attrs().find("new_int") != e->attrs().end());
        bool r = G.insert_or_assign_edge(e.value());
        REQUIRE(r);
    }

    SECTION("Runtime insert rt_covariance attribute on RT edge") {
        std::optional<Edge> e = G.get_edge(100, 150, "RT");
        REQUIRE(e.has_value());

        const std::vector<float> covariance(36, 1.25f);
        REQUIRE_NOTHROW(G.runtime_checked_add_or_modify_attrib_local(e.value(), std::string{rt_covariance_str}, covariance));
        REQUIRE(G.insert_or_assign_edge(e.value()));

        auto stored_edge = G.get_edge(100, 150, "RT");
        REQUIRE(stored_edge.has_value());

        auto stored_covariance = G.get_attrib_by_name<rt_covariance_att>(stored_edge.value());
        REQUIRE(stored_covariance.has_value());
        REQUIRE(stored_covariance.value().get() == covariance);
    }

}

TEST_CASE("Other attribute operations and checks", "[ATTRIBUTES]") {

    auto filename = make_edge_config_file();
    DSRGraph G(random_string(10), rand() % 1200, filename);

    SECTION("Get attribute timestamp") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_attrib_local<string__att>(n.value(), std::string("string att"));
        G.update_node(n.value());
        REQUIRE(G.get_attrib_timestamp<string__att>(n.value()).has_value());
        REQUIRE(G.get_attrib_timestamp_by_name(n.value(), "string_").has_value());
    }

    SECTION("Get attribute by name using only the id (no references)") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_or_modify_attrib_local<string__att>(n.value(), std::string("string att"));
        G.update_node(n.value());
        std::string str2 {G.get_attrib_by_name<string__att>(n.value()).value()};
        std::string str1 = G.get_attrib_by_name<string__att>(100).value();

        REQUIRE(str1 == str2);
    }

    SECTION("In add_or_modify_attrib_local, we can pass a reference or move the value") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        std::string stratt("string att lvalue");
        std::string strattmove("string att rvalue");
        G.add_or_modify_attrib_local<string__att>(n.value(), stratt);
        G.update_node(n.value());
        std::string str1 = G.get_attrib_by_name<string__att>(100).value();
        REQUIRE(str1 == stratt);
        G.add_or_modify_attrib_local<string__att>(n.value(), std::move(strattmove));
        G.update_node(n.value());
        str1 = G.get_attrib_by_name<string__att>(100).value();
        REQUIRE(str1 == "string att rvalue");
    }

    SECTION("runtime_checked_add_or_modify_attrib_local is equivalent to add_or_modify_attrib_local") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        std::string stratt("string att lvalue");
        G.add_or_modify_attrib_local<string__att>(n.value(), stratt);
        G.update_node(n.value());
        std::string str1 = G.get_attrib_by_name<string__att>(100).value();
        REQUIRE(str1  == stratt);
        REQUIRE_NOTHROW(G.runtime_checked_add_or_modify_attrib_local(n.value(), std::string{string__str}, stratt));
        G.update_node(n.value());
        str1 = G.get_attrib_by_name<string__att>(100).value();
        REQUIRE(str1  == stratt);
    }

    SECTION("add_attrib_local is equivalent to runtime_checked_add_attrib_local") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        std::string stratt("string att");
        G.remove_attrib_local<string__att>(n.value());
        G.update_node(n.value());
        REQUIRE_FALSE(G.get_attrib_by_name<string__att>(100).has_value());
        REQUIRE(G.add_attrib_local<string__att>(n.value(), stratt));
        REQUIRE_FALSE(G.add_attrib_local<string__att>(n.value(), stratt));
        G.update_node(n.value());
        std::optional<std::string> value1 = G.get_attrib_by_name<string__att>(100);
        REQUIRE(value1.has_value());

        G.remove_attrib_local<string__att>(n.value());
        G.update_node(n.value());
        REQUIRE_FALSE(G.get_attrib_by_name<string__att>(100).has_value());
        REQUIRE(G.runtime_checked_add_attrib_local(n.value(), std::string{string__str}, stratt));
        REQUIRE_FALSE(G.runtime_checked_add_attrib_local(n.value(), std::string{string__str}, stratt));
        G.update_node(n.value());

        std::optional<std::string> value2 = G.get_attrib_by_name<string__att>(100);
        REQUIRE(value2.has_value());
        REQUIRE(value1.value() == value2.value());
    }

    SECTION("modify_attrib_local is equivalent to runtime_checked_modify_attrib_local") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        std::string stratt("string att");
        G.remove_attrib_local<string__att>(n.value());
        G.update_node(n.value());
        REQUIRE_FALSE(G.get_attrib_by_name<string__att>(100).has_value());

        REQUIRE_FALSE(G.modify_attrib_local<string__att>(n.value(), stratt));
        REQUIRE(G.add_attrib_local<string__att>(n.value(), std::string{"initial_value"}));
        REQUIRE(G.get_attrib_by_name<string__att>(n.value()).value().get() == std::string{"initial_value"});
        REQUIRE(G.modify_attrib_local<string__att>(n.value(), stratt));
        REQUIRE(G.get_attrib_by_name<string__att>(n.value()).value().get() == stratt);

        G.update_node(n.value());
        std::optional<std::string> value1 = G.get_attrib_by_name<string__att>(100);
        REQUIRE(value1.has_value());

        G.remove_attrib_local<string__att>(n.value());
        G.update_node(n.value());
        REQUIRE_FALSE(G.get_attrib_by_name<string__att>(100).has_value());

        REQUIRE_FALSE(G.runtime_checked_modify_attrib_local(n.value(), std::string{string__str}, stratt));
        REQUIRE(G.add_attrib_local<string__att>(n.value(), std::string{"initial_value"}));
        REQUIRE(G.get_attrib_by_name<string__att>(n.value()).value().get() == std::string{"initial_value"});
        REQUIRE(G.runtime_checked_modify_attrib_local(n.value(), std::string{string__str}, stratt));
        REQUIRE(G.get_attrib_by_name<string__att>(n.value()).value().get() == stratt);

        G.update_node(n.value());
        std::optional<std::string> value2 = G.get_attrib_by_name<string__att>(100);
        REQUIRE(value2.has_value());
        REQUIRE(value1.value() == value2.value());
    }
}
TEST_CASE("Native types in attributes", "[ATTRIBUTES]") {

    auto filename = make_edge_config_file();
    DSRGraph G(random_string(10), rand() % 1200, filename);

    SECTION("Insert a string attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_attrib_local<string__att>(n.value(), std::string("string att"));
        G.update_node(n.value());

        REQUIRE(n->attrs().find("string_") != n->attrs().end());
        std::optional<Node> n2 = G.get_node(100);
        REQUIRE(n2.has_value());
        REQUIRE(n.value() == n2.value());
    }
    
    SECTION("Insert an int32 attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_attrib_local<int__att>(n.value(),  11);
        G.update_node(n.value());

        REQUIRE(n->attrs().find("int_") != n->attrs().end());
        std::optional<Node> n2 = G.get_node(100);
        REQUIRE(n2.has_value());
        REQUIRE(G.get_attrib_by_name<int__att>( n.value()) == G.get_attrib_by_name<int__att>( n2.value()));
    }

    SECTION("Insert an uint32 attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_attrib_local<uint__att>(n.value(),  11u);
        G.update_node(n.value());

        REQUIRE(n->attrs().find("uint_") != n->attrs().end());
        std::optional<Node> n2 = G.get_node(100);
        REQUIRE(n2.has_value());
        REQUIRE(G.get_attrib_by_name<uint__att>( n.value()) == G.get_attrib_by_name<uint__att>( n2.value()));
    }

    SECTION("Insert an uint64 attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_attrib_local<uint64__att>(n.value(),  static_cast<uint64_t>(1));
        G.update_node(n.value());

        REQUIRE(n->attrs().find("uint64_") != n->attrs().end());
        std::optional<Node> n2 = G.get_node(100);
        REQUIRE(n2.has_value());
        REQUIRE(G.get_attrib_by_name<uint64__att>( n.value()) == G.get_attrib_by_name<uint64__att>( n2.value()));
    }

    SECTION("Insert a float attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_attrib_local<float__att>(n.value(), static_cast<float>(11.0));
        G.update_node(n.value());

        REQUIRE(n->attrs().find("float_") != n->attrs().end());
        std::optional<Node> n2 = G.get_node(100);
        REQUIRE(n2.has_value());
        REQUIRE(G.get_attrib_by_name<float__att>( n.value()) == G.get_attrib_by_name<float__att>( n2.value()));

    }

    SECTION("Insert a float_vector attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_attrib_local<vec_float_att>(n.value(), std::vector<float>{11.0, 167.23, 55.66});
        G.update_node(n.value());

        REQUIRE(n->attrs().find("vec_float") != n->attrs().end());
        std::optional<Node> n2 = G.get_node(100);
        REQUIRE(n2.has_value());
        REQUIRE(G.get_attrib_by_name<vec_float_att>( n.value()).value().get() == G.get_attrib_by_name<vec_float_att>( n2.value()).value().get());
    }

    SECTION("Insert a bool attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_attrib_local<bool__att>(n.value(), true);
        G.update_node(n.value());
        REQUIRE(n->attrs().find("bool_") != n->attrs().end());
        std::optional<Node> n2 = G.get_node(100);
        REQUIRE(n2.has_value());
        REQUIRE(G.get_attrib_by_name<bool__att>( n.value()) == G.get_attrib_by_name<bool__att>( n2.value()));
    }

    SECTION("Insert a byte_vector attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_attrib_local<vec_byte_att>(n.value(), std::vector<uint8_t>{11, 167, 55});
        G.update_node(n.value());

        REQUIRE(n->attrs().find("vec_byte") != n->attrs().end());
        std::optional<Node> n2 = G.get_node(100);
        REQUIRE(n2.has_value());
        REQUIRE(G.get_attrib_by_name<vec_byte_att>( n.value()).value().get() == G.get_attrib_by_name<vec_byte_att>( n2.value()).value().get());
    }

    SECTION("Insert a double attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_attrib_local<doub_att>(n.value(), double{33.3});
        G.update_node(n.value());
        REQUIRE(n->attrs().find("doub") != n->attrs().end());
        std::optional<Node> n2 = G.get_node(100);
        REQUIRE(n2.has_value());
        REQUIRE(G.get_attrib_by_name<doub_att>( n.value()) == G.get_attrib_by_name<doub_att>( n2.value()));
    }

    SECTION("Insert a byte_vector attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_attrib_local<vec_u64_att>(n.value(), std::vector<uint64_t>{11, 167, 55});
        G.update_node(n.value());

        REQUIRE(n->attrs().find("vec_u64") != n->attrs().end());
        std::optional<Node> n2 = G.get_node(100);
        REQUIRE(n2.has_value());
        REQUIRE(G.get_attrib_by_name<vec_u64_att>( n.value()).value().get() == G.get_attrib_by_name<vec_u64_att>( n2.value()).value().get());
    }

    SECTION("Insert an array<float, 2> attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_attrib_local<vec2__att>(n.value(), std::array<float, 2>{11.0, 167.0});
        G.update_node(n.value());

        REQUIRE(n->attrs().find("vec2_") != n->attrs().end());
        std::optional<Node> n2 = G.get_node(100);
        REQUIRE(n2.has_value());
        REQUIRE(G.get_attrib_by_name<vec2__att>( n.value()).value().get() == G.get_attrib_by_name<vec2__att>( n2.value()).value().get());
    }

    SECTION("Insert an array<float, 3> attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_attrib_local<vec3__att>(n.value(), std::array<float, 3>{11.0, 167.0, 0.0});
        G.update_node(n.value());

        REQUIRE(n->attrs().find("vec3_") != n->attrs().end());
        std::optional<Node> n2 = G.get_node(100);
        REQUIRE(n2.has_value());
        REQUIRE(G.get_attrib_by_name<vec3__att>( n.value()).value().get() == G.get_attrib_by_name<vec3__att>( n2.value()).value().get());
    }

    SECTION("Insert an array<float, 4> attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_attrib_local<vec4__att>(n.value(), std::array<float, 4>{11.0, 167.0, 1.0, 0.0});
        G.update_node(n.value());

        REQUIRE(n->attrs().find("vec4_") != n->attrs().end());
        std::optional<Node> n2 = G.get_node(100);
        REQUIRE(n2.has_value());
        REQUIRE(G.get_attrib_by_name<vec4__att>( n.value()).value().get() == G.get_attrib_by_name<vec4__att>( n2.value()).value().get());
    }

    SECTION("Insert an array<float, 6> attribute") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_attrib_local<vec6__att>(n.value(), std::array<float, 6>{0.0, 0.0,11.0, 167.0, 1.0, 1.0});
        G.update_node(n.value());

        REQUIRE(n->attrs().find("vec6_") != n->attrs().end());
        std::optional<Node> n2 = G.get_node(100);
        REQUIRE(n2.has_value());
        REQUIRE(G.get_attrib_by_name<vec6__att>( n.value()).value().get() == G.get_attrib_by_name<vec6__att>( n2.value()).value().get());
    }
    

    SECTION("Insert an array<float, 6> attribute without reference wrapper") {
        std::optional<Node> n = G.get_node(100);
        REQUIRE(n.has_value());
        G.add_attrib_local<att_no_reference_att>(n.value(), std::array<float, 6>{0.0, 0.0,11.0, 167.0, 1.0, 0.0});
        G.update_node(n.value());

        REQUIRE(n->attrs().find("att_no_reference") != n->attrs().end());
        std::optional<Node> n2 = G.get_node(100);
        REQUIRE(n2.has_value());
        REQUIRE(G.get_attrib_by_name<att_no_reference_att>( n.value()).value() == G.get_attrib_by_name<att_no_reference_att>( n2.value()).value());
    
    }
}
