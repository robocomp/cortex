//
// Created by crivac on 5/02/19.
//

#include "dsr/api/dsr_graph_settings.h"
#include "dsr/api/dsr_crdt_sync_engine.h"
#include "dsr/api/dsr_lww_sync_engine.h"
#include "dsr/core/types/internal_types.h"
#include "dsr/core/types/translator.h"
#include "dsr/core/profiling.h"
#include "dsr/api/dsr_signal_emitter.h"
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

namespace {
DSR::SyncEnginePtr make_sync_engine(DSR::DSRGraph& graph, DSR::SyncMode mode)
{
    if (mode == DSR::SyncMode::LWW) {
        return std::make_unique<DSR::LWWSyncEngine>(graph);
    }
    return std::make_unique<DSR::CRDTSyncEngine>(graph);
}

bool protocol_version_matches(
    DSR::GraphSettings::LOGLEVEL log_level,
    const char* channel,
    uint32_t remote_version)
{
    if (remote_version == DSR::DSR_PROTOCOL_VERSION) {
        return true;
    }

    DSR_LOG_ERROR(
        "[PROTOCOL] incompatible", channel,
        "remote:", remote_version,
        "local:", DSR::DSR_PROTOCOL_VERSION);
    return false;
}

const char* sync_mode_name(DSR::SyncMode mode)
{
    switch (mode) {
        case DSR::SyncMode::CRDT: return "CRDT";
        case DSR::SyncMode::LWW:  return "LWW";
    }
    return "UNKNOWN";
}

bool network_compatibility_or_fatal(
    const char* channel,
    uint32_t remote_version,
    uint8_t remote_sync_mode,
    DSR::SyncMode local_sync_mode)
{
    if (remote_version != DSR::DSR_PROTOCOL_VERSION) {
        const auto message = std::string("DSRGraph aborting: incompatible protocol on ") + channel +
                             " remote=" + std::to_string(remote_version) +
                             " local=" + std::to_string(DSR::DSR_PROTOCOL_VERSION);
        qFatal("%s", message.c_str());
        return false;
    }

    const auto local_wire = DSR::sync_mode_wire_value(local_sync_mode);
    if (remote_sync_mode != local_wire) {
        const auto message = std::string("DSRGraph aborting: incompatible sync mode on ") + channel +
                             " remote=" + std::to_string(remote_sync_mode) +
                             " (" + sync_mode_name(static_cast<DSR::SyncMode>(remote_sync_mode)) + ")" +
                             " local=" + std::to_string(local_wire) +
                             " (" + sync_mode_name(local_sync_mode) + ")";
        qFatal("%s", message.c_str());
        return false;
    }

    return true;
}
}

void print_sample_info(DSR::GraphSettings::LOGLEVEL log_level, const eprosima::fastdds::dds::SampleInfo& info);

/////////////////////////////////////////////////
///// PUBLIC METHODS
/////////////////////////////////////////////////

