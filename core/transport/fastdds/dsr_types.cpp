#include "dsr/core/crdt/delta_crdt.h"
#include "dsr/core/transport/fastdds/dds_types.h"
#include "dsr/core/types/common_types.h"
#include "dsr/core/types/crdt_types.h"

#include <array>
#include <cstdint>

using namespace eprosima::fastrtps;
using namespace eprosima::fastrtps::rtps;

template <typename T>
constexpr size_t serialized_size(const T &val, size_t current_alignment) {
    return serialized_size(val, current_alignment);
}

constexpr size_t serialized_size(const std::string &val, size_t current_alignment)
{
    size_t initial_alignment = current_alignment;
    current_alignment += 4 + eprosima::fastcdr::Cdr::alignment(current_alignment, 4) + val.size() + 1;
    return current_alignment - initial_alignment;
}

template <typename T>
constexpr size_t serialized_size(T val, size_t current_alignment) requires(std::integral<T> or std::floating_point<T>)
{
    size_t initial_alignment = current_alignment;
    current_alignment += sizeof(val) + eprosima::fastcdr::Cdr::alignment(current_alignment, sizeof(val));
    return current_alignment - initial_alignment;
}

constexpr size_t serialized_size(const DSR::ValType &val, size_t current_alignment)
{
    size_t initial_alignment = current_alignment;
    current_alignment += serialized_size((uint32_t)val.index(), current_alignment);
    switch (val.index())
    {
        case 0: current_alignment += serialized_size(std::get<std::string>(val), current_alignment); break;
        case 1:
        case 2:
        case 6: current_alignment += serialized_size(uint32_t(), current_alignment); break;
        case 3: current_alignment += serialized_size(std::get<std::vector<float>>(val), current_alignment); break;
        case 4: current_alignment += serialized_size(bool(), current_alignment); break;
        case 5: current_alignment += serialized_size(std::get<std::vector<uint8_t>>(val), current_alignment); break;
        case 7:
        case 8: current_alignment += serialized_size(uint64_t(), current_alignment); break;
        case 9: current_alignment += serialized_size(std::get<std::vector<uint64_t>>(val), current_alignment); break;
        case 10: current_alignment += serialized_size(std::get<std::array<float, 2>>(val), current_alignment); break;
        case 11: current_alignment += serialized_size(std::get<std::array<float, 3>>(val), current_alignment); break;
        case 12: current_alignment += serialized_size(std::get<std::array<float, 4>>(val), current_alignment); break;
        case 13: current_alignment += serialized_size(std::get<std::array<float, 6>>(val), current_alignment); break;
        default: break;
    }
    return current_alignment - initial_alignment;
}

template <typename P1, typename P2>
constexpr size_t serialized_size(const std::pair<P1, P2> &val, size_t current_alignment)
{
    size_t initial_alignment = current_alignment;
    current_alignment += serialized_size(val.first, current_alignment);
    current_alignment += serialized_size(val.second, current_alignment);
    return current_alignment - initial_alignment;
}

template <typename... P1>
constexpr size_t serialized_size(const std::tuple<P1...> &val, size_t current_alignment)
{
    size_t initial_alignment = current_alignment;
    ((current_alignment += serialized_size(std::get<P1>(val), current_alignment)), ...);
    return current_alignment - initial_alignment;
}

constexpr size_t serialized_size(const DSR::CRDTAttribute &val, size_t current_alignment)
{
    size_t initial_alignment = current_alignment;
    current_alignment += serialized_size(uint32_t(), current_alignment);
    current_alignment += serialized_size(val.value(), current_alignment);
    // timestamp
    current_alignment += serialized_size(uint64_t(), current_alignment);
    // agent_id
    current_alignment += serialized_size(uint32_t(), current_alignment);
    return current_alignment - initial_alignment;
}

template <typename P1, typename P2>
constexpr size_t serialized_size(const std::map<P1, P2> &val, size_t current_alignment)
{
    size_t initial_alignment = current_alignment;
    // encapsulation
    current_alignment += 4 + eprosima::fastcdr::Cdr::alignment(current_alignment, 4);
    for (const auto &[k, v] : val)
    {
        current_alignment += serialized_size(k, current_alignment);
        current_alignment += serialized_size(v, current_alignment);
    }
    return current_alignment - initial_alignment;
}

