
#include "dsr/api/dsr_api.h"
#include "../utils.h"

#include "catch2/catch_test_macros.hpp"

#include <cmath>
#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
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

TEST_CASE("RT api interpolation mode", "[GRAPH][RT]") {
    auto ctx = make_edge_config_file();
    auto id1 = rand() % 1000;
    DSRGraph G(random_string(10), id1, ctx);

    auto root = G.get_node("root");
    REQUIRE(root.has_value());

    auto room = G.get_node("room");
    REQUIRE(room.has_value());

    auto rt = G.get_rt_api();
    REQUIRE(rt);

    rt->insert_or_assign_edge_RT(root.value(), room->id(), std::vector<float>{0.f, 0.f, 0.f}, std::vector<float>{0.f, 0.f, 0.f}, 1000);
    rt->insert_or_assign_edge_RT(root.value(), room->id(), std::vector<float>{10.f, 0.f, 0.f}, std::vector<float>{0.f, 0.f, static_cast<float>(M_PI_2)}, 2000);

    auto nearest_translation = rt->get_translation(root->id(), room->id(), 1400);
    REQUIRE(nearest_translation.has_value());
    CHECK(std::abs(nearest_translation->x()) < 1e-9);

    auto interpolated_translation = rt->get_translation(root->id(), room->id(), 1500, RT_API::TimeQuery::Interpolated);
    REQUIRE(interpolated_translation.has_value());
    CHECK(std::abs(interpolated_translation->x() - 5.0) < 1e-9);

    auto edge = G.get_edge(root->id(), room->id(), "RT");
    REQUIRE(edge.has_value());

    auto interpolated_rt = rt->get_edge_RT_as_rtmat(edge.value(), 1500, RT_API::TimeQuery::Interpolated);
    REQUIRE(interpolated_rt.has_value());
    CHECK(std::abs(interpolated_rt->matrix()(0, 3) - 5.0) < 1e-9);

    const auto rotated_x = interpolated_rt->rotation() * Eigen::Vector3d::UnitX();
    CHECK(std::abs(rotated_x.x() - std::sqrt(0.5)) < 1e-7);
    CHECK(std::abs(rotated_x.y() - std::sqrt(0.5)) < 1e-7);
}

TEST_CASE("RT api covariance timestamp queries", "[GRAPH][RT]") {
    auto ctx = make_edge_config_file();
    auto id1 = rand() % 1000;
    DSRGraph G(random_string(10), id1, ctx);

    auto root = G.get_node("root");
    REQUIRE(root.has_value());

    auto room = G.get_node("room");
    REQUIRE(room.has_value());

    auto rt = G.get_rt_api();
    REQUIRE(rt);

    const std::vector<float> covariance_1(36, 1.f);
    const std::vector<float> covariance_2(36, 3.f);

    rt->insert_or_assign_edge_RT(root.value(), room->id(), std::vector<float>{0.f, 0.f, 0.f}, std::vector<float>{0.f, 0.f, 0.f}, covariance_1, 1000);
    rt->insert_or_assign_edge_RT(root.value(), room->id(), std::vector<float>{10.f, 0.f, 0.f}, std::vector<float>{0.f, 0.f, 0.f}, covariance_2, 2000);

    auto edge = G.get_edge(root->id(), room->id(), "RT");
    REQUIRE(edge.has_value());

    auto latest_covariance = rt->get_edge_RT_covariance(edge.value());
    REQUIRE(latest_covariance.has_value());
    CHECK(std::abs((*latest_covariance)(0, 0) - 3.0) < 1e-9);
    CHECK(std::abs((*latest_covariance)(5, 5) - 3.0) < 1e-9);

    auto nearest_covariance = rt->get_covariance_matrix(root->id(), room->id(), 1400);
    REQUIRE(nearest_covariance.has_value());
    CHECK(std::abs((*nearest_covariance)(0, 0) - 1.0) < 1e-9);
    CHECK(std::abs((*nearest_covariance)(4, 4) - 1.0) < 1e-9);

    auto interpolated_covariance = rt->get_covariance_matrix(root->id(), room->id(), 1500, RT_API::TimeQuery::Interpolated);
    REQUIRE(interpolated_covariance.has_value());
    CHECK(std::abs((*interpolated_covariance)(0, 0) - 2.0) < 1e-9);
    CHECK(std::abs((*interpolated_covariance)(3, 3) - 2.0) < 1e-9);
    CHECK(std::abs((*interpolated_covariance)(5, 5) - 2.0) < 1e-9);
}

TEST_CASE("InnerEigen historical queries bypass cache", "[GRAPH][RT][INNER]") {
    auto ctx = make_edge_config_file();
    auto id1 = rand() % 1000;
    DSRGraph G(random_string(10), id1, ctx);

    auto root = G.get_node("root");
    REQUIRE(root.has_value());

    auto room = G.get_node("room");
    REQUIRE(room.has_value());

    auto rt = G.get_rt_api();
    REQUIRE(rt);

    rt->insert_or_assign_edge_RT(root.value(), room->id(), std::vector<float>{1.f, 0.f, 0.f}, std::vector<float>{0.f, 0.f, 0.f}, 1000);
    rt->insert_or_assign_edge_RT(root.value(), room->id(), std::vector<float>{10.f, 0.f, 0.f}, std::vector<float>{0.f, 0.f, 0.f}, 2000);

    auto inner = G.get_inner_eigen_api();
    REQUIRE(inner);

    auto old_transform = inner->get_translation_vector("room", "root", 1000);
    REQUIRE(old_transform.has_value());
    CHECK(std::abs(old_transform->x() + 1.0) < 1e-9);

    auto new_transform = inner->get_translation_vector("room", "root", 2000);
    REQUIRE(new_transform.has_value());
    CHECK(std::abs(new_transform->x() + 10.0) < 1e-9);

    auto latest_transform = inner->get_translation_vector("room", "root");
    REQUIRE(latest_transform.has_value());
    CHECK(std::abs(latest_transform->x() + 10.0) < 1e-9);
}

