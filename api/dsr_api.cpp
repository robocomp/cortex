//
// Created by crivac on 5/02/19.
//

#include "dsr/api/dsr_graph_settings.h"
#include "dsr/core/topics/IDLGraph.hpp"
#include "dsr/core/types/translator.h"
#include "include/dsr/api/dsr_signal_emitter.h"
#include <chrono>
#include <ctime>
#include <dsr/api/dsr_api.h>
#include <dsr/core/types/crdt_types.h>
#include <iostream>
#include <optional>
#include <qglobal.h>
#include <unistd.h>
#include <algorithm>
#include <utility>
#include <cmath>
#include <dsr/api/dsr_logging.h>

#include <fastdds/rtps/RTPSDomain.hpp>


#include <QtCore/qlogging.h>
#include <QtCore/qdebug.h>

using namespace DSR;

using namespace std::literals;

/////////////////////////////////////////////////
///// PUBLIC METHODS
/////////////////////////////////////////////////

DSRGraph::DSRGraph(GraphSettings settings) :
        agent_id(settings.agent_id),
        agent_name(std::move(settings.graph_name)),
        copy(false),
        tp(settings.theradpool_threads),
        tp_delta_attr(settings.attribute_threadpool_threads),
        same_host(settings.same_host),
        generator(settings.agent_id),
        log_level(settings.log_level)
{

    qDebug() << "Agent name: " << QString::fromStdString(agent_name);
    utils =  std::make_unique<Utilities>(this);
    if (settings.signal_mode == SignalMode::QT) {
        set_qt_signals();
    } else {
        set_queued_signals();
    }
    // RTPS Create participant
    auto[suc, participant_handle] = dsrparticipant.init(agent_id, agent_name, settings.same_host,
                                                        ParticipantChangeFunctor(this, [&](DSR::DSRGraph *graph,
                                                                eprosima::fastdds::rtps::ParticipantDiscoveryStatus status,
                                                                const eprosima::fastdds::rtps::ParticipantBuiltinTopicData& info)
                                                                {
                                                                    if (status == eprosima::fastdds::rtps::ParticipantDiscoveryStatus::DISCOVERED_PARTICIPANT)
                                                                    {
                                                                        std::unique_lock<std::mutex> lck(participant_set_mutex);
                                                                        std::cout << "Participant matched [" << info.participant_name.to_string() << "]" << std::endl;
                                                                        graph->participant_set.emplace(info.participant_name.to_string(), false);
                                                                    }
                                                                    else if (status == eprosima::fastdds::rtps::ParticipantDiscoveryStatus::REMOVED_PARTICIPANT ||
                                                                             status == eprosima::fastdds::rtps::ParticipantDiscoveryStatus::DROPPED_PARTICIPANT)
                                                                    {
                                                                        std::unique_lock<std::mutex> lck(participant_set_mutex);
                                                                        graph->participant_set.erase(info.participant_name.to_string());
                                                                        std::cout << "Participant unmatched [" << info.participant_name.to_string() << "]" << std::endl;
                                                                        graph->delete_node(info.participant_name.to_string());
                                                                    }
                                                                }), settings.domain_id);


    // RTPS Initialize publisher with general topic
    auto [res, pub, writer] = dsrpub_node.init(participant_handle, dsrparticipant.getNodeTopic(), dsrparticipant.get_domain_id());
    auto [res2, pub2, writer2] = dsrpub_node_attrs.init(participant_handle, dsrparticipant.getAttNodeTopic(), dsrparticipant.get_domain_id());

    auto [res3, pub3, writer3] = dsrpub_edge.init(participant_handle, dsrparticipant.getEdgeTopic(), dsrparticipant.get_domain_id());
    auto [res4, pub4, writer4] = dsrpub_edge_attrs.init(participant_handle, dsrparticipant.getAttEdgeTopic(), dsrparticipant.get_domain_id());

    auto [res5, pub5, writer5] = dsrpub_graph_request.init(participant_handle, dsrparticipant.getGraphRequestTopic(), dsrparticipant.get_domain_id());
    auto [res6, pub6, writer6] = dsrpub_request_answer.init(participant_handle, dsrparticipant.getGraphTopic(), dsrparticipant.get_domain_id());

    dsrparticipant.add_publisher(dsrparticipant.getNodeTopic()->get_name(), {pub, writer});
    dsrparticipant.add_publisher(dsrparticipant.getAttNodeTopic()->get_name(), {pub2, writer2});
    dsrparticipant.add_publisher(dsrparticipant.getEdgeTopic()->get_name(), {pub3, writer3});
    dsrparticipant.add_publisher(dsrparticipant.getAttEdgeTopic()->get_name(), {pub4, writer4});
    dsrparticipant.add_publisher(dsrparticipant.getGraphRequestTopic()->get_name(), {pub5, writer5});
    dsrparticipant.add_publisher(dsrparticipant.getGraphTopic()->get_name(), {pub6, writer6});

    // RTPS Initialize comms threads
    if (!settings.input_file.empty())
    {
        try
        {
            read_from_json_file(settings.input_file);
            qDebug() << __FUNCTION__ << "Warning, graph read from file " << QString::fromStdString(settings.input_file);
        }
        catch(const DSR::DSRException& e)
        {
            std::cout << e.what() << '\n';
            qFatal("Aborting program. Cannot continue without intial file");
        }
        start_fullgraph_server_thread();
        start_subscription_threads();
    }
    else
    {
        start_subscription_threads();     // regular subscription to deltas
        auto [response, repeated]  = start_fullgraph_request_thread();    // for agents that want to request the graph for other agent

        if(!response)
        {
            dsrparticipant.remove_participant_and_entities(); // Remove a Participant and all associated publishers and subscribers.

            if (repeated)
            {
                qFatal("%s", (std::string("There is already an agent connected with the id: ") + std::to_string(agent_id)).c_str());
            }
            else {
                qFatal("DSRGraph aborting: could not get DSR from the network after timeout");
            }
        }
    }
    qDebug() << __FUNCTION__ << "Constructor finished OK";
}

DSRGraph::DSRGraph(std::string name, uint32_t id, const std::string &dsr_input_file, bool all_same_host, int8_t domain_id, SignalMode mode)
    : DSR::DSRGraph(GraphSettings {id, 5, 1, name, dsr_input_file, "", all_same_host, GraphSettings::LOGLEVEL::INFOL, domain_id, mode})
{}


DSRGraph::~DSRGraph()
{
    qDebug() << "Removing DSRGraph";
    dsrparticipant.remove_participant_and_entities();
    if (!copy) {
        qDebug() << "Removing rtps participant";
    }
}

//////////////////////////////////////
/// NODE METHODS
/////////////////////////////////////

std::optional<DSR::Node> DSRGraph::get_node(const std::string &name)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    if (name.empty()) return {};
    std::optional<uint64_t> id = get_id_from_name(name);
    if (id.has_value())
    {
        std::optional<CRDTNode> n = get_(id.value());
        if (n.has_value()) return Node(std::move(n.value()));
    }
    return {};
}

std::optional<DSR::Node> DSRGraph::get_node(uint64_t id)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::optional<CRDTNode> n = get_(id);
    if (n.has_value()) return Node(std::move(n.value()));
    return {};
}

std::tuple<bool, std::optional<IDL::MvregNode>> DSRGraph::insert_node_(CRDTNode &&node)
{
    if (!deleted.contains(node.id()))
    {
        if (auto it = nodes.find(node.id()); it != nodes.end() and not it->second.empty() and it->second.read_reg() == node)
        {
            return {true, {}};
        }

        uint64_t id = node.id();
        update_maps_node_insert(id, node);
        mvreg<CRDTNode> delta = nodes[id].write(std::move(node));

        return {true, CRDTNode_to_IDL(agent_id, id, delta)};
    }
    return {false, {}};
}

template<typename No>
std::optional<uint64_t> DSRGraph::insert_node(No &&node)
    requires (std::is_same_v<std::remove_reference_t<No>, DSR::Node>)
{
    std::optional<IDL::MvregNode> delta;
    bool inserted = false;
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        std::shared_lock<std::shared_mutex> lck_cache(_mutex_cache_maps);
        uint64_t new_node_id = generator.generate();
        node.id(new_node_id);
        if (node.name().empty() or name_map.contains(node.name()))
            node.name(node.type() + "_" + id_generator::hex_string(new_node_id));
        lck_cache.unlock();
        std::tie(inserted, delta) = insert_node_(user_node_to_crdt(std::forward<No>(node)));
    }
    if (inserted)
    {
        if (!copy)
        {
            if (delta.has_value())
            {
                dsrpub_node.write(&delta.value());
                DSR_LOG_DEBUG("[INSERT_NODE] emitting update_node_signal", node.id(), node.type());
                emitter.update_node_signal(node.id(), node.type(), SignalInfo{agent_id});
                for (const auto &[k, v]: node.fano())
                {
                    emitter.update_edge_signal(node.id(), k.first, k.second,  SignalInfo{agent_id});
                }
            }
        }
        return node.id();
    }
    return {};
}

template std::optional<uint64_t>  DSRGraph::insert_node<DSR::Node &&>(DSR::Node&&);
template std::optional<uint64_t>  DSRGraph::insert_node<DSR::Node&>(DSR::Node&);


template<typename No>
std::optional<uint64_t> DSRGraph::insert_node_with_id(No &&node)
    requires (std::is_same_v<std::remove_reference_t<No>, DSR::Node>)
{
    std::optional<IDL::MvregNode> delta;
    bool inserted = false;
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        std::shared_lock<std::shared_mutex> lck_cache(_mutex_cache_maps);
        //Check id
        if (nodes.contains(node.id())) {
            DSR_LOG_WARNING("[INSERT_NODE_WITH_ID] Node id already exists", node.id(), node.type());
            return {};
        }
        if (node.name().empty() or name_map.contains(node.name()))
            node.name(node.type() + "_" + id_generator::hex_string(node.id()));
        lck_cache.unlock();
        std::tie(inserted, delta) = insert_node_(user_node_to_crdt(std::forward<No>(node)));
    }
    if (inserted)
    {
        if (!copy)
        {
            if (delta.has_value())
            {
                dsrpub_node.write(&delta.value());
                DSR_LOG_DEBUG("[INSERT_NODE_WITH_ID] emitting update_node_signal", node.id(), node.type());
                emitter.update_node_signal(node.id(), node.type(), SignalInfo{agent_id});
                for (const auto &[k, v]: node.fano())
                {
                    emitter.update_edge_signal(node.id(), k.first, k.second,  SignalInfo{agent_id});
                }
            }
        }
        return node.id();
    }
    return {};
}

