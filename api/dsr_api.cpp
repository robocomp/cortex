//
// Created by crivac on 5/02/19.
//

#include "dsr/api/dsr_graph_settings.h"
#include "dsr/api/dsr_crdt_sync_engine.h"
#include "dsr/api/dsr_lww_sync_engine.h"
#include "dsr/core/types/internal_types.h"
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
#include <charconv>
#include <utility>
#include <cmath>
#include <dsr/api/dsr_logging.h>
#include <dsr/core/rtps/env_overrides.h>

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

template <typename Sample>
uint32_t remote_agent_id_of(const Sample& sample)
{
    return sample.agent_id;
}

template <typename Batch>
bool batch_is_from_remote(const Batch& batch, uint32_t local_agent_id)
{
    return !batch.vec.empty() && remote_agent_id_of(batch.vec.front()) != local_agent_id;
}

template <typename Batch>
bool batch_is_compatible(const char* channel, const Batch& batch, DSR::SyncMode local_sync_mode)
{
    return !batch.vec.empty() &&
           network_compatibility_or_fatal(channel, batch.vec.front().protocol_version, batch.vec.front().sync_mode, local_sync_mode);
}

inline size_t full_graph_payload_size(const DSR::OrMap& sample)
{
    return sample.m.size();
}

inline size_t full_graph_payload_size(const DSR::LWWGraphSnapshot& sample)
{
    return sample.nodes.size() + sample.edges.size();
}

// ── Deferred signal emission (fix for the bottle-join heap-corruption data race) ─────────────────
// Remote deltas are applied on the MAIN thread (the subscription functors marshal via
// QMetaObject::invokeMethod) while holding unique_lock(_mutex). Emitting DSR Qt signals there is
// hazardous twice over: (1) a queued std::string payload emitted from a worker thread races the
// receiver's QMetaType marshaling (TSan-confirmed; corrupts the heap, fatal in heavy-heap consumers
// like voxelizer); (2) emitting WHILE holding _mutex lets a same-thread re-entrant slot deadlock
// (EDEADLK). So during a remote apply we DEFER every emit into this thread-local buffer and flush it
// AFTER releasing the lock, on the main thread — no cross-thread payload, no re-entrant deadlock.
// Outside a deferred apply (defer depth 0, e.g. local writes) emits fire immediately as before.
thread_local int            tl_defer_depth = 0;
thread_local std::vector<std::function<void()>> tl_deferred_signals;

void defer_or_emit(std::function<void()> emit_fn)
{
    if (tl_defer_depth > 0)
        tl_deferred_signals.push_back(std::move(emit_fn));
    else
        emit_fn();
}

// RAII for a remote-delta apply: turn on deferral for the duration, then on destruction (which the
// caller arranges to happen AFTER the unique_lock block has ended) flush the buffered emits. So the
// signal emits fire on the main thread, after the graph lock is released.
struct RemoteApplyScope
{
    RemoteApplyScope() { ++tl_defer_depth; }
    ~RemoteApplyScope()
    {
        if (--tl_defer_depth == 0)
        {
            auto pending = std::move(tl_deferred_signals);
            tl_deferred_signals.clear();
            for (auto& f : pending) f();
        }
    }
    RemoteApplyScope(const RemoteApplyScope&) = delete;
    RemoteApplyScope& operator=(const RemoteApplyScope&) = delete;
};
}

void print_sample_info(DSR::GraphSettings::LOGLEVEL log_level, const DSR::Transport::ReceivedSampleInfo& info);

/////////////////////////////////////////////////
///// PUBLIC METHODS
/////////////////////////////////////////////////

