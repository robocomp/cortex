#include "dsr/api/dsr_api.h"
#include "../utils.h"

#include "catch2/catch_test_macros.hpp"

#include <random>

using namespace DSR;

namespace
{
    using LocalRTMat = Eigen::Transform<double, 3, Eigen::Affine, Eigen::DontAlign>;

    Cov6d diagonal_covariance(double tx, double ty, double tz, double rx, double ry, double rz)
    {
        Cov6d covariance = Cov6d::Zero();
        covariance(0, 0) = tx;
        covariance(1, 1) = ty;
        covariance(2, 2) = tz;
        covariance(3, 3) = rx;
        covariance(4, 4) = ry;
        covariance(5, 5) = rz;
        return covariance;
    }

    std::vector<float> covariance_to_vector(const Cov6d &covariance)
    {
        std::vector<float> packed(36, 0.f);
        for (int row = 0; row < 6; ++row)
            for (int col = 0; col < 6; ++col)
                packed[static_cast<std::size_t>(row * 6 + col)] = static_cast<float>(covariance(row, col));
        return packed;
    }

    Cov6d diagonal_square_root(const Cov6d &covariance)
    {
        Cov6d square_root = Cov6d::Zero();
        for (int diagonal = 0; diagonal < 6; ++diagonal)
            square_root(diagonal, diagonal) = std::sqrt(covariance(diagonal, diagonal));
        return square_root;
    }

    Cov3d diagonal_square_root(const Cov3d &covariance)
    {
        Cov3d square_root = Cov3d::Zero();
        for (int diagonal = 0; diagonal < 3; ++diagonal)
            square_root(diagonal, diagonal) = std::sqrt(covariance(diagonal, diagonal));
        return square_root;
    }

    double wrap_angle_local(double angle)
    {
        while (angle > M_PI)
            angle -= 2.0 * M_PI;
        while (angle < -M_PI)
            angle += 2.0 * M_PI;
        return angle;
    }

    Mat::Vector6d pose_difference(const Mat::Vector6d &lhs, const Mat::Vector6d &rhs)
    {
        Mat::Vector6d difference = lhs - rhs;
        for (int axis = 3; axis < 6; ++axis)
            difference(axis) = wrap_angle_local(difference(axis));
        return difference;
    }

    Eigen::Vector3d pose2d_difference_local(const Eigen::Vector3d &lhs, const Eigen::Vector3d &rhs)
    {
        Eigen::Vector3d difference = lhs - rhs;
        difference(2) = wrap_angle_local(difference(2));
        return difference;
    }

    Eigen::Vector3d pose_vector_to_pose2d(const Mat::Vector6d &pose)
    {
        return Eigen::Vector3d{pose(0), pose(1), pose(5)};
    }

    LocalRTMat pose_vector_to_rtmat_local(const Mat::Vector6d &pose)
    {
        auto rtmat = LocalRTMat::Identity();
        rtmat.translate(pose.head<3>());
        rtmat.rotate(Eigen::AngleAxisd(pose(3), Eigen::Vector3d::UnitX()) *
                     Eigen::AngleAxisd(pose(4), Eigen::Vector3d::UnitY()) *
                     Eigen::AngleAxisd(pose(5), Eigen::Vector3d::UnitZ()));
        return rtmat;
    }

    Mat::Vector6d rtmat_to_pose_vector_local(const LocalRTMat &rtmat)
    {
        Mat::Vector6d pose;
        const auto angles = rtmat.rotation().eulerAngles(0, 1, 2);
        pose << rtmat.translation().x(), rtmat.translation().y(), rtmat.translation().z(),
                angles.x(), angles.y(), angles.z();
        return pose;
    }

    Mat::Vector6d sample_standard_normal(std::mt19937 &generator)
    {
        static std::normal_distribution<double> distribution(0.0, 1.0);
        Mat::Vector6d sample;
        for (int index = 0; index < sample.size(); ++index)
            sample(index) = distribution(generator);
        return sample;
    }