DSRGraph::DSRGraph(GraphSettings settings) :
        agent_id(settings.agent_id),
        agent_name(std::move(settings.graph_name)),
        copy(false),
        sync_mode(settings.sync_mode),
        tp(settings.theradpool_threads, "join"),
        tp_delta_attr(settings.attribute_threadpool_threads, "attr"),
        same_host(settings.same_host),
        generator(settings.agent_id),
        log_level(settings.log_level),
        engine_(make_sync_engine(*this, settings.sync_mode))
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::DSRGraph");

    qDebug() << "Agent name: " << QString::fromStdString(agent_name);
    {
        CORTEX_PROFILE_ZONE_N("DSRGraph::DSRGraph setup utils/signals");
        utils =  std::make_unique<Utilities>(this);
        if (settings.signal_mode == SignalMode::QT) {
            set_qt_signals();
        } else {
            set_queued_signals();
        }
    }
    if (sync_mode == SyncMode::LWW && !same_host)
    {
        qFatal("DSRGraph aborting: SyncMode::LWW currently supports same_host only");
    }
    // RTPS Create participant
    auto participant_init_result = [&]() {
        CORTEX_PROFILE_ZONE_N("DSRGraph::DSRGraph init participant");
        return dsrparticipant.init(agent_id, agent_name, settings.same_host,
                                   ParticipantChangeFunctor(this, [&](DSR::DSRGraph *graph,
                                           eprosima::fastdds::rtps::ParticipantDiscoveryStatus status,
                                           const eprosima::fastdds::rtps::ParticipantBuiltinTopicData& info)
                                           {
                                               CORTEX_PROFILE_ZONE_N("DSRGraph::participant discovery callback");
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
                                                   // Participant name is "Participant_<id> ( <agent_name> )" which doesn't
                                                   // match the DSR node name "<agent_name> <id>". Find the agent node by
                                                   // its agent_id attribute instead.
                                                   const std::string pname = info.participant_name.to_string();
                                                   const std::string prefix = "Participant_";
                                                   bool deleted = false;
                                                   if (pname.size() > prefix.size() && pname.substr(0, prefix.size()) == prefix) {
                                                       try {
                                                           uint32_t peer_id = static_cast<uint32_t>(std::stoul(pname.substr(prefix.size())));
                                                           for (auto& node : graph->get_nodes_by_type("agent")) {
                                                               auto attr = graph->get_attrib_by_name<agent_id_att>(node);
                                                               if (attr.has_value() && attr.value() == peer_id) {
                                                                   graph->delete_node(node.id());
                                                                   deleted = true;
                                                                   break;
                                                               }
                                                           }
                                                       } catch (...) {}
                                                   }
                                                   if (!deleted)
                                                       graph->delete_node(pname);
                                               }
                                           }), settings.domain_id, sync_mode_wire_value(sync_mode));
    }();
    auto[suc, participant_handle] = std::move(participant_init_result);


    // RTPS Initialize publisher with general topic
    auto [res, pub, writer, res2, pub2, writer2, res3, pub3, writer3, res4, pub4, writer4, res5, pub5, writer5, res6, pub6, writer6] = [&]() {
        CORTEX_PROFILE_ZONE_N("DSRGraph::DSRGraph init publishers");
        auto [r1, p1, w1] = dsrpub_node.init(participant_handle, dsrparticipant.getNodeTopic(), dsrparticipant.get_domain_id());
        auto [r2, p2, w2] = dsrpub_node_attrs.init(participant_handle, dsrparticipant.getAttNodeTopic(), dsrparticipant.get_domain_id());
        auto [r3, p3, w3] = dsrpub_edge.init(participant_handle, dsrparticipant.getEdgeTopic(), dsrparticipant.get_domain_id());
        auto [r4, p4, w4] = dsrpub_edge_attrs.init(participant_handle, dsrparticipant.getAttEdgeTopic(), dsrparticipant.get_domain_id());
        auto [r5, p5, w5] = dsrpub_graph_request.init(participant_handle, dsrparticipant.getGraphRequestTopic(), dsrparticipant.get_domain_id());
        auto [r6, p6, w6] = dsrpub_request_answer.init(participant_handle, dsrparticipant.getGraphTopic(), dsrparticipant.get_domain_id());
        return std::tuple{r1, p1, w1, r2, p2, w2, r3, p3, w3, r4, p4, w4, r5, p5, w5, r6, p6, w6};
    }();

    {
        CORTEX_PROFILE_ZONE_N("DSRGraph::DSRGraph register publishers");
        dsrparticipant.add_publisher(dsrparticipant.getNodeTopic()->get_name(), {pub, writer});
        dsrparticipant.add_publisher(dsrparticipant.getAttNodeTopic()->get_name(), {pub2, writer2});
        dsrparticipant.add_publisher(dsrparticipant.getEdgeTopic()->get_name(), {pub3, writer3});
        dsrparticipant.add_publisher(dsrparticipant.getAttEdgeTopic()->get_name(), {pub4, writer4});
        dsrparticipant.add_publisher(dsrparticipant.getGraphRequestTopic()->get_name(), {pub5, writer5});
        dsrparticipant.add_publisher(dsrparticipant.getGraphTopic()->get_name(), {pub6, writer6});
    }

    // RTPS Initialize comms threads
    if (!settings.input_file.empty())
    {
        try
        {
            CORTEX_PROFILE_ZONE_N("DSRGraph::DSRGraph load input graph");
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
        {
            CORTEX_PROFILE_ZONE_N("DSRGraph::DSRGraph bootstrap subscriptions");
            start_subscription_threads();     // regular subscription to deltas
        }
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
    : DSR::DSRGraph(GraphSettings {id, 5, 1, name, dsr_input_file, "", all_same_host, GraphSettings::LOGLEVEL::INFOL, domain_id, mode, SyncMode::CRDT})
{}


DSRGraph::~DSRGraph()
{
    qDebug() << "Removing DSRGraph";
    dsrparticipant.remove_participant_and_entities();
    if (!copy) {
        qDebug() << "Removing rtps participant";
    }
}

void DSRGraph::reset()
{
    dsrparticipant.remove_participant_and_entities();
    engine_ = make_sync_engine(*this, sync_mode);
    deleted.clear();
    name_map.clear();
    id_map.clear();
    edges.clear();
    edgeType.clear();
    nodeType.clear();
    to_edges.clear();
}

CRDTSyncEngine& DSRGraph::crdt_engine()
{
    return static_cast<CRDTSyncEngine&>(*engine_);
}

const CRDTSyncEngine& DSRGraph::crdt_engine() const
{
    return static_cast<const CRDTSyncEngine&>(*engine_);
}

LWWSyncEngine& DSRGraph::lww_engine()
{
    return static_cast<LWWSyncEngine&>(*engine_);
}

const LWWSyncEngine& DSRGraph::lww_engine() const
{
    return static_cast<const LWWSyncEngine&>(*engine_);
}

void DSRGraph::publish_node_message(const NodeDeltaMessage& message)
{
    std::visit([this](const auto& payload) {
        dsrpub_node.write(payload);
    }, message);
}

void DSRGraph::publish_node_attr_batch(const NodeAttrDeltaBatchMessage& message)
{
    std::visit([this](const auto& payload) {
        dsrpub_node_attrs.write(payload);
    }, message);
}

void DSRGraph::publish_edge_message(const EdgeDeltaMessage& message)
{
    std::visit([this](const auto& payload) {
        dsrpub_edge.write(payload);
    }, message);
}

void DSRGraph::publish_edge_attr_batch(const EdgeAttrDeltaBatchMessage& message)
{
    std::visit([this](const auto& payload) {
        dsrpub_edge_attrs.write(payload);
    }, message);
}

void DSRGraph::publish_full_graph_message(FullGraphMessage&& message, int32_t sender_id)
{
    std::visit([this, sender_id](auto&& payload) {
        payload.id = sender_id;
        dsrpub_request_answer.write(payload);
    }, std::move(message));
}

const DSRGraph::TransportProfile& DSRGraph::transport_profile() const
{
    static const TransportProfile profiles[] = {
        {
            &DSRGraph::make_crdt_node_subscription_functor,
            &DSRGraph::make_crdt_edge_subscription_functor,
            &DSRGraph::make_crdt_edge_attrs_subscription_functor,
            &DSRGraph::make_crdt_node_attrs_subscription_functor,
            &DSRGraph::make_crdt_fullgraph_request_functor,
        },
        {
            &DSRGraph::make_lww_node_subscription_functor,
            &DSRGraph::make_lww_edge_subscription_functor,
            &DSRGraph::make_lww_edge_attrs_subscription_functor,
            &DSRGraph::make_lww_node_attrs_subscription_functor,
            &DSRGraph::make_lww_fullgraph_request_functor,
        },
    };
    return profiles[sync_mode_wire_value(sync_mode)];
}

DSRGraph::NewMessageFunctor DSRGraph::make_crdt_node_subscription_functor()
{
    auto name = "node_subscription_thread";
    return NewMessageFunctor(this, [this, name, showReceived = log_level]
    (eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("node_sub"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::node_subscription_thread callback");
        try {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo m_info;
                DSR::MvregNodeMsg sample;
                if (reader->take_next_sample(&sample, &m_info) != 0) {
                    break;
                }
                if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(showReceived, m_info);
                if (m_info.valid_data && sample.agent_id != agent_id) {
                    if (!network_compatibility_or_fatal("DSR_NODE", sample.protocol_version, sample.sync_mode, sync_mode)) {
                        continue;
                    }
                    if (sample.id == CLEAR_DELETED_SIGNAL) {
                        std::unique_lock<std::shared_mutex> lock(_mutex);
                        std::unique_lock<std::shared_mutex> lck_cache(_mutex_cache_maps);
                        deleted.clear();
                        continue;
                    }
                    if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) {
                        qDebug() << name << " Received:" << std::to_string(sample.id).c_str() << " node from: "
                                 << m_info.sample_identity.writer_guid().entityId.value;
                    }
                    tp.spawn_task([this, sample = std::move(sample)]() mutable {
                        engine_->apply_remote_node_delta(NodeDeltaMessage{std::move(sample)});
                    });
                }
            }
        }
        catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
    });
}

DSRGraph::NewMessageFunctor DSRGraph::make_lww_node_subscription_functor()
{
    return NewMessageFunctor(this, [this, showReceived = log_level]
    (eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("node_sub"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::node_subscription_thread callback");
        try {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo m_info;
                DSR::LWWNodeMsg sample;
                if (reader->take_next_sample(&sample, &m_info) != 0) {
                    break;
                }
                if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(showReceived, m_info);
                if (m_info.valid_data && sample.agent_id != agent_id) {
                    if (!network_compatibility_or_fatal("LWW_NODE", sample.protocol_version, sample.sync_mode, sync_mode)) {
                        continue;
                    }
                    tp.spawn_task([this, sample = std::move(sample)]() mutable {
                        engine_->apply_remote_node_delta(NodeDeltaMessage{std::move(sample)});
                    });
                }
            }
        }
        catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
    });
}

DSRGraph::NewMessageFunctor DSRGraph::make_crdt_edge_subscription_functor()
{
    auto name = "edge_subscription_thread";
    return NewMessageFunctor(this, [this, name, showReceived = log_level]
    (eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("edge_sub"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::edge_subscription_thread callback");
        try {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo m_info;
                DSR::MvregEdgeMsg sample;
                if (reader->take_next_sample(&sample, &m_info) != 0) {
                    break;
                }
                if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(showReceived, m_info);
                if (m_info.valid_data && sample.agent_id != agent_id) {
                    if (!network_compatibility_or_fatal("DSR_EDGE", sample.protocol_version, sample.sync_mode, sync_mode)) {
                        continue;
                    }
                    if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) {
                        qDebug() << name << " Received:" << std::to_string(sample.id).c_str() << " node from: "
                                 << m_info.sample_identity.writer_guid().entityId.value;
                    }
                    tp.spawn_task([this, sample = std::move(sample)]() mutable {
                        engine_->apply_remote_edge_delta(EdgeDeltaMessage{std::move(sample)});
                    });
                }
            }
        }
        catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
    });
}

