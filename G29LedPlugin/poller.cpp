#include "pch.h"
#include "log.h"
#include "poller.h"
#include "truck.h"
#include "g29led.h"

#define LOCK truck_data_access.lock();
#define UNLOCK truck_data_access.unlock();

#define POLL_INTERVAL 10

#define WAITPOLL Sleep(POLL_INTERVAL);
#define WAITNEXT WAITPOLL continue;

bool concurrent_thread_running = false;
bool polling = false;
std::mutex threadlock;

void Poll();

HRESULT StartPolling() {
    polling = true;
    log("Polling for truck state changes...");
    new std::thread(Poll);
    concurrent_thread_running = true;
    return S_OK;
}

HRESULT StopPolling() {
    polling = false;
    threadlock.lock();
    log("Stopped polling for truck state changes.");
    // don't just set it to false, wait the thread to exit!
    concurrent_thread_running = false;
    threadlock.unlock();
    return S_OK;
}

void Poll() {
    threadlock.lock();
    truck_info_t last, current;
    memset(&last, 0, sizeof(current));

    bool shut_leds = false;
    bool start_leds = false;
    bool status_failed = false;
#define UpdateFuelCHK() status_failed = UpdateFuelLevel() != S_OK
    log("Thread started polling.");
    while (polling) {
        LOCK;
        if (truck_data.paused) {
            UNLOCK;
            if (!last.paused) {
                log("Paused.");
                // stop all effects, but be ready to resume where they were once it is unpaused.
                last.paused = true;
            }
            WAITNEXT;
        } else if (last.paused) {
            log("Unpaused. Resuming fuel gauge updates.");
            last = truck_data;
            UNLOCK;
            if (last.electricity) UpdateFuelCHK();
            else ClearLEDs();
            WAITNEXT;
        }

        if (truck_data.electricity != last.electricity) {
            if (!truck_data.electricity) {
                // Truck turned off. Turn all LEDs off.
                shut_leds = true;
            } else {
                start_leds = true;
            }
            last = truck_data;
        }
        current = truck_data;
        UNLOCK;

        if (shut_leds) {
            ShutdownFuelGaugeAnimation();
            shut_leds = false;
        } else if (start_leds) {
            InitFuelGaugeAnimation();
            UpdateFuelCHK();
            start_leds = false;
        } else if (current.fuel < (last.fuel - 0.1) || current.fuel > (last.fuel + 0.1) || current.fuel_max != last.fuel_max) {
            UpdateFuelCHK();
            last = current;
        }

        if (status_failed) {
            log("Failed updating LED status.");
            Sleep(1000);
        }

        WAITPOLL;
    }
    ClearLEDs();
    log("Thread stopped polling.");
    threadlock.unlock();
}