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

enum class Press : uint8_t { None, Key, Boot };

void waitForKeyRelease();  // Waits until both KEY and BOOT are released.
// Waits while awake. Returns the button that was pressed, or None when the timeout expires.
Press waitForPress(uint32_t seconds);
// The button that woke the board from deep sleep, or None for any other wake-up.
Press wakePress();

// True if a PC is connected over USB (not just a charger).
bool usbConnected();
void deepSleep(uint32_t seconds);
