#pragma once

#include <stdint.h>

// Pins from Waveshare's RLCD guide and the ESPHome configuration for the board.
constexpr int PIN_LCD_SCK = 11;
constexpr int PIN_LCD_MOSI = 12;
constexpr int PIN_LCD_CS = 40;
constexpr int PIN_LCD_DC = 5;
constexpr int PIN_LCD_RST = 41;

constexpr int PIN_I2C_SDA = 13;
constexpr int PIN_I2C_SCL = 14;

constexpr int PIN_KEY = 18;     // Active low, RTC GPIO, wakes from deep sleep.
constexpr int PIN_BATTERY = 4;  // ADC, 3x spenningsdeler.

// A single-cell Li-ion/LiPo is not usable down to 2.5 V; the board browns out long before that,
// so treat a more realistic no-load cutoff as empty (see docs/waveshare-esp32-s3-rlcd-42-battery).
constexpr float BATTERY_EMPTY_VOLTS = 3.3f;
constexpr float BATTERY_FULL_VOLTS = 4.2f;

constexpr const char *TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";

// How long the network screen stays visible before returning to the local screen.
constexpr uint32_t NET_SCREEN_SECONDS = 5 * 60;
// Refresh every REFRESH_INTERVAL_SECONDS during the first AUTO_REFRESH_SECONDS. 0 disables it.
constexpr uint32_t AUTO_REFRESH_SECONDS = 2 * 60;
constexpr uint32_t REFRESH_INTERVAL_SECONDS = 30;

constexpr uint32_t WIFI_TIMEOUT_MS = 10000;
constexpr uint32_t HTTP_TIMEOUT_MS = 5000;