template std::optional<uint64_t>  DSRGraph::insert_node_with_id<DSR::Node &&>(DSR::Node&&);
template std::optional<uint64_t>  DSRGraph::insert_node_with_id<DSR::Node&>(DSR::Node&);

std::tuple<bool, std::optional<std::vector<IDL::MvregNodeAttr>>> DSRGraph::update_node_(CRDTNode &&node)
{

    if (!deleted.contains(node.id()))
    {
        auto nit = nodes.find(node.id());
        if (nit != nodes.end() && !nit->second.empty())
        {
            std::vector<IDL::MvregNodeAttr> atts_deltas;
            auto &iter = nit->second.read_reg().attrs();
            //New attributes and updates.
            for (auto &[k, att]: node.attrs()) {
                auto &attr_reg = iter.try_emplace(k, mvreg<CRDTAttribute>()).first->second;
                if (attr_reg.empty() or att.read_reg() != attr_reg.read_reg()) {
                    auto delta = attr_reg.write(std::move(att.read_reg()));
                    atts_deltas.emplace_back(
                            CRDTNodeAttr_to_IDL(agent_id, node.id(), node.id(), k, delta));
                }
            }
            //Remove old attributes.
            auto it_a = iter.begin();
            while (it_a != iter.end()) {
                const std::string &k = it_a->first;
                if (ignored_attributes.contains(k)) {
                    it_a = iter.erase(it_a);
                } else if (!node.attrs().contains(k)) {
                    auto delta = it_a->second.reset();
                    atts_deltas.emplace_back(
                            CRDTNodeAttr_to_IDL(node.agent_id(), node.id(), node.id(), k, delta));
                    it_a = iter.erase(it_a);
                } else {
                    ++it_a;
                }
            }

            return {true, std::move(atts_deltas)};
        }
    }

    return {false, {}};
}
template<typename No>
bool DSRGraph::update_node(No &&node)
requires (std::is_same_v<std::remove_cvref_t<No>, DSR::Node>)
{

    bool updated = false;
    std::optional<std::vector<IDL::MvregNodeAttr>> vec_node_attr;

    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        std::shared_lock<std::shared_mutex> lck_cache(_mutex_cache_maps);
        if (deleted.contains(node.id()))
            throw std::runtime_error(
                    (std::string("Cannot update node in G, " + std::to_string(node.id()) + " is deleted") + __FILE__ +
                     " " + __FUNCTION__ + " " + std::to_string(__LINE__)).data());
        else if (( id_map.contains(node.id()) and id_map.at(node.id()) != node.name()) or
                 ( name_map.contains(node.name()) and name_map.at(node.name()) != node.id()))
            throw std::runtime_error(
                    (std::string("Cannot update node in G, id and name must be unique") + __FILE__ + " " +
                     __FUNCTION__ + " " + std::to_string(__LINE__)).data());
        else if (nodes.contains(node.id())) {
            lck_cache.unlock();
            std::tie(updated, vec_node_attr) = update_node_(user_node_to_crdt(std::forward<No>(node)));
        }
    }
    if (updated) {
        if (!copy) {
            if (vec_node_attr.has_value()) {
                dsrpub_node_attrs.write(&vec_node_attr.value());
                DSR_LOG_DEBUG("[UPDATE_NODE] emitting update_node_signal", node.id(), node.type());
                emitter.update_node_signal(node.id(), node.type(), SignalInfo{agent_id});
                std::vector<std::string> atts_names(vec_node_attr->size());
                std::transform(std::make_move_iterator(vec_node_attr->begin()),
                               std::make_move_iterator(vec_node_attr->end()),
                               atts_names.begin(),
                               [](auto &&x) { return x.attr_name(); });
                emitter.update_node_attr_signal(node.id(), atts_names, SignalInfo{agent_id});

            }
        }
    }
    return updated;
}

template bool DSRGraph::update_node<DSR::Node &&>(DSR::Node&&);
template bool DSRGraph::update_node<DSR::Node&>(DSR::Node&);
template bool DSRGraph::update_node<const DSR::Node&>(const DSR::Node&);
template bool DSRGraph::update_node<DSR::Node>(DSR::Node&&);



std::tuple<bool, std::vector<Edge>, std::optional<IDL::MvregNode>, std::vector<IDL::MvregEdge>>
DSRGraph::delete_node_(uint64_t id) {

    std::vector<Edge> deleted_edges;
    std::vector<IDL::MvregEdge> delta_vec;

    //Get and remove node.
    auto node = get_(id);
    if (!node.has_value()) return make_tuple(false, deleted_edges, std::nullopt, delta_vec);
    // Delete all edges from this node.
    for (const auto &v : node.value().fano()) {
        deleted_edges.emplace_back(v.second.read_reg());
    }
    // Get remove delta.
    auto delta = nodes[id].reset();
    IDL::MvregNode delta_remove = CRDTNode_to_IDL(agent_id, id, delta);
    // Search and remove incoming edges using to_edges cache: O(k) instead of O(n).
    {
        decltype(to_edges)::mapped_type incoming;
        {
            std::shared_lock<std::shared_mutex> lck_cache(_mutex_cache_maps);
            if (to_edges.contains(id))
                incoming = to_edges.at(id);
        }
        for (const auto &[from, type] : incoming)
        {
            if (!nodes.contains(from)) continue;
            auto &visited_node = nodes.at(from).read_reg();
            deleted_edges.emplace_back(visited_node.fano().at({id, type}).read_reg());
            auto delta_fano = visited_node.fano().at({id, type}).reset();
            delta_vec.emplace_back(CRDTEdge_to_IDL(agent_id, from, id, type, delta_fano));
            visited_node.fano().erase({id, type});
            update_maps_edge_delete(from, id, type);
        }
    }
    update_maps_node_delete(id, node.value());

    return make_tuple(true, std::move(deleted_edges), std::move(delta_remove), std::move(delta_vec));

}
bool DSRGraph::delete_node(const DSR::Node &node)
{
    return delete_node(node.id());
}

bool DSRGraph::delete_node(const std::string &name)
{

    bool result = false;
    std::vector<Edge> deleted_edges;
    std::optional<IDL::MvregNode> deleted_node;
    std::vector<IDL::MvregEdge> delta_vec;
    std::optional<Node> node_signal;
    std::optional<uint64_t> id = {};
    {
        id = get_id_from_name(name);
        if (id.has_value()) {
            node_signal = get_(*id);
            std::unique_lock<std::shared_mutex> lock(_mutex);
            std::tie(result, deleted_edges, deleted_node, delta_vec) = delete_node_(id.value());
        } else {
            return false;
        }
    }

    if (result) {
        if (!copy) {
            DSR_LOG_DEBUG("[DELETE_NODE] emitting del_node_signal", id.value());
            emitter.del_node_signal(id.value(), SignalInfo{agent_id});
            if (node_signal) emitter.deleted_node_signal(*node_signal, SignalInfo{agent_id});
            dsrpub_node.write(&deleted_node.value());

            for (auto &a : delta_vec) {
                dsrpub_edge.write(&a);
            }
            for (auto &edge : deleted_edges) {
                emitter.del_edge_signal(edge.from(), edge.to(), edge.type(), SignalInfo{ agent_id });
                emitter.deleted_edge_signal(edge, SignalInfo{ agent_id });
            }
        }
        return true;
    }
    return false;
}

bool DSRGraph::delete_node(uint64_t id)
{

    bool result = false;
    std::vector<Edge> deleted_edges;
    std::optional<IDL::MvregNode> deleted_node;
    std::optional<Node> node_signal;
    std::vector<IDL::MvregEdge> delta_vec;
    {
        node_signal = get_(id);
        std::unique_lock<std::shared_mutex> lock(_mutex);
        std::tie(result, deleted_edges, deleted_node, delta_vec) = delete_node_(id);
    }

    if (result) {
        if (!copy) {
            DSR_LOG_DEBUG("[DELETE_NODE] emitting del_node_signal", id);
            emitter.del_node_signal(id, SignalInfo{ agent_id });
            if (node_signal) emitter.deleted_node_signal(*node_signal, SignalInfo{agent_id});
            dsrpub_node.write(&deleted_node.value());

            for (auto &a  : delta_vec) {
                dsrpub_edge.write(&a);
            }
            for (auto &edge : deleted_edges) {
                emitter.del_edge_signal(edge.from(), edge.to(), edge.type(), SignalInfo{ agent_id });
                emitter.deleted_edge_signal(edge, SignalInfo{ agent_id });
            }
        }
        return true;
    }

    return false;
}


std::vector<DSR::Node> DSRGraph::get_nodes_by_type(const std::string &type)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::shared_lock<std::shared_mutex> lck(_mutex_cache_maps);

    std::vector<Node> nodes_;
    if (nodeType.contains(type))
    {
        nodes_.reserve(nodeType.at(type).size());
        for (auto &id: nodeType.at(type))
        {
            std::optional<CRDTNode> n = get_(id);
            if (n.has_value())
                nodes_.emplace_back(std::move(n.value()));
        }
    }
    return nodes_;
}

std::vector<Node> DSRGraph::get_nodes()
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    std::vector<Node> nodes_;
    nodes_.reserve(nodes.size());

    for (auto &[id, N]: nodes)
    {
        nodes_.emplace_back(N.read_reg());
    }
    return nodes_;

}


