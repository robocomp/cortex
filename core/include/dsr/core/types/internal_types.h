#pragma once

#include "crdt_types.h"
#include "dsr/core/serialization/serializable.h"
#include <map>
#include <string>
#include <vector>

namespace DSR {

    inline constexpr uint32_t DSR_PROTOCOL_VERSION = 1;

    // ---- GraphRequest --------------------------------------------------------
    // Sent by an agent that wants to receive the full graph snapshot.

    struct GraphRequest : public ISerializable<GraphRequest>
    {
        std::string from;
        int32_t     id{};
        uint32_t    protocol_version{DSR_PROTOCOL_VERSION};

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            cdr << from;
            cdr << id;
            cdr << protocol_version;
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            cdr >> from;
            cdr >> id;
            cdr >> protocol_version;
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            size_t size = 0;
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), from, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), protocol_version, ca);
            return size;
        }
    };

    // ---- MvregNodeMsg --------------------------------------------------------
    // Delta or full mvreg<CRDTNode> plus routing metadata.

    struct MvregNodeMsg : public ISerializable<MvregNodeMsg>
    {
        mvreg<CRDTNode> dk;
        uint64_t        id{};
        uint32_t        agent_id{};
        uint64_t        timestamp{};
        uint32_t        protocol_version{DSR_PROTOCOL_VERSION};

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            dk.serialize(cdr);
            cdr << id;
            cdr << agent_id;
            cdr << timestamp;
            cdr << protocol_version;
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            dk.deserialize(cdr);
            cdr >> id;
            cdr >> agent_id;
            cdr >> timestamp;
            cdr >> protocol_version;
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            size_t size = dk.serialized_size(calc, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), agent_id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), timestamp, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), protocol_version, ca);
            return size;
        }
    };

    // ---- MvregEdgeMsg --------------------------------------------------------

    struct MvregEdgeMsg : public ISerializable<MvregEdgeMsg>
    {
        mvreg<CRDTEdge> dk;
        uint64_t        id{};      // "from" node id
        uint64_t        to{};
        uint64_t        from{};
        std::string     type;
        uint32_t        agent_id{};
        uint64_t        timestamp{};
        uint32_t        protocol_version{DSR_PROTOCOL_VERSION};

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            dk.serialize(cdr);
            cdr << id;
            cdr << to;
            cdr << from;
            cdr << type;
            cdr << agent_id;
            cdr << timestamp;
            cdr << protocol_version;
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            dk.deserialize(cdr);
            cdr >> id;
            cdr >> to;
            cdr >> from;
            cdr >> type;
            cdr >> agent_id;
            cdr >> timestamp;
            cdr >> protocol_version;
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            size_t size = dk.serialized_size(calc, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), to, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), from, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), type, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(4), agent_id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(5), timestamp, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(6), protocol_version, ca);
            return size;
        }
    };

    // ---- MvregNodeAttrMsg ----------------------------------------------------

    struct MvregNodeAttrMsg : public ISerializable<MvregNodeAttrMsg>
    {
        mvreg<CRDTAttribute> dk;
        uint64_t             id{};        // publisher/delta id
        uint64_t             node{};      // owning node
        std::string          attr_name;
        uint32_t             agent_id{};
        uint64_t             timestamp{};
        uint32_t             protocol_version{DSR_PROTOCOL_VERSION};

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            dk.serialize(cdr);
            cdr << id;
            cdr << node;
            cdr << attr_name;
            cdr << agent_id;
            cdr << timestamp;
            cdr << protocol_version;
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            dk.deserialize(cdr);
            cdr >> id;
            cdr >> node;
            cdr >> attr_name;
            cdr >> agent_id;
            cdr >> timestamp;
            cdr >> protocol_version;
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            size_t size = dk.serialized_size(calc, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), node, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), attr_name, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), agent_id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(4), timestamp, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(5), protocol_version, ca);
            return size;
        }
    };

    // ---- MvregEdgeAttrMsg ----------------------------------------------------

    struct MvregEdgeAttrMsg : public ISerializable<MvregEdgeAttrMsg>
    {
        mvreg<CRDTAttribute> dk;
        uint64_t             id{};      // "from" node id
        uint64_t             from_node{};
        uint64_t             to_node{};
        std::string          type;
        std::string          attr_name;
        uint32_t             agent_id{};
        uint64_t             timestamp{};
        uint32_t             protocol_version{DSR_PROTOCOL_VERSION};

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            dk.serialize(cdr);
            cdr << id;
            cdr << from_node;
            cdr << to_node;
            cdr << type;
            cdr << attr_name;
            cdr << agent_id;
            cdr << timestamp;
            cdr << protocol_version;
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            dk.deserialize(cdr);
            cdr >> id;
            cdr >> from_node;
            cdr >> to_node;
            cdr >> type;
            cdr >> attr_name;
            cdr >> agent_id;
            cdr >> timestamp;
            cdr >> protocol_version;
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            size_t size = dk.serialized_size(calc, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), from_node, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), to_node, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), type, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(4), attr_name, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(5), agent_id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(6), timestamp, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(7), protocol_version, ca);
            return size;
        }
    };

    // ---- MvregNodeAttrVec ----------------------------------------------------
    // Batch of node-attribute deltas sent in one DDS message.

    struct MvregNodeAttrVec : public ISerializable<MvregNodeAttrVec>
    {
        std::vector<MvregNodeAttrMsg> vec;

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            auto count = static_cast<uint32_t>(vec.size());
            cdr << count;
            for (const auto& m : vec)
                m.serialize(cdr);
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            uint32_t count = 0;
            cdr >> count;
            vec.resize(count);
            for (auto& m : vec)
                m.deserialize(cdr);
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            auto count = static_cast<uint32_t>(vec.size());
            size_t size = calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), count, ca);
            for (const auto& m : vec)
                size += m.serialized_size(calc, ca);
            return size;
        }
    };

    // ---- MvregEdgeAttrVec ----------------------------------------------------

    struct MvregEdgeAttrVec : public ISerializable<MvregEdgeAttrVec>
    {
        std::vector<MvregEdgeAttrMsg> vec;

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            auto count = static_cast<uint32_t>(vec.size());
            cdr << count;
            for (const auto& m : vec)
                m.serialize(cdr);
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            uint32_t count = 0;
            cdr >> count;
            vec.resize(count);
            for (auto& m : vec)
                m.deserialize(cdr);
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            auto count = static_cast<uint32_t>(vec.size());
            size_t size = calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), count, ca);
            for (const auto& m : vec)
                size += m.serialized_size(calc, ca);
            return size;
        }
    };

    // ---- OrMap ---------------------------------------------------------------
    // Full graph snapshot exchanged during the join handshake.

    struct OrMap : public ISerializable<OrMap>
    {
        uint32_t                            to_id{};
        int32_t                             id{};
        uint32_t                            protocol_version{DSR_PROTOCOL_VERSION};
        std::map<uint64_t, MvregNodeMsg>    m;
        dot_context                         cbase;

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            cdr << to_id;
            cdr << id;
            cdr << protocol_version;
            auto count = static_cast<uint32_t>(m.size());
            cdr << count;
            for (const auto& [k, v] : m) {
                cdr << k;
                v.serialize(cdr);
            }
            cbase.serialize(cdr);
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            cdr >> to_id;
            cdr >> id;
            cdr >> protocol_version;
            uint32_t count = 0;
            cdr >> count;
            m.clear();
            for (uint32_t i = 0; i < count; ++i) {
                uint64_t k = 0;
                cdr >> k;
                MvregNodeMsg v;
                v.deserialize(cdr);
                m.emplace(k, std::move(v));
            }
            cbase.deserialize(cdr);
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            size_t size = 0;
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), to_id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), protocol_version, ca);
            auto count = static_cast<uint32_t>(m.size());
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), count, ca);
            for (const auto& [k, v] : m) {
                size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(4), k, ca);
                size += v.serialized_size(calc, ca);
            }
            size += cbase.serialized_size(calc, ca);
            return size;
        }
    };

} // namespace DSR
