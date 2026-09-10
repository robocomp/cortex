#include <dsr/api/dsr_inner_eigen_api.h>
#include <dsr/api/dsr_api.h>

using namespace DSR;

//InnerAPI::InnerAPI(std::shared_ptr<DSR::DSRGraph> _G)
InnerEigenAPI::InnerEigenAPI(DSR::DSRGraph *G_)
{
    G = G_;
    rt = G->get_rt_api();
    //update signals
    connect(G, &DSR::DSRGraph::update_edge_signal, this, &InnerEigenAPI::add_or_assign_edge_slot, Qt::QueuedConnection);
    connect(G, &DSR::DSRGraph::update_edge_attr_signal, this, &InnerEigenAPI::add_or_assign_edge_attr_slot, Qt::QueuedConnection);
    connect(G, &DSR::DSRGraph::del_edge_signal, this, &InnerEigenAPI::del_edge_slot, Qt::QueuedConnection);
    connect(G, &DSR::DSRGraph::del_node_signal, this, &InnerEigenAPI::del_node_slot, Qt::QueuedConnection);
}

////////////////////////////////////////////////////////////////////////////////////////
////// TRANSFORMATION MATRIX
////////////////////////////////////////////////////////////////////////////////////////

std::optional<Mat::RTMat> InnerEigenAPI::get_transformation_matrix(const std::string &dest, const std::string &orig, std::uint64_t timestamp, const std::string &edge_type, RT_API::TimeQuery time_query,
                                                                  RT_API::TimeQueryInfo *info)
{
    if( not DSR::DSRGraph::is_valid_edge_type(edge_type)) 
        return {};
    if (info != nullptr) *info = RT_API::TimeQueryInfo{};
    RT_API::TimeQueryInfo edge_info;
    // Severity order, worst last: a chain is as trustworthy as its least trustworthy edge.
    // ★STALE RANKS LOW, AND THAT IS THE WHOLE POINT OF THIS ORDER. It was highest, which made every
    // chain on a tree containing ONE bootstrap-written mount report Stale for ever — measured
    // 632/632 rows on the controller's room<-robot query, a field that could not vary and therefore
    // said nothing. Stale means the edge is not participating in time at all, which for a static
    // mount is correct behaviour, not a defect; Clamped is the serious one, because it means a LIVE
    // ring failed to bracket the query and the pose came from a nearby but wrong instant with nothing
    // to correct it. The ambiguous half of Stale — an edge whose producer DIED — is not lost by this:
    // it is counted in stale_edges, which is what to watch for a change in.
    const auto severity = [](RT_API::TimeQueryInfo::Outcome o)
    {
        switch (o)
        {
            case RT_API::TimeQueryInfo::Outcome::Exact:        return 0;
            case RT_API::TimeQueryInfo::Outcome::Stale:        return 1;
            case RT_API::TimeQueryInfo::Outcome::Interpolated: return 2;
            case RT_API::TimeQueryInfo::Outcome::Extrapolated: return 3;
            case RT_API::TimeQueryInfo::Outcome::Clamped:      return 4;
        }
        return 0;
    };
    const auto note = [&](const RT_API::TimeQueryInfo &e)
    {
        if (info == nullptr) return;
        if (e.outcome == RT_API::TimeQueryInfo::Outcome::Stale) info->stale_edges += 1;
        if (severity(e.outcome) > severity(info->outcome)) info->outcome = e.outcome;
        if (std::llabs(e.applied_dt_ms) > std::llabs(info->applied_dt_ms)) info->applied_dt_ms = e.applied_dt_ms;
        if (std::llabs(e.gap_ms)        > std::llabs(info->gap_ms))        info->gap_ms        = e.gap_ms;
        if (e.ring_span_ms              > info->ring_span_ms)              info->ring_span_ms  = e.ring_span_ms;
    };
    const bool use_cache = (timestamp == 0);
    KeyTransform key = std::make_tuple(dest, orig, edge_type);
    if(use_cache)
    {
        // Section 1 of 2: the lookup. Copy the value OUT under the lock — returning it->second would
        // hand back a reference into a map another thread may erase from a moment later.
        std::scoped_lock lk(cache_mutex);
        if( auto it = cache.find(key) ; it != cache.end())
            return it->second;
    }
    // The tree walk below runs with NO lock held: it calls into DSRGraph, which takes its own
    // shared_mutex, and nesting this one inside that would fix a cache->graph lock order that any
    // graph->cache path would deadlock against. The node ids touched are accumulated here and
    // committed in one short section at the end.
    std::vector<uint64_t> touched;
    {
        Mat::RTMat atotal(Mat::RTMat::Identity());
        Mat::RTMat btotal(Mat::RTMat::Identity());

        auto an = G->get_node(orig);
        auto bn = G->get_node(dest);
        if ( not an.has_value() or  not bn.has_value())
        {
            qWarning() << __FUNCTION__ << ":"<<__LINE__<< " origen or dest nodes do not exist: " << QString::fromStdString(orig) << QString::fromStdString(dest);
            return {};
        }
        auto a = an.value(); auto b = bn.value();
        int minLevel = std::min(G->get_node_level(a).value_or(-1), G->get_node_level(b).value_or(-1));
        if (minLevel == -1)
        {
            qWarning() << __FUNCTION__ << ":"<<__LINE__ << " Incorrect level in one origin or dest nodes: " << QString::fromStdString(orig) << QString::fromStdString(dest) << ". The nodes may not have RT edges";
            return {};
        }
        while (G->get_node_level(a).value_or(-1) >= minLevel)
        {
            //qDebug() << "listaA" << a.id() << G->get_node_level(a).value() << G->get_parent_id(a).value();
            auto p_node = G->get_parent_node(a);
            if( not p_node.has_value())
                break;
            auto edge_rt = rt->get_edge_RT(p_node.value(), a.id(), edge_type);
            if (not edge_rt.has_value())
            {
                qWarning() << __FUNCTION__ << ":"<<__LINE__<< " Cannot find " << QString::fromStdString(edge_type) << " edge between Parent (" << QString::fromStdString(p_node->name()) << ", " << p_node->id() <<") and son (" << QString::fromStdString(a.name()) << ", " << a.id() <<") nodes going from: " << QString::fromStdString(orig) << " to: " << QString::fromStdString(dest);
                return {};
            }
            if( auto rtmat = rt->get_edge_RT_as_rtmat(edge_rt.value(), timestamp, time_query, &edge_info); rtmat.has_value())
            {
                note(edge_info);
                atotal = rtmat.value() * atotal;
                if(use_cache)
                    touched.push_back(p_node.value().id());   // committed under the lock at the end
                a = p_node.value();
            }
            else return {};
        }
        while (G->get_node_level(b).value_or(-1) >= minLevel)
        {
            //qDebug() << "listaB" << b.id() << G->get_node_level(b).value() << G->get_parent_id(b).value();
            auto p_node = G->get_parent_node(b);
            if(not p_node.has_value())
                break;
            auto edge_rt = rt->get_edge_RT(p_node.value(), b.id(), edge_type);
            if (not edge_rt.has_value())
            {
                qWarning() << __FUNCTION__ << ":"<<__LINE__ << " Cannot find " << QString::fromStdString(edge_type) << " edge between Parent (" << QString::fromStdString(p_node->name()) << ", " << p_node->id() <<") and son (" << QString::fromStdString(a.name()) << ", " << a.id() <<") nodes going from: " << QString::fromStdString(orig) << " to: " << QString::fromStdString(dest);
                return {};
            }
            if( auto rtmat = rt->get_edge_RT_as_rtmat(edge_rt.value(), timestamp, time_query, &edge_info); rtmat.has_value())
            {
                note(edge_info);
                btotal = rtmat.value() * btotal;
                if(use_cache)
                    touched.push_back(p_node.value().id());   // committed under the lock at the end
                b = p_node.value();
            }
            else
                return {};
        }
        // from min_level up tp the common ancestor
        while (a.id() != b.id())
        {
            auto p_node = G->get_node(G->get_parent_id(a).value_or(-1));
            auto q_node = G->get_node(G->get_parent_id(b).value_or(-1));
            if(p_node.has_value() and q_node.has_value())
            {
                //qDebug() << "listas A&B" << p_node.value().id() << q_node.value().id();
                auto a_edge_rt = rt->get_edge_RT(p_node.value(), a.id(), edge_type);
                if (not a_edge_rt.has_value())
                {
                    qWarning() << __FUNCTION__ << ":"<<__LINE__ << " Cannot find " << QString::fromStdString(edge_type) << " edge between Parent (" << QString::fromStdString(p_node->name()) << ", " << p_node->id() <<") and son (" << QString::fromStdString(a.name()) << ", " << a.id() <<") nodes going from: " << QString::fromStdString(orig) << " to: " << QString::fromStdString(dest);
                    return {};
                }
                auto b_edge_rt = rt->get_edge_RT(q_node.value(), b.id(), edge_type);
                if (not b_edge_rt.has_value())
                {
                    qWarning() << __FUNCTION__ << ":"<<__LINE__ << " Cannot find " << QString::fromStdString(edge_type) << " edge between Parent (" << QString::fromStdString(p_node->name()) << ", " << p_node->id() <<") and son (" << QString::fromStdString(a.name()) << ", " << a.id() <<") nodes going from: " << QString::fromStdString(orig) << " to: " << QString::fromStdString(dest);
                    return {};
                }
                RT_API::TimeQueryInfo a_info, b_info;
                auto a_rtmat = rt->get_edge_RT_as_rtmat(a_edge_rt.value(), timestamp, time_query, &a_info);
                auto b_rtmat = rt->get_edge_RT_as_rtmat(b_edge_rt.value(), timestamp, time_query, &b_info);
                note(a_info); note(b_info);
                if(a_rtmat.has_value() and b_rtmat.has_value())
                {
                    atotal = a_rtmat.value() * atotal;
                    btotal = b_rtmat.value() * btotal;
                    if(use_cache)
                    {
                        touched.push_back(p_node.value().id());
                        touched.push_back(q_node.value().id());
                    }
                    a = p_node.value();
                    b = q_node.value();
                }
                else return {};
            }
            else
            {
                qWarning() << __FUNCTION__ << ":"<<__LINE__<< " non existing nodes while merging to common ancestor" << QString::fromStdString(a.name()) << QString::fromStdString(b.name());
                return {};
            }
        }
        // update node cache reference
        if(use_cache)
        {
            touched.push_back(bn.value().id());
            touched.push_back(an.value().id());
        }

        auto ret = btotal.inverse() * atotal;
        if(use_cache)
        {
            // Section 2 of 2: commit. Another thread may have inserted the same key meanwhile — same
            // inputs, same value, so last write wins and the duplicated work is the accepted cost.
            std::scoped_lock lk(cache_mutex);
            for(const auto id : touched)
                node_map[id].push_back(key);
            cache[key] = ret;
        }
        return ret;
    }
}