std::vector<DSR::Node> DSRGraph::get_nodes_by_types(const std::vector<std::string> &types)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::shared_lock<std::shared_mutex> lck(_mutex_cache_maps);

    std::vector<Node> nodes_;
    {
        size_t total = 0;
        for (const auto &type : types)
            if (nodeType.contains(type)) total += nodeType.at(type).size();
        nodes_.reserve(total);
    }
    for (auto &type : types)
    {
        if (nodeType.contains(type))
        {
            for (auto &id: nodeType.at(type))
            {
                std::optional<CRDTNode> n = get_(id);
                if (n.has_value())
                    nodes_.emplace_back(std::move(n.value()));
            }
        }
    }
    return nodes_;
}

//////////////////////////////////////////////////////////////////////////////////
// EDGE METHODS
//////////////////////////////////////////////////////////////////////////////////
std::optional<CRDTEdge> DSRGraph::get_edge_(uint64_t from, uint64_t to, const std::string &key)
{
    //std::shared_lock<std::shared_mutex> lock(_mutex);
    if (nodes.contains(from) && nodes.contains(to))
    {
        auto n = get_(from);
        if (n.has_value()) {
            auto edge = n.value().fano().find({to, key});
            if (edge != n.value().fano().end()) {
                return edge->second.read_reg();
            }
        }
    }
    return {};
}

std::optional<DSR::Edge> DSRGraph::get_edge(const std::string &from, const std::string &to, const std::string &key)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::optional<uint64_t> id_from = get_id_from_name(from);
    std::optional<uint64_t> id_to = get_id_from_name(to);
    if (id_from.has_value() and id_to.has_value())
    {
        auto edge_opt = get_edge_(id_from.value(), id_to.value(), key);
        if (edge_opt.has_value()) return Edge(edge_opt.value());
    }
    return {};
}

std::optional<DSR::Edge> DSRGraph::get_edge(uint64_t from, uint64_t to, const std::string &key)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    auto edge_opt = get_edge_(from, to, key);
    if (edge_opt.has_value()) return Edge(std::move(edge_opt.value()));
    return {};
}

std::optional<Edge> DSRGraph::get_edge(const Node &n, const std::string &to, const std::string &key)
{
    std::optional<uint64_t> id_to = get_id_from_name(to);
    if (id_to.has_value())
    {
        auto it = n.fano().find({id_to.value(), key});
        if (it != n.fano().end()) return it->second;
    }
    return {};
}

std::optional<Edge> DSRGraph::get_edge(const Node &n, uint64_t to, const std::string &key)
{
    auto it = n.fano().find({to, key});
    if (it != n.fano().end()) return it->second;
    return {};
}


std::tuple<bool, std::optional<IDL::MvregEdge>, std::optional<std::vector<IDL::MvregEdgeAttr>>>
DSRGraph::insert_or_assign_edge_(CRDTEdge &&attrs, uint64_t from, uint64_t to)
{

    std::optional<IDL::MvregEdge> delta_edge;
    std::optional<std::vector<IDL::MvregEdgeAttr>> delta_attrs;

    if (nodes.contains(from))
    {
        auto &node = nodes.at(from).read_reg();
        //check if we are creating an edge or we are updating it.
        auto fano_it = node.fano().find({to, attrs.type()});
        if (fano_it != node.fano().end())
        {
            //Update
            std::vector<IDL::MvregEdgeAttr> atts_deltas;
            auto &iter_edge = fano_it->second.read_reg().attrs();
            for (auto &[k, att]: attrs.attrs()) {
                auto &attr_reg = iter_edge.try_emplace(k, mvreg<CRDTAttribute>()).first->second;
                if (attr_reg.empty() or att.read_reg() != attr_reg.read_reg()) {
                    auto delta = attr_reg.write(std::move(att.read_reg()));
                    atts_deltas.emplace_back(
                            CRDTEdgeAttr_to_IDL(agent_id, from, from, to, attrs.type(), k, delta));
                }
            }
            auto it = iter_edge.begin();
            while (it != iter_edge.end()) {
                if (!attrs.attrs().contains(it->first)) {
                    std::string att = it->first;
                    auto delta = it->second.reset();
                    it = iter_edge.erase(it);
                    atts_deltas.emplace_back(
                            CRDTEdgeAttr_to_IDL(agent_id, from, from, to, attrs.type(), att, delta));
                } else {
                    ++it;
                }
            }
            return {true, {}, std::move(atts_deltas)};
        } else
        { // Insert
            //node.fano().insert({{to, attrs.type()}, mvreg<CRDTEdge>()});
            std::string att_type = attrs.type();
            auto delta = node.fano()[{to, attrs.type()}].write(std::move(attrs));
            update_maps_edge_insert(from, to, att_type);
            return {true, CRDTEdge_to_IDL(agent_id, from, to, att_type, delta), {}};
        }
    }
    return {false, {}, {}};
}

template<typename Ed>
bool DSRGraph::insert_or_assign_edge(Ed &&attrs)
requires (std::is_same_v<std::remove_cvref_t<Ed>, DSR::Edge>)
{
    bool result = false;
    std::optional<IDL::MvregEdge> delta_edge;
    std::optional<std::vector<IDL::MvregEdgeAttr>> delta_attrs;

    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        uint64_t from = attrs.from();
        uint64_t to = attrs.to();
        if (nodes.contains(from) && nodes.contains(to)) {
            std::tie(result, delta_edge, delta_attrs) = insert_or_assign_edge_(user_edge_to_crdt(std::forward<Ed>(attrs)), from, to);
        } else {
            std::cout << __FUNCTION__ << ":" << __LINE__ << " Error. ID:" << from << " or " << to
                      << " not found. Cant update. " << std::endl;
            return false;
        }
    }
    if (result) {
        if (!copy) {
            DSR_LOG_DEBUG("[INSERT_OR_ASSIGN_EDGE] emitting update_edge_signal", attrs.from(), attrs.to(), attrs.type());
            emitter.update_edge_signal(attrs.from(), attrs.to(), attrs.type(), SignalInfo{ agent_id });

            if (delta_edge.has_value()) { //Insert
                dsrpub_edge.write(&delta_edge.value());
            }
            if (delta_attrs.has_value()) { //Update
                dsrpub_edge_attrs.write(&delta_attrs.value());
                std::vector<std::string> atts_names(delta_attrs->size());
                std::transform(std::make_move_iterator(delta_attrs->begin()),
                               std::make_move_iterator(delta_attrs->end()),
                               atts_names.begin(),
                               [](auto &&x) { return x.attr_name(); });

                emitter.update_edge_attr_signal(attrs.from(), attrs.to(), attrs.type(), atts_names, SignalInfo{ agent_id });

            }
        }
    }
    return true;
}


template bool DSRGraph::insert_or_assign_edge<DSR::Edge>(DSR::Edge&&);
template bool DSRGraph::insert_or_assign_edge<DSR::Edge &&>(DSR::Edge&&);
template bool DSRGraph::insert_or_assign_edge<DSR::Edge&>(DSR::Edge&);
template bool DSRGraph::insert_or_assign_edge<const DSR::Edge&>(const DSR::Edge&);


std::optional<IDL::MvregEdge> DSRGraph::delete_edge_(uint64_t from, uint64_t to, const std::string &key)
{
    if (nodes.contains(from)) {
        auto &node = nodes.at(from).read_reg();
        if (node.fano().contains({to, key})) {
            auto delta = node.fano().at({to, key}).reset();
            node.fano().erase({to, key});
            update_maps_edge_delete(from, to, key);
            return CRDTEdge_to_IDL(agent_id, from, to, key, delta);
        }
    }
    return {};
}

bool DSRGraph::delete_edge(uint64_t from, uint64_t to, const std::string &key)
{

    std::optional<IDL::MvregEdge> delta;
    std::optional<Edge> deleted_edge;
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        deleted_edge = get_edge_(from, to, key);
        delta = delete_edge_(from, to, key);
    }
    if (delta.has_value())
    {
        if (!copy) {
            DSR_LOG_DEBUG("[DELETE_EDGE] emitting del_edge_signal", from, to, key);
            emitter.del_edge_signal(from, to, key, SignalInfo{ agent_id });
            if (deleted_edge.has_value()) {
                emitter.deleted_edge_signal(*deleted_edge, SignalInfo{ agent_id });
            }
            dsrpub_edge.write(&delta.value());
        }
        return true;
    }
    return false;
}

bool DSRGraph::delete_edge(const std::string &from, const std::string &to, const std::string &key)
{

    std::optional<uint64_t> id_from = {};
    std::optional<uint64_t> id_to = {};
    std::optional<IDL::MvregEdge> delta;
    std::optional<Edge> deleted_edge;
    {
        id_from = get_id_from_name(from);
        id_to = get_id_from_name(to);
        std::unique_lock<std::shared_mutex> lock(_mutex);
        deleted_edge = get_edge_(id_from.value(), id_to.value(), key);
        if (id_from.has_value() && id_to.has_value())
        {
            delta = delete_edge_(id_from.value(), id_to.value(), key);
        }
    }
    if (delta.has_value())
    {
        if (!copy) {
            emitter.del_edge_signal(id_from.value(), id_to.value(), key, SignalInfo{ agent_id });
            if (deleted_edge.has_value()) {
                emitter.deleted_edge_signal(*deleted_edge, SignalInfo{ agent_id });
            }
            dsrpub_edge.write(&delta.value());
        }
        return true;
    }
    return false;
}


std::vector<DSR::Edge> DSRGraph::get_node_edges_by_type(const Node &node, const std::string &type)
{
    std::vector<Edge> edges_;
    for (auto &[key, edge] : node.fano())
        if (key.second == type)
            edges_.emplace_back(edge);
    return edges_;
}

std::vector<DSR::Edge> DSRGraph::get_edges_by_type(const std::string &type)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::shared_lock<std::shared_mutex> lock_cache(_mutex_cache_maps);
    std::vector<Edge> edges_;
    if (edgeType.contains(type)) {
        edges_.reserve(edgeType.at(type).size());
        for (auto &[from, to] : edgeType.at(type)) {
            auto n = get_edge_(from, to, type);
            if (n.has_value())
                edges_.emplace_back(std::move(n.value()));
        }
    }
    return edges_;
}

