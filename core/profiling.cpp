#include "dsr/core/profiling.h"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace DSR::profiling {
namespace {

std::atomic<int> g_detail_level{static_cast<int>(DetailLevel::Default)};
std::atomic<bool> g_detail_level_explicit{false};
std::once_flag g_detail_init_once;

bool try_parse_detail_level_impl(const char* value, DetailLevel& out) noexcept
{
    if (value == nullptr || value[0] == '\0')
        return false;

    char normalized[16];
    size_t i = 0;
    for (; value[i] != '\0' && i + 1 < sizeof(normalized); ++i)
        normalized[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
    normalized[i] = '\0';

    if (std::strcmp(normalized, "off") == 0 || std::strcmp(normalized, "0") == 0) {
        out = DetailLevel::Off;
    } else if (std::strcmp(normalized, "min") == 0 || std::strcmp(normalized, "1") == 0) {
        out = DetailLevel::Min;
    } else if (std::strcmp(normalized, "default") == 0 || std::strcmp(normalized, "2") == 0) {
        out = DetailLevel::Default;
    } else if (std::strcmp(normalized, "detail") == 0 || std::strcmp(normalized, "3") == 0) {
        out = DetailLevel::Detail;
    } else if (std::strcmp(normalized, "hot") == 0 || std::strcmp(normalized, "4") == 0) {
        out = DetailLevel::Hot;
    } else {
        return false;
    }

    return true;
}

} // namespace

void configure_detail_level_from_env() noexcept
{
    std::call_once(g_detail_init_once, [] {
        if (g_detail_level_explicit.load(std::memory_order_relaxed))
            return;

        DetailLevel parsed{};
        if (try_parse_detail_level_impl(std::getenv("CORTEX_PROFILE_DETAIL"), parsed) ||
            try_parse_detail_level_impl(std::getenv("BENCH_PROFILE_DETAIL"), parsed)) {
            set_detail_level(parsed);
        }
    });
}

void set_detail_level(DetailLevel level) noexcept
{
    g_detail_level_explicit.store(true, std::memory_order_relaxed);
    g_detail_level.store(static_cast<int>(level), std::memory_order_relaxed);
}

DetailLevel get_detail_level() noexcept
{
    return static_cast<DetailLevel>(g_detail_level.load(std::memory_order_relaxed));
}

bool detail_enabled(DetailLevel level) noexcept
{
    configure_detail_level_from_env();
    return static_cast<int>(level) <= g_detail_level.load(std::memory_order_relaxed);
}

bool try_parse_detail_level(const char* value, DetailLevel& out) noexcept
{
    return try_parse_detail_level_impl(value, out);
}

const char* detail_level_name(DetailLevel level) noexcept
{
    switch (level) {
        case DetailLevel::Off: return "off";
        case DetailLevel::Min: return "min";
        case DetailLevel::Default:
        case DetailLevel::Detail: return "detail";
        case DetailLevel::Hot: return "hot";
    }
    return "default";
}

} // namespace DSR::profiling

#if defined(CORTEX_PROFILING_BACKEND_PERFETTO)

#include <perfetto.h>

#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include <unistd.h>

// Included here (before any namespace) so that dladdr/backtrace are declared
// in the global namespace and accessible via :: inside DSR::profiling.
#if defined(CORTEX_PERFETTO_CALLSTACK_STACKFRAME)
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#endif

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
    const auto pid = static_cast<long long>(::getpid());

    return (out_dir / (exe_name + "-" + std::to_string(pid) + "-" + std::to_string(ts_ms) + ".pftrace")).string();
}

