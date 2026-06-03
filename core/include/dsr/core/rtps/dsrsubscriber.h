#ifndef _SUBSCRIBER_H_
#define _SUBSCRIBER_H_

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>

#include <functional>
#include <dsr/core/transport/transport_crtp.h>


class DSRSubscriber : public DSR::Transport::SubscriberTransportCRTP<
    DSRSubscriber,
    eprosima::fastdds::dds::DomainParticipant,
    eprosima::fastdds::dds::Topic,
    std::function<void(eprosima::fastdds::dds::DataReader*)>>
{
public:
    using subscriber_handle_type = eprosima::fastdds::dds::Subscriber;
    using reader_handle_type = eprosima::fastdds::dds::DataReader;
	DSRSubscriber();
	virtual ~DSRSubscriber();
    [[nodiscard]] std::tuple<bool, subscriber_handle_type*, reader_handle_type*>
	          init_impl(participant_handle_type *mp_participant_,
                   topic_handle_type *topic,
                   int8_t domain_id,
				   callback_type  f_,
				   std::mutex& mtx,
				   bool isStreamData = false);

    subscriber_handle_type *getSubscriber_impl();
    reader_handle_type *getDataReader_impl();

private:
    eprosima::fastdds::dds::DomainParticipant *mp_participant;
    eprosima::fastdds::dds::Subscriber *mp_subscriber;
    eprosima::fastdds::dds::DataReader *mp_reader{};

	class SubListener : public eprosima::fastdds::dds::DataReaderListener
	{
	public:
		SubListener() = default;
		~SubListener() override= default;
		void on_subscription_matched(eprosima::fastdds::dds::DataReader* reader,
                                     const eprosima::fastdds::dds::SubscriptionMatchedStatus& info) override;
        void on_data_available(
                eprosima::fastdds::dds::DataReader* reader) override;

		callback_type  f;

	} m_listener;

};

#endif // _SUBSCRIBER_H_
