//
// Created by juancarlos on 1/7/20.
//

#include <dsr/core/types/user_types.h>

using namespace DSR;

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

    void Edge::attrs(const std::map<std::string, Attribute> &attrs)
    {
        m_attrs = attrs;
    }

    void Edge::agent_id(uint32_t agentId)
    {
        m_agent_id = agentId;
    }

    Edge::Edge(uint64_t to, uint64_t from, std::string type, std::map<std::string, Attribute> attrs, uint32_t agent_id)
        : m_to(to),
          m_from(from),
          m_type(std::move(type)),
          m_attrs{std::move(attrs)},
          m_agent_id(agent_id)
    {
        if (!edge_types::check_type(m_type))
        {
            throw std::runtime_error("Error, \"" + m_type + "\" is not a valid edge type");
        }
    }

    Edge::Edge(uint64_t to, uint64_t from, std::string type, uint32_t agent_id)
        : m_to(to),
          m_from(from),
          m_type(std::move(type)),
          m_attrs{},
          m_agent_id(agent_id)
    {
        if (!edge_types::check_type(m_type))
        {
            throw std::runtime_error("Error, \"" + m_type + "\" is not a valid edge type");
        }
    }

    Edge::Edge(uint64_t from, uint64_t to, std::string type, uint32_t agent_id, std::map<std::string, Attribute> attrs)
        : m_to(to),
          m_from(from),
          m_type(std::move(type)),
          m_attrs(std::move(attrs)),
          m_agent_id(agent_id)
    {
    }

    Edge::Edge(const CRDTEdge &edge)
    {
        m_agent_id = edge.agent_id();
        m_from = edge.from();
        m_to = edge.to();
        m_type = edge.type();
        for (const auto &[k, v] : edge.attrs())
        {
            assert(!v.dk.ds.empty());
            m_attrs.emplace(k, v.dk.ds.begin()->second);
        }
    }

    Edge::Edge(CRDTEdge &&edge)
    {
        m_agent_id = edge.agent_id();
        m_from = edge.from();
        m_to = edge.to();
        m_type = edge.type();
        for (auto &[k, v] : edge.attrs())
        {
            assert(!v.dk.ds.empty());
            m_attrs.emplace(k, std::move(v.dk.ds.begin()->second));
        }
    }

    Edge &Edge::operator=(const CRDTEdge &attr)
    {
        m_agent_id = attr.agent_id();
        m_from = attr.from();
        m_to = attr.to();
        m_type = attr.type();
        for (const auto &[k, v] : attr.attrs())
        {
            assert(!v.dk.ds.empty());
            m_attrs.emplace(k, v.dk.ds.begin()->second);
        }
        return *this;
    }

    bool Edge::operator==(const Edge &rhs) const
    {
        return m_to == rhs.m_to && m_from == rhs.m_from && m_type == rhs.m_type && m_attrs == rhs.m_attrs;
    }

    bool Edge::operator!=(const Edge &rhs) const
    {
        return !(rhs == *this);
    }

    bool Edge::operator<(const Edge &rhs) const
    {
        if (m_to < rhs.m_to) return true;
        if (rhs.m_to < m_to) return false;
        if (m_from < rhs.m_from) return true;
        if (rhs.m_from < m_from) return false;
        if (m_type < rhs.m_type) return true;
        if (rhs.m_type < m_type) return false;
        return true;
    }

    bool Edge::operator>(const Edge &rhs) const
    {
        return rhs < *this;
    }

    bool Edge::operator<=(const Edge &rhs) const
    {
        return !(rhs < *this);
    }

    bool Edge::operator>=(const Edge &rhs) const
    {
        return !(*this < rhs);
    }

    Attribute &Edge::operator[](const std::string &str)
    {
        // This can throw
        return m_attrs.at(str);
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

    void Node::fano(const std::map<std::pair<uint64_t, std::string>, Edge> &mFano)
    {
        m_fano = mFano;
    }

    void Node::agent_id(uint32_t agentId)
    {
        m_agent_id = agentId;
    }

    Node::Node(std::string type, uint32_t agent_id, std::map<std::string, Attribute> attrs,
               std::map<std::pair<uint64_t, std::string>, Edge> fano, std::string name)
        : m_id(0),
          m_type{std::move(type)},
          m_name{std::move(name)},
          m_attrs{std::move(attrs)},
          m_fano{std::move(fano)},
          m_agent_id(agent_id)
    {
    }

    Node::Node(uint64_t agent_id, std::string type)
        : m_id(0),
          m_type(std::move(type)),
          m_attrs{},
          m_fano{},
          m_agent_id(agent_id)
    {
        if (!node_types::check_type(m_type))
        {
            throw std::runtime_error("Error, \"" + m_type + "\" is not a valid node type");
        }
    }

    Node::Node(std::string type, uint32_t agent_id, std::map<std::string, Attribute> attrs,
               std::map<std::pair<uint64_t, std::string>, Edge> fano)
        : m_id(0),
          m_type(std::move(type)),
          m_attrs{std::move(attrs)},
          m_fano{std::move(fano)},
          m_agent_id(agent_id)
    {
        if (!node_types::check_type(m_type))
        {
            throw std::runtime_error("Error, \"" + m_type + "\" is not a valid node type");
        }
    }

    Node::Node(const CRDTNode &node)
    {
        m_agent_id = node.agent_id();
        m_id = node.id();
        m_name = node.name();
        m_type = node.type();
        for (const auto &[k, v] : node.attrs())
        {
            assert(!v.dk.ds.empty());
            m_attrs.emplace(k, v.dk.ds.begin()->second);
        }
        for (const auto &[k, v] : node.fano())
        {
            assert(!v.dk.ds.empty());
            m_fano.emplace(k, v.dk.ds.begin()->second);
        }
    }

    Node::Node(CRDTNode &&node)
    {
        m_agent_id = node.agent_id();
        m_id = node.id();
        m_name = node.name();
        m_type = node.type();
        for (auto &[k, v] : node.attrs())
        {
            assert(!v.dk.ds.empty());
            m_attrs.emplace(k, std::move(v.dk.ds.begin()->second));
        }
        for (auto &[k, v] : node.fano())
        {
            assert(!v.dk.ds.empty());
            m_fano.emplace(k, std::move(v.dk.ds.begin()->second));
        }
    }

    Node &Node::operator=(const CRDTNode &node)
    {
        m_agent_id = node.agent_id();
        m_id = node.id();
        m_name = node.name();
        m_type = node.type();
        for (const auto &[k, v] : node.attrs())
        {
            assert(!v.dk.ds.empty());
            m_attrs.emplace(k, v.dk.ds.begin()->second);
        }
        for (const auto &[k, v] : node.fano())
        {
            assert(!v.dk.ds.empty());
            m_fano.emplace(k, v.dk.ds.begin()->second);
        }

        return *this;
    }

    bool Node::operator==(const Node &rhs) const
    {
        return m_id == rhs.m_id && m_type == rhs.m_type && m_name == rhs.m_name && m_attrs == rhs.m_attrs &&
               m_fano == rhs.m_fano;
    }

    bool Node::operator!=(const Node &rhs) const
    {
        return !(rhs == *this);
    }

    bool Node::operator<(const Node &rhs) const
    {
        if (m_id < rhs.m_id) return true;
        if (rhs.m_id < m_id) return false;
        if (m_type < rhs.m_type) return true;
        if (rhs.m_type < m_type) return false;
        if (m_name < rhs.m_name) return true;
        if (rhs.m_name < m_name) return false;
        return true;
    }

    bool Node::operator>(const Node &rhs) const
    {
        return rhs < *this;
    }

    bool Node::operator<=(const Node &rhs) const
    {
        return !(rhs < *this);
    }

    bool Node::operator>=(const Node &rhs) const
    {
        return !(*this < rhs);
    }

    Attribute &Node::operator[](const std::string &str)
    {
        // This can throw
        return m_attrs.at(str);
    }

    Edge &Node::operator[](std::pair<uint64_t, std::string> &str)
    {
        // This can throw
        return m_fano.at(str);
    }
