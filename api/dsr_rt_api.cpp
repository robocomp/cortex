#include <algorithm>
#include <cmath>
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

std::optional<Mat::RTMat>  RT_API::get_edge_RT_as_rtmat(const Edge &edge, std::uint64_t timestamp, TimeQuery time_query)
{
    CORTEX_PROFILE_ZONE_N("RT_API::get_edge_RT_as_rtmat");
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
                    if (time_query == TimeQuery::Interpolated)
                    {
                        const auto [lower, upper] = bracketing_blocks(blocks, timestamp);
                        if (lower.second == upper.second)
                            return rtmat_at(t, r, lower.second);

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

std::optional<Eigen::Matrix<double, 6, 6>> RT_API::get_edge_RT_covariance(const Edge &edge, std::uint64_t timestamp, TimeQuery time_query)
{
    auto covariance_o = G->get_attrib_by_name<rt_covariance_att>(edge);
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
            if (time_query == TimeQuery::Interpolated)
            {
                const auto [lower, upper] = bracketing_blocks(blocks, timestamp);
                const auto lower_index = lower.second * RT_COVARIANCE_BLOCK_SIZE;
                const auto upper_index = upper.second * RT_COVARIANCE_BLOCK_SIZE;
                if (lower.second == upper.second)
                    return covariance_at(covariance_pack, lower_index);

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

std::optional<Eigen::Matrix<double, 6, 6>> RT_API::get_covariance_matrix(const Node &n, uint64_t to, std::uint64_t timestamp, TimeQuery time_query)
{
    if (auto edge = get_edge_RT(n, to); edge.has_value())
        return get_edge_RT_covariance(edge.value(), timestamp, time_query);
    return {};
}

std::optional<Eigen::Matrix<double, 6, 6>> RT_API::get_covariance_matrix(uint64_t node_id, uint64_t to, std::uint64_t timestamp, TimeQuery time_query)
{
    if (const auto node = G->get_node(node_id); node.has_value())
        return get_covariance_matrix(node.value(), to, timestamp, time_query);
    return {};
}

void RT_API::insert_or_assign_edge_RT(Node &n, uint64_t to, const std::vector<float> &trans, const std::vector<float> &rot_euler, std::optional<uint64_t> timestamp)
{
    CORTEX_PROFILE_ZONE_N("RT_API::insert_or_assign_edge_RT(copy)");
    insert_or_assign_edge_RT_impl(n, to, trans, rot_euler, std::nullopt, timestamp);
}

void RT_API::insert_or_assign_edge_RT(Node &n, uint64_t to, std::vector<float> &&trans, std::vector<float> &&rot_euler, std::optional<uint64_t> timestamp)
{
    CORTEX_PROFILE_ZONE_N("RT_API::insert_or_assign_edge_RT(move)");
    insert_or_assign_edge_RT_impl(n, to, std::move(trans), std::move(rot_euler), std::nullopt, timestamp);
}

void RT_API::insert_or_assign_edge_RT(Node &n, uint64_t to, const std::vector<float> &trans, const std::vector<float> &rot_euler,
                                      const std::vector<float> &covariance, std::optional<uint64_t> timestamp)
{
    insert_or_assign_edge_RT_impl(n, to, trans, rot_euler, covariance, timestamp);
}

void RT_API::insert_or_assign_edge_RT(Node &n, uint64_t to, std::vector<float> &&trans, std::vector<float> &&rot_euler,
                                      std::vector<float> &&covariance, std::optional<uint64_t> timestamp)
{
    insert_or_assign_edge_RT_impl(n, to, std::move(trans), std::move(rot_euler), std::move(covariance), timestamp);
}

void RT_API::insert_or_assign_edge_RT_impl(Node &n, uint64_t to, std::vector<float> trans, std::vector<float> rot_euler,
                                           std::optional<std::vector<float>> covariance, std::optional<uint64_t> timestamp)
{
    if (covariance.has_value() && covariance->size() != RT_COVARIANCE_BLOCK_SIZE)
        throw std::runtime_error("RT covariance must contain exactly 36 float values");

    const bool with_covariance = covariance.has_value();

    auto edge = G->get_edge(n.id(), to, "RT").value_or(Edge::create<RT_edge_type>(n.id(), to));
    if (HISTORY_SIZE <= 0)
    {
        G->add_or_modify_attrib_local<rt_translation_att>(edge, std::move(trans));
        G->add_or_modify_attrib_local<rt_rotation_euler_xyz_att>(edge, std::move(rot_euler));
        if (with_covariance)
            G->add_or_modify_attrib_local<rt_covariance_att>(edge, std::move(covariance.value()));
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

            time_stamps.erase(time_stamps.begin() + timestamp_index);
            tr_pack.erase(tr_pack.begin() + index, tr_pack.begin() + index + BLOCK_SIZE);
            rot_pack.erase(rot_pack.begin() + index, rot_pack.begin() + index + BLOCK_SIZE);
            if (with_covariance)
            {
                const auto covariance_index = timestamp_index * RT_COVARIANCE_BLOCK_SIZE;
                cov_pack.erase(cov_pack.begin() + covariance_index, cov_pack.begin() + covariance_index + RT_COVARIANCE_BLOCK_SIZE);
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
            time_stamps[timestamp_index] = timestamp_v;
        }

        G->add_or_modify_attrib_local<rt_rotation_euler_xyz_att>(edge, std::move(rot_pack));
        G->add_or_modify_attrib_local<rt_translation_att>(edge, std::move(tr_pack));
        if (with_covariance)
            G->add_or_modify_attrib_local<rt_covariance_att>(edge, std::move(cov_pack));
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
    if (auto x = G->get_attrib_by_name<level_att>(*to_n); !x.has_value() || x.value() != next_level)
    {
        G->add_or_modify_attrib_local<level_att>(*to_n, next_level);
        node_changed = true;
    }

    if (node_changed && !G->update_node(*to_n))
    {
        throw std::runtime_error(
                "Could not update destination node " + std::to_string(to) + " in insert_or_assign_edge_RT() " +
                __FILE__ + " " + __FUNCTION__ + " " + std::to_string(__LINE__));
    }

    if (!G->insert_or_assign_edge(std::move(edge)))
        throw std::runtime_error(
                "Could not insert RT edge " + std::to_string(n.id()) + " -> " + std::to_string(to) +
                " in insert_or_assign_edge_RT() " + __FILE__ + " " + __FUNCTION__ + " " + std::to_string(__LINE__));
}
