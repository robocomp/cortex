//
// Created by jc on 19/09/22.
//

#include "dsr/core/transport/dsr_transport.h"

#include <dsr/api/dsr_api.h>
#include <dsr/api/dsr_core_api.h>
#include <dsr/api/dsr_signal_info.h>
#include <dsr/api/dsr_transport_fastdds.h>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <new>
#include <shared_mutex>

using namespace std::literals;
using namespace DSR;

FastDDSTransport::FastDDSTransport() : tp(5), tp_delta_attr(2) {}

auto FastDDSTransport::set_transport(std::weak_ptr<Transport> ptr) -> void
{
    transport = ptr;
}

auto FastDDSTransport::stop() -> void
{
    participant.remove_participant_and_entities();
}

//////////////////////////////////////////////////
///// Topics
/////////////////////////////////////////////////
auto FastDDSTransport::start_fullgraph_request(Graph *graph) -> std::pair<bool, bool>
{
    bool sync = false;
    bool repeated = false;
    auto lambda_request_answer = [&](DDSReader *reader, Graph *graph)
    {
        while (true)
        {
            eprosima::fastdds::dds::SampleInfo m_info;
            GraphInfoTuple sample;
            if (reader->take_next_sample(&sample, &m_info) == ReturnCode_t::RETCODE_OK)
            {
                if (m_info.instance_state == eprosima::fastdds::dds::ALIVE_INSTANCE_STATE)
                {
                    if (sample.id() != graph->get_agent_id())
                    {
                        if (sample.id() != static_cast<uint32_t>(-1))
                        {
                            std::cout << " Received Full Graph from "
                                      << m_info.sample_identity.writer_guid().entityId.value << " whith "
                                      << sample.m().size() << " elements\n";
                            tp.spawn_task(&Graph::join_full_graph, static_cast<Graph *>(graph), std::move(sample));
                            std::cout << "Synchronized.\n";
                            sync = true;
                            break;
                        }
                        else if (!sync && sample.to_id() == graph->get_agent_id())
                        {
                            repeated = true;
                        }
                    }
                }
            }
            else
            {
                break;
            }
        }
    };

    DDSPubSub pub_sub;

    auto [res, sub, reader] = pub_sub.dsr_sub.init(participant.getParticipant(), participant.getGraphTopic(),
                                                   NewMessageFn(graph, lambda_request_answer), mtx_entity_creation);
    participant.add_subscriber(participant.getGraphTopic()->get_name(), {sub, reader});

    std::this_thread::sleep_for(300ms);  // NEEDED ?

    std::cout << " Requesting the complete graph \n";

    GraphRequestTuple gr;
    gr.from(participant.getParticipant()->get_qos().name().to_string());
    gr.id(graph->get_agent_id());
    write_request(&gr);

    bool timeout = false;
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    while (!sync and !timeout and !repeated)
    {
        std::this_thread::sleep_for(1000ms);
        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        timeout = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() > TIMEOUT * 3;
        std::cout << " Waiting for the graph ... seconds to timeout ["
                  << std::ceil(std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() / 10) / 100.0
                  << "/" << TIMEOUT / 1000 * 3 << "] \n";
        write_request(&gr);
    }

    participant.delete_publisher(participant.getGraphRequestTopic()->get_name());
    participant.delete_subscriber(participant.getGraphTopic()->get_name());

    return {sync, repeated};
}

auto FastDDSTransport::start_fullgraph_server(Graph *graph) -> void
{
    auto lambda_graph_request = [&](DDSReader *reader, Graph *graph)
    {
        while (true)
        {
            eprosima::fastdds::dds::SampleInfo m_info;
            GraphRequestTuple sample;
            if (reader->take_next_sample(&sample, &m_info) == ReturnCode_t::RETCODE_OK)
            {
                if (m_info.instance_state == eprosima::fastdds::dds::ALIVE_INSTANCE_STATE)
                {
                    {
                        auto tr = transport.lock();
                        if (!tr)
                        {
                            std::cout << "Transport is null. Aborting\n";
                            std::terminate();
                        }
                        std::unique_lock<std::mutex> lck(tr->participant_set_mutex);
                        if (auto [it, ok] = tr->participant_set.emplace(sample.from(), true); it->second and !ok)
                        {
                            if (it->second)
                            {
                                lck.unlock();
                                GraphInfoTuple mp;
                                mp.id(-1);
                                mp.to_id(sample.id());
                                graph_server.dsr_pub.write(&mp);
                                continue;
                            }
                            else
                            {
                            }
                        }
                        else
                        {
                            it->second = true;
                            lck.unlock();
                        }
                    }
                    if (static_cast<uint32_t>(sample.id()) != graph->get_agent_id())
                    {

                        std::cout << " Received Full Graph request: from "
                                  << m_info.sample_identity.writer_guid().entityId.value << "\n";
                        GraphInfoTuple mp;
                        mp.id(graph->get_agent_id());
                        mp.m(graph->copy_map());
                        graph_server.dsr_pub.write(&mp);

                        std::cout << "Full graph written\n";
                    }
                }
            }
            else
            {
                break;
            }
        }
    };

    auto [res, sub, reader] =
        graph_server.dsr_sub.init(participant.getParticipant(), participant.getGraphRequestTopic(),
                                  NewMessageFn(graph, lambda_graph_request), mtx_entity_creation);
    participant.add_subscriber(participant.getGraphRequestTopic()->get_name(), {sub, reader});
}

