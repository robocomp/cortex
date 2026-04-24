#ifndef DSR_CORE_PROFILING_H
#define DSR_CORE_PROFILING_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <memory>

#ifndef CORTEX_CALLSTACK_DEPTH
#define CORTEX_CALLSTACK_DEPTH 0
#endif

namespace DSR::profiling {

enum class DetailLevel : int {
    Off = 0,
    Min = 1,
    Default = 2,
    Detail = 3,
    Hot = 4,
};

void ensure_started();
void shutdown();
void configure_detail_level_from_env() noexcept;
void set_detail_level(DetailLevel level) noexcept;
DetailLevel get_detail_level() noexcept;
bool detail_enabled(DetailLevel level) noexcept;
bool try_parse_detail_level(const char* value, DetailLevel& out) noexcept;
const char* detail_level_name(DetailLevel level) noexcept;

} // namespace DSR::profiling

#if defined(CORTEX_PROFILING_BACKEND_TRACY)

#include <tracy/Tracy.hpp>

namespace DSR::profiling {

class ScopedZone {
public:
    template <size_t N>
    ScopedZone(DetailLevel level,
               uint32_t line,
               const char* source,
               const char* function,
               const char (&name)[N],
               int callstack_depth = 0)
    {
        if (!detail_enabled(level))
            return;

        const auto source_sz = std::strlen(source);
        const auto function_sz = std::strlen(function);
        const auto name_sz = std::strlen(name);

        if (callstack_depth > 0) {
            zone_ = std::make_unique<tracy::ScopedZone>(
                line, source, source_sz, function, function_sz, name, name_sz, callstack_depth, true);
        } else {
            zone_ = std::make_unique<tracy::ScopedZone>(
                line, source, source_sz, function, function_sz, name, name_sz, true);
        }
    }

private:
    std::unique_ptr<tracy::ScopedZone> zone_;
};

} // namespace DSR::profiling

#define CORTEX_PROFILE_FRAME()                   FrameMark
#define CORTEX_PROFILE_TEXT(text_ptr, text_size) ZoneText(text_ptr, text_size)
#define CORTEX_PROFILE_VALUE(value)              ZoneValue(value)
#define CORTEX_PROFILE_THREAD_NAME(name)         tracy::SetThreadName(name)

#elif defined(CORTEX_PROFILING_BACKEND_PERFETTO)

#include <perfetto.h>
#include <pthread.h>

PERFETTO_DEFINE_CATEGORIES_IN_NAMESPACE(
    DSR::profiling,
    perfetto::Category("cortex").SetDescription("Cortex runtime events"));
PERFETTO_USE_CATEGORIES_FROM_NAMESPACE(DSR::profiling);

namespace DSR::profiling {

#if defined(CORTEX_PERFETTO_CALLSTACK_STACKFRAME)
struct CallstackFrames {
    static constexpr int kMax = 64;
    void* frames[kMax];
    int count;
};
CallstackFrames capture_callstack(int depth) noexcept;
void emit_callstack_stackframe(perfetto::EventContext& ctx, const CallstackFrames& cs) noexcept;
#endif

class ScopedZone {
public:
    template <size_t N>
    ScopedZone(DetailLevel level,
               uint32_t,
               const char*,
               const char*,
               const char (&name)[N],
               int callstack_depth = 0)
        : active_(detail_enabled(level))
    {
        if (!active_)
            return;

        ensure_started();

#if defined(CORTEX_PERFETTO_CALLSTACK_STACKFRAME)
        if (callstack_depth > 0) {
            auto cs = capture_callstack(callstack_depth);
            TRACE_EVENT_BEGIN("cortex", perfetto::StaticString{name}, [cs](perfetto::EventContext ctx) {
                emit_callstack_stackframe(ctx, cs);
            });
            return;
        }
#else
        static_cast<void>(callstack_depth);
#endif
        TRACE_EVENT_BEGIN("cortex", perfetto::StaticString{name});
    }

