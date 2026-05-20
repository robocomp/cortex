#include <dsr/api/dsr_inner_gaussian_api.h>

#include <dsr/api/dsr_api.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

using namespace DSR;

namespace
{
    Eigen::Matrix3d skew_symmetric(const Eigen::Vector3d &vector)
    {
        Eigen::Matrix3d skew = Eigen::Matrix3d::Zero();
        skew(0, 1) = -vector.z();
        skew(0, 2) = vector.y();
        skew(1, 0) = vector.z();
        skew(1, 2) = -vector.x();
        skew(2, 0) = -vector.y();
        skew(2, 1) = vector.x();
        return skew;
    }

    Cov6d adjoint_matrix(const Mat::RTMat &transform)
    {
        Cov6d adjoint = Cov6d::Zero();
        const auto rotation = transform.rotation();
        const auto translation = transform.translation();

        adjoint.block<3, 3>(0, 0) = rotation;
        adjoint.block<3, 3>(0, 3) = skew_symmetric(translation) * rotation;
        adjoint.block<3, 3>(3, 3) = rotation;
        return adjoint;
    }

    double wrap_angle_local(double angle)
    {
        while (angle > M_PI)
            angle -= 2.0 * M_PI;
        while (angle < -M_PI)
            angle += 2.0 * M_PI;
        return angle;
    }

    Mat::RTMat pose_vector_to_rtmat_local(const Mat::Vector6d &pose)
    {
        auto rtmat = Mat::RTMat::Identity();
        rtmat.translate(pose.head<3>());
        rtmat.rotate(Eigen::AngleAxisd(pose(3), Eigen::Vector3d::UnitX()) *
                     Eigen::AngleAxisd(pose(4), Eigen::Vector3d::UnitY()) *
                     Eigen::AngleAxisd(pose(5), Eigen::Vector3d::UnitZ()));
        return rtmat;
    }

    template<int OutDim, int InDim, typename Function, typename Difference>
    Eigen::Matrix<double, OutDim, InDim> numerical_jacobian(Function function, Difference difference, double epsilon = 1e-6)
    {
        Eigen::Matrix<double, OutDim, InDim> jacobian = Eigen::Matrix<double, OutDim, InDim>::Zero();
        Eigen::Matrix<double, InDim, 1> delta = Eigen::Matrix<double, InDim, 1>::Zero();

        for (int column = 0; column < InDim; ++column)
        {
            delta.setZero();
            delta(column) = epsilon;
            const auto positive = function(delta);
            const auto negative = function(-delta);
            jacobian.col(column) = difference(positive, negative) / (2.0 * epsilon);
        }

        return jacobian;
    }

    Mat::Vector6d pose_difference(const Mat::Vector6d &lhs, const Mat::Vector6d &rhs)
    {
        Mat::Vector6d difference = lhs - rhs;
        for (int axis = 3; axis < 6; ++axis)
            difference(axis) = wrap_angle_local(difference(axis));
        return difference;
    }

    Eigen::Vector3d pose2d_difference(const Eigen::Vector3d &lhs, const Eigen::Vector3d &rhs)
    {
        Eigen::Vector3d difference = lhs - rhs;
        difference(2) = wrap_angle_local(difference(2));
        return difference;
    }

    Mat::RTMat chain_to_rtmat(const GaussianPose3D &transform)
    {
        return pose_vector_to_rtmat_local(transform.mean);
    }
}

InnerGaussianAPI::InnerGaussianAPI(DSRGraph *graph) : m_graph(graph), m_rt(graph->get_rt_api())
{
}

std::optional<GaussianPose3D> InnerGaussianAPI::get_transformation_gaussian(const std::string &dest,
                                                                            const std::string &orig,
                                                                            std::uint64_t timestamp,
                                                                            const std::string &edge_type,
                                                                            RT_API::TimeQuery time_query) const
{
    if (not DSR::DSRGraph::is_valid_edge_type(edge_type))
        return {};

    auto steps = build_path_steps(dest, orig, timestamp, edge_type, time_query);
    if (not steps.has_value())
        return {};

    GaussianPose3D transform;
    auto prefix = Mat::RTMat::Identity();
    for (const auto &step : steps.value())
    {
        auto operation_covariance = step.edge_covariance;
        if (step.invert)
        {
            const auto inverse_map = adjoint_matrix(step.edge_mean);
            operation_covariance = inverse_map * operation_covariance * inverse_map.transpose();
        }

        transform.covariance += adjoint_matrix(prefix.inverse()) * operation_covariance * adjoint_matrix(prefix.inverse()).transpose();

        const auto operation = step.invert ? step.edge_mean.inverse() : step.edge_mean;
        prefix = operation * prefix;
    }

    transform.mean = rtmat_to_pose_vector(prefix);

    return std::make_optional(transform);
}

