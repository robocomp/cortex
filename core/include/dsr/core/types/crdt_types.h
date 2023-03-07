//
// Created by juancarlos on 8/6/20.
//

#pragma once

#include "../crdt/delta_crdt.h"
#include "../utils.h"
#include "common_types.h"

#include <iostream>
#include <map>
#include <unordered_map>
#include <variant>

namespace DSR
{

    typedef Attribute CRDTAttribute;

    class CRDTEdge
    {
    public:
        CRDTEdge();

        ~CRDTEdge() = default;

        void to(uint64_t _to);

        [[nodiscard]] uint64_t to() const;

        void type(const std::string &type);

        void type(std::string &&type);

        [[nodiscard]] const std::string &type() const;

        [[nodiscard]] std::string &type();

        void from(uint64_t from);

        [[nodiscard]] uint64_t from() const;

        void attrs(const std::map<std::string, mvreg<CRDTAttribute>> &attrs);

        void attrs(std::map<std::string, mvreg<CRDTAttribute>> &&attrs);

        [[nodiscard]] const std::map<std::string, mvreg<CRDTAttribute>> &attrs() const;

        [[nodiscard]] std::map<std::string, mvreg<CRDTAttribute>> &attrs();

        void agent_id(uint32_t agent_id);

        [[nodiscard]] uint32_t agent_id() const;

        bool operator==(const CRDTEdge &rhs) const;

        bool operator<(const CRDTEdge &rhs) const;

        bool operator!=(const CRDTEdge &rhs) const;

        bool operator<=(const CRDTEdge &rhs) const;

        bool operator>(const CRDTEdge &rhs) const;

        bool operator>=(const CRDTEdge &rhs) const;

        friend std::ostream &operator<<(std::ostream &output, const CRDTEdge &rhs);;

    private:
        uint64_t m_to;
        uint64_t m_from;
        std::string m_type;
        std::map<std::string, mvreg<CRDTAttribute>> m_attrs;
        uint32_t m_agent_id;
        uint64_t m_timestamp;
    };

    class CRDTNode
    {

    public:
        CRDTNode() : m_id(0), m_agent_id(0) {}

        ~CRDTNode() = default;

        CRDTNode(const CRDTNode &x);

        void type(const std::string &type);

        void type(std::string &&type);

        [[nodiscard]] const std::string &type() const;

        [[nodiscard]] std::string &type();

        void name(const std::string &name);

        void name(std::string &&name);

        [[nodiscard]] const std::string &name() const;

        [[nodiscard]] std::string &name();

        void id(uint64_t id);

        [[nodiscard]] uint64_t id() const;

        void agent_id(uint32_t agent_id);

        [[nodiscard]] uint32_t agent_id() const;

        void attrs(const std::map<std::string, mvreg<CRDTAttribute>> &attrs);

        void attrs(std::map<std::string, mvreg<CRDTAttribute>> &&attrs);

        [[nodiscard]] std::map<std::string, mvreg<CRDTAttribute>> &attrs() &;

        [[nodiscard]] const std::map<std::string, mvreg<CRDTAttribute>> &attrs() const &;

        void fano(const std::map<std::pair<uint64_t, std::string>, mvreg<CRDTEdge>> &fano);

        void fano(std::map<std::pair<uint64_t, std::string>, mvreg<CRDTEdge>> &&fano);

        [[nodiscard]] std::map<std::pair<uint64_t, std::string>, mvreg<CRDTEdge>> &fano();

        [[nodiscard]] const std::map<std::pair<uint64_t, std::string>, mvreg<CRDTEdge>> &fano() const;

        bool operator==(const CRDTNode &rhs) const;

        bool operator<(const CRDTNode &rhs) const;

        bool operator!=(const CRDTNode &rhs) const;

        bool operator<=(const CRDTNode &rhs) const;

        bool operator>(const CRDTNode &rhs) const;

        bool operator>=(const CRDTNode &rhs) const;

        friend std::ostream &operator<<(std::ostream &output, CRDTNode &rhs);

    private:

        uint64_t m_id;
        std::string m_type;
        std::string m_name;
        std::map<std::string, mvreg<CRDTAttribute>> m_attrs;
        std::map<std::pair<uint64_t, std::string>, mvreg<CRDTEdge>> m_fano;
        uint32_t m_agent_id;
        uint64_t m_timestamp;
    };

}  // namespace DSR

