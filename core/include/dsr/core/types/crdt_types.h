//
// Created by juancarlos on 8/6/20.
//

#ifndef DSR_CRDT_TYPES_H
#define DSR_CRDT_TYPES_H


#include <iostream>
#include <map>

#include "../crdt/delta_crdt.h"
#include "../topics/IDLGraph.hpp"
#include "common_types.h"

namespace DSR {

    typedef DSR::Attribute CRDTAttribute;

    class CRDTEdge
    {
    public:

        CRDTEdge() : m_to(0), m_from(0), m_agent_id(0) {}

        ~CRDTEdge() = default;

        explicit CRDTEdge (IDL::IDLEdge &&x) noexcept;

        CRDTEdge &operator=(IDL::IDLEdge &&x);

        void to(uint64_t  _to);

        [[nodiscard]] uint64_t  to() const;

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

        [[nodiscard]] IDL::IDLEdge to_IDL_edge(uint64_t id);


        bool operator==(const CRDTEdge &rhs) const
        {
            if (this == &rhs) {
                return true;
            }
            if (m_type != rhs.m_type || from() != rhs.from() || to() != rhs.to() || attrs() != rhs.attrs()) {
                return false;
            }
            return true;
        }

        bool operator<(const CRDTEdge &rhs) const
        {
            if (this == &rhs) {
                return false;
            }
            if (m_type < rhs.m_type) {
                return true;
            } else if (rhs.m_type < m_type) {
                return false;
            }
            return false;
        }

        bool operator!=(const CRDTEdge &rhs) const
        {
            return !operator==(rhs);
        }

        bool operator<=(const CRDTEdge &rhs) const
        {
            return operator<(rhs) || operator==(rhs);
        }

        bool operator>(const CRDTEdge &rhs) const
        {
            return !operator<(rhs) && !operator==(rhs);
        }

        bool operator>=(const CRDTEdge &rhs) const
        {
            return !operator<(rhs);
        }

        friend std::ostream &operator<<(std::ostream &output, const CRDTEdge &rhs)
        {
            output << "IDL::EdgeAttribs[" << rhs.m_type << ", from:" << std::to_string(rhs.from()) << "-> to:" << std::to_string(rhs.to())
                   << " Attribs:[";
            for (const auto &v : rhs.attrs())
                output << v.first << ":" << v.second << " - ";
            output << "]]";
            return output;
        };

    private:
        uint64_t m_to;
        std::string m_type;
        uint64_t  m_from;
        std::map<std::string, mvreg<CRDTAttribute>> m_attrs;
        uint32_t m_agent_id{};
    };

    class CRDTNode {

    public:

        CRDTNode() : m_id(0), m_agent_id(0) {}

        ~CRDTNode() = default;

        CRDTNode(const CRDTNode &x)
        {
            m_type = x.m_type;
            m_name = x.m_name;
            m_id = x.m_id;
            m_agent_id = x.m_agent_id;
            m_attrs = x.m_attrs;
            m_fano = x.m_fano;
        }

        explicit CRDTNode(IDL::IDLNode &&x);

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

        [[nodiscard]] IDL::IDLNode to_IDL_node(uint64_t id);

        bool operator==(const CRDTNode &rhs) const
        {
            if (this == &rhs) {
                return true;
            }
            if (id() != rhs.id() || type() != rhs.type() || attrs() != rhs.attrs() || fano() != rhs.fano()) {
                return false;
            }
            return true;
        }

        bool operator<(const CRDTNode &rhs) const
        {
            if (this == &rhs) {
                return false;
            }
            if (id() < rhs.id()) {
                return true;
            } else if (rhs.id() < id()) {
                return false;
            }
            return false;
        }

        bool operator!=(const CRDTNode &rhs) const
        {
            return !operator==(rhs);
        }

        bool operator<=(const CRDTNode &rhs) const
        {
            return operator<(rhs) || operator==(rhs);
        }

        bool operator>(const CRDTNode &rhs) const
        {
            return !operator<(rhs) && !operator==(rhs);
        }

        bool operator>=(const CRDTNode &rhs) const
        {
            return !operator<(rhs);
        }

        friend std::ostream &operator<<(std::ostream &output, CRDTNode &rhs)
        {
            output << "IDL::Node:[" << std::to_string(rhs.id()) << "," << rhs.name() << "," << rhs.type() << "], Attribs:[";
            for (const auto &v : rhs.attrs())
                output << v.first << ":(" << v.second << ");";
            output << "], FanOut:[";
            for (auto &v : rhs.fano())
                output << "[ " << std::to_string(v.first.first) << " " << v.first.second << "] " << ":(" << v.second << ");";
            output << "]";
            return output;
        }
    private:
        std::string m_type;
        std::string m_name;
        uint64_t m_id{};
        uint32_t m_agent_id{};
        std::map<std::string, mvreg<CRDTAttribute>> m_attrs;
        std::map<std::pair<uint64_t, std::string>, mvreg<CRDTEdge>> m_fano;
    };


}

#endif //DSR_CRDT_TYPES_H