std::optional<Mat::Rot3D> InnerEigenAPI::get_rotation_matrix(const std::string &dest, const std::string &orig, std::uint64_t timestamp, const std::string &edge_type, RT_API::TimeQuery time_query)
{
    if( not DSR::DSRGraph::is_valid_edge_type(edge_type)) 
        return {};
    if( auto r = get_transformation_matrix(dest, orig, timestamp, edge_type, time_query); r.has_value())
        return r.value().rotation();
    else
    {
        qWarning() << __FUNCTION__ << " Not able to compute transformation matrix between " << QString::fromStdString(orig) << " and " << QString::fromStdString(dest);
        return {};
    }
}

std::optional<Mat::Vector3d> InnerEigenAPI::get_translation_vector(const std::string &dest, const std::string &orig, std::uint64_t timestamp, const std::string &edge_type, RT_API::TimeQuery time_query)
{
    if( not DSR::DSRGraph::is_valid_edge_type(edge_type)) 
        return {};
    if (auto r = get_transformation_matrix(dest, orig, timestamp, edge_type, time_query); r.has_value())
        return r.value().translation();
    else
    {
        qWarning() << __FUNCTION__ << " Not able to compute transformation matrix between "
                   << QString::fromStdString(orig) << " and " << QString::fromStdString(dest);
        return {};
    }
}
std::optional<Mat::Vector3d> InnerEigenAPI::get_euler_xyz_angles(const std::string &dest, const std::string &orig, std::uint64_t timestamp, const std::string &edge_type, RT_API::TimeQuery time_query)
{
    if( not DSR::DSRGraph::is_valid_edge_type(edge_type)) 
        return {};
    if( auto r = get_transformation_matrix(dest, orig, timestamp, edge_type, time_query); r.has_value())
        return r.value().rotation().eulerAngles(0,1,2);  //X Y Z order
    else
    {
        qWarning() << __FUNCTION__ << " Not able to compute transformation matrix between " << QString::fromStdString(orig) << " and " << QString::fromStdString(dest);
        return {};
    }
}

