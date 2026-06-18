#include <fastdds/rtps/participant/RTPSParticipant.hpp>
//#include <fastdds/rtps/attributes/PublisherAttributes.h>
#include <fastdds/rtps/RTPSDomain.hpp>
#include <fastdds/rtps/transport/TransportDescriptorInterface.hpp>
#include <fastdds/rtps/transport/shared_mem/SharedMemTransportDescriptor.hpp>
#include <fastdds/utils/IPFinder.hpp>
#include <fastdds/rtps/common/MatchingInfo.hpp>

#include <dsr/core/rtps/env_overrides.h>
#include <dsr/core/rtps/dsrpublisher.h>

#include <QDebug>

#include <algorithm>

using namespace eprosima::fastdds;
using namespace eprosima::fastdds::rtps;
using namespace eprosima::fastdds::dds;

namespace {
Locator_t domain_multicast_locator(int8_t domain_id)
{
    const auto domain = static_cast<uint8_t>(domain_id);
    Locator_t locator;
    locator.port = 7900;
    locator.kind = LOCATOR_KIND_UDPv4;
    IPLocator::setIPv4(locator,
        ("239.255." + std::to_string(domain / 250) + "." + std::to_string(1 + (domain % 250))).c_str());
    return locator;
}

uint32_t env_u32_or(const char* name, uint32_t fallback, uint32_t minimum = 0)
{
    const auto value = DSR::RTPS::Env::read_u32(name);
    if (!value.present()) {
        return fallback;
    }
    if (!value.value.has_value() || *value.value < minimum) {
        qWarning() << "Ignoring invalid" << name << "value" << value.raw;
        return fallback;
    }
    qInfo() << "Using" << name << "override" << *value.value;
    return *value.value;
}

eprosima::fastdds::dds::Duration_t duration_from_ms(uint32_t ms)
{
    return eprosima::fastdds::dds::Duration_t(
        static_cast<int32_t>(ms / 1000U),
        static_cast<uint32_t>((ms % 1000U) * 1000000U));
}
}

DSRPublisher::DSRPublisher() : mp_participant(nullptr), mp_publisher(nullptr), mp_writer(nullptr)
{}

DSRPublisher::~DSRPublisher()
{
}

