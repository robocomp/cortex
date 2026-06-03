#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "dsr/core/profiling.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

// ── shared state set up in main() before any test runs ───────────────────────

static fs::path g_trace_path;
static std::string g_trace_bytes;

// A noinline call chain so the callstack has at least two recognisable frames.
[[noinline]] static void profiling_leaf()
{
    CORTEX_PROFILE_ZONE_CS("perfetto_test_leaf");
    volatile int x = 0;
    for (int i = 0; i < 10'000; ++i) x += i;
}

[[noinline]] static void profiling_caller()
{
    CORTEX_PROFILE_ZONE_N("perfetto_test_caller");
    profiling_leaf();
}

// ── tests ─────────────────────────────────────────────────────────────────────

TEST_CASE("trace file is written and non-empty", "[perfetto]")
{
    REQUIRE(fs::exists(g_trace_path));
    REQUIRE(fs::file_size(g_trace_path) > 256);  // sanity: more than a header
}

#if defined(CORTEX_PERFETTO_CALLSTACK_STACKFRAME)

TEST_CASE("STACKFRAME: trace contains interned function name strings", "[perfetto][stackframe]")
{
    // The STACKFRAME path interns demangled symbol names as InternedStrings.
    // They are stored as raw UTF-8 bytes inside the proto binary, so a plain
    // substring search is sufficient.
    CHECK(g_trace_bytes.find("perfetto_test_leaf") != std::string::npos);
    CHECK(g_trace_bytes.find("perfetto_test_caller") != std::string::npos);
}

TEST_CASE("STACKFRAME: trace contains callstack_iid field tag", "[perfetto][stackframe]")
{
    // TrackEvent.callstack_iid = field 36, wire type 0 (varint).
    // Tag = (36 << 3) | 0 = 288 = 0x0120, encoded as two varint bytes: 0xa0 0x02.
    const std::string tag("\xa0\x02", 2);
    CHECK(g_trace_bytes.find(tag) != std::string::npos);
}

#endif // CORTEX_PERFETTO_CALLSTACK_STACKFRAME

#if defined(CORTEX_PERFETTO_CALLSTACK_LINUX_PERF)

TEST_CASE("LINUX_PERF: trace is large enough to contain perf samples", "[perfetto][linux_perf]")
{
    // Perf samples are produced by traced_probes independently of track events.
    // We can only confirm the file is non-trivially large; detailed content
    // verification would require the Perfetto trace processor.
    REQUIRE(fs::file_size(g_trace_path) > 1024);
}

#endif // CORTEX_PERFETTO_CALLSTACK_LINUX_PERF

// ── custom main: run workload, flush, then hand off to Catch2 ────────────────

int main(int argc, char* argv[])
{
    g_trace_path = fs::temp_directory_path() / "cortex_perfetto_test.pftrace";
    fs::remove(g_trace_path);
    ::setenv("CORTEX_PERFETTO_TRACE_FILE", g_trace_path.string().c_str(), /*overwrite=*/1);

    // Emit events from multiple threads so the flush path exercises all writers.
    {
        std::thread t1(profiling_caller);
        std::thread t2(profiling_caller);
        t1.join();
        t2.join();
        profiling_caller();  // main thread too
    }

    // Flush thread-local buffers and write the file.
    DSR::profiling::shutdown();

    // Load trace bytes once; individual tests search within them.
    if (fs::exists(g_trace_path)) {
        std::ifstream f(g_trace_path, std::ios::binary);
        g_trace_bytes.assign(std::istreambuf_iterator<char>(f), {});
    }

    return Catch::Session().run(argc, argv);
}
