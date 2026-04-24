#ifndef _PUBLISHER_H_
#define _PUBLISHER_H_


#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>

#include <dsr/core/types/internal_types.h>
#include <dsr/core/transport/transport_crtp.h>

class DSRPublisher : public DSR::Transport::PublisherTransportCRTP<DSRPublisher>
{
public:
    DSRPublisher();
    virtual ~DSRPublisher();
    [[nodiscard]] std::tuple<bool, eprosima::fastdds::dds::Publisher*, eprosima::fastdds::dds::DataWriter*> init_impl(
        eprosima::fastdds::dds::DomainParticipant *mp_participant_,
        eprosima::fastdds::dds::Topic *topic,
        int8_t domain_id,
        bool isStreamData = false);
    [[nodiscard]] eprosima::fastdds::rtps::GUID_t getParticipantID_impl() const;
    bool write_impl(const DSR::GraphRequest &object);
    bool write_impl(const DSR::MvregNodeMsg &object);
    bool write_impl(const DSR::OrMap &object);
    bool write_impl(const DSR::MvregEdgeMsg &object);
    bool write_impl(const DSR::MvregEdgeAttrVec &object);
    bool write_impl(const DSR::MvregNodeAttrVec &object);
    bool write_impl(const DSR::LWWNodeMsg &object);
    bool write_impl(const DSR::LWWEdgeMsg &object);
    bool write_impl(const DSR::LWWNodeAttrVec &object);
    bool write_impl(const DSR::LWWEdgeAttrVec &object);
    bool write_impl(const DSR::LWWGraphSnapshot &object);

private:
    eprosima::fastdds::dds::DomainParticipant *mp_participant;
    eprosima::fastdds::dds::Publisher *mp_publisher;
    eprosima::fastdds::dds::DataWriter *mp_writer;

	class PubListener : public eprosima::fastdds::dds::DataWriterListener
	{
	public:
        PubListener() : n_matched(0){};
        ~PubListener() override = default;
		void on_publication_matched(eprosima::fastdds::dds::DataWriter* writer,
                                    const eprosima::fastdds::dds::PublicationMatchedStatus& info) override;
		int n_matched;
	} m_listener;

};

#endif // _PUBLISHER_H_
