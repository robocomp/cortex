
#include "dsr/api/dsr_api.h"
#include "../utils.h"
#include <cstdint>
#include <optional>

#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators.hpp"

#include "dsr/core/crdt/delta_crdt.h"
#include "dsr/core/types/internal_types.h"
#include "dsr/core/types/common_types.h"
#include "dsr/core/types/crdt_types.h"
#include "dsr/core/types/translator.h"
#include "dsr/core/types/type_checking/dsr_edge_type.h"
#include "dsr/core/types/type_checking/dsr_node_type.h"
#include "dsr/core/types/user_types.h"
#include "dsr/core/rtps/CRDTPubSubTypes.h"

#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>

using namespace DSR;

// Helper: serialize T to a byte buffer and deserialize into a fresh T.
template<typename T>
T roundtrip(const T& src)
{
    eprosima::fastcdr::FastBuffer buf;
    eprosima::fastcdr::Cdr ser(buf, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN);
    ser.serialize_encapsulation();
    src.serialize_impl(ser);

    eprosima::fastcdr::Cdr deser(buf, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN);
    deser.read_encapsulation();
    T dst;
    dst.deserialize_impl(deser);
    return dst;
}


TEST_CASE("NODE: from DSR representation to CRDT and back via serialization", "[TRANSLATION][NODE]"){

    uint32_t agent_id = random_number();
    auto id = random_number();
    auto name = random_string();


    static auto new_attribute_ = [&]() -> std::pair<std::string, DSR::Attribute> {

        const auto val = random_choose(std::vector<ValType>{
            (int)12,
            random_string(),
            std::vector<float>{1.0, 2.0, 3.0}
        });
        Attribute attr(val, random_number(), static_cast<uint32_t>(random_number()));
        return std::make_pair(random_string(), attr);
    };


    auto attributes = GENERATE(std::map<std::string, DSR::Attribute>{},
                               std::map<std::string, DSR::Attribute>{new_attribute_(), new_attribute_(), new_attribute_(), new_attribute_()});

    static auto new_edge_ = [&]() -> std::pair<std::pair<uint64_t, std::string>, DSR::Edge> {

        auto type = random_choose(std::vector<std::string>{"in", "RT", "reachable", "visible"});
        auto to = random_number();
        auto key = std::pair<uint64_t, std::string>{to, type};
        auto edge = Edge();
        edge.to(to);
        edge.from(id);
        edge.type(type);
        edge.agent_id(random_number());
        edge.attrs(attributes);
        return std::make_pair(key, edge);
    };

    auto fano = GENERATE(std::map<std::pair<uint64_t, std::string>, DSR::Edge>{},
                         std::map<std::pair<uint64_t, std::string>, DSR::Edge>{
                            new_edge_(), new_edge_(),new_edge_()
                            });

    SECTION("User Node representation"){

        auto node = Node::create<robot_node_type> (attributes, fano);
        node.id(id);
        node.name(name);
        node.agent_id(agent_id);

        REQUIRE(node.fano() == fano);
        REQUIRE(node.attrs() == attributes);
        REQUIRE(node.id() == id);
        REQUIRE(node.name() == name);
        REQUIRE(node.agent_id() == agent_id);


        SECTION("Copy node — CRDT round-trip"){
            CRDTNode crdt_node = user_node_to_crdt(node);
            REQUIRE(crdt_node.attrs().size() == attributes.size());
            REQUIRE(crdt_node.fano().size() == fano.size());
            REQUIRE(crdt_node.id() == id);
            REQUIRE(crdt_node.name() == name);
            REQUIRE(crdt_node.agent_id() == agent_id);

            mvreg<CRDTNode> mvreg_node;
            auto delta = mvreg_node.write(crdt_node);

            REQUIRE(mvreg_node.read_reg() == crdt_node);
            REQUIRE(delta.read_reg() == crdt_node);

            auto& reg = mvreg_node.read_reg();
            REQUIRE(reg.attrs().size() == attributes.size());
            REQUIRE(reg.fano().size() == fano.size());
            REQUIRE(reg.id() == id);
            REQUIRE(reg.name() == name);
            REQUIRE(reg.agent_id() == agent_id);

            // Serialization round-trip
            CRDTNode rt = roundtrip(reg);
            REQUIRE(rt.attrs().size() == attributes.size());
            REQUIRE(rt.fano().size() == fano.size());
            REQUIRE(rt.id() == id);
            REQUIRE(rt.name() == name);
            REQUIRE(rt.agent_id() == agent_id);
            REQUIRE(rt.type() == robot_node_type::attr_name);
        }

        SECTION("Move node — CRDT round-trip"){

            CRDTNode crdt_node = user_node_to_crdt(std::move(node));
            REQUIRE(crdt_node.attrs().size() == attributes.size());
            REQUIRE(crdt_node.fano().size() == fano.size());
            REQUIRE(crdt_node.id() == id);
            REQUIRE(crdt_node.name() == name);
            REQUIRE(crdt_node.agent_id() == agent_id);

            mvreg<CRDTNode> mvreg_node;
            auto delta = mvreg_node.write(std::move(crdt_node));

            auto& reg = mvreg_node.read_reg();
            REQUIRE(delta.read_reg() == reg);
            REQUIRE(reg.attrs().size() == attributes.size());
            REQUIRE(reg.fano().size() == fano.size());
            REQUIRE(reg.id() == id);
            REQUIRE(reg.name() == name);
            REQUIRE(reg.agent_id() == agent_id);

            // Serialization round-trip
            CRDTNode rt = roundtrip(reg);
            REQUIRE(rt.attrs().size() == attributes.size());
            REQUIRE(rt.fano().size() == fano.size());
            REQUIRE(rt.id() == id);
            REQUIRE(rt.name() == name);
            REQUIRE(rt.agent_id() == agent_id);
            REQUIRE(rt.type() == robot_node_type::attr_name);
        }

    }


}



