#ifndef _PARTICIPANT_H_
#define _PARTICIPANT_H_

#include <fastdds/dds/log/Log.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <fastdds/rtps/builtin/data/ParticipantBuiltinTopicData.hpp>

#include <dsr/core/rtps/CRDTPubSubTypes.h>
#include <dsr/core/types/internal_types.h>
#include <dsr/core/rtps/dsrpublisher.h>
#include <dsr/core/rtps/dsrsubscriber.h>
#include <dsr/core/transport/transport_crtp.h>

class DSRParticipant : public DSR::Transport::ParticipantTransportCRTP<DSRParticipant>
{
public:
    DSRParticipant();
    virtual ~DSRParticipant();
    [[nodiscard]] std::tuple<bool, eprosima::fastdds::dds::DomainParticipant *> init_impl(uint32_t agent_id, const std::string& agent_name, int localhost, std::function<void(eprosima::fastdds::rtps::ParticipantDiscoveryStatus, const eprosima::fastdds::rtps::ParticipantBuiltinTopicData&)> fn, int8_t domain_id=0, uint8_t sync_mode_wire = 0);
    [[nodiscard]] int8_t get_domain_id() const { return domain_id_; }
    [[nodiscard]] uint8_t get_sync_mode_wire() const { return sync_mode_wire_; }
    [[nodiscard]] const eprosima::fastdds::rtps::GUID_t& getID() const;
    [[nodiscard]] const char *getNodeTopicName()     const { return dsrgraphType->get_name().data();}
    [[nodiscard]] const char *getRequestTopicName()  const { return graphrequestType->get_name().data();}
    [[nodiscard]] const char *getAnswerTopicName()   const { return graphRequestAnswerType->get_name().data();}
    [[nodiscard]] const char *getEdgeTopicName()     const { return dsrEdgeType->get_name().data();}
    [[nodiscard]] const char *getNodeAttrTopicName() const { return dsrNodeAttrType->get_name().data();}
    [[nodiscard]] const char *getEdgeAttrTopicName() const { return dsrEdgeAttrType->get_name().data();}

    [[nodiscard]] eprosima::fastdds::dds::Topic*  getNodeTopic()          { return topic_node; }
    [[nodiscard]] eprosima::fastdds::dds::Topic*  getEdgeTopic()          { return topic_edge; }
    [[nodiscard]] eprosima::fastdds::dds::Topic*  getGraphTopic()         { return topic_graph;}
    [[nodiscard]] eprosima::fastdds::dds::Topic*  getGraphRequestTopic()  { return topic_graph_request;}
    [[nodiscard]] eprosima::fastdds::dds::Topic*  getAttNodeTopic()       { return topic_node_att;}
    [[nodiscard]] eprosima::fastdds::dds::Topic*  getAttEdgeTopic()       { return topic_edge_att;}
    [[nodiscard]] eprosima::fastdds::dds::DomainParticipant *getParticipant();

    void add_subscriber_impl(const std::string& id, std::pair<eprosima::fastdds::dds::Subscriber*, eprosima::fastdds::dds::DataReader*>);
    void add_publisher_impl(const std::string& id, std::pair<eprosima::fastdds::dds::Publisher*, eprosima::fastdds::dds::DataWriter*>);
    void delete_subscriber_impl(const std::string& id);
    void delete_publisher_impl(const std::string& id);

    void remove_participant_and_entities_impl();

private:
    int8_t domain_id_ {0};
    uint8_t sync_mode_wire_ {0};
    bool cleanup_enabled_ {true};
    eprosima::fastdds::dds::DomainParticipant* mp_participant{};

    eprosima::fastdds::dds::Topic*  topic_node{};
    eprosima::fastdds::dds::Topic*  topic_edge{};
    eprosima::fastdds::dds::Topic*  topic_graph{};
    eprosima::fastdds::dds::Topic*  topic_graph_request{};
    eprosima::fastdds::dds::Topic*  topic_node_att{};
    eprosima::fastdds::dds::Topic*  topic_edge_att{};

    eprosima::fastdds::dds::TypeSupport dsrgraphType{};
    eprosima::fastdds::dds::TypeSupport graphrequestType{};
    eprosima::fastdds::dds::TypeSupport graphRequestAnswerType{};
    eprosima::fastdds::dds::TypeSupport dsrEdgeType{};
    eprosima::fastdds::dds::TypeSupport dsrNodeAttrType{};
    eprosima::fastdds::dds::TypeSupport dsrEdgeAttrType{};

    std::map<std::string, std::pair<eprosima::fastdds::dds::Subscriber*, eprosima::fastdds::dds::DataReader*>> subscribers;
    std::map<std::string, std::pair<eprosima::fastdds::dds::Publisher*, eprosima::fastdds::dds::DataWriter*>> publishers;
    mutable std::mutex pub_mtx;
    mutable std::mutex sub_mtx;

    class ParticpantListener : public eprosima::fastdds::dds::DomainParticipantListener
    {
    public:
        explicit ParticpantListener(std::function<void(eprosima::fastdds::rtps::ParticipantDiscoveryStatus,
                                                       const eprosima::fastdds::rtps::ParticipantBuiltinTopicData&)>&& fn)
            : eprosima::fastdds::dds::DomainParticipantListener(), f(std::move(fn)){};
        ~ParticpantListener() override = default;

         void on_participant_discovery  (
                eprosima::fastdds::dds::DomainParticipant* participant,
                eprosima::fastdds::rtps::ParticipantDiscoveryStatus status,
                const eprosima::fastdds::rtps::ParticipantBuiltinTopicData& info,
                bool& should_be_ignored) override
        {
            //Callback
            f(status, info);
        }


        std::function<void(eprosima::fastdds::rtps::ParticipantDiscoveryStatus,
                           const eprosima::fastdds::rtps::ParticipantBuiltinTopicData&)> f;
        //int n_matched;
    };
    std::unique_ptr<ParticpantListener> m_listener;
};

#endif // _Participant_H_
