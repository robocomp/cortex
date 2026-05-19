#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
#include <fastdds/rtps/transport/shared_mem/SharedMemTransportDescriptor.hpp>
#include <fastdds/dds/core/detail/DDSReturnCode.hpp>
#include <fastdds/utils/IPFinder.hpp>

#include <QDebug>
#include <dsr/core/rtps/dsrparticipant.h>

using namespace eprosima::fastdds::dds;
using namespace eprosima::fastdds::rtps;

namespace {
std::vector<std::string> host_ipv4_interfaces()
{
    std::vector<std::string> ips{"127.0.0.1"};
    std::vector<IPFinder::info_IP> found;
    IPFinder::getIPs(&found, false);
    for (const auto& ip : found) {
        if (ip.type == IPFinder::IP4) {
            if (std::find(ips.begin(), ips.end(), ip.name) == ips.end()) {
                ips.push_back(ip.name);
            }
        }
    }
    return ips;
}
}

DSRParticipant::DSRParticipant() : mp_participant(nullptr),
                                   dsrgraphType(new MvregNodePubSubType()),
                                   graphrequestType(new GraphRequestPubSubType()),
                                   graphRequestAnswerType(new OrMapPubSubType()),
                                   dsrEdgeType(new MvregEdgePubSubType()),
                                   dsrNodeAttrType(new MvregNodeAttrVecPubSubType()),
                                   dsrEdgeAttrType(new MvregEdgeAttrVecPubSubType()),
                                   m_listener(nullptr)

{}

DSRParticipant::~DSRParticipant()
{

    remove_participant_and_entities();

    qDebug()  << "Removing DSRParticipant" ;

}