    Mat::Vector3d sample_standard_normal3(std::mt19937 &generator)
    {
        static std::normal_distribution<double> distribution(0.0, 1.0);
        Mat::Vector3d sample;
        for (int index = 0; index < sample.size(); ++index)
            sample(index) = distribution(generator);
        return sample;
    }
}

TEST_CASE("InnerGaussian propagates RT chain covariance", "[GRAPH][RT][GAUSSIAN]")
{
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

    const auto root_room_cov = diagonal_covariance(0.25, 0.0, 0.0, 0.0, 0.0, 0.0);
    const auto room_robot_cov = diagonal_covariance(0.75, 0.0, 0.0, 0.0, 0.0, 0.0);

    rt->insert_or_assign_edge_RT(root.value(), room->id(),
                                 std::vector<float>{10.f, 0.f, 0.f},
                                 std::vector<float>{0.f, 0.f, 0.f},
                                 covariance_to_vector(root_room_cov), 1000);
    rt->insert_or_assign_edge_RT(room.value(), robot->id(),
                                 std::vector<float>{2.f, 0.f, 0.f},
                                 std::vector<float>{0.f, 0.f, 0.f},
                                 covariance_to_vector(room_robot_cov), 1000);

    auto root_room_covariance = rt->get_covariance_matrix(root->id(), room->id(), 1000);
    REQUIRE(root_room_covariance.has_value());
    CHECK(std::abs((*root_room_covariance)(0, 0) - 0.25) < 1e-6);

    auto room_robot_covariance = rt->get_covariance_matrix(room->id(), robot->id(), 1000);
    REQUIRE(room_robot_covariance.has_value());
    CHECK(std::abs((*room_robot_covariance)(0, 0) - 0.75) < 1e-6);

    auto gaussian = G.get_inner_gaussian_api();
    REQUIRE(gaussian);

    auto transform = gaussian->get_transformation_gaussian("Shadow", "root", 1000);
    REQUIRE(transform.has_value());
    CHECK(std::abs(transform->mean(0) + 12.0) < 1e-6);
    CHECK(std::abs(transform->mean(1)) < 1e-6);
    CHECK(std::abs(transform->covariance(0, 0) - 1.0) < 1e-4);
    CHECK(std::abs(transform->covariance(1, 1)) < 1e-6);
}

TEST_CASE("InnerGaussian transforms points and poses", "[GRAPH][RT][GAUSSIAN]")
{
    auto ctx = make_edge_config_file();
    auto id1 = rand() % 1000;
    DSRGraph G(random_string(10), id1, ctx);

    auto root = G.get_node("root");
    REQUIRE(root.has_value());

    auto room = G.get_node("room");
    REQUIRE(room.has_value());

    auto rt = G.get_rt_api();
    REQUIRE(rt);

    const auto edge_cov = diagonal_covariance(0.5, 0.25, 0.0, 0.0, 0.0, 0.04);
    rt->insert_or_assign_edge_RT(root.value(), room->id(),
                                 std::vector<float>{4.f, 0.f, 0.f},
                                 std::vector<float>{0.f, 0.f, 0.f},
                                 covariance_to_vector(edge_cov), 1000);

    auto gaussian = G.get_inner_gaussian_api();
    REQUIRE(gaussian);

    GaussianPoint3D point;
    point.mean << 1.0, 2.0, 0.0;
    point.covariance.diagonal() << 0.1, 0.2, 0.3;

    auto transformed_point = gaussian->transform_point("room", point, "root", 1000);
    REQUIRE(transformed_point.has_value());
    CHECK(std::abs(transformed_point->mean.x() + 3.0) < 1e-6);
    CHECK(std::abs(transformed_point->mean.y() - 2.0) < 1e-6);
    CHECK(transformed_point->covariance(0, 0) > point.covariance(0, 0));

    GaussianPose2D pose2d;
    pose2d.mean << 1.0, 0.5, 0.2;
    pose2d.covariance.diagonal() << 0.1, 0.05, 0.02;

    auto transformed_pose2d = gaussian->transform_pose2d("room", pose2d, "root", 1000);
    REQUIRE(transformed_pose2d.has_value());
    CHECK(std::abs(transformed_pose2d->mean.x() + 3.0) < 1e-6);
    CHECK(std::abs(transformed_pose2d->mean.y() - 0.5) < 1e-6);
    CHECK(std::abs(transformed_pose2d->mean.z() - 0.2) < 1e-6);
    CHECK(transformed_pose2d->covariance(0, 0) > pose2d.covariance(0, 0));

    GaussianPose3D pose3d;
    pose3d.mean << 1.0, 0.5, 0.0, 0.0, 0.0, 0.2;
    pose3d.covariance.setZero();

    auto transformed_pose3d = gaussian->transform_pose3d("room", pose3d, "root", 1000);
    REQUIRE(transformed_pose3d.has_value());
    CHECK(std::abs(transformed_pose3d->mean(0) + 3.0) < 1e-6);
    CHECK(std::abs(transformed_pose3d->mean(1) - 0.5) < 1e-6);
    CHECK(std::abs(transformed_pose3d->mean(5) - 0.2) < 1e-6);
    CHECK(transformed_pose3d->covariance(0, 0) > 0.0);
}

