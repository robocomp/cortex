#pragma once

#include "dsr/core/types/user_types.h"
#include "dsr/core/utils.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

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

struct EdgeKeyView
{
    uint64_t from{};
    uint64_t to{};
    std::string_view type;
};

struct EdgeKey
{
    uint64_t from{};
    uint64_t to{};
    std::string type;

    auto tie() const { return std::tie(from, to, type); }
};

inline bool operator==(const EdgeKey& lhs, const EdgeKey& rhs)
{
    return lhs.from == rhs.from && lhs.to == rhs.to && lhs.type == rhs.type;
}

inline bool operator==(const EdgeKey& lhs, EdgeKeyView rhs)
{
    return lhs.from == rhs.from && lhs.to == rhs.to && lhs.type == rhs.type;
}

inline bool operator==(EdgeKeyView lhs, const EdgeKey& rhs)
{
    return rhs == lhs;
}

inline bool operator<(const EdgeKey& lhs, const EdgeKey& rhs)
{
    return lhs.tie() < rhs.tie();
}

struct EdgeKeyHash
{
    using is_transparent = void;

    size_t operator()(const EdgeKey& key) const noexcept
    {
        return (*this)(EdgeKeyView{key.from, key.to, key.type});
    }

    size_t operator()(EdgeKeyView key) const noexcept
    {
        size_t seed = std::hash<uint64_t>{}(key.from);
        seed ^= std::hash<uint64_t>{}(key.to) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        seed ^= std::hash<std::string_view>{}(key.type) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct EdgeKeyEqual
{
    using is_transparent = void;

    bool operator()(const EdgeKey& lhs, const EdgeKey& rhs) const noexcept { return lhs == rhs; }
    bool operator()(const EdgeKey& lhs, EdgeKeyView rhs) const noexcept { return lhs == rhs; }
    bool operator()(EdgeKeyView lhs, const EdgeKey& rhs) const noexcept { return rhs == lhs; }
};

inline bool is_newer(const Version& lhs, const Version& rhs)
{
    return lhs.tie() > rhs.tie();
}

inline Version version_of(uint64_t timestamp, uint32_t agent_id)
{
    return Version{timestamp, agent_id};
}

inline EdgeKey edge_key(uint64_t from, uint64_t to, std::string type)
{
    return EdgeKey{from, to, std::move(type)};
}

inline EdgeKeyView edge_key_view(uint64_t from, uint64_t to, std::string_view type)
{
    return EdgeKeyView{from, to, type};
}

} // namespace DSR::LWW
