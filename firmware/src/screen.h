#pragma once

#include <U8g2lib.h>

#include "data.h"

// coldBoot: fully initialize the display (causes a flash). Otherwise reuse its deep-sleep state.
void screenBegin(bool coldBoot);
// The display, for drawing that lives outside screen.cpp (face.cpp).
U8G2 &display();
// Fast SPI and the panel's high power mode (32 Hz instead of 1 Hz), for animation.
void screenAnimate(bool on);
// Hold the display pins so the image remains unchanged while the ESP32 is in deep sleep.
void screenSleep();

void drawLocalScreen(const Climate &climate, float batteryVolts, bool usbConnected);
void drawNetScreen(const NetData &data, const char *error, bool usbConnected);
// Could not contact the server at all (Wi-Fi, network, timeout, or invalid response).
void drawServerDown(const char *detail);
// The server responds, but all of its services are unavailable.
void drawTeapot();
void drawMessage(const char *title, const char *detail);
