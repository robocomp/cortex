#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

namespace DSR::Transport {

enum class ParticipantDiscoveryStatus : uint8_t
{
    discovered,
    removed,
    dropped,
    other
};

// The 12-byte RTPS GUID prefix. It is the same for a participant and for every reader/writer it owns,
// so it is what ties "a remote reader matched one of our writers" to "which participant that is".
using ParticipantGuidPrefix = std::array<uint8_t, 12>;

struct ParticipantDiscoveryInfo
{
    std::string participant_name;
    ParticipantGuidPrefix guid_prefix{};
};

// The built-in graph writers of a participant, in the order used for the per-peer matched bitmask.
enum class BuiltinWriter : uint8_t
{
    node = 0,
    edge,
    node_attrs,
    edge_attrs,
    graph_answer,
    graph_request
};

// One (un)match between one of OUR writers and a reader of ANOTHER participant.
// Delivered on a FastDDS listener thread: the receiver must not do graph work there.
struct PublicationMatchInfo
{
    BuiltinWriter writer{BuiltinWriter::node};
    bool matched{false};                    // true: newly matched, false: unmatched
    ParticipantGuidPrefix remote_prefix{};  // the remote reader's participant
};

struct ReceivedSampleInfo
{
    bool valid_data{false};
    uint32_t source_entity_id{0};
    int64_t source_timestamp_ns{0};
    int64_t reception_timestamp_ns{0};
};

template<typename Derived, typename ParticipantHandle, typename SubscriberEntry, typename PublisherEntry, typename DiscoveryCallback>
class ParticipantTransportCRTP
{
public:
    using participant_handle_type = ParticipantHandle;
    using subscriber_entry_type = SubscriberEntry;
    using publisher_entry_type = PublisherEntry;
    using discovery_callback_type = DiscoveryCallback;

    auto init(
        uint32_t agent_id,
        const std::string& agent_name,
        int localhost,
        discovery_callback_type fn,
        int8_t domain_id = 0,
        uint8_t sync_mode_wire = 0)
    {
        return derived().init_impl(agent_id, agent_name, localhost, std::move(fn), domain_id, sync_mode_wire);
    }

    void add_subscriber(const std::string& id, subscriber_entry_type entry)
    {
        derived().add_subscriber_impl(id, std::move(entry));
    }

    void add_publisher(const std::string& id, publisher_entry_type entry)
    {
        derived().add_publisher_impl(id, std::move(entry));
    }

    void delete_subscriber(const std::string& id)
    {
        derived().delete_subscriber_impl(id);
    }

    void delete_publisher(const std::string& id)
    {
        derived().delete_publisher_impl(id);
    }

    void remove_participant_and_entities()
    {
        derived().remove_participant_and_entities_impl();
    }

private:
    Derived& derived() { return static_cast<Derived&>(*this); }
};

template<typename Derived, typename ParticipantHandle, typename TopicHandle>
class PublisherTransportCRTP
{
public:
    using participant_handle_type = ParticipantHandle;
    using topic_handle_type = TopicHandle;

    auto init(
        participant_handle_type* participant,
        topic_handle_type* topic,
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

template<typename Derived, typename ParticipantHandle, typename TopicHandle, typename Callback>
class SubscriberTransportCRTP
{
public:
    using participant_handle_type = ParticipantHandle;
    using topic_handle_type = TopicHandle;
    using callback_type = Callback;

    auto init(
        participant_handle_type* participant,
        topic_handle_type* topic,
        int8_t domain_id,
        callback_type callback,
        std::mutex& creation_mutex,
        bool is_stream_data = false)
    {
        return derived().init_impl(
            participant,
            topic,
            domain_id,
            std::move(callback),
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