DSRGraph::NewMessageFunctor DSRGraph::make_lww_edge_subscription_functor()
{
    return NewMessageFunctor(this, [this, showReceived = log_level]
    (eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("edge_sub"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::edge_subscription_thread callback");
        try {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo m_info;
                DSR::LWWEdgeMsg sample;
                if (reader->take_next_sample(&sample, &m_info) != 0) {
                    break;
                }
                if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(showReceived, m_info);
                if (m_info.valid_data && sample.agent_id != agent_id) {
                    if (!network_compatibility_or_fatal("LWW_EDGE", sample.protocol_version, sample.sync_mode, sync_mode)) {
                        continue;
                    }
                    tp.spawn_task([this, sample = std::move(sample)]() mutable {
                        engine_->apply_remote_edge_delta(EdgeDeltaMessage{std::move(sample)});
                    });
                }
            }
        }
        catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
    });
}

DSRGraph::NewMessageFunctor DSRGraph::make_crdt_edge_attrs_subscription_functor()
{
    auto name = "edge_attrs_subscription_thread";
    return NewMessageFunctor(this, [this, name, showReceived = log_level]
    (eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("edge_attr_sub"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::edge_attrs_subscription_thread callback");
        try {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo m_info;
                DSR::MvregEdgeAttrVec samples;
                if (reader->take_next_sample(&samples, &m_info) != 0) {
                    break;
                }
                if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(showReceived, m_info);
                if (m_info.valid_data) {
                    if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) {
                        qDebug() << name << " Received:" << samples.vec.size() << " edge attr from: "
                                 << m_info.sample_identity.writer_guid().entityId.value;
                    }
                    if (!samples.vec.empty() and samples.vec.at(0).agent_id != agent_id)
                    {
                        const auto& first = samples.vec.front();
                        if (!network_compatibility_or_fatal("DSR_EDGE_ATTS", first.protocol_version, first.sync_mode, sync_mode)) {
                            continue;
                        }
                        tp_delta_attr.spawn_task([this, samples = std::move(samples)]() mutable {
                            CORTEX_PROFILE_ZONE_N("DSRGraph::edge_attrs_subscription_thread apply batch");
                            engine_->apply_remote_edge_attr_batch(EdgeAttrDeltaBatchMessage{std::move(samples)});
                        });
                    }
                }
            }
        }
        catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
    });
}

DSRGraph::NewMessageFunctor DSRGraph::make_lww_edge_attrs_subscription_functor()
{
    return NewMessageFunctor(this, [this]
    (eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("edge_attr_sub"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::edge_attrs_subscription_thread callback");
        try {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo m_info;
                DSR::LWWEdgeAttrVec samples;
                if (reader->take_next_sample(&samples, &m_info) != 0) {
                    break;
                }
                if (m_info.valid_data && !samples.vec.empty() && samples.vec.at(0).agent_id != agent_id) {
                    const auto& first = samples.vec.front();
                    if (!network_compatibility_or_fatal("LWW_EDGE_ATTS", first.protocol_version, first.sync_mode, sync_mode)) {
                        continue;
                    }
                    tp_delta_attr.spawn_task([this, samples = std::move(samples)]() mutable {
                        engine_->apply_remote_edge_attr_batch(EdgeAttrDeltaBatchMessage{std::move(samples)});
                    });
                }
            }
        }
        catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
    });
}