TEST_CASE("InnerGaussian pose transform matches Monte Carlo covariance", "[GRAPH][RT][GAUSSIAN][MONTE_CARLO]")
{
    auto ctx = make_edge_config_file();
    auto id1 = rand() % 1000;
    DSRGraph G(random_string(10), id1, ctx);

    auto root = G.get_node("root");
    REQUIRE(root.has_value());

    auto room = G.get_node("room");
    REQUIRE(room.has_value());

    auto rt = G.get_rt_api();
    REQUIRE(rt);

    const Mat::Vector6d edge_mean = (Mat::Vector6d() << 4.0, -1.0, 0.5, 0.0, 0.0, 0.25).finished();
    const auto edge_covariance = diagonal_covariance(0.06, 0.04, 0.03, 0.0, 0.0, 0.01);

    rt->insert_or_assign_edge_RT(root.value(), room->id(),
                                 std::vector<float>{static_cast<float>(edge_mean(0)), static_cast<float>(edge_mean(1)), static_cast<float>(edge_mean(2))},
                                 std::vector<float>{static_cast<float>(edge_mean(3)), static_cast<float>(edge_mean(4)), static_cast<float>(edge_mean(5))},
                                 covariance_to_vector(edge_covariance), 1000);

    auto gaussian = G.get_inner_gaussian_api();
    REQUIRE(gaussian);

    GaussianPose3D pose;
    pose.mean << 1.0, 0.5, -0.2, 0.0, 0.0, 0.15;
    pose.covariance = diagonal_covariance(0.02, 0.015, 0.01, 0.0, 0.0, 0.004);

    auto analytical = gaussian->transform_pose3d("room", pose, "root", 1000);
    REQUIRE(analytical.has_value());

    const auto edge_covariance_sqrt = diagonal_square_root(edge_covariance);
    const auto pose_covariance_sqrt = diagonal_square_root(pose.covariance);
    const auto edge_mean_rt = pose_vector_to_rtmat_local(edge_mean);
    const auto pose_mean_rt = pose_vector_to_rtmat_local(pose.mean);

    constexpr std::size_t sample_count = 6000;
    std::mt19937 generator(123456u);
    Mat::Vector6d empirical_mean_delta = Mat::Vector6d::Zero();
    Cov6d second_moment = Cov6d::Zero();

    for (std::size_t sample_index = 0; sample_index < sample_count; ++sample_index)
    {
        const auto edge_delta = edge_covariance_sqrt * sample_standard_normal(generator);
        const auto pose_delta = pose_covariance_sqrt * sample_standard_normal(generator);

        const auto sampled_transform = (edge_mean_rt * pose_vector_to_rtmat_local(edge_delta)).inverse();
        const auto sampled_pose = pose_mean_rt * pose_vector_to_rtmat_local(pose_delta);
        const auto sampled_output = rtmat_to_pose_vector_local(sampled_transform * sampled_pose);
        const auto delta = pose_difference(sampled_output, analytical->mean);

        empirical_mean_delta += delta;
        second_moment += delta * delta.transpose();
    }

    empirical_mean_delta /= static_cast<double>(sample_count);
    const auto empirical_covariance = (second_moment - static_cast<double>(sample_count) * empirical_mean_delta * empirical_mean_delta.transpose())
                                    / static_cast<double>(sample_count - 1);

    CHECK(empirical_mean_delta.head<3>().norm() < 0.03);
    CHECK(std::abs(empirical_mean_delta(5)) < 0.02);

    for (const int diagonal : {0, 1, 2, 5})
        CHECK(std::abs(empirical_covariance(diagonal, diagonal) - analytical->covariance(diagonal, diagonal)) < 0.02);
}