auto FastDDSTransport::start_subs(Graph *graph, bool show) -> void
{
    auto delta_node_thread =
        std::async(std::launch::async, &FastDDSTransport::start_node_subscription, this, graph, show);
    auto delta_edge_thread =
        std::async(std::launch::async, &FastDDSTransport::start_edge_subscription, this, graph, show);
    auto delta_node_attrs_thread =
        std::async(std::launch::async, &FastDDSTransport::start_node_attrs_subscription, this, graph, show);
    auto delta_edge_attrs_thread =
        std::async(std::launch::async, &FastDDSTransport::start_edge_attrs_subscription, this, graph, show);

    delta_node_thread.get();
    delta_edge_thread.get();
    delta_node_attrs_thread.get();
    delta_edge_attrs_thread.get();
}

auto FastDDSTransport::start_pubs(Graph *graph, bool show) -> void
{
    [[maybe_unused]] auto _ = participant.init(
        graph->get_agent_id(), graph->config.agent_name, graph->config.localhost,
        [&](eprosima::fastrtps::rtps::ParticipantDiscoveryInfo &&info)
        {
            if (info.status == eprosima::fastrtps::rtps::ParticipantDiscoveryInfo::DISCOVERED_PARTICIPANT)
            {
                auto tr = transport.lock();
                std::unique_lock<std::mutex> lck(tr->participant_set_mutex);
                std::cout << "Participant matched [" << info.info.m_participantName.to_string() << "]" << std::endl;
                tr->participant_set.insert({info.info.m_participantName.to_string(), false});
            }
            else if (info.status == eprosima::fastrtps::rtps::ParticipantDiscoveryInfo::REMOVED_PARTICIPANT ||
                        info.status == eprosima::fastrtps::rtps::ParticipantDiscoveryInfo::DROPPED_PARTICIPANT)
            {
                auto tr = transport.lock();
                std::unique_lock<std::mutex> lck(tr->participant_set_mutex);
                tr->participant_set.erase(info.info.m_participantName.to_string());
                std::cout << "Participant unmatched [" << info.info.m_participantName.to_string() << "]" << std::endl;
                // graph->delete_node(info.info.m_participantName.to_string()); TODO: delete this node.
            }
        });

    auto [res, pub, writer] = node.dsr_pub.init(participant.getParticipant(), participant.getNodeTopic());
    auto [res2, pub2, writer2] =
        node_attribute.dsr_pub.init(participant.getParticipant(), participant.getAttNodeTopic());

    auto [res3, pub3, writer3] = edge.dsr_pub.init(participant.getParticipant(), participant.getEdgeTopic());
    auto [res4, pub4, writer4] =
        edge_attribute.dsr_pub.init(participant.getParticipant(), participant.getAttEdgeTopic());

    auto [res5, pub5, writer5] =
        graph_request.dsr_pub.init(participant.getParticipant(), participant.getGraphRequestTopic());
    auto [res6, pub6, writer6] = graph_request.dsr_pub.init(participant.getParticipant(), participant.getGraphTopic());

    participant.add_publisher(participant.getNodeTopic()->get_name(), {pub, writer});
    participant.add_publisher(participant.getAttNodeTopic()->get_name(), {pub2, writer2});
    participant.add_publisher(participant.getEdgeTopic()->get_name(), {pub3, writer3});
    participant.add_publisher(participant.getAttEdgeTopic()->get_name(), {pub4, writer4});
    participant.add_publisher(participant.getGraphRequestTopic()->get_name(), {pub5, writer5});
    participant.add_publisher(participant.getGraphTopic()->get_name(), {pub6, writer6});
}