std::tuple<bool, eprosima::fastdds::dds::Publisher*, eprosima::fastdds::dds::DataWriter*>
        DSRPublisher::init_impl(eprosima::fastdds::dds::DomainParticipant *mp_participant_, eprosima::fastdds::dds::Topic *topic, int8_t domain_id, bool isStreamData )
{
    mp_participant = mp_participant_;


    eprosima::fastdds::dds::DataWriterQos dataWriterQos;
    dataWriterQos.reliability().kind = eprosima::fastdds::dds::RELIABLE_RELIABILITY_QOS;
    dataWriterQos.history().kind = eprosima::fastdds::dds::KEEP_ALL_HISTORY_QOS;
    dataWriterQos.resource_limits().max_samples = 300;
    dataWriterQos.resource_limits().allocated_samples = 300;
    dataWriterQos.durability().kind = eprosima::fastdds::dds::VOLATILE_DURABILITY_QOS;
    dataWriterQos.publish_mode().kind = ASYNCHRONOUS_PUBLISH_MODE;

    dataWriterQos.endpoint().history_memory_policy = DYNAMIC_REUSABLE_MEMORY_MODE;

    bool local = std::find_if(mp_participant_->get_qos().transport().user_transports.begin(),
                              mp_participant_->get_qos().transport().user_transports.end(),
                              [&](auto &transport) {
                                    return transport != nullptr && dynamic_cast<eprosima::fastdds::rtps::SharedMemTransportDescriptor*>(transport.get()) != nullptr;
                               }) != mp_participant_->get_qos().transport().user_transports.end();


    if (not local) {
        dataWriterQos.endpoint().multicast_locator_list.push_back(domain_multicast_locator(domain_id));
    }

    //ThroughputControllerDescriptor PublisherThroughputController{30000000, 1000};
    //dataWriterQos.throughput_controller() = PublisherThroughputController;

    if (isStreamData) {
        dataWriterQos.reliability().kind = eprosima::fastdds::dds::BEST_EFFORT_RELIABILITY_QOS;
        dataWriterQos.history().kind = eprosima::fastdds::dds::KEEP_LAST_HISTORY_QOS;
        const auto depth = static_cast<int32_t>(env_u32_or("DSR_STREAM_HISTORY_DEPTH", 50, 1));
        dataWriterQos.history().depth = depth;
        dataWriterQos.resource_limits().max_samples = std::max(dataWriterQos.resource_limits().max_samples, depth);
        dataWriterQos.resource_limits().allocated_samples =
            std::max(dataWriterQos.resource_limits().allocated_samples, depth);
        dataWriterQos.reliable_writer_qos().disable_positive_acks.enabled = true;
    } else {
        const auto max_samples = static_cast<int32_t>(env_u32_or("DSR_RELIABLE_MAX_SAMPLES", 300, 1));
        dataWriterQos.resource_limits().max_samples = max_samples;
        dataWriterQos.resource_limits().allocated_samples = max_samples;

        // Phase 1 liveliness-decoupling: by default the reliable writers use KEEP_ALL, which
        // pins the history full of un-ACKed samples when a reader is slow/saturated. That
        // blocks every subsequent write() — including the tiny agent heartbeat published via
        // update_node() — for as long as the reader stays behind (observed: a 135 s heartbeat
        // freeze under CPU load, which the presence protocol misread as a crashed peer).
        // Opting into KEEP_LAST(depth) makes a full history discard the OLDEST sample instead
        // of blocking, so bulk sensor traffic (lidar/camera node updates) can never perpetually
        // starve liveliness updates. Default to KEEP_LAST(64) for this deployment; the
        // environment variable still overrides it when a different depth is needed.
        const auto keep_last_depth = static_cast<int32_t>(env_u32_or("DSR_RELIABLE_KEEP_LAST_DEPTH", 64));
        if (keep_last_depth > 0) {
            dataWriterQos.history().kind = eprosima::fastdds::dds::KEEP_LAST_HISTORY_QOS;
            dataWriterQos.history().depth = keep_last_depth;
            dataWriterQos.resource_limits().max_samples =
                std::max(dataWriterQos.resource_limits().max_samples, keep_last_depth);
            dataWriterQos.resource_limits().allocated_samples =
                std::max(dataWriterQos.resource_limits().allocated_samples, keep_last_depth);
        }
    }

    // Bound how long a single write() may block waiting for history space. Default to 20 ms so
    // update_node() returns promptly instead of stalling the calling thread — notably the
    // heartbeat thread — when a reader falls behind. The environment variable still overrides it.
    if (const auto max_blocking_ms = env_u32_or("DSR_WRITER_MAX_BLOCKING_MS", 20); max_blocking_ms > 0) {
        dataWriterQos.reliability().max_blocking_time = duration_from_ms(max_blocking_ms);
    }

    // Check ACK for sended messages.
    dataWriterQos.reliable_writer_qos().times.heartbeat_period =
        duration_from_ms(env_u32_or("DSR_WRITER_HEARTBEAT_PERIOD_MS", 20));

    //Check latency
    dataWriterQos.latency_budget().duration =
        duration_from_ms(env_u32_or("DSR_WRITER_LATENCY_BUDGET_MS", 10));

    //Invalidate data after 1 second. If we dont receive it after this time we probably won't get it.
    dataWriterQos.lifespan().duration =
        duration_from_ms(env_u32_or("DSR_WRITER_LIFESPAN_MS", 1000));


    int retry = 0;
    while (retry < 5) {

        mp_publisher = mp_participant->create_publisher(eprosima::fastdds::dds::PUBLISHER_QOS_DEFAULT);
        mp_writer = mp_publisher->create_datawriter(topic, dataWriterQos, &m_listener);

        if(mp_publisher != nullptr && mp_writer != nullptr) {
            qDebug() << "Publisher created, waiting for Subscribers." ;
            return { true, mp_publisher, mp_writer };
        }
        retry++;
        qDebug() << "Error creating publisher, retrying. [" << retry <<"/5]"  ;

    }

    qFatal("%s", std::string_view("Could not create publisher " + std::string(topic->get_name()) + " after 5 attempts").data());

}

GUID_t DSRPublisher::getParticipantID_impl() const
{
    return mp_participant->guid();
}

namespace {
template <typename T, typename LogFn>
bool write_with_retry(eprosima::fastdds::dds::DataWriter* writer, const T& object, LogFn&& log_error)
{
    ReturnCode_t rt;
    int retry = 0;
    while (retry < 5) {
        if (rt = writer->write(const_cast<T*>(&object)); rt == RETCODE_OK) return true;
        retry++;
    }
    log_error(rt);
    return false;
}
}

