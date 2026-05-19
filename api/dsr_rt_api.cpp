#include <algorithm>
#include <cmath>
#include <dsr/api/dsr_rt_api.h>
#include <dsr/api/dsr_api.h>

using namespace DSR;

namespace
{
    constexpr std::size_t RT_BLOCK_SIZE = 3;
    using TimedBlock = std::pair<std::uint64_t, std::size_t>;

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
        auto rtmat = Mat::RTMat::Identity();
        rtmat.translate(translation);
        rtmat.rotate(rotation);
        return rtmat;
    }

    Mat::RTMat rtmat_at(const std::vector<float> &translation_pack, const std::vector<float> &rotation_pack, std::size_t block_index)
    {
        return make_rtmat(translation_at(translation_pack, block_index), rotation_at(rotation_pack, block_index));
    }

    std::vector<TimedBlock> valid_blocks(const std::vector<std::uint64_t> &timestamps,
                                         const std::vector<float> &translation_pack,
                                         const std::vector<float> *rotation_pack = nullptr)
    {
        std::size_t block_count = std::min(timestamps.size(), translation_pack.size() / RT_BLOCK_SIZE);
        if (rotation_pack != nullptr)
            block_count = std::min(block_count, rotation_pack->size() / RT_BLOCK_SIZE);

        std::vector<TimedBlock> blocks;
        blocks.reserve(block_count);
        for (std::size_t i = 0; i < block_count; ++i)
        {
            if (timestamps[i] == 0)
                continue;
            blocks.emplace_back(timestamps[i], i * RT_BLOCK_SIZE);
        }

        std::sort(blocks.begin(), blocks.end(), [](const TimedBlock &lhs, const TimedBlock &rhs)
        {
            return lhs.first < rhs.first;
        });

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

std::optional<Eigen::Vector3d> RT_API::get_translation(const Node &n, uint64_t to, std::uint64_t timestamp, TimeQuery time_query)
{
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

void RT_API::insert_or_assign_edge_RT(Node &n, uint64_t to, const std::vector<float> &trans, const std::vector<float> &rot_euler, std::optional<uint64_t> timestamp)
{
    bool r1 = false;
    bool r2 = false;
    bool no_send = true;

    std::optional<IDL::MvregEdge> node1_insert;
    std::optional<std::vector<IDL::MvregEdgeAttr>> node1_update;
    std::optional<std::vector<IDL::MvregNodeAttr>> node2;
    std::optional<CRDTNode> to_n;
    {
        std::unique_lock<std::shared_mutex> lock(G->_mutex);
        if (G->nodes.contains(to))
        {
            CRDTEdge e;
            if (HISTORY_SIZE <= 0)
            {
                e.to(to);  e.from(n.id()); e.type("RT"); e.agent_id(G->agent_id);
                CRDTAttribute tr(trans, get_unix_timestamp(), 0);
                CRDTAttribute rot(rot_euler, get_unix_timestamp(), 0);

                auto [it, new_el] = e.attrs().emplace("rt_rotation_euler_xyz", mvreg<CRDTAttribute> ());
                it->second.write(std::move(rot));
                auto [it2, new_el2] = e.attrs().emplace("rt_translation", mvreg<CRDTAttribute> ());
                it2->second.write(std::move(tr));
            } else {

                e = G->get_edge_(n.id(), to, "RT").value_or(CRDTEdge());
                e.to(to);  e.from(n.id()); e.type("RT"); e.agent_id(G->agent_id);
                auto head_o = G->get_attrib_by_name<rt_head_index_att>(e);
                std::optional<std::vector<uint64_t>> tstamps_o = G->get_attrib_by_name<rt_timestamps_att>(e);
                std::optional<std::vector<float>> tr_pack_o = G->get_attrib_by_name<rt_translation_att>(e);
                std::optional<std::vector<float>> rot_pack_o = G->get_attrib_by_name<rt_rotation_euler_xyz_att>(e);
                auto time_stamps = tstamps_o.value_or(std::vector<std::uint64_t>(HISTORY_SIZE, 0));
                auto tr_pack = tr_pack_o.value_or(std::vector<float> (BLOCK_SIZE * HISTORY_SIZE, 0.f));
                auto rot_pack = rot_pack_o.value_or(std::vector<float> (BLOCK_SIZE * HISTORY_SIZE, 0.f));

                if (time_stamps.size() < BLOCK_SIZE * HISTORY_SIZE) time_stamps.resize(HISTORY_SIZE);
                if (tr_pack.size() < BLOCK_SIZE * HISTORY_SIZE) tr_pack.resize(BLOCK_SIZE * HISTORY_SIZE);
                if (rot_pack.size() < BLOCK_SIZE * HISTORY_SIZE) rot_pack.resize(BLOCK_SIZE * HISTORY_SIZE);

                bool update_index = true;
                auto timestamp_index = 0;
                if (!head_o.has_value()) {
                        timestamp_index = 0;
                } else {
                        timestamp_index = (int)(head_o.value_or(0)/BLOCK_SIZE) % HISTORY_SIZE;
                }
                int index = timestamp_index * BLOCK_SIZE;

                auto timestamp_v = (timestamp.has_value()) ? *timestamp : static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());



                if (timestamp_v < time_stamps[prev(timestamp_index, HISTORY_SIZE)]) {
                    std::vector<int64_t> diffs;
                    std::transform(time_stamps.begin(), time_stamps.end(), std::back_inserter(diffs),
                                 [t = *timestamp](auto &val) {
                                    return ((int64_t)t - (int64_t)val > 0) ? ((int64_t)t - (int64_t)val) : std::numeric_limits<int64_t>::min();
                                 });

                    auto pos = (((std::min_element(diffs.begin(), diffs.end())) - diffs.begin()))  % HISTORY_SIZE;

                    // too old to insert it
                    if (pos == timestamp_index && timestamp_v < time_stamps[pos]) {return;}
                    if (pos > timestamp_index) {
                        update_index = false;
                    }

                    time_stamps.erase(time_stamps.begin() + timestamp_index);
                    tr_pack.erase(tr_pack.begin() + index, tr_pack.begin() + index + 3);
                    rot_pack.erase(rot_pack.begin() + index, rot_pack.begin() + index + 3);

                    tr_pack.insert(tr_pack.begin() + pos*BLOCK_SIZE, trans[0]);
                    tr_pack.insert(tr_pack.begin() + pos*BLOCK_SIZE + 1, trans[1]);
                    tr_pack.insert(tr_pack.begin() + pos*BLOCK_SIZE + 2, trans[2]);
                    rot_pack.insert(rot_pack.begin() + pos*BLOCK_SIZE, rot_euler[0]);
                    rot_pack.insert(rot_pack.begin() + pos*BLOCK_SIZE + 1, rot_euler[1]);
                    rot_pack.insert(rot_pack.begin() + pos*BLOCK_SIZE + 2, rot_euler[2]);
                    time_stamps.insert(time_stamps.begin() + pos, *timestamp);

                } else {

                    tr_pack[index] = trans[0];
                    tr_pack[index + 1] = trans[1];
                    tr_pack[index + 2] = trans[2];
                    rot_pack[index] = rot_euler[0];
                    rot_pack[index + 1] = rot_euler[1];
                    rot_pack[index + 2] = rot_euler[2];
                    time_stamps[timestamp_index] = timestamp_v;
                }

                CRDTAttribute tr(std::move(tr_pack), get_unix_timestamp(), 0);
                CRDTAttribute rot(std::move(rot_pack), get_unix_timestamp(), 0);
                CRDTAttribute head_index(index+3, get_unix_timestamp(), 0);
                CRDTAttribute timestamps(std::move(time_stamps), get_unix_timestamp(), 0);

                auto [it, new_el] = e.attrs().insert_or_assign("rt_rotation_euler_xyz", mvreg<CRDTAttribute> ());
                it->second.write(std::move(rot));
                std::tie(it, new_el) = e.attrs().insert_or_assign("rt_translation", mvreg<CRDTAttribute> ());
                it->second.write(std::move(tr));
                if (update_index) {
                    std::tie(it, new_el) = e.attrs().insert_or_assign("rt_head_index", mvreg<CRDTAttribute> ());
                    it->second.write(std::move(head_index));
                }
                std::tie(it, new_el) = e.attrs().insert_or_assign("rt_timestamps", mvreg<CRDTAttribute> ());
                it->second.write(std::move(timestamps));
            }

            to_n = G->get_(to).value();
            if (auto x = G->get_crdt_attrib_by_name<parent_att>(to_n.value()); x.has_value())
            {
                if ( x.value() != n.id())
                {
                    no_send = !G->modify_attrib_local<parent_att>(to_n.value(), n.id());
                }
            }
            else
            {
                no_send = !G->add_attrib_local<parent_att>(to_n.value(), n.id());
            }

            if (auto x = G->get_crdt_attrib_by_name<level_att>(to_n.value()); x.has_value())
            {
                if (x.value() != G->get_node_level(n).value() + 1)
                {
                    no_send = !G->modify_attrib_local<level_att>(to_n.value(),  G->get_node_level(n).value() + 1 );
                }
            }
            else
            {
                no_send = !G->add_attrib_local<level_att>(to_n.value(),  G->get_node_level(n).value() + 1 );
            }

            //Check if RT edge exist.
            if (!n.fano().contains({to, "RT"}))
            {
                //Create -> from: IDL::MvregEdge, to: vector<IDL::MvregNodeAttr>
                std::tie(r1, node1_insert, std::ignore) = G->insert_or_assign_edge_(std::move(e), n.id(), to);
                if (!no_send) std::tie(r2, node2) = G->update_node_(std::move(to_n.value()));

            }
            else
            {
                //Update -> from: IDL::MvregEdgeAttr, to: vector<IDL::MvregNodeAttr>
                std::tie(r1, std::ignore, node1_update) = G->insert_or_assign_edge_(std::move(e), n.id(), to);
                if (!no_send) std::tie(r2, node2) = G->update_node_(std::move(to_n.value()));

            }
            if (!r1)
            {
                throw std::runtime_error(
                        "Could not insert Node " + std::to_string(n.id()) + " in G in insert_or_assign_edge_RT() " +
                        __FILE__ + " " + __FUNCTION__ + " " + std::to_string(__LINE__));
            }
            if (!r2 and !no_send)
            {
                throw std::runtime_error(
                        "Could not insert Node " + std::to_string(to_n->id()) + " in G in insert_or_assign_edge_RT() " +
                        __FILE__ + " " + __FUNCTION__ + " " + std::to_string(__LINE__));
            }
        } else
            throw std::runtime_error(
                    "Destination node " + std::to_string(to) + " not found in G in insert_or_assign_edge_RT() " +
                    __FILE__ + " " + __FUNCTION__ + " " + std::to_string(__LINE__));
    }
    if (!G->copy)
    {
        if (node1_insert.has_value())
        {
            G->dsrpub_edge.write(&node1_insert.value());

        }
        if (node1_update.has_value()) G->dsrpub_edge_attrs.write(&node1_update.value());

        if (!no_send and node2.has_value()) G->dsrpub_node_attrs.write(&node2.value());

        G->emitter.update_edge_attr_signal(n.id(), to, "RT" ,{"rt_rotation_euler_xyz", "rt_translation"}, SignalInfo{ G->agent_id });
        G->emitter.update_edge_signal(n.id(), to, "RT", SignalInfo{ G->agent_id });
        if (!no_send)
        {
            G->emitter.update_node_signal(to_n->id(), to_n->type(), SignalInfo{ G->agent_id });
            G->emitter.update_node_attr_signal(to_n->id(), {"level", "parent"}, SignalInfo{ G->agent_id });
        }
    }
}

void RT_API::insert_or_assign_edge_RT(Node &n, uint64_t to, std::vector<float> &&trans, std::vector<float> &&rot_euler, std::optional<uint64_t> timestamp)
{
    bool r1 = false;
    bool r2 = false;
    bool no_send = true;

    std::optional<IDL::MvregEdge> node1_insert;
    std::optional<std::vector<IDL::MvregEdgeAttr>> node1_update;
    std::optional<std::vector<IDL::MvregNodeAttr>> node2;
    std::optional<CRDTNode> to_n;
    {
        std::unique_lock<std::shared_mutex> lock(G->_mutex);
        if (G->nodes.contains(to))
        {
            CRDTEdge e;
            if (HISTORY_SIZE <= 0)
            {
                e.to(to);  e.from(n.id()); e.type("RT"); e.agent_id(G->agent_id);
                CRDTAttribute tr(std::move(trans), get_unix_timestamp(), 0);
                CRDTAttribute rot(std::move(rot_euler), get_unix_timestamp(), 0);

                auto [it, new_el] = e.attrs().emplace("rt_rotation_euler_xyz", mvreg<CRDTAttribute> ());
                it->second.write(std::move(rot));
                auto [it2, new_el2] = e.attrs().emplace("rt_translation", mvreg<CRDTAttribute> ());
                it2->second.write(std::move(tr));
            } else {

                e = G->get_edge_(n.id(), to, "RT").value_or(CRDTEdge());
                e.to(to);  e.from(n.id()); e.type("RT"); e.agent_id(G->agent_id);
                auto head_o = G->get_attrib_by_name<rt_head_index_att>(e);
                std::optional<std::vector<uint64_t>> tstamps_o = G->get_attrib_by_name<rt_timestamps_att>(e);
                std::optional<std::vector<float>> tr_pack_o = G->get_attrib_by_name<rt_translation_att>(e);
                std::optional<std::vector<float>> rot_pack_o = G->get_attrib_by_name<rt_rotation_euler_xyz_att>(e);
                auto time_stamps = tstamps_o.value_or(std::vector<std::uint64_t>(HISTORY_SIZE, 0));
                auto tr_pack = tr_pack_o.value_or(std::vector<float> (BLOCK_SIZE * HISTORY_SIZE, 0.f));
                auto rot_pack = rot_pack_o.value_or(std::vector<float> (BLOCK_SIZE * HISTORY_SIZE, 0.f));

                if (time_stamps.size() < BLOCK_SIZE * HISTORY_SIZE) time_stamps.resize(HISTORY_SIZE);
                if (tr_pack.size() < BLOCK_SIZE * HISTORY_SIZE) tr_pack.resize(BLOCK_SIZE * HISTORY_SIZE);
                if (rot_pack.size() < BLOCK_SIZE * HISTORY_SIZE) rot_pack.resize(BLOCK_SIZE * HISTORY_SIZE);

                bool update_index = true;
                auto timestamp_index = 0;
                if (!head_o.has_value()) {
                        timestamp_index = 0;
                } else {
                        timestamp_index = (int)(head_o.value_or(0)/BLOCK_SIZE) % HISTORY_SIZE;
                }
                int index = timestamp_index * BLOCK_SIZE;
                auto timestamp_v = (timestamp.has_value()) ? *timestamp : static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());


                if (timestamp_v < time_stamps[prev(timestamp_index, HISTORY_SIZE)]) {
                    std::vector<int64_t> diffs;
                    std::transform(time_stamps.begin(), time_stamps.end(), std::back_inserter(diffs),
                                 [t = *timestamp](auto &val) {
                                    return ((int64_t)t - (int64_t)val > 0) ? ((int64_t)t - (int64_t)val) : std::numeric_limits<int64_t>::min();
                                 });

                    auto pos = (((std::min_element(diffs.begin(), diffs.end())) - diffs.begin()))  % HISTORY_SIZE;
                    
                    // too old to insert it
                    if (pos == timestamp_index && timestamp_v < time_stamps[pos]) {return;}
                    if (pos >= timestamp_index) {
                        update_index = false;
                    }

                    time_stamps.erase(time_stamps.begin() + timestamp_index);
                    tr_pack.erase(tr_pack.begin() + index, tr_pack.begin() + index + 3);
                    rot_pack.erase(rot_pack.begin() + index, rot_pack.begin() + index + 3);

                    tr_pack.insert(tr_pack.begin() + pos*BLOCK_SIZE, trans[0]);
                    tr_pack.insert(tr_pack.begin() + pos*BLOCK_SIZE + 1, trans[1]);
                    tr_pack.insert(tr_pack.begin() + pos*BLOCK_SIZE + 2, trans[2]);
                    rot_pack.insert(rot_pack.begin() + pos*BLOCK_SIZE, rot_euler[0]);
                    rot_pack.insert(rot_pack.begin() + pos*BLOCK_SIZE + 1, rot_euler[1]);
                    rot_pack.insert(rot_pack.begin() + pos*BLOCK_SIZE + 2, rot_euler[2]);
                    time_stamps.insert(time_stamps.begin() + pos, *timestamp);

                } else {

                    tr_pack[index] = trans[0];
                    tr_pack[index + 1] = trans[1];
                    tr_pack[index + 2] = trans[2];
                    rot_pack[index] = rot_euler[0];
                    rot_pack[index + 1] = rot_euler[1];
                    rot_pack[index + 2] = rot_euler[2];
                    time_stamps[timestamp_index] = timestamp_v;
                }


                CRDTAttribute tr(std::move(tr_pack), get_unix_timestamp(), 0);
                CRDTAttribute rot(std::move(rot_pack), get_unix_timestamp(), 0);
                CRDTAttribute head_index(index+3, get_unix_timestamp(), 0);
                CRDTAttribute timestamps(std::move(time_stamps), get_unix_timestamp(), 0);

                auto [it, new_el] = e.attrs().insert_or_assign("rt_rotation_euler_xyz", mvreg<CRDTAttribute> ());
                it->second.write(std::move(rot));
                std::tie(it, new_el) = e.attrs().insert_or_assign("rt_translation", mvreg<CRDTAttribute> ());
                it->second.write(std::move(tr));
                if (update_index) {
                    std::tie(it, new_el) = e.attrs().insert_or_assign("rt_head_index", mvreg<CRDTAttribute> ());
                    it->second.write(std::move(head_index));
                }
                std::tie(it, new_el) = e.attrs().insert_or_assign("rt_timestamps", mvreg<CRDTAttribute> ());
                it->second.write(std::move(timestamps));
            }

            to_n = G->get_(to).value();
            if (auto x = G->get_crdt_attrib_by_name<parent_att>(to_n.value()); x.has_value())
            {
                if (x.value() != n.id())
                {
                    no_send = !G->modify_attrib_local<parent_att>(to_n.value(), n.id());
                }
            }
            else
            {
                no_send = !G->add_attrib_local<parent_att>(to_n.value(), n.id());
            }

            if (auto x = G->get_crdt_attrib_by_name<level_att>(to_n.value()); x.has_value())
            {
                if (x.value() != G->get_node_level(n).value() + 1)
                {
                    no_send = !G->modify_attrib_local<level_att>(to_n.value(),  G->get_node_level(n).value() + 1 );
                }
            }
            else
            {
                no_send = !G->add_attrib_local<level_att>(to_n.value(),  G->get_node_level(n).value() + 1 );
            }

            //Check if RT edge exist.
            if (!n.fano().contains({to, "RT"}))
            {
                //Create -> from: IDL::MvregEdge, to: vector<IDL::MvregNodeAttr>
                std::tie(r1, node1_insert, std::ignore) = G->insert_or_assign_edge_(std::move(e), n.id(), to);
                if (!no_send) std::tie(r2, node2) = G->update_node_(std::move(to_n.value()));

            }
            else
            {
                //Update -> from: IDL::MvregEdgeAttr, to: vector<IDL::MvregNodeAttr>
                std::tie(r1, std::ignore, node1_update) = G->insert_or_assign_edge_(std::move(e), n.id(), to);
                if (!no_send) std::tie(r2, node2) = G->update_node_(std::move(to_n.value()));

            }
            if (!r1)
            {
                throw std::runtime_error(
                        "Could not insert Node " + std::to_string(n.id()) + " in G in insert_or_assign_edge_RT() " +
                        __FILE__ + " " + __FUNCTION__ + " " + std::to_string(__LINE__));
            }
            if (!r2 and !no_send)
            {
                throw std::runtime_error(
                        "Could not insert Node " + std::to_string(to_n->id()) + " in G in insert_or_assign_edge_RT() " +
                        __FILE__ + " " + __FUNCTION__ + " " + std::to_string(__LINE__));
            }
        } else
            throw std::runtime_error(
                    "Destination node " + std::to_string(to) + " not found in G in insert_or_assign_edge_RT() " +
                    __FILE__ + " " + __FUNCTION__ + " " + std::to_string(__LINE__));
    }
    if (!G->copy)
    {
        if (node1_insert.has_value())
        {
            G->dsrpub_edge.write(&node1_insert.value());
        }
        if (node1_update.has_value()) G->dsrpub_edge_attrs.write(&node1_update.value());

        if (!no_send and node2.has_value()) G->dsrpub_node_attrs.write(&node2.value());

        G->emitter.update_edge_attr_signal(n.id(), to, "RT",{"rt_rotation_euler_xyz", "rt_translation"}, SignalInfo{ G->agent_id });
        G->emitter.update_edge_signal(n.id(), to, "RT", SignalInfo{ G->agent_id });
        if (!no_send)
        {
            G->emitter.update_node_signal(to_n->id(), to_n->type(), SignalInfo{ G->agent_id });
            G->emitter.update_node_attr_signal(to_n->id(), {"level", "parent"}, SignalInfo{ G->agent_id });
        }
    }
}
