// Departure display. The board deep-sleeps between steps; wake() runs after each wake-up:
//   Timer, local screen:  read sensors, draw the clock and indoor climate, sleep until the next minute.
//   KEY, local screen:    show the loading screen, fetch from the server, draw the network screen.
//   Timer, network screen: refresh during the first few minutes, then update the countdown without Wi-Fi.
//                         Return to the local screen after NET_SCREEN_SECONDS.
//   KEY, network screen: return to the local screen.
//
// When connected to a PC, the board stays awake and waits instead of sleeping. Deep sleep disconnects
// USB, which would otherwise make the PC beep every minute and prevent firmware uploads.

#include <Arduino.h>
#include <esp_sleep.h>
#include <time.h>

#include "config.h"
#include "data.h"
#include "hardware.h"
#include "net.h"
#include "screen.h"

enum class Mode : uint8_t { Local, Net };

// RTC memory survives deep sleep.
RTC_DATA_ATTR Mode mode = Mode::Local;
RTC_DATA_ATTR time_t netShownSince;
RTC_DATA_ATTR NetData netData;
RTC_DATA_ATTR char netError[48];

Climate climate;
float batteryVolts;

void showLocal() {
  mode = Mode::Local;
  drawLocalScreen(climate, batteryVolts);
}

void showNet() {
  if (!netData.valid) drawServerDown(netError);
  else if (netData.departuresError[0] && netData.weatherError[0] && netData.calendarError[0]) drawTeapot();
  else drawNetScreen(netData, netError);  // Each unavailable service gets its own warning line.
}

void fetchAndShowNet() {
  if (fetchScreen(netData, netError, sizeof(netError))) {
    netError[0] = '\0';
    setClock(netData.fetchedAt);
  }
  showNet();
}

void onKeyPressed() {
  if (mode == Mode::Net) {
    showLocal();
  } else {
    drawMessage("Henter…", "");
    mode = Mode::Net;
    // Do not show stale data from a previous session. If fetching fails, show the error screen.
    // Automatic refreshes keep the existing data and show the error in the footer.
    netData.valid = false;
    fetchAndShowNet();
    netShownSince = time(nullptr);  // Set after fetching, since the clock may have been adjusted.
  }
  waitForKeyRelease();
}

void onNetTimer() {
  time_t elapsed = time(nullptr) - netShownSince;
  if (elapsed >= NET_SCREEN_SECONDS) showLocal();
  else if (elapsed < AUTO_REFRESH_SECONDS) fetchAndShowNet();
  else showNet();
}

uint32_t secondsToSleep() {
  time_t now = time(nullptr);
  if (mode == Mode::Net && now - netShownSince < AUTO_REFRESH_SECONDS) return REFRESH_INTERVAL_SECONDS;
  if (!timeIsValid()) return 60;
  return 60 - now % 60 + 1;  // Add one second so we do not wake just before the minute changes.
}

void wake(esp_sleep_wakeup_cause_t cause) {
  // Read the sensor first, before Wi-Fi heats up the board.
  climate = readClimate();
  batteryVolts = readBatteryVolts();
  loadTimeFromRtc();

  if (cause == ESP_SLEEP_WAKEUP_EXT0) onKeyPressed();
  else if (cause == ESP_SLEEP_WAKEUP_TIMER && mode == Mode::Net) onNetTimer();
  else showLocal();
}

void setup() {
  hardwareBegin();
  setenv("TZ", TIMEZONE, 1);
  tzset();

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  screenBegin(cause == ESP_SLEEP_WAKEUP_UNDEFINED);
  wake(cause);
}

void loop() {
  uint32_t seconds = secondsToSleep();
  if (!usbConnected()) {
    screenSleep();
    deepSleep(seconds);  // Never returns. The next wake-up starts in setup().
  }
  wake(waitForKey(seconds) ? ESP_SLEEP_WAKEUP_EXT0 : ESP_SLEEP_WAKEUP_TIMER);
}
