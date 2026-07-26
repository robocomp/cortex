/*
 *    Copyright (C) 2026 by RoboLab at the University of Extremadura
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef GRAPHNODEMINDWIDGET_H
#define GRAPHNODEMINDWIDGET_H

// GraphNodeMindWidget — native "mind" node dashboard.
//
// Opens when the user selects "View data" on the mind node in the graph viewer. Draws the agents
// hanging off the mind node (DSR nodes of type "agent", one per running RoboComp process) plus the
// ICE interconnection network derived from each agent's self-reported etc/config (agent_config
// attribute): RPC edges resolved by matching a required proxy port to the agent that implements it,
// pub/sub edges routed through an IceStorm broker node, and dashed "external" boxes for required
// interfaces whose server is outside the mind subtree (e.g. the sensorimotor layer). Live per-edge
// bandwidth is captured by an AF_PACKET loopback sniffer (needs cap_net_raw on the viewer) and
// attributed to RPC edges by server port. Agent boxes carry live CPU / memory / uptime pushed at
// ~1 Hz by every agent's AgentInfoAPI heartbeat. Everything is read straight from the graph — no
// launcher, no TOML, no psutil.

#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <optional>
#include <vector>
#include <algorithm>
#include <functional>
#include <string>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>

#include <unistd.h>
#include <cerrno>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>

#include <QWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QLabel>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QPainter>
#include <QFont>
#include <QBrush>
#include <QPen>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPointF>

#include <dsr/api/dsr_api.h>
#include "dds_stats_monitor.h"   // optional Fast DDS Statistics consumer (compile-gated by RC_DDS_STATS)

// ─── Small QGraphicsView with wheel zoom + double-click-to-fit (no Q_OBJECT → no moc) ──────────────
class MindGraphicsView : public QGraphicsView
{
    public:
        explicit MindGraphicsView(QGraphicsScene* s, QWidget* parent = nullptr)
            : QGraphicsView(s, parent)
        {
            setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
            setResizeAnchor(QGraphicsView::AnchorViewCenter);
        }
        std::function<void()> on_double_click;
        std::function<void(qulonglong)> on_node_click;   // fires on every left-click of a selectable node
    protected:
        void wheelEvent(QWheelEvent* e) override
        {
            const double f = (e->angleDelta().y() > 0) ? 1.15 : 1.0 / 1.15;
            scale(f, f);
            e->accept();
        }
        void mousePressEvent(QMouseEvent* e) override
        {
            // Click ON a selectable node → activate it (fires even when it's already selected, so the
            // caller can toggle). Click on empty space → fall through to ScrollHandDrag panning.
            if (e->button() == Qt::LeftButton)
            {
                QGraphicsItem* it = itemAt(e->pos());
                while (it && !(it->flags() & QGraphicsItem::ItemIsSelectable)) it = it->parentItem();
                if (it && on_node_click) { on_node_click(it->data(0).toULongLong()); e->accept(); return; }
            }
            QGraphicsView::mousePressEvent(e);
        }
        void mouseDoubleClickEvent(QMouseEvent* e) override
        {
            if (on_double_click) on_double_click();
            QGraphicsView::mouseDoubleClickEvent(e);
        }
};

namespace mind_ui
{
    // ── ICE config parsing (native port of netmon/topology.py::_parse_config) ───────────────────────
    struct Impl { std::string iface; int port; };
    struct Req  { std::string identity; int port; std::string label; };
    struct Parsed { std::vector<Impl> impl; std::vector<Req> req;
                    std::vector<std::string> pub, sub; };

    inline std::string trim(std::string s)
    {
        const char* ws = " \t\r\n\"";
        const auto b = s.find_first_not_of(ws);
        if (b == std::string::npos) return {};
        const auto e = s.find_last_not_of(ws);
        return s.substr(b, e - b + 1);
    }
    inline int extract_flag_int(const std::string& v, const std::string& flag)  // e.g. "-p" -> port
    {
        const auto p = v.find(flag + " ");
        if (p == std::string::npos) return 0;
        try { return std::stoi(v.substr(p + flag.size() + 1)); } catch (...) { return 0; }
    }
    inline bool ends_with(const std::string& s, const std::string& suf)
    { return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0; }

    inline Parsed parse_config(const std::string& text)
    {
        Parsed out;
        std::string section;
        std::size_t i = 0;
        while (i < text.size())
        {
            std::size_t nl = text.find('\n', i);
            std::string line = text.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
            i = (nl == std::string::npos) ? text.size() : nl + 1;
            if (const auto h = line.find('#'); h != std::string::npos) line = line.substr(0, h);
            const std::string t = trim(line);
            if (t.empty()) continue;
            if (t.front() == '[')
            {
                const auto e = t.find(']');
                if (e != std::string::npos) section = trim(t.substr(1, e - 1));
                continue;
            }
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));

            std::string group, name;
            if (key.rfind("Proxies.", 0) == 0)       { group = "Proxies";   name = key.substr(8); }
            else if (key.rfind("Endpoints.", 0) == 0){ group = "Endpoints"; name = key.substr(10); }
            else if (section == "Proxies" || section == "Endpoints") { group = section; name = key; }
            else continue;

            if (group == "Endpoints")
            {
                if (ends_with(name, "Topic"))       out.sub.push_back(name.substr(0, name.size() - 5));
                else if (ends_with(name, "Prefix")) out.sub.push_back(name.substr(0, name.size() - 6));
                else if (const int p = extract_flag_int(val, "-p"); p > 0)
                    out.impl.push_back({name, p});
            }
            else  // Proxies
            {
                if (name == "TopicManager") continue;
                if (ends_with(name, "Prefix")) { out.pub.push_back(name.substr(0, name.size() - 6)); continue; }
                const std::string identity = trim(val.substr(0, val.find(':')));
                if (!identity.empty())
                    out.req.push_back({identity, extract_flag_int(val, "-p"), name});
            }
        }
        return out;
    }

    // ── Interconnection graph (native port of topology.py::build_topology) ──────────────────────────
    // kind 0 rpc, 1 pub, 2 sub. `name` is the requiring proxy key (e.g. "MediaPlaneDDS1"): it names the
    // logical link, disambiguating several proxies that share one ICE identity but hit different ports.
    struct Edge { std::string src, dst; int port = 0; std::string iface, topic, name; int kind = 0; };
    struct AgentRef { std::string key; std::string config; };
    struct Topo { std::vector<Edge> edges; std::vector<std::string> externals;
                  bool any_ps = false; std::set<int> server_ports; };

    inline Topo build_topo(const std::vector<AgentRef>& agents)
    {
        Topo topo;
        std::map<std::string, Parsed> data;
        std::map<int, std::string> port_owner;
        std::map<std::string, std::vector<std::pair<std::string,int>>> ident_owner;
        for (const auto& a : agents)
        {
            Parsed p = parse_config(a.config);
            for (const auto& e : p.impl)
            {
                port_owner[e.port] = a.key;
                std::string lo = e.iface; std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
                ident_owner[lo].push_back({a.key, e.port});
                topo.server_ports.insert(e.port);
            }
            data[a.key] = std::move(p);
        }

        std::map<std::string, std::vector<std::string>> subs_by_topic;
        for (const auto& a : agents)
        {
            const auto& d = data[a.key];
            // RPC: match a required port to the implementing agent; else fall back to identity; else external.
            for (const auto& r : d.req)
            {
                if (r.port > 0) topo.server_ports.insert(r.port);
                std::string target; int tport = r.port;
                if (r.port > 0 && port_owner.count(r.port)) target = port_owner[r.port];
                else
                {
                    std::string lo = r.identity; std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
                    if (auto it = ident_owner.find(lo); it != ident_owner.end() && it->second.size() == 1)
                    { target = it->second[0].first; tport = it->second[0].second; }
                }
                if (!target.empty() && target != a.key)
                    topo.edges.push_back({a.key, target, tport, r.identity, "", r.label, 0});
                else if (target.empty())
                {
                    const std::string ext = r.identity + ":" + std::to_string(r.port);
                    if (std::find(topo.externals.begin(), topo.externals.end(), ext) == topo.externals.end())
                        topo.externals.push_back(ext);
                    topo.edges.push_back({a.key, ext, r.port, r.identity, "", r.label, 0});
                }
            }
            for (const auto& s : d.sub) subs_by_topic[s].push_back(a.key);
        }
        for (const auto& a : agents)
            for (const auto& t : data[a.key].pub)
            { topo.any_ps = true; topo.edges.push_back({a.key, "IceStorm", 0, "", t, "", 1}); }
        for (const auto& [t, subs] : subs_by_topic)
            for (const auto& s : subs)
            { topo.any_ps = true; topo.edges.push_back({"IceStorm", s, 0, "", t, "", 2}); }
        return topo;
    }

    // ── AF_PACKET loopback sniffer (native port of netmon/bandwidth.py) ─────────────────────────────
    // Sniffs `lo`, counts wire bytes per TCP port-pair filtered to the known server ports, drops the
    // PACKET_OUTGOING copy (loopback delivers each frame twice). Needs cap_net_raw+ep on the binary
    // (or root); otherwise `available` stays false and the view just shows topology with no Bps.
    class LoopbackBandwidth
    {
        public:
            ~LoopbackBandwidth() { stop(); }

            void start()
            {
                int fd = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
                if (fd < 0) { error = std::strerror(errno); return; }
                sockaddr_ll sll{};
                sll.sll_family   = AF_PACKET;
                sll.sll_protocol = htons(ETH_P_ALL);
                sll.sll_ifindex  = static_cast<int>(if_nametoindex("lo"));
                if (::bind(fd, reinterpret_cast<sockaddr*>(&sll), sizeof(sll)) < 0)
                { error = std::strerror(errno); ::close(fd); return; }
                timeval tv{0, 500000};
                ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                int rb = 16 * 1024 * 1024;   // big ring so high-rate bursts don't overflow → drop → undercount
#ifdef SO_RCVBUFFORCE
                if (::setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &rb, sizeof(rb)) < 0)  // bypasses rmem_max (needs cap_net_admin)
#endif
                    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rb, sizeof(rb));           // capped fallback
                sock = fd;
                running = true;
                available = true;
                prev_t = std::chrono::steady_clock::now();
                th = std::thread([this] { loop(); });
            }

            void stop()
            {
                running = false;
                if (sock >= 0) { ::close(sock); sock = -1; }
                if (th.joinable()) th.join();
            }

            void set_ports(std::set<int> p) { std::lock_guard<std::mutex> lk(mtx); ports = std::move(p); }
            std::set<int> server_ports() { std::lock_guard<std::mutex> lk(mtx); return ports; }

            // Bps per (loport, hiport) connection since the previous call. The widget resolves each
            // pair to a concrete client→server edge (inode→pid→agent), so attribution stays per-connection.
            std::map<std::pair<int,int>, double> sample_pairs()
            {
                const auto now = std::chrono::steady_clock::now();
                const double dt = std::max(1e-3, std::chrono::duration<double>(now - prev_t).count());
                std::map<std::pair<int,int>, std::uint64_t> snap;
                { std::lock_guard<std::mutex> lk(mtx); snap = counters; }
                std::map<std::pair<int,int>, double> out;
                for (const auto& [k, total] : snap)
                {
                    const auto it = prev.find(k);
                    const std::uint64_t base = (it == prev.end()) ? total : it->second;
                    out[k] = std::max(0.0, static_cast<double>(total - base) / dt);
                }
                prev = snap;
                prev_t = now;
                return out;
            }

            std::atomic_bool available{false};
            std::string error;

        private:
            void loop()
            {
                std::vector<std::uint8_t> buf(65536);
                while (running)
                {
                    sockaddr_ll addr{};
                    socklen_t alen = sizeof(addr);
                    const ssize_t n = ::recvfrom(sock, buf.data(), buf.size(), 0,
                                                 reinterpret_cast<sockaddr*>(&addr), &alen);
                    if (n < 0) { if (errno == EAGAIN || errno == EWOULDBLOCK) continue; break; }
                    if (addr.sll_pkttype == PACKET_OUTGOING) continue;      // drop loopback duplicate
                    if (n < 38) continue;                                    // 14 eth + 20 ip + 4 ports
                    std::uint16_t ethertype;
                    std::memcpy(&ethertype, buf.data() + 12, 2);
                    if (ntohs(ethertype) != 0x0800) continue;                // IPv4 only
                    const int ihl = (buf[14] & 0x0F) * 4;
                    if (buf[14 + 9] != 6) continue;                          // TCP only
                    const int off = 14 + ihl;
                    if (n < off + 4) continue;
                    std::uint16_t sp, dp;
                    std::memcpy(&sp, buf.data() + off, 2);
                    std::memcpy(&dp, buf.data() + off + 2, 2);
                    const int sport = ntohs(sp), dport = ntohs(dp);
                    std::lock_guard<std::mutex> lk(mtx);
                    if (!ports.count(sport) && !ports.count(dport)) continue;
                    const auto key = std::make_pair(std::min(sport, dport), std::max(sport, dport));
                    counters[key] += static_cast<std::uint64_t>(n);
                }
            }

            std::atomic_bool running{false};
            int sock = -1;
            std::thread th;
            std::mutex mtx;
            std::set<int> ports;                                       // server ports to attribute to
            std::map<std::pair<int,int>, std::uint64_t> counters;      // cumulative wire bytes per pair
            std::map<std::pair<int,int>, std::uint64_t> prev;
            std::chrono::steady_clock::time_point prev_t;
    };

    inline QString fmt_bps(double bps)
    {
        if (bps < 1) return {};
        static const char* u[] = {"B", "KB", "MB", "GB"};
        int i = 0; double v = bps;
        while (v >= 1024 && i < 3) { v /= 1024; ++i; }
        return QString::number(v, 'f', v < 10 ? 1 : 0) + " " + u[i] + "/s";
    }

    // The Fast DDS participant name is "Participant_<agent_id> ( <agent_name> )" (cortex
    // dsrparticipant.cpp), NOT the DSR node name "<agent_name> <id>". The agent node carries the same
    // integer in agent_id_att, so we join stats to boxes on that id. Returns -1 if it doesn't parse.
    inline int participant_agent_id(const std::string& pname)
    {
        const std::string tag = "Participant_";
        const auto p = pname.find(tag);
        if (p == std::string::npos) return -1;
        std::size_t i = p + tag.size();
        int id = 0; bool any = false;
        for (; i < pname.size() && std::isdigit(static_cast<unsigned char>(pname[i])); ++i)
            { id = id * 10 + (pname[i] - '0'); any = true; }
        return any ? id : -1;
    }
} // namespace mind_ui


class GraphNodeMindWidget : public QWidget
{
    Q_OBJECT
    public:
        GraphNodeMindWidget(std::shared_ptr<DSR::DSRGraph> graph_, std::uint64_t node_id_)
            : graph(std::move(graph_)), node_id(node_id_)
        {
            setWindowTitle("mind — agents & network");
            resize(1100, 620);

            view = new MindGraphicsView(&scene, this);
            view->setRenderHint(QPainter::Antialiasing);
            view->setDragMode(QGraphicsView::ScrollHandDrag);
            view->on_double_click = [this]{ fit_view(); };
            scene.setBackgroundBrush(QColor("#12141a"));

            bw_status = new QLabel(this);
            bw_status->setStyleSheet("QLabel { padding: 3px; color: #9aa4b2; }");
            dds_status_ = new QLabel(this);   // per-participant DDS throughput (only when RC_DDS_STATS on)
            dds_status_->setStyleSheet("QLabel { padding: 3px; color: #7fd1a0; }");
            dds_status_->setWordWrap(true);
            dds_status_->setVisible(dds_stats_.enabled());
            header = new QLabel("(no agent selected)", this);
            header->setStyleSheet("QLabel { font-weight: bold; padding: 4px; }");
            header->setWordWrap(true);
            agent_list = new QListWidget(this);
            agent_list->setMaximumWidth(240);
            config_view = new QPlainTextEdit(this);
            config_view->setReadOnly(true);
            config_view->setLineWrapMode(QPlainTextEdit::NoWrap);
            config_view->setFont(QFont("monospace", 9));

            right_pane = new QWidget(this);
            auto* right_lay = new QVBoxLayout(right_pane);
            right_lay->setContentsMargins(2, 2, 2, 2);
            right_lay->addWidget(header);
            right_lay->addWidget(config_view, 1);
            right_pane->hide();   // deployment panel stays hidden until an agent is selected

            auto* left = new QWidget(this);
            auto* left_lay = new QVBoxLayout(left);
            left_lay->setContentsMargins(0, 0, 0, 0);
            left_lay->addWidget(bw_status);
            left_lay->addWidget(dds_status_);
            left_lay->addWidget(view, 1);

            splitter = new QSplitter(Qt::Horizontal, this);
            splitter->addWidget(left);
            splitter->addWidget(agent_list);
            splitter->addWidget(right_pane);
            splitter->setStretchFactor(0, 3);
            splitter->setStretchFactor(2, 2);

            auto* lay = new QHBoxLayout(this);
            lay->setContentsMargins(2, 2, 2, 2);
            lay->addWidget(splitter);

            // Both the list and the canvas activate an agent; activating the already-shown agent hides
            // the panel again (toggle).
            connect(agent_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it){
                if (it) toggle_agent(static_cast<std::uint64_t>(it->data(Qt::UserRole).toULongLong()));
            });
            view->on_node_click = [this](qulonglong id){ if (id != 0) toggle_agent(static_cast<std::uint64_t>(id)); };

            bw.start();
            bw_status->setText(bw.available
                ? "● bandwidth: capturing on lo (wire bytes, incl. headers)"
                : QString("○ bandwidth: unavailable — %1 (setcap cap_net_raw+ep on the viewer binary)")
                      .arg(QString::fromStdString(bw.error.empty() ? "no capture permission" : bw.error)));

            rebuild();
            timer = new QTimer(this);
            connect(timer, &QTimer::timeout, this, &GraphNodeMindWidget::refresh);
            timer->start(1000);
            show();
        }

        void closeEvent(QCloseEvent*) override
        {
            if (timer) timer->stop();
            bw.stop();
            disconnect(graph.get(), nullptr, this, nullptr);
        }

    private:
        struct RpcEdgeVis { QGraphicsLineItem* line; QGraphicsSimpleTextItem* label;
                            std::string src, dst; int port; QString base; double smooth = 0.0; };

        // agent DSR node pid (agent_pid attr) -> agent key (node name). Empty for agents that predate
        // the pid self-report; those connections simply fall back to per-port attribution.
        std::map<std::uint32_t, std::string> pid_to_key(const std::vector<DSR::Node>& agents)
        {
            std::map<std::uint32_t, std::string> m;
            for (const auto& n : agents)
                if (auto p = graph->get_attrib_by_name<agent_pid_att>(n); p.has_value() && p.value() > 0)
                    m[p.value()] = n.name();
            return m;
        }

        // Map every local TCP port owned by a known agent to that agent, via the kernel's socket tables:
        // scan each agent pid's /proc/<pid>/fd for socket inodes, then read /proc/net/tcp[6] to turn
        // (established socket inode) into its local port. Both peers of a loopback connection appear
        // here, so a sniffed (client_port, server_port) pair resolves to (client agent, server agent).
        std::map<int, std::string> local_port_to_agent(const std::map<std::uint32_t, std::string>& pk)
        {
            std::map<unsigned long, std::string> inode_key;
            for (const auto& [pid, key] : pk)
            {
                const std::string dir = "/proc/" + std::to_string(pid) + "/fd";
                DIR* d = ::opendir(dir.c_str());
                if (!d) continue;
                while (dirent* e = ::readdir(d))
                {
                    char buf[256];
                    const ssize_t n = ::readlink((dir + "/" + e->d_name).c_str(), buf, sizeof(buf) - 1);
                    if (n <= 0) continue;
                    buf[n] = '\0';
                    if (std::strncmp(buf, "socket:[", 8) == 0)
                        inode_key[std::strtoul(buf + 8, nullptr, 10)] = key;
                }
                ::closedir(d);
            }
            std::map<int, std::string> out;
            for (const char* path : {"/proc/net/tcp", "/proc/net/tcp6"})
            {
                std::ifstream f(path);
                if (!f) continue;
                std::string line;
                std::getline(f, line);                       // header
                while (std::getline(f, line))
                {
                    std::istringstream ss(line);
                    std::vector<std::string> tok;
                    for (std::string t; ss >> t; ) tok.push_back(t);
                    if (tok.size() < 10 || tok[3] != "01") continue;   // 01 = ESTABLISHED
                    const auto colon = tok[1].rfind(':');
                    if (colon == std::string::npos) continue;
                    const int lport = static_cast<int>(std::strtol(tok[1].c_str() + colon + 1, nullptr, 16));
                    const unsigned long ino = std::strtoul(tok[9].c_str(), nullptr, 10);
                    if (auto it = inode_key.find(ino); it != inode_key.end())
                        out[lport] = it->second;
                }
            }
            return out;
        }

        std::vector<DSR::Node> current_agents()
        {
            std::vector<DSR::Node> out;
            std::uint64_t mind_id = 0;
            if (auto m = graph->get_node("mind"); m.has_value()) mind_id = m->id();
            for (auto& n : graph->get_nodes_by_type("agent"))
            {
                if (mind_id != 0)
                    if (auto p = graph->get_attrib_by_name<parent_att>(n); p.has_value() && p.value() != mind_id)
                        continue;
                out.push_back(n);
            }
            std::sort(out.begin(), out.end(),
                      [](const DSR::Node& a, const DSR::Node& b){ return a.name() < b.name(); });
            return out;
        }

        static QString fmt_uptime(std::uint64_t s)
        {
            return QString("%1:%2:%3").arg(s / 3600, 2, 10, QChar('0'))
                       .arg((s % 3600) / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
        }
        QString node_label(const DSR::Node& n)
        {
            const auto cpu = graph->get_attrib_by_name<cpu_usage_att>(n);
            const auto mem = graph->get_attrib_by_name<memory_usage_att>(n);
            const auto up  = graph->get_attrib_by_name<timestamp_alivetime_att>(n);
            QString l = QString::fromStdString(n.name());
            l += QString("\ncpu %1%   %2 MB")
                     .arg(cpu.has_value() ? QString::number(cpu.value(), 'f', 1) : "—")
                     .arg(mem.has_value() ? QString::number(mem.value() / 1024.0, 'f', 1) : "—");
            l += "\nup " + (up.has_value() ? fmt_uptime(up.value()) : QString("—"));
            return l;
        }

        // Pull the "<key>_topic":"value" stream topics out of a media_descriptor JSON string.
        static QString parse_topics(const std::string& j)
        {
            QStringList out;
            const std::string needle = "topic\":\"";
            for (std::size_t p = 0; (p = j.find(needle, p)) != std::string::npos; )
            {
                p += needle.size();
                const auto e = j.find('"', p);
                if (e == std::string::npos) break;
                out << QString::fromStdString(j.substr(p, e - p));
                p = e;
            }
            return out.join(", ");
        }
        static QString media_label(const QString& name, const QString& topics, double bps)
        {
            QString s = name + "   [media·SHM]";
            if (!topics.isEmpty()) s += "\n" + topics;
            const QString b = mind_ui::fmt_bps(bps);
            s += "\n" + (b.isEmpty() ? QString("idle") : b);
            return s;
        }
        double media_bps_of(std::uint64_t id)
        {
            if (auto n = graph->get_node(id); n.has_value())
                if (auto b = graph->get_attrib_by_name<media_bps_att>(n.value()); b.has_value())
                    return static_cast<double>(b.value());
            return 0.0;
        }
        // Runtime string attr media_ice_port (written by robot_concept): the mediaplanedds ICE port
        // that serves this producer's descriptor. 0 when absent (nothing to fuse with).
        static int media_ice_port_of(const DSR::Node& n)
        {
            if (auto it = n.attrs().find("media_ice_port"); it != n.attrs().end())
                try { return std::stoi(it->second.str()); } catch (...) {}
            return 0;
        }
        // Fused source label: SHM topic rate (media plane, big) + ICE descriptor rate (small).
        static QString merged_label(const QString& name, const QString& topics, double shm, double ice)
        {
            QString s = name + "   [SHM ↔ ICE]";
            if (!topics.isEmpty()) s += "\n" + topics;
            const QString sh = mind_ui::fmt_bps(shm), ic = mind_ui::fmt_bps(ice);
            s += "\nSHM " + (sh.isEmpty() ? QString("idle") : sh)
               + "    ICE " + (ic.isEmpty() ? QString("—") : ic);
            return s;
        }

        QGraphicsSimpleTextItem* add_text(const QString& s, const QColor& c, qreal x, qreal y, qreal z)
        {
            auto* t = scene.addSimpleText(s);
            t->setBrush(c);
            t->setPos(x, y);
            t->setZValue(z);
            return t;
        }

        // Deterministic 3-row layered layout (replaces the old Graphviz twopi radial view):
        //   row 1 (top)    — agents, spread horizontally
        //   row 2 (middle) — the shared buses/brokers as simple figures: domain-0 DSR graph plane
        //                    (cyan DIAMOND), domain-7 media plane (green SQUARE), IceStorm pub/sub broker
        //   row 3 (bottom) — sources: media producers (green) + external RPC servers (dashed grey)
        // Structural connections route THROUGH the middle figures (agent→d0, agent↔IceStorm pub/sub,
        // source→d7, consumer agent→d7); direct point-to-point RPC edges are kept but drawn only while
        // they carry traffic, and a standalone external server box is hidden until an RPC edge lights up.
        static constexpr double Y_AGENTS  = 0.0;
        static constexpr double Y_HUBS    = 320.0;
        static constexpr double Y_SOURCES = 640.0;

        void rebuild()
        {
            scene.clear();
            items.clear();
            drawn_ids.clear();
            rpc_vis.clear();
            media_items_.clear();
            merged_items_.clear();
            ext_items_.clear();
            d7_src_edges_.clear();
            agent_list->clear();

            const auto agents = current_agents();

            // Build the ICE interconnection graph from the self-reported configs.
            std::vector<mind_ui::AgentRef> refs;
            for (const auto& n : agents)
            {
                std::string cfg;
                if (auto c = graph->get_attrib_by_name<agent_config_att>(n); c.has_value()) cfg = c->get();
                refs.push_back({n.name(), cfg});
            }
            const mind_ui::Topo topo = mind_ui::build_topo(refs);
            server_ports_ = topo.server_ports;
            bw.set_ports(topo.server_ports);

            // Media producers (nodes with a media_descriptor). Each may carry a media_ice_port linking it
            // to a mediaplanedds:<port> ICE endpoint, so we FUSE that external into the producer's box.
            std::vector<DSR::Node> producers;
            for (auto& n : graph->get_nodes())
                if (n.attrs().find("media_descriptor") != n.attrs().end())
                    producers.push_back(n);
            std::sort(producers.begin(), producers.end(),
                      [](const DSR::Node& a, const DSR::Node& b){ return a.name() < b.name(); });
            std::map<int, std::size_t> port_to_prod;       // ICE port -> producer index
            for (std::size_t i = 0; i < producers.size(); ++i)
                if (const int port = media_ice_port_of(producers[i]); port > 0) port_to_prod[port] = i;
            auto topics_of = [&](const DSR::Node& n) -> QString
            {
                if (auto it = n.attrs().find("media_descriptor"); it != n.attrs().end())
                    return parse_topics(it->second.str());
                return {};
            };

            // Split externals into (a) those fused into a producer box (mediaplanedds endpoints) and
            // (b) standalone RPC servers (the sensorimotor layer) that get their own dashed source box.
            std::map<std::size_t, std::string> prod_ext_id;   // producer idx -> fused external id
            std::vector<std::string> standalone_ext;
            for (const auto& ext : topo.externals)
            {
                const auto colon = ext.rfind(':');
                const int port = (ext.rfind("mediaplanedds", 0) == 0 && colon != std::string::npos)
                                     ? std::atoi(ext.c_str() + colon + 1) : 0;
                if (auto pi = port_to_prod.find(port); pi != port_to_prod.end())
                    prod_ext_id[pi->second] = ext;
                else
                    standalone_ext.push_back(ext);
            }

            // ── Positions ─────────────────────────────────────────────────────────────────────────────
            std::map<std::string, QPointF> pos;   // visual-node id (agent name / external id / hub) -> scene
            const double AG_PITCH = 300.0;
            const double ax0 = -((static_cast<int>(agents.size()) - 1) * AG_PITCH) / 2.0;
            for (std::size_t i = 0; i < agents.size(); ++i)
                pos[agents[i].name()] = QPointF(ax0 + i * AG_PITCH, Y_AGENTS);

            const double HUB_PITCH = 320.0;         // d0 (left)   d7 (centre)   IceStorm (right)
            d0_center_  = QPointF(-HUB_PITCH, Y_HUBS);
            d7_center_  = QPointF(0.0,        Y_HUBS);
            ice_center_ = QPointF(HUB_PITCH,  Y_HUBS);
            pos["IceStorm"] = ice_center_;

            const double SRC_PITCH = 300.0;
            const std::size_t nsrc = producers.size() + standalone_ext.size();
            double sx = -((static_cast<int>(nsrc) - 1) * SRC_PITCH) / 2.0;
            std::vector<QPointF> prod_pos(producers.size());
            for (std::size_t i = 0; i < producers.size(); ++i)
            {
                prod_pos[i] = QPointF(sx, Y_SOURCES);
                pos[producers[i].name()] = prod_pos[i];
                if (auto it = prod_ext_id.find(i); it != prod_ext_id.end())
                    pos[it->second] = prod_pos[i];   // fused mediaplanedds RPC edge lands on the producer box
                sx += SRC_PITCH;
            }
            std::map<std::string, QPointF> ext_pos;
            for (const auto& ext : standalone_ext)
            {
                ext_pos[ext] = QPointF(sx, Y_SOURCES);
                pos[ext] = ext_pos[ext];
                sx += SRC_PITCH;
            }

            // ── Structural plane edges (drawn first, under the boxes) ───────────────────────────────────
            // Source → domain-7 SQUARE (media/SHM plane); thickness tracks media_bps in refresh().
            for (std::size_t i = 0; i < producers.size(); ++i)
            {
                auto* ln = scene.addLine(prod_pos[i].x(), prod_pos[i].y(), d7_center_.x(), d7_center_.y(),
                                         QPen(QColor("#2f6f3a"), 1.4));
                ln->setZValue(0);
                d7_src_edges_.push_back({ln, producers[i].id()});
            }

            // ICE topology edges: pub/sub route THROUGH the IceStorm figure; RPC stays point-to-point
            // (kept per design) but hidden until it carries traffic.
            for (const auto& e : topo.edges)
            {
                const auto a = pos.find(e.src), b = pos.find(e.dst);
                if (a == pos.end() || b == pos.end()) continue;
                if (e.kind == 0)   // RPC — drawn only while it carries traffic (empty flows stay hidden)
                {
                    auto* ln = scene.addLine(a->second.x(), a->second.y(), b->second.x(), b->second.y(),
                                             QPen(QColor("#4a5568"), 1.2));
                    ln->setZValue(0);
                    ln->setVisible(false);
                    // Label by the proxy name when known (identifies the concrete link, e.g. two
                    // "mediaplanedds" proxies on different ports), falling back to the ICE identity.
                    const QString who = e.name.empty() ? QString::fromStdString(e.iface)
                                                       : QString::fromStdString(e.name);
                    const QString base = QString("%1 :%2").arg(who).arg(e.port);
                    auto* lbl = add_text(base, QColor("#9aa4b2"),
                                         (a->second.x() + b->second.x()) / 2,
                                         (a->second.y() + b->second.y()) / 2, 1);
                    lbl->setVisible(false);
                    rpc_vis.push_back({ln, lbl, e.src, e.dst, e.port, base, 0.0});
                }
                else               // pub/sub via IceStorm
                {
                    QPen pen(QColor("#b072e0"), 1.4);
                    pen.setStyle(Qt::DashLine);
                    scene.addLine(a->second.x(), a->second.y(), b->second.x(), b->second.y(), pen)->setZValue(0);
                    add_text((e.kind == 1 ? "pub " : "sub ") + QString::fromStdString(e.topic),
                             QColor("#c39be6"), (a->second.x() + b->second.x()) / 2,
                             (a->second.y() + b->second.y()) / 2, 1);
                }
            }

            // ── Middle-row hub figures ──────────────────────────────────────────────────────────────────
            // Domain-0 DSR graph plane: cyan DIAMOND. Every agent joins it by a spoke (multicast bus).
            {
                const double R = 46;
                QPolygonF d;
                d << QPointF(d0_center_.x(),     d0_center_.y() - R) << QPointF(d0_center_.x() + R, d0_center_.y())
                  << QPointF(d0_center_.x(),     d0_center_.y() + R) << QPointF(d0_center_.x() - R, d0_center_.y());
                scene.addPolygon(d, QPen(QColor("#38bdf8"), 2.2), QBrush(QColor("#0e2a3a")))->setZValue(2);
                add_text("DDS d0\ngraph plane", QColor("#9fdbf5"),
                         d0_center_.x() - 34, d0_center_.y() + R + 4, 3)->setData(0, QVariant());
            }
            // Domain-7 media plane: green SQUARE. Sources publish into it; consumer agents read from it.
            {
                const double S = 84;
                scene.addRect(d7_center_.x() - S / 2, d7_center_.y() - S / 2, S, S,
                              QPen(QColor("#3fb950"), 2.2), QBrush(QColor("#16351c")))->setZValue(2);
                add_text("DDS d7\nmedia plane", QColor("#9be6a8"),
                         d7_center_.x() - 34, d7_center_.y() + S / 2 + 4, 3)->setData(0, QVariant());
            }
            // IceStorm pub/sub broker: purple circle.
            {
                const double R = 42;
                scene.addEllipse(ice_center_.x() - R, ice_center_.y() - R, 2 * R, 2 * R,
                                 QPen(QColor("#b072e0"), 2.0), QBrush(QColor("#2b1d3d")))->setZValue(2);
                add_text("IceStorm\npub/sub", QColor("#c39be6"),
                         ice_center_.x() - 30, ice_center_.y() + R + 4, 3)->setData(0, QVariant());
            }

            // Proxy name(s) referencing each external, so a generic ICE identity shared by several
            // proxies (e.g. "mediaplanedds" on :12002 vs :10099) is labelled by the concrete link name.
            std::map<std::string, std::set<std::string>> ext_names;
            for (const auto& e : topo.edges)
                if (e.kind == 0 && !e.name.empty()) ext_names[e.dst].insert(e.name);

            // ── Bottom-row sources: media producers (green, always shown) ───────────────────────────────
            for (std::size_t i = 0; i < producers.size(); ++i)
            {
                const auto& n = producers[i];
                const auto& p = prod_pos[i];
                const QString topics = topics_of(n);
                constexpr double W = 236, H = 74;
                scene.addRect(p.x() - W / 2, p.y() - H / 2, W, H, QPen(QColor("#3fb950"), 1.6),
                              QBrush(QColor("#16351c")))->setZValue(3);
                auto* label = add_text("", QColor("#d7ffe0"), p.x() - W / 2 + 6, p.y() - H / 2 + 6, 4);
                if (auto it = prod_ext_id.find(i); it != prod_ext_id.end())
                {
                    // Fused source: SHM topic rate (media_bps) + small ICE descriptor rate on the same box.
                    const auto colon = it->second.rfind(':');
                    const int  port  = (colon != std::string::npos) ? std::atoi(it->second.c_str() + colon + 1) : 0;
                    merged_items_.push_back({label, n.id(), port, QString::fromStdString(n.name()), topics});
                    label->setText(merged_label(QString::fromStdString(n.name()), topics, media_bps_of(n.id()), 0.0));
                }
                else
                {
                    media_items_.push_back({n.id(), label, QString::fromStdString(n.name()), topics});
                    label->setText(media_label(QString::fromStdString(n.name()), topics, media_bps_of(n.id())));
                }
            }

            // Standalone external RPC servers (dashed grey): hidden until an RPC edge into them is active.
            for (const auto& ext : standalone_ext)
            {
                const auto& p = ext_pos[ext];
                constexpr double W = 168, H = 44;
                QPen pen(QColor("#55606e"), 1.4);
                pen.setStyle(Qt::DashLine);
                auto* box = scene.addRect(p.x() - W / 2, p.y() - H / 2, W, H, pen, QBrush(QColor("#22252b")));
                box->setZValue(3);
                box->setVisible(false);
                QString lbl = QString::fromStdString(ext);
                if (auto it = ext_names.find(ext); it != ext_names.end() && !it->second.empty())
                {
                    QStringList names;
                    for (const auto& s : it->second) names << QString::fromStdString(s);
                    lbl = names.join(" / ") + "\n" + QString::fromStdString(ext);   // proxy name over identity:port
                }
                auto* label = add_text(lbl, QColor("#c8ccd2"), p.x() - W / 2 + 6, p.y() - H / 2 + 6, 4);
                label->setVisible(false);
                ext_items_.push_back({box, label, ext});
            }

            // ── Top-row agents: blue ellipses, selectable, carrying live metrics ────────────────────────
            for (const auto& n : agents)
            {
                const auto& p = pos[n.name()];
                constexpr double W = 210, H = 96;   // wider ellipse so the 3-line label fits inside
                auto* box = scene.addEllipse(p.x() - W / 2, p.y() - H / 2, W, H,
                                             QPen(QColor("#5aa0e6"), 1.8), QBrush(QColor("#123b66")));
                box->setFlag(QGraphicsItem::ItemIsSelectable, true);
                box->setData(0, QVariant::fromValue<qulonglong>(n.id()));
                box->setZValue(4);
                auto* label = scene.addSimpleText(node_label(n));
                label->setBrush(QColor("#eaf2fb"));
                label->setParentItem(box);
                const auto br = label->boundingRect();      // centre the text in the ellipse
                label->setPos(p.x() - br.width() / 2, p.y() - br.height() / 2);
                label->setZValue(5);

                // Domain-0 spoke: agent → d0 diamond. Always drawn (structural); refresh() thickens/
                // brightens it with live domain-0 throughput.
                auto* spoke = scene.addLine(p.x(), p.y(), d0_center_.x(), d0_center_.y(),
                                            QPen(QColor("#25506a"), 1.0));
                spoke->setZValue(0);   // under the boxes
                // Domain-7 consumer edge: agent → d7 square. Hidden until this agent RECEIVES media
                // (measurable only with RC_DDS_STATS); refresh() shows + thickens it with the d7 in-rate.
                auto* d7e = scene.addLine(p.x(), p.y(), d7_center_.x(), d7_center_.y(),
                                          QPen(QColor("#2f6f3a"), 1.0));
                d7e->setZValue(0);
                d7e->setVisible(false);

                // Domain-0 (DSR graph plane) throughput: two labels that sit ON the spoke, filled in by
                // refresh() from the DDS statistics monitor. Hidden until they carry traffic (and only
                // ever visible when built + run with RC_DDS_STATS). Children of the box so they survive
                // with it; positioned in scene coords along the spoke each tick.
                auto* in_lbl = scene.addSimpleText(QString());
                in_lbl->setBrush(QColor("#7fd1a0"));   // green = incoming (bus → agent)
                in_lbl->setParentItem(box);
                in_lbl->setZValue(6);
                in_lbl->setVisible(false);
                auto* out_lbl = scene.addSimpleText(QString());
                out_lbl->setBrush(QColor("#e0a35a"));  // amber = outgoing (agent → bus)
                out_lbl->setParentItem(box);
                out_lbl->setZValue(6);
                out_lbl->setVisible(false);

                items[n.id()] = {box, label, in_lbl, out_lbl, spoke, d7e};
                drawn_ids.push_back(n.id());

                auto* it = new QListWidgetItem(QString::fromStdString(n.name()), agent_list);
                it->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(n.id()));
            }

            scene.setSceneRect(scene.itemsBoundingRect().adjusted(-60, -60, 60, 60));
            QTimer::singleShot(0, this, [this]{ fit_view(); });

            // Re-apply the highlight if the shown agent survived the rebuild; otherwise close the panel.
            if (panel_visible)
            {
                if (auto it = items.find(selected_id); it != items.end()) it->second.box->setSelected(true);
                else hide_panel();
            }
        }

        void fit_view()
        {
            const auto r = scene.itemsBoundingRect().adjusted(-60, -60, 60, 60);
            if (r.isValid()) view->fitInView(r, Qt::KeepAspectRatio);
        }

        void refresh()
        {
            const auto agents = current_agents();
            std::vector<std::uint64_t> ids;
            ids.reserve(agents.size());
            for (auto& n : agents) ids.push_back(n.id());
            if (ids != drawn_ids) { rebuild(); return; }

            for (auto& n : agents)
                if (auto it = items.find(n.id()); it != items.end())
                    it->second.label->setText(node_label(n));

            // Media producers: rebuild if the set OR its ICE-port fusion changed (a sensor came up/left,
            // or robot_concept just published a media_ice_port a couple seconds after start). Otherwise
            // just refresh live throughput from the producer-reported media_bps.
            std::vector<std::pair<std::uint64_t,int>> prod_sig;
            for (auto& n : graph->get_nodes())
                if (n.attrs().find("media_descriptor") != n.attrs().end())
                    prod_sig.emplace_back(n.id(), media_ice_port_of(n));
            std::sort(prod_sig.begin(), prod_sig.end());
            std::vector<std::pair<std::uint64_t,int>> cur_sig;
            for (auto& m : media_items_)  cur_sig.emplace_back(m.id, 0);              // unmerged (left column)
            for (auto& m : merged_items_) cur_sig.emplace_back(m.producer_id, m.port);// fused with ICE endpoint
            std::sort(cur_sig.begin(), cur_sig.end());
            if (prod_sig != cur_sig) { rebuild(); return; }
            for (auto& m : media_items_)
                m.label->setText(media_label(m.name, m.topics, media_bps_of(m.id)));

            // Source → domain-7 plane edges: brighten + thicken with the producer's live SHM rate.
            for (auto& d : d7_src_edges_)
            {
                const double b = media_bps_of(d.producer_id);
                QPen pn = d.line->pen();
                pn.setColor(b > 1.0 ? QColor("#3fb950") : QColor("#2f6f3a"));
                pn.setWidthF(std::min(6.0, 1.0 + std::log2(1.0 + b / 4096.0)));
                d.line->setPen(pn);
            }

            // ── Per-connection bandwidth attribution ────────────────────────────────────────────────
            // Each sniffed (loport,hiport) pair → its server port (∈ server_ports_) picks the server
            // side; the other port is the client's ephemeral port, resolved to the owning agent via
            // /proc. The instantaneous Bps lands on the exact edge whose src==client and port==server.
            // Pairs whose client can't be resolved (e.g. an external client) fall back to an even split
            // across the edges sharing that server port, so nothing is silently dropped.
            const auto pairs  = bw.sample_pairs();
            const auto lp2key = local_port_to_agent(pid_to_key(agents));
            std::vector<double> inst(rpc_vis.size(), 0.0);
            std::map<int, double> port_unattr;
            for (const auto& [pr, bps] : pairs)
            {
                if (bps <= 0.0) continue;
                const int a = pr.first, b = pr.second;
                const int sp = server_ports_.count(a) ? a : (server_ports_.count(b) ? b : 0);
                if (sp == 0) continue;
                const int cp = (sp == a) ? b : a;
                std::string ck;
                if (auto it = lp2key.find(cp); it != lp2key.end()) ck = it->second;
                int idx = -1;
                if (!ck.empty())
                    for (std::size_t i = 0; i < rpc_vis.size(); ++i)
                        if (rpc_vis[i].port == sp && rpc_vis[i].src == ck) { idx = static_cast<int>(i); break; }
                if (idx >= 0) inst[idx] += bps;
                else          port_unattr[sp] += bps;
            }
            std::map<int, int> port_edges;
            for (const auto& e : rpc_vis) ++port_edges[e.port];
            std::map<int, double> ice_port_bps;   // smoothed ICE rate per server port → fused source boxes
            for (std::size_t i = 0; i < rpc_vis.size(); ++i)
            {
                if (auto it = port_unattr.find(rpc_vis[i].port);
                    it != port_unattr.end() && port_edges[rpc_vis[i].port] > 0)
                    inst[i] += it->second / port_edges[rpc_vis[i].port];

                // EMA smoothing: bursty request/reply links (unlike the steady IMU stream) would flicker
                // to blank every other second; smoothing holds a readable value that decays over ~3 s.
                auto& e = rpc_vis[i];
                e.smooth = 0.4 * inst[i] + 0.6 * e.smooth;
                ice_port_bps[e.port] += e.smooth;
                // Empty ICE flows are not shown: only draw the edge + label while it carries traffic.
                const bool active = e.smooth > 4.0;
                e.line->setVisible(active);
                e.label->setVisible(active);
                if (active)
                {
                    QPen pen = e.line->pen();
                    pen.setColor(QColor("#3fb950"));
                    pen.setWidthF(std::min(6.0, 1.2 + std::log2(1.0 + e.smooth / 512.0)));
                    e.line->setPen(pen);
                    const QString bw_txt = mind_ui::fmt_bps(e.smooth);
                    e.label->setText(bw_txt.isEmpty() ? e.base : e.base + "  " + bw_txt);
                }
            }

            // Fused media sources: SHM rate from the producer's media_bps, ICE rate from its port.
            for (auto& m : merged_items_)
            {
                const double ice = ice_port_bps.count(m.port) ? ice_port_bps[m.port] : 0.0;
                m.label->setText(merged_label(m.name, m.topics, media_bps_of(m.producer_id), ice));
            }

            // Standalone external servers: shown only while an RPC edge into them carries traffic
            // ("do not show the dark-grey elements that are not showing traffic").
            for (auto& x : ext_items_)
            {
                bool active = false;
                for (const auto& e : rpc_vis)
                    if (e.dst == x.id and e.smooth > 4.0) { active = true; break; }
                x.box->setVisible(active);
                x.label->setVisible(active);
            }

            // DDS Statistics readout (only present/active when built with RC_DDS_STATS and env-enabled):
            // per-participant throughput measured at the writer, so it sees SHM media traffic too.
            if (dds_stats_.enabled())
            {
                // Join participant → agent node on agent_id (participant name embeds it; the node carries
                // it in agent_id_att), and fold domain-0 stats into a node-keyed map + running totals.
                std::map<int, std::uint64_t> id_to_node;
                for (const auto& [nid, itm] : items)
                    if (auto nn = graph->get_node(nid); nn.has_value())
                        if (auto aid = graph->get_attrib_by_name<agent_id_att>(nn.value()); aid.has_value())
                            id_to_node[static_cast<int>(aid.value())] = nid;

                std::map<std::uint64_t, DdsInOut> node_io;   // domain-0 in/out per agent node
                DdsInOut d0_total, d7_total;
                for (const auto& [pname, io] : dds_stats_.per_participant(0))
                {
                    d0_total.in += io.in; d0_total.out += io.out;
                    const int aid = mind_ui::participant_agent_id(pname);
                    if (auto it = id_to_node.find(aid); aid >= 0 and it != id_to_node.end())
                        node_io[it->second] = io;
                }
                std::map<std::uint64_t, DdsInOut> node_io7;   // domain-7 in/out per agent node (consumers)
                for (const auto& [pname, io] : dds_stats_.per_participant(7))
                {
                    d7_total.in += io.in; d7_total.out += io.out;
                    const int aid = mind_ui::participant_agent_id(pname);
                    if (auto it = id_to_node.find(aid); aid >= 0 and it != id_to_node.end())
                        node_io7[it->second] = io;
                }

                // Canvas status line: AGGREGATE bus load only — per-agent detail now lives in the right
                // panel (agent list). Keeps the multicast bus's total fan-in/out at a glance.
                auto tot = [](const DdsInOut& t) {
                    const QString i = mind_ui::fmt_bps(t.in), o = mind_ui::fmt_bps(t.out);
                    return QString("↓ %1   ↑ %2").arg(i.isEmpty() ? "—" : i, o.isEmpty() ? "—" : o);
                };
                dds_status_->setText("DDS d0 multicast bus   " + tot(d0_total)
                                     + "        d7 media   " + tot(d7_total));

                // Reset spokes + spoke labels; only agents seen this tick light up.
                for (auto& [nid, itm] : items)
                {
                    if (itm.in_label)  itm.in_label->setVisible(false);
                    if (itm.out_label) itm.out_label->setVisible(false);
                    if (itm.spoke) { QPen pn = itm.spoke->pen(); pn.setColor(QColor("#25506a"));
                                     pn.setWidthF(1.0); itm.spoke->setPen(pn); }
                }
                for (const auto& [nid, io] : node_io)
                {
                    auto iit = items.find(nid);
                    if (iit == items.end() or not iit->second.in_label or not iit->second.out_label) continue;
                    auto& itm = iit->second;

                    // Anchor point on the spoke: a fixed distance from the agent toward the bus, so the
                    // two labels sit on the agent→bus edge just clear of the ellipse (perpendicular offset
                    // separates ↓in from ↑out). Spoke endpoints ARE the agent + bus centres.
                    const QLineF sp = itm.spoke ? itm.spoke->line() : QLineF(0, 0, 0, 0);
                    QPointF u = sp.p2() - sp.p1();
                    const double len = std::hypot(u.x(), u.y());
                    if (len > 1e-3) u /= len; else u = QPointF(0, -1);
                    const QPointF perp(-u.y(), u.x());
                    const QPointF anchor = sp.p1() + u * 118.0;   // 118px inward from agent centre
                    itm.in_label->setText("↓ " + mind_ui::fmt_bps(io.in));
                    itm.out_label->setText("↑ " + mind_ui::fmt_bps(io.out));
                    const QPointF ip = anchor - perp * 9.0, op = anchor + perp * 9.0;
                    itm.in_label->setPos(ip.x() - itm.in_label->boundingRect().width() / 2,
                                         ip.y() - itm.in_label->boundingRect().height() / 2);
                    itm.out_label->setPos(op.x() - itm.out_label->boundingRect().width() / 2,
                                          op.y() - itm.out_label->boundingRect().height() / 2);
                    itm.in_label->setVisible(io.in > 1.0);
                    itm.out_label->setVisible(io.out > 1.0);

                    // Live spoke: brighten + thicken with total domain-0 throughput on this edge.
                    if (itm.spoke)
                    {
                        const double t = io.in + io.out;
                        QPen pn = itm.spoke->pen();
                        pn.setColor(t > 1.0 ? QColor("#38bdf8") : QColor("#25506a"));
                        pn.setWidthF(std::min(6.0, 1.0 + std::log2(1.0 + t / 512.0)));
                        itm.spoke->setPen(pn);
                    }
                }

                // Domain-7 consumer edges: light agent → d7 square for agents receiving media this tick.
                for (auto& [nid, itm] : items)
                {
                    if (not itm.d7_edge) continue;
                    auto iit = node_io7.find(nid);
                    const double t = (iit != node_io7.end()) ? (iit->second.in + iit->second.out) : 0.0;
                    itm.d7_edge->setVisible(t > 1.0);
                    if (t > 1.0)
                    {
                        QPen pn = itm.d7_edge->pen();
                        pn.setColor(QColor("#3fb950"));
                        pn.setWidthF(std::min(6.0, 1.0 + std::log2(1.0 + t / 512.0)));
                        itm.d7_edge->setPen(pn);
                    }
                }

                // Per-agent totals in the RIGHT PANEL: one line per known agent, name + its d0 ↓in/↑out.
                for (int i = 0; i < agent_list->count(); ++i)
                {
                    auto* it = agent_list->item(i);
                    const auto id = static_cast<std::uint64_t>(it->data(Qt::UserRole).toULongLong());
                    QString base;
                    if (auto nn = graph->get_node(id); nn.has_value()) base = QString::fromStdString(nn->name());
                    else base = it->text().section('\n', 0, 0);   // fall back to the current first line
                    QString rate = "   ↓ —  ↑ —";
                    if (auto iit = node_io.find(id); iit != node_io.end())
                    {
                        const QString in = mind_ui::fmt_bps(iit->second.in), out = mind_ui::fmt_bps(iit->second.out);
                        rate = QString("   ↓ %1  ↑ %2").arg(in.isEmpty() ? "—" : in, out.isEmpty() ? "—" : out);
                    }
                    it->setText(base + "\n" + rate);
                }
            }
            // The deployment panel (cmd/cwd/config) is static per selection, so it is NOT re-rendered here.
        }

        // Activate an agent; activating the one already shown toggles the panel closed.
        void toggle_agent(std::uint64_t id)
        {
            if (id == selected_id && panel_visible) { hide_panel(); return; }
            show_agent(id);
        }

        void hide_panel()
        {
            panel_visible = false;
            selected_id = 0;
            right_pane->hide();
            scene.clearSelection();
            agent_list->setCurrentRow(-1);
        }

        void show_agent(std::uint64_t id)
        {
            auto n = graph->get_node(id);
            if (!n.has_value()) return;
            selected_id = id;

            for (int i = 0; i < agent_list->count(); ++i)
                if (agent_list->item(i)->data(Qt::UserRole).toULongLong() == id)
                    { agent_list->setCurrentRow(i); break; }
            if (auto it = items.find(id); it != items.end() && !it->second.box->isSelected())
            { scene.clearSelection(); it->second.box->setSelected(true); }

            const auto cwd = graph->get_attrib_by_name<agent_cwd_att>(n.value());
            const auto cmd = graph->get_attrib_by_name<agent_cmd_att>(n.value());
            const auto cfg = graph->get_attrib_by_name<agent_config_att>(n.value());
            header->setText(QString::fromStdString(n->name())
                            + (cwd.has_value() ? "\ncwd: " + QString::fromStdString(cwd->get()) : QString()));
            QString body;
            if (cmd.has_value() && !cmd->get().empty())
                body += "$ " + QString::fromStdString(cmd->get()) + "\n\n";
            if (cfg.has_value() && !cfg->get().empty())
                body += QString::fromStdString(cfg->get());
            else
                body += "(no config reported — agent predates the deployment self-report, "
                        "or was launched without a recognizable etc/config)";
            config_view->setPlainText(body);

            right_pane->show();
            panel_visible = true;
        }

        std::shared_ptr<DSR::DSRGraph> graph;
        std::uint64_t node_id;
        std::uint64_t selected_id = 0;
        bool panel_visible = false;

        QGraphicsScene scene;
        MindGraphicsView* view = nullptr;
        QListWidget* agent_list = nullptr;
        QPlainTextEdit* config_view = nullptr;
        QLabel* header = nullptr;
        QLabel* bw_status = nullptr;
        QLabel* dds_status_ = nullptr;
        DdsStatsMonitor dds_stats_{std::vector<std::uint32_t>{0u, 7u}};   // watch DSR domain 0 + media domain 7
        QSplitter* splitter = nullptr;
        QWidget* right_pane = nullptr;
        QTimer* timer = nullptr;

        mind_ui::LoopbackBandwidth bw;
        std::set<int> server_ports_;   // ports the sniffer attributes to (impl ∪ required)

        struct AgentItem { QGraphicsItem* box; QGraphicsSimpleTextItem* label;
                           QGraphicsSimpleTextItem* in_label = nullptr;    // ↓ domain-0 incoming (subscription)
                           QGraphicsSimpleTextItem* out_label = nullptr;   // ↑ domain-0 outgoing (publication)
                           QGraphicsLineItem* spoke = nullptr;             // agent → domain-0 diamond
                           QGraphicsLineItem* d7_edge = nullptr; };        // agent → domain-7 square (consumer)
        std::map<std::uint64_t, AgentItem> items;
        QPointF d0_center_, d7_center_, ice_center_;   // scene positions of the three middle-row hub figures
        std::vector<std::uint64_t> drawn_ids;
        std::vector<RpcEdgeVis> rpc_vis;

        // Standalone external RPC servers (sensorimotor layer) drawn in the bottom source row. Kept
        // hidden until an RPC edge into them lights up, so idle grey boxes never clutter the view.
        struct ExtItem { QGraphicsRectItem* box; QGraphicsSimpleTextItem* label; std::string id; };
        std::vector<ExtItem> ext_items_;

        // Source → domain-7 media-plane edges, thickened live by each producer's media_bps.
        struct D7Edge { QGraphicsLineItem* line; std::uint64_t producer_id; };
        std::vector<D7Edge> d7_src_edges_;

        // Media producers: nodes carrying a media_descriptor. Their pixels travel over zero-copy DDS
        // shared memory (invisible to the sniffer), so throughput comes from the self-reported
        // media_bps attribute the producer writes, and the topic names from the descriptor JSON.
        struct MediaItem { std::uint64_t id; QGraphicsSimpleTextItem* label; QString name, topics; };
        std::vector<MediaItem> media_items_;

        // Fused source (producer node + its mediaplanedds ICE endpoint): shows SHM rate (media_bps by
        // producer id) and ICE rate (sniffer bytes/s on that port) in one box.
        struct MergedItem { QGraphicsSimpleTextItem* label; std::uint64_t producer_id; int port;
                            QString name, topics; };
        std::vector<MergedItem> merged_items_;
};

#endif // GRAPHNODEMINDWIDGET_H