constexpr size_t serialized_size(const DSR::CRDTEdge &val, size_t current_alignment)
{
    size_t initial_alignment = current_alignment;
    current_alignment += serialized_size(val.type(), current_alignment);
    current_alignment += serialized_size(val.from(), current_alignment);
    current_alignment += serialized_size(val.to(), current_alignment);
    current_alignment += serialized_size(val.agent_id(), current_alignment);
    current_alignment += serialized_size(val.attrs(), current_alignment);
    return current_alignment - initial_alignment;
}

constexpr size_t serialized_size(const DSR::CRDTNode &val, size_t current_alignment)
{
    size_t initial_alignment = current_alignment;
    current_alignment += serialized_size(val.type(), current_alignment);
    current_alignment += serialized_size(val.name(), current_alignment);
    current_alignment += serialized_size(val.id(), current_alignment);
    current_alignment += serialized_size(val.agent_id(), current_alignment);
    current_alignment += serialized_size(val.attrs(), current_alignment);
    current_alignment += serialized_size(val.fano(), current_alignment);
    return current_alignment - initial_alignment;
}

constexpr size_t serialized_size(const dot_context &val, size_t current_alignment)
{
    size_t initial_alignment = current_alignment;
    current_alignment += serialized_size(val.cc, current_alignment);
    current_alignment += serialized_size(val.dc, current_alignment);
    return current_alignment - initial_alignment;
}

template <typename P1, size_t n>
constexpr size_t serialized_size(const std::array<P1, n> &val, size_t current_alignment)
{
    size_t initial_alignment = current_alignment;
    // Array don't need encapsulation (at least fastddsgen does it like that)
    // current_alignment += 4 + eprosima::fastcdr::Cdr::alignment(current_alignment, 4);
    for (const auto &v : val)
    {
        current_alignment += serialized_size(v, current_alignment);
    }
    return current_alignment - initial_alignment;
}

template <typename P1>
constexpr size_t serialized_size(const std::vector<P1> &val, size_t current_alignment)
{
    size_t initial_alignment = current_alignment;
    if (val.size() > 0)
    {
        // encapsulation
        current_alignment += 4 + eprosima::fastcdr::Cdr::alignment(current_alignment, 4);
        for (const auto &v : val)
        {
            current_alignment += serialized_size(v, current_alignment);
        }
    }
    return current_alignment - initial_alignment;
}

template <typename P1>
constexpr size_t serialized_size(const std::set<P1> &val, size_t current_alignment)
{
    size_t initial_alignment = current_alignment;
    if (val.size() > 0)
    {
        // encapsulation
        current_alignment += 4 + eprosima::fastcdr::Cdr::alignment(current_alignment, 4);
        for (const auto &v : val)
        {
            current_alignment += serialized_size(v, current_alignment);
        }
    }
    return current_alignment - initial_alignment;
}

template <typename T>
constexpr size_t serialized_size(const dot_kernel<T> &val, size_t current_alignment)
{
    size_t initial_alignment = current_alignment;
    current_alignment += serialized_size(val.ds, current_alignment);
    current_alignment += serialized_size(val.c, current_alignment);
    return current_alignment - initial_alignment;
}

template <typename T>
constexpr size_t serialized_size(const mvreg<T> &val, size_t current_alignment)
{
    size_t initial_alignment = current_alignment;

    // id uint64_t (8)
    current_alignment += serialized_size(val.id, current_alignment);
    // DK
    current_alignment += serialized_size(val.dk, current_alignment);
    return current_alignment - initial_alignment;
}

/*

//MVREG NODE, Operation timestamp, agent_id
typedef std::tuple<mvreg<DSR::CRDTNode>, uint64_t, uint32_t> NodeInfoTuple;
//MVREG EDGE, from, to, Operation timestamp,  agent_id
typedef std::tuple<mvreg<DSR::CRDTEdge>, uint64_t, uint64_t, uint64_t, uint32_t> EdgeInfoTuple;
//VECTOR <MVREG ATTRIBUTE, Operation timestamp>, agent_id
typedef std::tuple<std::vector<std::tuple<mvreg<DSR::CRDTAttribute>, uint64_t>>, uint32_t> NodeAttributeVecTuple;
//VECTOR <MVREG ATTRIBUTE, Operation timestamp, from, to>, agent_id
typedef std::tuple<std::vector<std::tuple<mvreg<DSR::CRDTAttribute>, uint64_t, uint64_t, uint64_t>>, uint32_t>
EdgeAttributeVecTuple;
//agent_id, request_agent_id, map<id, node>, deleted ids VECTOR <uint64_t>, dot_context
typedef std::tuple<uint32_t, uint32_t, std::map<uint64_t, NodeInfoTuple>, std::vector<uint64_t>, dot_context>
GraphInfoTuple;
//agent_name, agent_id
typedef std::tuple<std::string, uint32_t> GraphRequestTuple;
*/

