//
// Created by juancarlos on 8/6/20.
//

#ifndef DSR_CRDT_TYPES_H
#define DSR_CRDT_TYPES_H


#include <iostream>
#include <map>

#include "dsr/core/crdt/delta_crdt.h"
#include "dsr/core/serialization/serializable.h"
#include "dsr/core/types/common_types.h"

namespace DSR {
namespace CRDT {

    struct Edge : public ISerializable<Edge>
    {

        Edge() : to(0), from(0), agent_id(0) {}
        ~Edge() = default;

        // ---- ISerializable implementation ----
        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const;
        void deserialize_impl(eprosima::fastcdr::Cdr& cdr);
        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const;

        bool operator==(const CRDT::Edge &rhs) const
        {
            if (this == &rhs) {
                return true;
            }
            if (type != rhs.type || from != rhs.from || to != rhs.to || attrs != rhs.attrs) {
                return false;
            }
            return true;
        }

        bool operator<(const CRDT::Edge &rhs) const
        {
            if (this == &rhs) {
                return false;
            }
            if (type < rhs.type) {
                return true;
            } else if (rhs.type < type) {
                return false;
            }
            return false;
        }

        friend std::ostream &operator<<(std::ostream &output, const CRDT::Edge &rhs)
        {
            output << " CRDT::Edge [" << rhs.type << ", from:" << std::to_string(rhs.from) << "-> to:" << std::to_string(rhs.to)
                   << " Attribs:[";
            for (const auto &v : rhs.attrs)
                output << v.first << ":" << v.second << " - ";
            output << "]]";
            return output;
        };

        uint64_t to;
        std::string type;
        uint64_t  from;
        std::map<std::string, mvreg<Attribute>> attrs;
        uint32_t agent_id{};
    };

    struct Node : public ISerializable<CRDT::Node> 
    {

        Node() : id(0), agent_id(0) {}
        ~Node() = default;

        Node(const Node &x) = default;

        // ---- ISerializable implementation ----
        void serialize_impl(eprosima::fastcdr::Cdr& cdr) const;
        void deserialize_impl(eprosima::fastcdr::Cdr& cdr);
        size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const;

        bool operator==(const CRDT::Node &rhs) const
        {
            if (this == &rhs) {
                return true;
            }
            if (id != rhs.id || type != rhs.type || attrs != rhs.attrs || fano != rhs.fano) {
                return false;
            }
            return true;
        }

        bool operator<(const CRDT::Node &rhs) const
        {
            if (this == &rhs) {
                return false;
            }
            if (id < rhs.id) {
                return true;
            } else if (rhs.id < id) {
                return false;
            }
            return false;
        }

        friend std::ostream &operator<<(std::ostream &output, CRDT::Node &rhs)
        {
            output << "CRDT::Node: [" << std::to_string(rhs.id) << "," << rhs.name << "," << rhs.type << "], Attribs:[";
            for (const auto &v : rhs.attrs)
                output << v.first << ":(" << v.second << ");";
            output << "], FanOut:[";
            for (auto &v : rhs.fano)
                output << "[ " << std::to_string(v.first.first) << " " << v.first.second << "] " << ":(" << v.second << ");";
            output << "]";
            return output;
        }

        std::string type;
        std::string name;
        uint64_t id{};
        uint32_t agent_id{};
        std::map<std::string, mvreg<Attribute>> attrs;
        std::map<std::pair<uint64_t, std::string>, mvreg<CRDT::Edge>> fano;
    };

}
}

#endif //DSR_CRDT_TYPES_H