auto FastDDSTransport::start_node_subscription(Graph *graph, bool show) -> void
{
    auto name = __FUNCTION__;
    auto lambda_general_topic = [&, name = name, showReceived = show](DDSReader *reader, Graph *graph)
    {
        try
        {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo m_info;
                NodeInfoTuple sample;
                if (reader->take_next_sample(&sample, &m_info) == ReturnCode_t::RETCODE_OK)
                {
                    if (m_info.instance_state == eprosima::fastdds::dds::ALIVE_INSTANCE_STATE)
                    {
                        if (sample.agent_id() != graph->get_agent_id())
                        {
                            if (showReceived)
                            {
                                std::cout << name << " Received:" << std::to_string(sample.id()).c_str()
                                          << " node from: " << m_info.sample_identity.writer_guid().entityId.value
                                          << "\n";
                            }
                            tp.spawn_task(&Graph::join_delta_node, static_cast<Graph *>(graph), std::move(sample));
                        }
                    }
                }
                else
                {
                    break;
                }
            }
        }
        catch (const std::exception &ex)
        {
            std::cerr << ex.what() << std::endl;
        }
    };

    auto [res, sub, reader] = node.dsr_sub.init(participant.getParticipant(), participant.getNodeTopic(),
                                                NewMessageFn(graph, lambda_general_topic), mtx_entity_creation);
    participant.add_subscriber(participant.getNodeTopic()->get_name(), {sub, reader});
}

auto FastDDSTransport::start_edge_subscription(Graph *graph, bool show) -> void
{
    auto name = __FUNCTION__;
    auto lambda_general_topic = [&, name = name, showReceived = show](DDSReader *reader, Graph *graph)
    {
        try
        {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo m_info;
                EdgeInfoTuple sample;
                if (reader->take_next_sample(&sample, &m_info) == ReturnCode_t::RETCODE_OK)
                {
                    if (m_info.instance_state == eprosima::fastdds::dds::ALIVE_INSTANCE_STATE)
                    {
                        if (sample.agent_id() != graph->get_agent_id())
                        {
                            if (showReceived)
                            {
                                std::cout << name << " Received:" << std::to_string(sample.id()).c_str()
                                          << " node from: " << m_info.sample_identity.writer_guid().entityId.value
                                          << "\n";
                            }
                            tp.spawn_task(&Graph::join_delta_edge, static_cast<Graph *>(graph), std::move(sample));
                        }
                    }
                }
                else
                {
                    break;
                }
            }
        }
        catch (const std::exception &ex)
        {
            std::cerr << ex.what() << std::endl;
        }
    };
    auto [res, sub, reader] = edge.dsr_sub.init(participant.getParticipant(), participant.getEdgeTopic(),
                                                NewMessageFn(graph, lambda_general_topic), mtx_entity_creation);
    participant.add_subscriber(participant.getEdgeTopic()->get_name(), {sub, reader});
}

auto FastDDSTransport::start_node_attrs_subscription(Graph *graph, bool show) -> void
{
    auto name = __FUNCTION__;
    auto lambda_general_topic = [this, name = name, showReceived = show](DDSReader *reader, Graph *graph)
    {
        try
        {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo m_info;
                NodeAttributeVecTupleVec samples;
                if (reader->take_next_sample(&samples, &m_info) == ReturnCode_t::RETCODE_OK)
                {
                    if (m_info.instance_state == eprosima::fastdds::dds::ALIVE_INSTANCE_STATE)
                    {
                        if (showReceived)
                        {
                            std::cout << name << " Received:" << samples.vec().size()
                                      << " node attrs from: " << m_info.sample_identity.writer_guid().entityId.value
                                      << "\n";
                        }
                        if (!samples.vec().empty() and samples.vec().at(0).agent_id() != graph->get_agent_id())
                        {
                            tp_delta_attr.spawn_task(
                                [this, graph, samples = std::move(samples)]() mutable
                                {
                                    if (samples.vec().empty()) return;

                                    auto id = samples.vec().at(0).id();
                                    std::string type;
                                    {
                                        // TODO: replace with get_node_type()
                                        std::shared_lock<std::shared_mutex> lock(graph->_mutex_data);
                                        if (auto itn = graph->nodes.find(id); itn != graph->nodes.end())
                                            type = itn->second.read_reg().type();
                                    }
                                    std::vector<std::future<std::optional<std::string>>> futures;
                                    for (auto &&s : samples.vec())
                                    {
                                        if (graph->get_ignored_attributes().find(s.attr_name().data()) ==
                                            graph->get_ignored_attributes().end())
                                        {
                                            futures.emplace_back(tp.spawn_task_waitable(
                                                [graph, samp{std::move(s)}]() mutable
                                                {
                                                    auto f = graph->join_delta_node_attr(std::move(samp));
                                                    return f;
                                                }));
                                        }
                                    }

                                    std::vector<std::string> sig(futures.size());
                                    for (auto &f : futures)
                                    {
                                        auto opt_str = f.get();
                                        if (opt_str.has_value()) sig.emplace_back(std::move(opt_str.value()));
                                    }

                                    emit graph->get_config().dsr->update_node_attr_signal(
                                        id, sig, SignalInfo{samples.vec().at(0).agent_id()});
                                    emit graph->get_config().dsr->update_node_signal(
                                        id, type, SignalInfo{samples.vec().at(0).agent_id()});
                                });
                        }
                    }
                }
                else
                {
                    break;
                }
            }
        }
        catch (const std::exception &ex)
        {
            std::cerr << ex.what() << std::endl;
        }
    };

    auto [res, sub, reader] =
        node_attribute.dsr_sub.init(participant.getParticipant(), participant.getAttNodeTopic(),
                                    NewMessageFn(graph, lambda_general_topic), mtx_entity_creation);
    participant.add_subscriber(participant.getAttNodeTopic()->get_name(), {sub, reader});
}

