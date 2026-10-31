
#include "dsr/api/dsr_api.h"
#include "../utils.h"

#include "catch2/catch_test_macros.hpp"
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include "dsr/core/utils.h"

using namespace DSR;

//TODO: add REQUIRES, I'm checking manually
TEST_CASE("RT api timestamp", "[GRAPH][RT]") {
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    INFO("sync_mode=" << sync_mode_label(sync_mode));
    auto ctx = make_edge_config_file();
    auto id1 = rand() % 1000;
    DSRGraph G(make_test_graph_settings(random_string(10), id1, ctx, true, 0, SignalMode::QT, sync_mode));
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
    
    std::cout << "EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";

    rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{7., 7.2, 7.5}, std::vector<float>{5., 5., 5.}, timestamp_older);


    node = G.get_node(*r);
    n = node.value();
    edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());

    std::cout << "EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";


    rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{13., 12.2, 7.5}, std::vector<float>{5., 3., 2.}, (get_unix_timestamp()/1000000) + 1000);

    node = G.get_node(*r);
    n = node.value();
    edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());

    std::cout << "EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";


    rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{13., 12.2, 7.5}, std::vector<float>{5., 3., 2.});

    node = G.get_node(*r);
    n = node.value();
    edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());

    std::cout << "EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";

    rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{13., 12.2, 7.5}, std::vector<float>{5., 3., 2.});

    node = G.get_node(*r);
    n = node.value();
    edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());

    std::cout << "EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";


    rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{0., 0.2, 0.5}, std::vector<float>{0., 1., 0.}, 3000);

    node = G.get_node(*r);
    n = node.value();
    edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());

    std::cout << "EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";

    rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{0., 0.2, 0.5}, std::vector<float>{0., 1., 0.}, 60000);

    node = G.get_node(*r);
    n = node.value();
    edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());

    std::cout << "EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";


    rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{0., 0.2, 0.5}, std::vector<float>{0., 1., 0.});

    node = G.get_node(*r);
    n = node.value();
    edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());

    std::cout << "EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
            << " Attribs:[";
    for (const auto &v : edge_rt->attrs())
        std::cout << v.first << ":" << v.second << " - \n";
    std::cout << "]]\n";

        rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{0., 0.2, 0.5}, std::vector<float>{0., 1., 0.}, 1947239916500);

    node = G.get_node(*r);
    n = node.value();
    edge_rt = rt->get_edge_RT(n, *r2);
    REQUIRE(edge_rt.has_value());

    std::cout << "EdgeAttribs[" << edge_rt->type() << ", from:" << std::to_string(edge_rt->from()) << "-> to:" << std::to_string(edge_rt->to())
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

    const Eigen::Vector3d rotated_x = interpolated_rt->rotation() * Eigen::Vector3d::UnitX();
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

