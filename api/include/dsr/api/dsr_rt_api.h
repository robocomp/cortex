#ifndef RTAPI
#define RTAPI

/* =================================================================================================
 * RT_API — the RT (rigid transform) edges of the graph, and how to ask them for a POSE AT A TIME.
 *
 * An RT edge is not one transform. It carries a HISTORY RING of HISTORY_SIZE blocks: rt_translation
 * and rt_rotation_euler_xyz hold 3 floats per block, rt_timestamps the instant each block is valid
 * for, and rt_head_index which slot is written next. insert_or_assign_edge_RT advances that ring;
 * get_edge_RT_as_rtmat reads it back at a requested timestamp.
 *
 * WHY A HISTORY AT ALL. A consumer's data has its own capture stamp — a LiDAR sweep, a camera frame —
 * and the pose that belongs with it is the pose the robot held AT THAT INSTANT, not the newest one.
 * Registering a cloud against "the latest pose" smears it by (pose age x speed), which is invisible
 * standing still and becomes a bulk omega*dt rotation the moment the robot turns.
 *
 * ── THE TWIST RING (rt_twist_linear / rt_twist_angular) ─────────────────────────────────────────
 * Each block may also carry the CHILD'S VELOCITY as measured at that block, in the same slot, so it
 * inherits that block's timestamp. Write it through the RTBlock overload of insert_or_assign_edge_RT,
 * never as loose attributes: a twist with no slot and no stamp cannot be paired with a transform, and
 * a reader cannot tell a fresh one from a dead producer's last.
 * ★AXIS ORDER, in the CHILD frame's own axes — [vx,vy,vz], [wx,wy,wz]. This is the whole reason for
 * the name: the older rt_translation_velocity is ARRAY order in a producer-specific body convention
 * ([adv, side, _] on a robot whose +Y is forward) sitting beside rt_translation in true axis order on
 * the SAME EDGE, and re-encoding that convention per consumer produced the identical 90-degree bug in
 * three of them. With axis order, dp = R*v*dt is correct without knowing whose robot this is.
 *
 * ── ASKING FOR A TIME: TimeQuery, AND THE CLAMP ─────────────────────────────────────────────────
 * Nearest / Interpolated / Extrapolated, below. The one thing to understand before using any of them:
 *
 *   ★A TIMESTAMPED QUERY THAT FALLS OUTSIDE THE RING RETURNS THE END BLOCK AND LOOKS LIKE A SUCCESS.
 *
 * That is not an edge case. It is the NORMAL case for a consumer whose data is fresher than the pose
 * feed — a pose derived FROM a scan can only arrive after it — so the freshest scan routinely outruns
 * the freshest block. The returned RTMat cannot express "this is from a different instant", which is
 * why the same defect has been re-diagnosed downstream as a sensor fault, a calibration error and a
 * localiser problem in turn.
 * Two facilities exist for it, and they answer different questions:
 *   - TimeQuery::Extrapolated walks the end block along its own twist onto the instant asked for,
 *     bounded by the ring's own span (a twist is evidence on the timescale it was sampled at).
 *   - TimeQueryInfo reports what actually happened — above all Clamped and Stale. Pass it whenever
 *     the answer matters; a caller that omits it keeps the old silent behaviour by construction.
 *
 * Both are OPT-IN and additive: every call site written before them behaves exactly as it did.
 * ================================================================================================= */

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
                // Between two blocks, lerp the translation and slerp the rotation. OUTSIDE the ring
                // it CLAMPS to the end block and says nothing about having done so — the caller gets
                // a confident pose from the wrong instant. That clamp is the normal case for a
                // consumer whose data is fresher than the pose feed (a pose derived FROM a scan can
                // only arrive after it), and it registers a whole cloud with a pose |dt| ms old,
                // which is invisible at rest and turns into a bulk omega*dt rotation the moment the
                // robot turns.
                Interpolated,
                // Interpolated INSIDE the ring, and past its ends walk the pose along the twist
                // stored with the end block (rt_twist_linear / rt_twist_angular). Opt-in, because it
                // changes what every existing caller would receive.
                // ★NO HORIZON CAP, DELIBERATELY, AND THE CALLER MUST SUPPLY ONE. Skipping the walk is
                // not the neutral choice it looks like — it is the same extrapolation with the
                // velocity assumed to be ZERO, which is strictly worse than integrating a twist that
                // was actually measured. But nor may this run unbounded across a dead producer, and
                // cortex cannot know a caller's tolerance (measured policies in this fleet differ by
                // design, from 0.2 s to none at all). So it always reports how far it walked through
                // `applied_dt_ms`: bound it there, or watch it and say so.
                // Requires a twist on the edge; an edge without one (every static mount) falls
                // through to the Interpolated behaviour, which is what makes this safe to pass down
                // a whole chain.
                Extrapolated
            };

            // ── WHAT THE QUERY ACTUALLY DID, BECAUSE THE POSE CANNOT SAY ────────────────────────
            // ★A TIMESTAMPED RT QUERY THAT FALLS OUTSIDE THE RING RETURNS THE END BLOCK AND LOOKS
            // EXACTLY LIKE A SUCCESSFUL ONE. That silent clamp is the defect this whole channel has
            // been working around: a consumer asks for the pose at its capture stamp, gets a
            // confident matrix from a different instant, and registers a cloud or a mask against it.
            // It shows up downstream as a bulk omega*dt rotation that appears when the robot turns
            // and vanishes at rest -- which reads as a sensor or calibration fault, and has been
            // chased as one more than once.
            // Nothing in an RTMat can carry that. So the query reports it, and a caller that passes
            // this struct can no longer be lied to by omission.
            struct TimeQueryInfo
            {
                enum class Outcome
                {
                    Exact,          // the query landed on a block
                    Interpolated,   // bracketed between two blocks -- the healthy case
                    Extrapolated,   // past an end of the ring, walked along that block's twist
                    Clamped,        // past an end, returned the end block AS-IS. ★THE SILENT ONE.
                    Stale           // past an end by more than the ring's own span: too far for the
                                    // twist to be evidence, so the end block was returned unwalked
                };
                // ★SEVERITY IS NOT THE DECLARATION ORDER. A chain reports its worst edge, and ranking
                // runs Exact < Stale < Interpolated < Extrapolated < Clamped — see the note on
                // stale_edges for why Stale sits near the bottom rather than the top.
                Outcome outcome = Outcome::Exact;
                // Signed ms the pose was actually walked. NON-ZERO ONLY for Extrapolated -- it is
                // "how far did this move", not "how far was it asked to move".
                std::int64_t applied_dt_ms = 0;
                // Signed ms the query fell OUTSIDE the ring (+ past the newest block, - before the
                // oldest); 0 when it was inside. This is the number a clamp hides, and it is
                // reported for Clamped and Stale as well as Extrapolated.
                std::int64_t gap_ms = 0;
                // newest - oldest valid block on the edge: the timescale that channel itself says it
                // works on, and the bound on how far its twist may be asked to predict.
                std::int64_t ring_span_ms = 0;
                // How many edges in the chain came back Stale. ★THIS EXISTS BECAUSE THE SCALAR CANNOT
                // CARRY IT. Stale is ambiguous by nature: on a static mount it is simply what static
                // looks like (root->Shadow here is written once at bootstrap, so every timestamped
                // query is minutes past its ring, for ever), but on an edge whose producer has DIED it
                // is the only warning you get. Ranking it worst made every chain on this robot read
                // Stale and the field carried no information; ranking it mild lets a dead producer
                // hide behind healthier edges. So the ranking answers "how well was my instant
                // matched" and this counts the edges that opted out of time altogether — expected to
                // be a small constant for a given tree, and a CHANGE in it is the signal.
                int stale_edges = 0;
                // Convenience: did this pose come from an instant nobody measured?
                [[nodiscard]] bool clamped() const
                { return outcome == Outcome::Clamped or outcome == Outcome::Stale; }
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

            // ── EVERYTHING THAT BELONGS TO ONE RING BLOCK, IN ONE EDGE ROUND TRIP ───────────────
            // The overloads above write the pose (and optionally its covariance) into a history slot.
            // A producer that also measures the child's VELOCITY had no way to put it in the same slot:
            // it had to fetch the edge, add the twist attributes and insert_or_assign_edge it, then call
            // one of the above — two round trips, and the first one re-publishes the ring state as of
            // its own fetch. The documented workaround was to order the two writes so the ring advance
            // lands last. This overload removes the need for that rule: one fetch, one slot, one publish.
            //
            // ★TWIST AXIS ORDER. twist_linear/twist_angular are [x,y,z] in the CHILD frame's OWN AXES,
            // not the [adv, side, _] array order of the deprecated rt_translation_velocity. See the note
            // on rt_twist_linear in dsr_attr_name.h — a consumer must be able to write dp = R*v*dt
            // without knowing the producer's body convention, or the convention has to be re-encoded
            // (and mis-encoded) in every consumer, which is exactly what happened.
            //
            // Any optional left empty is simply not written, so this overload can carry a pose alone.
            // Sizes are checked: translation/rotation/twists must be 3, covariances 36 (row-major SE(3),
            // [x,y,z,rx,ry,rz]), or it throws rather than silently packing a short block.
            struct RTBlock
            {
                std::vector<float> translation;                     // [x,y,z] in the PARENT frame
                std::vector<float> rotation_euler;                  // [rx,ry,rz]
                std::optional<std::vector<float>> covariance;       // 36, pose
                std::optional<std::vector<float>> twist_linear;     // [vx,vy,vz] m/s,   CHILD axes
                std::optional<std::vector<float>> twist_angular;    // [wx,wy,wz] rad/s, CHILD axes
                std::optional<std::vector<float>> twist_covariance; // 36, velocity
            };
            void insert_or_assign_edge_RT(Node &n, uint64_t to, RTBlock block,
                                          std::optional<uint64_t> timestamp = std::nullopt);

            // ── A PARAMETER, NOT A STATE: one block, NO timestamps, overwritten in place ───────
            // For a transform that does not vary with time — a sensor mount, a body offset, a camera
            // extrinsic being refined by self-calibration. It marks the edge rt_static, so the
            // timestamped overloads above will not silently promote it to a ring.
            // ★WHY UNTIMESTAMPED IS THE CORRECT STORAGE, not merely a cheaper one. A parameter has no
            // validity interval. Give it one and every timestamped query falls outside it — for ever,
            // and by a margin that grows one second per second — so the edge reports a clamp it did
            // not earn and drags down every chain it sits in. Untimestamped, get_edge_RT_as_rtmat
            // returns the single block for any timestamp and reports Exact, which is the truth.
            // ★RE-ESTIMATION OVERWRITES. Self-calibration produces successively better estimates of
            // ONE constant, so the newest is the best answer for every frame, including ones captured
            // before it was computed. Keeping a history here would let an old frame fetch a WORSE
            // estimate, which is the opposite of what the caller wants.
            void insert_or_assign_edge_RT_static(Node &n, uint64_t to, const std::vector<float> &trans,
                                                 const std::vector<float> &rot_euler);
            bool insert_or_assign_edge_RT_static(uint64_t node_id, uint64_t to, const std::vector<float> &trans,
                                                 const std::vector<float> &rot_euler);

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
            // Pass `info` to learn what the query did — above all whether it CLAMPED (see TimeQueryInfo).
            // Optional so every existing call site is unchanged; a caller that omits it gets exactly the
            // behaviour it had, including the silent clamp.
            std::optional<Mat::RTMat> get_edge_RT_as_rtmat(const Edge &edge, std::uint64_t timestamp = 0, TimeQuery time_query = TimeQuery::Nearest,
                                                           TimeQueryInfo *info = nullptr);
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
            void insert_or_assign_edge_RT_impl(Node &n, uint64_t to, RTBlock block, std::optional<uint64_t> timestamp);

            // Shared BFS over the RT subtree rooted at start_id, re-deriving level = parent.level+1
            // and parent = RT source for every descendant. With repair=true it writes corrections;
            // with report=true it logs each defect/fix. Returns true if the subtree was consistent.
            // Used by insert_or_assign_edge_RT (cascade after a re-parent) and check_RT_tree (whole tree).
            bool walk_and_fix_levels(uint64_t start_id, bool repair, bool report);

    };
}

#endif
