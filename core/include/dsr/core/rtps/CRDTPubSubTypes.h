#pragma once

#include <fastdds/dds/topic/TopicDataType.hpp>
#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>
#include <fastcdr/CdrSizeCalculator.hpp>

#include <string>
#include <stdexcept>

/**
 * Template FastDDS TopicDataType backed by any ISerializable<T>.
 *
 * Replaces the IDL-generated *PubSubType classes for all DSR topics.
 * T must satisfy ISerializable<T> (i.e. implement serialize_impl,
 * deserialize_impl, serialized_size_impl).
 */
template<typename T>
class CRDTPubSubType : public eprosima::fastdds::dds::TopicDataType
{
public:
    explicit CRDTPubSubType(const std::string& type_name)
    {
        set_name(type_name);
        // Generous initial buffer size; FastDDS reallocates as needed.
        max_serialized_type_size = 65536 + 4u; /* +4 for encapsulation */
        is_compute_key_provided = false;
    }

    ~CRDTPubSubType() override = default;

    bool serialize(
        const void* const data,
        eprosima::fastdds::rtps::SerializedPayload_t& payload,
        eprosima::fastdds::dds::DataRepresentationId_t data_representation) override
    {
        const T* p_type = static_cast<const T*>(data);

        eprosima::fastcdr::FastBuffer fastbuffer(
            reinterpret_cast<char*>(payload.data), payload.max_size);

        eprosima::fastcdr::Cdr ser(
            fastbuffer,
            eprosima::fastcdr::Cdr::DEFAULT_ENDIAN,
            data_representation == eprosima::fastdds::dds::DataRepresentationId_t::XCDR_DATA_REPRESENTATION
                ? eprosima::fastcdr::CdrVersion::XCDRv1
                : eprosima::fastcdr::CdrVersion::XCDRv2);

        payload.encapsulation =
            ser.endianness() == eprosima::fastcdr::Cdr::BIG_ENDIANNESS ? CDR_BE : CDR_LE;

        ser.set_encoding_flag(
            data_representation == eprosima::fastdds::dds::DataRepresentationId_t::XCDR_DATA_REPRESENTATION
                ? eprosima::fastcdr::EncodingAlgorithmFlag::PLAIN_CDR
                : eprosima::fastcdr::EncodingAlgorithmFlag::PLAIN_CDR2);

        try {
            ser.serialize_encapsulation();
            p_type->serialize_impl(ser);
        } catch (eprosima::fastcdr::exception::Exception& /*e*/) {
            return false;
        }

        payload.length = static_cast<uint32_t>(ser.get_serialized_data_length());
        return true;
    }

    bool deserialize(
        eprosima::fastdds::rtps::SerializedPayload_t& payload,
        void* data) override
    {
        T* p_type = static_cast<T*>(data);

        eprosima::fastcdr::FastBuffer fastbuffer(
            reinterpret_cast<char*>(payload.data), payload.length);

        eprosima::fastcdr::Cdr deser(
            fastbuffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN);

        try {
            deser.read_encapsulation();
            payload.encapsulation =
                deser.endianness() == eprosima::fastcdr::Cdr::BIG_ENDIANNESS ? CDR_BE : CDR_LE;
            p_type->deserialize_impl(deser);
        } catch (eprosima::fastcdr::exception::Exception& /*e*/) {
            return false;
        }

        return true;
    }

    uint32_t calculate_serialized_size(
        const void* const data,
        eprosima::fastdds::dds::DataRepresentationId_t data_representation) override
    {
        const T* p_type = static_cast<const T*>(data);

        eprosima::fastcdr::CdrSizeCalculator calculator(
            data_representation == eprosima::fastdds::dds::DataRepresentationId_t::XCDR_DATA_REPRESENTATION
                ? eprosima::fastcdr::CdrVersion::XCDRv1
                : eprosima::fastcdr::CdrVersion::XCDRv2);

        size_t current_alignment{0};
        size_t size = 0;

        try {
            size += p_type->serialized_size_impl(calculator, current_alignment);
        } catch (eprosima::fastcdr::exception::Exception& /*e*/) {
            return 0;
        }

        return static_cast<uint32_t>(size) + 4u; /* encapsulation */
    }

    void* create_data() override
    {
        return new T();
    }

    void delete_data(void* data) override
    {
        delete static_cast<T*>(data);
    }

    bool compute_key(
        eprosima::fastdds::rtps::SerializedPayload_t& /*payload*/,
        eprosima::fastdds::rtps::InstanceHandle_t& /*handle*/,
        bool /*force_md5*/) override
    {
        return false;
    }

    bool compute_key(
        const void* const /*data*/,
        eprosima::fastdds::rtps::InstanceHandle_t& /*handle*/,
        bool /*force_md5*/) override
    {
        return false;
    }
};
