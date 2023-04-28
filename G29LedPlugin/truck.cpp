#include "pch.h"
#include "log.h"
#include "poller.h"
#include "truck.h"

std::mutex truck_data_access;
truck_info_t truck_data;

HRESULT InitTruckData() {
    if (concurrent_thread_running) {
        log("Too late to initialize truck data: concurrent thread is already running.");
        return RPC_E_TOO_LATE;
    }

    memset(&truck_data, 0, sizeof(truck_data));

    // Initially, the game is paused.
    truck_data.paused = true;

    return S_OK;
}