DSRGraph::NewMessageFunctor DSRGraph::make_crdt_node_attrs_subscription_functor()
{
    auto name = "node_attrs_subscription_thread";
    return NewMessageFunctor(this, [this, name, showReceived = log_level]
    (eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("node_attr_sub"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::node_attrs_subscription_thread callback");
        try {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo m_info;
                DSR::MvregNodeAttrVec samples;
                if (reader->take_next_sample(&samples, &m_info) != 0) {
                    break;
                }
                if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(showReceived, m_info);
                if (m_info.valid_data) {
                    if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) {
                        qDebug() << name << " Received:" << samples.vec.size() << " node attrs from: "
                                 << m_info.sample_identity.writer_guid().entityId.value;
                    }
                    if (!samples.vec.empty() and samples.vec.at(0).agent_id != agent_id) {
                        const auto& first = samples.vec.front();
                        if (!network_compatibility_or_fatal("DSR_NODE_ATTS", first.protocol_version, first.sync_mode, sync_mode)) {
                            continue;
                        }
                        tp_delta_attr.spawn_task([this, samples = std::move(samples)]() mutable {
                            CORTEX_PROFILE_ZONE_N("DSRGraph::node_attrs_subscription_thread apply batch");
                            engine_->apply_remote_node_attr_batch(NodeAttrDeltaBatchMessage{std::move(samples)});
                        });
                    }
                }
            }
        }
        catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
    });
}

DSRGraph::NewMessageFunctor DSRGraph::make_lww_node_attrs_subscription_functor()
{
    return NewMessageFunctor(this, [this]
    (eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("node_attr_sub"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::node_attrs_subscription_thread callback");
        try {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo m_info;
                DSR::LWWNodeAttrVec samples;
                if (reader->take_next_sample(&samples, &m_info) != 0) {
                    break;
                }
                if (m_info.valid_data && !samples.vec.empty() && samples.vec.at(0).agent_id != agent_id) {
                    const auto& first = samples.vec.front();
                    if (!network_compatibility_or_fatal("LWW_NODE_ATTS", first.protocol_version, first.sync_mode, sync_mode)) {
                        continue;
                    }
                    tp_delta_attr.spawn_task([this, samples = std::move(samples)]() mutable {
                        engine_->apply_remote_node_attr_batch(NodeAttrDeltaBatchMessage{std::move(samples)});
                    });
                }
            }
        }
        catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
    });
}

DSRGraph::NewMessageFunctor DSRGraph::make_crdt_fullgraph_request_functor(std::atomic<bool>& sync, std::atomic<bool>& repeated)
{
    return NewMessageFunctor(this, [this, &sync, &repeated](eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *graph)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("fg_request"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::fullgraph_request_thread callback");
        while (true)
        {
            eprosima::fastdds::dds::SampleInfo m_info;
            DSR::OrMap sample;
            if (reader->take_next_sample(&sample, &m_info) != 0) {
                break;
            }
            if (log_level == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(log_level, m_info);
            if (m_info.valid_data) {
                if (!network_compatibility_or_fatal("GRAPH_ANSWER", sample.protocol_version, sample.sync_mode, sync_mode)) {
                    continue;
                }
                if (static_cast<uint32_t>(sample.id) != graph->get_agent_id()) {
                    if (sample.id != -1) {
                        qDebug() << " Received Full Graph from " << m_info.sample_identity.writer_guid().entityId.value
                                 << " whith "
                                 << sample.m.size() << " elements";
                        tp.spawn_task([this, s = std::move(sample)]() mutable {
                            CORTEX_PROFILE_ZONE_N("DSRGraph::fullgraph_request_thread apply full graph");
                            engine_->import_full_graph(FullGraphMessage{std::move(s)});
                        });
                        qDebug() << "Synchronized.";
                        sync = true;
                        break;
                    } else if (!sync && sample.to_id == agent_id) {
                        repeated = true;
                    }
                }
            }
        }
    });
}

DSRGraph::NewMessageFunctor DSRGraph::make_lww_fullgraph_request_functor(std::atomic<bool>& sync, std::atomic<bool>& repeated)
{
    return NewMessageFunctor(this, [this, &sync, &repeated](eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *graph)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("fg_request"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::fullgraph_request_thread callback");
        while (true)
        {
            eprosima::fastdds::dds::SampleInfo m_info;
            DSR::LWWGraphSnapshot sample;
            if (reader->take_next_sample(&sample, &m_info) != 0) {
                break;
            }
            if (log_level == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(log_level, m_info);
            if (m_info.valid_data) {
                if (!network_compatibility_or_fatal("LWW_GRAPH_ANSWER", sample.protocol_version, sample.sync_mode, sync_mode)) {
                    continue;
                }
                if (static_cast<uint32_t>(sample.id) != graph->get_agent_id()) {
                    if (sample.id != -1) {
                        tp.spawn_task([this, s = std::move(sample)]() mutable {
                            engine_->import_full_graph(FullGraphMessage{std::move(s)});
                        });
                        sync = true;
                        break;
                    } else if (!sync && sample.to_id == agent_id) {
                        repeated = true;
                    }
                }
            }
        }
    });
}

//////////////////////////////////////
/// NODE METHODS
/////////////////////////////////////

std::optional<DSR::Node> DSRGraph::get_node(const std::string &name)
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::get_node(name)");
    std::shared_lock<std::shared_mutex> lock(_mutex);
    if (name.empty()) return {};
    std::optional<uint64_t> id = get_id_from_name(name);
    if (id.has_value())
    {
        return engine_->get_node(id.value());
    }
    return {};
}

std::optional<DSR::Node> DSRGraph::get_node(uint64_t id)
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::get_node(id)");
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return engine_->get_node(id);
}

std::tuple<bool, std::optional<DSR::MvregNodeMsg>> DSRGraph::insert_node_(CRDTNode &&node)
{
    return crdt_engine().insert_node_raw(std::move(node));
}

