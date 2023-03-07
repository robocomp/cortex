//
// Created by juancarlos on 1/7/20.
//
#pragma once

#include "common_types.h"
#include "crdt_types.h"
#include "type_checking/type_checker.h"

#include <cstdint>
#include <dsr/core/utils.h>
#include <functional>
#include <utility>

namespace DSR
{

    class Edge;

    inline const std::map<std::string, Attribute> default_attributes = {};
    inline const std::map<std::pair<uint64_t, std::string>, Edge> default_fano = {};
    inline const std::string default_str;

    class Edge
    {

    private:
        Edge(uint64_t from, uint64_t to, std::string type, uint32_t agent_id, std::map<std::string, Attribute> attrs);

    public:
        Edge() = default;
        ~Edge() = default;

        [[deprecated("Use Edge::create<example_edge_type>(...)")]] Edge(uint64_t to, uint64_t from, std::string type,
                                                                        uint32_t agent_id);

        [[deprecated("Use Edge::create<example_edge_type>(...)")]] Edge(uint64_t to, uint64_t from, std::string type,
                                                                        std::map<std::string, Attribute> attrs,
                                                                        uint32_t agent_id);


        template <typename edge_type>
        static Edge
        create(uint64_t from, uint64_t to,
               const std::map<std::string, Attribute> &attrs = default_attributes) requires(edge_type::edge_type)
        {
            return {from, to, std::string(edge_type::attr_name.data()), 0, attrs};
        }

        explicit Edge(const CRDTEdge &edge);

        explicit Edge(CRDTEdge &&edge);

        Edge &operator=(const CRDTEdge &attr);

        [[nodiscard]] uint64_t to() const;

        [[nodiscard]] uint64_t from() const;

        [[nodiscard]] const std::string &type() const;

        [[nodiscard]] std::string &type();

        [[nodiscard]] const std::map<std::string, Attribute> &attrs() const;

        [[nodiscard]] std::map<std::string, Attribute> &attrs();

        [[nodiscard]] uint32_t agent_id() const;

        void to(uint64_t to);

        void from(uint64_t from);

        void type(const std::string &type);

        void attrs(const std::map<std::string, Attribute> &attrs);

        void agent_id(uint32_t agent_id);

        bool operator==(const Edge &rhs) const;

        bool operator!=(const Edge &rhs) const;

        bool operator<(const Edge &rhs) const;

        bool operator>(const Edge &rhs) const;

        bool operator<=(const Edge &rhs) const;

        bool operator>=(const Edge &rhs) const;

        Attribute &operator[](const std::string &str);

    private:
        uint64_t m_to;
        uint64_t m_from;
        std::string m_type;
        std::map<std::string, Attribute> m_attrs;
        uint32_t m_agent_id;
        uint64_t m_timestamp;
    };

    class Node
    {
    private:
        Node(std::string type, uint32_t agent_id, std::map<std::string, Attribute> attrs,
             std::map<std::pair<uint64_t, std::string>, Edge> fano, std::string name = "");

    public:
        Node() = default;
        ~Node() = default;

        [[deprecated("Use Node::create<example_node_type>(...)")]] Node(uint64_t agent_id, std::string type);

        [[deprecated("Use Node::create<example_node_type>(...)")]] Node(
            std::string type, uint32_t agent_id, std::map<std::string, Attribute> attrs,
            std::map<std::pair<uint64_t, std::string>, Edge> fano);

        template <typename node_type>
        static Node create(
            const std::string &name = default_str, const std::map<std::string, Attribute> &attrs = default_attributes,
            const std::map<std::pair<uint64_t, std::string>, Edge> &fano = default_fano) requires(node_type::node_type)
        {
            return {std::string(node_type::attr_name.data()), 0, attrs, fano, name};
        }

        explicit Node(const CRDTNode &node);

        explicit Node(CRDTNode &&node);

        Node &operator=(const CRDTNode &node);

        [[nodiscard]] uint64_t id() const;

        [[nodiscard]] const std::string &type() const;

        [[nodiscard]] std::string &type();

        [[nodiscard]] const std::string &name() const;

        [[nodiscard]] std::string &name();

        [[nodiscard]] const std::map<std::string, Attribute> &attrs() const;

        [[nodiscard]] std::map<std::string, Attribute> &attrs();

        [[nodiscard]] const std::map<std::pair<uint64_t, std::string>, Edge> &fano() const;

        [[nodiscard]] std::map<std::pair<uint64_t, std::string>, Edge> &fano();

        [[nodiscard]] uint32_t agent_id() const;

        void id(uint64_t id);

        void type(const std::string &type);

        void name(const std::string &name);

        void attrs(const std::map<std::string, Attribute> &attrs);

        void fano(const std::map<std::pair<uint64_t, std::string>, Edge> &fano);

        void agent_id(uint32_t agent_id);

        bool operator==(const Node &rhs) const;

        bool operator!=(const Node &rhs) const;

        bool operator<(const Node &rhs) const;

        bool operator>(const Node &rhs) const;

        bool operator<=(const Node &rhs) const;

        bool operator>=(const Node &rhs) const;

        Attribute &operator[](const std::string &str);

        Edge &operator[](std::pair<uint64_t, std::string> &str);

    private:
        uint64_t m_id;
        std::string m_type;
        std::string m_name;
        std::map<std::string, Attribute> m_attrs;
        std::map<std::pair<uint64_t, std::string>, Edge> m_fano;
        uint32_t m_agent_id;
        uint64_t m_timestamp;
    };

}  // namespace DSR
