#include <algorithm>
#include <mutex>
#include <cmath>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <dsr/api/dsr_rt_api.h>
#include <dsr/api/dsr_api.h>
#include <dsr/api/dsr_crdt_sync_engine.h>
#include <dsr/core/profiling.h>

using namespace DSR;

namespace
{
    constexpr std::size_t RT_BLOCK_SIZE = 3;
    constexpr std::size_t RT_COVARIANCE_BLOCK_SIZE = 36;
    using TimedBlock = std::pair<std::uint64_t, std::size_t>;
    using CovarianceMatrix = Eigen::Matrix<double, 6, 6>;

    Eigen::Vector3d translation_at(const std::vector<float> &translation_pack, std::size_t block_index)
    {
        return {translation_pack[block_index], translation_pack[block_index + 1], translation_pack[block_index + 2]};
    }

    Eigen::Quaterniond rotation_at(const std::vector<float> &rotation_pack, std::size_t block_index)
    {
        return Eigen::AngleAxisd(rotation_pack[block_index], Eigen::Vector3d::UnitX()) *
               Eigen::AngleAxisd(rotation_pack[block_index + 1], Eigen::Vector3d::UnitY()) *
               Eigen::AngleAxisd(rotation_pack[block_index + 2], Eigen::Vector3d::UnitZ());
    }

    Mat::RTMat make_rtmat(const Eigen::Vector3d &translation, const Eigen::Quaterniond &rotation)
    {
        return Mat::RTMat(Eigen::Translation3d(translation) * rotation);
    }

    Mat::RTMat rtmat_at(const std::vector<float> &translation_pack, const std::vector<float> &rotation_pack, std::size_t block_index)
    {
        return make_rtmat(translation_at(translation_pack, block_index), rotation_at(rotation_pack, block_index));
    }

    // ── WALK A BLOCK ALONG ITS OWN TWIST ────────────────────────────────────────────────────────
    // Exp(xi*dt) for the twist stored WITH that block, composed on the RIGHT of the block's pose:
    //
    //     parent<-child(t) = parent<-child(t_block) . child(t_block)<-child(t)
    //
    // Right-multiplication is not a detail, it is what makes this frame-agnostic. The twist is
    // expressed in the CHILD's own axes (see rt_twist_linear in dsr_attr_name.h), so the increment is
    // built entirely in the child frame and needs NOTHING from the base pose — no yaw extraction, no
    // assumption that the parent is level. A version that rotates the displacement into the parent
    // frame first has to dig a heading out of the base matrix, which is only meaningful for a planar
    // chain; this one is correct for any parent.
    //
    // ★PLANAR BY CONSTRUCTION. Only vx, vy and wz are used: the producers of this channel measure a
    // ground robot's odometry, and inventing a z/roll/pitch prediction from a twist nobody measures
    // would be worse than leaving those components alone. dt may be NEGATIVE (walk back before the
    // oldest block); the algebra is identical.
    Mat::RTMat extrapolate_along_twist(const Mat::RTMat &base,
                                       const std::vector<float> &twist_linear,
                                       const std::vector<float> &twist_angular,
                                       std::size_t block_index, double dt_s)
    {
        const double vx = twist_linear[block_index] * dt_s;      // displacement over dt, child axes
        const double vy = twist_linear[block_index + 1] * dt_s;
        const double phi = twist_angular[block_index + 2] * dt_s; // yaw swept over dt
        // SE(2) exponential. The left Jacobian turns the body velocity into the CHORD of the arc the
        // child actually drove; v*dt alone follows the tangent and cuts the corner by phi^2/24 -- at
        // phi = 0.1 rad that is already ~0.2% of the displacement, and free to do right. The limit as
        // phi -> 0 is the straight step, which is why the guard returns v*dt rather than dividing.
        double dx = vx, dy = vy;
        if (std::abs(phi) > 1e-9)
        {
            const double sn = std::sin(phi), cs = std::cos(phi);
            dx = ( sn * vx - (1.0 - cs) * vy) / phi;
            dy = ((1.0 - cs) * vx +  sn   * vy) / phi;
        }
        Mat::RTMat delta(Mat::RTMat::Identity());
        delta.linear() = Eigen::AngleAxisd(phi, Eigen::Vector3d::UnitZ()).toRotationMatrix();
        delta.translation() = Eigen::Vector3d(dx, dy, 0.0);
        return Mat::RTMat(base * delta);
    }

    CovarianceMatrix covariance_at(const std::vector<float> &covariance_pack, std::size_t block_index)
    {
        CovarianceMatrix covariance = CovarianceMatrix::Zero();
        for (std::size_t row = 0; row < 6; ++row)
            for (std::size_t col = 0; col < 6; ++col)
                covariance(static_cast<Eigen::Index>(row), static_cast<Eigen::Index>(col)) =
                    covariance_pack[block_index + row * 6 + col];
        return covariance;
    }

    std::size_t latest_slot_from_head(std::size_t head_index, std::size_t history_size)
    {
        const auto next_slot = (head_index / RT_BLOCK_SIZE) % history_size;
        return (next_slot == 0) ? history_size - 1 : next_slot - 1;
    }

    std::vector<TimedBlock> valid_blocks(const std::vector<std::uint64_t> &timestamps, std::size_t block_count)
    {
        std::vector<TimedBlock> blocks;
        blocks.reserve(block_count);
        for (std::size_t i = 0; i < block_count; ++i)
        {
            if (timestamps[i] == 0)
                continue;
            blocks.emplace_back(timestamps[i], i);
        }

        std::sort(blocks.begin(), blocks.end(), [](const TimedBlock &lhs, const TimedBlock &rhs)
        {
            return lhs.first < rhs.first;
        });

        return blocks;
    }

    std::vector<TimedBlock> valid_blocks(const std::vector<std::uint64_t> &timestamps,
                                         const std::vector<float> &translation_pack,
                                         const std::vector<float> *rotation_pack = nullptr)
    {
        std::size_t block_count = std::min(timestamps.size(), translation_pack.size() / RT_BLOCK_SIZE);
        if (rotation_pack != nullptr)
            block_count = std::min(block_count, rotation_pack->size() / RT_BLOCK_SIZE);
        auto blocks = valid_blocks(timestamps, block_count);
        for (auto &[timestamp, block_index] : blocks)
            block_index *= RT_BLOCK_SIZE;
        return blocks;
    }

    std::optional<std::size_t> nearest_block_index(const std::vector<TimedBlock> &blocks, std::uint64_t timestamp)
    {
        if (blocks.empty())
            return {};

        auto nearest = std::min_element(blocks.begin(), blocks.end(), [timestamp](const TimedBlock &lhs, const TimedBlock &rhs)
        {
            return std::llabs(static_cast<long long>(lhs.first) - static_cast<long long>(timestamp)) <
                   std::llabs(static_cast<long long>(rhs.first) - static_cast<long long>(timestamp));
        });

        return nearest->second;
    }

    std::pair<TimedBlock, TimedBlock> bracketing_blocks(const std::vector<TimedBlock> &blocks, std::uint64_t timestamp)
    {
        auto upper = std::lower_bound(blocks.begin(), blocks.end(), timestamp, [](const TimedBlock &block, std::uint64_t ts)
        {
            return block.first < ts;
        });

        if (upper == blocks.begin())
            return {*upper, *upper};
        if (upper == blocks.end())
            return {blocks.back(), blocks.back()};
        if (upper->first == timestamp)
            return {*upper, *upper};

        return {*std::prev(upper), *upper};
    }

