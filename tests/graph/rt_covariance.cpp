
#include "dsr/api/dsr_api.h"
#include "../utils.h"

#include "catch2/catch_test_macros.hpp"
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

using namespace DSR;

namespace
{
    Eigen::Matrix<double, 6, 6> filled_covariance(double seed)
    {
        Eigen::Matrix<double, 6, 6> covariance = Eigen::Matrix<double, 6, 6>::Zero();
        for (int row = 0; row < 6; ++row)
            for (int col = 0; col < 6; ++col)
                covariance(row, col) = seed + row * 6 + col;
        return covariance;
    }
}

TEST_CASE("RT covariance read/write through RT_API", "[GRAPH][RT][COVARIANCE]")
{
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    INFO("sync_mode=" << sync_mode_label(sync_mode));
    auto ctx = make_edge_config_file();
    DSRGraph G(make_test_graph_settings(random_string(10), rand() % 1000, ctx, true, 0, SignalMode::QT, sync_mode));

    auto parent = Node::create<testtype_node_type>(random_string());
    G.add_attrib_local<level_att>(parent, 0);
    const auto parent_id = G.insert_node(parent);
    REQUIRE(parent_id.has_value());

    auto child = Node::create<testtype_node_type>(random_string());
    const auto child_id = G.insert_node(child);
    REQUIRE(child_id.has_value());

    auto rt = G.get_rt_api();
    REQUIRE(rt);

    auto parent_node = G.get_node(*parent_id);
    REQUIRE(parent_node.has_value());
    rt->insert_or_assign_edge_RT(parent_node.value(), *child_id, std::vector<float>{1.f, 2.f, 3.f}, std::vector<float>{0.f, 0.f, 0.f});

    SECTION("Velocity and acceleration covariances round-trip independently")
    {
        const auto velocity = filled_covariance(100.0);
        const auto acceleration = filled_covariance(200.0);

        REQUIRE(rt->insert_or_assign_edge_RT_covariance(*parent_id, *child_id, RT_API::CovarianceKind::Velocity, velocity));
        REQUIRE(rt->insert_or_assign_edge_RT_covariance(*parent_id, *child_id, RT_API::CovarianceKind::Acceleration, acceleration));

        const auto read_velocity = rt->get_covariance_matrix(*parent_id, *child_id, 0, RT_API::TimeQuery::Nearest,
                                                             RT_API::CovarianceKind::Velocity);
        REQUIRE(read_velocity.has_value());
        CHECK((read_velocity.value() - velocity).cwiseAbs().maxCoeff() < 1e-4);

        const auto read_acceleration = rt->get_covariance_matrix(*parent_id, *child_id, 0, RT_API::TimeQuery::Nearest,
                                                                 RT_API::CovarianceKind::Acceleration);
        REQUIRE(read_acceleration.has_value());
        CHECK((read_acceleration.value() - acceleration).cwiseAbs().maxCoeff() < 1e-4);

        // Writing velocity/acceleration must not manufacture a pose covariance.
        CHECK_FALSE(rt->get_covariance_matrix(*parent_id, *child_id, 0, RT_API::TimeQuery::Nearest,
                                              RT_API::CovarianceKind::Pose).has_value());
    }

    SECTION("Pose covariance lands on the slot of the latest transform")
    {
        const auto pose = filled_covariance(1.0);
        REQUIRE(rt->insert_or_assign_edge_RT_covariance(*parent_id, *child_id, RT_API::CovarianceKind::Pose, pose));

        const auto read_pose = rt->get_covariance_matrix(*parent_id, *child_id);
        REQUIRE(read_pose.has_value());
        CHECK((read_pose.value() - pose).cwiseAbs().maxCoeff() < 1e-4);

        // A standalone covariance write must not disturb the pose payload or the ring bookkeeping.
        const auto translation = rt->get_translation(*parent_id, *child_id);
        REQUIRE(translation.has_value());
        CHECK(std::abs(translation.value().x() - 1.0) < 1e-4);
        CHECK(std::abs(translation.value().y() - 2.0) < 1e-4);
        CHECK(std::abs(translation.value().z() - 3.0) < 1e-4);
    }

    SECTION("A 36-float vector is accepted and anything else throws")
    {
        const std::vector<float> ok(36, 0.25f);
        REQUIRE(rt->insert_or_assign_edge_RT_covariance(*parent_id, *child_id, RT_API::CovarianceKind::Velocity, ok));

        const std::vector<float> too_short(9, 0.25f);
        REQUIRE_THROWS_AS(rt->insert_or_assign_edge_RT_covariance(*parent_id, *child_id, RT_API::CovarianceKind::Velocity, too_short),
                          std::runtime_error);
    }

    SECTION("Writing on a missing RT edge reports failure instead of creating one")
    {
        auto orphan = Node::create<testtype_node_type>(random_string());
        const auto orphan_id = G.insert_node(orphan);
        REQUIRE(orphan_id.has_value());

        CHECK_FALSE(rt->insert_or_assign_edge_RT_covariance(*parent_id, *orphan_id, RT_API::CovarianceKind::Velocity,
                                                            filled_covariance(0.0)));
        CHECK_FALSE(G.get_edge(*parent_id, *orphan_id, "RT").has_value());
    }
}
