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

            explicit RT_API(DSRGraph *G_);

            const int32_t BLOCK_SIZE = 3;   // size of 3-vector for translation and euler xyz angles
            uint32_t HISTORY_SIZE = 5; // Number of blocks in the history.

            void insert_or_assign_edge_RT(Node &n, uint64_t to, const std::vector<float> &trans, const std::vector<float> &rot_euler, std::optional<uint64_t> timestamp = std::nullopt);
            void insert_or_assign_edge_RT(Node &n, uint64_t to, std::vector<float> &&trans, std::vector<float> &&rot_euler, std::optional<uint64_t> timestamp = std::nullopt);
            void insert_or_assign_edge_RT(Node &n, uint64_t to, const std::vector<float> &trans, const std::vector<float> &rot_euler,
                                          const std::vector<float> &covariance, std::optional<uint64_t> timestamp = std::nullopt);
            void insert_or_assign_edge_RT(Node &n, uint64_t to, std::vector<float> &&trans, std::vector<float> &&rot_euler,
                                          std::vector<float> &&covariance, std::optional<uint64_t> timestamp = std::nullopt);

            // Verify (and optionally repair) the whole RT tree: every node's level == parent.level+1
            // and its parent attr == its RT-edge source, single root, no cycles / multi-parents.
            // Returns true if the tree was already consistent. With repair=true it rewrites the
            // offending level/parent attributes (use after a json bootstrap that may carry stale levels).
            bool check_RT_tree(bool repair = false);

            static std::optional<Edge> get_edge_RT(const Node &n, uint64_t to, const std::string &edge_type = "RT");
            std::optional<Mat::RTMat> get_RT_pose_from_parent(const Node &n, const std::string &edge_type = "RT");
            std::optional<Mat::RTMat> get_edge_RT_as_rtmat(const Edge &edge, std::uint64_t timestamp = 0, TimeQuery time_query = TimeQuery::Nearest);
            std::optional<Eigen::Matrix<double, 6, 6>> get_edge_RT_covariance(const Edge &edge, std::uint64_t timestamp = 0, TimeQuery time_query = TimeQuery::Nearest);
            std::optional<Eigen::Vector3d> get_translation(const Node &n, uint64_t to, std::uint64_t timestamp = 0, TimeQuery time_query = TimeQuery::Nearest);
            std::optional<Eigen::Vector3d> get_translation(uint64_t node_id, uint64_t to, std::uint64_t timestamp = 0, TimeQuery time_query = TimeQuery::Nearest);
            std::optional<Eigen::Matrix<double, 6, 6>> get_covariance_matrix(const Node &n, uint64_t to, std::uint64_t timestamp = 0, TimeQuery time_query = TimeQuery::Nearest);
            std::optional<Eigen::Matrix<double, 6, 6>> get_covariance_matrix(uint64_t node_id, uint64_t to, std::uint64_t timestamp = 0, TimeQuery time_query = TimeQuery::Nearest);
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
