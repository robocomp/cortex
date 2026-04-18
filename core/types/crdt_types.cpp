//
// Created by juancarlos on 8/6/20.
//


#include <dsr/core/types/crdt_types.h>
#include <fastcdr/Cdr.h>
#include <fastcdr/CdrSizeCalculator.hpp>

namespace DSR {

    void CRDTEdge::to(uint64_t  _to)
    {
        m_to = _to;
    }

    uint64_t  CRDTEdge::to() const
    {
        return m_to;
    }

    void CRDTEdge::type(const std::string &type)
    {
        m_type = type;
    }

    void CRDTEdge::type(std::string &&type)
    {
        m_type = std::move(type);
    }

    const std::string &CRDTEdge::type() const 
    {
        return m_type;
    }

    std::string &CRDTEdge::type()
    {
        return m_type;
    }

    void CRDTEdge::from(uint64_t from)
    {
        m_from = from;
    }

    uint64_t  CRDTEdge::from() const
    {
        return m_from;
    }

    void CRDTEdge::attrs(const std::map<std::string, mvreg<CRDTAttribute>> &attrs)
    {
        m_attrs = attrs;
    }

    void CRDTEdge::attrs(std::map<std::string, mvreg<CRDTAttribute>> &&attrs)
    {
        m_attrs = std::move(attrs);
    }

    const std::map<std::string, mvreg<CRDTAttribute>> &CRDTEdge::attrs() const
    {
        return m_attrs;
    }

    std::map<std::string, mvreg<CRDTAttribute>> &CRDTEdge::attrs()
    {
        return m_attrs;
    }

    void CRDTEdge::agent_id(uint32_t agent_id)
    {
        m_agent_id = agent_id;
    }

    uint32_t CRDTEdge::agent_id() const
    {
        return m_agent_id;
    }

    void CRDTEdge::serialize_impl(eprosima::fastcdr::Cdr& cdr) const
    {
        cdr << m_to;
        cdr << m_from;
        cdr << m_type;
        cdr << m_agent_id;
        // serialize attrs map: uint32_t count, then key + value
        auto count = static_cast<uint32_t>(m_attrs.size());
        cdr << count;
        for (const auto &[k, v] : m_attrs) {
            cdr << k;
            v.serialize(cdr);
        }
    }

    void CRDTEdge::deserialize_impl(eprosima::fastcdr::Cdr& cdr)
    {
        cdr >> m_to;
        cdr >> m_from;
        cdr >> m_type;
        cdr >> m_agent_id;
        uint32_t count = 0;
        cdr >> count;
        m_attrs.clear();
        for (uint32_t i = 0; i < count; ++i) {
            std::string key;
            cdr >> key;
            mvreg<CRDTAttribute> val;
            val.deserialize(cdr);
            m_attrs.emplace(std::move(key), std::move(val));
        }
    }