TEST_CASE("InnerEigen latest queries do not pollute historical queries", "[GRAPH][RT][INNER]") {
    auto ctx = make_edge_config_file();
    auto id1 = rand() % 1000;
    DSRGraph G(random_string(10), id1, ctx);

    auto root = G.get_node("root");
    REQUIRE(root.has_value());

    auto room = G.get_node("room");
    REQUIRE(room.has_value());

    auto rt = G.get_rt_api();
    REQUIRE(rt);

    rt->insert_or_assign_edge_RT(root.value(), room->id(), std::vector<float>{1.f, 0.f, 0.f}, std::vector<float>{0.f, 0.f, 0.f}, 1000);
    rt->insert_or_assign_edge_RT(root.value(), room->id(), std::vector<float>{10.f, 0.f, 0.f}, std::vector<float>{0.f, 0.f, 0.f}, 2000);

    auto inner = G.get_inner_eigen_api();
    REQUIRE(inner);

    auto latest_transform = inner->get_translation_vector("room", "root");
    REQUIRE(latest_transform.has_value());
    CHECK(std::abs(latest_transform->x() + 10.0) < 1e-9);

    auto old_transform = inner->get_translation_vector("room", "root", 1000);
    REQUIRE(old_transform.has_value());
    CHECK(std::abs(old_transform->x() + 1.0) < 1e-9);

    auto new_transform = inner->get_translation_vector("room", "root", 2000);
    REQUIRE(new_transform.has_value());
    CHECK(std::abs(new_transform->x() + 10.0) < 1e-9);
}

TEST_CASE("InnerEigen queued invalidation refreshes live cache after RT update", "[GRAPH][RT][INNER]") {
    int argc = 0;
    QCoreApplication app(argc, nullptr);

    auto ctx = make_edge_config_file();
    auto id1 = rand() % 1000;
    DSRGraph G(random_string(10), id1, ctx);

    auto root = G.get_node("root");
    REQUIRE(root.has_value());

    auto room = G.get_node("room");
    REQUIRE(room.has_value());

    auto rt = G.get_rt_api();
    REQUIRE(rt);

    rt->insert_or_assign_edge_RT(root.value(), room->id(), std::vector<float>{10.f, 0.f, 0.f}, std::vector<float>{0.f, 0.f, 0.f});

    auto inner = G.get_inner_eigen_api();
    REQUIRE(inner);

    auto cached_transform = inner->get_translation_vector("room", "root");
    REQUIRE(cached_transform.has_value());
    CHECK(std::abs(cached_transform->x() + 10.0) < 1e-9);

    rt->insert_or_assign_edge_RT(root.value(), room->id(), std::vector<float>{20.f, 0.f, 0.f}, std::vector<float>{0.f, 0.f, 0.f});

    auto stale_transform = inner->get_translation_vector("room", "root");
    REQUIRE(stale_transform.has_value());
    CHECK(std::abs(stale_transform->x() + 10.0) < 1e-9);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents();

    auto refreshed_transform = inner->get_translation_vector("room", "root");
    REQUIRE(refreshed_transform.has_value());
    CHECK(std::abs(refreshed_transform->x() + 20.0) < 1e-9);
}

TEST_CASE("InnerEigen interpolation mode on RT chain", "[GRAPH][RT][INNER]") {
    auto ctx = make_edge_config_file();
    auto id1 = rand() % 1000;
    DSRGraph G(random_string(10), id1, ctx);

    auto root = G.get_node("root");
    REQUIRE(root.has_value());

    auto room = G.get_node("room");
    REQUIRE(room.has_value());

    auto robot = G.get_node("Shadow");
    REQUIRE(robot.has_value());

    auto rt = G.get_rt_api();
    REQUIRE(rt);

    rt->insert_or_assign_edge_RT(root.value(), room->id(), std::vector<float>{10.f, 0.f, 0.f}, std::vector<float>{0.f, 0.f, 0.f}, 1000);
    rt->insert_or_assign_edge_RT(root.value(), room->id(), std::vector<float>{20.f, 0.f, 0.f}, std::vector<float>{0.f, 0.f, 0.f}, 2000);

    auto fresh_room = G.get_node("room");
    REQUIRE(fresh_room.has_value());
    rt->insert_or_assign_edge_RT(fresh_room.value(), robot->id(), std::vector<float>{1.f, 0.f, 0.f}, std::vector<float>{0.f, 0.f, 0.f}, 1000);

    fresh_room = G.get_node("room");
    REQUIRE(fresh_room.has_value());
    rt->insert_or_assign_edge_RT(fresh_room.value(), robot->id(), std::vector<float>{3.f, 0.f, 0.f}, std::vector<float>{0.f, 0.f, 0.f}, 2000);

    auto inner = G.get_inner_eigen_api();
    REQUIRE(inner);

    auto interpolated_transform = inner->get_translation_vector("Shadow", "root", 1500, "RT", RT_API::TimeQuery::Interpolated);
    REQUIRE(interpolated_transform.has_value());
    CHECK(std::abs(interpolated_transform->x() + 17.0) < 1e-9);

    auto nearest_transform = inner->get_translation_vector("Shadow", "root", 1500, "RT", RT_API::TimeQuery::Nearest);
    REQUIRE(nearest_transform.has_value());
    CHECK(std::abs(nearest_transform->x() + 11.0) < 1e-9);
}
