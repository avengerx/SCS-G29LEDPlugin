#ifndef __TRUCK_H_INCLUDED__
#define __TRUCK_H_INCLUDED__
#include <mutex>

extern std::mutex truck_data_access;

// The game should keep updating this structure.
// We may create a separate thread loop to poll this and, when
// it detect changes, update the LEDs accordingly.
struct truck_info_t {
    bool paused; // if the game is paused, in menu, etc
    bool electricity;
    float fuel_max;
    float fuel;
};

extern truck_info_t truck_data;

HRESULT InitTruckData();

#endif