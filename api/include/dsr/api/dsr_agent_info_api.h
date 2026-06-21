//
// Created by juancarlos on 6/5/21.
//

#ifndef DSR_AGENTINFO_API_H
#define DSR_AGENTINFO_API_H

#include <thread>
#include <dsr/core/types/type_checking/dsr_node_type.h>
#include <dsr/core/types/type_checking/dsr_attr_name.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <QDebug>

namespace DSR {

    class DSRGraph;

    class Timer {

        std::function<void()> Fn;
        std::thread thr;
        std::atomic_bool done{ false };
        std::atomic_int period_ms;

    public:
        Timer(int period_ms_, std::function<void()> fn) :  Fn(std::move(fn)), period_ms(period_ms_)
        {

            thr = std::thread([this](){
                auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()); //Time point before calling Fn.
                while (!done.load(std::memory_order_acquire)) {
                    auto period_iter = std::chrono::microseconds {period_ms.load(std::memory_order_acquire) * 1000};
                    Fn();
                    auto end_call =  std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()); //Time point after calling Fn.
                    std::chrono::microseconds wait_time = (end_call - now);
                    if (auto t = period_iter - wait_time; t.count() > 0 )
                    {
                        qDebug() << "[TIMER - DEBUG] Execution time was: "<<  static_cast<double>(wait_time.count())/1000
                               << "ms. Sleeping " << t.count()/1000
                               << "ms. Total: " << static_cast<double>(wait_time.count())/1000+ static_cast<double>(t.count())/1000;
                        std::this_thread::sleep_for(t);   // t is already microseconds; the old duration_cast<ms>(t/1000) slept ~0 and busy-spun
                    } else {
                        qWarning() << "[TIMER] Execution time it's longer than period.";
                    }
                    now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch());;
                }
            });
        }

        ~Timer() {
            stop_timer();
        }

        void stop_timer()
        {
            bool f = false;
            while (!done.compare_exchange_strong(f, true, std::memory_order_acq_rel)){}
            if (thr.joinable()) thr.join();
            qDebug("Timer Stopped");
        }

        void period(int period)
        {
            period_ms.store(period, std::memory_order_acq_rel);
        }

        bool is_running()
        {
            return !thr.joinable();
        }

    };


    class AgentInfoAPI {

    public:
        explicit AgentInfoAPI(DSR::DSRGraph *g, uint32_t period_ = 1000)
        : G(g), period(period_), timer(period, [this]() { heartbeat_tick();})
        {}

        void stopTimer();
        bool isRunning();
        //void setPriod(uint32_t period_);

    private:

        static std::string exec(const char* cmd);
        // Runs on the raw Timer std::thread. Marshals the actual agent-node update to the DSRGraph's
        // (main) thread so create_or_update_agent()'s graph write + Qt-signal emit NEVER happen on
        // this non-GUI thread. Emitting DSR Qt signals (update_node_signal, ... carrying std::string/
        // vector<string> payloads) from a worker thread races the main-thread slots' QMetaType arg
        // marshaling (TSan-confirmed); the heap corruption is fatal in heavy-heap consumers
        // (voxelizer) under participant-join churn. Defined in the .cpp where DSRGraph is complete.
        void heartbeat_tick();
        void create_or_update_agent();
        void refresh_process_metrics();

        DSRGraph *G;
        uint64_t timestamp_start{0};
        uint32_t period;

        // Process metrics (top/pstree) are expensive to collect, so they are
        // refreshed on a short-lived background thread at a slow cadence and
        // cached here. Every heartbeat just reads these atomics, so the heartbeat
        // thread never blocks on popen/fork and keeps its period.
        std::atomic<float>   cached_cpu{-1.0f};
        std::atomic<int64_t> cached_memory_kb{-1};
        std::atomic<int>     cached_nprocs{-1};
        std::atomic_bool     metrics_in_flight{false};
        uint32_t heartbeat_count{0};
        uint32_t metrics_every_n{8};

        Timer timer;
    };

}

#endif //DSR_AGENTINFO_API_H