std::vector<DSR::Edge> DSRGraph::get_edges_to_id(uint64_t id)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::shared_lock<std::shared_mutex> lock_cache(_mutex_cache_maps);
    std::vector<Edge> edges_;
    if (to_edges.contains(id)) {
        edges_.reserve(to_edges.at(id).size());
        for (const auto &[k, v] : to_edges.at(id)) {
            auto n = get_edge_(k, id, v);
            if (n.has_value())
                edges_.emplace_back(std::move(n.value()));
        }
    }

    return edges_;
}

std::optional<std::map<std::pair<uint64_t, std::string>, DSR::Edge>> DSRGraph::get_edges(uint64_t id) {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::optional<Node> n = get_node(id);
    if (n.has_value())
    {
        return n->fano();
    }
    return std::nullopt;
}


/////////////////////////////////////////////////
///// Utils
/////////////////////////////////////////////////

std::map<uint64_t, DSR::Node> DSRGraph::getCopy() const
{
    std::map<uint64_t, Node> mymap;
    std::shared_lock<std::shared_mutex> lock(_mutex);

    for (auto &[key, val] : nodes)
        mymap.emplace(key, Node(val.read_reg()));

    return mymap;
}

//////////////////////////////////////////////////////////////////////////////
/////  CORE
//////////////////////////////////////////////////////////////////////////////

std::optional<CRDTNode> DSRGraph::get_(uint64_t id)
{
    auto it = nodes.find(id);
    if (it != nodes.end() and !it->second.empty())
    {
        return std::make_optional(it->second.read_reg());
    }
    return {};
}

std::optional<std::int32_t> DSRGraph::get_node_level(const Node &n)
{
    return get_attrib_by_name<level_att>(n);
}

std::optional<uint64_t> DSRGraph::get_parent_id(const Node &n)
{
    return get_attrib_by_name<parent_att>(n);
}

std::optional<DSR::Node> DSRGraph::get_parent_node(const Node &n)
{
    auto p = get_attrib_by_name<parent_att>(n);
    if (p.has_value())
    {
        std::shared_lock<std::shared_mutex> lock(_mutex);
        auto tmp = get_(p.value());
        if (tmp.has_value()) return Node(tmp.value());
    }
    return {};
}


std::string DSRGraph::get_node_type(Node &n)
{
    return n.type();
}

//////////////////////////////////////////////////////////////////////////////////////////////

inline void DSRGraph::update_maps_node_delete(uint64_t id, const std::optional<CRDTNode> &n)
{
    nodes.erase(id);

    std::unique_lock<std::shared_mutex> lck(_mutex_cache_maps);
    if (id_map.contains(id))
    {
        name_map.erase(id_map.at(id));
        id_map.erase(id);
    }
    deleted.insert(id);
    to_edges.erase(id);

    if (n.has_value())
    {
        if (nodeType.contains(n->type())) {
            nodeType.at(n->type()).erase(id);
            if (nodeType.at(n->type()).empty()) nodeType.erase(n->type());
        }
        for (const auto &[k, v] : n->fano()) {
            if (auto tuple = std::pair{id, v.read_reg().to()}; edges.contains(tuple)) {
                edges.at(tuple).erase(k.second);
                if (edges.at(tuple).empty()) edges.erase(tuple);
            }
            if (auto tuple = std::pair{id, k.first}; edgeType.contains(k.second)) {
                edgeType.at(k.second).erase(tuple);
                if (edgeType.at(k.second).empty())edgeType.erase(k.second);
            }
            if (auto tuple = std::pair{id, k.second}; to_edges.contains(k.first)) {
                to_edges.at(k.first).erase(tuple);
                if (to_edges.at(k.first).empty()) to_edges.erase(k.first);
            }
        }
    }
}

inline void DSRGraph::update_maps_node_insert(uint64_t id, const CRDTNode &n)
{
    std::unique_lock<std::shared_mutex> lck(_mutex_cache_maps);

    name_map[n.name()] = id;
    id_map[id] = n.name();
    nodeType[n.type()].emplace(id);
    for (const auto &[k, v] : n.fano())
    {
        edges[{id, k.first}].insert(k.second);
        edgeType[k.second].insert({id, k.first});
        to_edges[k.first].insert({id, k.second});
    }
}


inline void DSRGraph::update_maps_edge_delete(uint64_t from, uint64_t to, const std::string &key)
{

    std::unique_lock<std::shared_mutex> lck(_mutex_cache_maps);
    if (const auto tuple = std::pair{from, to}; edges.contains(tuple)) {
        edges.at(tuple).erase(key);
        if (edges.at(tuple).empty()) edges.erase(tuple);
    }

    if (to_edges.contains(to)) {
        to_edges.at(to).erase({from, key});
        if (to_edges.at(to).empty()) to_edges.erase(to);
    }

    if (edgeType.contains(key)) {
        edgeType.at(key).erase({from, to});
        if (edgeType.at(key).empty()) edgeType.erase(key);
    }
}

inline void DSRGraph::update_maps_edge_insert(uint64_t from, uint64_t to, const std::string &key)
{
    std::unique_lock<std::shared_mutex> lck(_mutex_cache_maps);

    edges[{from, to}].insert(key);
    to_edges[to].insert({from, key});
    edgeType[key].insert({from, to});

}


std::optional<uint64_t> DSRGraph::get_id_from_name(const std::string &name)
{
    std::shared_lock<std::shared_mutex> lck(_mutex_cache_maps);
    auto v = name_map.find(name);
    if (v != name_map.end()) return v->second;
    return {};
}

std::optional<std::string> DSRGraph::get_name_from_id(uint64_t id)
{
    std::shared_lock<std::shared_mutex> lck(_mutex_cache_maps);
    auto v = id_map.find(id);
    if (v != id_map.end()) return v->second;
    return {};
}

size_t DSRGraph::size() const {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return nodes.size();
}


bool DSRGraph::empty(const uint64_t &id)
{
    auto it = nodes.find(id);
    if (it != nodes.end()) {
        return it->second.empty();
    } else
        return false;
}