template<typename No>
std::optional<uint64_t> DSRGraph::insert_node(No &&node)
    requires (std::is_same_v<std::remove_reference_t<No>, DSR::Node>)
{
    NodeMutationEffect effect;
    {
        CORTEX_PROFILE_ZONE_N("DSRGraph::insert_node local");
        std::unique_lock<std::shared_mutex> lock(_mutex);
        std::shared_lock<std::shared_mutex> lck_cache(_mutex_cache_maps);
        uint64_t new_node_id = generator.generate();
        node.id(new_node_id);
        if (node.name().empty() or name_map.contains(node.name()))
            node.name(node.type() + "_" + id_generator::hex_string(new_node_id));
        lck_cache.unlock();
        effect = engine_->insert_node_local(Node(node));
        if (!effect.applied) {
            return {};
        }
    }
    if (!copy)
    {
        if (effect.node_delta.has_value())
        {
            publish_node_message(*effect.node_delta);
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

template std::optional<uint64_t>  DSRGraph::insert_node<DSR::Node &&>(DSR::Node&&);
template std::optional<uint64_t>  DSRGraph::insert_node<DSR::Node&>(DSR::Node&);


template<typename No>
std::optional<uint64_t> DSRGraph::insert_node_with_id(No &&node)
    requires (std::is_same_v<std::remove_reference_t<No>, DSR::Node>)
{
    NodeMutationEffect effect;
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        std::shared_lock<std::shared_mutex> lck_cache(_mutex_cache_maps);
        if (id_map.contains(node.id())) {
            DSR_LOG_WARNING("[INSERT_NODE_WITH_ID] Node id already exists", node.id(), node.type());
            return {};
        }
        if (node.name().empty() or name_map.contains(node.name()))
            node.name(node.type() + "_" + id_generator::hex_string(node.id()));
        lck_cache.unlock();
        effect = engine_->insert_node_local(Node(node));
        if (!effect.applied) {
            return {};
        }
    }
    if (!copy)
    {
        if (effect.node_delta.has_value())
        {
            publish_node_message(*effect.node_delta);
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

template std::optional<uint64_t>  DSRGraph::insert_node_with_id<DSR::Node &&>(DSR::Node&&);
template std::optional<uint64_t>  DSRGraph::insert_node_with_id<DSR::Node&>(DSR::Node&);

std::tuple<bool, std::optional<DSR::MvregNodeAttrVec>> DSRGraph::update_node_(CRDTNode &&node)
{
    return crdt_engine().update_node_raw(std::move(node));
}
template<typename No>
bool DSRGraph::update_node(No &&node)
requires (std::is_same_v<std::remove_cvref_t<No>, DSR::Node>)
{
    CORTEX_PROFILE_ZONE_CS("DSRGraph::update_node");
    NodeMutationEffect effect;

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
        else if (id_map.contains(node.id())) {
            lck_cache.unlock();
            effect = engine_->update_node_local(Node(node));
            if (!effect.applied) {
                return false;
            }
        } else {
            return false;
        }
    }
    if (!copy) {
        if (effect.node_delta.has_value()) {
            publish_node_message(*effect.node_delta);
        }
        if (effect.node_attr_batch.has_value()) {
            publish_node_attr_batch(*effect.node_attr_batch);
        }
        if (effect.node_delta.has_value() || effect.node_attr_batch.has_value()) {
            DSR_LOG_DEBUG("[UPDATE_NODE] emitting update_node_signal", node.id(), node.type());
            emitter.update_node_signal(node.id(), node.type(), SignalInfo{agent_id});
            if (!effect.changed_attributes.empty()) {
                emitter.update_node_attr_signal(node.id(), effect.changed_attributes, SignalInfo{agent_id});
            }
        }
    }
    return true;
}

template bool DSRGraph::update_node<DSR::Node &&>(DSR::Node&&);
template bool DSRGraph::update_node<DSR::Node&>(DSR::Node&);
template bool DSRGraph::update_node<const DSR::Node&>(const DSR::Node&);
template bool DSRGraph::update_node<DSR::Node>(DSR::Node&&);



std::tuple<bool, std::vector<Edge>, std::optional<DSR::MvregNodeMsg>, std::vector<DSR::MvregEdgeMsg>>
DSRGraph::delete_node_(uint64_t id, const CRDTNode &node) {
    return crdt_engine().delete_node_raw(id, node);

}
bool DSRGraph::delete_node(const DSR::Node &node)
{
    return delete_node(node.id());
}

bool DSRGraph::delete_node(const std::string &name)
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::delete_node(name)");
    auto id = get_id_from_name(name);
    if (!id.has_value()) {
        return false;
    }
    return delete_node(*id);
}

bool DSRGraph::delete_node(uint64_t id)
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::delete_node(id)");
    NodeMutationEffect effect;
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        effect = engine_->delete_node_local(id);
    }
    if (!effect.applied) {
        return false;
    }

    if (!copy) {
        DSR_LOG_DEBUG("[DELETE_NODE] emitting del_node_signal", id);
        emitter.del_node_signal(id, SignalInfo{ agent_id });
        if (effect.deleted_node) {
            emitter.deleted_node_signal(*effect.deleted_node, SignalInfo{agent_id});
        }
        if (effect.node_delta.has_value()) {
            publish_node_message(*effect.node_delta);
        }
        for (const auto &delta : effect.edge_deltas) {
            publish_edge_message(delta);
        }
        for (auto &edge : effect.deleted_edges) {
            emitter.del_edge_signal(edge.from(), edge.to(), edge.type(), SignalInfo{ agent_id });
            emitter.deleted_edge_signal(edge, SignalInfo{ agent_id });
        }
    }

    return true;
}

template<typename Ed>
bool DSRGraph::insert_or_assign_edge(Ed &&attrs)
requires (std::is_same_v<std::remove_cvref_t<Ed>, DSR::Edge>)
{
    CORTEX_PROFILE_ZONE_CS("DSRGraph::insert_or_assign_edge");

    EdgeMutationEffect effect;
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        auto edge_copy = Edge(attrs);
        effect = engine_->insert_or_assign_edge_local(std::move(edge_copy));
        if (!effect.applied) {
            std::cout << __FUNCTION__ << ":" << __LINE__ << " Error. ID:" << attrs.from() << " or " << attrs.to()
                      << " not found. Cant update. " << std::endl;
            return false;
        }
    }
    if (!copy) {
        DSR_LOG_DEBUG("[INSERT_OR_ASSIGN_EDGE] emitting update_edge_signal", attrs.from(), attrs.to(), attrs.type());
        emitter.update_edge_signal(attrs.from(), attrs.to(), attrs.type(), SignalInfo{ agent_id });

        if (effect.edge_delta.has_value()) {
            publish_edge_message(*effect.edge_delta);
        }
        if (effect.edge_attr_batch.has_value()) {
            publish_edge_attr_batch(*effect.edge_attr_batch);
        }
        if (!effect.changed_attributes.empty()) {
            emitter.update_edge_attr_signal(attrs.from(), attrs.to(), attrs.type(), effect.changed_attributes, SignalInfo{ agent_id });
        }
    }
    return true;
}