bool DSRPublisher::write_impl(const DSR::MvregNodeMsg& object)
{
    return write_with_retry(mp_writer, object, [&](ReturnCode_t rt) {
        qInfo() << "Error writing NODE " << object.id << " after 5 attempts. error code: " << rt;
    });
}


bool DSRPublisher::write_impl(const DSR::MvregEdgeMsg& object)
{
    return write_with_retry(mp_writer, object, [&](ReturnCode_t rt) {
        qInfo() << "Error writing EDGE " << object.from << " " << object.to << " " << object.type.data() << " after 5 attempts. error code: " << rt;
    });
}


bool DSRPublisher::write_impl(const DSR::OrMap& object)
{
    return write_with_retry(mp_writer, object, [&](ReturnCode_t rt) {
        qInfo() << "Error writing GRAPH " << object.m.size() << " after 5 attempts. error code: " << rt;
    });
}

bool DSRPublisher::write_impl(const DSR::GraphRequest& object)
{
    return write_with_retry(mp_writer, object, [&](ReturnCode_t rt) {
        qInfo() << "Error writing GRAPH REQUEST after 5 attempts. error code: " << rt;
    });
}

bool DSRPublisher::write_impl(const DSR::MvregEdgeAttrVec& object)
{
    return write_with_retry(mp_writer, object, [&](ReturnCode_t rt) {
        qInfo() << "Error writing EDGE ATTRIBUTE VECTOR after 5 attempts. error code: " << rt;
    });
}

bool DSRPublisher::write_impl(const DSR::MvregNodeAttrVec& object) {
    return write_with_retry(mp_writer, object, [&](ReturnCode_t rt) {
        qInfo() << "Error writing NODE ATTRIBUTE VECTOR after 5 attempts. error code: " << rt;
    });
}

bool DSRPublisher::write_impl(const DSR::LWWNodeMsg& object)
{
    return write_with_retry(mp_writer, object, [&](ReturnCode_t rt) {
        qInfo() << "Error writing LWW NODE " << object.id << " after 5 attempts. error code: " << rt;
    });
}

bool DSRPublisher::write_impl(const DSR::LWWEdgeMsg& object)
{
    return write_with_retry(mp_writer, object, [&](ReturnCode_t rt) {
        qInfo() << "Error writing LWW EDGE " << object.from << " " << object.to << " " << object.type.data() << " after 5 attempts. error code: " << rt;
    });
}

bool DSRPublisher::write_impl(const DSR::LWWNodeAttrVec& object)
{
    return write_with_retry(mp_writer, object, [&](ReturnCode_t rt) {
        qInfo() << "Error writing LWW NODE ATTRIBUTE VECTOR after 5 attempts. error code: " << rt;
    });
}

bool DSRPublisher::write_impl(const DSR::LWWEdgeAttrVec& object)
{
    return write_with_retry(mp_writer, object, [&](ReturnCode_t rt) {
        qInfo() << "Error writing LWW EDGE ATTRIBUTE VECTOR after 5 attempts. error code: " << rt;
    });
}

bool DSRPublisher::write_impl(const DSR::LWWGraphSnapshot& object)
{
    return write_with_retry(mp_writer, object, [&](ReturnCode_t rt) {
        qInfo() << "Error writing LWW GRAPH " << object.nodes.size() << " nodes after 5 attempts. error code: " << rt;
    });
}


void DSRPublisher::PubListener::on_publication_matched(eprosima::fastdds::dds::DataWriter* writer,
                                                       const eprosima::fastdds::dds::PublicationMatchedStatus& info)
{
    if (info.current_count == eprosima::fastdds::rtps::MatchingStatus::MATCHED_MATCHING) {
        n_matched++;
        qInfo() << "Subscriber [" << writer->get_topic()->get_name().data() <<"] matched " << info.last_subscription_handle.value;// << " self: " << info.remoteEndpointGuid.is_on_same_process_as(pub->getGuid());
    } else {
        n_matched--;
        qInfo() << "Subscriber [" << writer->get_topic()->get_name().data() <<"] unmatched" << info.last_subscription_handle.value;// << " self: " <<info.remoteEndpointGuid.is_on_same_process_as(pub->getGuid());
    }
}
