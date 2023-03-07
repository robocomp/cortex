//
// Created by juancarlos on 8/6/20.
//


#include <dsr/core/types/crdt_types.h>
#include <dsr/core/types/translator.h>

namespace DSR {

    void CRDTEdge::to(uint64_t _to)
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
    bool CRDTEdge::operator==(const CRDTEdge &rhs) const
    {
        if (this == &rhs)
        {
            return true;
        }
        if (m_type != rhs.m_type || from() != rhs.from() || to() != rhs.to() || attrs() != rhs.attrs())
        {
            return false;
        }
        return true;
    }
    bool CRDTEdge::operator<(const CRDTEdge &rhs) const
    {
        if (this == &rhs)
        {
            return false;
        }
        if (m_type < rhs.m_type)
        {
            return true;
        }
        else if (rhs.m_type < m_type)
        {
            return false;
        }
        return false;
    }
    bool CRDTEdge::operator!=(const CRDTEdge &rhs) const
    {
        return !operator==(rhs);
    }
    bool CRDTEdge::operator<=(const CRDTEdge &rhs) const
    {
        return operator<(rhs) || operator==(rhs);
    }
    bool CRDTEdge::operator>(const CRDTEdge &rhs) const
    {
        return !operator<(rhs) && !operator==(rhs);
    }
    bool CRDTEdge::operator>=(const CRDTEdge &rhs) const
    {
        return !operator<(rhs);
    }
    std::ostream &operator<<(std::ostream &output, const CRDTEdge &rhs)
    {
        output << "EdgeAttribs[" << rhs.m_type << ", from:" << std::to_string(rhs.from())
               << "-> to:" << std::to_string(rhs.to()) << " Attribs:[";
        for (const auto &v : rhs.attrs())
            output << v.first << ":" << v.second << " - ";
        output << "]]";
        return output;
    }

    CRDTEdge::CRDTEdge() : m_to(0), m_from(0), m_agent_id(0), m_timestamp(0) {}

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
    CRDTNode::CRDTNode(const CRDTNode &x)
    {
        m_type = x.m_type;
        m_name = x.m_name;
        m_id = x.m_id;
        m_agent_id = x.m_agent_id;
        m_attrs = x.m_attrs;
        m_fano = x.m_fano;
    }
    bool CRDTNode::operator==(const CRDTNode &rhs) const
    {
        if (this == &rhs)
        {
            return true;
        }
        if (id() != rhs.id() || type() != rhs.type() || attrs() != rhs.attrs() || fano() != rhs.fano())
        {
            return false;
        }
        return true;
    }
    bool CRDTNode::operator<(const CRDTNode &rhs) const
    {
        if (this == &rhs)
        {
            return false;
        }
        if (id() < rhs.id())
        {
            return true;
        }
        else if (rhs.id() < id())
        {
            return false;
        }
        return false;
    }
    bool CRDTNode::operator!=(const CRDTNode &rhs) const
    {
        return !operator==(rhs);
    }
    bool CRDTNode::operator<=(const CRDTNode &rhs) const
    {
        return operator<(rhs) || operator==(rhs);
    }
    bool CRDTNode::operator>(const CRDTNode &rhs) const
    {
        return !operator<(rhs) && !operator==(rhs);
    }
    bool CRDTNode::operator>=(const CRDTNode &rhs) const
    {
        return !operator<(rhs);
    }
    std::ostream &operator<<(std::ostream &output, CRDTNode &rhs)
    {
        output << "Node:[" << std::to_string(rhs.id()) << "," << rhs.name() << "," << rhs.type()
               << "], Attribs:[";
        for (const auto &v : rhs.attrs())
            output << v.first << ":(" << v.second << ");";
        output << "], FanOut:[";
        for (auto &v : rhs.fano())
            output << "[ " << std::to_string(v.first.first) << " " << v.first.second << "] "
                   << ":(" << v.second << ");";
        output << "]";
        return output;
    }

}