auto FastDDSTransport::start_edge_attrs_subscription(Graph *graph, bool show) -> void
{
    auto name = __FUNCTION__;
    auto lambda_general_topic = [&, name = name, showReceived = show](DDSReader *reader, Graph *graph)
    {
        try
        {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo m_info;
                EdgeAttributeVecTupleVec samples;
                if (reader->take_next_sample(&samples, &m_info) == ReturnCode_t::RETCODE_OK)
                {
                    if (m_info.instance_state == eprosima::fastdds::dds::ALIVE_INSTANCE_STATE)
                    {
                        if (showReceived)
                        {
                            std::cout << name << " Received:" << samples.vec().size()
                                      << " edge attr from: " << m_info.sample_identity.writer_guid().entityId.value
                                      << "\n";
                        }
                        if (!samples.vec().empty() and samples.vec().at(0).agent_id() != graph->get_agent_id())
                        {
                            tp_delta_attr.spawn_task(
                                [this, graph, samples = std::move(samples)]() mutable
                                {
                                    if (samples.vec().empty()) return;

                                    auto from = samples.vec().at(0).from();
                                    auto to = samples.vec().at(0).to();
                                    auto type = samples.vec().at(0).type();

                                    std::vector<std::future<std::optional<std::string>>> futures;

                                    for (auto &&sample : samples.vec())
                                    {
                                        if (!graph->get_ignored_attributes().contains(sample.attr_name().data()))
                                        {
                                            futures.emplace_back(tp.spawn_task_waitable(
                                                [graph, sample = std::move(sample)]() mutable
                                                { return graph->join_delta_edge_attr(std::move(sample)); }));
                                        }
                                    }

                                    std::vector<std::string> sig(futures.size());
                                    for (auto &f : futures)
                                    {
                                        auto opt_str = f.get();
                                        if (opt_str.has_value()) sig.emplace_back(std::move(opt_str.value()));
                                    }

                                    emit graph->get_config().dsr->update_edge_attr_signal(
                                        from, to, type, sig, SignalInfo{samples.vec().at(0).agent_id()});
                                    emit graph->get_config().dsr->update_edge_signal(
                                        from, to, type, SignalInfo{samples.vec().at(0).agent_id()});
                                });
                        }
                    }
                }
                else
                {
                    break;
                }
            }
        }
        catch (const std::exception &ex)
        {
            std::cerr << ex.what() << std::endl;
        }
    };
    auto [res, sub, reader] =
        edge_attribute.dsr_sub.init(participant.getParticipant(), participant.getAttEdgeTopic(),
                                    NewMessageFn(graph, lambda_general_topic), mtx_entity_creation);
    participant.add_subscriber(participant.getAttEdgeTopic()->get_name(), {sub, reader});
    // dsrsub_edge_attrs_stream.init(participant.getParticipant(), "DSR_EDGE_ATTRS_STREAM",
    // participant.getEdgeAttrTopicName(),
    //                        dsrpub_call_edge_attrs, true);
}

//////////////////////////////////////////////////
///// write
/////////////////////////////////////////////////

auto FastDDSTransport::write_node(NodeInfoTuple *_node) -> bool
{
    return node.dsr_pub.write(_node);
}

auto FastDDSTransport::write_edge(EdgeInfoTuple *_edge) -> bool
{
    return edge.dsr_pub.write(_edge);
}

auto FastDDSTransport::write_node_attributes(std::vector<DSR::CRDTAttribute> &&attributes, uint64_t node_id, uint64_t timestamp, uint32_t agent_id) -> bool
{
    return node_attribute.dsr_pub.write(attributes);
}

auto FastDDSTransport::write_edge_attributes(EdgeAttributeVecTuple *attributes) -> bool
{
    return edge_attribute.dsr_pub.write(attributes);
}

auto FastDDSTransport::write_graph(std::map<uint64_t, DSR::CRDTNode> &&graph, std::vector<uint64_t> deleted_nodes) -> bool
{
    return graph_server.dsr_pub.write(map);
}

auto FastDDSTransport::write_request(std::string& agent_name, uint32_t agent_id) -> bool
{
    return graph_request.dsr_pub.write(request);
}
