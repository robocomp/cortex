#pragma once

#include "dsr/core/types/crdt_types.h"
#include "dsr/core/serialization/serializable.h"
#include <map>
#include <string>
#include <vector>

namespace DSR {

    enum struct SyncMode : uint8_t {
        CRDT = 0,
        LWW = 1,
    };

    constexpr uint8_t sync_mode_wire_value(SyncMode mode) noexcept
    {
        return static_cast<uint8_t>(mode);
    }

    inline constexpr uint32_t DSR_PROTOCOL_VERSION = 1;

    // ---- GraphRequest --------------------------------------------------------
    // Sent by an agent that wants to receive the full graph snapshot.

    struct GraphRequest : public ISerializable<GraphRequest>
    {
        std::string from;
        int32_t     id{};
        uint32_t    protocol_version{DSR_PROTOCOL_VERSION};
        uint8_t     sync_mode{0};

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            cdr << from;
            cdr << id;
            cdr << protocol_version;
            cdr << sync_mode;
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            cdr >> from;
            cdr >> id;
            cdr >> protocol_version;
            cdr >> sync_mode;
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            size_t size = 0;
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), from, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), protocol_version, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), sync_mode, ca);
            return size;
        }
    };

    // ---- MvregNodeMsg --------------------------------------------------------
    // Delta or full mvreg<CRDT::Node> plus routing metadata.

    struct MvregNodeMsg : public ISerializable<MvregNodeMsg>
    {
        mvreg<CRDT::Node> dk;
        uint64_t        id{};
        uint32_t        agent_id{};
        uint64_t        timestamp{};
        uint32_t        protocol_version{DSR_PROTOCOL_VERSION};
        uint8_t         sync_mode{0};

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            dk.serialize(cdr);
            cdr << id;
            cdr << agent_id;
            cdr << timestamp;
            cdr << protocol_version;
            cdr << sync_mode;
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            dk.deserialize(cdr);
            cdr >> id;
            cdr >> agent_id;
            cdr >> timestamp;
            cdr >> protocol_version;
            cdr >> sync_mode;
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            size_t size = dk.serialized_size(calc, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), agent_id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), timestamp, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), protocol_version, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(4), sync_mode, ca);
            return size;
        }
    };

    // ---- MvregEdgeMsg --------------------------------------------------------

    struct MvregEdgeMsg : public ISerializable<MvregEdgeMsg>
    {
        mvreg<CRDT::Edge> dk;
        uint64_t        id{};      // "from" node id
        uint64_t        to{};
        uint64_t        from{};
        std::string     type;
        uint32_t        agent_id{};
        uint64_t        timestamp{};
        uint32_t        protocol_version{DSR_PROTOCOL_VERSION};
        uint8_t         sync_mode{0};

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
            cdr << sync_mode;
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
            cdr >> sync_mode;
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
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(7), sync_mode, ca);
            return size;
        }
    };

    // ---- MvregNodeAttrMsg ----------------------------------------------------

    struct MvregNodeAttrMsg : public ISerializable<MvregNodeAttrMsg>
    {
        mvreg<Attribute> dk;
        uint64_t             id{};        // publisher/delta id
        uint64_t             node{};      // owning node
        std::string          attr_name;
        uint32_t             agent_id{};
        uint64_t             timestamp{};
        uint32_t             protocol_version{DSR_PROTOCOL_VERSION};
        uint8_t              sync_mode{0};

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            dk.serialize(cdr);
            cdr << id;
            cdr << node;
            cdr << attr_name;
            cdr << agent_id;
            cdr << timestamp;
            cdr << protocol_version;
            cdr << sync_mode;
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
            cdr >> sync_mode;
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
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(6), sync_mode, ca);
            return size;
        }
    };

    // ---- MvregEdgeAttrMsg ----------------------------------------------------

    struct MvregEdgeAttrMsg : public ISerializable<MvregEdgeAttrMsg>
    {
        mvreg<Attribute> dk;
        uint64_t             id{};      // "from" node id
        uint64_t             from_node{};
        uint64_t             to_node{};
        std::string          type;
        std::string          attr_name;
        uint32_t             agent_id{};
        uint64_t             timestamp{};
        uint32_t             protocol_version{DSR_PROTOCOL_VERSION};
        uint8_t              sync_mode{0};

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
            cdr << sync_mode;
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
            cdr >> sync_mode;
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
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(8), sync_mode, ca);
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
        uint8_t                             sync_mode{0};
        std::map<uint64_t, MvregNodeMsg>    m;
        dot_context                         cbase;

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            cdr << to_id;
            cdr << id;
            cdr << protocol_version;
            cdr << sync_mode;
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
            cdr >> sync_mode;
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
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), sync_mode, ca);
            auto count = static_cast<uint32_t>(m.size());
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(4), count, ca);
            for (const auto& [k, v] : m) {
                size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(5), k, ca);
                size += v.serialized_size(calc, ca);
            }
            size += cbase.serialized_size(calc, ca);
            return size;
        }
    };

    // ---- LWWNodeMsg ----------------------------------------------------------

    struct LWWNodeMsg : public ISerializable<LWWNodeMsg>
    {
        uint64_t                        id{};
        std::string                     type;
        std::string                     name;
        std::map<std::string, Attribute> attrs;
        uint32_t                        agent_id{};
        uint64_t                        timestamp{};
        bool                            deleted{};
        uint32_t                        protocol_version{DSR_PROTOCOL_VERSION};
        uint8_t                         sync_mode{0};

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            cdr << id;
            cdr << type;
            cdr << name;
            auto count = static_cast<uint32_t>(attrs.size());
            cdr << count;
            for (const auto& [key, value] : attrs) {
                cdr << key;
                value.serialize(cdr);
            }
            cdr << agent_id;
            cdr << timestamp;
            cdr << deleted;
            cdr << protocol_version;
            cdr << sync_mode;
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            cdr >> id;
            cdr >> type;
            cdr >> name;
            uint32_t count = 0;
            cdr >> count;
            attrs.clear();
            for (uint32_t i = 0; i < count; ++i) {
                std::string key;
                Attribute value;
                cdr >> key;
                value.deserialize(cdr);
                attrs.emplace(std::move(key), std::move(value));
            }
            cdr >> agent_id;
            cdr >> timestamp;
            cdr >> deleted;
            cdr >> protocol_version;
            cdr >> sync_mode;
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            size_t size = 0;
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), type, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), name, ca);
            auto count = static_cast<uint32_t>(attrs.size());
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), count, ca);
            for (const auto& [key, value] : attrs) {
                size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(4), key, ca);
                size += value.serialized_size(calc, ca);
            }
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(5), agent_id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(6), timestamp, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(7), deleted, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(8), protocol_version, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(9), sync_mode, ca);
            return size;
        }
    };

    // ---- LWWEdgeMsg ----------------------------------------------------------

    struct LWWEdgeMsg : public ISerializable<LWWEdgeMsg>
    {
        uint64_t                        from{};
        uint64_t                        to{};
        std::string                     type;
        std::map<std::string, Attribute> attrs;
        uint32_t                        agent_id{};
        uint64_t                        timestamp{};
        bool                            deleted{};
        uint32_t                        protocol_version{DSR_PROTOCOL_VERSION};
        uint8_t                         sync_mode{0};

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            cdr << from;
            cdr << to;
            cdr << type;
            auto count = static_cast<uint32_t>(attrs.size());
            cdr << count;
            for (const auto& [key, value] : attrs) {
                cdr << key;
                value.serialize(cdr);
            }
            cdr << agent_id;
            cdr << timestamp;
            cdr << deleted;
            cdr << protocol_version;
            cdr << sync_mode;
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            cdr >> from;
            cdr >> to;
            cdr >> type;
            uint32_t count = 0;
            cdr >> count;
            attrs.clear();
            for (uint32_t i = 0; i < count; ++i) {
                std::string key;
                Attribute value;
                cdr >> key;
                value.deserialize(cdr);
                attrs.emplace(std::move(key), std::move(value));
            }
            cdr >> agent_id;
            cdr >> timestamp;
            cdr >> deleted;
            cdr >> protocol_version;
            cdr >> sync_mode;
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            size_t size = 0;
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), from, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), to, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), type, ca);
            auto count = static_cast<uint32_t>(attrs.size());
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), count, ca);
            for (const auto& [key, value] : attrs) {
                size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(4), key, ca);
                size += value.serialized_size(calc, ca);
            }
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(5), agent_id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(6), timestamp, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(7), deleted, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(8), protocol_version, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(9), sync_mode, ca);
            return size;
        }
    };

    // ---- LWWNodeAttrMsg ------------------------------------------------------

    struct LWWNodeAttrMsg : public ISerializable<LWWNodeAttrMsg>
    {
        uint64_t    node_id{};
        std::string attr_name;
        Attribute   value;
        uint32_t    agent_id{};
        uint64_t    timestamp{};
        bool        deleted{};
        uint32_t    protocol_version{DSR_PROTOCOL_VERSION};
        uint8_t     sync_mode{0};

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            cdr << node_id;
            cdr << attr_name;
            value.serialize(cdr);
            cdr << agent_id;
            cdr << timestamp;
            cdr << deleted;
            cdr << protocol_version;
            cdr << sync_mode;
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            cdr >> node_id;
            cdr >> attr_name;
            value.deserialize(cdr);
            cdr >> agent_id;
            cdr >> timestamp;
            cdr >> deleted;
            cdr >> protocol_version;
            cdr >> sync_mode;
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            size_t size = 0;
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), node_id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), attr_name, ca);
            size += value.serialized_size(calc, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), agent_id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), timestamp, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(4), deleted, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(5), protocol_version, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(6), sync_mode, ca);
            return size;
        }
    };

    // ---- LWWEdgeAttrMsg ------------------------------------------------------

    struct LWWEdgeAttrMsg : public ISerializable<LWWEdgeAttrMsg>
    {
        uint64_t    from{};
        uint64_t    to{};
        std::string type;
        std::string attr_name;
        Attribute   value;
        uint32_t    agent_id{};
        uint64_t    timestamp{};
        bool        deleted{};
        uint32_t    protocol_version{DSR_PROTOCOL_VERSION};
        uint8_t     sync_mode{0};

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            cdr << from;
            cdr << to;
            cdr << type;
            cdr << attr_name;
            value.serialize(cdr);
            cdr << agent_id;
            cdr << timestamp;
            cdr << deleted;
            cdr << protocol_version;
            cdr << sync_mode;
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            cdr >> from;
            cdr >> to;
            cdr >> type;
            cdr >> attr_name;
            value.deserialize(cdr);
            cdr >> agent_id;
            cdr >> timestamp;
            cdr >> deleted;
            cdr >> protocol_version;
            cdr >> sync_mode;
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            size_t size = 0;
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), from, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), to, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), type, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), attr_name, ca);
            size += value.serialized_size(calc, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(4), agent_id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(5), timestamp, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(6), deleted, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(7), protocol_version, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(8), sync_mode, ca);
            return size;
        }
    };

    // ---- LWWNodeAttrVec ------------------------------------------------------

    struct LWWNodeAttrVec : public ISerializable<LWWNodeAttrVec>
    {
        std::vector<LWWNodeAttrMsg> vec;

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            auto count = static_cast<uint32_t>(vec.size());
            cdr << count;
            for (const auto& item : vec) {
                item.serialize(cdr);
            }
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            uint32_t count = 0;
            cdr >> count;
            vec.resize(count);
            for (auto& item : vec) {
                item.deserialize(cdr);
            }
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            auto count = static_cast<uint32_t>(vec.size());
            size_t size = calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), count, ca);
            for (const auto& item : vec) {
                size += item.serialized_size(calc, ca);
            }
            return size;
        }
    };

    // ---- LWWEdgeAttrVec ------------------------------------------------------

    struct LWWEdgeAttrVec : public ISerializable<LWWEdgeAttrVec>
    {
        std::vector<LWWEdgeAttrMsg> vec;

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            auto count = static_cast<uint32_t>(vec.size());
            cdr << count;
            for (const auto& item : vec) {
                item.serialize(cdr);
            }
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            uint32_t count = 0;
            cdr >> count;
            vec.resize(count);
            for (auto& item : vec) {
                item.deserialize(cdr);
            }
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            auto count = static_cast<uint32_t>(vec.size());
            size_t size = calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), count, ca);
            for (const auto& item : vec) {
                size += item.serialized_size(calc, ca);
            }
            return size;
        }
    };

    // ---- LWWGraphSnapshot ----------------------------------------------------

    struct LWWGraphSnapshot : public ISerializable<LWWGraphSnapshot>
    {
        uint32_t                to_id{};
        int32_t                 id{};
        uint64_t                tombstone_window_ms{};
        uint32_t                protocol_version{DSR_PROTOCOL_VERSION};
        uint8_t                 sync_mode{0};
        std::vector<LWWNodeMsg> nodes;
        std::vector<LWWEdgeMsg> edges;

        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
        {
            cdr << to_id;
            cdr << id;
            cdr << tombstone_window_ms;
            cdr << protocol_version;
            cdr << sync_mode;
            auto node_count = static_cast<uint32_t>(nodes.size());
            cdr << node_count;
            for (const auto& node : nodes) {
                node.serialize(cdr);
            }
            auto edge_count = static_cast<uint32_t>(edges.size());
            cdr << edge_count;
            for (const auto& edge : edges) {
                edge.serialize(cdr);
            }
        }

        void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
        {
            cdr >> to_id;
            cdr >> id;
            cdr >> tombstone_window_ms;
            cdr >> protocol_version;
            cdr >> sync_mode;
            uint32_t node_count = 0;
            cdr >> node_count;
            nodes.resize(node_count);
            for (auto& node : nodes) {
                node.deserialize(cdr);
            }
            uint32_t edge_count = 0;
            cdr >> edge_count;
            edges.resize(edge_count);
            for (auto& edge : edges) {
                edge.deserialize(cdr);
            }
        }

        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
        {
            size_t size = 0;
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), to_id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), id, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), tombstone_window_ms, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), protocol_version, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(4), sync_mode, ca);
            auto node_count = static_cast<uint32_t>(nodes.size());
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(5), node_count, ca);
            for (const auto& node : nodes) {
                size += node.serialized_size(calc, ca);
            }
            auto edge_count = static_cast<uint32_t>(edges.size());
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(6), edge_count, ca);
            for (const auto& edge : edges) {
                size += edge.serialized_size(calc, ca);
            }
            return size;
        }
    };

} // namespace DSR
