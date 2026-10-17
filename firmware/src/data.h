#pragma once

#include <stdint.h>

// The server sends everything for the rest of the day in one fetch, so these are generous.
constexpr int MAX_DEPARTURES = 128;
constexpr int MAX_CALENDAR = 10;
constexpr int MAX_FORECAST = 24;

struct Departure {
  char line[4];
  uint32_t time;  // Unix epoch, UTC
};

struct CalendarEvent {
  uint32_t start;  // Unix epoch, UTC
  uint32_t end;
  bool allDay;
  char title[48];
};

// Weather for one hour. The server describes each of the next 24 hours, so the board can show the
// right one throughout the day without fetching again.
struct WeatherHour {
  uint32_t time;  // Unix epoch, UTC
  int8_t temp;
  char icon[14];
  char text[24];
};

// Most recent valid server response. Stored in RTC memory and retained during deep sleep.
struct NetData {
  bool valid;
  uint32_t fetchedAt;
  Departure departures[MAX_DEPARTURES];
  uint16_t departureCount;
  CalendarEvent calendar[MAX_CALENDAR];
  uint8_t calendarCount;
  WeatherHour forecast[MAX_FORECAST];
  uint8_t forecastCount;
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
