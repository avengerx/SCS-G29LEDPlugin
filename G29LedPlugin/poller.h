#ifndef __POLLER_H_INCLUDED__
#define __POLLER_H_INCLUDED__
#include "pch.h"

extern bool concurrent_thread_running;

HRESULT StartPolling();
HRESULT StopPolling();

#endif