void start_session()
{
    perfetto::TracingInitArgs args;
#if defined(CORTEX_PERFETTO_CALLSTACK_LINUX_PERF)
    args.backends |= perfetto::kSystemBackend;
#else
    args.backends |= perfetto::kInProcessBackend;
#endif
    perfetto::Tracing::Initialize(args);
    DSR::profiling::TrackEvent::Register();

#if defined(CORTEX_PERFETTO_CALLSTACK_LINUX_PERF)
    // Wait for traced_perf to connect to traced and register linux.perf.
    // traced_perf is a separate producer from traced_probes — both must be
    // running before this point.  If linux.perf isn't registered when we send
    // the config, traced silently skips that data source.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif

    perfetto::TraceConfig cfg;
    auto* track_buffer = cfg.add_buffers();
    track_buffer->set_size_kb(64 * 1024);
    track_buffer->set_fill_policy(perfetto::TraceConfig::BufferConfig::DISCARD);
#if defined(CORTEX_PERFETTO_CALLSTACK_LINUX_PERF)
    auto* perf_buffer = cfg.add_buffers();
    perf_buffer->set_size_kb(64 * 1024);
#endif
    cfg.set_flush_period_ms(1000);
    cfg.mutable_incremental_state_config()->set_clear_period_ms(1000);

    {
        auto* ds_cfg = cfg.add_data_sources()->mutable_config();
        ds_cfg->set_name("track_event");
        ds_cfg->set_target_buffer(0);
        perfetto::protos::gen::TrackEventConfig te_cfg;
        te_cfg.add_enabled_categories("*");
        // Keep packets more self-contained on the system backend. This reduces
        // the impact of isolated incremental-state loss, which is more common
        // under WSL than on native Linux.
        te_cfg.set_disable_incremental_timestamps(true);
        ds_cfg->set_track_event_config_raw(te_cfg.SerializeAsString());
    }

#if defined(CORTEX_PERFETTO_CALLSTACK_LINUX_PERF)
    {
        auto* ds_cfg = cfg.add_data_sources()->mutable_config();
        ds_cfg->set_name("linux.perf");
        ds_cfg->set_target_buffer(1);
        perfetto::protos::gen::PerfEventConfig perf_cfg;
        perf_cfg.mutable_timebase()->set_frequency(1000);
        // Per-process scope: works without root when perf_event_paranoid <= 1.
        // Per-CPU scope (no target_pid) requires paranoid <= 0 or CAP_PERFMON.
        auto* cs = perf_cfg.mutable_callstack_sampling();
        cs->mutable_scope()->add_target_pid(static_cast<int32_t>(::getpid()));
        cs->set_kernel_frames(false);
        ds_cfg->set_perf_event_config_raw(perf_cfg.SerializeAsString());
    }
    g_session = perfetto::Tracing::NewTrace(perfetto::kSystemBackend);
#else
    g_session = perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
#endif
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

        DSR::profiling::TrackEvent::Flush();
        g_session->FlushBlocking(3000);
        g_session->StopBlocking();
        const auto trace_data = g_session->ReadTraceBlocking();

        if (trace_data.empty()) {
            // System backend not reachable (e.g. traced not running or socket
            // permission mismatch). Avoid creating a misleading 0-byte file.
            fprintf(stderr, "[cortex profiling] WARNING: trace data is empty — "
                    "check that traced/traced_probes are running as the same user\n");
        } else {
            std::ofstream output(default_trace_path(), std::ios::out | std::ios::binary);
            if (output.is_open())
                output.write(trace_data.data(), static_cast<std::streamsize>(trace_data.size()));
        }

        g_session.reset();
    });
}

// ── StackFrame / InternedData callstack capture ───────────────────────────────
#if defined(CORTEX_PERFETTO_CALLSTACK_STACKFRAME)

#include <unordered_map>

// Three-level interning: FunctionName → Frame → Callstack.
//
// Perfetto's TrackEventInternedDataIndex::Add receives only (InternedData*, iid,
// key), not the EventContext, so we cannot call inner Get() from inside Add().
// We work around this with two thread-local scratch maps that carry the
// already-computed inner IIDs across the interning boundary:
//
//   tl_frame_fn_iid[pc]           set before InternedFrame::Get()
//   tl_callstack_frame_iids[key]  set before InternedCallstack::Get()