    double interpolation_factor(const TimedBlock &lower, const TimedBlock &upper, std::uint64_t timestamp)
    {
        if (lower.first == upper.first)
            return 0.0;

        return std::clamp(static_cast<double>(timestamp - lower.first) / static_cast<double>(upper.first - lower.first), 0.0, 1.0);
    }
}

RT_API::RT_API(DSR::DSRGraph *G_)
{
    G = G_;
}

std::optional<Edge> RT_API::get_edge_RT(const Node &n, uint64_t to, const std::string &edge_type)
{
    if( not DSR::DSRGraph::is_valid_edge_type(edge_type)) 
        return {};
    auto edges_ = n.fano();
    auto res = edges_.find({to, edge_type});
    if (res != edges_.end())
        return res->second;
    else
        return {};
}

std::optional<Mat::RTMat> RT_API::get_RT_pose_from_parent(const Node &n, const std::string &edge_type)
{
    CORTEX_PROFILE_ZONE_N("RT_API::get_RT_pose_from_parent");
    if( not DSR::DSRGraph::is_valid_edge_type(edge_type)) 
        return {};
    auto p = G->get_parent_node(n);
    if (p.has_value())
    {
        auto edges_ = p->fano();
        auto res = edges_.find({n.id(), edge_type});
        if (res != edges_.end())
        {
            auto r = G->get_attrib_by_name<rt_rotation_euler_xyz_att>(res->second);
            auto t = G->get_attrib_by_name<rt_translation_att>(res->second);
            if (r.has_value() && t.has_value() )
            {
                Mat::RTMat rt(Eigen::Translation3d(t->get()[0], t->get()[1], t->get()[2]) *
                              Eigen::AngleAxisd(r->get()[0], Eigen::Vector3d::UnitX()) *
                              Eigen::AngleAxisd(r->get()[1], Eigen::Vector3d::UnitY()) *
                              Eigen::AngleAxisd(r->get()[2], Eigen::Vector3d::UnitZ()));
                return rt;
            }
        }
    }
    return {};
}

std::optional<Mat::RTMat>  RT_API::get_edge_RT_as_rtmat(const Edge &edge, std::uint64_t timestamp, TimeQuery time_query,
                                                        TimeQueryInfo *info)
{
    CORTEX_PROFILE_ZONE_N("RT_API::get_edge_RT_as_rtmat");
    if (info != nullptr) *info = TimeQueryInfo{};
    auto r_o = G->get_attrib_by_name<rt_rotation_euler_xyz_att>(edge);
    auto t_o =  G->get_attrib_by_name<rt_translation_att>(edge);
    auto head_o = G->get_attrib_by_name<rt_head_index_att>(edge);
    auto tstamps_o = G->get_attrib_by_name<rt_timestamps_att>(edge);
    if (r_o.has_value() and t_o.has_value() and t_o.value().get().size() >= 3 and r_o.value().get().size() >= 3)
    {
        const auto &t = t_o.value().get();
        const auto &r = r_o.value().get();
        if (timestamp == 0)  // return with the first 3 elements of the arrays
        {
            if (head_o.has_value())
            {
                const auto &head = prev(head_o.value(), t.size(), 3);
                return rtmat_at(t, r, head);
            }
            else
                return rtmat_at(t, r, 0);
        }
        else  // timestamp not 0
        {
            if (head_o.has_value() and tstamps_o.has_value())
            {
                const auto &tstamps = tstamps_o.value().get();
                const auto blocks = valid_blocks(tstamps, t, &r);
                if (not blocks.empty())
                {
                    if (time_query == TimeQuery::Interpolated or time_query == TimeQuery::Extrapolated)
                    {
                        const auto [lower, upper] = bracketing_blocks(blocks, timestamp);
                        if (info != nullptr)
                            info->ring_span_ms = static_cast<std::int64_t>(blocks.back().first)
                                               - static_cast<std::int64_t>(blocks.front().first);
                        if (lower.second == upper.second)
                        {
                            // ── THE CLAMP, AND THE ONLY PLACE Extrapolated DIFFERS ──────────────
                            // bracketing_blocks returns the same block twice when the query fell
                            // outside the ring (or landed exactly on a block, where there is nothing
                            // to predict because dt is 0). Interpolated stops here and hands back a
                            // pose from the wrong instant; Extrapolated walks it onto the instant
                            // that was asked for, using the twist stored WITH this block.
                            const auto base = rtmat_at(t, r, lower.second);
                            // Landing exactly ON a block is not a clamp — it is the best possible
                            // answer, and it must not be reported as a defect.
                            const std::int64_t gap_ms = static_cast<std::int64_t>(timestamp)
                                                      - static_cast<std::int64_t>(lower.first);
                            // ── CLASSIFY BEFORE DECIDING WHETHER TO ACT ─────────────────────────
                            // ★MEASURED DEFECT, 2026-09-10: Stale used to be assigned only inside the
                            // Extrapolated branch below, so EVERY Interpolated caller — which is every
                            // object fitter and room_concept's camera path — reported Clamped whether
                            // the edge was a live ring that missed by 40 ms or a mount frozen for four
                            // minutes. room_concept's probe read clamped=100% with max|gap| climbing
                            // 177 s -> 237 s at exactly 1 s per second (a frozen root->Shadow mount
                            // dominating the chain's worst edge) while stale_edges stayed 0, because
                            // nothing on that path could ever set it. A field that cannot vary is not
                            // evidence, and the distinction was missing precisely where it was needed.
                            // So the CLASSIFICATION is unconditional; only the ACTION stays gated on
                            // the enum. A caller asking for Interpolated now learns which of the two
                            // it got, and can ignore the structural one.
                            const std::int64_t ring_span_ms = static_cast<std::int64_t>(blocks.back().first)
                                                            - static_cast<std::int64_t>(blocks.front().first);
                            const bool beyond_span = (ring_span_ms <= 0) or (std::llabs(gap_ms) > ring_span_ms);
                            if (info != nullptr)
                            {
                                info->gap_ms = gap_ms;
                                info->outcome = (gap_ms == 0)  ? TimeQueryInfo::Outcome::Exact
                                              : beyond_span    ? TimeQueryInfo::Outcome::Stale
                                                               : TimeQueryInfo::Outcome::Clamped;
                            }
                            if (time_query != TimeQuery::Extrapolated)
                                return base;
                            std::optional<std::vector<float>> twl = G->get_attrib_by_name<rt_twist_linear_att>(edge);
                            std::optional<std::vector<float>> twa = G->get_attrib_by_name<rt_twist_angular_att>(edge);
                            // No twist on this edge -> behave exactly as Interpolated. Every static
                            // mount takes this path, which is what lets a caller pass Extrapolated
                            // down a whole chain and have only the moving edge respond to it.
                            if (not twl.has_value() or not twa.has_value()
                                or twl->size() <= lower.second + 2 or twa->size() <= lower.second + 2)
                                return base;   // no twist here: stays Clamped, and now says so
                            const std::int64_t dt_ms = gap_ms;
                            if (dt_ms == 0)
                                return base;   // Exact, set above
                            // ── HOW FAR MAY A TWIST BE ASKED TO PREDICT? THE RING ANSWERS. ──────
                            // ★MEASURED BUG, 2026-09-09, and it is why this is not left to the caller.
                            // On this robot the chain is root -> Shadow -> room. room_concept
                            // publishes the live twist on Shadow->room, but root->Shadow ALSO carries
                            // twist attributes, written once and then never again. Asked for a pose
                            // 60 ms ahead, this walked that stale edge 1,939,575 ms — 32 minutes —
                            // and reported it. The pose damage was ~0 only because that particular
                            // twist is ~0; the REPORTED dt was not, and since a chain reports the
                            // largest dt of any edge, every caller's horizon cap would have fired and
                            // silently disabled extrapolation fleet-wide while appearing to work.
                            // ★THE BOUND IS THE EDGE'S OWN RING SPAN, not a constant. A twist is
                            // evidence about motion on the timescale it was sampled at; predicting one
                            // ring-span ahead is an extrapolation, predicting ten thousand of them is
                            // not a less accurate answer but an unjustified one. The span is what that
                            // channel itself says its timescale is (~204 ms measured on the live
                            // Shadow->room edge), it costs no configuration, and it adapts to a
                            // producer that speeds up or slows down.
                            // ★REFUSING IS REPORTED AS applied_dt_ms == 0, i.e. "this pose was not
                            // walked" — which is exactly what a caller needs to know, and is why the
                            // refusal returns the clamped block rather than a partially-walked pose.
                            // Staleness is a different question with a different instrument: read the
                            // ring bounds if that is what you want to know.
                            if (beyond_span)
                            {
                                // Too far outside for the twist to be evidence. The end block is
                                // still the best available answer, so it is returned — already
                                // labelled Stale above, never as a silent success.
                                return base;
                            }
                            if (info != nullptr)
                            {
                                info->outcome = TimeQueryInfo::Outcome::Extrapolated;
                                info->applied_dt_ms = dt_ms;
                            }
                            return extrapolate_along_twist(base, twl.value(), twa.value(), lower.second,
                                                           static_cast<double>(dt_ms) * 1e-3);
                        }

                        if (info != nullptr) info->outcome = TimeQueryInfo::Outcome::Interpolated;
                        const auto alpha = interpolation_factor(lower, upper, timestamp);
                        auto lower_rotation = rotation_at(r, lower.second);
                        auto upper_rotation = rotation_at(r, upper.second);
                        if (lower_rotation.dot(upper_rotation) < 0.0)
                            upper_rotation.coeffs() *= -1.0;

                        const Eigen::Vector3d interpolated_translation =
                            (1.0 - alpha) * translation_at(t, lower.second) + alpha * translation_at(t, upper.second);
                        auto interpolated_rtmat = make_rtmat(interpolated_translation,
                                                             lower_rotation.slerp(alpha, upper_rotation).normalized());
                        return interpolated_rtmat;
                    }

                    if (auto bix = nearest_block_index(blocks, timestamp); bix.has_value())
                        return rtmat_at(t, r, bix.value());
                }
            }

            return rtmat_at(t, r, 0);
        }
    }
    else
    {
        qWarning() << __FUNCTION__ << "NO translation or rotation found in RT edge from node " << edge.from() << " to: " << edge.to();
        return {};
    }
}