TEST_CASE("InnerGaussian transform retrieval matches Monte Carlo covariance", "[GRAPH][RT][GAUSSIAN][MONTE_CARLO]")
{
    auto ctx = make_edge_config_file();
    auto id1 = rand() % 1000;
    DSRGraph G(random_string(10), id1, ctx);

    auto root = G.get_node("root");
    REQUIRE(root.has_value());

    auto room = G.get_node("room");
    REQUIRE(room.has_value());

    auto rt = G.get_rt_api();
    REQUIRE(rt);

    const Mat::Vector6d edge_mean = (Mat::Vector6d() << 2.8, 1.2, -0.4, 0.0, 0.0, -0.22).finished();
    const auto edge_covariance = diagonal_covariance(0.04, 0.03, 0.02, 0.0, 0.0, 0.006);

    rt->insert_or_assign_edge_RT(root.value(), room->id(),
                                 std::vector<float>{static_cast<float>(edge_mean(0)), static_cast<float>(edge_mean(1)), static_cast<float>(edge_mean(2))},
                                 std::vector<float>{static_cast<float>(edge_mean(3)), static_cast<float>(edge_mean(4)), static_cast<float>(edge_mean(5))},
                                 covariance_to_vector(edge_covariance), 1000);

    auto gaussian = G.get_inner_gaussian_api();
    REQUIRE(gaussian);

    auto analytical = gaussian->get_transformation_gaussian("room", "root", 1000);
    REQUIRE(analytical.has_value());

    const auto edge_covariance_sqrt = diagonal_square_root(edge_covariance);
    const auto edge_mean_rt = pose_vector_to_rtmat_local(edge_mean);

    constexpr std::size_t sample_count = 6000;
    std::mt19937 generator(112233u);
    Mat::Vector6d empirical_mean_delta = Mat::Vector6d::Zero();
    Cov6d second_moment = Cov6d::Zero();

    for (std::size_t sample_index = 0; sample_index < sample_count; ++sample_index)
    {
        const auto edge_delta = edge_covariance_sqrt * sample_standard_normal(generator);
        const auto sampled_output = rtmat_to_pose_vector_local((edge_mean_rt * pose_vector_to_rtmat_local(edge_delta)).inverse());
        const auto delta = pose_difference(sampled_output, analytical->mean);

        empirical_mean_delta += delta;
        second_moment += delta * delta.transpose();
    }

    empirical_mean_delta /= static_cast<double>(sample_count);
    const auto empirical_covariance = (second_moment - static_cast<double>(sample_count) * empirical_mean_delta * empirical_mean_delta.transpose())
                                    / static_cast<double>(sample_count - 1);

    CHECK(empirical_mean_delta.head<3>().norm() < 0.03);
    CHECK(std::abs(empirical_mean_delta(5)) < 0.02);

    for (const int diagonal : {0, 1, 2, 5})
        CHECK(std::abs(empirical_covariance(diagonal, diagonal) - analytical->covariance(diagonal, diagonal)) < 0.02);
}