////////////////////////////////////////////////////////////////////////////////////////
////// TRANSFORM
////////////////////////////////////////////////////////////////////////////////////////
std::optional<Mat::Vector3d> InnerEigenAPI::transform(const std::string &dest, const Mat::Vector3d &vector, const std::string &orig, std::uint64_t timestamp, const std::string &edge_type, RT_API::TimeQuery time_query)
{
    if( not DSR::DSRGraph::is_valid_edge_type(edge_type)) 
        return {};
    //std::cout <<__FUNCTION__ << " " << initVec << std::endl;
    auto tm = get_transformation_matrix(dest, orig, timestamp, edge_type, time_query);
    //std::cout << __FUNCTION__ << " " << tm.value().matrix().format(CleanFmt) << std::endl;
    if(tm.has_value())
    {
        //qInfo() << __FUNCTION__ << " " << tm.value().matrix().size() << initVec.homogeneous().size();
        return (tm.value() * vector.homogeneous());
    }
    else
        return {};
}

std::optional<Mat::Vector3d> InnerEigenAPI::transform( const std::string &dest, const std::string & orig, std::uint64_t timestamp, const std::string &edge_type, RT_API::TimeQuery time_query)
 {
    if( not DSR::DSRGraph::is_valid_edge_type(edge_type)) 
        return {};
	return transform(dest, Mat::Vector3d(0.,0.,0.), orig, timestamp, edge_type, time_query);
 }