std::optional<Eigen::Matrix<double, 6, 6>> RT_API::get_edge_RT_covariance(const Edge &edge, std::uint64_t timestamp, TimeQuery time_query, CovarianceKind kind)
{
    std::optional<std::reference_wrapper<const std::vector<float>>> covariance_o;
    switch (kind)
    {
        case CovarianceKind::Pose:         covariance_o = G->get_attrib_by_name<rt_covariance_att>(edge); break;
        case CovarianceKind::Velocity:     covariance_o = G->get_attrib_by_name<rt_covariance_velocity_att>(edge); break;
        case CovarianceKind::Acceleration: covariance_o = G->get_attrib_by_name<rt_covariance_acceleration_att>(edge); break;
    }
    auto head_o = G->get_attrib_by_name<rt_head_index_att>(edge);
    auto tstamps_o = G->get_attrib_by_name<rt_timestamps_att>(edge);

    if (not covariance_o.has_value() || covariance_o->get().size() < RT_COVARIANCE_BLOCK_SIZE)
        return {};

    const auto &covariance_pack = covariance_o->get();
    if (timestamp == 0)
    {
        if (head_o.has_value() && covariance_pack.size() >= RT_COVARIANCE_BLOCK_SIZE * HISTORY_SIZE)
        {
            const auto block_index = latest_slot_from_head(static_cast<std::size_t>(head_o.value()), HISTORY_SIZE) * RT_COVARIANCE_BLOCK_SIZE;
            if (block_index + RT_COVARIANCE_BLOCK_SIZE <= covariance_pack.size())
                return covariance_at(covariance_pack, block_index);
        }
        return covariance_at(covariance_pack, 0);
    }

    if (head_o.has_value() && tstamps_o.has_value() && covariance_pack.size() >= RT_COVARIANCE_BLOCK_SIZE * 2)
    {
        const auto &timestamps = tstamps_o->get();
        const auto blocks = valid_blocks(timestamps, covariance_pack.size() / RT_COVARIANCE_BLOCK_SIZE);
        if (not blocks.empty())
        {
            if (time_query == TimeQuery::Interpolated or time_query == TimeQuery::Extrapolated)
            {
                const auto [lower, upper] = bracketing_blocks(blocks, timestamp);
                const auto lower_index = lower.second * RT_COVARIANCE_BLOCK_SIZE;
                const auto upper_index = upper.second * RT_COVARIANCE_BLOCK_SIZE;
                if (lower.second == upper.second)
                {
                    auto base = covariance_at(covariance_pack, lower_index);
                    if (time_query != TimeQuery::Extrapolated or kind != CovarianceKind::Pose)
                        return base;
                    // ── A WALKED POSE IS LESS CERTAIN THAN A MEASURED ONE, AND MUST SAY SO ───────
                    // get_edge_RT_as_rtmat walks the end block along its twist onto the instant asked
                    // for. Until now this function handed back that block's covariance UNCHANGED, so
                    // an extrapolated pose claimed exactly the confidence of the measurement it was
                    // predicted from -- position the query invented, reported as if observed. A caller
                    // weighting a sensor reading against that pose is then overconfident by construction,
                    // and nothing in the data can reveal it.
                    // ★THIS IS ALSO THE RIGHT ANSWER TO "THE CONSTANT-TWIST MODEL IS IMPERFECT". The
                    // tempting fix is a higher-order term, but the acceleration would have to be
                    // differenced out of the twist ring, and measured on 834 moving samples that
                    // estimate is dominated by noise: lag-1 correlation -0.41 (a real acceleration is
                    // POSITIVELY correlated; differentiated white noise tends to -0.5) with 61% sign
                    // flips. Correcting with it would add variance. Growing the covariance with dt is
                    // the honest form of the same knowledge.
                    // ★FRAME. The twist covariance is in the CHILD's own axes; the pose covariance is
                    // in the PARENT's. Because the walk right-composes -- T = T_base * exp(xi*dt) -- a
                    // twist perturbation is a BODY perturbation of the result, so mapping it onto the
                    // pose parameters is the base block's rotation and nothing else. There is no
                    // [t]x R adjoint term here: we perturb the pose, not a spatial twist about the
                    // parent origin.
                    const std::int64_t gap_ms = static_cast<std::int64_t>(timestamp)
                                              - static_cast<std::int64_t>(lower.first);
                    const std::int64_t ring_span_ms = static_cast<std::int64_t>(blocks.back().first)
                                                    - static_cast<std::int64_t>(blocks.front().first);
                    // Exactly the bound the pose walk uses, and it must stay exactly that: a refused
                    // walk returns the end block unwalked, so inflating it would charge the caller for
                    // a prediction it never received. Landing ON a block is not a walk either.
                    if (gap_ms == 0 or ring_span_ms <= 0 or std::llabs(gap_ms) > ring_span_ms)
                        return base;

                    const auto twcov_o = G->get_attrib_by_name<rt_covariance_velocity_att>(edge);
                    if (not twcov_o.has_value()
                        or twcov_o->get().size() < lower_index + RT_COVARIANCE_BLOCK_SIZE)
                        return base;   // no twist covariance here (every static mount): report what was measured
                    const auto twist_covariance = covariance_at(twcov_o->get(), lower_index);

                    Eigen::Matrix3d child_to_parent = Eigen::Matrix3d::Identity();
                    if (const auto rot_o = G->get_attrib_by_name<rt_rotation_euler_xyz_att>(edge);
                        rot_o.has_value() and rot_o->get().size() >= lower.second * RT_BLOCK_SIZE + RT_BLOCK_SIZE)
                        child_to_parent = rotation_at(rot_o->get(), lower.second * RT_BLOCK_SIZE).toRotationMatrix();

                    const double dt_s = static_cast<double>(gap_ms) * 1e-3;
                    Eigen::Matrix<double, 6, 6> jacobian = Eigen::Matrix<double, 6, 6>::Zero();
                    jacobian.topLeftCorner<3, 3>()     = child_to_parent * dt_s;
                    jacobian.bottomRightCorner<3, 3>() = child_to_parent * dt_s;
                    return CovarianceMatrix(base + jacobian * twist_covariance * jacobian.transpose());
                }

                const auto alpha = interpolation_factor(lower, upper, timestamp);
                const auto lower_covariance = covariance_at(covariance_pack, lower_index);
                const auto upper_covariance = covariance_at(covariance_pack, upper_index);
                return (1.0 - alpha) * lower_covariance + alpha * upper_covariance;
            }

            if (auto block_index = nearest_block_index(blocks, timestamp); block_index.has_value())
                return covariance_at(covariance_pack, block_index.value() * RT_COVARIANCE_BLOCK_SIZE);
        }
    }

    return covariance_at(covariance_pack, 0);
}

