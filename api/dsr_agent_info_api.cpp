//
// Created by juancarlos on 6/5/21.
//

#include "dsr/core/types/type_checking/dsr_edge_type.h"
#include <dsr/api/dsr_agent_info_api.h>
#include <dsr/api/dsr_api.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <QMetaObject>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

namespace DSR {

// Heartbeat runs on the raw Timer std::thread; bounce the agent-node update onto the DSRGraph's
// thread (the agent's main/GUI thread) so the graph write + Qt-signal emit happen there, never on a
// worker thread. QueuedConnection just posts; the timer thread does not block. If the graph thread's
// event loop isn't running yet the post simply waits — correct and harmless.
void AgentInfoAPI::heartbeat_tick()
{
    if (G == nullptr)
        return;
    QMetaObject::invokeMethod(G, [this]() { create_or_update_agent(); }, Qt::QueuedConnection);
}

    namespace {
        struct PipeCloser
        {
            void operator()(FILE *pipe) const
            {
                if (pipe != nullptr)
                    pclose(pipe);
            }
        };

        // ── Deployment self-report helpers ──────────────────────────────────────────────────────────
        // Read this process's argv from /proc/self/cmdline (NUL-separated). Empty on failure.
        std::vector<std::string> proc_argv()
        {
            std::vector<std::string> argv;
            std::ifstream f("/proc/self/cmdline", std::ios::binary);
            if (!f)
                return argv;
            std::string tok;
            for (char ch; f.get(ch); )
            {
                if (ch == '\0') { if (!tok.empty()) argv.push_back(tok); tok.clear(); }
                else            tok.push_back(ch);
            }
            if (!tok.empty()) argv.push_back(tok);
            return argv;
        }

        // This process's current working directory via /proc/self/cwd. Empty on failure.
        std::string proc_cwd()
        {
            char buf[PATH_MAX];
            const ssize_t n = ::readlink("/proc/self/cwd", buf, sizeof(buf) - 1);
            return n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string{};
        }

        // Pick the config path out of argv, mirroring the launcher's topology.py::_config_path:
        // the last token that looks like a config (under etc/, or ending in config/.toml/.conf),
        // unwrapping an --Ice.Config= prefix. Relative paths are resolved against cwd.
        std::string derive_config_path(const std::vector<std::string>& argv, const std::string& cwd)
        {
            std::string cand;
            for (std::string tok : argv)
            {
                constexpr const char* kIcePrefix = "--Ice.Config=";
                if (tok.rfind(kIcePrefix, 0) == 0)
                    tok = tok.substr(std::string(kIcePrefix).size());
                const bool looks_config =
                    tok.find("etc/") != std::string::npos ||
                    (tok.size() >= 6 && tok.compare(tok.size() - 6, 6, "config") == 0) ||
                    tok.find(".toml") != std::string::npos ||
                    tok.find(".conf")  != std::string::npos;
                if (looks_config)
                    cand = tok;
            }
            if (cand.empty())
                return cand;
            if (!cand.empty() && cand.front() != '/' && !cwd.empty())
                cand = cwd + "/" + cand;
            return cand;
        }

        // Slurp a text file whole. Empty on failure.
        std::string slurp_file(const std::string& path)
        {
            if (path.empty())
                return {};
            std::ifstream f(path, std::ios::binary);
            if (!f)
                return {};
            std::ostringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }
    }


    void AgentInfoAPI::stopTimer()
    {
        timer.stop_timer();
    }

    bool AgentInfoAPI::isRunning()
    {
        return timer.is_running();
    }
    /*void AgentInfoAPI::setPriod(uint32_t period_)
    {
        period = period_;
        timer.setInterval(static_cast<int32_t>(period_));
    }*/

    std::string AgentInfoAPI::exec(const char* cmd)
    {
        struct PipeCloser
        {
            void operator()(FILE* pipe) const noexcept
            {
                if (pipe != nullptr) {
                    pclose(pipe);
                }
            }
        };

        std::array<char, 128> buffer{};
        std::string result;
        std::unique_ptr<FILE, PipeCloser> pipe(popen(cmd, "r"));
        if (!pipe) {
            throw std::runtime_error("");
        }
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        return result;
    }
    
