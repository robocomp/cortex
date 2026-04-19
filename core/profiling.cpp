#include "dsr/core/profiling.h"

#if defined(CORTEX_PROFILING_BACKEND_PERFETTO)

#include <perfetto.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <unistd.h>


PERFETTO_TRACK_EVENT_STATIC_STORAGE_IN_NAMESPACE(DSR::profiling);

namespace DSR::profiling {
namespace {

std::once_flag g_init_once;
std::once_flag g_shutdown_once;
std::unique_ptr<perfetto::TracingSession> g_session;

std::string default_trace_path()
{
    if (const char* env = std::getenv("CORTEX_PERFETTO_TRACE_FILE"); env != nullptr && env[0] != '\0')
        return env;

    namespace fs = std::filesystem;
    fs::path out_dir = fs::current_path() / ".artifacts" / "perfetto";
    std::error_code ec;
    fs::create_directories(out_dir, ec);

    std::string exe_name = "cortex";
#if defined(__linux__)
    std::error_code symlink_ec;
    const auto exe = fs::read_symlink("/proc/self/exe", symlink_ec);
    if (!symlink_ec && exe.has_filename())
        exe_name = exe.filename().string();
#endif

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

#if defined(__linux__)
    const auto pid = static_cast<long long>(::getpid());
#else
    const auto pid = 0LL;
#endif

    return (out_dir / (exe_name + "-" + std::to_string(pid) + "-" + std::to_string(ts_ms) + ".pftrace")).string();
}

void start_session()
{
    perfetto::TracingInitArgs args;
    args.backends |= perfetto::kInProcessBackend;
    perfetto::Tracing::Initialize(args);
    DSR::profiling::TrackEvent::Register();

    perfetto::TraceConfig cfg;
    cfg.add_buffers()->set_size_kb(16 * 1024);
    auto* ds_cfg = cfg.add_data_sources()->mutable_config();
    ds_cfg->set_name("track_event");

    perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_enabled_categories("*");
    ds_cfg->set_track_event_config_raw(te_cfg.SerializeAsString());

    g_session = perfetto::Tracing::NewTrace();
    g_session->Setup(cfg);
    g_session->StartBlocking();

    std::atexit(&shutdown);
}

} // namespace

void ensure_started()
{
    std::call_once(g_init_once, &start_session);
}

void shutdown()
{
    std::call_once(g_shutdown_once, [] {
        if (!g_session)
            return;

        g_session->StopBlocking();
        const auto trace_data = g_session->ReadTraceBlocking();

        std::ofstream output(default_trace_path(), std::ios::out | std::ios::binary);
        if (output.is_open()) {
            output.write(trace_data.data(), static_cast<std::streamsize>(trace_data.size()));
            output.close();
        }

        g_session.reset();
    });
}

CallstackFrames capture_callstack(int depth) noexcept
{
    CallstackFrames cs{};
    int capped = depth < CallstackFrames::kMax ? depth : CallstackFrames::kMax;
    // +2 to skip backtrace() itself and capture_callstack()
    cs.count = ::backtrace(cs.frames, capped + 2);
    return cs;
}

void emit_callstack(perfetto::EventContext& ctx, const CallstackFrames& cs) noexcept
{
    // skip frame 0 (backtrace) and frame 1 (capture_callstack)
    constexpr int kSkip = 2;
    std::string stack;
    stack.reserve(cs.count * 64);

    for (int i = kSkip; i < cs.count; ++i) {
        Dl_info info{};
        if (::dladdr(cs.frames[i], &info) && info.dli_sname) {
            int status = -1;
            char* dem = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
            stack += (status == 0 && dem) ? dem : info.dli_sname;
            ::free(dem);
        } else {
            char buf[20];
            ::snprintf(buf, sizeof(buf), "%p", cs.frames[i]);
            stack += buf;
        }
        stack += '\n';
    }

    auto* ann = ctx.event()->add_debug_annotations();
    ann->set_name("callstack");
    ann->set_string_value(stack);
}

} // namespace DSR::profiling

#else

namespace DSR::profiling {
void ensure_started() {}
void shutdown() {}
}

#endif