std::optional<Eigen::Vector3d> RT_API::get_translation(const Node &n, uint64_t to, std::uint64_t timestamp, TimeQuery time_query)
{
    CORTEX_PROFILE_ZONE_N("RT_API::get_translation(node,to)");
    if( auto edge = get_edge_RT(n, to); edge.has_value())
    {
        auto t_o = G->get_attrib_by_name<rt_translation_att>(edge.value());
        auto head_o = G->get_attrib_by_name<rt_head_index_att>(edge.value());
        auto tstamps_o = G->get_attrib_by_name<rt_timestamps_att>(edge.value());
        if (t_o.has_value() and t_o.value().get().size() >= 3)
        {
            const auto &t = t_o.value().get();
            if (timestamp == 0)
            {
                if (head_o.has_value() and tstamps_o.has_value()){
                    auto h = prev(head_o.value(), t.size(), 3);
                    return translation_at(t, h);
                } else
                    return translation_at(t, 0);
            }
            else  // timestamp not 0
            {
                if (head_o.has_value() and tstamps_o.has_value())
                {
                    const auto &tstamps = tstamps_o.value().get();
                    const auto blocks = valid_blocks(tstamps, t);
                    if (not blocks.empty())
                    {
                        if (time_query == TimeQuery::Interpolated)
                        {
                            const auto [lower, upper] = bracketing_blocks(blocks, timestamp);
                            if (lower.second == upper.second)
                                return translation_at(t, lower.second);

                            const auto alpha = interpolation_factor(lower, upper, timestamp);
                            return (1.0 - alpha) * translation_at(t, lower.second) + alpha * translation_at(t, upper.second);
                        }

                        if (auto bix = nearest_block_index(blocks, timestamp); bix.has_value())
                            return translation_at(t, bix.value());
                    }
                }

                qWarning() << __FUNCTION__ << " Not timestamp or not head found in RT edge from node "  << QString::fromStdString(n.name()) << " to: " << to << " Returning first element in array";
                return translation_at(t, 0);
            }
        }
        else
        {
            qWarning() << __FUNCTION__ << " NO translation found in RT edge from node " << QString::fromStdString(n.name()) << " to: " << to ;
            return {};
        }
    }
    else
    {
        qWarning() << __FUNCTION__ << "NO RT edge found from node " << QString::fromStdString(n.name()) << " to: " << to ;
        return {};
    }
}

std::optional<Eigen::Vector3d> RT_API::get_translation(uint64_t node_id, uint64_t to, std::uint64_t timestamp, TimeQuery time_query)
{
    if( const auto node = G->get_node(node_id); node.has_value())
        return get_translation(node.value(), to, timestamp, time_query);
    else
        return {};
}

std::optional<Eigen::Matrix<double, 6, 6>> RT_API::get_covariance_matrix(const Node &n, uint64_t to, std::uint64_t timestamp, TimeQuery time_query, CovarianceKind kind)
{
    if (auto edge = get_edge_RT(n, to); edge.has_value())
        return get_edge_RT_covariance(edge.value(), timestamp, time_query, kind);
    return {};
}

std::optional<Eigen::Matrix<double, 6, 6>> RT_API::get_covariance_matrix(uint64_t node_id, uint64_t to, std::uint64_t timestamp, TimeQuery time_query, CovarianceKind kind)
{
    if (const auto node = G->get_node(node_id); node.has_value())
        return get_covariance_matrix(node.value(), to, timestamp, time_query, kind);
    return {};
}

bool RT_API::insert_or_assign_edge_RT_covariance(const Node &n, uint64_t to, CovarianceKind kind,
                                                 const std::vector<float> &covariance, std::optional<uint64_t> timestamp)
{
    CORTEX_PROFILE_ZONE_N("RT_API::insert_or_assign_edge_RT_covariance");
    if (covariance.size() != RT_COVARIANCE_BLOCK_SIZE)
        throw std::runtime_error("RT covariance must contain exactly 36 float values");

    auto edge_o = G->get_edge(n.id(), to, "RT");
    if (not edge_o.has_value())
    {
        qWarning() << __FUNCTION__ << "NO RT edge found from node" << QString::fromStdString(n.name()) << "to:" << to;
        return false;
    }
    auto edge = std::move(edge_o.value());

    std::optional<std::vector<float>> pack_o;
    switch (kind)
    {
        case CovarianceKind::Pose:         pack_o = G->get_attrib_by_name<rt_covariance_att>(edge); break;
        case CovarianceKind::Velocity:     pack_o = G->get_attrib_by_name<rt_covariance_velocity_att>(edge); break;
        case CovarianceKind::Acceleration: pack_o = G->get_attrib_by_name<rt_covariance_acceleration_att>(edge); break;
    }

    const auto head_o = G->get_attrib_by_name<rt_head_index_att>(edge);
    std::vector<float> pack;
    if (HISTORY_SIZE > 0 and head_o.has_value())
    {
        // The edge keeps a history ring: land the block on the slot the pose payload is using,
        // so a later timestamped read pairs this covariance with the transform it belongs to.
        pack = pack_o.value_or(std::vector<float>(RT_COVARIANCE_BLOCK_SIZE * HISTORY_SIZE, 0.f));
        if (pack.size() < RT_COVARIANCE_BLOCK_SIZE * HISTORY_SIZE)
            pack.resize(RT_COVARIANCE_BLOCK_SIZE * HISTORY_SIZE);

        auto slot = latest_slot_from_head(static_cast<std::size_t>(head_o.value()), HISTORY_SIZE);
        if (timestamp.has_value())
        {
            if (const auto tstamps_o = G->get_attrib_by_name<rt_timestamps_att>(edge); tstamps_o.has_value())
            {
                const auto &tstamps = tstamps_o.value().get();
                const auto blocks = valid_blocks(tstamps, std::min<std::size_t>(tstamps.size(), HISTORY_SIZE));
                if (const auto block_index = nearest_block_index(blocks, timestamp.value()); block_index.has_value())
                    slot = block_index.value();
            }
        }
        std::copy(covariance.begin(), covariance.end(), pack.begin() + slot * RT_COVARIANCE_BLOCK_SIZE);
    }
    else
        pack = covariance;   // no history on this edge: a single flat 6x6 block

    switch (kind)
    {
        case CovarianceKind::Pose:         G->add_or_modify_attrib_local<rt_covariance_att>(edge, std::move(pack)); break;
        case CovarianceKind::Velocity:     G->add_or_modify_attrib_local<rt_covariance_velocity_att>(edge, std::move(pack)); break;
        case CovarianceKind::Acceleration: G->add_or_modify_attrib_local<rt_covariance_acceleration_att>(edge, std::move(pack)); break;
    }

    return G->insert_or_assign_edge(std::move(edge));
}