template bool DSRGraph::insert_or_assign_edge<DSR::Edge>(DSR::Edge&&);
template bool DSRGraph::insert_or_assign_edge<DSR::Edge &&>(DSR::Edge&&);
template bool DSRGraph::insert_or_assign_edge<DSR::Edge&>(DSR::Edge&);
template bool DSRGraph::insert_or_assign_edge<const DSR::Edge&>(const DSR::Edge&);


std::optional<DSR::MvregEdgeMsg> DSRGraph::delete_edge_(uint64_t from, uint64_t to, const std::string &key)
{
    return crdt_engine().delete_edge_raw(from, to, key);
}

bool DSRGraph::delete_edge(uint64_t from, uint64_t to, const std::string &key)
{
    EdgeMutationEffect effect;
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        effect = engine_->delete_edge_local(from, to, key);
    }
    if (!effect.applied)
    {
        return false;
    }
    if (!copy) {
        DSR_LOG_DEBUG("[DELETE_EDGE] emitting del_edge_signal", from, to, key);
        emitter.del_edge_signal(from, to, key, SignalInfo{ agent_id });
        if (effect.deleted_edge.has_value()) {
            emitter.deleted_edge_signal(*effect.deleted_edge, SignalInfo{ agent_id });
        }
        if (effect.edge_delta.has_value()) {
            publish_edge_message(*effect.edge_delta);
        }
    }
    return true;
}

bool DSRGraph::delete_edge(const std::string &from, const std::string &to, const std::string &key)
{
    std::optional<uint64_t> id_from = get_id_from_name(from);
    std::optional<uint64_t> id_to = get_id_from_name(to);
    if (!id_from.has_value() || !id_to.has_value()) {
        return false;
    }
    return delete_edge(*id_from, *id_to, key);
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
            if (auto node = engine_->get_node(id); node.has_value()) {
                nodes_.emplace_back(std::move(*node));
            }
        }
    }
    return nodes_;
}

std::vector<Node> DSRGraph::get_nodes()
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    auto snapshot = engine_->snapshot();
    std::vector<Node> nodes_;
    nodes_.reserve(snapshot.size());
    for (auto& [id, node] : snapshot)
    {
        nodes_.emplace_back(std::move(node));
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
                if (auto node = engine_->get_node(id); node.has_value()) {
                    nodes_.emplace_back(std::move(*node));
                }
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
    return crdt_engine().get_crdt_edge(from, to, key);
}

std::optional<DSR::Edge> DSRGraph::get_edge(const std::string &from, const std::string &to, const std::string &key)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::optional<uint64_t> id_from = get_id_from_name(from);
    std::optional<uint64_t> id_to = get_id_from_name(to);
    if (id_from.has_value() and id_to.has_value())
    {
        return engine_->get_edge(id_from.value(), id_to.value(), key);
    }
    return {};
}

std::optional<DSR::Edge> DSRGraph::get_edge(uint64_t from, uint64_t to, const std::string &key)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return engine_->get_edge(from, to, key);
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

std::tuple<bool, std::optional<DSR::MvregEdgeMsg>, std::optional<DSR::MvregEdgeAttrVec>>
DSRGraph::insert_or_assign_edge_(CRDTEdge &&attrs, uint64_t from, uint64_t to)
{
    return crdt_engine().insert_or_assign_edge_raw(std::move(attrs), from, to);
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
        engine_->for_each_edge_of_type(type, [&](uint64_t, uint64_t, const Edge& edge) {
            edges_.emplace_back(edge);
        });
    } else {
        engine_->for_each_edge_of_type(type, [&](uint64_t, uint64_t, const Edge& edge) {
            edges_.emplace_back(edge);
        });
        if (!edges_.empty()) {
            edges_.shrink_to_fit();
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
    }
    engine_->for_each_edge_to(id, [&](uint64_t, const std::string&, const Edge& edge) {
        edges_.emplace_back(edge);
    });

    return edges_;
}

std::optional<std::map<std::pair<uint64_t, std::string>, DSR::Edge>> DSRGraph::get_edges(uint64_t id) {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::map<std::pair<uint64_t, std::string>, DSR::Edge> edges_;
    if (engine_->for_each_edge_from(id, [&](uint64_t to, const std::string& type, const Edge& edge) {
        edges_.emplace(std::pair{to, type}, edge);
    })) {
        return edges_;
    }
    return std::nullopt;
}


/////////////////////////////////////////////////
///// Utils
/////////////////////////////////////////////////

std::map<uint64_t, DSR::Node> DSRGraph::getCopy() const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return engine_->snapshot();
}

//////////////////////////////////////////////////////////////////////////////
/////  CORE
//////////////////////////////////////////////////////////////////////////////

std::optional<CRDTNode> DSRGraph::get_(uint64_t id)
{
    return crdt_engine().get_crdt_node(id);
}

const CRDTNode* DSRGraph::get_node_ptr_(uint64_t id) const
{
    return crdt_engine().get_node_ptr(id);
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
        return engine_->get_node(p.value());
    }
    return {};
}


std::string DSRGraph::get_node_type(Node &n)
{
    return n.type();
}

//////////////////////////////////////////////////////////////////////////////////////////////

void DSRGraph::update_maps_node_delete(uint64_t id, const std::optional<CRDTNode> &n)
{
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

void DSRGraph::update_maps_node_insert(uint64_t id, const CRDTNode &n)
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

void DSRGraph::update_maps_node_delete(uint64_t id, const std::optional<Node>& n)
{
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
            if (const auto tuple = std::pair{id, v.to()}; edges.contains(tuple)) {
                edges.at(tuple).erase(k.second);
                if (edges.at(tuple).empty()) edges.erase(tuple);
            }
            if (edgeType.contains(k.second)) {
                edgeType.at(k.second).erase({id, k.first});
                if (edgeType.at(k.second).empty()) edgeType.erase(k.second);
            }
            if (to_edges.contains(k.first)) {
                to_edges.at(k.first).erase({id, k.second});
                if (to_edges.at(k.first).empty()) to_edges.erase(k.first);
            }
        }
    }
}