TEST_CASE("InnerGaussian point transform matches Monte Carlo covariance", "[GRAPH][RT][GAUSSIAN][MONTE_CARLO]")
{
    auto ctx = make_edge_config_file();
    auto id1 = rand() % 1000;
    DSRGraph G(random_string(10), id1, ctx);

    auto root = G.get_node("root");
    REQUIRE(root.has_value());

    auto room = G.get_node("room");
    REQUIRE(room.has_value());

    auto rt = G.get_rt_api();
    REQUIRE(rt);

    const Mat::Vector6d edge_mean = (Mat::Vector6d() << 3.2, -1.1, 0.4, 0.0, 0.0, 0.28).finished();
    const auto edge_covariance = diagonal_covariance(0.05, 0.035, 0.02, 0.0, 0.0, 0.007);

    rt->insert_or_assign_edge_RT(root.value(), room->id(),
                                 std::vector<float>{static_cast<float>(edge_mean(0)), static_cast<float>(edge_mean(1)), static_cast<float>(edge_mean(2))},
                                 std::vector<float>{static_cast<float>(edge_mean(3)), static_cast<float>(edge_mean(4)), static_cast<float>(edge_mean(5))},
                                 covariance_to_vector(edge_covariance), 1000);

    auto gaussian = G.get_inner_gaussian_api();
    REQUIRE(gaussian);

    GaussianPoint3D point;
    point.mean << 0.7, -0.3, 1.1;
    point.covariance = Cov3d::Zero();
    point.covariance(0, 0) = 0.012;
    point.covariance(1, 1) = 0.009;
    point.covariance(2, 2) = 0.015;

    auto analytical = gaussian->transform_point("room", point, "root", 1000);
    REQUIRE(analytical.has_value());

    const auto edge_covariance_sqrt = diagonal_square_root(edge_covariance);
    const auto point_covariance_sqrt = diagonal_square_root(point.covariance);
    const auto edge_mean_rt = pose_vector_to_rtmat_local(edge_mean);

    constexpr std::size_t sample_count = 6000;
    std::mt19937 generator(778899u);
    Mat::Vector3d empirical_mean_delta = Mat::Vector3d::Zero();
    Cov3d second_moment = Cov3d::Zero();

    for (std::size_t sample_index = 0; sample_index < sample_count; ++sample_index)
    {
        const auto edge_delta = edge_covariance_sqrt * sample_standard_normal(generator);
        const auto point_delta = point_covariance_sqrt * sample_standard_normal3(generator);
        const auto sampled_transform = (edge_mean_rt * pose_vector_to_rtmat_local(edge_delta)).inverse();
        const auto sampled_output = (sampled_transform * (point.mean + point_delta).homogeneous()).eval();
        const auto delta = sampled_output - analytical->mean;

        empirical_mean_delta += delta;
        second_moment += delta * delta.transpose();
    }

    empirical_mean_delta /= static_cast<double>(sample_count);
    const auto empirical_covariance = (second_moment - static_cast<double>(sample_count) * empirical_mean_delta * empirical_mean_delta.transpose())
                                    / static_cast<double>(sample_count - 1);

    CHECK(empirical_mean_delta.norm() < 0.15);

    for (int diagonal = 0; diagonal < 3; ++diagonal)
        CHECK(std::abs(empirical_covariance(diagonal, diagonal) - analytical->covariance(diagonal, diagonal)) < 0.02);
}