bool RT_API::insert_or_assign_edge_RT_covariance(const Node &n, uint64_t to, CovarianceKind kind,
                                                 const Eigen::Matrix<double, 6, 6> &covariance, std::optional<uint64_t> timestamp)
{
    std::vector<float> packed(RT_COVARIANCE_BLOCK_SIZE);
    for (std::size_t row = 0; row < 6; ++row)
        for (std::size_t col = 0; col < 6; ++col)
            packed[row * 6 + col] = static_cast<float>(covariance(static_cast<Eigen::Index>(row), static_cast<Eigen::Index>(col)));
    return insert_or_assign_edge_RT_covariance(n, to, kind, packed, timestamp);
}

bool RT_API::insert_or_assign_edge_RT_covariance(uint64_t node_id, uint64_t to, CovarianceKind kind,
                                                 const std::vector<float> &covariance, std::optional<uint64_t> timestamp)
{
    if (const auto node = G->get_node(node_id); node.has_value())
        return insert_or_assign_edge_RT_covariance(node.value(), to, kind, covariance, timestamp);
    qWarning() << __FUNCTION__ << "NO node found with id" << node_id;
    return false;
}

bool RT_API::insert_or_assign_edge_RT_covariance(uint64_t node_id, uint64_t to, CovarianceKind kind,
                                                 const Eigen::Matrix<double, 6, 6> &covariance, std::optional<uint64_t> timestamp)
{
    if (const auto node = G->get_node(node_id); node.has_value())
        return insert_or_assign_edge_RT_covariance(node.value(), to, kind, covariance, timestamp);
    qWarning() << __FUNCTION__ << "NO node found with id" << node_id;
    return false;
}

void RT_API::insert_or_assign_edge_RT(Node &n, uint64_t to, const std::vector<float> &trans, const std::vector<float> &rot_euler, std::optional<uint64_t> timestamp)
{
    CORTEX_PROFILE_ZONE_N("RT_API::insert_or_assign_edge_RT(copy)");
    insert_or_assign_edge_RT_impl(n, to, RTBlock{.translation = trans, .rotation_euler = rot_euler}, timestamp);
}

void RT_API::insert_or_assign_edge_RT(Node &n, uint64_t to, std::vector<float> &&trans, std::vector<float> &&rot_euler, std::optional<uint64_t> timestamp)
{
    CORTEX_PROFILE_ZONE_N("RT_API::insert_or_assign_edge_RT(move)");
    insert_or_assign_edge_RT_impl(n, to, RTBlock{.translation = std::move(trans),
                                                 .rotation_euler = std::move(rot_euler)}, timestamp);
}

void RT_API::insert_or_assign_edge_RT(Node &n, uint64_t to, const std::vector<float> &trans, const std::vector<float> &rot_euler,
                                      const std::vector<float> &covariance, std::optional<uint64_t> timestamp)
{
    insert_or_assign_edge_RT_impl(n, to, RTBlock{.translation = trans, .rotation_euler = rot_euler,
                                                 .covariance = covariance}, timestamp);
}

void RT_API::insert_or_assign_edge_RT(Node &n, uint64_t to, std::vector<float> &&trans, std::vector<float> &&rot_euler,
                                      std::vector<float> &&covariance, std::optional<uint64_t> timestamp)
{
    insert_or_assign_edge_RT_impl(n, to, RTBlock{.translation = std::move(trans),
                                                 .rotation_euler = std::move(rot_euler),
                                                 .covariance = std::move(covariance)}, timestamp);
}

void RT_API::insert_or_assign_edge_RT(Node &n, uint64_t to, RTBlock block, std::optional<uint64_t> timestamp)
{
    CORTEX_PROFILE_ZONE_N("RT_API::insert_or_assign_edge_RT(block)");
    insert_or_assign_edge_RT_impl(n, to, std::move(block), timestamp);
}

void RT_API::insert_or_assign_edge_RT_static(Node &n, uint64_t to, const std::vector<float> &trans,
                                             const std::vector<float> &rot_euler)
{
    CORTEX_PROFILE_ZONE_N("RT_API::insert_or_assign_edge_RT_static");
    if (trans.size() < 3 or rot_euler.size() < 3)
        throw std::runtime_error("RT static edge needs 3-vectors for translation and rotation");

    auto edge = G->get_edge(n.id(), to, "RT").value_or(Edge::create<RT_edge_type>(n.id(), to));
    // ONE block, overwritten. No rt_timestamps and no rt_head_index: their ABSENCE is what makes
    // get_edge_RT_as_rtmat return this block for any timestamp and report Exact rather than a clamp.
    G->add_or_modify_attrib_local<rt_translation_att>(edge, std::vector<float>(trans.begin(), trans.begin() + 3));
    G->add_or_modify_attrib_local<rt_rotation_euler_xyz_att>(edge, std::vector<float>(rot_euler.begin(), rot_euler.begin() + 3));
    G->add_or_modify_attrib_local<rt_static_att>(edge, true);

    // ★The parent/level bookkeeping every RT write owes the tree, and — just as importantly — the same
    // FOUR failure checks the timestamped sibling makes. A static edge is not a second-class citizen:
    // it must be as loud on a bad write as a dynamic one, or a link into nothing lands in silence.
    // (It did: while this returned void and discarded every result, a dangling RT link in a bootstrap
    //  JSON was absorbed without a word, because the loader routes RT links here and skips the generic
    //  path's "Dest Node N does not exist" warning. Two months of a dangling body->210 link in
    //  shadow.json went unreported that way.)
    auto to_n = G->get_node(to);
    if (!to_n.has_value())
        throw std::runtime_error(
                "Destination node " + std::to_string(to) + " not found in G in insert_or_assign_edge_RT_static() " +
                __FILE__ + " " + __FUNCTION__ + " " + std::to_string(__LINE__));

    bool node_changed = false;
    if (auto x = G->get_attrib_by_name<parent_att>(*to_n); !x.has_value() or x.value() != n.id())
    {
        G->add_or_modify_attrib_local<parent_att>(*to_n, n.id());
        node_changed = true;
    }

    const auto n_level = G->get_node_level(n);
    if (!n_level.has_value())
        throw std::runtime_error(
                "Source node " + std::to_string(n.id()) + " has no level in insert_or_assign_edge_RT_static() " +
                __FILE__ + " " + __FUNCTION__ + " " + std::to_string(__LINE__));
    const auto next_level = n_level.value() + 1;
    bool level_changed = false;
    if (auto l = G->get_attrib_by_name<level_att>(*to_n); !l.has_value() or l.value() != next_level)
    {
        G->add_or_modify_attrib_local<level_att>(*to_n, next_level);
        node_changed = true;
        level_changed = true;
    }

    if (node_changed and !G->update_node(*to_n))
        throw std::runtime_error(
                "Could not update destination node " + std::to_string(to) + " in insert_or_assign_edge_RT_static() " +
                __FILE__ + " " + __FUNCTION__ + " " + std::to_string(__LINE__));

    // Only on an actual re-parent, matching the timestamped path: a value-only rewrite of a mount
    // leaves every level alone and must not pay for a subtree walk.
    if (level_changed)
        walk_and_fix_levels(to, /*repair*/ true, /*report*/ false);

    if (!G->insert_or_assign_edge(std::move(edge)))
        throw std::runtime_error(
                "Could not insert RT edge " + std::to_string(n.id()) + " -> " + std::to_string(to) +
                " in insert_or_assign_edge_RT_static() " + __FILE__ + " " + __FUNCTION__ + " " + std::to_string(__LINE__));
}