std::tuple<bool, eprosima::fastdds::dds::DomainParticipant*> DSRParticipant::init(uint32_t agent_id, const std::string& agent_name, int localhost, std::function<void(eprosima::fastdds::rtps::ParticipantDiscoveryStatus, const eprosima::fastdds::rtps::ParticipantBuiltinTopicData&)> fn, int8_t domain_id)
{
    domain_id_ = domain_id;
    // Create RTPSParticipant     
    DomainParticipantQos PParam;
    PParam.name(("Participant_" + std::to_string(agent_id)+ " ( " + agent_name + " )").data() );


    //Disable the built-in Transport Layer.
    PParam.transport().use_builtin_transports = false;

    if (localhost) {
        // Same-host deployments should prefer shared memory. Keep loopback UDP
        // as a discovery/data fallback for environments where SHM is limited.
        auto shm_transport = std::make_shared<SharedMemTransportDescriptor>();
        PParam.transport().user_transports.push_back(shm_transport);

        auto udp_transport = std::make_shared<UDPv4TransportDescriptor>();
        udp_transport->maxMessageSize = 65500;
        udp_transport->interface_allowlist.emplace_back("127.0.0.1");
        PParam.transport().user_transports.push_back(udp_transport);
    } else {
        auto udp_transport = std::make_shared<UDPv4TransportDescriptor>();
        udp_transport->maxMessageSize = 65500;
        for (const auto& ip : host_ipv4_interfaces()) {
            udp_transport->interface_allowlist.emplace_back(ip);
        }
        PParam.transport().user_transports.push_back(udp_transport);
    }

    PParam.transport().send_socket_buffer_size = 33554432;
    PParam.transport().listen_socket_buffer_size = 33554432;


    //Discovery
    /*PParam.wire_protocol().builtin.discovery_config.ignoreParticipantFlags =
            static_cast<eprosima::fastdds::rtps::ParticipantFilteringFlags>(
            eprosima::fastdds::rtps::ParticipantFilteringFlags::FILTER_SAME_PROCESS);*/

    PParam.wire_protocol().builtin.discovery_config.leaseDuration = /*eprosima::fastdds::c_TimeInfinite;*/ Duration_t(6);
    PParam.wire_protocol().builtin.discovery_config.leaseDuration_announcementperiod =
            eprosima::fastdds::dds::Duration_t(3, 0);

    eprosima::fastdds::dds::Log::SetVerbosity(eprosima::fastdds::dds::Log::Error);

    m_listener = std::make_unique<ParticpantListener>(std::move(fn));

    int retry = 0;
    while (retry < 5) {
        mp_participant = DomainParticipantFactory::get_instance()->create_participant(domain_id, PParam, m_listener.get(), StatusMask::none());
        if(mp_participant != nullptr) break;
        retry++;
        qDebug() << "Error creating participant, retrying. [" << retry <<"/5]";
    }


    if(mp_participant == nullptr)
    {
        qFatal("Could not create particpant after 5 attemps");
    }


    //Register types
    dsrgraphType.register_type(mp_participant);
    dsrgraphType.register_type(mp_participant);
    graphrequestType.register_type(mp_participant);
    graphRequestAnswerType.register_type(mp_participant);
    dsrEdgeType.register_type(mp_participant);
    dsrNodeAttrType.register_type(mp_participant);
    dsrEdgeAttrType.register_type(mp_participant);

    //Create topics
    topic_node = mp_participant->create_topic("DSR_NODE", dsrgraphType.get_type_name(), eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
    topic_edge = mp_participant->create_topic("DSR_EDGE", dsrEdgeType.get_type_name(), eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
    topic_node_att = mp_participant->create_topic("DSR_NODE_ATTS", dsrNodeAttrType.get_type_name(), eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
    topic_edge_att = mp_participant->create_topic("DSR_EDGE_ATTS", dsrEdgeAttrType.get_type_name(), eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
    topic_graph_request = mp_participant->create_topic("GRAPH_REQUEST", graphrequestType.get_type_name(), eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
    topic_graph = mp_participant->create_topic("GRAPH_ANSWER", graphRequestAnswerType.get_type_name(), eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);

    return std::make_tuple(true, mp_participant);
}

eprosima::fastdds::dds::DomainParticipant *DSRParticipant::getParticipant()
{
    return mp_participant;
}

void DSRParticipant::remove_participant_and_entities()
{
    if (mp_participant != nullptr)
    {

        {
            //std::unique_lock<std::recursive_mutex> lck (pub_mtx);
            for (auto &[topic_name, ptr] : publishers) {
                //std::cout << "REMOVE PUB " << topic_name << std::endl;
                auto[pub, writer] = ptr;
                if (writer != nullptr && pub != nullptr) {
                    writer->close();
                    pub->close();


                    writer->set_listener(nullptr);
                    pub->set_listener(nullptr);

                    if (auto res = pub->delete_datawriter(writer); res == 0)
                    {
                        if (res = mp_participant->delete_publisher(pub); res != 0)
                        {
                            std::cout << "DELETE PUBLISHER " << topic_name << " RETURNED: " << res << std::endl;
                        }
                    }
                    else {
                        std::cout << "DELETE DATAWRITER " << topic_name << " RETURNED: " << res << std::endl;
                    }
                }
            }
            publishers.clear();
        }

        {
            //std::unique_lock<std::recursive_mutex> lck (sub_mtx);
            for (auto &[topic_name, ptr] : subscribers) {
                //std::cout << "REMOVE SUB " << topic_name << std::endl;
                auto[sub, reader] = ptr;
                if (reader != nullptr && sub != nullptr) {
                    reader->close();
                    sub->close();


                    reader->set_listener(nullptr);
                    sub->set_listener(nullptr);

                    if (auto res = sub->delete_datareader(reader); res == 0)
                    {
                        if (res = mp_participant->delete_subscriber(sub); res != 0)
                        {
                            std::cout << "DELETE SUBSCRIBER " << topic_name << " RETURNED: " << res << std::endl;
                        }
                    }
                    else {
                        std::cout << "DELETE DATAREADER " << topic_name << " RETURNED: " << res << std::endl;
                    }
                }
            }
            subscribers.clear();
        }

        if (topic_node)
        {
            topic_node->close();
            if(mp_participant->delete_topic(topic_node) == RETCODE_PRECONDITION_NOT_MET)
            {
                std::cout << " Remove topic error " << topic_node->get_name() << std::endl;
            }
        }
        if (topic_edge)
        {
            topic_edge->close();
            if(mp_participant->delete_topic(topic_edge) == RETCODE_PRECONDITION_NOT_MET)
            {
                std::cout << " Remove topic error " << topic_edge->get_name() << std::endl;
            }
        }
        if (topic_graph)
        {
            topic_graph->close();
            if(mp_participant->delete_topic(topic_graph) == RETCODE_PRECONDITION_NOT_MET)
            {
                std::cout << " Remove topic error " << topic_graph->get_name() << std::endl;
            }
        }
        if (topic_graph_request)
        {
            topic_graph_request->close();
            if(mp_participant->delete_topic(topic_graph_request) == RETCODE_PRECONDITION_NOT_MET)
            {
                std::cout << " Remove topic error " << topic_graph_request->get_name() << std::endl;
            }
        }
        if (topic_node_att)
        {
            topic_node_att->close();
            if(mp_participant->delete_topic(topic_node_att) == RETCODE_PRECONDITION_NOT_MET)
            {
                std::cout << " Remove topic error " << topic_node_att->get_name() << std::endl;
            }
        }
        if (topic_edge_att)
        {
            topic_edge_att->close();
            if(mp_participant->delete_topic(topic_edge_att) == RETCODE_PRECONDITION_NOT_MET)
            {
                std::cout << " Remove topic error " << topic_edge_att->get_name() << std::endl;
            }

        }

        auto res = eprosima::fastdds::dds::DomainParticipantFactory::get_instance()->delete_participant(mp_participant);
        if (res == RETCODE_PRECONDITION_NOT_MET) {
            std::cout << "Error removing participant. There are entities in use." <<std::endl;
        }
        mp_participant = nullptr;
    }
}

const eprosima::fastdds::rtps::GUID_t& DSRParticipant::getID() const
{
    return mp_participant->guid();
}

void DSRParticipant::add_subscriber(const std::string& id, std::pair<eprosima::fastdds::dds::Subscriber*, eprosima::fastdds::dds::DataReader*> val)
{
    std::unique_lock<std::mutex> lck (sub_mtx);
    subscribers.emplace(id, val);
}
void DSRParticipant::add_publisher(const std::string& id, std::pair<eprosima::fastdds::dds::Publisher*, eprosima::fastdds::dds::DataWriter*> val)
{
    std::unique_lock<std::mutex> lck (pub_mtx);
    publishers.emplace(id, val);
}

void DSRParticipant::delete_subscriber(const std::string& id)
{
    std::unique_lock<std::mutex> lck (sub_mtx);
    try {
        auto[sub, reader] = subscribers.at(id);
        if (mp_participant != nullptr)
        {
            if (reader != nullptr && sub != nullptr)
            {
                reader->close();
                sub->close();

                reader->set_listener(nullptr);
                sub->set_listener(nullptr);
                if (auto res = sub->delete_datareader(reader); res == 0)
                {
                    if (res = mp_participant->delete_subscriber(sub); res != 0)
                    {
                        std::cout << "DELETE SUBSCRIBER " << id << " RETURNED: " << res << std::endl;
                    } else {
                        subscribers.erase(id);
                    }
                }
                else {
                    std::cout << "DELETE DATAREADER " << id << " RETURNED: " << res << std::endl;
                }
            }
        }
    } catch (...) {
        std::cout << "delete sub error: " << id << std::endl;
    }
}

void DSRParticipant::delete_publisher(const std::string& id)
{
    std::unique_lock<std::mutex> lck (pub_mtx);
    try {
        auto[pub, writer] = publishers.at(id);
        if (mp_participant != nullptr)
        {
            if (writer != nullptr && pub != nullptr)
            {
                writer->close();
                pub->close();

                writer->set_listener(nullptr);
                pub->set_listener(nullptr);

                if (auto res = pub->delete_datawriter(writer); res == 0)
                {
                    if (res = mp_participant->delete_publisher(pub); res != 0)
                    {
                        std::cout << "DELETE PUBLISHER " << id << " RETURNED: " << res << std::endl;
                    } else {
                        publishers.erase(id);
                    }
                }
                else {
                    std::cout << "DELETE DATAWRITER " << id << " RETURNED: " << res << std::endl;
                }
            }
        }
    } catch (...) {
        std::cout << "delete pub error: " << id << std::endl;
    }
}
