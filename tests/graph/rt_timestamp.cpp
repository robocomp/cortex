
#include "dsr/api/dsr_api.h"
#include "../utils.h"

#include "catch2/catch_test_macros.hpp"
#include "dsr/core/utils.h"

using namespace DSR;

//TODO: add REQUIRES, I'm checking manually
TEST_CASE("RT api timestamp", "[GRAPH][RT]") {
    auto ctx = make_edge_config_file();
    auto id1 = rand() % 1000;
    int argc = 0;
    DSRGraph G(random_string(10), id1, ctx);
    auto node_name = random_string();
    auto n = Node::create<testtype_node_type>(node_name);
    G.add_attrib_local<level_att>(n, 0);
    const std::optional<uint64_t> r  = G.insert_node(n);
    REQUIRE(r.has_value());

    node_name = random_string();
    n = Node::create<testtype_node_type>(node_name);
    const std::optional<uint64_t> r2  = G.insert_node(n);
    REQUIRE(r2.has_value());

    auto timestamp_older = 50000;

    auto rt = G.get_rt_api();
    REQUIRE (rt);
    auto node = G.get_node(*r);
    REQUIRE(node.has_value());
    n = node.value();
    rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{0., 0.2, 0.5}, std::vector<float>{0., 0., 0.});

    node = G.get_node(*r);
    n = node.value();
    auto edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());
    
    std::cout << "IDL::EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";

    rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{7., 7.2, 7.5}, std::vector<float>{5., 5., 5.}, timestamp_older);


    node = G.get_node(*r);
    n = node.value();
    edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());

    std::cout << "IDL::EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";


    rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{13., 12.2, 7.5}, std::vector<float>{5., 3., 2.}, (get_unix_timestamp()/1000000) + 1000);

    node = G.get_node(*r);
    n = node.value();
    edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());

    std::cout << "IDL::EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";


    rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{13., 12.2, 7.5}, std::vector<float>{5., 3., 2.});

    node = G.get_node(*r);
    n = node.value();
    edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());

    std::cout << "IDL::EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";

    rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{13., 12.2, 7.5}, std::vector<float>{5., 3., 2.});

    node = G.get_node(*r);
    n = node.value();
    edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());

    std::cout << "IDL::EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";


    rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{0., 0.2, 0.5}, std::vector<float>{0., 1., 0.}, 3000);

    node = G.get_node(*r);
    n = node.value();
    edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());

    std::cout << "IDL::EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";

    rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{0., 0.2, 0.5}, std::vector<float>{0., 1., 0.}, 60000);

    node = G.get_node(*r);
    n = node.value();
    edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());

    std::cout << "IDL::EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";


    rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{0., 0.2, 0.5}, std::vector<float>{0., 1., 0.});

    node = G.get_node(*r);
    n = node.value();
    edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());

    std::cout << "IDL::EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";

        rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{0., 0.2, 0.5}, std::vector<float>{0., 1., 0.}, 1947239916500);

    node = G.get_node(*r);
    n = node.value();
    edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());

    std::cout << "IDL::EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";
}