bool RT_API::insert_or_assign_edge_RT_static(uint64_t node_id, uint64_t to, const std::vector<float> &trans,
                                             const std::vector<float> &rot_euler)
{
    if (auto node = G->get_node(node_id); node.has_value())
    {
        insert_or_assign_edge_RT_static(node.value(), to, trans, rot_euler);
        return true;
    }
    qWarning() << __FUNCTION__ << "NO node found with id" << node_id;
    return false;
}

void RT_API::insert_or_assign_edge_RT_identity(Node &n, uint64_t to, std::optional<uint64_t> timestamp)
{
    CORTEX_PROFILE_ZONE_N("RT_API::insert_or_assign_edge_RT_identity");
    insert_or_assign_edge_RT_impl(n, to, RTBlock{.translation = std::vector<float>{0.f, 0.f, 0.f},
                                                 .rotation_euler = std::vector<float>{0.f, 0.f, 0.f}}, timestamp);
}

bool RT_API::insert_or_assign_edge_RT_identity(uint64_t node_id, uint64_t to, std::optional<uint64_t> timestamp)
{
    if (auto node = G->get_node(node_id); node.has_value())
    {
        insert_or_assign_edge_RT_identity(node.value(), to, timestamp);
        return true;
    }
    qWarning() << __FUNCTION__ << "NO node found with id" << node_id;
    return false;
}