void DSRGraph::update_maps_node_insert(const Node& n)
{
    std::unique_lock<std::shared_mutex> lck(_mutex_cache_maps);

    deleted.erase(n.id());
    name_map[n.name()] = n.id();
    id_map[n.id()] = n.name();
    nodeType[n.type()].emplace(n.id());
    for (const auto& [k, v] : n.fano())
    {
        edges[{n.id(), k.first}].insert(k.second);
        edgeType[k.second].insert({n.id(), k.first});
        to_edges[k.first].insert({n.id(), k.second});
    }
}


void DSRGraph::update_maps_edge_delete(uint64_t from, uint64_t to, const std::string &key)
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

void DSRGraph::update_maps_edge_insert(uint64_t from, uint64_t to, const std::string &key)
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
    return engine_->size();
}


bool DSRGraph::empty(const uint64_t &id)
{
    return get_node_ptr_(id) == nullptr;
}

void DSRGraph::join_delta_node(DSR::MvregNodeMsg &&mvreg)
{
    crdt_engine().join_delta_node(std::move(mvreg));
}


void DSRGraph::join_delta_edge(DSR::MvregEdgeMsg &&mvreg)
{
    crdt_engine().join_delta_edge(std::move(mvreg));
}

std::optional<std::string> DSRGraph::join_delta_node_attr(DSR::MvregNodeAttrMsg &&mvreg)
{
    return crdt_engine().join_delta_node_attr(std::move(mvreg));
}


std::optional<std::string> DSRGraph::join_delta_edge_attr(DSR::MvregEdgeAttrMsg &&mvreg)
{
    return crdt_engine().join_delta_edge_attr(std::move(mvreg));
}

void DSRGraph::join_full_graph(DSR::OrMap &&full_graph)
{
    crdt_engine().join_full_graph(std::move(full_graph));
#if 0
    CORTEX_PROFILE_ZONE_N("DSRGraph::join_full_graph");
    if (!protocol_version_matches(log_level, "GRAPH_ANSWER", full_graph.protocol_version)) {
        return;
    }
    // 5th element: post-join node snapshot captured inside the lock, used for
    // signal emission after the lock is released to avoid racing with
    // insert_node_/update_node (same pattern as join_delta_node).
    std::vector<std::tuple<bool, uint64_t, std::string, std::optional<CRDTNode>, std::optional<CRDTNode>>> updates;

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

        for (auto &[k, val] : full_graph.m) {
            auto mv = std::move(val.dk);
            bool mv_empty = mv.empty();
            agent_id_ch = val.agent_id;
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
                    updates.emplace_back(false, k, "", std::nullopt, std::nullopt);
                    delete_unprocessed_deltas();
                } else {
                    const auto& reg = it->second.read_reg();
                    update_maps_node_insert(k, reg);
                    updates.emplace_back(true, k, reg.type(), nd, reg);
                    consume_unprocessed_deltas();
                }
            }
        }

    }
    {
        CORTEX_PROFILE_ZONE_N("DSRGraph::join_full_graph emit phase");
        for (auto &[signal, id, type, nd, current_nd] : updates)
        if (signal) {
            //check what change is joined — use the snapshot captured inside the lock,
            //not nodes[id], which races with concurrent insert_node_/update_node calls.
            if (!nd.has_value() || nd->attrs() != current_nd->attrs()) {
                emitter.update_node_signal(id, type, SignalInfo{ agent_id_ch });
            } else if (nd.value() != *current_nd) {
                const auto& iter = current_nd->fano();
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
#endif
}

std::pair<bool, bool> DSRGraph::start_fullgraph_request_thread()
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::start_fullgraph_request_thread");
    return fullgraph_request_thread();
}

void DSRGraph::start_fullgraph_server_thread()
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::start_fullgraph_server_thread");
    auto fullgraph_thread = std::thread(&DSRGraph::fullgraph_server_thread, this);
    if (fullgraph_thread.joinable()) fullgraph_thread.join();
}

void DSRGraph::start_subscription_threads()
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::start_subscription_threads");
    auto delta_node_thread = std::thread(&DSRGraph::node_subscription_thread, this);
    auto delta_edge_thread = std::thread(&DSRGraph::edge_subscription_thread, this);
    auto delta_node_attrs_thread = std::thread(&DSRGraph::node_attrs_subscription_thread, this);
    auto delta_edge_attrs_thread = std::thread(&DSRGraph::edge_attrs_subscription_thread, this);

    if (delta_node_thread.joinable()) delta_node_thread.join();
    if (delta_edge_thread.joinable()) delta_edge_thread.join();
    if (delta_node_attrs_thread.joinable()) delta_node_attrs_thread.join();
    if (delta_edge_attrs_thread.joinable()) delta_edge_attrs_thread.join();
}

std::map<uint64_t, DSR::MvregNodeMsg> DSRGraph::Map()
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return crdt_engine().export_mvreg_map();
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
    CORTEX_PROFILE_ZONE_N("DSRGraph::node_subscription_thread setup");
    dsrpub_call_node = (this->*transport_profile().make_node_functor)();
    auto [res, sub, reader] = dsrsub_node.init(dsrparticipant.getParticipant(), dsrparticipant.getNodeTopic(), dsrparticipant.get_domain_id(), dsrpub_call_node, mtx_entity_creation);
    dsrparticipant.add_subscriber(dsrparticipant.getNodeTopic()->get_name(), {sub, reader});
}

void DSRGraph::edge_subscription_thread()
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::edge_subscription_thread setup");
    dsrpub_call_edge = (this->*transport_profile().make_edge_functor)();
    auto [res, sub, reader]  = dsrsub_edge.init(dsrparticipant.getParticipant(), dsrparticipant.getEdgeTopic(), dsrparticipant.get_domain_id(), dsrpub_call_edge, mtx_entity_creation);
    dsrparticipant.add_subscriber(dsrparticipant.getEdgeTopic()->get_name(), {sub, reader});

}

void DSRGraph::edge_attrs_subscription_thread()
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::edge_attrs_subscription_thread setup");
    dsrpub_call_edge_attrs = (this->*transport_profile().make_edge_attrs_functor)();
    auto [res, sub, reader] = dsrsub_edge_attrs.init(dsrparticipant.getParticipant(), dsrparticipant.getAttEdgeTopic(), dsrparticipant.get_domain_id(),
                           dsrpub_call_edge_attrs, mtx_entity_creation);
    dsrparticipant.add_subscriber(dsrparticipant.getAttEdgeTopic()->get_name(), {sub, reader});
    //dsrsub_edge_attrs_stream.init(dsrparticipant.getParticipant(), "DSR_EDGE_ATTRS_STREAM", dsrparticipant.getEdgeAttrTopicName(),
    //                       dsrpub_call_edge_attrs, true);
}

