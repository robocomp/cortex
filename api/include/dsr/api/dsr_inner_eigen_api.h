#ifndef INNER_EIGEN_API
#define INNER_EIGEN_API

#include <QObject>
#include <dsr/core/types/internal_types.h>
#include <dsr/api/dsr_eigen_defs.h>
#include <dsr/api/dsr_rt_api.h>
#include <optional>
#include <cstdint>
#include <tuple>
#include <map>

namespace DSR
{
    class DSRGraph;

    class InnerEigenAPI : public QObject
    {
        Q_OBJECT
        using KeyTransform = std::tuple<std::string, std::string, std::string>;
        using NodeReference = std::map<uint64_t , std::list<KeyTransform>>;
        using TransformCache = std::map<KeyTransform, Mat::RTMat>;
        using NodeMatrix = std::tuple<uint64_t , Mat::RTMat>;

        public:
            explicit InnerEigenAPI(DSRGraph *G_);

            /////////////////////////////////////////////////
            /// Kinematic transformation methods
            ////////////////////////////////////////////////
            std::optional<Mat::Vector3d> transform( const std::string &dest, const std::string &orig, std::uint64_t timestamp = 0, const std::string &edge_type="RT", RT_API::TimeQuery time_query = RT_API::TimeQuery::Nearest);
            std::optional<Mat::Vector3d> transform( const std::string &dest, const Mat::Vector3d &vector, const std::string &orig, std::uint64_t timestamp = 0, const std::string &edge_type="RT", RT_API::TimeQuery time_query = RT_API::TimeQuery::Nearest);
            std::optional<Mat::Vector6d> transform_axis(const std::string &dest, const std::string & orig, std::uint64_t timestamp = 0, const std::string &edge_type="RT", RT_API::TimeQuery time_query = RT_API::TimeQuery::Nearest);
            std::optional<Mat::Vector6d> transform_axis(const std::string &dest, const Mat::Vector6d &vector, const std::string &orig, std::uint64_t timestamp = 0, const std::string &edge_type="RT", RT_API::TimeQuery time_query = RT_API::TimeQuery::Nearest);

            ////////////////////////////////////////////////
            /// Transformation matrix retrieval methods
            ////////////////////////////////////////////////
            // `info` (optional) reports what the composition did — above all whether ANY edge in the
            // chain clamped, which is the failure the returned matrix cannot express. See
            // RT_API::TimeQueryInfo.
            // ★AGGREGATED AS THE WORST OUTCOME, NOT THE LAST ONE. A chain is only as trustworthy as
            // its least trustworthy edge: one stale edge among five healthy ones still means the
            // composed pose is partly asserted rather than measured, and reporting the last edge
            // visited would make that depend on tree order. `applied_dt_ms`, `gap_ms` and
            // `ring_span_ms` carry the largest magnitude seen, for the same reason — the edges are
            // composed, not travelled in series, so one edge walked 30 ms is a 30 ms prediction, not
            // 30 ms of accumulated error. In practice exactly one edge in a chain carries a twist.
            std::optional<Mat::RTMat> get_transformation_matrix(const std::string &dest, const std::string &orig, std::uint64_t timestamp = 0, const std::string &edge_type="RT", RT_API::TimeQuery time_query = RT_API::TimeQuery::Nearest,
                                                                RT_API::TimeQueryInfo *info = nullptr);
            std::optional<Mat::Rot3D> get_rotation_matrix(const std::string &dest, const std::string &orig, std::uint64_t timestamp = 0, const std::string &edge_type="RT", RT_API::TimeQuery time_query = RT_API::TimeQuery::Nearest);
            std::optional<Mat::Vector3d> get_translation_vector(const std::string &dest, const std::string &orig, std::uint64_t timestamp = 0, const std::string &edge_type="RT", RT_API::TimeQuery time_query = RT_API::TimeQuery::Nearest);
            std::optional<Mat::Vector3d> get_euler_xyz_angles(const std::string &dest, const std::string &orig, std::uint64_t timestamp = 0, const std::string &edge_type="RT", RT_API::TimeQuery time_query = RT_API::TimeQuery::Nearest);

        public slots:
            void add_or_assign_edge_slot(uint64_t from, uint64_t to, const std::string& edge_type);
            void add_or_assign_edge_attr_slot(uint64_t from, uint64_t to, const std::string& edge_type, const std::vector<std::string>& att_names);
            void del_node_slot(uint64_t id);
            void del_edge_slot(uint64_t from, uint64_t to, const std::string &edge_type);

        private:
            DSR::DSRGraph *G;
            std::unique_ptr<DSR::RT_API> rt;
            TransformCache cache;
            NodeReference node_map;
            void remove_cache_entry(const uint64_t id);
    };
}

#endif
