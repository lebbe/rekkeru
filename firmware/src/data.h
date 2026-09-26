#pragma once

#include <stdint.h>

constexpr int MAX_DEPARTURES = 5;
constexpr int MAX_CALENDAR = 4;

struct Departure {
  char line[8];
  char dest[40];
  uint32_t time;  // Unix epoch, UTC
};

// Most recent valid server response. Stored in RTC memory and retained during deep sleep.
struct NetData {
  bool valid;
  uint32_t fetchedAt;
  Departure departures[MAX_DEPARTURES];
  uint8_t departureCount;
  bool hasWeather;
  int16_t temp;
  char icon[16];
  char weatherText[48];
  char calendar[MAX_CALENDAR][64];
  uint8_t calendarCount;
  // Server error message when a service is unavailable; empty otherwise.
  char departuresError[48];
  char weatherError[48];
  char calendarError[48];
};

struct Climate {
  bool valid;
  float temperature;
  float humidity;
};
