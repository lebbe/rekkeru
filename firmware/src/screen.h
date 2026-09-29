#pragma once

#include "data.h"

// coldBoot: fully initialize the display (causes a flash). Otherwise reuse its deep-sleep state.
void screenBegin(bool coldBoot);
// Hold the display pins so the image remains unchanged while the ESP32 is in deep sleep.
void screenSleep();

void drawLocalScreen(const Climate &climate, float batteryVolts, bool usbConnected);
void drawNetScreen(const NetData &data, const char *error, bool usbConnected);
// Could not contact the server at all (Wi-Fi, network, timeout, or invalid response).
void drawServerDown(const char *detail);
// The server responds, but all of its services are unavailable.
void drawTeapot();
void drawMessage(const char *title, const char *detail);
