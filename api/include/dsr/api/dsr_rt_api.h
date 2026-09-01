#ifndef RTAPI
#define RTAPI

#include <cassert>
#include <QtCore>
#include <dsr/core/types/internal_types.h>
#include <dsr/core/types/user_types.h>
#include <dsr/core/types/type_checking/dsr_attr_name.h>
#include <dsr/api/dsr_eigen_defs.h>
#include <optional>

namespace DSR
{
    class DSRGraph;

    class RT_API : public QObject
    {
        public:
            enum class TimeQuery
            {
                Nearest,
                Interpolated
            };

            // Which of the three 6x6 covariance blocks stored on an RT edge is addressed.
            // Pose  -> rt_covariance, Velocity -> rt_covariance_velocity, Acceleration -> rt_covariance_acceleration.
            enum class CovarianceKind
            {
                Pose,
                Velocity,
                Acceleration
            };

            explicit RT_API(DSRGraph *G_);

            const int32_t BLOCK_SIZE = 3;   // size of 3-vector for translation and euler xyz angles
            uint32_t HISTORY_SIZE = 5; // Number of blocks in the history.

            void insert_or_assign_edge_RT(Node &n, uint64_t to, const std::vector<float> &trans, const std::vector<float> &rot_euler, std::optional<uint64_t> timestamp = std::nullopt);
            void insert_or_assign_edge_RT(Node &n, uint64_t to, std::vector<float> &&trans, std::vector<float> &&rot_euler, std::optional<uint64_t> timestamp = std::nullopt);
            void insert_or_assign_edge_RT(Node &n, uint64_t to, const std::vector<float> &trans, const std::vector<float> &rot_euler,
                                          const std::vector<float> &covariance, std::optional<uint64_t> timestamp = std::nullopt);
            void insert_or_assign_edge_RT(Node &n, uint64_t to, std::vector<float> &&trans, std::vector<float> &&rot_euler,
                                          std::vector<float> &&covariance, std::optional<uint64_t> timestamp = std::nullopt);

            // Hang `to` from `n` with the IDENTITY transform: zero translation, zero euler rotation.
            // The child's frame then coincides with the parent's, so a node inserted purely to give a
            // subtree a semantic parent (a floor the walls hang from, a rig the parts hang from) costs
            // no geometry: every descendant keeps the pose it had relative to the grandparent.
            // Like every insert_or_assign_edge_RT it also fixes the child's parent/level attributes and
            // cascades the level fix down the subtree, so it is the correct way to RE-PARENT as well.
            void insert_or_assign_edge_RT_identity(Node &n, uint64_t to, std::optional<uint64_t> timestamp = std::nullopt);
            // Same, addressing the parent by id. Returns false (and warns) if that node is not in G.
            bool insert_or_assign_edge_RT_identity(uint64_t node_id, uint64_t to, std::optional<uint64_t> timestamp = std::nullopt);

            // Write one 6x6 covariance block on an existing RT edge, without touching the pose payload.
            // Use this when the covariance is produced separately from the transform (velocity/acceleration
            // uncertainty, or a pose covariance refined after the fact) instead of writing the attribute raw.
            // The matrix is stored row-major as 36 floats; the vector overload throws if it is not size 36.
            // If the edge carries a history ring (rt_head_index present and HISTORY_SIZE > 0) the block goes
            // into the slot of the most recent pose, or into the slot nearest `timestamp` when one is given;
            // otherwise it is stored as a single flat block. Returns false if the RT edge does not exist.
            bool insert_or_assign_edge_RT_covariance(const Node &n, uint64_t to, CovarianceKind kind,
                                                     const std::vector<float> &covariance, std::optional<uint64_t> timestamp = std::nullopt);
            bool insert_or_assign_edge_RT_covariance(const Node &n, uint64_t to, CovarianceKind kind,
                                                     const Eigen::Matrix<double, 6, 6> &covariance, std::optional<uint64_t> timestamp = std::nullopt);
            bool insert_or_assign_edge_RT_covariance(uint64_t node_id, uint64_t to, CovarianceKind kind,
                                                     const std::vector<float> &covariance, std::optional<uint64_t> timestamp = std::nullopt);
            bool insert_or_assign_edge_RT_covariance(uint64_t node_id, uint64_t to, CovarianceKind kind,
                                                     const Eigen::Matrix<double, 6, 6> &covariance, std::optional<uint64_t> timestamp = std::nullopt);