namespace {

thread_local std::unordered_map<uint64_t, uint64_t> tl_frame_fn_iid;
thread_local std::unordered_map<std::string, std::vector<uint64_t>> tl_cs_frame_iids;

static std::string resolve_pc(void* pc) noexcept
{
    Dl_info info{};
    if (::dladdr(pc, &info)) {
        if (info.dli_sname) {
            int status = -1;
            char* dem = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
            std::string name = (status == 0 && dem) ? dem : info.dli_sname;
            ::free(dem);
            return name;
        }
        // No exported symbol (lambda, inline, template, etc.) — show library+offset
        // so the frame can be resolved offline with:
        //   addr2line -e <dli_fname> -f -C <offset>
        if (info.dli_fname) {
            const char* slash = ::strrchr(info.dli_fname, '/');
            const char* base  = slash ? slash + 1 : info.dli_fname;
            uintptr_t offset  = reinterpret_cast<uintptr_t>(pc)
                              - reinterpret_cast<uintptr_t>(info.dli_fbase);
            char buf[256];
            ::snprintf(buf, sizeof(buf), "%s+0x%lx", base, static_cast<unsigned long>(offset));
            return buf;
        }
    }
    char buf[20];
    ::snprintf(buf, sizeof(buf), "%p", pc);
    return buf;
}

// Intern a function-name string.
struct InternedFnName
    : perfetto::TrackEventInternedDataIndex<
          InternedFnName,
          perfetto::protos::pbzero::InternedData::kFunctionNamesFieldNumber,
          std::string> {
    static void Add(perfetto::protos::pbzero::InternedData* data,
                    size_t iid, const std::string& name) {
        auto* fn = data->add_function_names();
        fn->set_iid(iid);
        fn->set_str(reinterpret_cast<const uint8_t*>(name.data()), name.size());
    }
};

// Intern a frame keyed by PC address.
// tl_frame_fn_iid[pc] must be set before calling Get().
struct InternedFrame
    : perfetto::TrackEventInternedDataIndex<
          InternedFrame,
          perfetto::protos::pbzero::InternedData::kFramesFieldNumber,
          uint64_t> {
    static void Add(perfetto::protos::pbzero::InternedData* data,
                    size_t iid, uint64_t pc) {
        auto* frame = data->add_frames();
        frame->set_iid(iid);
        auto it = tl_frame_fn_iid.find(pc);
        if (it != tl_frame_fn_iid.end())
            frame->set_function_name_id(it->second);
    }
};

// Intern a callstack keyed by a binary encoding of its frame IIDs.
// tl_cs_frame_iids[key] must be set before calling Get().
struct InternedCallstack
    : perfetto::TrackEventInternedDataIndex<
          InternedCallstack,
          perfetto::protos::pbzero::InternedData::kCallstacksFieldNumber,
          std::string> {
    static void Add(perfetto::protos::pbzero::InternedData* data,
                    size_t iid, const std::string& key) {
        auto* cs = data->add_callstacks();
        cs->set_iid(iid);
        auto it = tl_cs_frame_iids.find(key);
        if (it != tl_cs_frame_iids.end())
            for (uint64_t fid : it->second)
                cs->add_frame_ids(fid);
    }
};

// Encode a vector of uint64 frame IIDs as a raw byte string for use as a map key.
static std::string encode_frame_iids(const std::vector<uint64_t>& ids)
{
    std::string key(ids.size() * sizeof(uint64_t), '\0');
    std::memcpy(key.data(), ids.data(), key.size());
    return key;
}

} // namespace

CallstackFrames capture_callstack(int depth) noexcept
{
    CallstackFrames cs{};
    int capped = depth < CallstackFrames::kMax ? depth : CallstackFrames::kMax;
    // +2 to skip backtrace() itself and capture_callstack()
    cs.count = ::backtrace(cs.frames, capped + 2);
    return cs;
}

void emit_callstack_stackframe(perfetto::EventContext& ctx,
                               const CallstackFrames& cs) noexcept
{
    constexpr int kSkip = 2;  // skip backtrace() + capture_callstack()

    // Phase 1: intern function names; populate tl_frame_fn_iid for phase 2.
    std::vector<uint64_t> pcs;
    pcs.reserve(cs.count - kSkip);
    for (int i = kSkip; i < cs.count; ++i) {
        uint64_t pc = reinterpret_cast<uint64_t>(cs.frames[i]);
        pcs.push_back(pc);
        std::string name = resolve_pc(cs.frames[i]);
        uint64_t fn_iid = InternedFnName::Get(&ctx, name);
        tl_frame_fn_iid[pc] = fn_iid;
    }

    // Phase 2: intern frames; populate tl_cs_frame_iids for phase 3.
    std::vector<uint64_t> frame_iids;
    frame_iids.reserve(pcs.size());
    for (uint64_t pc : pcs)
        frame_iids.push_back(InternedFrame::Get(&ctx, pc));

    // Phase 3: intern the callstack and attach it to the event.
    std::string cs_key = encode_frame_iids(frame_iids);
    tl_cs_frame_iids[cs_key] = frame_iids;
    uint64_t cs_iid = InternedCallstack::Get(&ctx, cs_key);
    ctx.event()->set_callstack_iid(cs_iid);

    // Cleanup scratch state.
    for (uint64_t pc : pcs)
        tl_frame_fn_iid.erase(pc);
    tl_cs_frame_iids.erase(cs_key);
}

#endif // CORTEX_PERFETTO_CALLSTACK_STACKFRAME

} // namespace DSR::profiling

#else

namespace DSR::profiling {
void ensure_started() {}
void shutdown() {}
}

#endif // CORTEX_PROFILING_BACKEND_PERFETTO
