//
// Created by jc on 9/07/22.
//

#ifndef DSR_DSR_SIGNAL_INFO_H
#define DSR_DSR_SIGNAL_INFO_H

#include <cstdint>
#include <QtCore>

namespace DSR{
    struct SignalInfo
    {
        uint32_t agent_id; //change agent_id
        //uint64_t send_timestamp; //send timestamp from the middleware
        //uint64_t recv_timestamp; //recv timestamp from the middleware
    };
}

Q_DECLARE_METATYPE(DSR::SignalInfo)

#endif //DSR_DSR_SIGNAL_INFO_H