            // Verify (and optionally repair) the whole RT tree: every node's level == parent.level+1
            // and its parent attr == its RT-edge source, single root, no cycles / multi-parents.
            // Returns true if the tree was already consistent. With repair=true it rewrites the
            // offending level/parent attributes (use after a json bootstrap that may carry stale levels).
            bool check_RT_tree(bool repair = false);

            static std::optional<Edge> get_edge_RT(const Node &n, uint64_t to, const std::string &edge_type = "RT");
            std::optional<Mat::RTMat> get_RT_pose_from_parent(const Node &n, const std::string &edge_type = "RT");
            std::optional<Mat::RTMat> get_edge_RT_as_rtmat(const Edge &edge, std::uint64_t timestamp = 0, TimeQuery time_query = TimeQuery::Nearest);
            // `kind` is last so existing pose-covariance call sites keep compiling unchanged.
            std::optional<Eigen::Matrix<double, 6, 6>> get_edge_RT_covariance(const Edge &edge, std::uint64_t timestamp = 0, TimeQuery time_query = TimeQuery::Nearest, CovarianceKind kind = CovarianceKind::Pose);
            std::optional<Eigen::Vector3d> get_translation(const Node &n, uint64_t to, std::uint64_t timestamp = 0, TimeQuery time_query = TimeQuery::Nearest);
            std::optional<Eigen::Vector3d> get_translation(uint64_t node_id, uint64_t to, std::uint64_t timestamp = 0, TimeQuery time_query = TimeQuery::Nearest);
            std::optional<Eigen::Matrix<double, 6, 6>> get_covariance_matrix(const Node &n, uint64_t to, std::uint64_t timestamp = 0, TimeQuery time_query = TimeQuery::Nearest, CovarianceKind kind = CovarianceKind::Pose);
            std::optional<Eigen::Matrix<double, 6, 6>> get_covariance_matrix(uint64_t node_id, uint64_t to, std::uint64_t timestamp = 0, TimeQuery time_query = TimeQuery::Nearest, CovarianceKind kind = CovarianceKind::Pose);
            // std::optional<Mat::RTMat> get_edge_RT_as_rtmat(const Node &n, uint32_t to);
            // std::optional<std::tuple<Mat::Vector3d, Mat::Quaterniond>> get_edge_RT_as_tr_plus_quaternion(const Edge &edge);
            // std::optional<Mat::MatXX> get_jacobian(const Node &base, const Node &tip)

        public slots:
//            void add_or_assign_edge_slot(const std::int32_t from, const std::int32_t to, const std::string& edge_type);
//            void del_node_slot(const std::int32_t id);
//            void del_edge_slot(const std::int32_t from, const std::int32_t to, const std::string &edge_type);
        private:
            DSR::DSRGraph *G;
            static constexpr auto next = [](auto v, int size, int decr = 1) { return (v + decr) % size; };
            static constexpr auto prev = [](auto v, int size, int inc = 1) { return (v > 0) ? v - inc : size - inc; };
            void insert_or_assign_edge_RT_impl(Node &n, uint64_t to, std::vector<float> trans, std::vector<float> rot_euler,
                                               std::optional<std::vector<float>> covariance, std::optional<uint64_t> timestamp);

            // Shared BFS over the RT subtree rooted at start_id, re-deriving level = parent.level+1
            // and parent = RT source for every descendant. With repair=true it writes corrections;
            // with report=true it logs each defect/fix. Returns true if the subtree was consistent.
            // Used by insert_or_assign_edge_RT (cascade after a re-parent) and check_RT_tree (whole tree).
            bool walk_and_fix_levels(uint64_t start_id, bool repair, bool report);

    };
}

#endif