TEST_CASE("InnerGaussian pose2d transform matches Monte Carlo covariance", "[GRAPH][RT][GAUSSIAN][MONTE_CARLO]")
{
    auto ctx = make_edge_config_file();
    auto id1 = rand() % 1000;
    DSRGraph G(random_string(10), id1, ctx);

    auto root = G.get_node("root");
    REQUIRE(root.has_value());

    auto room = G.get_node("room");
    REQUIRE(room.has_value());

    auto rt = G.get_rt_api();
    REQUIRE(rt);

    const Mat::Vector6d edge_mean = (Mat::Vector6d() << 3.5, -0.8, 0.0, 0.0, 0.0, 0.3).finished();
    const auto edge_covariance = diagonal_covariance(0.05, 0.03, 0.0, 0.0, 0.0, 0.008);

    rt->insert_or_assign_edge_RT(root.value(), room->id(),
                                 std::vector<float>{static_cast<float>(edge_mean(0)), static_cast<float>(edge_mean(1)), static_cast<float>(edge_mean(2))},
                                 std::vector<float>{static_cast<float>(edge_mean(3)), static_cast<float>(edge_mean(4)), static_cast<float>(edge_mean(5))},
                                 covariance_to_vector(edge_covariance), 1000);

    auto gaussian = G.get_inner_gaussian_api();
    REQUIRE(gaussian);

    GaussianPose2D pose;
    pose.mean << 0.8, -0.4, 0.12;
    pose.covariance = Cov3d::Zero();
    pose.covariance(0, 0) = 0.015;
    pose.covariance(1, 1) = 0.01;
    pose.covariance(2, 2) = 0.003;

    auto analytical = gaussian->transform_pose2d("room", pose, "root", 1000);
    REQUIRE(analytical.has_value());

    Cov6d lifted_pose_covariance = Cov6d::Zero();
    lifted_pose_covariance(0, 0) = pose.covariance(0, 0);
    lifted_pose_covariance(1, 1) = pose.covariance(1, 1);
    lifted_pose_covariance(5, 5) = pose.covariance(2, 2);

    Mat::Vector6d lifted_pose_mean = Mat::Vector6d::Zero();
    lifted_pose_mean << pose.mean(0), pose.mean(1), 0.0, 0.0, 0.0, pose.mean(2);

    const auto edge_covariance_sqrt = diagonal_square_root(edge_covariance);
    const auto pose_covariance_sqrt = diagonal_square_root(lifted_pose_covariance);
    const auto edge_mean_rt = pose_vector_to_rtmat_local(edge_mean);
    const auto pose_mean_rt = pose_vector_to_rtmat_local(lifted_pose_mean);

    constexpr std::size_t sample_count = 6000;
    std::mt19937 generator(654321u);
    Eigen::Vector3d empirical_mean_delta = Eigen::Vector3d::Zero();
    Cov3d second_moment = Cov3d::Zero();

    for (std::size_t sample_index = 0; sample_index < sample_count; ++sample_index)
    {
        const auto edge_delta = edge_covariance_sqrt * sample_standard_normal(generator);
        const auto pose_delta = pose_covariance_sqrt * sample_standard_normal(generator);

        const auto sampled_transform = (edge_mean_rt * pose_vector_to_rtmat_local(edge_delta)).inverse();
        const auto sampled_pose = pose_mean_rt * pose_vector_to_rtmat_local(pose_delta);
        const auto sampled_output = pose_vector_to_pose2d(rtmat_to_pose_vector_local(sampled_transform * sampled_pose));
        const auto delta = pose2d_difference_local(sampled_output, analytical->mean);

        empirical_mean_delta += delta;
        second_moment += delta * delta.transpose();
    }

    empirical_mean_delta /= static_cast<double>(sample_count);
    const auto empirical_covariance = (second_moment - static_cast<double>(sample_count) * empirical_mean_delta * empirical_mean_delta.transpose())
                                    / static_cast<double>(sample_count - 1);

    CHECK(empirical_mean_delta.head<2>().norm() < 0.03);
    CHECK(std::abs(empirical_mean_delta(2)) < 0.02);

    for (int diagonal = 0; diagonal < 3; ++diagonal)
        CHECK(std::abs(empirical_covariance(diagonal, diagonal) - analytical->covariance(diagonal, diagonal)) < 0.02);
}