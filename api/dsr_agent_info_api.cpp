//
// Created by juancarlos on 6/5/21.
//

#include "dsr/core/types/type_checking/dsr_edge_type.h"
#include <dsr/api/dsr_agent_info_api.h>
#include <dsr/api/dsr_api.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <QMetaObject>

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
            G->add_or_modify_attrib_local<agent_description_att>(new_node, std::string{"TODO"});
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

