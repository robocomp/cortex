// Standalone check of cortex's dsr_proc_metrics.h, under the es_ES locale the agents run in.
// Not part of the Catch2 suite (no graph needed). From the cortex root:
//   g++ -std=c++23 -I api/include tests/proc_metrics_test.cpp -o /tmp/pmt && /tmp/pmt
#include <dsr/api/dsr_proc_metrics.h>

#include <chrono>
#include <clocale>
#include <cmath>
#include <print>
#include <thread>
#include <unistd.h>

int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (not (x)) { ++failures; std::println("FAIL line {}: {}", __LINE__, #x); } } while (0)

int main()
{
    std::setlocale(LC_ALL, "es_ES.UTF-8");
    using namespace DSR::proc;

    // /proc/<pid>/stat: comm may hold spaces and ')' — fields are counted after the LAST ')'. utime=14, stime=15.
    const std::string stat = "4242 (my (weird) agent) S 1 4242 4242 0 -1 4194560 1500 0 0 0 731 269 0 0 20 0 9 0 12345 "
                             "987654321 2000 18446744073709551615 1 1 0 0 0 0 0 4096 0 0 0 0 17 3 0 0 0 0 0";
    CHECK(cpu_ticks_from_stat(stat) == std::optional<std::uint64_t>(1000));
    CHECK(not cpu_ticks_from_stat("garbage").has_value());
    CHECK(not cpu_ticks_from_stat("1 (x) S 1 2").has_value());   // too few fields

    const std::string status = "Name:\tcontroller\nUmask:\t0002\nState:\tS (sleeping)\nVmPeak:\t 2000000 kB\n"
                               "VmRSS:\t  812345 kB\nRssAnon:\t  700000 kB\nThreads:\t42\n";
    CHECK(rss_kb_from_status(status) == std::optional<std::int64_t>(812345));
    CHECK(not rss_kb_from_status("Name:\tx\n").has_value());

    // CPU %: of one core, over the interval; 2 cores busy = 200 %.
    CHECK(std::abs(cpu_percent(150, 1.5, 100) - 100.0f) < 1e-4f);
    CHECK(std::abs(cpu_percent(300, 1.5, 100) - 200.0f) < 1e-4f);
    CHECK(std::abs(cpu_percent(25, 2.0, 100) - 12.5f) < 1e-4f);   // the fraction the old stof dropped under es_ES
    CHECK(cpu_percent(10, 0.0, 100) < 0.0f);                       // no interval: unknown, not infinite

    // live: this process, reading its own /proc, burning ~one core for 0.5 s
    const auto t0 = read_self();
    CHECK(t0.has_value() and t0->rss_kb > 0);
    const auto start = std::chrono::steady_clock::now();
    volatile double x = 0;
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500)) x = x + std::sqrt(x + 1.0);
    const auto t1 = read_self();
    const float pct = cpu_percent(t1->cpu_ticks - t0->cpu_ticks,
                                  std::chrono::duration<double>(t1->when - t0->when).count(), sysconf(_SC_CLK_TCK));
    std::println("live: rss {} kB, cpu {:.1f} % over the busy loop", t1->rss_kb, pct);
    CHECK(pct > 70.0f and pct < 130.0f);

    std::println("proc_metrics_test: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
