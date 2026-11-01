// Equivalence + micro-benchmark for the id-based InnerEigenAPI::get_transformation_matrix walk.
//
// Compares the new implementation against a verbatim copy of the OLD algorithm (full Node copies per
// hop) on a root -> room -> {robot -> {lidar, zed}, N objects} tree plus a second branch, for ts==0,
// ts!=0 (Interpolated / Extrapolated / Nearest), and the failure cases (missing node, missing edge,
// missing level, invalid edge type). Then times both on a room with 200 objects.
//
// Build (from cortex/, after `make` in build/ so libdsr_api.so is fresh; uses IN-TREE headers+libs):
//   g++ -std=c++23 -O3 -g -fPIC tests/standalone/inner_eigen_walk_equiv.cpp -o build/inner_eigen_walk_equiv \
//     -DFASTCDR_DYN_LINK -DFASTDDS_DYN_LINK -DQT_CORE_LIB -DQT_NO_DEBUG \
//     -Iapi/include -Icore/include -I../qmat/include -Itests -I/usr/include/eigen3 \
//     -I/usr/include/x86_64-linux-gnu/qt6 -I/usr/include/x86_64-linux-gnu/qt6/QtCore \
//     -isystem /usr/local/include/foonathan_memory \
//     -Lbuild/api -Lbuild/core -ldsr_api -ldsr_core -lQt6Core -lfastdds -lfastcdr -Wl,-rpath,$PWD/build/api:$PWD/build/core
// Run: build/inner_eigen_walk_equiv
#include <dsr/api/dsr_api.h>
#include <dsr/api/dsr_inner_eigen_api.h>
#include <QtCore/QCoreApplication>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "utils.h"

using namespace DSR;
using TQ = RT_API::TimeQuery;

// ---- verbatim OLD walk (Node copies per hop), no cache, warnings dropped ----
static std::optional<Mat::RTMat> old_tm(DSRGraph &G, RT_API &rt, const std::string &dest, const std::string &orig,
                                        std::uint64_t timestamp, const std::string &edge_type, TQ tq,
                                        RT_API::TimeQueryInfo *info)
{
    if (not DSRGraph::is_valid_edge_type(edge_type)) return {};
    if (info) *info = RT_API::TimeQueryInfo{};
    RT_API::TimeQueryInfo edge_info;
    const auto severity = [](RT_API::TimeQueryInfo::Outcome o) {
        switch (o) { case RT_API::TimeQueryInfo::Outcome::Exact: return 0; case RT_API::TimeQueryInfo::Outcome::Stale: return 1;
            case RT_API::TimeQueryInfo::Outcome::Interpolated: return 2; case RT_API::TimeQueryInfo::Outcome::Extrapolated: return 3;
            case RT_API::TimeQueryInfo::Outcome::Clamped: return 4; } return 0; };
    const auto note = [&](const RT_API::TimeQueryInfo &e) {
        if (!info) return;
        if (e.outcome == RT_API::TimeQueryInfo::Outcome::Stale) info->stale_edges += 1;
        if (severity(e.outcome) > severity(info->outcome)) info->outcome = e.outcome;
        if (std::llabs(e.applied_dt_ms) > std::llabs(info->applied_dt_ms)) info->applied_dt_ms = e.applied_dt_ms;
        if (std::llabs(e.gap_ms) > std::llabs(info->gap_ms)) info->gap_ms = e.gap_ms;
        if (e.ring_span_ms > info->ring_span_ms) info->ring_span_ms = e.ring_span_ms;
    };
    Mat::RTMat atotal(Mat::RTMat::Identity()), btotal(Mat::RTMat::Identity());
    auto an = G.get_node(orig); auto bn = G.get_node(dest);
    if (not an or not bn) return {};
    auto a = an.value(); auto b = bn.value();
    int minLevel = std::min(G.get_node_level(a).value_or(-1), G.get_node_level(b).value_or(-1));
    if (minLevel == -1) return {};
    while (G.get_node_level(a).value_or(-1) >= minLevel) {
        auto p = G.get_parent_node(a);
        if (not p) break;
        auto e = rt.get_edge_RT(p.value(), a.id(), edge_type);
        if (not e) return {};
        if (auto m = rt.get_edge_RT_as_rtmat(e.value(), timestamp, tq, &edge_info); m) { note(edge_info); atotal = m.value() * atotal; a = p.value(); }
        else return {};
    }
    while (G.get_node_level(b).value_or(-1) >= minLevel) {
        auto p = G.get_parent_node(b);
        if (not p) break;
        auto e = rt.get_edge_RT(p.value(), b.id(), edge_type);
        if (not e) return {};
        if (auto m = rt.get_edge_RT_as_rtmat(e.value(), timestamp, tq, &edge_info); m) { note(edge_info); btotal = m.value() * btotal; b = p.value(); }
        else return {};
    }
    while (a.id() != b.id()) {
        auto p = G.get_node(G.get_parent_id(a).value_or(-1));
        auto q = G.get_node(G.get_parent_id(b).value_or(-1));
        if (p and q) {
            auto ae = rt.get_edge_RT(p.value(), a.id(), edge_type); if (not ae) return {};
            auto be = rt.get_edge_RT(q.value(), b.id(), edge_type); if (not be) return {};
            RT_API::TimeQueryInfo ai, bi;
            auto am = rt.get_edge_RT_as_rtmat(ae.value(), timestamp, tq, &ai);
            auto bm = rt.get_edge_RT_as_rtmat(be.value(), timestamp, tq, &bi);
            note(ai); note(bi);
            if (am and bm) { atotal = am.value() * atotal; btotal = bm.value() * btotal; a = p.value(); b = q.value(); }
            else return {};
        } else return {};
    }
    return btotal.inverse() * atotal;
}

