#ifndef INNER_GAUSSIAN_API
#define INNER_GAUSSIAN_API

#include <QObject>
#include <Eigen/StdVector>
#include <dsr/api/dsr_eigen_defs.h>
#include <dsr/api/dsr_rt_api.h>
#include <optional>
#include <vector>

namespace DSR
{
    class DSRGraph;

    using Cov3d = Eigen::Matrix<double, 3, 3, Eigen::DontAlign>;
    using Cov6d = Eigen::Matrix<double, 6, 6, Eigen::DontAlign>;

    struct GaussianPoint3D
    {
        EIGEN_MAKE_ALIGNED_OPERATOR_NEW
        Mat::Vector3d mean = Mat::Vector3d::Zero();
        Cov3d covariance = Cov3d::Zero();
    };

    struct GaussianPose2D
    {
        EIGEN_MAKE_ALIGNED_OPERATOR_NEW
        Eigen::Vector3d mean = Eigen::Vector3d::Zero();
        Cov3d covariance = Cov3d::Zero();
    };

    struct GaussianPose3D
    {
        EIGEN_MAKE_ALIGNED_OPERATOR_NEW
        Mat::Vector6d mean = Mat::Vector6d::Zero();
        Cov6d covariance = Cov6d::Zero();
    };

    class InnerGaussianAPI : public QObject
    {
        public:
            explicit InnerGaussianAPI(DSRGraph *graph);

            std::optional<GaussianPose3D> get_transformation_gaussian(
                const std::string &dest,
                const std::string &orig,
                std::uint64_t timestamp = 0,
                const std::string &edge_type = "RT",
                RT_API::TimeQuery time_query = RT_API::TimeQuery::Nearest) const;

            std::optional<GaussianPoint3D> transform_point(
                const std::string &dest,
                const GaussianPoint3D &point,
                const std::string &orig,
                std::uint64_t timestamp = 0,
                const std::string &edge_type = "RT",
                RT_API::TimeQuery time_query = RT_API::TimeQuery::Nearest) const;

            std::optional<GaussianPose2D> transform_pose2d(
                const std::string &dest,
                const GaussianPose2D &pose,
                const std::string &orig,
                std::uint64_t timestamp = 0,
                const std::string &edge_type = "RT",
                RT_API::TimeQuery time_query = RT_API::TimeQuery::Nearest) const;

            std::optional<GaussianPose3D> transform_pose3d(
                const std::string &dest,
                const GaussianPose3D &pose,
                const std::string &orig,
                std::uint64_t timestamp = 0,
                const std::string &edge_type = "RT",
                RT_API::TimeQuery time_query = RT_API::TimeQuery::Nearest) const;

        private:
            struct PathStep
            {
                EIGEN_MAKE_ALIGNED_OPERATOR_NEW
                std::uint64_t parent_id = 0;
                std::uint64_t child_id = 0;
                Mat::RTMat edge_mean = Mat::RTMat::Identity();
                Cov6d edge_covariance = Cov6d::Zero();
                bool invert = false;
            };

            using PathSteps = std::vector<PathStep, Eigen::aligned_allocator<PathStep>>;

            DSRGraph *m_graph;
            std::unique_ptr<RT_API> m_rt;

            std::optional<PathSteps> build_path_steps(
                const std::string &dest,
                const std::string &orig,
                std::uint64_t timestamp,
                const std::string &edge_type,
                RT_API::TimeQuery time_query) const;

            Mat::RTMat compose_chain(const PathSteps &steps,
                                     std::optional<std::size_t> perturbed_step = std::nullopt,
                                     const Mat::Vector6d &delta = Mat::Vector6d::Zero()) const;

            static Mat::RTMat pose_vector_to_rtmat(const Mat::Vector6d &pose);
            static Mat::Vector6d rtmat_to_pose_vector(const Mat::RTMat &rtmat);
            static Mat::RTMat local_delta_rtmat(const Mat::Vector6d &delta);
            static double wrap_angle(double angle);
        };
}

#endif