void DSRGraph::join_delta_node(IDL::MvregNode &&mvreg)
{

    std::optional<CRDTNode> maybe_deleted_node = {};
    try {
        bool signal = false, joined = false;
        auto id = mvreg.id();
        uint64_t timestamp = mvreg.timestamp();

        DSR_LOG_DEBUG("[JOIN_NODE] id:", id, "timestamp:", timestamp, "agent:", mvreg.agent_id());
        auto crdt_delta = IDLNode_to_CRDT(std::move(mvreg));
        bool d_empty = crdt_delta.empty();


        auto delete_unprocessed_deltas = [&](){
            unprocessed_delta_node_att.erase(id);
            decltype(unprocessed_delta_edge_from)::node_type node_handle = unprocessed_delta_edge_from.extract(id);
            while (!node_handle.empty())
            {
                node_handle = unprocessed_delta_edge_from.extract(id);
            }

            std::erase_if(unprocessed_delta_edge_to,
                          [&](auto &it){ return std::get<0>(it.second) == id;});
            std::erase_if(unprocessed_delta_edge_att,
                          [&](auto &it){ return std::get<0>(it.first) == id or std::get<1>(it.first) == id;});
        };


        std::unordered_set<std::pair<uint64_t, std::string>,hash_tuple> map_new_to_edges = {};
        std::unordered_set<std::tuple<uint64_t, uint64_t, std::string>,hash_tuple> map_new_from_edges = {};

        auto consume_unprocessed_deltas = [&](){
            decltype(unprocessed_delta_node_att)::node_type node_handle_node_att = unprocessed_delta_node_att.extract(id);
            while (!node_handle_node_att.empty())
            {
                auto &[att_name, delta, timestamp_node_att] = node_handle_node_att.mapped();
                DSR_LOG_DEBUG("[JOIN_NODE] node_att", id, att_name, (timestamp < timestamp_node_att));
                if (timestamp < timestamp_node_att) {
                    process_delta_node_attr(id, att_name,std::move(delta));
                }
                node_handle_node_att = unprocessed_delta_node_att.extract(id);
            }

            decltype(unprocessed_delta_edge_from)::node_type node_handle_edge = unprocessed_delta_edge_from.extract(id);
            while (!node_handle_edge.empty()) {
                auto &[to, type, delta, timestamp_edge] = node_handle_edge.mapped();
                auto att_key = std::tuple{id, to, type};
                DSR_LOG_DEBUG("[JOIN_NODE] unprocessed_delta_edge_from", id, to, type, (timestamp < timestamp_edge));
                if (timestamp < timestamp_edge) {
                    if (process_delta_edge(id, to, type, std::move(delta))) map_new_to_edges.emplace(to, type);
                }
                if (nodes.contains(id) and nodes.at(id).read_reg().fano().contains({to, type})) {
                    decltype(unprocessed_delta_edge_att)::node_type node_handle_edge_att =  unprocessed_delta_edge_att.extract(att_key);
                    while (!node_handle_edge_att.empty()) {
                        auto &[att_name, delta, timestamp_edge_att] = node_handle_edge_att.mapped();
                        DSR_LOG_DEBUG("[JOIN_NODE] edge_att", id, to, type, att_name, (timestamp < timestamp_edge));
                        if (timestamp < timestamp_edge_att) {
                            process_delta_edge_attr(id, to, type, att_name, std::move(delta));
                        }
                        node_handle_edge_att = unprocessed_delta_edge_att.extract(att_key);
                    }
                }
                std::erase_if(unprocessed_delta_edge_to,
                              [to = to, id = id, type = type](auto& it) { return it.first == to && std::get<0>(it.second) == id && std::get<1>(it.second) == type;});
                node_handle_edge = unprocessed_delta_edge_from.extract(id);
            }

            node_handle_edge = unprocessed_delta_edge_to.extract(id);
            while (!node_handle_edge.empty()) {
                auto &[from, type, delta, timestamp_edge] = node_handle_edge.mapped();
                auto att_key = std::tuple{from, id, type};
                DSR_LOG_DEBUG("[JOIN_NODE] unprocessed_delta_edge_to", from, id, type, (timestamp < timestamp_edge));
                if (timestamp < timestamp_edge) {
                    if (process_delta_edge(from, id, type, std::move(delta))) map_new_from_edges.emplace(from, id, type);
                }
                if (nodes.contains(from) and nodes.at(from).read_reg().fano().contains({id, type})) {
                    decltype(unprocessed_delta_edge_att)::node_type node_handle_edge_att =  unprocessed_delta_edge_att.extract(att_key);
                    while (!node_handle_edge_att.empty()) {
                        auto &[att_name, delta, timestamp_edge_att] = node_handle_edge_att.mapped();
                        DSR_LOG_DEBUG("[JOIN_NODE] edge_att", from, id, type, att_name, (timestamp < timestamp_edge));
                        if (timestamp < timestamp_edge_att) {
                            process_delta_edge_attr(from, id, type, att_name, std::move(delta));
                        }
                        node_handle_edge_att = unprocessed_delta_edge_att.extract(att_key);
                    }
                }

                node_handle_edge = unprocessed_delta_edge_to.extract(id);
            }

        };

        std::optional<std::unordered_set<std::pair<uint64_t, std::string>,hash_tuple>> cache_map_to_edges = {};
        {
            std::unique_lock<std::shared_mutex> lock(_mutex);
            if (!deleted.contains(id)) {
                joined = true;
                maybe_deleted_node = (nodes[id].empty()) ? std::nullopt : std::make_optional(nodes.at(id).read_reg());
                nodes[id].join(std::move(crdt_delta));
                if (nodes.at(id).empty() or d_empty) {
                    nodes.erase(id);
                    cache_map_to_edges = to_edges[id];
                    update_maps_node_delete(id, maybe_deleted_node);
                    delete_unprocessed_deltas();
                } else {
                    signal = true;
                    update_maps_node_insert(id, nodes.at(id).read_reg());
                    consume_unprocessed_deltas();
                }
            } else {
                delete_unprocessed_deltas();
            }
        }

        if (joined) {
            if (signal) {
                DSR_LOG_DEBUG("[JOIN_NODE] node inserted/updated:", id, nodes.at(id).read_reg().type());
                emitter.update_node_signal(id, nodes.at(id).read_reg().type(), SignalInfo{ mvreg.agent_id() });
                for (const auto &[k, v] : nodes.at(id).read_reg().fano()) {
                    DSR_LOG_DEBUG("[JOIN_NODE] add edge FROM:", id, k.first, k.second);
                    emitter.update_edge_signal(id, k.first, k.second, SignalInfo{ mvreg.agent_id() });
                }

                for (const auto &[k, v]: map_new_to_edges)
                {
                    DSR_LOG_DEBUG("[JOIN_NODE] add edge TO:", k, id, v);
                    emitter.update_edge_signal(k, id, v, SignalInfo{ mvreg.agent_id() });
                }

                for (const auto &[from, to, type]: map_new_from_edges)
                {
                    DSR_LOG_DEBUG("[JOIN_NODE] add edge FROM (unprocessed_to):", from, to, type);
                    emitter.update_edge_signal(from, to, type, SignalInfo{ mvreg.agent_id() });
                }
            } else {
                DSR_LOG_DEBUG("[JOIN_NODE] node deleted:", id);
                emitter.del_node_signal(id, SignalInfo{ mvreg.agent_id() });
                if (maybe_deleted_node.has_value()) {
                    Node tmp_node(*maybe_deleted_node);
                    emitter.deleted_node_signal(tmp_node, SignalInfo{ agent_id });
                    for (const auto &node: maybe_deleted_node->fano()) {
                        DSR_LOG_DEBUG("[JOIN_NODE] delete edge FROM:", node.second.read_reg().from(), node.second.read_reg().to(), node.second.read_reg().type());
                        emitter.del_edge_signal(node.second.read_reg().from(), node.second.read_reg().to(),
                                             node.second.read_reg().type(), SignalInfo{ mvreg.agent_id() });
                        Edge tmp_edge(node.second.read_reg());
                        emitter.deleted_edge_signal(tmp_edge, SignalInfo{ agent_id });
                    }
                }

                //TODO: deleted_edge_signal. update_maps_node_delete was called before so the maps are probably wrong...
                for (const auto &[from, type] : cache_map_to_edges.value()) {
                    DSR_LOG_DEBUG("[JOIN_NODE] delete edge TO:", from, id, type);
                    emitter.del_edge_signal(from, id, type, SignalInfo{ mvreg.agent_id() });
                    //emitter.deleted_edge_signal(Edge(node.second.read_reg())); TODO: fix this
                }

            }
        }

    }
    catch (const std::exception &e) {
        std::cout << "EXCEPTION: " << __FILE__ << " " << __FUNCTION__ << ":" << __LINE__ << " " << e.what()
                  << std::endl;
    }
}

bool DSRGraph::process_delta_edge(uint64_t from, uint64_t to, const std::string& type, mvreg<CRDTEdge> && delta)
{
    const bool d_empty = delta.empty();
    auto &node = nodes.at(from).read_reg();
    node.fano()[{to, type}].join(std::move(delta));
    if (d_empty or !node.fano().contains({to, type})) { //Remove
        node.fano().erase({to, type});
        update_maps_edge_delete(from, to, type);
        return false;
    } else { //Insert
        update_maps_edge_insert(from, to, type);
        return true;
    }
}


void DSRGraph::join_delta_edge(IDL::MvregEdge &&mvreg)
{
    try {
        bool signal = false, joined = false;
        auto from = mvreg.id();
        auto to = mvreg.to();
        std::string type = mvreg.type();
        DSR_LOG_DEBUG("[JOIN_EDGE] from:", from, "to:", to, "type:", type, "agent:", mvreg.agent_id());

        uint64_t timestamp = mvreg.timestamp();

        //Clean remaining delta edges.
        auto delete_unprocessed_deltas = [&](){
            //1. Delete all the delta attr for the edge.
            //2. Delete all unprocessed delta edges with the same key (from and to)
            //unprocessed_delta_edge_from.erase(from); // This should not be needed.
            unprocessed_delta_edge_att.erase(std::tuple{from, to, type});
            std::erase_if(unprocessed_delta_edge_to,
                          [&](auto &it){ return it.first == to && std::get<0>(it.second) == from && type == std::get<1>(it.second);});
            std::erase_if(unprocessed_delta_edge_to,
                          [&](auto &it){ return it.first == from && std::get<0>(it.second) == to && type == std::get<1>(it.second);});
        };

        //Consumes all delta attributes and deletes a possible previous delta from unprocessed map.
        auto consume_unprocessed_deltas = [&](){
            //1. Consume all the delta attributes for the edge key
            //2. Delete all unprocessed delta edges with the same key (from and to)
            auto att_key = std::tuple{from, to, type};
            decltype(unprocessed_delta_edge_att)::node_type node_handle_edge_att = unprocessed_delta_edge_att.extract(att_key);
            while (!node_handle_edge_att.empty())
            {
                auto &[att_name, delta, timestamp_delta_edge] = node_handle_edge_att.mapped();
                if (timestamp <  timestamp_delta_edge) {
                    process_delta_edge_attr(from, to, type, att_name,std::move(delta));
                }
                node_handle_edge_att = unprocessed_delta_edge_att.extract(att_key);
            }

            std::erase_if(unprocessed_delta_edge_to,
                          [&](auto &it){ return it.first == to && std::get<0>(it.second) == from && std::get<1>(it.second) == type; });
            std::erase_if(unprocessed_delta_edge_from,
                                            [&](auto &it){ return it.first == from && std::get<0>(it.second) == to && std::get<1>(it.second) == type; });
        };

        std::optional<Edge> deleted_edge;
        {
            auto crdt_delta = IDLEdge_to_CRDT(std::move(mvreg));
            std::unique_lock<std::shared_mutex> lock(_mutex);
            deleted_edge = get_edge_(from, to, type);
            //Check if the node where we are joining the edge exist.
            bool cfrom{nodes.contains(from)}, cto{nodes.contains(to)};
            bool dfrom{deleted.contains(from)}, dto{deleted.contains(to)};
            if (cfrom and cto) {
                joined = true;
                signal = process_delta_edge(from, to, type, std::move(crdt_delta));
                if (signal) {
                    consume_unprocessed_deltas();
                }
                else {
                    delete_unprocessed_deltas();
                }

            } else if (!dfrom and !dto) {
                //We should receive the node later. To avoid storing more than one element on the unprocessed cache we use the mvreg join.
                bool find = false;
                for (auto [begin, end] = unprocessed_delta_edge_from.equal_range(from); begin != end; ++begin) { //There should not be many elements in this iteration
                    if (std::get<0>(begin->second) == to && std::get<1>(begin->second) == type){
                        std::get<2>(begin->second).join(::mvreg<CRDTEdge>(crdt_delta));
                        DSR_LOG_DEBUG("[JOIN_EDGE] JOIN UNPROCESSED (from)", from, to, type);
                        find = true; break;
                    }
                }

                for (auto [begin, end] = unprocessed_delta_edge_from.equal_range(to); begin != end; ++begin) { //There should not be many elements in this iteration
                    if (std::get<0>(begin->second) == from && std::get<1>(begin->second) == type){
                        std::get<2>(begin->second).join(std::move(crdt_delta));
                        DSR_LOG_DEBUG("[JOIN_EDGE] JOIN UNPROCESSED (to)", from, to, type);
                        find = true; break;
                    }
                }

                if (!find) {
                    if (!cfrom) {
                        DSR_LOG_DEBUG("[JOIN_EDGE] INSERT UNPROCESSED, no from", from, "unprocessed_delta_edge_from");
                        unprocessed_delta_edge_from.emplace(from, std::tuple{to, type, crdt_delta, timestamp});
                    }
                    if (cfrom && !cto)
                    {
                        DSR_LOG_DEBUG("[JOIN_EDGE] INSERT UNPROCESSED, no to", to, "unprocessed_delta_edge_to");
                        unprocessed_delta_edge_to.emplace(to, std::tuple{from, type, std::move(crdt_delta), timestamp});
                    }
                } else {
                    DSR_LOG_WARNING("Unhandled case, should sync deleted_nodes set");
                }
            } else {
                DSR_LOG_DEBUG("[JOIN_EDGE] edge is part of deleted node, cleaning unprocessed");
                auto node_deleted = [&](uint64_t id){
                    unprocessed_delta_edge_from.erase(id);
                    unprocessed_delta_node_att.erase(id);
                    std::erase_if(unprocessed_delta_edge_to,
                                  [&](auto &it){ return std::get<0>(it.second) == id;});
                    std::erase_if(unprocessed_delta_edge_att,
                              [&](auto &it){ return std::get<0>(it.first) == id;});
                };
                if (dfrom) node_deleted(from);
                if (dto) node_deleted(to);
            }
        }

        if (joined) {
            if (signal) {
                DSR_LOG_DEBUG("[JOIN_EDGE] add edge:", from, to, type);
                emitter.update_edge_signal(from, to, type, SignalInfo{ mvreg.agent_id() });
            } else {
                DSR_LOG_DEBUG("[JOIN_EDGE] delete edge:", from, to, type);
                emitter.del_edge_signal(from, to, type, SignalInfo{ mvreg.agent_id() });
                if (deleted_edge.has_value()) {
                    emitter.deleted_edge_signal(*deleted_edge, SignalInfo{ agent_id });
                }
            }
        }

    } catch (const std::exception &e) {
        std::cout << "EXCEPTION: " << __FILE__ << " " << __FUNCTION__ << ":" << __LINE__ << " " << e.what()
                  << std::endl;
    }
}


