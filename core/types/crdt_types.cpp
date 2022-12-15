//
// Created by juancarlos on 8/6/20.
//


#include <dsr/core/types/crdt_types.h>
#include <dsr/core/types/translator.h>

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

}

