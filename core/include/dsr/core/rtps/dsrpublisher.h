#ifndef _PUBLISHER_H_
#define _PUBLISHER_H_


#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>

#include <dsr/core/types/internal_types.h>

class DSRPublisher
{
public:
    DSRPublisher();
    virtual ~DSRPublisher();
    [[nodiscard]] std::tuple<bool, eprosima::fastdds::dds::Publisher*, eprosima::fastdds::dds::DataWriter*> init(
        eprosima::fastdds::dds::DomainParticipant *mp_participant_,
        eprosima::fastdds::dds::Topic *topic,
        int8_t domain_id,
        bool isStreamData = false);
    [[nodiscard]] eprosima::fastdds::rtps::GUID_t getParticipantID() const;
    bool write(const DSR::GraphRequest &object);
    bool write(const DSR::MvregNodeMsg &object);
    bool write(const DSR::OrMap &object);
    bool write(const DSR::MvregEdgeMsg &object);
    bool write(const DSR::MvregEdgeAttrVec &object);
    bool write(const DSR::MvregNodeAttrVec &object);
    bool write(const DSR::LWWNodeMsg &object);
    bool write(const DSR::LWWEdgeMsg &object);
    bool write(const DSR::LWWNodeAttrVec &object);
    bool write(const DSR::LWWEdgeAttrVec &object);
    bool write(const DSR::LWWGraphSnapshot &object);

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