void RT_API::insert_or_assign_edge_RT_impl(Node &n, uint64_t to, RTBlock block, std::optional<uint64_t> timestamp)
{
    auto &trans = block.translation;
    auto &rot_euler = block.rotation_euler;
    auto &covariance = block.covariance;

    // Reject a short payload rather than packing it: a block silently written 2-of-3 wide reads back
    // as a plausible pose with someone else's third component, and nothing downstream can detect it.
    const auto require = [](const std::optional<std::vector<float>> &v, std::size_t want, const char *what)
    {
        if (v.has_value() and v->size() != want)
            throw std::runtime_error(std::string("RT ") + what + " must contain exactly "
                                     + std::to_string(want) + " float values");
    };
    require(covariance, RT_COVARIANCE_BLOCK_SIZE, "covariance");
    require(block.twist_covariance, RT_COVARIANCE_BLOCK_SIZE, "twist covariance");
    require(block.twist_linear, static_cast<std::size_t>(BLOCK_SIZE), "linear twist");
    require(block.twist_angular, static_cast<std::size_t>(BLOCK_SIZE), "angular twist");

    const bool with_covariance = covariance.has_value();
    // ★THE TWIST PAIR IS ALL-OR-NOTHING. Half a twist is not a conservative subset of one — a reader
    // that finds a linear rate and no angular rate cannot tell "the child is not rotating" from "the
    // producer did not measure rotation", and the first reading turns a pivot into a straight line.
    const bool with_twist = block.twist_linear.has_value() and block.twist_angular.has_value();
    if (block.twist_linear.has_value() != block.twist_angular.has_value())
        throw std::runtime_error("RT twist must supply BOTH twist_linear and twist_angular, or neither");
    const bool with_twist_cov = with_twist and block.twist_covariance.has_value();

    auto edge = G->get_edge(n.id(), to, "RT").value_or(Edge::create<RT_edge_type>(n.id(), to));
    // ★A STATIC EDGE IS NOT PROMOTED TO A RING, EVER. Someone calling the timestamped overload on a
    // sensor mount is almost certainly refreshing a re-estimated extrinsic, not recording a state —
    // and giving that edge timestamps makes every timestamped query through it report a clamp for the
    // life of the process, growing one second per second, poisoning every chain it sits in. So the
    // edge's declared nature wins over the call site's choice of overload, and it says so once.
    if (G->get_attrib_by_name<rt_static_att>(edge).value_or(false))
    {
        static std::once_flag warned;
        std::call_once(warned, [&]{ qWarning() << "RT_API: timestamped write on an rt_static edge"
                                               << n.id() << "->" << to
                                               << "— storing as a single block. Use "
                                                  "insert_or_assign_edge_RT_static() for parameters."; });
        insert_or_assign_edge_RT_static(n, to, block.translation, block.rotation_euler);
        return;
    }
    if (HISTORY_SIZE <= 0)
    {
        G->add_or_modify_attrib_local<rt_translation_att>(edge, std::move(trans));
        G->add_or_modify_attrib_local<rt_rotation_euler_xyz_att>(edge, std::move(rot_euler));
        if (with_covariance)
            G->add_or_modify_attrib_local<rt_covariance_att>(edge, std::move(covariance.value()));
        if (with_twist)
        {
            G->add_or_modify_attrib_local<rt_twist_linear_att>(edge, std::move(block.twist_linear.value()));
            G->add_or_modify_attrib_local<rt_twist_angular_att>(edge, std::move(block.twist_angular.value()));
            if (with_twist_cov)
                G->add_or_modify_attrib_local<rt_covariance_velocity_att>(edge, std::move(block.twist_covariance.value()));
        }
    }
    else
    {
        auto head_o = G->get_attrib_by_name<rt_head_index_att>(edge);
        std::optional<std::vector<uint64_t>> tstamps_o = G->get_attrib_by_name<rt_timestamps_att>(edge);
        std::optional<std::vector<float>> tr_pack_o = G->get_attrib_by_name<rt_translation_att>(edge);
        std::optional<std::vector<float>> rot_pack_o = G->get_attrib_by_name<rt_rotation_euler_xyz_att>(edge);
        std::optional<std::vector<float>> cov_pack_o;
        auto time_stamps = tstamps_o.value_or(std::vector<std::uint64_t>(HISTORY_SIZE, 0));
        auto tr_pack = tr_pack_o.value_or(std::vector<float>(BLOCK_SIZE * HISTORY_SIZE, 0.f));
        auto rot_pack = rot_pack_o.value_or(std::vector<float>(BLOCK_SIZE * HISTORY_SIZE, 0.f));
        std::vector<float> cov_pack;
        if (with_covariance)
            cov_pack_o = G->get_attrib_by_name<rt_covariance_att>(edge);

        if (time_stamps.size() < HISTORY_SIZE) time_stamps.resize(HISTORY_SIZE);
        if (tr_pack.size() < BLOCK_SIZE * HISTORY_SIZE) tr_pack.resize(BLOCK_SIZE * HISTORY_SIZE);
        if (rot_pack.size() < BLOCK_SIZE * HISTORY_SIZE) rot_pack.resize(BLOCK_SIZE * HISTORY_SIZE);
        if (with_covariance)
        {
            cov_pack = cov_pack_o.value_or(std::vector<float>(RT_COVARIANCE_BLOCK_SIZE * HISTORY_SIZE, 0.f));
            if (cov_pack.size() < RT_COVARIANCE_BLOCK_SIZE * HISTORY_SIZE)
                cov_pack.resize(RT_COVARIANCE_BLOCK_SIZE * HISTORY_SIZE);
        }
        // ── THE TWIST RING, SLOTTED BY THE SAME HEAD INDEX AS THE POSE ─────────────────────────
        // Which is the whole reason it lives in here rather than beside the call: block i of the twist
        // is then the twist AT block i of the pose, and it inherits block i's timestamp. A twist
        // written through a separate edge round trip has no slot and no stamp, so a reader cannot pair
        // it with a transform, cannot tell whether it is fresh, and cannot integrate across an
        // interval. An edge that was carrying pose-only history starts its twist packs at zero and
        // fills them going forward; a reader must therefore treat an all-zero block as "no twist
        // recorded for this slot", which is what a stationary child looks like too — harmless, since
        // both mean "predict no motion".
        std::vector<float> twl_pack, twa_pack, twcov_pack;
        if (with_twist)
        {
            // The two-step through a value optional is not a style choice: get_attrib_by_name returns
            // optional<reference_wrapper<const vector>>, which cannot value_or a vector. Same shape as
            // tr_pack_o/rot_pack_o above.
            std::optional<std::vector<float>> twl_pack_o = G->get_attrib_by_name<rt_twist_linear_att>(edge);
            std::optional<std::vector<float>> twa_pack_o = G->get_attrib_by_name<rt_twist_angular_att>(edge);
            twl_pack = twl_pack_o.value_or(std::vector<float>(BLOCK_SIZE * HISTORY_SIZE, 0.f));
            twa_pack = twa_pack_o.value_or(std::vector<float>(BLOCK_SIZE * HISTORY_SIZE, 0.f));
            if (twl_pack.size() < BLOCK_SIZE * HISTORY_SIZE) twl_pack.resize(BLOCK_SIZE * HISTORY_SIZE);
            if (twa_pack.size() < BLOCK_SIZE * HISTORY_SIZE) twa_pack.resize(BLOCK_SIZE * HISTORY_SIZE);
            if (with_twist_cov)
            {
                std::optional<std::vector<float>> twcov_pack_o =
                    G->get_attrib_by_name<rt_covariance_velocity_att>(edge);
                twcov_pack = twcov_pack_o.value_or(
                    std::vector<float>(RT_COVARIANCE_BLOCK_SIZE * HISTORY_SIZE, 0.f));
                // A pre-existing FLAT 36-float velocity covariance (the layout producers wrote before
                // this overload existed) is not a short ring — resizing would leave its single block in
                // slot 0 and zeros elsewhere, which reads as "slot 0 is certain, the rest are perfect".
                // Start the ring clean instead.
                if (twcov_pack.size() < RT_COVARIANCE_BLOCK_SIZE * HISTORY_SIZE)
                    twcov_pack.assign(RT_COVARIANCE_BLOCK_SIZE * HISTORY_SIZE, 0.f);
            }
        }

        bool update_index = true;
        int timestamp_index = 0;
        if (head_o.has_value())
            timestamp_index = static_cast<int>(head_o.value_or(0) / BLOCK_SIZE) % HISTORY_SIZE;
        const int index = timestamp_index * BLOCK_SIZE;
        const auto timestamp_v = timestamp.has_value() ? *timestamp : static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());

        if (timestamp_v < time_stamps[prev(timestamp_index, HISTORY_SIZE)])
        {
            std::vector<int64_t> diffs;
            std::transform(time_stamps.begin(), time_stamps.end(), std::back_inserter(diffs),
                           [timestamp_v](auto val) {
                               return ((int64_t)timestamp_v - (int64_t)val > 0)
                                          ? ((int64_t)timestamp_v - (int64_t)val)
                                          : std::numeric_limits<int64_t>::min();
                           });

            const auto pos = static_cast<int>(std::min_element(diffs.begin(), diffs.end()) - diffs.begin()) % HISTORY_SIZE;

            if (pos == timestamp_index && timestamp_v < time_stamps[pos]) return;
            if (pos >= timestamp_index) update_index = false;

            // ★EVERY PACK MOVES TOGETHER OR NONE OF THEM DOES. This branch re-sorts the ring by
            // erasing one slot and re-inserting it at its correct chronological position; a pack left
            // out of that shuffle stays indexed by the OLD slot order while the timestamps use the new
            // one. Nothing detects that: the block is well-formed, it is simply attached to a
            // different instant than the pose beside it, so a consumer confidently extrapolates with
            // the wrong twist. Add any future per-block pack here as well as below.
            time_stamps.erase(time_stamps.begin() + timestamp_index);
            tr_pack.erase(tr_pack.begin() + index, tr_pack.begin() + index + BLOCK_SIZE);
            rot_pack.erase(rot_pack.begin() + index, rot_pack.begin() + index + BLOCK_SIZE);
            if (with_covariance)
            {
                const auto covariance_index = timestamp_index * RT_COVARIANCE_BLOCK_SIZE;
                cov_pack.erase(cov_pack.begin() + covariance_index, cov_pack.begin() + covariance_index + RT_COVARIANCE_BLOCK_SIZE);
            }
            if (with_twist)
            {
                twl_pack.erase(twl_pack.begin() + index, twl_pack.begin() + index + BLOCK_SIZE);
                twa_pack.erase(twa_pack.begin() + index, twa_pack.begin() + index + BLOCK_SIZE);
                if (with_twist_cov)
                {
                    const auto tcov_index = timestamp_index * RT_COVARIANCE_BLOCK_SIZE;
                    twcov_pack.erase(twcov_pack.begin() + tcov_index,
                                     twcov_pack.begin() + tcov_index + RT_COVARIANCE_BLOCK_SIZE);
                }
            }

            const auto insert_index = pos * BLOCK_SIZE;
            tr_pack.insert(tr_pack.begin() + insert_index, trans[0]);
            tr_pack.insert(tr_pack.begin() + insert_index + 1, trans[1]);
            tr_pack.insert(tr_pack.begin() + insert_index + 2, trans[2]);
            rot_pack.insert(rot_pack.begin() + insert_index, rot_euler[0]);
            rot_pack.insert(rot_pack.begin() + insert_index + 1, rot_euler[1]);
            rot_pack.insert(rot_pack.begin() + insert_index + 2, rot_euler[2]);
            if (with_covariance)
                cov_pack.insert(cov_pack.begin() + pos * RT_COVARIANCE_BLOCK_SIZE, covariance->begin(), covariance->end());
            if (with_twist)
            {
                twl_pack.insert(twl_pack.begin() + insert_index,
                                block.twist_linear->begin(), block.twist_linear->end());
                twa_pack.insert(twa_pack.begin() + insert_index,
                                block.twist_angular->begin(), block.twist_angular->end());
                if (with_twist_cov)
                    twcov_pack.insert(twcov_pack.begin() + pos * RT_COVARIANCE_BLOCK_SIZE,
                                      block.twist_covariance->begin(), block.twist_covariance->end());
            }
            time_stamps.insert(time_stamps.begin() + pos, timestamp_v);
        }
        else
        {
            tr_pack[index] = trans[0];
            tr_pack[index + 1] = trans[1];
            tr_pack[index + 2] = trans[2];
            rot_pack[index] = rot_euler[0];
            rot_pack[index + 1] = rot_euler[1];
            rot_pack[index + 2] = rot_euler[2];
            if (with_covariance)
                std::copy(covariance->begin(), covariance->end(), cov_pack.begin() + timestamp_index * RT_COVARIANCE_BLOCK_SIZE);
            if (with_twist)
            {
                std::copy(block.twist_linear->begin(), block.twist_linear->end(), twl_pack.begin() + index);
                std::copy(block.twist_angular->begin(), block.twist_angular->end(), twa_pack.begin() + index);
                if (with_twist_cov)
                    std::copy(block.twist_covariance->begin(), block.twist_covariance->end(),
                              twcov_pack.begin() + timestamp_index * RT_COVARIANCE_BLOCK_SIZE);
            }
            time_stamps[timestamp_index] = timestamp_v;
        }

        G->add_or_modify_attrib_local<rt_rotation_euler_xyz_att>(edge, std::move(rot_pack));
        G->add_or_modify_attrib_local<rt_translation_att>(edge, std::move(tr_pack));
        if (with_covariance)
            G->add_or_modify_attrib_local<rt_covariance_att>(edge, std::move(cov_pack));
        if (with_twist)
        {
            G->add_or_modify_attrib_local<rt_twist_linear_att>(edge, std::move(twl_pack));
            G->add_or_modify_attrib_local<rt_twist_angular_att>(edge, std::move(twa_pack));
            if (with_twist_cov)
                G->add_or_modify_attrib_local<rt_covariance_velocity_att>(edge, std::move(twcov_pack));
        }
        if (update_index)
            G->add_or_modify_attrib_local<rt_head_index_att>(edge, index + BLOCK_SIZE);
        G->add_or_modify_attrib_local<rt_timestamps_att>(edge, std::move(time_stamps));
    }

    auto to_n = G->get_node(to);
    if (!to_n.has_value())
        throw std::runtime_error(
                "Destination node " + std::to_string(to) + " not found in G in insert_or_assign_edge_RT() " +
                __FILE__ + " " + __FUNCTION__ + " " + std::to_string(__LINE__));

    bool node_changed = false;
    if (auto x = G->get_attrib_by_name<parent_att>(*to_n); !x.has_value() || x.value() != n.id())
    {
        G->add_or_modify_attrib_local<parent_att>(*to_n, n.id());
        node_changed = true;
    }

    const auto n_level = G->get_node_level(n);
    if (!n_level.has_value())
        throw std::runtime_error(
                "Source node " + std::to_string(n.id()) + " has no level in insert_or_assign_edge_RT() " +
                __FILE__ + " " + __FUNCTION__ + " " + std::to_string(__LINE__));
    const auto next_level = n_level.value() + 1;
    bool level_changed = false;
    if (auto x = G->get_attrib_by_name<level_att>(*to_n); !x.has_value() || x.value() != next_level)
    {
        G->add_or_modify_attrib_local<level_att>(*to_n, next_level);
        node_changed = true;
        level_changed = true;
    }

    if (node_changed && !G->update_node(*to_n))
    {
        throw std::runtime_error(
                "Could not update destination node " + std::to_string(to) + " in insert_or_assign_edge_RT() " +
                __FILE__ + " " + __FUNCTION__ + " " + std::to_string(__LINE__));
    }

    // Re-parent cascade: if the destination's level shifted, every descendant's level shifted too.
    // Only fires on an actual level change (a structural re-parent), so the per-cycle pose-only RT
    // updates — which leave level unchanged — never pay for this walk.
    if (level_changed)
        walk_and_fix_levels(to, /*repair*/ true, /*report*/ false);

    if (!G->insert_or_assign_edge(std::move(edge)))
        throw std::runtime_error(
                "Could not insert RT edge " + std::to_string(n.id()) + " -> " + std::to_string(to) +
                " in insert_or_assign_edge_RT() " + __FILE__ + " " + __FUNCTION__ + " " + std::to_string(__LINE__));
}