    void AgentInfoAPI::refresh_process_metrics()
    {
        pid_t pid = getpid();

        int nprocs = -1;
        int64_t memory_kb = -1;
        float cpu = -1.0;

        const char* w = " \t\n\r\f\v";
        const auto trim = [&](std::string & str) {
            str.erase(str.find_last_not_of(w) + 1);
            str.erase(0, str.find_first_not_of(w));
        };

        struct stat sts;
        if (stat(std::string("/proc/"+ std::to_string(pid) ).c_str(), &sts) == -1 && errno == ENOENT) {
            std::cout << __FILE__ << ":" << __LINE__ << " PROCESS DOES NOT EXIST" << std::endl;
        }

        try {
            std::string memory_kb_and_cpu = exec(
                    ("top -p " + std::to_string(pid) +" -b -c -n1 | grep  "+ std::to_string(pid) + " | awk '{print $6 \";\" $9}'").c_str());

            trim(memory_kb_and_cpu);
            const char delimiter = ';';
            const auto pos = memory_kb_and_cpu.find(delimiter);
            if (pos != std::string::npos) {
                try {
                    memory_kb = std::stoi(memory_kb_and_cpu.substr(0, pos));
                    memory_kb_and_cpu.erase(0, pos + 1);
                    std::replace(memory_kb_and_cpu.begin(), memory_kb_and_cpu.end(), ',', '.');
                    try {
                        cpu = std::stof(memory_kb_and_cpu);
                    } catch (...)
                    {
                        std::cerr << "Error in stof (parsing value "<< memory_kb_and_cpu << ")." << __FILE__ << ":" << __LINE__  << std::endl;
                    }
                } catch (...)
                {
                    std::cerr << "Error in stoi (parsing value "<< memory_kb_and_cpu.substr(0, pos) << ")." << __FILE__ << ":" << __LINE__  << std::endl;
                }
            } else {
                std::cerr << "Error parsing top output. " << __FILE__ << ":" << __LINE__  << std::endl;
            }
        } catch (const std::runtime_error &e)
        {
            std::cerr << "Error in popen. " << __FILE__ << ":" << __LINE__  << std::endl;
        }

        std::string number_of_process;
        try {
            number_of_process = exec(("echo $((`pstree -p "+ std::to_string(pid) +" | wc -l` + 1))").c_str());
            trim(number_of_process);
            if (!number_of_process.empty()) {
                nprocs = std::stoi(number_of_process);
            } else {
                std::cerr << "Error in pstree. " << __FILE__ << ":" << __LINE__  << std::endl;
            }
        } catch (const std::runtime_error &e)
        {
            std::cerr << "Error in popen. " << __FILE__ << ":" << __LINE__  << std::endl;
        } catch (...)
        {
            std::cerr << "Error in stoi. (parsing value "<< number_of_process << ")." << __FILE__ << ":" << __LINE__  << std::endl;
        }

        cached_cpu.store(cpu, std::memory_order_relaxed);
        cached_memory_kb.store(memory_kb, std::memory_order_relaxed);
        cached_nprocs.store(nprocs, std::memory_order_relaxed);
        metrics_in_flight.store(false, std::memory_order_release);
    }