// rewrite_edge_RT_since: a late correction replaces the predictions written after its stamp, in one publish.
// Own nodes (like "RT api timestamp"): the root/room tests above depend on the shipped test graph.
TEST_CASE("RT api rewrite since", "[GRAPH][RT]") {
    auto ctx = make_edge_config_file();
    DSRGraph G(make_test_graph_settings(random_string(10), rand() % 1000, ctx, true, 0, SignalMode::QT, SyncMode::CRDT));
    auto a = Node::create<testtype_node_type>(random_string());
    G.add_attrib_local<level_att>(a, 0);
    const auto ida = G.insert_node(a);
    REQUIRE(ida.has_value());
    auto b = Node::create<testtype_node_type>(random_string());
    const auto idb = G.insert_node(b);
    REQUIRE(idb.has_value());
    auto rt = G.get_rt_api();
    REQUIRE(rt);
    rt->HISTORY_SIZE = 5;

    const auto parent = [&] { auto n = G.get_node(*ida); REQUIRE(n.has_value()); return n.value(); };
    const auto edge = [&] { auto e = G.get_edge(*ida, *idb, "RT"); REQUIRE(e.has_value()); return e.value(); };
    const auto block = [](float x, float vx) {
        return RT_API::RTBlock{.translation = {x, 0.f, 0.f}, .rotation_euler = {0.f, 0.f, 0.f},
                               .twist_linear = std::vector<float>{vx, 0.f, 0.f},
                               .twist_angular = std::vector<float>{0.f, 0.f, 0.f}};
    };
    const auto write = [&](float x, std::uint64_t ts) { auto n = parent(); rt->insert_or_assign_edge_RT(n, *idb, block(x, 0.f), ts); };
    const auto rewrite = [&](std::vector<std::pair<std::uint64_t, RT_API::RTBlock>> v) { auto n = parent(); rt->rewrite_edge_RT_since(n, *idb, std::move(v)); };
    const auto x_at = [&](std::uint64_t ts) {
        auto m = rt->get_edge_RT_as_rtmat(edge(), ts, RT_API::TimeQuery::Interpolated);
        REQUIRE(m.has_value());
        return m->translation().x();
    };
    const auto stamps = [&] {
        const auto e = edge();   // get_attrib_by_name returns a reference into it: keep it alive
        const auto ts = G.get_attrib_by_name<rt_timestamps_att>(e);
        REQUIRE(ts.has_value());
        std::vector<std::uint64_t> v(ts->get().begin(), ts->get().end());
        std::erase(v, 0);
        std::ranges::sort(v);
        return v;
    };

    // predictions at 1000..1300 (x = 0, 1, 2, 3)
    for (int k = 0; k < 4; ++k)
        write(static_cast<float>(k), 1000 + 100 * k);
    REQUIRE(stamps() == std::vector<std::uint64_t>{1000, 1100, 1200, 1300});
    CHECK(std::abs(x_at(1250) - 2.5) < 1e-6);

    // the correction for 1100 arrives late, with the re-propagated newest (1300); 1200 is superseded
    rewrite({{1300, block(30.f, 3.f)}, {1100, block(10.f, 1.f)}});
    CHECK(stamps() == std::vector<std::uint64_t>{1000, 1100, 1300});
    CHECK(std::abs(x_at(1200) - 20.0) < 1e-6);   // between the correction and the re-propagated pose
    CHECK(std::abs(x_at(1050) - 5.0) < 1e-6);    // the block before `since` is kept
    CHECK(std::abs(x_at(0) - 30.0) < 1e-6);      // timestamp 0 = the newest, not the last written
    {
        const auto e = edge();
        const auto ts = G.get_attrib_by_name<rt_timestamps_att>(e);
        const auto tw = G.get_attrib_by_name<rt_twist_linear_att>(e);
        REQUIRE((ts.has_value() and tw.has_value()));
        const auto i = std::ranges::find(ts->get(), std::uint64_t{1300}) - ts->get().begin();
        CHECK(std::abs(tw->get()[3 * i] - 3.f) < 1e-6);   // the twist moved with its pose
    }

    // ordinary writes continue after the newest, and the ring evicts oldest first
    write(40.f, 1400);
    CHECK(std::abs(x_at(0) - 40.0) < 1e-6);
    write(50.f, 1500);
    write(60.f, 1600);
    CHECK(stamps() == std::vector<std::uint64_t>{1100, 1300, 1400, 1500, 1600});
    CHECK(std::abs(x_at(0) - 60.0) < 1e-6);

    // a repeated stamp keeps the last one given; a rewrite longer than the ring keeps the newest H
    rewrite({{1500, block(55.f, 0.f)}, {1500, block(56.f, 0.f)}});
    CHECK(stamps() == std::vector<std::uint64_t>{1100, 1300, 1400, 1500});
    CHECK(std::abs(x_at(1500) - 56.0) < 1e-6);
    std::vector<std::pair<std::uint64_t, RT_API::RTBlock>> many;
    for (int k = 0; k < 7; ++k)
        many.emplace_back(2000 + 10 * k, block(static_cast<float>(k), 0.f));
    rewrite(many);
    CHECK(stamps() == std::vector<std::uint64_t>{2020, 2030, 2040, 2050, 2060});
    CHECK(std::abs(x_at(0) - 6.0) < 1e-6);
    write(7.f, 2070);                                     // full ring: evicts 2020
    CHECK(stamps() == std::vector<std::uint64_t>{2030, 2040, 2050, 2060, 2070});

    // a malformed block is refused before anything is written
    CHECK_THROWS(rewrite({{2080, RT_API::RTBlock{.translation = {1.f, 2.f}, .rotation_euler = {0.f, 0.f, 0.f}}}}));
    CHECK(stamps() == std::vector<std::uint64_t>{2030, 2040, 2050, 2060, 2070});
}