TEST_CASE("EDGE: from DSR representation to CRDT and back via serialization", "[TRANSLATION][EDGE]"){

    uint32_t agent_id = random_number();
    auto from = random_number();
    auto to = random_number();


    static auto new_attribute_ = [&]() -> std::pair<std::string, DSR::Attribute> {

        auto val = random_choose(std::vector<ValType>{
            (int)12,
            random_string(),
            std::vector<float>{1.0, 2.0, 3.0}
        });
        Attribute attr(val, random_number(), static_cast<uint32_t>(random_number()));
        return std::make_pair(random_string(), attr);
    };


    auto attributes = GENERATE(std::map<std::string, DSR::Attribute>{},
                               std::map<std::string, DSR::Attribute>{new_attribute_(), new_attribute_(), new_attribute_(), new_attribute_()});

    SECTION("User Edge representation"){

        auto edge = Edge::create<in_edge_type> (from, to, attributes);
        edge.agent_id(agent_id);

        REQUIRE(edge.attrs() == attributes);
        REQUIRE(edge.from() == from);
        REQUIRE(edge.to() == to);
        REQUIRE(edge.type() == in_edge_type_str);
        REQUIRE(edge.agent_id() == agent_id);


        SECTION("Copy edge — CRDT round-trip"){
            CRDTEdge crdt_edge = user_edge_to_crdt(edge);
            REQUIRE(crdt_edge.attrs().size() == attributes.size());
            REQUIRE(crdt_edge.from() == from);
            REQUIRE(crdt_edge.to() == to);
            REQUIRE(crdt_edge.type() == in_edge_type_str);
            REQUIRE(crdt_edge.agent_id() == agent_id);

            mvreg<CRDTEdge> mvreg_edge;
            auto delta = mvreg_edge.write(crdt_edge);

            REQUIRE(mvreg_edge.read_reg() == crdt_edge);
            REQUIRE(delta.read_reg() == crdt_edge);

            auto& reg = mvreg_edge.read_reg();
            REQUIRE(reg.attrs().size() == attributes.size());
            REQUIRE(reg.from() == from);
            REQUIRE(reg.to() == to);
            REQUIRE(reg.type() == in_edge_type_str);
            REQUIRE(reg.agent_id() == agent_id);

            // Serialization round-trip
            CRDTEdge rt = roundtrip(reg);
            REQUIRE(rt.attrs().size() == attributes.size());
            REQUIRE(rt.from() == from);
            REQUIRE(rt.to() == to);
            REQUIRE(rt.agent_id() == agent_id);
            REQUIRE(rt.type() == in_edge_type_str);
        }

        SECTION("Move edge — CRDT round-trip"){

            CRDTEdge crdt_edge = user_edge_to_crdt(edge);
            REQUIRE(crdt_edge.attrs().size() == attributes.size());
            REQUIRE(crdt_edge.from() == from);
            REQUIRE(crdt_edge.to() == to);
            REQUIRE(crdt_edge.type() == in_edge_type_str);
            REQUIRE(crdt_edge.agent_id() == agent_id);

            mvreg<CRDTEdge> mvreg_edge;
            auto delta = mvreg_edge.write(crdt_edge);

            REQUIRE(delta.read_reg() == mvreg_edge.read_reg());

            auto& reg = mvreg_edge.read_reg();
            REQUIRE(reg.attrs().size() == attributes.size());
            REQUIRE(reg.from() == from);
            REQUIRE(reg.to() == to);
            REQUIRE(reg.type() == in_edge_type_str);
            REQUIRE(reg.agent_id() == agent_id);

            // Serialization round-trip
            CRDTEdge rt = roundtrip(reg);
            REQUIRE(rt.attrs().size() == attributes.size());
            REQUIRE(rt.from() == from);
            REQUIRE(rt.to() == to);
            REQUIRE(rt.agent_id() == agent_id);
            REQUIRE(rt.type() == in_edge_type_str);
        }

    }


}
