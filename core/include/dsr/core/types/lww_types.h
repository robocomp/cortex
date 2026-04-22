#pragma once

#include "dsr/core/types/user_types.h"
#include "dsr/core/utils.h"

#include <cstdint>
#include <map>
#include <string>
#include <tuple>

namespace DSR::LWW {

struct Version
{
    uint64_t timestamp{};
    uint32_t agent_id{};

    auto tie() const { return std::tie(timestamp, agent_id); }
};

struct Tombstone
{
    Version version;
    uint64_t expires_at_ms{};
};

struct AttrState
{
    Attribute value;
    Version version;
};

struct NodeState
{
    uint64_t id{};
    std::string type;
    std::string name;
    uint32_t agent_id{};
    Version version;
    std::map<std::string, AttrState> attrs;
};

struct EdgeState
{
    uint64_t from{};
    uint64_t to{};
    std::string type;
    uint32_t agent_id{};
    Version version;
    std::map<std::string, AttrState> attrs;
};

using EdgeKey = std::tuple<uint64_t, uint64_t, std::string>;

inline bool is_newer(const Version& lhs, const Version& rhs)
{
    return lhs.tie() > rhs.tie();
}

inline Version version_of(uint64_t timestamp, uint32_t agent_id)
{
    return Version{timestamp, agent_id};
}

inline EdgeKey edge_key(uint64_t from, uint64_t to, const std::string& type)
{
    return EdgeKey{from, to, type};
}

} // namespace DSR::LWW