    size_t CRDTEdge::serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
    {
        size_t size = 0;
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), m_to, ca);
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), m_from, ca);
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), m_type, ca);
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), m_agent_id, ca);
        auto count = static_cast<uint32_t>(m_attrs.size());
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(4), count, ca);
        for (const auto &[k, v] : m_attrs) {
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(5), k, ca);
            size += v.serialized_size(calc, ca);
        }
        return size;
    }


    void CRDTNode::type(const std::string &type)
    {
        m_type = type;
    }

    void CRDTNode::type(std::string &&type)
    {
        m_type = std::move(type);
    }

    const std::string &CRDTNode::type() const
    {
        return m_type;
    }

    std::string &CRDTNode::type()
    {
        return m_type;
    }

    void CRDTNode::name(const std::string &name)
    {
        m_name = name;
    }

    void CRDTNode::name(std::string &&name)
    {
        m_name = std::move(name);
    }

    const std::string &CRDTNode::name() const
    {
        return m_name;
    }

    std::string &CRDTNode::name()
    {
        return m_name;
    }

    void CRDTNode::id(uint64_t id)
    {
        m_id = id;
    }

    uint64_t CRDTNode::id() const
    {
        return m_id;
    }

    void CRDTNode::agent_id(uint32_t agent_id)
    {
        m_agent_id = agent_id;
    }

    uint32_t CRDTNode::agent_id() const
    {
        return m_agent_id;
    }

    void CRDTNode::attrs(const std::map<std::string, mvreg<CRDTAttribute>> &attrs)
    {
        m_attrs = attrs;
    }

    void CRDTNode::attrs(std::map<std::string, mvreg<CRDTAttribute>> &&attrs)
    {
        m_attrs = std::move(attrs);
    }

    std::map<std::string, mvreg<CRDTAttribute>> &CRDTNode::attrs() &
    {
        return m_attrs;
    }

    const std::map<std::string, mvreg<CRDTAttribute>> &CRDTNode::attrs() const &
    {
        return m_attrs;
    }

    void CRDTNode::fano(const std::map<std::pair<uint64_t , std::string>, mvreg<CRDTEdge>> &fano)
    {
        m_fano = fano;
    }

    void CRDTNode::fano(std::map<std::pair<uint64_t, std::string>, mvreg<CRDTEdge>> &&fano)
    {
        m_fano = std::move(fano);
    }

    std::map<std::pair<uint64_t, std::string>, mvreg<CRDTEdge>> &CRDTNode::fano()
    {
        return m_fano;
    }

    const std::map<std::pair<uint64_t, std::string>, mvreg<CRDTEdge>> &CRDTNode::fano() const
    {
        return m_fano;
    }


    void CRDTNode::serialize_impl(eprosima::fastcdr::Cdr& cdr) const
    {
        cdr << m_id;
        cdr << m_type;
        cdr << m_name;
        cdr << m_agent_id;
        // attrs map
        auto attr_count = static_cast<uint32_t>(m_attrs.size());
        cdr << attr_count;
        for (const auto &[k, v] : m_attrs) {
            cdr << k;
            v.serialize(cdr);
        }
        // fano map: key is pair<uint64_t, string>
        auto fano_count = static_cast<uint32_t>(m_fano.size());
        cdr << fano_count;
        for (const auto &[k, v] : m_fano) {
            cdr << k.first;
            cdr << k.second;
            v.serialize(cdr);
        }
    }

    void CRDTNode::deserialize_impl(eprosima::fastcdr::Cdr& cdr)
    {
        cdr >> m_id;
        cdr >> m_type;
        cdr >> m_name;
        cdr >> m_agent_id;
        uint32_t attr_count = 0;
        cdr >> attr_count;
        m_attrs.clear();
        for (uint32_t i = 0; i < attr_count; ++i) {
            std::string key;
            cdr >> key;
            mvreg<CRDTAttribute> val;
            val.deserialize(cdr);
            m_attrs.emplace(std::move(key), std::move(val));
        }
        uint32_t fano_count = 0;
        cdr >> fano_count;
        m_fano.clear();
        for (uint32_t i = 0; i < fano_count; ++i) {
            uint64_t fano_to = 0;
            std::string fano_type;
            cdr >> fano_to;
            cdr >> fano_type;
            mvreg<CRDTEdge> val;
            val.deserialize(cdr);
            m_fano.emplace(std::make_pair(fano_to, std::move(fano_type)), std::move(val));
        }
    }

    size_t CRDTNode::serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
    {
        size_t size = 0;
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), m_id, ca);
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(1), m_type, ca);
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(2), m_name, ca);
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(3), m_agent_id, ca);
        auto attr_count = static_cast<uint32_t>(m_attrs.size());
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(4), attr_count, ca);
        for (const auto &[k, v] : m_attrs) {
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(5), k, ca);
            size += v.serialized_size(calc, ca);
        }
        auto fano_count = static_cast<uint32_t>(m_fano.size());
        size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(6), fano_count, ca);
        for (const auto &[k, v] : m_fano) {
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(7), k.first, ca);
            size += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(8), k.second, ca);
            size += v.serialized_size(calc, ca);
        }
        return size;
    }

}

