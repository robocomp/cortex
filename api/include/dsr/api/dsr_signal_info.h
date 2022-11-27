
#pragma once 

#include <cstdint>
#include <QtCore/QMetaType>

namespace DSR {
    
    struct SignalInfo
    {
        uint32_t agent_id; //change agent_id
        //uint64_t send_timestamp; //send timestamp from the middleware
        //uint64_t recv_timestamp; //recv timestamp from the middleware
    };

    inline constexpr SignalInfo default_signal_info {};

}

Q_DECLARE_METATYPE(DSR::SignalInfo);