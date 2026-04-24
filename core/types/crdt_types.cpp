//
// Created by juancarlos on 8/6/20.
//


#include <dsr/core/types/crdt_types.h>
#include <fastcdr/Cdr.h>
#include <fastcdr/CdrSizeCalculator.hpp>

namespace DSR {

    void CRDT::Edge::serialize_impl(eprosima::fastcdr::Cdr& cdr) const
    {
        cdr << to;
        cdr << from;
        cdr << type;
        cdr << agent_id;
        // serialize attrs map: uint32_t count, then key + value
        auto count = static_cast<uint32_t>(attrs.size());
        cdr << count;
        for (const auto &[k, v] : attrs) {
            cdr << k;
            v.serialize(cdr);
        }
    }

    void CRDT::Edge::deserialize_impl(eprosima::fastcdr::Cdr& cdr)
    {
        cdr >> to;
        cdr >> from;
        cdr >> type;
        cdr >> agent_id;
        uint32_t count = 0;
        cdr >> count;
        attrs.clear();
        for (uint32_t i = 0; i < count; ++i) {
            std::string key;
            cdr >> key;
            mvreg<Attribute> val;
            val.deserialize(cdr);
            attrs.emplace(std::move(key), std::move(val));
        }
    }

    size_t CRDT::Edge::serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
    {
        size_t size = 0;
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), to, ca);
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), from, ca);
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), type, ca);
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), agent_id, ca);
        auto count = static_cast<uint32_t>(attrs.size());
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(4), count, ca);
        for (const auto &[k, v] : attrs) {
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(5), k, ca);
            size += v.serialized_size(calc, ca);
        }
        return size;
    }

    void CRDT::Node::serialize_impl(eprosima::fastcdr::Cdr& cdr) const
    {
        cdr << id;
        cdr << type;
        cdr << name;
        cdr << agent_id;
        // attrs map
        auto attr_count = static_cast<uint32_t>(attrs.size());
        cdr << attr_count;
        for (const auto &[k, v] : attrs) {
            cdr << k;
            v.serialize(cdr);
        }
        // fano map: key is pair<uint64_t, string>
        auto fano_count = static_cast<uint32_t>(fano.size());
        cdr << fano_count;
        for (const auto &[k, v] : fano) {
            cdr << k.first;
            cdr << k.second;
            v.serialize(cdr);
        }
    }

    void CRDT::Node::deserialize_impl(eprosima::fastcdr::Cdr& cdr)
    {
        cdr >> id;
        cdr >> type;
        cdr >> name;
        cdr >> agent_id;
        uint32_t attr_count = 0;
        cdr >> attr_count;
        attrs.clear();
        for (uint32_t i = 0; i < attr_count; ++i) {
            std::string key;
            cdr >> key;
            mvreg<Attribute> val;
            val.deserialize(cdr);
            attrs.emplace(std::move(key), std::move(val));
        }
        uint32_t fano_count = 0;
        cdr >> fano_count;
        fano.clear();
        for (uint32_t i = 0; i < fano_count; ++i) {
            uint64_t fano_to = 0;
            std::string fano_type;
            cdr >> fano_to;
            cdr >> fano_type;
            mvreg<CRDT::Edge> val;
            val.deserialize(cdr);
            fano.emplace(std::make_pair(fano_to, std::move(fano_type)), std::move(val));
        }
    }

    size_t CRDT::Node::serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
    {
        size_t size = 0;
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), id, ca);
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), type, ca);
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), name, ca);
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), agent_id, ca);
        auto attr_count = static_cast<uint32_t>(attrs.size());
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(4), attr_count, ca);
        for (const auto &[k, v] : attrs) {
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(5), k, ca);
            size += v.serialized_size(calc, ca);
        }
        auto fano_count = static_cast<uint32_t>(fano.size());
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(6), fano_count, ca);
        for (const auto &[k, v] : fano) {
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(7), k.first, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(8), k.second, ca);
            size += v.serialized_size(calc, ca);
        }
        return size;
    }
}

