#pragma once

#include "dsr/core/types/internal_types.h"
#include <cstdint>
#include <string>
#include <dsr/api/dsr_signal_emitter.h>

namespace DSR {

struct GraphSettings {
    uint32_t agent_id {0};
    int theradpool_threads {5};
    int attribute_threadpool_threads {1};
    std::string graph_name {""};
    std::string input_file {""};
    std::string dds_configuration_file {""};
    bool same_host {false};
    enum struct LOGLEVEL: uint8_t {
        DEBUGL = 0, INFOL, WARNINGL, ERRORL
    } log_level {LOGLEVEL::INFOL};
    int8_t domain_id = 0;
    SignalMode signal_mode = QT;
    SyncMode sync_mode = SyncMode::CRDT;
};

}