DSRGraph::DSRGraph(GraphSettings settings) :
        agent_id(settings.agent_id),
        agent_name(std::move(settings.graph_name)),
        copy(false),
        sync_mode(settings.sync_mode),
    tp(settings.theradpool_threads, "graph"),
    tp_delta_attr(settings.attribute_threadpool_threads, "graph-attr"),
        same_host(settings.same_host),
        generator(settings.agent_id),
        log_level(settings.log_level),
        engine_(make_sync_engine(*this, settings.sync_mode))
{
    CORTEX_PROFILE_MIN_N("DSRGraph::DSRGraph");

    qDebug() << "Agent name: " << QString::fromStdString(agent_name);
    {
        CORTEX_PROFILE_MIN_N("DSRGraph::DSRGraph setup utils/signals");
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
        CORTEX_PROFILE_MIN_N("DSRGraph::DSRGraph init participant");
        return dsrparticipant.init(agent_id, agent_name, settings.same_host,
                                   ParticipantChangeFn(this, [&](DSR::DSRGraph *graph,
                                           DSR::Transport::ParticipantDiscoveryStatus status,
                                           const DSR::Transport::ParticipantDiscoveryInfo& info)
                                           {
                                               CORTEX_PROFILE_DETAIL_N("DSRGraph::participant discovery callback");
                                               graph->on_peer_discovery(status, info);
                                               if (status == DSR::Transport::ParticipantDiscoveryStatus::discovered)
                                               {
                                                   std::unique_lock<std::mutex> lck(participant_set_mutex);
                                                   std::cout << "Participant matched [" << info.participant_name << "]" << std::endl;
                                                   graph->participant_set.emplace(info.participant_name, false);
                                               }
                                                else if (status == DSR::Transport::ParticipantDiscoveryStatus::removed ||
                                                         status == DSR::Transport::ParticipantDiscoveryStatus::dropped)
                                                {
                                                    std::unique_lock<std::mutex> lck(participant_set_mutex);
                                                   graph->participant_set.erase(info.participant_name);
                                                   std::cout << "Participant unmatched [" << info.participant_name << "]" << std::endl;
                                                   // Participant name is "Participant_<id> ( <agent_name> )" which doesn't
                                                   // match the DSR node name "<agent_name> <id>". Find the agent node by
                                                   // its agent_id attribute instead.
                                                   const std::string& pname = info.participant_name;
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
    static_cast<void>(participant_handle);
    {
        CORTEX_PROFILE_MIN_N("DSRGraph::DSRGraph init publishers");
        // Resync on match: set BEFORE the writers exist, a match can fire as soon as one does.
        // DSR_RESYNC_ON_MATCH=0 switches the SENDING side off (addressed pushes are still accepted).
        if (const auto resync = DSR::RTPS::Env::read_u32("DSR_RESYNC_ON_MATCH");
            not resync.present() or not resync.value.has_value() or *resync.value != 0)
        {
            dsrparticipant.set_publication_match_callback(
                [this](const DSR::Transport::PublicationMatchInfo& info) { on_builtin_writer_match(info); });
        }
        else
        {
            qInfo() << "DSR_RESYNC_ON_MATCH=0: this agent will not push its graph to newly matched peers";
        }
        dsrparticipant.init_builtin_publishers();
    }

    // RTPS Initialize comms threads
    if (!settings.input_file.empty())
    {
        try
        {
            CORTEX_PROFILE_MIN_N("DSRGraph::DSRGraph load input graph");
            read_from_json_file(settings.input_file);
            qDebug() << __FUNCTION__ << "Warning, graph read from file " << QString::fromStdString(settings.input_file);
        }
        catch(const DSR::DSRException& e)
        {
            std::cout << e.what() << '\n';
            qFatal("Aborting program. Cannot continue without intial file");
        }
        // The graph came from the file: there is no initial full-graph import still to come, so a
        // later import (an addressed push from a peer) is a re-sync and must emit its signals.
        engine_->mark_initial_state_loaded();
        start_fullgraph_server_thread();
        start_subscription_threads();
        addressed_fullgraph_subscription_thread();
    }
    else
    {
        {
            CORTEX_PROFILE_MIN_N("DSRGraph::DSRGraph bootstrap subscriptions");
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

void DSRGraph::publish_node_message(const NodeDeltaMessage& message)
{
    std::visit([this](const auto& payload) {
        dsrparticipant.publish_node(payload);
    }, message);
}

void DSRGraph::publish_node_attr_batch(const NodeAttrDeltaBatchMessage& message)
{
    std::visit([this](const auto& payload) {
        dsrparticipant.publish_node_attrs(payload);
    }, message);
}

void DSRGraph::publish_edge_message(const EdgeDeltaMessage& message)
{
    std::visit([this](const auto& payload) {
        dsrparticipant.publish_edge(payload);
    }, message);
}

void DSRGraph::publish_edge_attr_batch(const EdgeAttrDeltaBatchMessage& message)
{
    std::visit([this](const auto& payload) {
        dsrparticipant.publish_edge_attrs(payload);
    }, message);
}

void DSRGraph::publish_full_graph_message(FullGraphMessage&& message, int32_t sender_id)
{
    std::visit([this, sender_id](auto&& payload) {
        payload.id = sender_id;
        dsrparticipant.publish_graph_answer(payload);
    }, std::move(message));
}

template <typename Sample>
DSRGraph::SampleCallback<Sample> DSRGraph::make_node_subscription_functor_impl(const char* channel, bool clear_deleted_signal)
{
    auto name = "node_subscription_thread";
    return [this, channel, name, showReceived = log_level, clear_deleted_signal]
    (Sample&& sample, const DSR::Transport::ReceivedSampleInfo& m_info)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("node_sub"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::node_subscription_thread callback");
        try {
            if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(showReceived, m_info);
            if (!m_info.valid_data || remote_agent_id_of(sample) == agent_id) {
                return;
            }
            if (!network_compatibility_or_fatal(channel, sample.protocol_version, sample.sync_mode, sync_mode)) {
                return;
            }
            if (clear_deleted_signal && sample.id == CLEAR_DELETED_SIGNAL) {
                std::unique_lock<std::shared_mutex> lock(_mutex);
                std::unique_lock<std::shared_mutex> lck_cache(_mutex_cache_maps);
                deleted.clear();
                return;
            }
            if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) {
                qDebug() << name << " Received:" << std::to_string(sample.id).c_str() << " node from: "
                         << m_info.source_entity_id;
            }
            // Apply on the graph's (main) thread with deferred emit: marshal to main so the apply +
            // (deferred) signal emits run on the main/GUI thread — no std::string Qt payload crosses
            // a thread boundary (the bottle-join heap-corruption race) — and RemoteApplyScope flushes
            // the emits AFTER the unique_lock block ends (no re-entrant EDEADLK). Sample is MOVED in.
            QMetaObject::invokeMethod(this, [this, sample = std::move(sample)]() mutable {
                RemoteApplyScope defer;
                {
                    std::unique_lock<std::shared_mutex> lock(_mutex);
                    engine_->apply_remote_node_delta(NodeDeltaMessage{std::move(sample)});
                }
            }, Qt::QueuedConnection);
        }
        catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
    };
}

template <typename Sample>
DSRGraph::SampleCallback<Sample> DSRGraph::make_edge_subscription_functor_impl(const char* channel)
{
    auto name = "edge_subscription_thread";
    return [this, channel, name, showReceived = log_level]
    (Sample&& sample, const DSR::Transport::ReceivedSampleInfo& m_info)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("edge_sub"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::edge_subscription_thread callback");
        try {
            if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(showReceived, m_info);
            if (!m_info.valid_data || remote_agent_id_of(sample) == agent_id) {
                return;
            }
            if (!network_compatibility_or_fatal(channel, sample.protocol_version, sample.sync_mode, sync_mode)) {
                return;
            }
            // Apply on the graph's (main) thread with deferred emit — see apply_remote_node_delta note.
            QMetaObject::invokeMethod(this, [this, sample = std::move(sample)]() mutable {
                RemoteApplyScope defer;
                {
                    std::unique_lock<std::shared_mutex> lock(_mutex);
                    engine_->apply_remote_edge_delta(EdgeDeltaMessage{std::move(sample)});
                }
            }, Qt::QueuedConnection);
        }
        catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
    };
}

template <typename Batch>
DSRGraph::SampleCallback<Batch> DSRGraph::make_edge_attrs_subscription_functor_impl(const char* channel)
{
    auto name = "edge_attrs_subscription_thread";
    return [this, channel, name, showReceived = log_level]
    (Batch&& samples, const DSR::Transport::ReceivedSampleInfo& m_info)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("edge_attr_sub"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::edge_attrs_subscription_thread callback");
        try {
            if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(showReceived, m_info);
            if (!m_info.valid_data || !batch_is_from_remote(samples, agent_id)) {
                return;
            }
            if (!batch_is_compatible(channel, samples, sync_mode)) {
                return;
            }
            if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) {
                qDebug() << name << " Received:" << samples.vec.size() << " edge attr from: "
                         << m_info.source_entity_id;
            }
            // Apply on the graph's (main) thread with deferred emit — see apply_remote_node_delta note.
            QMetaObject::invokeMethod(this, [this, samples = std::move(samples)]() mutable {
                CORTEX_PROFILE_ZONE_N("DSRGraph::edge_attrs_subscription_thread apply batch");
                RemoteApplyScope defer;
                {
                    std::unique_lock<std::shared_mutex> lock(_mutex);
                    engine_->apply_remote_edge_attr_batch(EdgeAttrDeltaBatchMessage{std::move(samples)});
                }
            }, Qt::QueuedConnection);
        }
        catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
    };
}

template <typename Batch>
DSRGraph::SampleCallback<Batch> DSRGraph::make_node_attrs_subscription_functor_impl(const char* channel)
{
    auto name = "node_attrs_subscription_thread";
    return [this, channel, name, showReceived = log_level]
    (Batch&& samples, const DSR::Transport::ReceivedSampleInfo& m_info)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("node_attr_sub"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::node_attrs_subscription_thread callback");
        try {
            if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(showReceived, m_info);
            if (!m_info.valid_data || !batch_is_from_remote(samples, agent_id)) {
                return;
            }
            if (!batch_is_compatible(channel, samples, sync_mode)) {
                return;
            }
            if (showReceived == GraphSettings::LOGLEVEL::DEBUGL) {
                qDebug() << name << " Received:" << samples.vec.size() << " node attrs from: "
                         << m_info.source_entity_id;
            }
            // Apply on the graph's (main) thread with deferred emit — see apply_remote_node_delta note.
            QMetaObject::invokeMethod(this, [this, samples = std::move(samples)]() mutable {
                CORTEX_PROFILE_ZONE_N("DSRGraph::node_attrs_subscription_thread apply batch");
                RemoteApplyScope defer;
                {
                    std::unique_lock<std::shared_mutex> lock(_mutex);
                    engine_->apply_remote_node_attr_batch(NodeAttrDeltaBatchMessage{std::move(samples)});
                }
            }, Qt::QueuedConnection);
        }
        catch (const std::exception &ex) { std::cerr << ex.what() << std::endl; }
    };
}

void DSRGraph::queue_full_graph_import(FullGraphMessage&& message)
{
    QMetaObject::invokeMethod(this, [this, m = std::move(message)]() mutable {
        CORTEX_PROFILE_ZONE_N("DSRGraph::fullgraph_request_thread apply full graph");
        RemoteApplyScope defer;
        {
            std::unique_lock<std::shared_mutex> lock(_mutex);
            engine_->import_full_graph(std::move(m));
        }
    }, Qt::QueuedConnection);
}

// What travels on the GRAPH_ANSWER topic, told apart by (id, to_id) alone — no wire change:
//   id == -1, to_id == requester   "repeated": the server tells a joiner its agent id is taken.
//   id == sender, to_id == 0       the server's answer to a GRAPH_REQUEST. Broadcast: every agent that
//                                  joined through a server imports it, as it always did.
//   id == sender, to_id != 0       an ADDRESSED push (resync on match, see on_builtin_writer_match):
//                                  imported only by the agent whose id is to_id.
// to_id == 0 means "not addressed", so an agent whose id is 0 can never be the target of a push.
template <typename GraphSample>
DSRGraph::SampleCallback<GraphSample> DSRGraph::make_fullgraph_request_functor_impl(const char* channel, std::shared_ptr<FullGraphJoinState> state)
{
    // Capture the state BY VALUE (shared_ptr): this callback is stored in a permanent DDS subscription
    // and must keep the flags alive — capturing raw refs to fullgraph_request_thread()'s stack was the
    // dangling-write bug. (Fixed 2026-07-21.)
    return [this, channel, state](GraphSample&& sample, const DSR::Transport::ReceivedSampleInfo& m_info)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("fg_request"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::fullgraph_request_thread callback");
        if (log_level == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(log_level, m_info);
        if (!m_info.valid_data) {
            return;
        }
        if (!network_compatibility_or_fatal(channel, sample.protocol_version, sample.sync_mode, sync_mode)) {
            return;
        }
        if (static_cast<uint32_t>(sample.id) == get_agent_id()) {
            return;  // our own sample
        }
        if (sample.id == -1) {
            if (!state->sync && sample.to_id == agent_id) {
                state->repeated = true;
            }
            return;
        }
        if (sample.to_id != 0) {
            // An addressed push: only its target imports it. That is what keeps the cost at one
            // import per pairing instead of one per agent in the fleet.
            if (sample.to_id != agent_id) {
                return;
            }
            const int32_t sender = sample.id;
            std::lock_guard<std::mutex> lck(state->mtx);
            if (!state->sync) {
                // Still joining. A push is NOT the initial graph: its sender may itself be a joiner
                // holding little or nothing, so it must not end the wait for the server, and it must
                // not be the first import (which builds the copy silently, without signals). Keep it
                // — a later push from the same sender supersedes an earlier one — and import it
                // right after the server's answer.
                state->early_pushes.insert_or_assign(sender, FullGraphMessage{std::move(sample)});
                return;
            }
            qDebug() << " Received addressed Full Graph from agent " << sender
                     << " whith " << full_graph_payload_size(sample) << " elements";
            queue_full_graph_import(FullGraphMessage{std::move(sample)});
            return;
        }
        if (!state->accept_server_answers) {
            return;
        }
        qDebug() << " Received Full Graph from " << m_info.source_entity_id
                 << " whith " << full_graph_payload_size(sample) << " elements";
        std::lock_guard<std::mutex> lck(state->mtx);
        queue_full_graph_import(FullGraphMessage{std::move(sample)});
        // Queued in this order on the same thread: the server's graph first, then what peers pushed
        // while we were waiting for it.
        for (auto& [sender, early] : state->early_pushes) {
            queue_full_graph_import(std::move(early));
        }
        state->early_pushes.clear();
        qDebug() << "Synchronized.";
        state->sync = true;
    };
}

// ── Resync on match ─────────────────────────────────────────────────────────────────────────────
// THE LOSS. The graph writers are RELIABLE + VOLATILE: a sample written before a remote reader has
// matched the writer is never delivered to that reader. An agent that inserts a node before its
// DSR_NODE writer has matched some running peer therefore never gives that peer the node; the later
// attribute deltas for it are parked for ever in the peer's unprocessed queues, and nothing else
// repairs it (only the agent that loaded the JSON file answers GRAPH_REQUESTs, once per participant).
//
// THE REPAIR. When OUR writers finish pairing with a remote participant, send that participant our
// full graph, addressed to it only. A full graph is a CRDT state: joining it is idempotent, so the
// peer simply gains whatever it had missed.
//
// 1. WHO. on_publication_matched gives the remote reader's instance handle = its GUID, whose 12-byte
//    prefix is the participant's. Participant discovery gives the same prefix together with the name
//    "Participant_<agent_id> ( <name> )". The two arrive on FastDDS threads in either order, so the
//    peer entry is keyed by prefix and the agent id is filled in whenever discovery brings it.
// 2. WHEN. The push is itself a GRAPH_ANSWER sample, as lossy as any other until our GRAPH_ANSWER
//    writer has matched the peer's reader. So the trigger is the moment the LAST of our five writers
//    (node, edge, node attrs, edge attrs, graph answer) matches the peer: from then on no delta can
//    be lost to it, everything written earlier is already in our state (a local change is applied
//    under _mutex before it is published), and the push can be delivered. One event, one push — no
//    timer and nothing to tune. If a writer is unmatched and matched again, the pairing completes
//    again and the peer is pushed again, which is right: samples were lost in between.
//    A peer that never offers all five readers is never pushed to (that is today's behaviour).
// 3. WHERE. Nothing but bookkeeping happens on the FastDDS listener thread. The export and the write
//    run in one queued call on the graph's Qt thread; pairings that complete before it runs are
//    served by that single call, with a single export.
// 4. COST. One export and one addressed import per ordered pair of agents, per pairing. A fleet of N
//    agents starting together does N(N-1) of them in total — 56 for N = 8, seven sent and seven
//    imported by each agent — against the N-1 broadcast answers (each imported by every joined
//    agent) that the join already costs. In steady state it costs nothing.
void DSRGraph::on_builtin_writer_match(const DSR::Transport::PublicationMatchInfo& info)
{
    using DSR::Transport::BuiltinWriter;
    if (info.writer == BuiltinWriter::graph_request) {
        return;  // requests are not graph state
    }
    constexpr uint8_t all_writers = (1u << static_cast<uint8_t>(BuiltinWriter::node)) |
                                    (1u << static_cast<uint8_t>(BuiltinWriter::edge)) |
                                    (1u << static_cast<uint8_t>(BuiltinWriter::node_attrs)) |
                                    (1u << static_cast<uint8_t>(BuiltinWriter::edge_attrs)) |
                                    (1u << static_cast<uint8_t>(BuiltinWriter::graph_answer));
    const auto bit = static_cast<uint8_t>(1u << static_cast<uint8_t>(info.writer));

    std::lock_guard<std::mutex> lck(resync_mutex_);
    if (not info.matched) {
        // find, not operator[]: the unmatches of a participant that has already been removed must
        // not bring its entry back.
        if (auto it = resync_peers_.find(info.remote_prefix); it != resync_peers_.end()) {
            it->second.matched_writers &= static_cast<uint8_t>(~bit);
            it->second.push_pending = false;  // nothing can be delivered to it now; a re-match asks again
        }
        return;
    }
    auto& peer = resync_peers_[info.remote_prefix];
    const bool was_complete = peer.matched_writers == all_writers;
    peer.matched_writers |= bit;
    if (not was_complete and peer.matched_writers == all_writers) {
        peer.push_pending = true;
        schedule_resync_drain_locked();
    }
}

void DSRGraph::on_peer_discovery(DSR::Transport::ParticipantDiscoveryStatus status,
                                 const DSR::Transport::ParticipantDiscoveryInfo& info)
{
    using Status = DSR::Transport::ParticipantDiscoveryStatus;
    std::lock_guard<std::mutex> lck(resync_mutex_);
    if (status == Status::removed or status == Status::dropped) {
        resync_peers_.erase(info.guid_prefix);
        return;
    }
    if (status != Status::discovered) {
        return;
    }
    // "Participant_<agent_id> ( <name> )". Anything else on the domain is not a DSR agent.
    constexpr std::string_view prefix = "Participant_";
    const std::string_view name = info.participant_name;
    if (not name.starts_with(prefix)) {
        return;
    }
    uint32_t peer_id = 0;
    const auto* first = name.data() + prefix.size();
    const auto* last = name.data() + name.size();
    if (std::from_chars(first, last, peer_id).ec != std::errc{}) {
        return;
    }
    auto& peer = resync_peers_[info.guid_prefix];
    peer.agent_id = peer_id;
    if (peer.push_pending) {
        schedule_resync_drain_locked();  // the matches came before the discovery callback
    }
}

void DSRGraph::schedule_resync_drain_locked()
{
    if (resync_drain_scheduled_) {
        return;
    }
    resync_drain_scheduled_ = true;
    QMetaObject::invokeMethod(this, [this] { drain_resync_pending(); }, Qt::QueuedConnection);
}

void DSRGraph::drain_resync_pending()
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::drain_resync_pending");
    std::vector<uint32_t> targets;
    {
        std::lock_guard<std::mutex> lck(resync_mutex_);
        resync_drain_scheduled_ = false;
        for (auto& [prefix, peer] : resync_peers_) {
            if (not peer.push_pending or not peer.agent_id.has_value()) {
                continue;  // not asked for, or not yet known: discovery will schedule again
            }
            peer.push_pending = false;
            // to_id 0 means "not addressed"; our own id on another participant is a duplicate
            // agent that the join handshake is about to reject.
            if (*peer.agent_id != 0 and *peer.agent_id != agent_id) {
                targets.push_back(*peer.agent_id);
            }
        }
    }
    if (targets.empty()) {
        return;
    }
    // Same locking as the server's answer: export_full_graph() reads all of nodes_ and must exclude
    // the delta-apply writers (unique_lock(_mutex)). The message is a copy, so it is written after
    // the lock is released.
    FullGraphMessage message;
    {
        std::shared_lock<std::shared_mutex> graph_lock(_mutex);
        message = engine_->export_full_graph();
    }
    std::visit([&](auto& payload) {
        payload.id = static_cast<int32_t>(agent_id);
        payload.protocol_version = DSR::DSR_PROTOCOL_VERSION;
        payload.sync_mode = sync_mode_wire_value(sync_mode);
        for (const auto target : targets) {
            payload.to_id = target;
            const bool ok = dsrparticipant.publish_graph_answer(payload);
            qInfo() << "[resync] full graph (" << full_graph_payload_size(payload) << "elements )"
                    << (ok ? "pushed to agent" : "COULD NOT be pushed to agent") << target
                    << "after its readers matched";
        }
    }, message);
}

//////////////////////////////////////
/// NODE METHODS
/////////////////////////////////////

std::optional<DSR::Node> DSRGraph::get_node(const std::string &name)
{
    CORTEX_PROFILE_HOT_N("DSRGraph::get_node(name)");
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
    CORTEX_PROFILE_HOT_N("DSRGraph::get_node(id)");
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return engine_->get_node(id);
}

template<typename No>
std::optional<uint64_t> DSRGraph::insert_node(No &&node)
    requires (std::is_same_v<std::remove_reference_t<No>, DSR::Node>)
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::insert_node");
    NodeMutationEffect effect;
    {
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
            CORTEX_PROFILE_DETAIL_N("DSRGraph::insert_node publish deltas");
            publish_node_message(*effect.node_delta);
            CORTEX_PROFILE_DETAIL_N("DSRGraph::insert_node emit signals");
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
    CORTEX_PROFILE_ZONE_N("DSRGraph::insert_node_with_id");
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
            CORTEX_PROFILE_DETAIL_N("DSRGraph::insert_node_with_id send deltas");
            publish_node_message(*effect.node_delta);
            CORTEX_PROFILE_DETAIL_N("DSRGraph::insert_node_with_id emit signals");
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

template<typename No>
bool DSRGraph::update_node(No &&node)
requires (std::is_same_v<std::remove_cvref_t<No>, DSR::Node>)
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::update_node");
    NodeMutationEffect effect;

    // Cache id/type up-front: when the caller passes an rvalue Node we forward
    // (move) it into the engine below, after which `node` is moved-from and must
    // not be read. These cached values are used by the post-lock signal emission.
    const uint64_t cached_node_id = node.id();
    const std::string cached_node_type = node.type();

    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        std::shared_lock<std::shared_mutex> lck_cache(_mutex_cache_maps);
        if (deleted.contains(node.id()))
            throw std::runtime_error(
                    (std::string("Cannot update node in G, " + std::to_string(node.id()) + " is deleted") + __FILE__ +
                     " " + __FUNCTION__ + " " + std::to_string(__LINE__)).data());
        const auto id_it = id_map.find(node.id());
        const auto name_it = name_map.find(node.name());
        // A new name for an existing id is a RENAME and is allowed; the engine refreshes
        // name_map/id_map and propagates it. A name already held by a DIFFERENT id is refused.
        if (name_it != name_map.end() && name_it->second != node.id())
            return false;
        else if (id_it != id_map.end()) {
            lck_cache.unlock();
            // Perfect-forward into the engine: rvalue callers move (no deep blob
            // copy under _mutex), lvalue/const callers still copy as before.
            effect = engine_->update_node_local(Node(std::forward<No>(node)));
            if (!effect.applied) {
                return false;
            }
        } else {
            return false;
        }
    }
    if (!copy) {
        CORTEX_PROFILE_DETAIL_N("DSRGraph::update_node send deltas");
        if (effect.node_delta.has_value()) {
            publish_node_message(*effect.node_delta);
        }
        if (effect.node_attr_batch.has_value()) {
            publish_node_attr_batch(*effect.node_attr_batch);
        }
        CORTEX_PROFILE_DETAIL_N("DSRGraph::update_node emit signals");
        if (effect.node_delta.has_value() || effect.node_attr_batch.has_value()) {
            DSR_LOG_DEBUG("[UPDATE_NODE] emitting update_node_signal", cached_node_id, cached_node_type);
            emitter.update_node_signal(cached_node_id, cached_node_type, SignalInfo{agent_id});
            if (!effect.changed_attributes.empty()) {
                emitter.update_node_attr_signal(cached_node_id, effect.changed_attributes, SignalInfo{agent_id});
            }
        }
    }
    return true;
}

template bool DSRGraph::update_node<DSR::Node &&>(DSR::Node&&);
template bool DSRGraph::update_node<DSR::Node&>(DSR::Node&);
template bool DSRGraph::update_node<const DSR::Node&>(const DSR::Node&);
template bool DSRGraph::update_node<DSR::Node>(DSR::Node&&);



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
        CORTEX_PROFILE_DETAIL_N("DSRGraph::delete_node(id) send deltas");
        if (effect.node_delta.has_value()) {
            publish_node_message(*effect.node_delta);
        }
        for (const auto &delta : effect.edge_deltas) {
            publish_edge_message(delta);
        }
        CORTEX_PROFILE_DETAIL_N("DSRGraph::delete_node(id) emit signals");
        DSR_LOG_DEBUG("[DELETE_NODE] emitting del_node_signal", id);
        emitter.del_node_signal(id, SignalInfo{ agent_id });
        
        if (effect.deleted_node) {
            emitter.deleted_node_signal(*effect.deleted_node, SignalInfo{agent_id});
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
    CORTEX_PROFILE_ZONE_N("DSRGraph::insert_or_assign_edge");

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
        CORTEX_PROFILE_DETAIL_N("DSRGraph::insert_or_assign_edge send deltas");
        if (effect.edge_delta.has_value()) {
            publish_edge_message(*effect.edge_delta);
        }
        if (effect.edge_attr_batch.has_value()) {
            publish_edge_attr_batch(*effect.edge_attr_batch);
        }
        CORTEX_PROFILE_DETAIL_N("DSRGraph::insert_or_assign_edge emit signals");
        DSR_LOG_DEBUG("[INSERT_OR_ASSIGN_EDGE] emitting update_edge_signal", attrs.from(), attrs.to(), attrs.type());
        emitter.update_edge_signal(attrs.from(), attrs.to(), attrs.type(), SignalInfo{ agent_id });

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


bool DSRGraph::delete_edge(uint64_t from, uint64_t to, const std::string &key)
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::delete_edge(from, to, key)");
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
        CORTEX_PROFILE_ZONE_N("DSRGraph::delete_edge(from, to, key) send deltas");
        if (effect.edge_delta.has_value()) {
            publish_edge_message(*effect.edge_delta);
        }

        CORTEX_PROFILE_ZONE_N("DSRGraph::delete_edge(from, to, key) emit signals");
        DSR_LOG_DEBUG("[DELETE_EDGE] emitting del_edge_signal", from, to, key);
        emitter.del_edge_signal(from, to, key, SignalInfo{ agent_id });
        if (effect.deleted_edge.has_value()) {
            emitter.deleted_edge_signal(*effect.deleted_edge, SignalInfo{ agent_id });
        }
    }
    return true;
}

bool DSRGraph::delete_edge(const std::string &from, const std::string &to, const std::string &key)
{
    CORTEX_PROFILE_ZONE_N("DSRGraph::delete_edge(from_str, to_str, key)");
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
    if (const auto it = nodeType.find(type); it != nodeType.end())
    {
        nodes_.reserve(it->second.size());
        for (auto &id: it->second)
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
            if (const auto it = nodeType.find(type); it != nodeType.end()) total += it->second.size();
        nodes_.reserve(total);
    }
    for (auto &type : types)
    {
        if (const auto it = nodeType.find(type); it != nodeType.end())
        {
            for (auto &id: it->second)
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
std::optional<DSR::Edge> DSRGraph::get_edge(const std::string &from, const std::string &to, const std::string &key)
{
    CORTEX_PROFILE_HOT_N("DSRGraph::get_edge(from_str, to_str, key)");
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
    CORTEX_PROFILE_HOT_N("DSRGraph::get_edge(from, to, key)");
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return engine_->get_edge(from, to, key);
}

std::optional<Edge> DSRGraph::get_edge(const Node &n, const std::string &to, const std::string &key)
{
    CORTEX_PROFILE_HOT_N("DSRGraph::get_edge(Node, to_str, key)");
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
    CORTEX_PROFILE_HOT_N("DSRGraph::get_edge(Node, to, key)");
    auto it = n.fano().find({to, key});
    if (it != n.fano().end()) return it->second;
    return {};
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
    if (const auto it = edgeType.find(type); it != edgeType.end()) {
        edges_.reserve(it->second.size());
    }
    engine_->for_each_edge_of_type(type, [&](uint64_t, uint64_t, const Edge& edge) {
        edges_.emplace_back(edge);
    });
    return edges_;
}

std::vector<DSR::Edge> DSRGraph::get_edges_to_id(uint64_t id)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::shared_lock<std::shared_mutex> lock_cache(_mutex_cache_maps);
    std::vector<Edge> edges_;
    if (const auto it = to_edges.find(id); it != to_edges.end()) {
        edges_.reserve(it->second.size());
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

bool DSRGraph::with_node_attrs_(uint64_t id, const SyncEngine::NodeAttrsVisitor& visitor) const
{
    return engine_->with_node_attrs(id, visitor);
}

bool DSRGraph::with_node_view_(uint64_t id, const SyncEngine::NodeViewVisitor& visitor) const
{
    return engine_->with_node_view(id, visitor);
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

std::optional<DSRGraph::NodeTreeInfo> DSRGraph::get_node_tree_info(uint64_t id)
{
    CORTEX_PROFILE_HOT_N("DSRGraph::get_node_tree_info");
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::optional<NodeTreeInfo> out;
    with_node_attrs_(id, [&](const SyncEngine::NodeAttrsView& attrs) {
        NodeTreeInfo info;
        if (const auto* a = attrs.find(level_att::attr_name.data()); a != nullptr)
            info.level = get_attrib_by_name<level_att>(*a);
        if (const auto* a = attrs.find(parent_att::attr_name.data()); a != nullptr)
            info.parent = get_attrib_by_name<parent_att>(*a);
        out = info;
    });
    return out;
}

std::string DSRGraph::get_node_type(Node &n)
{
    return n.type();
}

//////////////////////////////////////////////////////////////////////////////////////////////

void DSRGraph::update_maps_node_insert(uint64_t id, std::string_view name, std::string_view type, const EdgeKeyList& outgoing_edges)
{
    update_maps_node_insert_impl(id, std::string{name}, std::string{type}, outgoing_edges);
}

void DSRGraph::update_maps_node_delete(uint64_t id, std::optional<std::string_view> type, const EdgeKeyList& outgoing_edges)
{
    update_maps_node_delete_impl(id, type, outgoing_edges);
}

void DSRGraph::update_maps_edge_delete(uint64_t from, uint64_t to, const std::string &key)
{

    std::unique_lock<std::shared_mutex> lck(_mutex_cache_maps);
    const auto tuple = std::pair{from, to};
    if (auto edge_it = edges.find(tuple); edge_it != edges.end()) {
        edge_it->second.erase(key);
        if (edge_it->second.empty()) edges.erase(edge_it);
    }

    if (auto to_edge_it = to_edges.find(to); to_edge_it != to_edges.end()) {
        to_edge_it->second.erase({from, key});
        if (to_edge_it->second.empty()) to_edges.erase(to_edge_it);
    }

    if (auto edge_type_it = edgeType.find(key); edge_type_it != edgeType.end()) {
        edge_type_it->second.erase({from, to});
        if (edge_type_it->second.empty()) edgeType.erase(edge_type_it);
    }
}

void DSRGraph::update_maps_edge_insert(uint64_t from, uint64_t to, const std::string &key)
{
    std::unique_lock<std::shared_mutex> lck(_mutex_cache_maps);

    edges[{from, to}].insert(key);
    to_edges[to].insert({from, key});
    edgeType[key].insert({from, to});
}

bool DSRGraph::is_attribute_ignored(const std::string& name) const
{
    return ignored_attributes.contains(name);
}

bool DSRGraph::is_node_deleted(uint64_t id) const
{
    std::shared_lock<std::shared_mutex> lck(_mutex_cache_maps);
    return deleted.contains(id);
}

void DSRGraph::for_each_incoming_edge(uint64_t to_id, std::function<void(uint64_t, const std::string&)> visitor) const
{
    std::shared_lock<std::shared_mutex> lck(_mutex_cache_maps);
    if (auto it = to_edges.find(to_id); it != to_edges.end()) {
        for (const auto& [from, type] : it->second) {
            visitor(from, type);
        }
    }
}

void DSRGraph::for_each_edge_of_type_cache(const std::string& type, std::function<void(uint64_t, uint64_t)> visitor) const
{
    std::shared_lock<std::shared_mutex> lck(_mutex_cache_maps);
    if (auto it = edgeType.find(type); it != edgeType.end()) {
        for (const auto& [from, to] : it->second) {
            visitor(from, to);
        }
    }
}

// NOTE: these run during a remote-delta apply (on the main thread, under unique_lock(_mutex)).
// defer_or_emit buffers each emit (args captured by value, produced here on main) so it fires AFTER
// the lock is released — see the deferred-emission note near the top of this file.
void DSRGraph::on_remote_node_updated(uint64_t id, const std::string& type, uint32_t agent_id)
{
    defer_or_emit([this, id, type, agent_id]{ emitter.update_node_signal(id, type, SignalInfo{agent_id}); });
}

void DSRGraph::on_remote_node_deleted(uint64_t id, const std::optional<Node>& node, const std::vector<Edge>& removed_edges, uint32_t agent_id)
{
    defer_or_emit([this, id, node, removed_edges, agent_id]{
        emitter.del_node_signal(id, SignalInfo{agent_id});
        if (node.has_value()) {
            emitter.deleted_node_signal(*node, SignalInfo{agent_id});
        }
        for (const auto& edge : removed_edges) {
            emitter.del_edge_signal(edge.from(), edge.to(), edge.type(), SignalInfo{agent_id});
            emitter.deleted_edge_signal(edge, SignalInfo{agent_id});
        }
    });
}

void DSRGraph::on_remote_edge_updated(uint64_t from, uint64_t to, const std::string& type, uint32_t agent_id)
{
    defer_or_emit([this, from, to, type, agent_id]{ emitter.update_edge_signal(from, to, type, SignalInfo{agent_id}); });
}

void DSRGraph::on_remote_edge_deleted(uint64_t from, uint64_t to, const std::string& type, const std::optional<Edge>& edge, uint32_t agent_id)
{
    defer_or_emit([this, from, to, type, edge, agent_id]{
        emitter.del_edge_signal(from, to, type, SignalInfo{agent_id});
        if (edge.has_value()) {
            emitter.deleted_edge_signal(*edge, SignalInfo{agent_id});
        }
    });
}

void DSRGraph::on_remote_node_attrs_updated(uint64_t id, const std::string& type, const std::vector<std::string>& attrs, uint32_t agent_id)
{
    defer_or_emit([this, id, type, attrs, agent_id]{
        emitter.update_node_attr_signal(id, attrs, SignalInfo{agent_id});
        emitter.update_node_signal(id, type, SignalInfo{agent_id});
    });
}

void DSRGraph::on_remote_edge_attrs_updated(uint64_t from, uint64_t to, const std::string& type, const std::vector<std::string>& attrs, uint32_t agent_id)
{
    defer_or_emit([this, from, to, type, attrs, agent_id]{
        emitter.update_edge_attr_signal(from, to, type, attrs, SignalInfo{agent_id});
        emitter.update_edge_signal(from, to, type, SignalInfo{agent_id});
    });
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
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return !engine_->get_node(id).has_value();
}

std::pair<bool, bool> DSRGraph::start_fullgraph_request_thread()
{
    CORTEX_PROFILE_MIN_N("DSRGraph::start_fullgraph_request_thread");
    return fullgraph_request_thread();
}

void DSRGraph::start_fullgraph_server_thread()
{
    CORTEX_PROFILE_MIN_N("DSRGraph::start_fullgraph_server_thread");
    fullgraph_server_thread();
}

void DSRGraph::start_subscription_threads()
{
    CORTEX_PROFILE_MIN_N("DSRGraph::start_subscription_threads");
    node_subscription_thread();
    edge_subscription_thread();
    node_attrs_subscription_thread();
    edge_attrs_subscription_thread();
}

void print_sample_info(DSR::GraphSettings::LOGLEVEL log_level, const DSR::Transport::ReceivedSampleInfo& info) {

    const double milliseconds_send = static_cast<double>(info.source_timestamp_ns) / 1000000.0;
    const double milliseconds_recv = static_cast<double>(info.reception_timestamp_ns) / 1000000.0;
    const double now = milliseconds_recv;

    std::ostringstream oss;
    oss << "SampleInfo:" << std::endl;
    oss << "  ms diff reception: " << (milliseconds_recv - milliseconds_send) << "ms" << std::endl;
    oss << "  ms taken from dds: " << (now - milliseconds_send) << "ms" << std::endl;
    oss << "  source entity: " << info.source_entity_id << std::endl;
    oss << "  Valid Data: " << (info.valid_data ? "true" : "false") << std::endl;
    qDebug() << QString::fromStdString(oss.str());
}


void DSRGraph::node_subscription_thread()
{
    CORTEX_PROFILE_MIN_N("DSRGraph::node_subscription_thread setup");
    if (sync_mode == SyncMode::LWW) {
        dsrparticipant.subscribe_node<LWWNodeMsg>(make_node_subscription_functor_impl<LWWNodeMsg>("LWW_NODE"), mtx_entity_creation);
    } else {
        dsrparticipant.subscribe_node<MvregNodeMsg>(make_node_subscription_functor_impl<MvregNodeMsg>("DSR_NODE", true), mtx_entity_creation);
    }
}

void DSRGraph::edge_subscription_thread()
{
    CORTEX_PROFILE_MIN_N("DSRGraph::edge_subscription_thread setup");
    if (sync_mode == SyncMode::LWW) {
        dsrparticipant.subscribe_edge<LWWEdgeMsg>(make_edge_subscription_functor_impl<LWWEdgeMsg>("LWW_EDGE"), mtx_entity_creation);
    } else {
        dsrparticipant.subscribe_edge<MvregEdgeMsg>(make_edge_subscription_functor_impl<MvregEdgeMsg>("DSR_EDGE"), mtx_entity_creation);
    }
}

void DSRGraph::edge_attrs_subscription_thread()
{
    CORTEX_PROFILE_MIN_N("DSRGraph::edge_attrs_subscription_thread setup");
    if (sync_mode == SyncMode::LWW) {
        dsrparticipant.subscribe_edge_attrs<LWWEdgeAttrVec>(make_edge_attrs_subscription_functor_impl<LWWEdgeAttrVec>("LWW_EDGE_ATTS"), mtx_entity_creation);
    } else {
        dsrparticipant.subscribe_edge_attrs<MvregEdgeAttrVec>(make_edge_attrs_subscription_functor_impl<MvregEdgeAttrVec>("DSR_EDGE_ATTS"), mtx_entity_creation);
    }
}

void DSRGraph::node_attrs_subscription_thread()
{
    CORTEX_PROFILE_MIN_N("DSRGraph::node_attrs_subscription_thread setup");
    if (sync_mode == SyncMode::LWW) {
        dsrparticipant.subscribe_node_attrs<LWWNodeAttrVec>(make_node_attrs_subscription_functor_impl<LWWNodeAttrVec>("LWW_NODE_ATTS"), mtx_entity_creation);
    } else {
        dsrparticipant.subscribe_node_attrs<MvregNodeAttrVec>(make_node_attrs_subscription_functor_impl<MvregNodeAttrVec>("DSR_NODE_ATTS"), mtx_entity_creation);
    }
}

void DSRGraph::fullgraph_server_thread()
{
    CORTEX_PROFILE_MIN_N("DSRGraph::fullgraph_server_thread setup");
    auto lambda_graph_request = [&](DSR::GraphRequest&& sample, const DSR::Transport::ReceivedSampleInfo& m_info)
    {
        [[maybe_unused]] static thread_local bool _named = []{ CORTEX_PROFILE_THREAD_NAME("fg_server"); return true; }();
        CORTEX_PROFILE_ZONE_N("DSRGraph::fullgraph_server_thread callback");
        if (log_level == GraphSettings::LOGLEVEL::DEBUGL) print_sample_info(log_level, m_info);
        if (!m_info.valid_data) {
            return;
        }
        if (!network_compatibility_or_fatal("GRAPH_REQUEST", sample.protocol_version, sample.sync_mode, sync_mode)) {
            return;
        }
        {
            std::unique_lock<std::mutex> lck(participant_set_mutex);
            if (auto [it, ok] = participant_set.emplace(sample.from, true);
                it->second && !ok)
            {
                if (it->second) {
                    lck.unlock();
                    // export_full_graph() serializes ALL of nodes_ (each node's fano Rb-tree included).
                    // It MUST exclude the delta-apply writers (unique_lock(_mutex)); without _mutex it
                    // races them on a peer's join -> corrupted fano tree -> later get_node() SEGV.
                    // It is a const read, so shared_lock is the correct (reader) lock.
                    std::shared_lock<std::shared_mutex> graph_lock(_mutex);
                    auto repeated_graph = engine_->export_full_graph();
                    std::visit([&](auto&& payload) {
                        using T = std::decay_t<decltype(payload)>;
                        T empty{};
                        empty.id = -1;
                        empty.to_id = static_cast<uint32_t>(sample.id);
                        empty.protocol_version = DSR::DSR_PROTOCOL_VERSION;
                        empty.sync_mode = sync_mode_wire_value(sync_mode);
                        dsrparticipant.publish_graph_answer(empty);
                    }, std::move(repeated_graph));
                    return;
                }
            } else {
                it->second = true;
                lck.unlock();
            }
        }
        if (static_cast<uint32_t>(sample.id) != agent_id ) {
            qDebug() << " Received Full Graph request: from " << m_info.source_entity_id;
            // Hold _mutex (shared) so the full-graph serialization can't race the delta-apply
            // writers (which hold unique_lock(_mutex)). See note at the other export_full_graph call.
            std::shared_lock<std::shared_mutex> graph_lock(_mutex);
            auto full_graph_message = engine_->export_full_graph();
            publish_full_graph_message(std::move(full_graph_message), static_cast<int32_t>(get_agent_id()));
            qDebug() << "Full graph written";
        }
    };
    dsrparticipant.subscribe_graph_requests<DSR::GraphRequest>(lambda_graph_request, mtx_entity_creation);

}

std::pair<bool, bool> DSRGraph::fullgraph_request_thread()
{
    CORTEX_PROFILE_MIN_N("DSRGraph::fullgraph_request_thread");
    // sync/repeated are shared_ptr, NOT stack locals: the GRAPH_ANSWER subscription below is PERMANENT and
    // its callback captures them by value, so they must outlive this function. Previously they were stack
    // atomics captured by reference — they dangled once this function returned, so a later peer's
    // GRAPH_ANSWER (on a FastDDS reader thread) ran `*sync = true` through the dangling reference, writing
    // 1 byte into freed/reused stack memory (the bit-48 wild write that randomly smashed cv::Mat headers
    // etc. under peer churn). (Fixed 2026-07-21.)
    auto state = std::make_shared<FullGraphJoinState>();
    auto& sync = state->sync;
    auto& repeated = state->repeated;
    if (sync_mode == SyncMode::LWW) {
        dsrparticipant.subscribe_graph_answers<LWWGraphSnapshot>(make_fullgraph_request_functor_impl<LWWGraphSnapshot>("LWW_GRAPH_ANSWER", state), mtx_entity_creation);
    } else {
        dsrparticipant.subscribe_graph_answers<OrMap>(make_fullgraph_request_functor_impl<OrMap>("GRAPH_ANSWER", state), mtx_entity_creation);
    }

    {
        CORTEX_PROFILE_DETAIL_N("DSRGraph::fullgraph_request_thread initial wait");
        std::this_thread::sleep_for(300ms);   // NEEDED ?
    }

    qDebug() << " Requesting the complete graph ";

    DSR::GraphRequest gr;
    gr.from = dsrparticipant.participant_name();
    gr.id = static_cast<int32_t>(agent_id);
    gr.protocol_version = DSR::DSR_PROTOCOL_VERSION;
    gr.sync_mode = sync_mode_wire_value(sync_mode);
    {
        CORTEX_PROFILE_DETAIL_N("DSRGraph::fullgraph_request_thread send request");
        dsrparticipant.publish_graph_request(gr);
    }


    bool timeout = false;
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    while (!sync and !timeout and !repeated) {
        CORTEX_PROFILE_DETAIL_N("DSRGraph::fullgraph_request_thread wait loop");
        std::this_thread::sleep_for(1000ms);
        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        timeout = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() > TIMEOUT * 3;
        qInfo() << " Waiting for the graph ... seconds to timeout ["
                << std::ceil(std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() / 10) / 100.0
                << "/" << TIMEOUT / 1000 * 3 << "] ";
        dsrparticipant.publish_graph_request(gr);
    }

    return { sync.load(), repeated.load() };
}

// The agent that loaded the graph from a file never read GRAPH_ANSWER. It now does, but only for the
// pushes addressed to it (resync on match): it is as exposed as anyone to a peer's early inserts.
void DSRGraph::addressed_fullgraph_subscription_thread()
{
    CORTEX_PROFILE_MIN_N("DSRGraph::addressed_fullgraph_subscription_thread setup");
    auto state = std::make_shared<FullGraphJoinState>();
    state->sync = true;                    // nothing to wait for: the graph is already here
    state->accept_server_answers = false;  // exactly as before: it does not import another server's answers
    if (sync_mode == SyncMode::LWW) {
        dsrparticipant.subscribe_graph_answers<LWWGraphSnapshot>(make_fullgraph_request_functor_impl<LWWGraphSnapshot>("LWW_GRAPH_ANSWER", state), mtx_entity_creation);
    } else {
        dsrparticipant.subscribe_graph_answers<OrMap>(make_fullgraph_request_functor_impl<OrMap>("GRAPH_ANSWER", state), mtx_entity_creation);
    }
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
    tp(1, "graph"),
    tp_delta_attr(1, "graph-attr")
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
