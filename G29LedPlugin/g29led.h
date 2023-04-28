#ifndef __G29LED_H_INCLUDED__
#define __G29LED_H_INCLUDED__
#include "pch.h"
#include "log.h"

HRESULT LoadController();
HRESULT UnloadController();
HRESULT ClearLEDs();
HRESULT UpdateFuelLevel();
HRESULT InitFuelGaugeAnimation();
HRESULT ShutdownFuelGaugeAnimation();

#endif