#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>

#include <fastdds/rtps/builtin/data/ParticipantBuiltinTopicData.hpp>
#include <fastdds/rtps/participant/ParticipantDiscoveryInfo.hpp>

namespace DSR::Transport {

template<typename Derived>
class ParticipantTransportCRTP
{
public:
    auto init(
        uint32_t agent_id,
        const std::string& agent_name,
        int localhost,
        std::function<void(eprosima::fastdds::rtps::ParticipantDiscoveryStatus,
                           const eprosima::fastdds::rtps::ParticipantBuiltinTopicData&)> fn,
        int8_t domain_id = 0,
        uint8_t sync_mode_wire = 0)
    {
        return derived().init_impl(agent_id, agent_name, localhost, std::move(fn), domain_id, sync_mode_wire);
    }

    template<typename Id, typename SubscriberEntry>
    void add_subscriber(Id&& id, SubscriberEntry&& entry)
    {
        derived().add_subscriber_impl(std::forward<Id>(id), std::forward<SubscriberEntry>(entry));
    }

    template<typename Id, typename PublisherEntry>
    void add_publisher(Id&& id, PublisherEntry&& entry)
    {
        derived().add_publisher_impl(std::forward<Id>(id), std::forward<PublisherEntry>(entry));
    }

    template<typename Id>
    void delete_subscriber(Id&& id)
    {
        derived().delete_subscriber_impl(std::forward<Id>(id));
    }

    template<typename Id>
    void delete_publisher(Id&& id)
    {
        derived().delete_publisher_impl(std::forward<Id>(id));
    }

    void remove_participant_and_entities()
    {
        derived().remove_participant_and_entities_impl();
    }

private:
    Derived& derived() { return static_cast<Derived&>(*this); }
};

template<typename Derived>
class PublisherTransportCRTP
{
public:
    template<typename ParticipantHandle, typename TopicHandle>
    auto init(
        ParticipantHandle* participant,
        TopicHandle* topic,
        int8_t domain_id,
        bool is_stream_data = false)
    {
        return derived().init_impl(participant, topic, domain_id, is_stream_data);
    }

    auto getParticipantID() const
    {
        return derived().getParticipantID_impl();
    }

    template<typename Message>
    bool write(const Message& object)
    {
        return derived().write_impl(object);
    }

private:
    Derived& derived() { return static_cast<Derived&>(*this); }
    const Derived& derived() const { return static_cast<const Derived&>(*this); }
};

template<typename Derived>
class SubscriberTransportCRTP
{
public:
    template<typename ParticipantHandle, typename TopicHandle, typename Callback>
    auto init(
        ParticipantHandle* participant,
        TopicHandle* topic,
        int8_t domain_id,
        Callback&& callback,
        std::mutex& creation_mutex,
        bool is_stream_data = false)
    {
        return derived().init_impl(
            participant,
            topic,
            domain_id,
            std::forward<Callback>(callback),
            creation_mutex,
            is_stream_data);
    }

    auto getSubscriber()
    {
        return derived().getSubscriber_impl();
    }

    auto getDataReader()
    {
        return derived().getDataReader_impl();
    }

private:
    Derived& derived() { return static_cast<Derived&>(*this); }
};

} // namespace DSR::Transport