    void AgentInfoAPI::create_or_update_agent()
    {
        // Collect expensive process metrics (top/pstree) off the heartbeat thread
        // at a slow cadence. The first beat seeds the values; subsequent beats just
        // read the cache, so the heartbeat thread never blocks on popen/fork.
        if ((heartbeat_count++ % metrics_every_n) == 0)
        {
            bool expected = false;
            if (metrics_in_flight.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                std::thread([this]{ refresh_process_metrics(); }).detach();
        }

        const float   cpu       = cached_cpu.load(std::memory_order_relaxed);
        const int64_t memory_kb = cached_memory_kb.load(std::memory_order_relaxed);
        const int     nprocs    = cached_nprocs.load(std::memory_order_relaxed);

        auto str = G->get_agent_name() + " " + std::to_string(G->get_agent_id());

        if (auto node = G->get_node(str))
        {

            uint64_t times = get_unix_timestamp();
            auto &node_ref = node.value();
            // Keep the PID fresh across restarts: the node can outlive the process (persisted graph),
            // so a re-launched agent finds its old node here and must overwrite the stale pid. Only
            // written when it actually changed, to avoid needless 1 Hz attribute churn/signals.
            const auto cur_pid = static_cast<std::uint32_t>(getpid());
            if (const auto p = G->get_attrib_by_name<agent_pid_att>(node_ref); not p.has_value() or p.value() != cur_pid)
                G->add_or_modify_attrib_local<agent_pid_att>(node_ref, cur_pid);
            G->add_or_modify_attrib_local<timestamp_agent_att>(node_ref, times);
            G->add_or_modify_attrib_local<timestamp_alivetime_att>(node_ref,
               static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::nanoseconds(times - timestamp_start)).count()));
            if (cpu >= 0.0)
            {
                G->add_or_modify_attrib_local<cpu_usage_att>(node_ref, cpu);
            }
            //memory usage
            if (memory_kb >= 0)
            {
                G->add_or_modify_attrib_local<memory_usage_att>(node_ref, static_cast<uint32_t>(memory_kb));
            }
            //num_threads
            if (nprocs >= 0)
            {
                G->add_or_modify_attrib_local<num_procs_att>(node_ref, static_cast<uint32_t>(nprocs));
            }

            G->update_node(node_ref);
        } else
        {
            // edge
            std::uint64_t parent_id;
            if(auto parent = G->get_node("mind"); parent.has_value())
                parent_id = parent.value().id();
            else parent_id = G->get_node_root().value().id();

            // node
            DSR::Node new_node = Node::create<agent_node_type> ({}, {}, str);
            timestamp_start = get_unix_timestamp();
            G->add_or_modify_attrib_local<timestamp_agent_att>(new_node, timestamp_start);
            G->add_or_modify_attrib_local<timestamp_creation_att>(new_node, timestamp_start);
            G->add_or_modify_attrib_local<timestamp_alivetime_att>(new_node, static_cast<uint64_t>(0));
            G->add_or_modify_attrib_local<agent_id_att>(new_node, static_cast<uint32_t>(G->get_agent_id()));
            G->add_or_modify_attrib_local<agent_name_att>(new_node, str);
            G->add_or_modify_attrib_local<pos_x_att>(new_node, (float) 10);
            G->add_or_modify_attrib_local<pos_y_att>(new_node, (float) 10);
            G->add_or_modify_attrib_local<parent_att>(new_node, parent_id);

            // Deployment self-report (once, at creation): launch command, working dir and the raw
            // etc/config used to start this agent. Feeds the "mind" node network view. Best-effort:
            // any field that can't be resolved is simply left empty.
            const auto argv       = proc_argv();
            const auto cwd        = proc_cwd();
            const auto cfg_path   = derive_config_path(argv, cwd);
            std::string cmd_line;
            for (const auto& a : argv) { if (!cmd_line.empty()) cmd_line += ' '; cmd_line += a; }
            G->add_or_modify_attrib_local<agent_cmd_att>(new_node, cmd_line);
            G->add_or_modify_attrib_local<agent_cwd_att>(new_node, cwd);
            G->add_or_modify_attrib_local<agent_config_att>(new_node, slurp_file(cfg_path));
            G->add_or_modify_attrib_local<agent_pid_att>(new_node, static_cast<std::uint32_t>(getpid()));
            const std::string desc = cfg_path.empty() ? G->get_agent_name()
                                                      : G->get_agent_name() + " (" + cfg_path + ")";
            G->add_or_modify_attrib_local<agent_description_att>(new_node, desc);
            //CPU usage
            if (cpu >= 0.0)
            {
                G->add_or_modify_attrib_local<cpu_usage_att>(new_node, cpu);
            }
            //memory usage
            if (memory_kb >= 0)
            {
                G->add_or_modify_attrib_local<memory_usage_att>(new_node, static_cast<uint32_t>(memory_kb));
            }
            //num_threads
            if (nprocs >= 0)
            {
                G->add_or_modify_attrib_local<num_procs_att>(new_node, static_cast<uint32_t>(nprocs));
            }
            G->insert_node(new_node);
            DSR::Edge edge = DSR::Edge::create<has_edge_type>(parent_id, new_node.id());
            G->insert_or_assign_edge(edge);
        }
    }
}

