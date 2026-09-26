#pragma once

#include <time.h>

#include "data.h"

void hardwareBegin();

Climate readClimate();
float readBatteryVolts();
int batteryPercent(float volts);

// Clock: PCF85063 keeps UTC; the system clock is used for the rest of the time.
bool loadTimeFromRtc();
void setClock(time_t utc);
bool timeIsValid();

void waitForKeyRelease();
// Waits while awake. Returns true if KEY was pressed, false when the timeout expires.
bool waitForKey(uint32_t seconds);

// True if a PC is connected over USB (not just a charger).
bool usbConnected();
void deepSleep(uint32_t seconds);
