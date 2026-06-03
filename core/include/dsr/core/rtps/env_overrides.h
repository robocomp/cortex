#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>

namespace DSR::RTPS::Env {

struct UInt32Override
{
    const char* raw {nullptr};
    std::optional<uint32_t> value;

    [[nodiscard]] bool present() const noexcept
    {
        return raw != nullptr && raw[0] != '\0';
    }
};

inline UInt32Override read_u32(
        const char* name)
{
    UInt32Override result;
    result.raw = std::getenv(name);
    if (!result.present())
    {
        return result;
    }

    errno = 0;
    char* end = nullptr;
    const auto parsed = std::strtoull(result.raw, &end, 10);
    if (errno == 0 && end != result.raw && *end == '\0' &&
            parsed <= std::numeric_limits<uint32_t>::max())
    {
        result.value = static_cast<uint32_t>(parsed);
    }

    return result;
}

} // namespace DSR::RTPS::Env
