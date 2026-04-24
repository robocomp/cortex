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

class DSRParticipant : public DSR::Transport::ParticipantTransportCRTP<
    DSRParticipant,
    eprosima::fastdds::dds::DomainParticipant,
    std::pair<eprosima::fastdds::dds::Subscriber*, eprosima::fastdds::dds::DataReader*>,
    std::pair<eprosima::fastdds::dds::Publisher*, eprosima::fastdds::dds::DataWriter*>,
    std::function<void(DSR::Transport::ParticipantDiscoveryStatus, const DSR::Transport::ParticipantDiscoveryInfo&)>>
{
public:
    DSRParticipant();
    virtual ~DSRParticipant();
    [[nodiscard]] std::tuple<bool, participant_handle_type *> init_impl(uint32_t agent_id, const std::string& agent_name, int localhost, discovery_callback_type fn, int8_t domain_id=0, uint8_t sync_mode_wire = 0);
    [[nodiscard]] int8_t get_domain_id() const { return domain_id_; }
    [[nodiscard]] uint8_t get_sync_mode_wire() const { return sync_mode_wire_; }
    [[nodiscard]] const eprosima::fastdds::rtps::GUID_t& getID() const;
    [[nodiscard]] const char *getNodeTopicName()     const { return dsrgraphType->get_name().data();}
    [[nodiscard]] const char *getRequestTopicName()  const { return graphrequestType->get_name().data();}
    [[nodiscard]] const char *getAnswerTopicName()   const { return graphRequestAnswerType->get_name().data();}
    [[nodiscard]] const char *getEdgeTopicName()     const { return dsrEdgeType->get_name().data();}
    [[nodiscard]] const char *getNodeAttrTopicName() const { return dsrNodeAttrType->get_name().data();}
    [[nodiscard]] const char *getEdgeAttrTopicName() const { return dsrEdgeAttrType->get_name().data();}
    [[nodiscard]] std::string participant_name() const;

    [[nodiscard]] eprosima::fastdds::dds::Topic*  getNodeTopic()          { return topic_node; }
    [[nodiscard]] eprosima::fastdds::dds::Topic*  getEdgeTopic()          { return topic_edge; }
    [[nodiscard]] eprosima::fastdds::dds::Topic*  getGraphTopic()         { return topic_graph;}
    [[nodiscard]] eprosima::fastdds::dds::Topic*  getGraphRequestTopic()  { return topic_graph_request;}
    [[nodiscard]] eprosima::fastdds::dds::Topic*  getAttNodeTopic()       { return topic_node_att;}
    [[nodiscard]] eprosima::fastdds::dds::Topic*  getAttEdgeTopic()       { return topic_edge_att;}
    [[nodiscard]] eprosima::fastdds::dds::DomainParticipant *getParticipant();

    void add_subscriber_impl(const std::string& id, subscriber_entry_type);
    void add_publisher_impl(const std::string& id, publisher_entry_type);
    void delete_subscriber_impl(const std::string& id);
    void delete_publisher_impl(const std::string& id);

    bool init_builtin_publishers();

    template <typename Sample, typename Callback>
    bool subscribe_node(Callback&& callback, std::mutex& mtx)
    {
        return subscribe_impl<Sample>("node", sub_node_, topic_node, std::forward<Callback>(callback), mtx);
    }

    template <typename Sample, typename Callback>
    bool subscribe_edge(Callback&& callback, std::mutex& mtx)
    {
        return subscribe_impl<Sample>("edge", sub_edge_, topic_edge, std::forward<Callback>(callback), mtx);
    }

    template <typename Sample, typename Callback>
    bool subscribe_node_attrs(Callback&& callback, std::mutex& mtx)
    {
        return subscribe_impl<Sample>("node_attrs", sub_node_attrs_, topic_node_att, std::forward<Callback>(callback), mtx);
    }

    template <typename Sample, typename Callback>
    bool subscribe_edge_attrs(Callback&& callback, std::mutex& mtx)
    {
        return subscribe_impl<Sample>("edge_attrs", sub_edge_attrs_, topic_edge_att, std::forward<Callback>(callback), mtx);
    }

    template <typename Sample, typename Callback>
    bool subscribe_graph_requests(Callback&& callback, std::mutex& mtx)
    {
        return subscribe_impl<Sample>("graph_request", sub_graph_request_, topic_graph_request, std::forward<Callback>(callback), mtx);
    }

    template <typename Sample, typename Callback>
    bool subscribe_graph_answers(Callback&& callback, std::mutex& mtx)
    {
        return subscribe_impl<Sample>("graph_answer", sub_graph_answer_, topic_graph, std::forward<Callback>(callback), mtx);
    }

    bool publish_node(const DSR::MvregNodeMsg& object) { return pub_node_.write(object); }
    bool publish_node(const DSR::LWWNodeMsg& object) { return pub_node_.write(object); }
    bool publish_edge(const DSR::MvregEdgeMsg& object) { return pub_edge_.write(object); }
    bool publish_edge(const DSR::LWWEdgeMsg& object) { return pub_edge_.write(object); }
    bool publish_node_attrs(const DSR::MvregNodeAttrVec& object) { return pub_node_attrs_.write(object); }
    bool publish_node_attrs(const DSR::LWWNodeAttrVec& object) { return pub_node_attrs_.write(object); }
    bool publish_edge_attrs(const DSR::MvregEdgeAttrVec& object) { return pub_edge_attrs_.write(object); }
    bool publish_edge_attrs(const DSR::LWWEdgeAttrVec& object) { return pub_edge_attrs_.write(object); }
    bool publish_graph_request(const DSR::GraphRequest& object) { return pub_graph_request_.write(object); }
    bool publish_graph_answer(const DSR::OrMap& object) { return pub_graph_answer_.write(object); }
    bool publish_graph_answer(const DSR::LWWGraphSnapshot& object) { return pub_graph_answer_.write(object); }

    void remove_participant_and_entities_impl();

private:
    template <typename Sample, typename Callback>
    bool subscribe_impl(const char* id,
                        DSRSubscriber& subscriber,
                        eprosima::fastdds::dds::Topic* topic,
                        Callback&& callback,
                        std::mutex& mtx)
    {
        auto entity_id_to_u32 = [](const auto& entity_id) -> uint32_t
        {
            return (static_cast<uint32_t>(entity_id.value[0]) << 24) |
                   (static_cast<uint32_t>(entity_id.value[1]) << 16) |
                   (static_cast<uint32_t>(entity_id.value[2]) << 8) |
                   static_cast<uint32_t>(entity_id.value[3]);
        };

        auto reader_callback = [callback = std::forward<Callback>(callback), entity_id_to_u32](eprosima::fastdds::dds::DataReader* reader) mutable
        {
            while (true)
            {
                eprosima::fastdds::dds::SampleInfo info;
                Sample sample;
                if (reader->take_next_sample(&sample, &info) != 0) {
                    break;
                }

                callback(std::move(sample), DSR::Transport::ReceivedSampleInfo{
                    .valid_data = info.valid_data,
                    .source_entity_id = entity_id_to_u32(info.sample_identity.writer_guid().entityId),
                    .source_timestamp_ns = static_cast<int64_t>(info.source_timestamp.seconds()) * 1000000000LL + info.source_timestamp.nanosec(),
                    .reception_timestamp_ns = static_cast<int64_t>(info.reception_timestamp.seconds()) * 1000000000LL + info.reception_timestamp.nanosec()
                });
            }
        };

        auto [res, sub, reader] = subscriber.init(mp_participant, topic, domain_id_, std::move(reader_callback), mtx);
        if (res && topic != nullptr) {
            add_subscriber(id, std::pair{sub, reader});
        }
        return res;
    }

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

    DSRPublisher pub_node_;
    DSRPublisher pub_edge_;
    DSRPublisher pub_node_attrs_;
    DSRPublisher pub_edge_attrs_;
    DSRPublisher pub_graph_request_;
    DSRPublisher pub_graph_answer_;

    DSRSubscriber sub_node_;
    DSRSubscriber sub_edge_;
    DSRSubscriber sub_node_attrs_;
    DSRSubscriber sub_edge_attrs_;
    DSRSubscriber sub_graph_request_;
    DSRSubscriber sub_graph_answer_;

    class ParticpantListener : public eprosima::fastdds::dds::DomainParticipantListener
    {
    public:
        explicit ParticpantListener(discovery_callback_type&& fn)
            : eprosima::fastdds::dds::DomainParticipantListener(), f(std::move(fn)){};
        ~ParticpantListener() override = default;

         void on_participant_discovery  (
                eprosima::fastdds::dds::DomainParticipant* participant,
                eprosima::fastdds::rtps::ParticipantDiscoveryStatus status,
                const eprosima::fastdds::rtps::ParticipantBuiltinTopicData& info,
                bool& should_be_ignored) override
        {
            f(map_status(status), DSR::Transport::ParticipantDiscoveryInfo{info.participant_name.to_string()});
        }

        static DSR::Transport::ParticipantDiscoveryStatus map_status(eprosima::fastdds::rtps::ParticipantDiscoveryStatus status)
        {
            switch (status) {
                case eprosima::fastdds::rtps::ParticipantDiscoveryStatus::DISCOVERED_PARTICIPANT:
                    return DSR::Transport::ParticipantDiscoveryStatus::discovered;
                case eprosima::fastdds::rtps::ParticipantDiscoveryStatus::REMOVED_PARTICIPANT:
                    return DSR::Transport::ParticipantDiscoveryStatus::removed;
                case eprosima::fastdds::rtps::ParticipantDiscoveryStatus::DROPPED_PARTICIPANT:
                    return DSR::Transport::ParticipantDiscoveryStatus::dropped;
                default:
                    return DSR::Transport::ParticipantDiscoveryStatus::other;
            }
        }

        discovery_callback_type f;
        //int n_matched;
    };
    std::unique_ptr<ParticpantListener> m_listener;
};

#endif // _Participant_H_
