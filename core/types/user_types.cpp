//
// Created by juancarlos on 1/7/20.
//

#include <dsr/core/types/user_types.h>

namespace DSR {

    /////////////////////////////////////////////////////
    /// Edge
    ////////////////////////////////////////////////////

    uint64_t Edge::to() const
    {
        return m_to;
    }

    uint64_t Edge::from() const
    {
        return m_from;
    }

    const std::string &Edge::type() const
    {
        return m_type;
    }

    std::string &Edge::type()
    {
        return m_type;
    }

    const std::map<std::string, Attribute> &Edge::attrs() const
    {
        return m_attrs;
    }

    std::map<std::string, Attribute> &Edge::attrs()
     {
        return m_attrs;
    }

    uint32_t Edge::agent_id() const
    {
        return m_agent_id;
    }

    void Edge::to(uint64_t to)
    {
        m_to = to;
    }

    void Edge::from(uint64_t from)
    {
        m_from = from;
    }

    void Edge::type(const std::string &type)
    {
        if(!edge_types::check_type(type)) {
            throw std::runtime_error("Error, \"" + type + "\" is not a valid edge type");
        }
        m_type = type;
    }

    void Edge::attrs(const  std::map<std::string, Attribute> &attrs)
    {
        m_attrs = attrs;
    }

    void Edge::agent_id(uint32_t agentId)
    {
        m_agent_id = agentId;
    }

    /////////////////////////////////////////////////////
    /// Node
    ////////////////////////////////////////////////////

    uint64_t Node::id() const
    {
        return m_id;
    }

    const std::string &Node::type() const
    {
        return m_type;
    }

    std::string &Node::type()
    {
        return m_type;
    }

    const std::string &Node::name() const
    {
        return m_name;
    }

    std::string &Node::name()
    {
        return m_name;
    }

    const  std::map<std::string, Attribute> &Node::attrs() const
    {
        return m_attrs;
    }

    std::map<std::string, Attribute>& Node::attrs()
     {
        return m_attrs;
    }

    const  std::map<std::pair<uint64_t, std::string>, Edge > &Node::fano() const
    {
        return m_fano;
    }

    std::map<std::pair<uint64_t, std::string>, Edge > &Node::fano()
    {
        return m_fano;
    }

    uint32_t Node::agent_id() const
    {
        return m_agent_id;
    }

    void Node::id(uint64_t mId)
    {
        m_id = mId;
    }

    void Node::type (const std::string &type)
    {
        if(!node_types::check_type(type)) {
            throw std::runtime_error("Error, \"" + type + "\" is not a valid node type");
        }
        m_type = type;
    }

    void Node::name(const std::string &mName)
    {
        m_name = mName;
    }

    void Node::attrs(const  std::map<std::string, Attribute> &attrs)
    {
        m_attrs = attrs;
    }

    void Node::fano(const  std::map<std::pair<uint64_t, std::string>, Edge > &mFano)
    {
        m_fano = mFano;
    }

    void Node::agent_id(uint32_t agentId)
    {
        m_agent_id = agentId;
    }
}