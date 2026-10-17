// Departure display. The board deep-sleeps between steps; wake() runs after each wake-up and draws the
// one main screen (clock, departures, calendar, weather, indoor climate), then sleeps until the next minute.
//   Fetch:  departures, weather, calendar and the clock are fetched over Wi-Fi once a day at FETCH_HOUR, on first
//           boot, and whenever BOOT is pressed. Between fetches the countdowns are calculated from the
//           stored departure times, so no Wi-Fi is needed.
//   KEY:    open the voice screen (see voice.cpp). KEY, or a few minutes without conversation, returns.
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
#include "voice.h"

// RTC memory survives deep sleep.
RTC_DATA_ATTR time_t nextFetch;  // 0 = fetch on the next wake.
RTC_DATA_ATTR NetData netData;
RTC_DATA_ATTR char netError[48];

Climate climate;
float batteryVolts;
bool usbActive = false;

// Read the sensors before Wi-Fi heats up the board.
void readSensors() {
  climate = readClimate();
  batteryVolts = readBatteryVolts();
}

void showMain(bool loading = false) {
  drawMainScreen(netData, netError, climate, batteryVolts, usbActive, loading);
}

// The next FETCH_HOUR:00 local time after now.
time_t nextFetchTime(time_t now) {
  tm t;
  localtime_r(&now, &t);
  t.tm_hour = FETCH_HOUR;
  t.tm_min = t.tm_sec = 0;
  t.tm_isdst = -1;
  time_t at = mktime(&t);
  if (at <= now) {
    t.tm_mday++;
    t.tm_isdst = -1;
    at = mktime(&t);
  }
  return at;
}

void fetchAndShow() {
  showMain(true);
  uint32_t started = millis();
  bool ok = fetchScreen(netData, netError, sizeof(netError));
  Serial.printf("[fetch] %s in %u ms: %u departures, %u forecast hours, %u events; errors dep='%s' weather='%s' cal='%s'; net='%s'\n",
                ok ? "ok" : "FAILED", millis() - started, netData.departureCount, netData.forecastCount,
                netData.calendarCount, netData.departuresError, netData.weatherError, netData.calendarError, netError);
  if (ok) {
    netError[0] = '\0';
    setClock(netData.fetchedAt);
  }
  time_t now = time(nullptr);
  bool complete = ok && !netData.departuresError[0] && !netData.weatherError[0] && !netData.calendarError[0];
  if (complete) nextFetch = nextFetchTime(now);
  else nextFetch = timeIsValid() ? now + FETCH_RETRY_SECONDS : 0;
  Serial.printf("[fetch] next fetch at %lld (in %lld s), clock valid=%d\n", (long long)nextFetch,
                (long long)(nextFetch - now), timeIsValid());
  showMain();
}

bool fetchDue() { return nextFetch == 0 || (timeIsValid() && time(nullptr) >= nextFetch); }

uint32_t secondsToSleep() {
  if (!timeIsValid()) return 60;
  return 60 - time(nullptr) % 60 + 1;  // Add one second so we do not wake just before the minute changes.
}

void wake(Press press) {
  bool clockOk = loadTimeFromRtc();
  Serial.printf("[wake] press=%d usb=%d rtc clock ok=%d time=%lld nextFetch=%lld fetchDue=%d\n", (int)press,
                usbActive, clockOk, (long long)time(nullptr), (long long)nextFetch, fetchDue());

  if (press == Press::Key) {
    waitForKeyRelease();
    runVoice();  // Blocks until the voice screen is closed.
    readSensors();
    showMain();
    waitForKeyRelease();
    return;
  }

  readSensors();
  Serial.printf("[sensors] %.1f C, %.0f %%, battery %.2f V (%d %%)\n", climate.temperature, climate.humidity,
                batteryVolts, batteryPercent(batteryVolts));
  if (press == Press::Boot || fetchDue()) fetchAndShow();
  else showMain();
  if (press == Press::Boot) waitForKeyRelease();
}

void setup() {
  Serial.begin(115200);  // USB log; only readable while connected to a PC.
  hardwareBegin();
  setenv("TZ", TIMEZONE, 1);
  tzset();

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  Serial.printf("\n[boot] wakeup cause=%d (0=cold boot, 4=timer, 8=ext1)\n", (int)cause);
  screenBegin(cause == ESP_SLEEP_WAKEUP_UNDEFINED);
  wake(wakePress());
}

void loop() {
  uint32_t seconds = secondsToSleep();
  usbActive = usbConnected();
  Serial.printf("[loop] usb=%d, sleeping %u s\n", usbActive, seconds);
  if (!usbActive) {
    screenSleep();
    deepSleep(seconds);  // Never returns. The next wake-up starts in setup().
  }
  wake(waitForPress(seconds));
}
