#ifndef DSR_CORE_PROFILING_H
#define DSR_CORE_PROFILING_H

namespace DSR::profiling {
void ensure_started();
void shutdown();
}

#if defined(CORTEX_PROFILING_BACKEND_TRACY)

#include <tracy/Tracy.hpp>

#define CORTEX_PROFILE_ZONE()                    ZoneScoped
#define CORTEX_PROFILE_ZONE_N(name_literal)      ZoneScopedN(name_literal)
#define CORTEX_PROFILE_ZONE_CS(name_literal)     ZoneScopedNS(name_literal, CORTEX_CALLSTACK_DEPTH)
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
struct CallstackFrames {
    static constexpr int kMax = 64;
    void* frames[kMax];
    int count;
};
CallstackFrames capture_callstack(int depth) noexcept;
void emit_callstack(perfetto::EventContext& ctx, const CallstackFrames& cs) noexcept;
} // namespace DSR::profiling

#define CORTEX_PROFILE_ZONE() \
    ::DSR::profiling::ensure_started(); \
    TRACE_EVENT("cortex", perfetto::StaticString{__func__})

#define CORTEX_PROFILE_ZONE_N(name_literal) \
    ::DSR::profiling::ensure_started(); \
    TRACE_EVENT("cortex", name_literal)

#define CORTEX_PROFILE_ZONE_CS(name_literal) \
    ::DSR::profiling::ensure_started(); \
    auto _cortex_cs = ::DSR::profiling::capture_callstack(CORTEX_CALLSTACK_DEPTH); \
    TRACE_EVENT("cortex", name_literal, [&_cortex_cs](perfetto::EventContext ctx) { \
        ::DSR::profiling::emit_callstack(ctx, _cortex_cs); \
    })

#define CORTEX_PROFILE_FRAME() ((void)0)
#define CORTEX_PROFILE_TEXT(text_ptr, text_size) ((void)0)
#define CORTEX_PROFILE_VALUE(value) ((void)0)
#define CORTEX_PROFILE_THREAD_NAME(name) \
    do { char _tn[16]; strncpy(_tn, (name), 15); _tn[15] = '\0'; pthread_setname_np(pthread_self(), _tn); } while(0)

#else

#define CORTEX_PROFILE_ZONE() ((void)0)
#define CORTEX_PROFILE_ZONE_N(name_literal) ((void)0)
#define CORTEX_PROFILE_ZONE_CS(name_literal) ((void)0)
#define CORTEX_PROFILE_FRAME() ((void)0)
#define CORTEX_PROFILE_TEXT(text_ptr, text_size) ((void)0)
#define CORTEX_PROFILE_VALUE(value) ((void)0)
#define CORTEX_PROFILE_THREAD_NAME(name) ((void)0)

#endif

#endif
