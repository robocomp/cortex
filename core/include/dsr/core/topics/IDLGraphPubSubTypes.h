#pragma once

#include "dsr/core/crdt/delta_crdt.h"
#include "dsr/core/types/crdt_types.h"
#include <cstdint>
#include <dsr/core/topics/IDLGraph.h>
#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>
#include <fastrtps/TopicDataType.h>
#include <fastrtps/config.h>
#include <vector>

template <typename T, const char* name, uint32_t s>
class DDSTtype : public eprosima::fastrtps::TopicDataType
{

    class CdrSerializationHelper
    {
    public:
        static size_t getCdrSerializedSize(const T &data, size_t current_alignment = 0)
        {
            size_t initial_alignment = current_alignment;
            current_alignment += serialized_size(data, current_alignment);
            return current_alignment - initial_alignment;
        }

        static void serialize(const T &data, eprosima::fastcdr::Cdr &cdr)
        {
            serialize(data, cdr);
        }

        static void deserialize(T &data, eprosima::fastcdr::Cdr &cdr)
        {
            deserialize(data, cdr);
        }
        
        static const char* getName()
        {
            return name;
        }

        static uint32_t getMaxExpectedSize()
        {
            return s;
        }
    };

public:
    typedef T type;

    DDSTtype()
    {
        setName(CdrSerializationHelper::getName());
        m_typeSize =  CdrSerializationHelper::getMaxExpectedSize();
        m_isGetKeyDefined = false;
    }

    ~DDSTtype()
    {
    }

    bool serialize(void *data, eprosima::fastrtps::rtps::SerializedPayload_t *payload) override
    {
        auto *p_type = static_cast<type *>(data);
        // Object that manages the raw buffer.
        eprosima::fastcdr::FastBuffer fastbuffer(reinterpret_cast<char *>(payload->data), payload->max_size);
        // Object that serializes the data.
        eprosima::fastcdr::Cdr ser(fastbuffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::Cdr::DDS_CDR);
        payload->encapsulation = ser.endianness() == eprosima::fastcdr::Cdr::BIG_ENDIANNESS ? CDR_BE : CDR_LE;
        // Serialize encapsulation
        ser.serialize_encapsulation();

        try
        {
            CdrSerializationHelper::serialize(*p_type, ser);  // Serialize the object:
        }
        catch (eprosima::fastcdr::exception::NotEnoughMemoryException & /*exception*/)
        {
            return false;
        }

        payload->length = static_cast<uint32_t>(ser.getSerializedDataLength());  // Get the serialized length
        return true;
    }

    bool deserialize(eprosima::fastrtps::rtps::SerializedPayload_t *payload, void *data) override
    {
        auto *p_type = static_cast<type *>(data);                                                   
        //  Object that manages the raw buffer.
        eprosima::fastcdr::FastBuffer fastbuffer(reinterpret_cast<char *>(payload->data), payload->length);
        // Object that deserializes the data.
        eprosima::fastcdr::Cdr deser(fastbuffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN,
                                     eprosima::fastcdr::Cdr::DDS_CDR);
        // Deserialize encapsulation.
        deser.read_encapsulation();
        payload->encapsulation = deser.endianness() == eprosima::fastcdr::Cdr::BIG_ENDIANNESS ? CDR_BE : CDR_LE;

        try
        {
            CdrSerializationHelper::serialize(*p_type, deser);  // Serialize the object:
        }
        catch (eprosima::fastcdr::exception::NotEnoughMemoryException & /*exception*/)
        {
            return false;
        }

        return true;
    }

    std::function<uint32_t()> getSerializedSizeProvider(void *data) override
    {
        return [data]() -> uint32_t
        { return static_cast<uint32_t>(type::getCdrSerializedSize(*static_cast<T *>(data))) + 4 /*encapsulation*/; };
    }

    bool getKey(void *data, eprosima::fastrtps::rtps::InstanceHandle_t *handle, bool force_md5 = false) override
    {
        return false;
    }

    void *createData() override
    {
        return reinterpret_cast<void *>(new T());
    }

    void deleteData(void *data) override
    {
        delete (reinterpret_cast<T *>(data));
    }
};

//MVREG NODE, Operation timestamp, agent_id
typedef std::tuple<mvreg<DSR::CRDTNode>, uint64_t, uint32_t> NodeInfoTuple;
//MVREG EDGE, from, to, type, Operation timestamp,  agent_id
typedef std::tuple<mvreg<DSR::CRDTEdge>, uint64_t, uint64_t, std::string, uint64_t, uint32_t> EdgeInfoTuple;
//VECTOR <MVREG ATTRIBUTE, Operation timestamp>, agent_id
typedef std::tuple<std::vector<std::tuple<mvreg<DSR::CRDTAttribute>, uint64_t>>, uint32_t> NodeAttributeVecTuple;
//VECTOR <MVREG ATTRIBUTE, Operation timestamp, from, to, type>, agent_id
typedef std::tuple<std::vector<std::tuple<mvreg<DSR::CRDTAttribute>, uint64_t, uint64_t, uint64_t, std::string>>, uint32_t> EdgeAttributeVecTuple;
//agent_id, request_agent_id, map<id, node>, deleted ids VECTOR <uint64_t>, dot_context
typedef std::tuple<uint32_t, uint32_t, std::map<uint64_t, NodeInfoTuple>, std::vector<uint64_t>, dot_context> GraphInfoTuple;
//agent_name, agent_id
typedef std::tuple<std::string, uint32_t> GraphRequestTuple;

constexpr char DDSTypeNodeStr [] = "Node";
constexpr char DDSTypeEdgeStr [] = "Edge";
constexpr char DDSTypeNodeAttributeStr [] = "NodeAttribute";
constexpr char DDSTypeEdgeAttributeStr [] = "EdgeAttribute";
constexpr char DDSTypeGraphStr [] = "Graph";
constexpr char DDSTypeGraphRequestStr [] = "GraphRequest";

typedef DDSTtype<NodeInfoTuple, DDSTypeNodeStr, 2 << 15> DDSTypeNode;
typedef DDSTtype<EdgeInfoTuple, DDSTypeEdgeStr, 2 << 10> DDSTypeEdge;
typedef DDSTtype<NodeAttributeVecTuple, DDSTypeNodeAttributeStr, 2 << 11> DDSTypeNodeAttribute;
typedef DDSTtype<EdgeAttributeVecTuple, DDSTypeEdgeAttributeStr, 2 << 10> DDSTypeEdgeAttribute;
typedef DDSTtype<GraphInfoTuple, DDSTypeGraphStr, 2 << 23> DDSTypeGraph;
typedef DDSTtype<GraphRequestTuple, DDSTypeGraphRequestStr, 2 << 8> DDSTypeGraphRequest;