void DSRGraph::process_delta_node_attr(uint64_t id, const std::string& att_name, mvreg<CRDTAttribute> && attr)
{
    const bool d_empty = attr.empty();
    auto &n = nodes.at(id).read_reg();
    n.attrs()[att_name].join(std::move(attr));
    //Check if we are inserting or deleting.
    if (d_empty or not n.attrs().contains(att_name)) { //Remove
        n.attrs().erase(att_name);
    }
}

std::optional<std::string> DSRGraph::join_delta_node_attr(IDL::MvregNodeAttr &&mvreg)
{

    try {
        bool joined = false;
        auto id = mvreg.id();
        std::string att_name = mvreg.attr_name();
        uint64_t timestamp = mvreg.timestamp();
        DSR_LOG_DEBUG("[JOIN_NODE_ATTR] node:", id, "attr:", att_name);
        {
            auto crdt_delta = IDLNodeAttr_to_CRDT(std::move(mvreg));
            std::unique_lock<std::shared_mutex> lock(_mutex);
            //Check if the node where we are joining the edge exist.
            if (nodes.contains(id)) {
                joined = true;
                process_delta_node_attr(id, att_name, std::move(crdt_delta));
                std::erase_if(unprocessed_delta_node_att,
                              [&](auto &it){ return it.first == id && std::get<0>(it.second) == att_name;});
            } else if (!deleted.contains(id)) {
                bool find = false;
                for (auto [begin, end] = unprocessed_delta_node_att.equal_range(id); begin != end; ++begin) { //There should not be many elements in this iteration
                    if (std::get<0>(begin->second) == att_name ){
                        std::get<1>(begin->second).join(std::move(crdt_delta));
                        find = true; break;
                    }
                }
                if (!find) {
                    DSR_LOG_DEBUG("[JOIN_NODE_ATTR] storing to unprocessed cache, node", id, att_name);
                    unprocessed_delta_node_att.emplace(id, std::tuple{att_name, std::move(crdt_delta), timestamp});
                }
            } else {
                unprocessed_delta_edge_from.erase(id);
                std::erase_if(unprocessed_delta_edge_to,
                              [&](auto &it){ return std::get<0>(it.second) == id;});
                unprocessed_delta_node_att.erase(id);
                std::erase_if(unprocessed_delta_edge_att,
                              [&](auto &it){ return std::get<0>(it.first) == id;});
            }
        }

        if (joined) {
            return att_name;
        }

    } catch (const std::exception &e) {
        std::cout << "EXCEPTION: " << __FILE__ << " " << __FUNCTION__ << ":" << __LINE__ << " " << e.what()
                  << std::endl;
    }

    return std::nullopt;
}

void DSRGraph::process_delta_edge_attr(uint64_t from, uint64_t to, const std::string& type, const std::string& att_name, mvreg<CRDTAttribute> && attr)
{
    const bool d_empty = attr.empty();
    auto &n = nodes.at(from).read_reg().fano().at({to, type}).read_reg();
    n.attrs()[att_name].join(std::move(attr));
    //Check if we are inserting or deleting.
    if (d_empty or !n.attrs().contains(att_name)) { //Remove
        n.attrs().erase(att_name);
    }
}


std::optional<std::string> DSRGraph::join_delta_edge_attr(IDL::MvregEdgeAttr &&mvreg)
{
    try {
        bool joined = false;
        auto from = mvreg.id();
        auto to = mvreg.to();
        std::string type = mvreg.type();
        std::string att_name = mvreg.attr_name();
        uint64_t timestamp = mvreg.timestamp();
        DSR_LOG_DEBUG("[JOIN_EDGE_ATTR] edge:", from, to, type, "attr:", att_name);

        {
            auto crdt_delta = IDLEdgeAttr_to_CRDT(std::move(mvreg));
            std::unique_lock<std::shared_mutex> lock(_mutex);
            //Check if the node where we are joining the edge exist.
            if (nodes.contains(from)  and nodes.at(from).read_reg().fano().contains({to, type}))
            {
                joined = true;
                process_delta_edge_attr(from, to, type, att_name, std::move(crdt_delta));
                std::erase_if(unprocessed_delta_edge_att,
                              [&](auto &it){ return it.first == std::tuple{from, to, type} && std::get<0>(it.second) == att_name;});
            } else if (!deleted.contains(from)){
                bool find = false;
                for (auto [begin, end] = unprocessed_delta_edge_att.equal_range(std::tuple{from, to, type}); begin != end; ++begin) { //There should not be many elements in this iteration
                    if (std::get<0>(begin->second) == att_name ){
                        std::get<1>(begin->second).join(std::move(crdt_delta));
                        find = true; break;
                    }
                }
                if (!find) {
                    DSR_LOG_DEBUG("[JOIN_EDGE_ATTR] storing to unprocessed cache, edge", from, to, type, att_name);
                    unprocessed_delta_edge_att.emplace(std::tuple{from, to, type},  std::tuple{att_name, std::move(crdt_delta), timestamp});
                }
            }  else { //If the node is deleted
                unprocessed_delta_edge_from.erase(from);
                std::erase_if(unprocessed_delta_edge_to,
                              [&](auto &it){ return std::get<0>(it.second) == from;});
                unprocessed_delta_node_att.erase(from);
                std::erase_if(unprocessed_delta_edge_att,
                              [&](auto &it){ return std::get<0>(it.first) == from;});
            }
        }

        if (joined) {
            return att_name;
        }

    } catch (const std::exception &e) {
        std::cout << "EXCEPTION: " << __FILE__ << " " << __FUNCTION__ << ":" << __LINE__ << " " << e.what()
                  << std::endl;
    }

    return std::nullopt;
}