std::optional<Mat::Vector6d> InnerEigenAPI::transform_axis(const std::string &dest, const Mat::Vector6d &vector, const std::string &orig, std::uint64_t timestamp, const std::string &edge_type, RT_API::TimeQuery time_query)
{
    if( not DSR::DSRGraph::is_valid_edge_type(edge_type)) 
        return {};
    auto tm = get_transformation_matrix(dest, orig, timestamp, edge_type, time_query);
    if(tm.has_value())
    {
        const Mat::RTMat rtmat = tm.value();
        const Mat::Vector3d a = rtmat * (vector.head(3).homogeneous());
        const Mat::Rot3D r_axis(Eigen::AngleAxisd(vector(3), Mat::Vector3d::UnitX()) *
                           Eigen::AngleAxisd(vector(4), Mat::Vector3d::UnitY()) *
                           Eigen::AngleAxisd(vector(5), Mat::Vector3d::UnitZ()));
        // multiply orig-dest rotation by rotation matrix of oriented point
        const Mat::Vector3d b = ( rtmat.rotation() * r_axis).eulerAngles(0,1,2);
        Mat::Vector6d ret;
        ret << a(0), a(1), a(2), b(0), b(1), b(2);
        return ret;
    }
    else
        return {};
 }

std::optional<Mat::Vector6d> InnerEigenAPI::transform_axis( const std::string &dest,  const std::string & orig, std::uint64_t timestamp, const std::string &edge_type, RT_API::TimeQuery time_query)
{
    if( not DSR::DSRGraph::is_valid_edge_type(edge_type)) 
        return {};
    Mat::Vector6d v;
	return transform_axis(dest, Mat::Vector6d::Zero(), orig, timestamp, edge_type, time_query);
}

////////////////////////////////////////////////////////////////////////
/// SLOTS ==> used to remove cached transforms when node/edge changes
///////////////////////////////////////////////////////////////////////
void InnerEigenAPI::add_or_assign_edge_slot(uint64_t from, uint64_t to, const std::string& edge_type)
{
    if(edge_type == "RT")
    {
        remove_cache_entry(from);
        remove_cache_entry(to);
    }
}
void InnerEigenAPI::add_or_assign_edge_attr_slot(uint64_t from,
                                                 uint64_t to,
                                                 const std::string& edge_type,
                                                 const std::vector<std::string>& /*att_names*/)
{
    add_or_assign_edge_slot(from, to, edge_type);
}
void InnerEigenAPI::del_node_slot(uint64_t id)
{
    remove_cache_entry(id);
}
void InnerEigenAPI::del_edge_slot(uint64_t from, uint64_t to, const std::string &edge_type)
{
    if(edge_type == "RT")
    {
        remove_cache_entry(from);
        remove_cache_entry(to);
    }
}
void InnerEigenAPI::remove_cache_entry(uint64_t id)
{
    // The erase side of the same pair. Without this lock the loop below iterates node_map[id] while a
    // reader may push_back to that very vector — a reallocation leaves this walking a freed buffer.
    std::scoped_lock lk(cache_mutex);
    auto it = node_map.find(id);
    if(it != node_map.end())
    {
        for(const KeyTransform& key: it->second)
        {
            cache.erase(key);
        }
    }
    node_map.erase(id);
}

/////////////////////