std::optional<GaussianPoint3D> InnerGaussianAPI::transform_point(const std::string &dest,
                                                                 const GaussianPoint3D &point,
                                                                 const std::string &orig,
                                                                 std::uint64_t timestamp,
                                                                 const std::string &edge_type,
                                                                 RT_API::TimeQuery time_query) const
{
    auto transform = get_transformation_gaussian(dest, orig, timestamp, edge_type, time_query);
    if (not transform.has_value())
        return {};

    const auto rtmat = chain_to_rtmat(transform.value());

    GaussianPoint3D result;
    result.mean = rtmat * point.mean.homogeneous();

    const auto transform_jacobian = numerical_jacobian<3, 6>(
        [&](const Mat::Vector6d &delta)
        {
            return (rtmat * local_delta_rtmat(delta) * point.mean.homogeneous()).eval();
        },
        [](const Mat::Vector3d &lhs, const Mat::Vector3d &rhs)
        {
            return lhs - rhs;
        });

    const auto point_jacobian = numerical_jacobian<3, 3>(
        [&](const Eigen::Vector3d &delta)
        {
            return (rtmat * (point.mean + delta).homogeneous()).eval();
        },
        [](const Mat::Vector3d &lhs, const Mat::Vector3d &rhs)
        {
            return lhs - rhs;
        });

    result.covariance = transform_jacobian * transform->covariance * transform_jacobian.transpose()
                      + point_jacobian * point.covariance * point_jacobian.transpose();
    return std::make_optional(result);
}

std::optional<GaussianPose2D> InnerGaussianAPI::transform_pose2d(const std::string &dest,
                                                                 const GaussianPose2D &pose,
                                                                 const std::string &orig,
                                                                 std::uint64_t timestamp,
                                                                 const std::string &edge_type,
                                                                 RT_API::TimeQuery time_query) const
{
    GaussianPose3D lifted_pose;
    lifted_pose.mean << pose.mean(0), pose.mean(1), 0.0, 0.0, 0.0, pose.mean(2);
    lifted_pose.covariance.setZero();
    lifted_pose.covariance(0, 0) = pose.covariance(0, 0);
    lifted_pose.covariance(0, 1) = pose.covariance(0, 1);
    lifted_pose.covariance(1, 0) = pose.covariance(1, 0);
    lifted_pose.covariance(1, 1) = pose.covariance(1, 1);
    lifted_pose.covariance(0, 5) = pose.covariance(0, 2);
    lifted_pose.covariance(5, 0) = pose.covariance(2, 0);
    lifted_pose.covariance(1, 5) = pose.covariance(1, 2);
    lifted_pose.covariance(5, 1) = pose.covariance(2, 1);
    lifted_pose.covariance(5, 5) = pose.covariance(2, 2);

    auto transformed_pose = transform_pose3d(dest, lifted_pose, orig, timestamp, edge_type, time_query);
    if (not transformed_pose.has_value())
        return {};

    GaussianPose2D result;
    result.mean << transformed_pose->mean(0), transformed_pose->mean(1), transformed_pose->mean(5);

    const std::array<int, 3> indices{0, 1, 5};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            result.covariance(row, col) = transformed_pose->covariance(indices[row], indices[col]);

    return std::make_optional(result);
}

std::optional<GaussianPose3D> InnerGaussianAPI::transform_pose3d(const std::string &dest,
                                                                 const GaussianPose3D &pose,
                                                                 const std::string &orig,
                                                                 std::uint64_t timestamp,
                                                                 const std::string &edge_type,
                                                                 RT_API::TimeQuery time_query) const
{
    auto transform = get_transformation_gaussian(dest, orig, timestamp, edge_type, time_query);
    if (not transform.has_value())
        return {};

    const auto transform_rt = chain_to_rtmat(transform.value());
    const auto pose_rt = pose_vector_to_rtmat(pose.mean);

    GaussianPose3D result;
    result.mean = rtmat_to_pose_vector(transform_rt * pose_rt);

    const auto transform_jacobian = numerical_jacobian<6, 6>(
        [&](const Mat::Vector6d &delta)
        {
            return rtmat_to_pose_vector(transform_rt * local_delta_rtmat(delta) * pose_rt);
        },
        [](const Mat::Vector6d &lhs, const Mat::Vector6d &rhs)
        {
            return pose_difference(lhs, rhs);
        });

    const auto pose_jacobian = numerical_jacobian<6, 6>(
        [&](const Mat::Vector6d &delta)
        {
            return rtmat_to_pose_vector(transform_rt * pose_rt * InnerGaussianAPI::local_delta_rtmat(delta));
        },
        [](const Mat::Vector6d &lhs, const Mat::Vector6d &rhs)
        {
            return pose_difference(lhs, rhs);
        });

    result.covariance = transform_jacobian * transform->covariance * transform_jacobian.transpose()
                      + pose_jacobian * pose.covariance * pose_jacobian.transpose();
    return std::make_optional(result);
}

