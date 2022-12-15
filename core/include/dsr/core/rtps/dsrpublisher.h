#ifndef _PUBLISHER_H_
#define _PUBLISHER_H_


#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastrtps/utils/IPLocator.h>

#include "dsr/core/topics/IDLGraphPubSubTypes.h"

class DSRPublisher
{
public:
    DSRPublisher();
    virtual ~DSRPublisher();
    [[nodiscard]] std::tuple<bool, eprosima::fastdds::dds::Publisher*, eprosima::fastdds::dds::DataWriter*> init(eprosima::fastdds::dds::DomainParticipant *mp_participant_, eprosima::fastdds::dds::Topic *topic,  bool isStreamData = false);
    [[nodiscard]] eprosima::fastrtps::rtps::GUID_t getParticipantID() const;
    bool write(NodeInfoTuple *object);
    bool write(EdgeInfoTuple *object);
    bool write(GraphInfoTuple *object);
    bool write(GraphRequestTuple *object);
    bool write(NodeAttributeVecTuple*object);
    bool write(EdgeAttributeVecTuple *object);

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