    ~ScopedZone()
    {
        if (active_)
            TRACE_EVENT_END("cortex");
    }

private:
    bool active_ = false;
};

} // namespace DSR::profiling

#define CORTEX_PROFILE_FRAME() ((void)0)
#define CORTEX_PROFILE_TEXT(text_ptr, text_size) ((void)0)
#define CORTEX_PROFILE_VALUE(value) ((void)0)
#define CORTEX_PROFILE_THREAD_NAME(name) \
    do { char _tn[16]; std::strncpy(_tn, (name), 15); _tn[15] = '\0'; pthread_setname_np(pthread_self(), _tn); } while(0)

#else

namespace DSR::profiling {

class ScopedZone {
public:
    template <size_t N>
    ScopedZone(DetailLevel, uint32_t, const char*, const char*, const char (&)[N], int = 0) {}
};

} // namespace DSR::profiling

#define CORTEX_PROFILE_FRAME() ((void)0)
#define CORTEX_PROFILE_TEXT(text_ptr, text_size) ((void)0)
#define CORTEX_PROFILE_VALUE(value) ((void)0)
#define CORTEX_PROFILE_THREAD_NAME(name) ((void)0)

#endif

#define CORTEX_PROFILE_CONCAT_IMPL(a, b) a##b
#define CORTEX_PROFILE_CONCAT(a, b) CORTEX_PROFILE_CONCAT_IMPL(a, b)

#define CORTEX_PROFILE_ZONE_L(level) \
    ::DSR::profiling::ScopedZone CORTEX_PROFILE_CONCAT(_cortex_zone_, __LINE__)( \
        (level), __LINE__, __FILE__, __func__, __func__)

#define CORTEX_PROFILE_ZONE_N_L(level, name_literal) \
    ::DSR::profiling::ScopedZone CORTEX_PROFILE_CONCAT(_cortex_zone_, __LINE__)( \
        (level), __LINE__, __FILE__, __func__, (name_literal))

#define CORTEX_PROFILE_ZONE_CS_L(level, name_literal) \
    ::DSR::profiling::ScopedZone CORTEX_PROFILE_CONCAT(_cortex_zone_, __LINE__)( \
        (level), __LINE__, __FILE__, __func__, (name_literal), CORTEX_CALLSTACK_DEPTH)

#define CORTEX_PROFILE_MIN() CORTEX_PROFILE_ZONE_L(::DSR::profiling::DetailLevel::Min)
#define CORTEX_PROFILE_MIN_N(name_literal) CORTEX_PROFILE_ZONE_N_L(::DSR::profiling::DetailLevel::Min, name_literal)
#define CORTEX_PROFILE_MIN_CS(name_literal) CORTEX_PROFILE_ZONE_CS_L(::DSR::profiling::DetailLevel::Min, name_literal)

#define CORTEX_PROFILE_DETAIL_N(name_literal) CORTEX_PROFILE_ZONE_N_L(::DSR::profiling::DetailLevel::Detail, name_literal)
#define CORTEX_PROFILE_DETAIL_CS(name_literal) CORTEX_PROFILE_ZONE_CS_L(::DSR::profiling::DetailLevel::Detail, name_literal)

#define CORTEX_PROFILE_HOT_N(name_literal) CORTEX_PROFILE_ZONE_N_L(::DSR::profiling::DetailLevel::Hot, name_literal)
#define CORTEX_PROFILE_HOT_CS(name_literal) CORTEX_PROFILE_ZONE_CS_L(::DSR::profiling::DetailLevel::Hot, name_literal)

#define CORTEX_PROFILE_ZONE() CORTEX_PROFILE_ZONE_L(::DSR::profiling::DetailLevel::Default)
#define CORTEX_PROFILE_ZONE_N(name_literal) CORTEX_PROFILE_ZONE_N_L(::DSR::profiling::DetailLevel::Default, name_literal)
#define CORTEX_PROFILE_ZONE_CS(name_literal) CORTEX_PROFILE_ZONE_CS_L(::DSR::profiling::DetailLevel::Default, name_literal)

#endif