std::optional<InnerGaussianAPI::PathSteps> InnerGaussianAPI::build_path_steps(const std::string &dest,
                                                                              const std::string &orig,
                                                                              std::uint64_t timestamp,
                                                                              const std::string &edge_type,
                                                                              RT_API::TimeQuery time_query) const
{
    auto orig_node = m_graph->get_node(orig);
    auto dest_node = m_graph->get_node(dest);
    if (not orig_node.has_value() || not dest_node.has_value())
        return {};

    std::vector<Node> orig_to_root;
    std::unordered_map<std::uint64_t, std::size_t> orig_positions;
    for (auto current = orig_node; current.has_value(); current = m_graph->get_parent_node(current.value()))
    {
        orig_positions.emplace(current->id(), orig_to_root.size());
        orig_to_root.push_back(current.value());
        if (current->name() == "root")
            break;
    }

    std::vector<Node> dest_to_root;
    std::optional<Node> lca;
    for (auto current = dest_node; current.has_value(); current = m_graph->get_parent_node(current.value()))
    {
        if (orig_positions.contains(current->id()))
        {
            lca = current;
            break;
        }
        dest_to_root.push_back(current.value());
        if (current->name() == "root")
            break;
    }

    if (not lca.has_value())
        return {};

    PathSteps steps;
    for (auto current = orig_node; current->id() != lca->id(); current = m_graph->get_parent_node(current.value()))
    {
        auto parent = m_graph->get_parent_node(current.value());
        if (not parent.has_value())
            return {};

        auto edge = m_rt->get_edge_RT(parent.value(), current->id(), edge_type);
        if (not edge.has_value())
            return {};

        auto mean = m_rt->get_edge_RT_as_rtmat(edge.value(), timestamp, time_query);
        if (not mean.has_value())
            return {};

        auto covariance = m_rt->get_edge_RT_covariance(edge.value(), timestamp, time_query).value_or(Cov6d::Zero());
        steps.push_back(PathStep{parent->id(), current->id(), mean.value(), covariance, false});
    }

    PathSteps descending_steps;
    for (auto current = dest_node; current->id() != lca->id(); current = m_graph->get_parent_node(current.value()))
    {
        auto parent = m_graph->get_parent_node(current.value());
        if (not parent.has_value())
            return {};

        auto edge = m_rt->get_edge_RT(parent.value(), current->id(), edge_type);
        if (not edge.has_value())
            return {};

        auto mean = m_rt->get_edge_RT_as_rtmat(edge.value(), timestamp, time_query);
        if (not mean.has_value())
            return {};

        auto covariance = m_rt->get_edge_RT_covariance(edge.value(), timestamp, time_query).value_or(Cov6d::Zero());
        descending_steps.push_back(PathStep{parent->id(), current->id(), mean.value(), covariance, true});
    }

    std::reverse(descending_steps.begin(), descending_steps.end());
    steps.insert(steps.end(), descending_steps.begin(), descending_steps.end());
    return steps;
}

Mat::RTMat InnerGaussianAPI::compose_chain(const PathSteps &steps,
                                           std::optional<std::size_t> perturbed_step,
                                           const Mat::Vector6d &delta) const
{
    auto transform = Mat::RTMat::Identity();
    for (std::size_t index = 0; index < steps.size(); ++index)
    {
        auto edge_mean = steps[index].edge_mean;
        if (perturbed_step.has_value() && perturbed_step.value() == index)
            edge_mean = edge_mean * local_delta_rtmat(delta);

        const auto operation = steps[index].invert ? edge_mean.inverse() : edge_mean;
        transform = operation * transform;
    }
    return transform;
}

Mat::RTMat InnerGaussianAPI::pose_vector_to_rtmat(const Mat::Vector6d &pose)
{
    auto rtmat = Mat::RTMat::Identity();
    rtmat.translate(pose.head<3>());
    rtmat.rotate(Eigen::AngleAxisd(pose(3), Eigen::Vector3d::UnitX()) *
                 Eigen::AngleAxisd(pose(4), Eigen::Vector3d::UnitY()) *
                 Eigen::AngleAxisd(pose(5), Eigen::Vector3d::UnitZ()));
    return rtmat;
}

Mat::Vector6d InnerGaussianAPI::rtmat_to_pose_vector(const Mat::RTMat &rtmat)
{
    Mat::Vector6d pose;
    const auto angles = rtmat.rotation().eulerAngles(0, 1, 2);
    pose << rtmat.translation().x(), rtmat.translation().y(), rtmat.translation().z(),
            angles.x(), angles.y(), angles.z();
    return pose;
}

Mat::RTMat InnerGaussianAPI::local_delta_rtmat(const Mat::Vector6d &delta)
{
    return pose_vector_to_rtmat(delta);
}

double InnerGaussianAPI::wrap_angle(double angle)
{
    while (angle > M_PI)
        angle -= 2.0 * M_PI;
    while (angle < -M_PI)
        angle += 2.0 * M_PI;
    return angle;
}