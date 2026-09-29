#include "hardware.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <driver/usb_serial_jtag.h>
#include <esp_sleep.h>
#include <sys/time.h>

#include "config.h"

namespace {

constexpr uint8_t SHTC3_ADDR = 0x70;
constexpr uint8_t PCF85063_ADDR = 0x51;
constexpr uint8_t PCF85063_REG_SECONDS = 0x04;

bool shtc3Command(uint16_t command) {
  Wire.beginTransmission(SHTC3_ADDR);
  Wire.write(command >> 8);
  Wire.write(command & 0xFF);
  return Wire.endTransmission() == 0;
}

uint8_t toBcd(int value) { return ((value / 10) << 4) | (value % 10); }
int fromBcd(uint8_t value) { return (value >> 4) * 10 + (value & 0x0F); }

// Dager siden 1970-01-01 for en dato (Howard Hinnants days_from_civil).
int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + doe - 719468;
}

}  // namespace

void hardwareBegin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  // After a KEY wake, the pin is still in RTC mode.
  rtc_gpio_deinit((gpio_num_t)PIN_KEY);
  pinMode(PIN_KEY, INPUT_PULLUP);
}

Climate readClimate() {
  Climate climate = {};
  if (!shtc3Command(0x3517)) return climate;  // Wakeup
  delay(1);
  if (!shtc3Command(0x7866)) return climate;  // Measure, temperature first, without clock stretching
  delay(15);
  if (Wire.requestFrom(SHTC3_ADDR, (uint8_t)6) != 6) return climate;

  uint8_t b[6];
  for (uint8_t &byte : b) byte = Wire.read();
  shtc3Command(0xB098);  // Sleep

  climate.temperature = -45.0f + 175.0f * ((b[0] << 8) | b[1]) / 65536.0f;
  climate.humidity = 100.0f * ((b[3] << 8) | b[4]) / 65536.0f;
  climate.valid = true;
  return climate;
}

float readBatteryVolts() {
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(PIN_BATTERY);
  return sum / 8 / 1000.0f * 3.0f;
}

int batteryPercent(float volts) {
  float percent = (volts - BATTERY_EMPTY_VOLTS) / (BATTERY_FULL_VOLTS - BATTERY_EMPTY_VOLTS) * 100.0f;
  int rounded = constrain((int)lroundf(percent), 0, 100);
  // The divider and ADC's tolerances mean a resting battery rarely reads exactly at either
  // calibration voltage, so a strict linear mapping would almost never show 0 % or 100 % even
  // when the battery is, for all practical purposes, empty or full.
  if (rounded >= 97) return 100;
  if (rounded <= 3) return 0;
  return rounded;
}

bool loadTimeFromRtc() {
  Wire.beginTransmission(PCF85063_ADDR);
  Wire.write(PCF85063_REG_SECONDS);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(PCF85063_ADDR, (uint8_t)7) != 7) return false;

  uint8_t r[7];
  for (uint8_t &byte : r) byte = Wire.read();
  if (r[0] & 0x80) return false;  // OS flag: the clock has stopped, so the time is invalid.

  int64_t days = daysFromCivil(2000 + fromBcd(r[6]), fromBcd(r[5] & 0x1F), fromBcd(r[3] & 0x3F));
  time_t utc = days * 86400 + fromBcd(r[2] & 0x3F) * 3600 + fromBcd(r[1] & 0x7F) * 60 +
               fromBcd(r[0] & 0x7F);

  timeval tv = {utc, 0};
  settimeofday(&tv, nullptr);
  return true;
}

void setClock(time_t utc) {
  timeval tv = {utc, 0};
  settimeofday(&tv, nullptr);

  tm t;
  gmtime_r(&utc, &t);
  Wire.beginTransmission(PCF85063_ADDR);
  Wire.write(PCF85063_REG_SECONDS);
  Wire.write(toBcd(t.tm_sec));  // Skriving av sekunder nullstiller OS-flagget.
  Wire.write(toBcd(t.tm_min));
  Wire.write(toBcd(t.tm_hour));
  Wire.write(toBcd(t.tm_mday));
  Wire.write(t.tm_wday);
  Wire.write(toBcd(t.tm_mon + 1));
  Wire.write(toBcd(t.tm_year - 100));
  Wire.endTransmission();
}

bool timeIsValid() { return time(nullptr) > 1700000000; }

void waitForKeyRelease() {
  uint32_t start = millis();
  while (digitalRead(PIN_KEY) == LOW && millis() - start < 5000) delay(10);
  delay(30);  // Debounce, or releasing the button would wake the board again.
}

bool waitForKey(uint32_t seconds) {
  uint32_t start = millis();
  while (millis() - start < seconds * 1000) {
    if (digitalRead(PIN_KEY) == LOW) return true;
    delay(20);
  }
  return false;
}

bool usbConnected() {
  // The PC sends SOF packets every millisecond, but needs a little time after startup.
  while (millis() < 500) {
    if (usb_serial_jtag_is_connected()) return true;
    delay(10);
  }
  return usb_serial_jtag_is_connected();
}

void deepSleep(uint32_t seconds) {
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_KEY, 0);
  rtc_gpio_pullup_en((gpio_num_t)PIN_KEY);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_KEY);
  esp_deep_sleep_start();
}