static int fails = 0, checks = 0;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const int N = (argc > 1) ? std::atoi(argv[1]) : 200;
    for (auto mode : {SyncMode::CRDT, SyncMode::LWW})
    {
        std::printf("=== sync_mode=%s ===\n", mode == SyncMode::CRDT ? "CRDT" : "LWW");
        auto ctx = make_empty_config_file();
        DSRGraph G(make_test_graph_settings(random_string(10), 7 + (int)mode, ctx, true, 0, SignalMode::QT, mode));
        auto rt = G.get_rt_api();
        auto mk = [&](const std::string &name, std::optional<int> level = std::nullopt) {
            auto n = Node::create<testtype_node_type>(name);
            if (level) G.add_attrib_local<level_att>(n, *level);
            auto id = G.insert_node(n); return id.value();
        };
        auto link = [&](const std::string &parent, uint64_t child, std::vector<float> t, std::vector<float> r,
                        std::optional<uint64_t> ts = std::nullopt) {
            auto p = G.get_node(parent).value();
            rt->insert_or_assign_edge_RT(p, child, t, r, ts);
        };
        const auto root = G.get_node("root").value().id();
        (void)root;
        auto room = mk("room"), robot = mk("robot"), lidar = mk("lidar"), zed = mk("zed"), other = mk("other"), thing = mk("thing");
        link("root", room, {1, 2, 0.5f}, {0.f, 0.f, 0.3f});
        link("room", robot, {0.5f, -0.4f, 0.f}, {0.f, 0.f, 0.1f});
        link("robot", lidar, {0.1f, 0.f, 0.8f}, {0.f, 0.1f, 0.f});
        link("robot", zed, {0.2f, 0.f, 0.9f}, {0.05f, 0.f, 0.f});
        link("root", other, {-3, 1, 0}, {0.f, 0.f, -0.7f});
        link("other", thing, {0.3f, 0.2f, 0.1f}, {0.f, 0.f, 0.2f});
        // moving edge with timestamped history (ts 1000..1800 ms)
        for (int k = 0; k < 8; ++k)
            link("room", robot, {0.5f + 0.1f * k, -0.4f, 0.f}, {0.f, 0.f, 0.1f + 0.05f * k}, 1000 + 100 * k);
        std::vector<std::string> names{"root", "room", "robot", "lidar", "zed", "other", "thing"};
        for (int i = 0; i < N; ++i) {
            auto nm = "obj_" + std::to_string(i);
            auto id = mk(nm);
            link("room", id, {0.01f * i, 0.02f * (i % 7), 0.f}, {0.f, 0.f, 0.01f * i});
            if (i < 40) names.push_back(nm);
        }
        // failure cases
        auto orphan_lvl = mk("orphan_level", 3);          // level but no parent / no edge
        auto orphan = mk("orphan");                        // no level
        (void)orphan_lvl; (void)orphan;
        auto ghost = mk("ghost_edge");
        link("room", ghost, {1, 1, 1}, {0, 0, 0});
        G.delete_edge(G.get_node("room").value().id(), ghost, "RT");   // parent attr remains, edge gone
        names.insert(names.end(), {"orphan_level", "orphan", "ghost_edge", "does_not_exist"});

        auto api = G.get_inner_eigen_api();
        struct Q { std::uint64_t ts; TQ tq; };
        const Q qs[] = {{0, TQ::Interpolated}, {1350, TQ::Interpolated}, {1350, TQ::Nearest}, {900, TQ::Interpolated},
                        {5000, TQ::Interpolated}, {1900, TQ::Extrapolated}, {1500, TQ::Extrapolated}};
        long pairs = 0;
        for (const auto &d : names) for (const auto &o : names) for (const auto &q : qs) {
            RT_API::TimeQueryInfo oi, ni;
            auto ov = old_tm(G, *rt, d, o, q.ts, "RT", q.tq, &oi);
            std::optional<Mat::RTMat> nv;
            if (q.ts == 0) { auto fresh = G.get_inner_eigen_api(); nv = fresh->get_transformation_matrix(d, o, 0, "RT", q.tq, &ni); }
            else nv = api->get_transformation_matrix(d, o, q.ts, "RT", q.tq, &ni);
            ++checks; ++pairs;
            bool ok = ov.has_value() == nv.has_value();
            if (ok and ov) ok = (ov->matrix() - nv->matrix()).cwiseAbs().maxCoeff() < 1e-12;
            if (ok and ov) ok = oi.outcome == ni.outcome and oi.applied_dt_ms == ni.applied_dt_ms and oi.gap_ms == ni.gap_ms
                                and oi.ring_span_ms == ni.ring_span_ms and oi.stale_edges == ni.stale_edges;
            if (not ok) { ++fails; if (fails < 10) std::printf("MISMATCH %s <- %s ts=%lu\n", d.c_str(), o.c_str(), (unsigned long)q.ts); }
        }
        // invalid edge type
        ++checks; if (api->get_transformation_matrix("room", "robot", 0, "bogus").has_value()) { ++fails; std::printf("MISMATCH invalid edge type\n"); }
        // ts==0 cache invalidation: change robot pose, cached value must follow after slots run
        {
            auto before = api->get_transformation_matrix("room", "robot", 0, "RT");
            link("room", robot, {9.f, 9.f, 9.f}, {0, 0, 0});
            for (int k = 0; k < 20; ++k) QCoreApplication::processEvents();
            auto after = api->get_transformation_matrix("room", "robot", 0, "RT");
            ++checks;
            if (not before or not after or std::abs(after->translation().x() - 9.0) > 1e-6) { ++fails; std::printf("MISMATCH cache invalidation\n"); }
        }
        std::printf("compared %ld (dest,orig,query) combos; %d tree nodes\n", pairs, (int)names.size());

        // ---- benchmark (ts != 0: bypasses the cache, i.e. the real cost of a walk) ----
        const int reps = 2000;
        auto bench = [&](bool old, const char *d, const char *o) {
            auto t0 = std::chrono::steady_clock::now();
            double sink = 0;
            for (int i = 0; i < reps; ++i) {
                std::optional<Mat::RTMat> r = old ? old_tm(G, *rt, d, o, 1350, "RT", TQ::Interpolated, nullptr)
                                                  : api->get_transformation_matrix(d, o, 1350, "RT", TQ::Interpolated);
                sink += r ? r->translation().x() : 0;
            }
            auto dt = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / reps;
            if (sink == 12345.678) std::puts("");
            return dt;
        };
        const char *pairs_b[][2] = {{"root", "lidar"}, {"room", "lidar"}, {"obj_3", "lidar"}, {"thing", "lidar"}};
        for (auto &p : pairs_b)
            std::printf("  %-6s <- %-6s  old %8.2f us   new %8.2f us   x%.1f\n", p[0], p[1],
                        bench(true, p[0], p[1]), bench(false, p[0], p[1]),
                        bench(true, p[0], p[1]) / bench(false, p[0], p[1]));
    }
    std::printf("\n%d / %d checks failed\n", fails, checks);
    return fails ? 1 : 0;
}