void DSRGraph::node_attrs_subscription_thread()
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::node_attrs_subscription_thread setup");
    dsrpub_call_node_attrs = (this->*transport_profile().make_node_attrs_functor)();
    auto [res, sub, reader] = dsrsub_node_attrs.init(dsrparticipant.getParticipant(), dsrparticipant.getAttNodeTopic(), dsrparticipant.get_domain_id(),
                           dsrpub_call_node_attrs, mtx_entity_creation);
    dsrparticipant.add_subscriber(dsrparticipant.getAttNodeTopic()->get_name(), {sub, reader});

}

void DSRGraph::fullgraph_server_thread()
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::fullgraph_server_thread setup");
    auto lambda_graph_request = [&](eprosima::fastdds::dds::DataReader *reader, DSR::DSRGraph *graph)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("fg_server"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::fullgraph_server_thread callback");
        while (true)
        {
            eprosima::fastdds::dds::SampleInfo m_info;
            DSR::GraphRequest sample;
            if (reader->take_next_sample(&sample, &m_info) == 0) {
                if (log_level == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(log_level, m_info);
                if (m_info.valid_data) {
                    if (!network_compatibility_or_fatal("GRAPH_REQUEST", sample.protocol_version, sample.sync_mode, sync_mode)) {
                        continue;
                    }
                    {
                        std::unique_lock<std::mutex> lck(participant_set_mutex);
                        if (auto [it, ok] = participant_set.emplace(sample.from, true);
                            it->second and !ok)
                        {
                            if (it->second) {
                                lck.unlock();
                                auto repeated_graph = graph->engine_->export_full_graph();
                                std::visit([&](auto&& payload) {
                                    using T = std::decay_t<decltype(payload)>;
                                    T empty{};
                                    empty.id = -1;
                                    empty.to_id = static_cast<uint32_t>(sample.id);
                                    empty.protocol_version = DSR::DSR_PROTOCOL_VERSION;
                                    empty.sync_mode = sync_mode_wire_value(sync_mode);
                                    dsrpub_request_answer.write(empty);
                                }, std::move(repeated_graph));
                                continue;
                            } else {}
                        } else {
                            it->second = true;
                            lck.unlock();
                        }
                    }
                    if (static_cast<uint32_t>(sample.id) != agent_id ) {

                        qDebug() << " Received Full Graph request: from "
                                << m_info.sample_identity.writer_guid().entityId.value;
                        auto full_graph_message = graph->engine_->export_full_graph();
                        publish_full_graph_message(std::move(full_graph_message), static_cast<int32_t>(graph->get_agent_id()));

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
    CORTEX_PROFILE_ZONE_N("DSRGraph::fullgraph_request_thread");
    std::atomic<bool> sync{false};
    std::atomic<bool> repeated{false};
    dsrpub_request_answer_call = (this->*transport_profile().make_fullgraph_request_functor)(sync, repeated);
    auto [res, sub, reader] = dsrsub_request_answer.init(dsrparticipant.getParticipant(), dsrparticipant.getGraphTopic(), dsrparticipant.get_domain_id(),
                               dsrpub_request_answer_call, mtx_entity_creation);
    dsrparticipant.add_subscriber(dsrparticipant.getGraphTopic()->get_name(), {sub, reader});

    {
        CORTEX_PROFILE_ZONE_N("DSRGraph::fullgraph_request_thread initial wait");
        std::this_thread::sleep_for(300ms);   // NEEDED ?
    }

    qDebug() << " Requesting the complete graph ";

    DSR::GraphRequest gr;
    gr.from = dsrparticipant.getParticipant()->get_qos().name().to_string();
    gr.id = static_cast<int32_t>(agent_id);
    gr.protocol_version = DSR::DSR_PROTOCOL_VERSION;
    gr.sync_mode = sync_mode_wire_value(sync_mode);
    {
        CORTEX_PROFILE_ZONE_N("DSRGraph::fullgraph_request_thread send request");
        dsrpub_graph_request.write(gr);
    }


    bool timeout = false;
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    while (!sync and !timeout and !repeated) {
        CORTEX_PROFILE_ZONE_N("DSRGraph::fullgraph_request_thread wait loop");
        std::this_thread::sleep_for(1000ms);
        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        timeout = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() > TIMEOUT * 3;
        qInfo() << " Waiting for the graph ... seconds to timeout ["
                << std::ceil(std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() / 10) / 100.0
                << "/" << TIMEOUT / 1000 * 3 << "] ";
        dsrpub_graph_request.write(gr);
    }

    dsrparticipant.delete_publisher(dsrparticipant.getGraphRequestTopic()->get_name());
    dsrparticipant.delete_subscriber(dsrparticipant.getGraphTopic()->get_name());

    return { sync, repeated };
}


//////////////////////////////////////////////////
///// PRIVATE COPY
/////////////////////////////////////////////////

DSRGraph::DSRGraph(const DSRGraph &G)
    : agent_id(G.agent_id),
      agent_name(G.agent_name),
      copy(true),
      sync_mode(G.sync_mode),
      ignored_attributes(G.ignored_attributes),
      same_host(G.same_host),
      generator(G.agent_id),
      log_level(G.log_level),
      engine_(make_sync_engine(*this, G.sync_mode)),
      tp(1, "join-copy"),
      tp_delta_attr(1, "attr-copy")
{
    std::shared_lock<std::shared_mutex> lock(G._mutex);
    std::shared_lock<std::shared_mutex> lock_cache(G._mutex_cache_maps);
    utils = std::make_unique<Utilities>(this);
    id_map = G.id_map;
    deleted = G.deleted;
    name_map = G.name_map;
    edges = G.edges;
    edgeType = G.edgeType;
    nodeType = G.nodeType;
    to_edges = G.to_edges;
    engine_ = G.engine_->clone(*this);
}

std::unique_ptr<DSRGraph> DSRGraph::G_copy()
{
    return std::unique_ptr<DSRGraph>(new DSRGraph(*this));
}

bool DSRGraph::is_copy() const
{
    return copy;
}