bool RT_API::walk_and_fix_levels(uint64_t start_id, bool repair, bool report)
{
    auto start_n = G->get_node(start_id);
    if (!start_n.has_value())
        return false;
    const auto start_level = G->get_node_level(start_n.value());
    if (!start_level.has_value())
    {
        if (report) qWarning() << "[RT_tree] node" << start_id << "has no level — cannot derive subtree";
        return false;
    }

    bool consistent = true;
    std::unordered_set<uint64_t> visited{start_id};
    std::deque<uint64_t> q{start_id};
    while (!q.empty())
    {
        const uint64_t cur = q.front();
        q.pop_front();
        auto cur_n = G->get_node(cur);
        if (!cur_n.has_value())
            continue;
        const int child_level = G->get_node_level(cur_n.value()).value_or(0) + 1;
        for (const auto &e : G->get_node_edges_by_type(cur_n.value(), "RT"))
        {
            const uint64_t ch = e.to();
            if (visited.count(ch))   // a tree node reached twice ⇒ cycle or a second RT parent
            {
                if (report) qWarning() << "[RT_tree] DEFECT: node" << ch << "reached twice via RT (cycle or multi-parent)";
                consistent = false;
                continue;
            }
            visited.insert(ch);
            auto ch_n = G->get_node(ch);
            if (!ch_n.has_value())
                continue;
            const int      jl = G->get_node_level(ch_n.value()).value_or(-1);
            const uint64_t jp = G->get_attrib_by_name<parent_att>(ch_n.value()).value_or(0);
            if (jl != child_level || jp != cur)
            {
                consistent = false;
                if (report)
                    qWarning() << "[RT_tree]" << (repair ? "FIX" : "DEFECT") << ": node" << ch
                               << QString::fromStdString(ch_n->name())
                               << "level" << jl << "->" << child_level << "parent" << jp << "->" << cur;
                if (repair)
                {
                    G->add_or_modify_attrib_local<level_att>(ch_n.value(), child_level);
                    G->add_or_modify_attrib_local<parent_att>(ch_n.value(), cur);
                    G->update_node(ch_n.value());
                }
            }
            q.push_back(ch);
        }
    }
    return consistent;
}

bool RT_API::check_RT_tree(bool repair)
{
    auto root = G->get_node_root();
    if (!root.has_value())
    {
        qWarning() << "[RT_tree] DEFECT: no 'root' node";
        return false;
    }
    // Root must sit at level 0 with no parent; normalise it so the subtree walk has a valid base.
    if (G->get_node_level(root.value()).value_or(0) != 0 ||
        G->get_attrib_by_name<parent_att>(root.value()).value_or(0) != 0)
    {
        if (repair)
        {
            G->add_or_modify_attrib_local<level_att>(root.value(), 0);
            G->add_or_modify_attrib_local<parent_att>(root.value(), static_cast<uint64_t>(0));
            G->update_node(root.value());
        }
        else
            qWarning() << "[RT_tree] DEFECT: root has non-zero level/parent";
    }

    const bool consistent = walk_and_fix_levels(root->id(), repair, /*report*/ true);

    // Orphan check: any node that declares a parent but was never reached from root is detached.
    std::unordered_set<uint64_t> reachable{root->id()};
    {
        std::deque<uint64_t> q{root->id()};
        while (!q.empty())
        {
            auto cn = G->get_node(q.front()); q.pop_front();
            if (!cn.has_value()) continue;
            for (const auto &e : G->get_node_edges_by_type(cn.value(), "RT"))
                if (reachable.insert(e.to()).second)
                    q.push_back(e.to());
        }
    }
    bool no_orphans = true;
    for (const auto &n : G->get_nodes())
        if (G->get_attrib_by_name<parent_att>(n).value_or(0) != 0 && !reachable.count(n.id()))
        {
            qWarning() << "[RT_tree] DEFECT: node" << n.id() << QString::fromStdString(n.name())
                       << "has a parent but is not reachable from root (detached subtree)";
            no_orphans = false;
        }

    return consistent && no_orphans;
}
