#pragma once

#include "dsr/core/types/lww_types.h"

namespace DSR::LWW {

inline Tombstone make_tombstone(const Version& version, uint64_t now, uint64_t tombstone_window_ms)
{
    return Tombstone{version, now + tombstone_window_ms};
}

template <typename TombstoneMap, typename StateMap, typename Key>
bool delta_is_stale(const TombstoneMap& tombstones, const StateMap& states, const Key& key, const Version& version)
{
    if (auto it = tombstones.find(key); it != tombstones.end() && !is_newer(version, it->second.version)) {
        return true;
    }
    if (auto it = states.find(key); it != states.end() && !is_newer(version, it->second.version)) {
        return true;
    }
    return false;
}

} // namespace DSR::LWW