///// Computation of resultant RTMat going from A to common ancestor and from common ancestor to B (inverted)
//std::optional<InnerEigenAPI::Lists> InnerEigenAPI::setLists(const std::string &dest, const std::string &orig)
//{
//    std::list<NodeMatrix> listA, listB;
//    Mat::RTMat atotal(Mat::RTMat::Identity());
//    Mat::RTMat btotal(Mat::RTMat::Identity());
//
//    auto an = G->get_node(orig);
//    auto bn = G->get_node(dest);
//    if ( not an.has_value() or  not bn.has_value())
//        return {};
//    auto a = an.value(); auto b = bn.value();
//
//    int minLevel = std::min(G->get_node_level(a).value_or(-1), G->get_node_level(b).value_or(-1));
//    if (minLevel == -1)
//        return {};
//    while (G->get_node_level(a).value_or(-1) >= minLevel)
//    {
//        qDebug() << "listaA" << a.id() << G->get_node_level(a).value() << G->get_parent_id(a).value();
//        auto p_node = G->get_parent_node(a);
//        if( not p_node.has_value())
//            break;  // FIX THIS GIVING MORE INFO
//        auto edge_rt = G->get_edge_RT(p_node.value(), a.id()).value();
//        auto rtmat = G->get_edge_RT_as_rtmat(edge_rt).value();
//        atotal = rtmat * atotal;
//        listA.emplace_back(std::make_tuple(p_node.value().id(), std::move(rtmat)));   // the downwards RT link from parent to a
//        a = p_node.value();
//    }
//    while (G->get_node_level(b).value_or(-1) >= minLevel)
//    {
//        qDebug() << "listaB" << b.id() << G->get_node_level(b).value() << G->get_parent_id(b).value();
//        auto p_node = G->get_parent_node(b);
//        if(not p_node.has_value())
//            break;
//        auto edge_rt = G->get_edge_RT(p_node.value(), b.id()).value();
//        auto rtmat = G->get_edge_RT_as_rtmat(edge_rt).value();
//        btotal = rtmat.inverse() * btotal;
//        listB.emplace_front(std::make_tuple(p_node.value().id(), std::move(rtmat)));
//        b = p_node.value();
//    }
//    // from min_level up tp the common ancestor
//    while (a.id() != b.id())
//    {
//        auto p_node = G->get_node(G->get_parent_id(a).value_or(-1));
//        auto q_node = G->get_node(G->get_parent_id(b).value_or(-1));
//        if(p_node.has_value() and q_node.has_value())
//        {
//            qDebug() << "listas A&B" << p_node.value().id() << q_node.value().id();
//            auto a_edge_rt = G->get_edge_RT(p_node.value(), a.id()).value();
//            auto b_edge_rt = G->get_edge_RT(q_node.value(), b.id()).value();
//            auto a_rtmat = G->get_edge_RT_as_rtmat(a_edge_rt).value();
//            auto b_rtmat = G->get_edge_RT_as_rtmat(b_edge_rt).value();
//            atotal = a_rtmat * atotal;
//            btotal = b_rtmat.inverse() * btotal;
//            listA.emplace_back(std::make_tuple(p_node.value().id(), std::move(a_rtmat)));
//            listB.emplace_front(std::make_tuple(q_node.value().id(), std::move(b_rtmat)));
//            a = p_node.value();
//            b = q_node.value();
//        }
//        else
//            return {};
//    }
//    //	std::cout << "----QAUDUD--------" << std::endl;
//    //	std::cout << (atotal*btotal).matrix() << std::endl;
//    return std::make_tuple(listA, listB);
//}

//std::optional<Mat::RTMat> InnerEigenAPI::get_transformation_matrix(const std::string &dest, const std::string &orig)
//{
//	Mat::RTMat ret(Mat::RTMat::Identity());
//   KeyTransform key = std::make_tuple(dest, orig);
//    if( TransformCache::iterator it = cache.find(key) ; it != cache.end())
//        ret = it->second;
//    else
//	{
//        auto lists = setLists(dest, orig);
//        if(!lists.has_value())
//            return {};
//        auto &[listA, listB] = lists.value();
//
//        for(auto &[id, mat]: listA )
//        {
//            ret = mat * ret;
//            node_map[id].push_back(key); // update node cache reference
//        }
//        Mat::RTMat retB(Mat::RTMat::Identity());
//        for(auto &[id, mat]: listB )
//        {
//            //ret = mat.inverse() * ret;
//            retB = mat * retB;
//            node_map[id].push_back(key); // update node cache reference
//        }
//        ret = retB.inverse() * ret;
//        std::cout << "----ORIG--------" << std::endl;
//        std::cout << ret.matrix() << std::endl;
//        // update node cache reference
//        int32_t dst_id = G->get_node(dest).value().id();
//        node_map[dst_id].push_back(key);
//        int32_t orig_id = G->get_node(orig).value().id();
//        node_map[orig_id].push_back(key);
//
//        // update cache
//        cache[key] = ret;
//    }
//	return ret;
//}