void DSRGraph::join_full_graph(IDL::OrMap &&full_graph)
{
    std::vector<std::tuple<bool, uint64_t, std::string, std::optional<CRDTNode>>> updates;

    uint64_t id{0}, timestamp{0};
    uint32_t agent_id_ch{0};
    auto delete_unprocessed_deltas = [&](){
        unprocessed_delta_node_att.erase(id);
        decltype(unprocessed_delta_edge_from)::node_type node_handle = unprocessed_delta_edge_from.extract(id);
        while (!node_handle.empty())
        {
            unprocessed_delta_edge_att.erase(std::tuple{id, std::get<0>(node_handle.mapped()), std::get<1>(node_handle.mapped())});
            node_handle = unprocessed_delta_edge_from.extract(id);
        }
        std::erase_if(unprocessed_delta_edge_to,
                      [&](auto &it){ return std::get<0>(it.second) == id;});
    };

    auto consume_unprocessed_deltas = [&](){
        decltype(unprocessed_delta_node_att)::node_type node_handle_node_att = unprocessed_delta_node_att.extract(id);
        while (!node_handle_node_att.empty())
        {
            auto &[att_name, delta, timestamp_node_att] = node_handle_node_att.mapped();
            if (timestamp < timestamp_node_att) {
                process_delta_node_attr(id, att_name,std::move(delta));
            }
            node_handle_node_att = unprocessed_delta_node_att.extract(id);
        }


            decltype(unprocessed_delta_edge_from)::node_type node_handle_edge = unprocessed_delta_edge_from.extract(id);
            while (!node_handle_edge.empty()) {
                auto &[to, type, delta, timestamp_edge] = node_handle_edge.mapped();
                auto att_key = std::tuple{id, to, type};
                DSR_LOG_DEBUG("[JOIN_FULL] unprocessed_delta_edge_from", id, to, type, (timestamp < timestamp_edge));
                if (timestamp < timestamp_edge) {
                //TODO: este se debería hacer después de insertar el nodo?
                     process_delta_edge(id, to, type, std::move(delta));
                }
                if (nodes.contains(id) and nodes.at(id).read_reg().fano().contains({to, type})) {
                    decltype(unprocessed_delta_edge_att)::node_type node_handle_edge_att =  unprocessed_delta_edge_att.extract(att_key);
                    while (!node_handle_edge_att.empty()) {
                        auto &[att_name, delta, timestamp_edge_att] = node_handle_edge_att.mapped();
                        DSR_LOG_DEBUG("[JOIN_FULL] edge_att", id, to, type, att_name, (timestamp < timestamp_edge));
                        if (timestamp < timestamp_edge_att) {
                            process_delta_edge_attr(id, to, type, att_name, std::move(delta));
                        }
                        node_handle_edge_att = unprocessed_delta_edge_att.extract(att_key);
                    }
                }
                //TODO: Check
                std::erase_if(unprocessed_delta_edge_to,
                              [to = to, id = id, type = type](auto& it) { return it.first == to && std::get<0>(it.second) == id && std::get<1>(it.second) == type;});
                node_handle_edge = unprocessed_delta_edge_from.extract(id);
            }

            //TODO: Check
            node_handle_edge = unprocessed_delta_edge_to.extract(id);
            while (!node_handle_edge.empty()) {
                auto &[from, type, delta, timestamp_edge] = node_handle_edge.mapped();
                auto att_key = std::tuple{from, id, type};
                DSR_LOG_DEBUG("[JOIN_FULL] unprocessed_delta_edge_to", from, id, type, (timestamp < timestamp_edge));
                if (timestamp < timestamp_edge) {
                    process_delta_edge(from, id, type, std::move(delta));
                }
                if (nodes.contains(from) and nodes.at(from).read_reg().fano().contains({id, type})) {
                    decltype(unprocessed_delta_edge_att)::node_type node_handle_edge_att =  unprocessed_delta_edge_att.extract(att_key);
                    while (!node_handle_edge_att.empty()) {
                        auto &[att_name, delta, timestamp_edge_att] = node_handle_edge_att.mapped();
                        DSR_LOG_DEBUG("[JOIN_FULL] edge_att", from, id, type, att_name, (timestamp < timestamp_edge));
                        if (timestamp < timestamp_edge_att) {
                            process_delta_edge_attr(from, id, type, att_name, std::move(delta));
                        }
                        node_handle_edge_att = unprocessed_delta_edge_att.extract(att_key);
                    }
                }

                node_handle_edge = unprocessed_delta_edge_to.extract(id);
            }

    };

    {
        std::unique_lock<std::shared_mutex> lock(_mutex);

        for (auto &[k, val] : full_graph.m()) {
            auto mv = IDLNode_to_CRDT(std::move(val));
            bool mv_empty = mv.empty();
            agent_id_ch = val.agent_id();
            auto it = nodes.find(k);
            std::optional<CRDTNode> nd =
                    (it != nodes.end() and !it->second.empty()) ? std::make_optional(it->second.read_reg()) : std::nullopt;
            id = k;
            if (!deleted.contains(k)) {
                if (it == nodes.end()) {
                    it = nodes.emplace(k, mvreg<CRDTNode>{}).first;
                }
                it->second.join(std::move(mv));
                if (mv_empty or it->second.empty()) {
                    update_maps_node_delete(k, nd);
                    updates.emplace_back(false, k, "", std::nullopt);
                    delete_unprocessed_deltas();
                } else {
                    update_maps_node_insert(k, it->second.read_reg());
                    updates.emplace_back(true, k, it->second.read_reg().type(), nd);
                    consume_unprocessed_deltas();
                }
            }
        }

    }
    for (auto &[signal, id, type, nd] : updates)
        if (signal) {
            //check what change is joined
            if (!nd.has_value() || nd->attrs() != nodes[id].read_reg().attrs()) {
                emitter.update_node_signal(id, nodes[id].read_reg().type(), SignalInfo{ agent_id_ch });
            } else if (nd.value() != nodes[id].read_reg()) {
                auto iter = nodes[id].read_reg().fano();
                for (const auto &[k, v] : nd->fano()) {
                    if (!iter.contains(k)) {
                        emitter.del_edge_signal(id, k.first, k.second, SignalInfo{ agent_id_ch });
                        if (v.dk.ds.size() > 0) {
                            Edge tmp_edge(v.read_reg());
                            emitter.deleted_edge_signal(tmp_edge, SignalInfo{ agent_id });
                        }
                    }
                }
                for (const auto &[k, v] : iter) {
                    if (auto it = nd->fano().find(k); it == nd->fano().end() or it->second != v)
                            emitter.update_edge_signal(id, k.first, k.second, SignalInfo{ agent_id_ch });
                }
            }
        } else {
            emitter.del_node_signal(id, SignalInfo{ agent_id_ch });
            if (nd.has_value()) {
                Node tmp_node(*nd);
                emitter.deleted_node_signal(tmp_node, SignalInfo{ agent_id_ch });
            }
        }

}

std::pair<bool, bool> DSRGraph::start_fullgraph_request_thread()
{
    return fullgraph_request_thread();
}

void DSRGraph::start_fullgraph_server_thread()
{
    auto fullgraph_thread = std::thread(&DSRGraph::fullgraph_server_thread, this);
    if (fullgraph_thread.joinable()) fullgraph_thread.join();
}

void DSRGraph::start_subscription_threads()
{
    auto delta_node_thread = std::thread(&DSRGraph::node_subscription_thread, this);
    auto delta_edge_thread = std::thread(&DSRGraph::edge_subscription_thread, this);
    auto delta_node_attrs_thread = std::thread(&DSRGraph::node_attrs_subscription_thread, this);
    auto delta_edge_attrs_thread = std::thread(&DSRGraph::edge_attrs_subscription_thread, this);

    if (delta_node_thread.joinable()) delta_node_thread.join();
    if (delta_edge_thread.joinable()) delta_edge_thread.join();
    if (delta_node_attrs_thread.joinable()) delta_node_attrs_thread.join();
    if (delta_edge_attrs_thread.joinable()) delta_edge_attrs_thread.join();
}

std::map<uint64_t , IDL::MvregNode> DSRGraph::Map()
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::map<uint64_t, IDL::MvregNode> m;
    for (auto kv : nodes) {
        m.emplace(kv.first, CRDTNode_to_IDL(agent_id, kv.first, kv.second));
    }
    return m;
}

void print_sample_info(DSR::GraphSettings::LOGLEVEL log_level, const eprosima::fastdds::dds::SampleInfo& info) {

    double milliseconds_send = static_cast<double>(info.source_timestamp.seconds()) * 1000.0;
    milliseconds_send += static_cast<double>(info.source_timestamp.nanosec()) / 1000000.0;

    double milliseconds_recv = static_cast<double>(info.reception_timestamp.seconds()) * 1000.0;
    milliseconds_recv += static_cast<double>(info.reception_timestamp.nanosec()) / 1000000.0;

    auto t_since_epoch = std::chrono::system_clock::now().time_since_epoch();
    auto secs_t = duration_cast<std::chrono::seconds>(t_since_epoch);
    t_since_epoch -= secs_t;

    double now = static_cast<double>(secs_t.count()) * 1000.0 ;
    now  += static_cast<double>(duration_cast<std::chrono::nanoseconds>(t_since_epoch).count())  / 1000000.0;

    std::ostringstream oss;
    oss << "SampleInfo:" << std::endl;
    oss << "  ms diff reception: " << (milliseconds_recv - milliseconds_send) << "ms" << std::endl;
    oss << "  ms taken from dds: " << (now - milliseconds_send) << "ms" << std::endl;
    oss << "  Valid Data: " << (info.valid_data ? "true" : "false") << std::endl;
    qDebug() << QString::fromStdString(oss.str());
}


void DSRGraph::node_subscription_thread()
{
    auto name = __FUNCTION__;
    auto lambda_general_topic = [&, name = name, showReceived = log_level]
    (eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *graph)
    {
        try {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo m_info;
                IDL::MvregNode sample;
                if (reader->take_next_sample(&sample, &m_info) == 0) {
                    if (showReceived  == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(showReceived, m_info);
                    if (m_info.valid_data) {
                        if (sample.agent_id() != agent_id) {
                            if (sample.id() == CLEAR_DELETED_SIGNAL) {
                                std::unique_lock<std::shared_mutex> lock(_mutex);
                                std::unique_lock<std::shared_mutex> lck_cache(_mutex_cache_maps);
                                deleted.clear();
                                continue;
                            }
                            if (showReceived  == GraphSettings::LOGLEVEL::DEBUGL) {
                                qDebug() << name << " Received:" << std::to_string(sample.id()).c_str() << " node from: "
                                        << m_info.sample_identity.writer_guid().entityId.value;
                            }
                            tp.spawn_task(&DSRGraph::join_delta_node, this, std::move(sample));
                        }
                    }
                } else {
                    break;
                }
            }
        }
        catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
    };
    dsrpub_call_node = NewMessageFunctor(this, lambda_general_topic);
    auto [res, sub, reader] = dsrsub_node.init(dsrparticipant.getParticipant(), dsrparticipant.getNodeTopic(), dsrparticipant.get_domain_id(), dsrpub_call_node, mtx_entity_creation);
    dsrparticipant.add_subscriber(dsrparticipant.getNodeTopic()->get_name(), {sub, reader});
}

void DSRGraph::edge_subscription_thread()
{
    auto name = __FUNCTION__;
    auto lambda_general_topic = [&, name = name, showReceived = log_level]
    (eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *graph)
    {
        try {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo m_info;
                IDL::MvregEdge sample;
                if (reader->take_next_sample(&sample, &m_info) == 0) {
                    if (showReceived  == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(showReceived, m_info);
                    if (m_info.valid_data) {
                        if (sample.agent_id() != agent_id) {
                            if (showReceived  == GraphSettings::LOGLEVEL::DEBUGL) {
                                qDebug() << name << " Received:" << std::to_string(sample.id()).c_str() << " node from: "
                                        << m_info.sample_identity.writer_guid().entityId.value;
                            }
                            tp.spawn_task(&DSRGraph::join_delta_edge, this, std::move(sample));
                        }
                    }
                } else {
                    break;
                }
            }
        }
        catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
    };
    dsrpub_call_edge = NewMessageFunctor(this, lambda_general_topic);
    auto [res, sub, reader]  = dsrsub_edge.init(dsrparticipant.getParticipant(), dsrparticipant.getEdgeTopic(), dsrparticipant.get_domain_id(), dsrpub_call_edge, mtx_entity_creation);
    dsrparticipant.add_subscriber(dsrparticipant.getEdgeTopic()->get_name(), {sub, reader});

}