template <typename... T>
void serialize(const std::tuple<T...> &val, eprosima::fastcdr::Cdr &cdr)
{
    ((cdr << std::get<T>(val)), ...);
}

void serialize(const DSR::CRDTAttribute &val, eprosima::fastcdr::Cdr &cdr)
{
    cdr << (uint32_t)val.selected();
    switch (val.selected())
    {
        case 0: cdr << val.str(); break;
        case 1: cdr << val.dec(); break;
        case 2: cdr << val.fl(); break;
        case 3: cdr << val.float_vec(); break;
        case 4: cdr << val.bl(); break;
        case 5: cdr << val.byte_vec(); break;
        case 6: cdr << val.uint(); break;
        case 7: cdr << val.uint64(); break;
        case 8: cdr << val.dob(); break;
        case 9: cdr << val.u64_vec(); break;
        case 10: cdr << val.vec2(); break;
        case 11: cdr << val.vec3(); break;
        case 12: cdr << val.vec4(); break;
        case 13: cdr << val.vec6(); break;
        default: break;
    }
}

void serialize(const DSR::CRDTNode &val, eprosima::fastcdr::Cdr &cdr)
{
    cdr << val.agent_id();
    cdr << val.name();
    cdr << val.type();
    cdr << val.attrs();
    cdr << val.fano();
}

void serialize(const DSR::CRDTEdge &val, eprosima::fastcdr::Cdr &cdr)
{
    cdr << val.agent_id();
    cdr << val.from();
    cdr << val.to();
    cdr << val.type();
    cdr << val.attrs();
}

template <typename... T>
void deserialize(std::tuple<T...> &val, eprosima::fastcdr::Cdr &cdr)
{
    ((cdr >> std::get<T>(val)), ...);
}

void deserialize(DSR::CRDTAttribute &val, eprosima::fastcdr::Cdr &cdr)
{
    // cdr >> (uint32_t)val.selected();
    switch (val.selected())
    {
        case 0: cdr >> val.str(); break;
        case 1:
            int32_t xi;
            cdr >> xi;
            val.dec(xi);
            break;
        case 2:
            float xf;
            cdr >> xf;
            val.fl(xf);
            break;
        case 3: cdr >> val.float_vec(); break;
        case 4:
            bool xb;
            cdr >> xb;
            val.bl(xb);
            break;
        case 5: cdr >> val.byte_vec(); break;
        case 6:
            uint32_t xu;
            cdr >> xu;
            val.uint(xu);
            break;
        case 7:
            uint64_t xu64;
            cdr >> xu64;
            val.uint64(xu64);
            break;
        case 8:
            double xd;
            cdr >> xd;
            val.dob(xd);
            break;
        case 9: cdr >> val.u64_vec(); break;
        case 10: cdr >> val.vec2(); break;
        case 11: cdr >> val.vec3(); break;
        case 12: cdr >> val.vec4(); break;
        case 13: cdr >> val.vec6(); break;
        default: break;
    }
}

void deserialize(DSR::CRDTNode &val, eprosima::fastcdr::Cdr &cdr)
{
    uint32_t x;
    cdr >> x;
    val.agent_id(x);
    cdr >> val.name();
    cdr >> val.type();
    cdr >> val.attrs();
    cdr >> val.fano();
}

void deserialize(DSR::CRDTEdge &val, eprosima::fastcdr::Cdr &cdr)
{
    uint32_t x1;
    uint64_t x2;
    uint64_t x3;
    cdr >> x1;
    val.agent_id(x1);
    cdr >> x2;
    val.from(x2);
    cdr >> x3;
    val.to(x3);
    cdr >> val.type();
    cdr >> val.attrs();
}