void DSRGraph::edge_attrs_subscription_thread()
{
    auto name = __FUNCTION__;
    auto lambda_general_topic = [&, name = name, showReceived = log_level]
    (eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *graph)
    {
        try {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo m_info;
                IDL::MvregEdgeAttrVec samples;
                if (reader->take_next_sample(&samples, &m_info) == 0) {
                    if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(showReceived, m_info);
                    if (m_info.valid_data) {
                        if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) {
                            qDebug() << name << " Received:" << samples.vec().size() << " edge attr from: "
                                    << m_info.sample_identity.writer_guid().entityId.value;
                        }
                        if (!samples.vec().empty() and samples.vec().at(0).agent_id() != agent_id)
                        {
                            tp_delta_attr.spawn_task([this, samples = std::move(samples)]() mutable {
                                if (samples.vec().empty()) return;

                                auto from = samples.vec().at(0).from();
                                auto to = samples.vec().at(0).to();
                                auto type = samples.vec().at(0).type();

                                std::vector<std::future<std::optional<std::string>>> futures;

                                for (auto &&sample: samples.vec()) {
                                    if (!ignored_attributes.contains(sample.attr_name())) {
                                        futures.emplace_back(tp.spawn_task_waitable([this, sample = std::move(sample)]() mutable {
                                                return join_delta_edge_attr(std::move(sample));
                                        }));
                                    }
                                }

                                std::vector<std::string> sig (futures.size());
                                for (auto &f: futures)
                                {
                                    auto opt_str = f.get();
                                    if (opt_str.has_value())
                                        sig.emplace_back(std::move(opt_str.value()));
                                }


                                emitter.update_edge_attr_signal(from, to, type, sig, SignalInfo{samples.vec().at(0).agent_id()});
                                emitter.update_edge_signal(from, to, type, SignalInfo{samples.vec().at(0).agent_id()});

                            });
                        }
                    }
                } else {
                    break;
                }
            }
        }
        catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }

    };
    dsrpub_call_edge_attrs = NewMessageFunctor(this, lambda_general_topic);
    auto [res, sub, reader] = dsrsub_edge_attrs.init(dsrparticipant.getParticipant(), dsrparticipant.getAttEdgeTopic(), dsrparticipant.get_domain_id(),
                           dsrpub_call_edge_attrs, mtx_entity_creation);
    dsrparticipant.add_subscriber(dsrparticipant.getAttEdgeTopic()->get_name(), {sub, reader});
    //dsrsub_edge_attrs_stream.init(dsrparticipant.getParticipant(), "DSR_EDGE_ATTRS_STREAM", dsrparticipant.getEdgeAttrTopicName(),
    //                       dsrpub_call_edge_attrs, true);
}

void DSRGraph::node_attrs_subscription_thread()
{
    auto name = __FUNCTION__;
    auto lambda_general_topic = [this, name = name, showReceived = log_level]
    (eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *graph)
    {
        try {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo m_info;
                IDL::MvregNodeAttrVec samples;
                if (reader->take_next_sample(&samples, &m_info) == 0) {
                    if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(showReceived, m_info);
                    if (m_info.valid_data) {
                        if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) {
                            qDebug() << name << " Received:" << samples.vec().size() << " node attrs from: "
                                    << m_info.sample_identity.writer_guid().entityId.value;
                        }
                        if (!samples.vec().empty() and samples.vec().at(0).agent_id() != agent_id) {
                            tp_delta_attr.spawn_task([this, samples = std::move(samples)]() mutable {

                                if (samples.vec().empty()) return;

                                auto id = samples.vec().at(0).id();
                                std::string type;
                                {
                                    std::shared_lock<std::shared_mutex> lock(_mutex);
                                    if (auto itn = nodes.find(id); itn != nodes.end())  type = itn->second.read_reg().type() ;
                                }
                                std::vector<std::future<std::optional<std::string>>> futures;
                                for (auto &&s: samples.vec()) {
                                    if (!ignored_attributes.contains(s.attr_name())) {
                                        futures.emplace_back(tp.spawn_task_waitable([this, samp{std::move(s)}]() mutable {
                                            auto f = join_delta_node_attr(std::move(samp));
                                            return f;
                                        }));

                                    }
                                }

                                std::vector<std::string> sig (futures.size());
                                for (auto &f: futures)
                                {
                                    auto opt_str = f.get();
                                    if (opt_str.has_value())
                                        sig.emplace_back(std::move(opt_str.value()));
                                }

                                emitter.update_node_attr_signal(id, sig, SignalInfo{samples.vec().at(0).agent_id()});
                                emitter.update_node_signal(id, type, SignalInfo{samples.vec().at(0).agent_id()});
                            });
                        }
                    }
                } else {
                    break;
                }
            }
        }
        catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }

    };
    dsrpub_call_node_attrs = NewMessageFunctor(this, lambda_general_topic);
    auto [res, sub, reader] = dsrsub_node_attrs.init(dsrparticipant.getParticipant(), dsrparticipant.getAttNodeTopic(), dsrparticipant.get_domain_id(),
                           dsrpub_call_node_attrs, mtx_entity_creation);
    dsrparticipant.add_subscriber(dsrparticipant.getAttNodeTopic()->get_name(), {sub, reader});

}

void DSRGraph::fullgraph_server_thread()
{
    auto lambda_graph_request = [&](eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *graph)
    {
        while (true)
        {
            eprosima::fastdds::dds::SampleInfo m_info;
            IDL::GraphRequest sample;
            if (reader->take_next_sample(&sample, &m_info) == 0) {
                if (log_level == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(log_level, m_info);
                if (m_info.valid_data) {
                    {
                        std::unique_lock<std::mutex> lck(participant_set_mutex);
                        if (auto [it, ok] = participant_set.emplace(sample.from(), true);
                            it->second and !ok)
                        {
                            if (it->second) {
                                lck.unlock();
                                IDL::OrMap mp;
                                mp.id(-1);
                                mp.to_id(sample.id());
                                dsrpub_request_answer.write(&mp);
                                continue;
                            } else {}
                        } else {
                            it->second = true;
                            lck.unlock();
                        }
                    }
                    if (static_cast<uint32_t>(sample.id()) != agent_id ) {

                        qDebug() << " Received Full Graph request: from "
                                << m_info.sample_identity.writer_guid().entityId.value;
                        IDL::OrMap mp;
                        mp.id(graph->get_agent_id());
                        mp.m(graph->Map());
                        dsrpub_request_answer.write(&mp);

                        qDebug() << "Full graph written";

                    }
                }
            } else {
                break;
            }
        }
    };
    dsrpub_graph_request_call = NewMessageFunctor(this, lambda_graph_request);
    auto [res, sub, reader] = dsrsub_graph_request.init(dsrparticipant.getParticipant(), dsrparticipant.getGraphRequestTopic(), dsrparticipant.get_domain_id(),
                              dsrpub_graph_request_call, mtx_entity_creation);
    dsrparticipant.add_subscriber(dsrparticipant.getGraphRequestTopic()->get_name(), {sub, reader});

}

std::pair<bool, bool> DSRGraph::fullgraph_request_thread()
{
    bool sync = false;
    bool repeated = false;
    auto lambda_request_answer = [&](eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *graph)
    {
        while (true)
        {
            eprosima::fastdds::dds::SampleInfo m_info;
            IDL::OrMap sample;
            if (reader->take_next_sample(&sample, &m_info) == 0) {
                if (log_level == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(log_level, m_info);
                if (m_info.valid_data) {
                    if (sample.id() != graph->get_agent_id()) {
                        if (sample.id() != static_cast<uint32_t>(-1)) {
                            qDebug() << " Received Full Graph from " << m_info.sample_identity.writer_guid().entityId.value
                                    << " whith "
                                    << sample.m().size() << " elements";
                            tp.spawn_task(&DSRGraph::join_full_graph, this, std::move(sample));
                            qDebug() << "Synchronized.";
                            sync = true;
                            break;
                        }
                        else if (!sync && sample.to_id() == agent_id)
                        {
                            repeated = true;
                        }
                    }
                }
            } else {
                break;
            }
        }
    };

    dsrpub_request_answer_call = NewMessageFunctor(this, lambda_request_answer);
    auto [res, sub, reader] = dsrsub_request_answer.init(dsrparticipant.getParticipant(), dsrparticipant.getGraphTopic(), dsrparticipant.get_domain_id(),
                               dsrpub_request_answer_call, mtx_entity_creation);
    dsrparticipant.add_subscriber(dsrparticipant.getGraphTopic()->get_name(), {sub, reader});

    std::this_thread::sleep_for(300ms);   // NEEDED ?

    qDebug() << " Requesting the complete graph ";

    IDL::GraphRequest gr;
    gr.from( dsrparticipant.getParticipant()->get_qos().name().to_string());
    gr.id(static_cast<int32_t>(agent_id));
    dsrpub_graph_request.write(&gr);


    bool timeout = false;
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    while (!sync and !timeout and !repeated) {
        std::this_thread::sleep_for(1000ms);
        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        timeout = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() > TIMEOUT * 3;
        qInfo() << " Waiting for the graph ... seconds to timeout ["
                << std::ceil(std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() / 10) / 100.0
                << "/" << TIMEOUT / 1000 * 3 << "] ";
        dsrpub_graph_request.write(&gr);
    }

    dsrparticipant.delete_publisher(dsrparticipant.getGraphRequestTopic()->get_name());
    dsrparticipant.delete_subscriber(dsrparticipant.getGraphTopic()->get_name());

    return { sync, repeated };
}


//////////////////////////////////////////////////
///// PRIVATE COPY
/////////////////////////////////////////////////

DSRGraph::DSRGraph(const DSRGraph &G) : agent_id(G.agent_id), copy(true), tp(1), generator(G.agent_id)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::shared_lock<std::shared_mutex> lock_cache(_mutex_cache_maps);
    nodes = G.nodes;
    utils = std::make_unique<Utilities>(this);
    id_map = G.id_map;
    deleted = G.deleted;
    name_map = G.name_map;
    edges = G.edges;
    edgeType = G.edgeType;
    nodeType = G.nodeType;
    same_host = G.same_host;
}

std::unique_ptr<DSRGraph> DSRGraph::G_copy()
{
    return std::unique_ptr<DSRGraph>(new DSRGraph(*this));
}

bool DSRGraph::is_copy() const
{
    return copy;
}
