#include "screen.h"

#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <time.h>

#include "config.h"
#include "hardware.h"

namespace {

// U8G2_R1 uses landscape orientation, 400x300.
U8G2_ST7305_300X400_F_4W_HW_SPI u8g2(U8G2_R1, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);

constexpr int WIDTH = 400;

const char *const WEEKDAYS[] = {"søndag", "mandag", "tirsdag", "onsdag", "torsdag", "fredag", "lørdag"};
const char *const MONTHS[] = {"januar", "februar", "mars",     "april",   "mai",      "juni",
                              "juli",   "august",  "september", "oktober", "november", "desember"};

const char *const SHORT_WEEKDAYS[] = {"søn", "man", "tir", "ons", "tor", "fre", "lør"};

void drawCentered(const char *text, int y) {
  u8g2.drawUTF8((WIDTH - u8g2.getUTF8Width(text)) / 2, y, text);
}

void drawRight(const char *text, int right, int y) {
  u8g2.drawUTF8(right - u8g2.getUTF8Width(text), y, text);
}

void formatClock(time_t t, char *out, size_t size) {
  if (t < 1700000000) {
    snprintf(out, size, "--:--");
    return;
  }
  tm local;
  localtime_r(&t, &local);
  strftime(out, size, "%H:%M", &local);
}

// Convert decimal points to commas: 22.5 -> 22,5.
void decimalComma(char *text) {
  for (char *c = text; *c; c++)
    if (*c == '.') *c = ',';
}

void drawBattery(float volts, int right, int top) {
  int percent = batteryPercent(volts);
  const int w = 34, h = 16;
  int x = right - w - 3;
  u8g2.drawFrame(x, top, w, h);
  u8g2.drawBox(x + w, top + 4, 3, h - 8);
  u8g2.drawBox(x + 2, top + 2, (w - 4) * percent / 100, h - 4);

  char text[8];
  snprintf(text, sizeof(text), "%d %%", percent);
  u8g2.setFont(u8g2_font_helvB14_tf);
  drawRight(text, x - 8, top + h - 1);
}

constexpr uint16_t WARNING_GLYPH = 71;  // Warning triangle in open_iconic_embedded.

// Draw a small warning triangle (16 px) before text, with baseline y.
void drawWarning(int x, int y, const char *text, const uint8_t *font) {
  u8g2.setFont(u8g2_font_open_iconic_embedded_2x_t);
  u8g2.drawGlyph(x, y + 1, WARNING_GLYPH);
  u8g2.setFont(font);
  u8g2.drawUTF8(x + 22, y, text);
}

// Draw the glyph with a white outline so it stands out from the content behind it.
void drawGlyphWithHalo(const uint8_t *font, uint16_t glyph, int x, int y, int halo) {
  u8g2.setFont(font);
  u8g2.setDrawColor(0);
  for (int dx = -halo; dx <= halo; dx++)
    for (int dy = -halo; dy <= halo; dy++) u8g2.drawGlyph(x + dx, y + dy, glyph);
  u8g2.setDrawColor(1);
  u8g2.drawGlyph(x, y, glyph);
}

void drawFlake(int cx, int cy) {
  u8g2.drawHLine(cx - 2, cy, 5);
  u8g2.drawVLine(cx, cy - 2, 5);
  u8g2.drawPixel(cx - 1, cy - 1);
  u8g2.drawPixel(cx + 1, cy - 1);
  u8g2.drawPixel(cx - 1, cy + 1);
  u8g2.drawPixel(cx + 1, cy + 1);
}

// Same shape as the raindrops in the rain icon.
void drawDrop(int cx, int cy) {
  u8g2.drawBox(cx - 1, cy - 3, 2, 1);
  u8g2.drawBox(cx - 2, cy - 2, 4, 6);
  u8g2.drawBox(cx - 1, cy + 4, 2, 1);
}

// Small inverted badge on the bottom row. "USB" is shown while a PC is connected over USB (see
// usbConnected() in hardware.cpp) so it is obvious why the board is not going to sleep.
void drawBadge(const char *label, int x) {
  u8g2.setFont(u8g2_font_helvB10_tf);
  const int boxW = u8g2.getUTF8Width(label) + 14, boxH = 16;
  const int y = 300 - boxH - 4;
  u8g2.drawRBox(x, y, boxW, boxH, 3);
  u8g2.setDrawColor(0);
  u8g2.drawUTF8(x + 7, y + boxH - 4, label);
  u8g2.setDrawColor(1);
}

void drawUsbIndicator() {
  u8g2.setFont(u8g2_font_helvB10_tf);
  drawBadge("USB", (WIDTH - (u8g2.getUTF8Width("USB") + 14)) / 2);
}

// Copy text into out, shortened with an ellipsis so it fits within maxWidth in the current font.
void fitText(const char *text, int maxWidth, char *out, size_t size) {
  strlcpy(out, text, size);
  if (u8g2.getUTF8Width(out) <= maxWidth) return;
  size_t len = strlen(out);
  while (len > 0) {
    len--;
    while (len > 0 && (out[len] & 0xC0) == 0x80) len--;  // Do not cut inside a UTF-8 character.
    out[len] = '\0';
    char candidate[64];
    snprintf(candidate, sizeof(candidate), "%s…", out);
    if (u8g2.getUTF8Width(candidate) <= maxWidth) {
      strlcpy(out, candidate, size);
      return;
    }
  }
}

// A warning icon with the message wrapped below it, for a service that is unavailable.
void drawWarningBlock(int x, int top, int maxWidth, const char *message) {
  u8g2.setFont(u8g2_font_open_iconic_embedded_2x_t);
  u8g2.drawGlyph(x, top + 17, WARNING_GLYPH);
  u8g2.setFont(u8g2_font_helvR12_tf);
  const char *rest = message;
  for (int row = 0; row < 3 && *rest; row++) {
    char line[48];
    size_t n = 0, lastSpace = 0;
    while (rest[n]) {
      if (rest[n] == ' ') lastSpace = n;
      strlcpy(line, rest, min(n + 2, sizeof(line)));
      if (u8g2.getUTF8Width(line) > maxWidth) break;
      n++;
    }
    if (rest[n] && lastSpace > 0) n = lastSpace;
    strlcpy(line, rest, min(n + 1, sizeof(line)));
    u8g2.drawUTF8(x, top + 40 + row * 16, line);
    rest += n;
    while (*rest == ' ') rest++;
  }
}

// 32x32 icon with its upper-left corner at (x, top). Glyph codes were checked against the font data;
// see docs/vaerikoner.md. Snow, sleet, fog, and thunder have no 32 px glyph, so they are drawn
// with the cloud (64) raised and details underneath.
void drawWeatherIcon(const char *icon, int x, int top) {
  const int baseline = top + 32;
  u8g2.setFont(u8g2_font_open_iconic_weather_4x_t);

  if (!strcmp(icon, "clear")) {
    u8g2.drawGlyph(x, baseline, 69);  // sun
  } else if (!strcmp(icon, "clear_night")) {
    u8g2.drawGlyph(x, baseline, 66);  // moon
  } else if (!strcmp(icon, "partly_night")) {
    u8g2.drawGlyph(x, baseline, 65);  // crescent behind cloud
  } else if (!strcmp(icon, "rain")) {
    u8g2.drawGlyph(x, baseline, 67);
  } else if (!strcmp(icon, "partly")) {
    u8g2.setFont(u8g2_font_open_iconic_weather_2x_t);
    u8g2.drawGlyph(x, top + 17, 69);  // small sun
    drawGlyphWithHalo(u8g2_font_open_iconic_weather_4x_t, 64, x + 2, top + 36, 2);
  } else if (!strcmp(icon, "snow")) {
    u8g2.drawGlyph(x, top + 28, 64);
    drawFlake(x + 5, top + 27);
    drawFlake(x + 15, top + 29);
    drawFlake(x + 25, top + 27);
  } else if (!strcmp(icon, "sleet")) {
    u8g2.drawGlyph(x, top + 28, 64);
    drawFlake(x + 6, top + 28);
    drawDrop(x + 15, top + 27);
    drawFlake(x + 24, top + 28);
  } else if (!strcmp(icon, "fog")) {
    u8g2.drawGlyph(x, top + 28, 64);
    u8g2.setDrawColor(0);
    u8g2.drawBox(x, top + 17, 32, 15);  // crop the cloud flat
    u8g2.setDrawColor(1);
    u8g2.drawBox(x + 1, top + 20, 30, 2);
    u8g2.drawBox(x + 4, top + 24, 26, 2);
    u8g2.drawBox(x + 1, top + 28, 22, 2);
  } else if (!strcmp(icon, "thunder")) {
    u8g2.drawGlyph(x, top + 28, 64);
    drawGlyphWithHalo(u8g2_font_open_iconic_embedded_2x_t, 67, x + 12, top + 33, 1);  // lightning
  } else {
    u8g2.drawGlyph(x, baseline, 64);  // cloudy and unknown values
  }
}

}  // namespace

void screenBegin(bool coldBoot) {
  // Set the pins high before releasing the deep-sleep hold, or RST may go low and reset the display.
  pinMode(PIN_LCD_CS, OUTPUT);
  digitalWrite(PIN_LCD_CS, HIGH);
  pinMode(PIN_LCD_RST, OUTPUT);
  digitalWrite(PIN_LCD_RST, HIGH);
  gpio_hold_dis((gpio_num_t)PIN_LCD_CS);
  gpio_hold_dis((gpio_num_t)PIN_LCD_RST);

  SPI.begin(PIN_LCD_SCK, -1, PIN_LCD_MOSI, PIN_LCD_CS);
  u8g2.setBusClock(1000000);
  if (coldBoot) {
    // Full init: reset, initialization sequence, and cleared RAM. This causes a flash, so only do it on boot.
    u8g2.beginSimple();
    u8g2.sendF("c", 0x39);  // ST7305 Low Power Mode ON; the image remains while the ESP32 sleeps.
  } else {
    // The display remained active while the ESP32 slept. Only reconfigure SPI and the pins.
    u8g2.initInterface();
  }
  u8g2.setFontMode(1);  // Transparent: glyphs draw only their own pixels.
  u8g2.enableUTF8Print();
}

U8G2 &display() { return u8g2; }

void screenAnimate(bool on) {
  u8g2.setBusClock(on ? 10000000 : 1000000);
  u8g2.sendF("c", on ? 0x38 : 0x39);  // ST7305 High / Low Power Mode
}

void screenSleep() {
  gpio_hold_en((gpio_num_t)PIN_LCD_CS);
  gpio_hold_en((gpio_num_t)PIN_LCD_RST);
  gpio_deep_sleep_hold_en();
}

void drawMainScreen(const NetData &data, const char *error, const Climate &climate, float batteryVolts,
                    bool usbConnected, bool loading) {
  constexpr int ROWS = 6, ROW_PITCH = 30, FIRST_ROW = 47, COLUMN_X = 133, CALENDAR_X = 144;
  time_t now = time(nullptr);
  char text[64];
  u8g2.clearBuffer();

  // Top row: time, date, battery.
  formatClock(now, text, sizeof(text));
  u8g2.setFont(u8g2_font_helvB24_tf);
  u8g2.drawUTF8(10, 30, text);
  int timeRight = 10 + u8g2.getUTF8Width(text);
  if (timeIsValid()) {
    tm local;
    localtime_r(&now, &local);
    snprintf(text, sizeof(text), "%s %d. %s", WEEKDAYS[local.tm_wday], local.tm_mday, MONTHS[local.tm_mon]);
    u8g2.setFont(u8g2_font_helvB14_tf);
    u8g2.drawUTF8((timeRight + 10 + 300 - u8g2.getUTF8Width(text)) / 2, 28, text);
  }
  drawBattery(batteryVolts, WIDTH - 10, 13);
  u8g2.drawHLine(0, 40, WIDTH);

  // Departures in the left third. Only the line and the minutes, and passed departures are skipped.
  int shown = 0;
  for (int i = 0; i < data.departureCount && shown < ROWS; i++) {
    const Departure &dep = data.departures[i];
    long seconds = (long)dep.time - (long)now;
    if (seconds < 0) continue;

    int top = FIRST_ROW + shown * ROW_PITCH;
    u8g2.setFont(u8g2_font_helvB18_tf);
    int lineWidth = u8g2.getUTF8Width(dep.line);
    int boxWidth = max(34, lineWidth + 12);
    u8g2.drawRBox(8, top, boxWidth, 24, 4);
    u8g2.setDrawColor(0);
    u8g2.drawUTF8(8 + (boxWidth - lineWidth) / 2, top + 19, dep.line);
    u8g2.setDrawColor(1);

    long minutes = seconds / 60;
    if (minutes < 1) snprintf(text, sizeof(text), "nå");
    else if (minutes < 60) snprintf(text, sizeof(text), "%ld min", minutes);
    else formatClock(dep.time, text, sizeof(text));  // Far away, e.g. overnight: show the time instead.
    drawRight(text, COLUMN_X - 8, top + 19);
    shown++;
  }
  if (data.departuresError[0]) {
    drawWarningBlock(8, FIRST_ROW, COLUMN_X - 16, data.departuresError);
  } else if (shown == 0) {
    u8g2.setFont(u8g2_font_helvR12_tf);
    u8g2.drawUTF8(8, FIRST_ROW + 19, data.valid ? "Ingen avganger" : "Ingen data");
  }

  // Calendar in the remaining two thirds. Day and time in bold, then the title.
  tm today;
  localtime_r(&now, &today);
  shown = 0;
  for (int i = 0; i < data.calendarCount && shown < ROWS; i++) {
    const CalendarEvent &event = data.calendar[i];
    if ((time_t)event.end <= now) continue;

    time_t start = event.start;
    tm local;
    localtime_r(&start, &local);
    bool startsToday = start <= now || (local.tm_year == today.tm_year && local.tm_yday == today.tm_yday);
    char label[24] = "";
    if (!startsToday) {
      if (start - now < 6 * 86400) snprintf(label, sizeof(label), "%s", SHORT_WEEKDAYS[local.tm_wday]);
      else snprintf(label, sizeof(label), "%d/%d", local.tm_mday, local.tm_mon + 1);
    }
    if (!event.allDay) {
      char clock[8];
      strftime(clock, sizeof(clock), "%H:%M", &local);
      size_t used = strlen(label);
      snprintf(label + used, sizeof(label) - used, "%s%s", used ? " " : "", clock);
    }

    int baseline = FIRST_ROW + shown * ROW_PITCH + 19;
    int x = CALENDAR_X;
    if (label[0]) {
      u8g2.setFont(u8g2_font_helvB14_tf);
      u8g2.drawUTF8(x, baseline, label);
      x += u8g2.getUTF8Width(label) + 8;
    }
    u8g2.setFont(u8g2_font_helvR14_tf);
    fitText(event.title, WIDTH - 8 - x, text, sizeof(text));
    u8g2.drawUTF8(x, baseline, text);
    shown++;
  }
  if (data.calendarError[0]) {
    drawWarningBlock(CALENDAR_X, FIRST_ROW, WIDTH - 8 - CALENDAR_X, data.calendarError);
  } else if (shown == 0) {
    u8g2.setFont(u8g2_font_helvR12_tf);
    u8g2.drawUTF8(CALENDAR_X, FIRST_ROW + 19, data.valid ? "Ingen planer" : "Ingen data");
  }
  u8g2.drawHLine(0, 225, WIDTH);

  // Weather in the left third, under the departures. The forecast hour that matches the current time
  // is shown, since the data is only fetched once a day.
  const WeatherHour *hour = nullptr;
  for (int i = 0; i < data.forecastCount; i++)
    if ((time_t)data.forecast[i].time <= now || !hour) hour = &data.forecast[i];
  if (hour) {
    drawWeatherIcon(hour->icon, 10, 229);
    snprintf(text, sizeof(text), "%d°", hour->temp);
    u8g2.setFont(u8g2_font_helvB24_tf);
    u8g2.drawUTF8(54, 255, text);
    u8g2.setFont(u8g2_font_helvR12_tf);
    fitText(hour->text, COLUMN_X - 16, text, sizeof(text));
    u8g2.drawUTF8(10, 276, text);
  } else if (data.weatherError[0]) {
    u8g2.setFont(u8g2_font_open_iconic_embedded_4x_t);
    u8g2.drawGlyph(10, 261, WARNING_GLYPH);
    u8g2.setFont(u8g2_font_helvR12_tf);
    fitText(data.weatherError, COLUMN_X - 16, text, sizeof(text));
    u8g2.drawUTF8(10, 276, text);
  }

  // Indoor climate in the right two thirds. Kept clear of the bottom row, where the badges and the
  // footer live.
  constexpr int INDOOR_X = COLUMN_X + 15, HUMIDITY_X = COLUMN_X + 147;
  u8g2.setFont(u8g2_font_helvR12_tf);
  u8g2.drawUTF8(INDOOR_X, 241, "Inne");
  u8g2.drawUTF8(HUMIDITY_X, 241, "Luftfuktighet");
  u8g2.setFont(u8g2_font_logisoso24_tf);
  if (climate.valid) {
    snprintf(text, sizeof(text), "%.1f °C", climate.temperature);
    decimalComma(text);
    u8g2.drawUTF8(INDOOR_X, 270, text);
    snprintf(text, sizeof(text), "%.0f %%", climate.humidity);
    u8g2.drawUTF8(HUMIDITY_X, 270, text);
  } else {
    u8g2.drawUTF8(INDOOR_X, 270, "--");
    u8g2.drawUTF8(HUMIDITY_X, 270, "--");
  }

  // One solid divider all the way down: timetable | calendar, and weather | indoor climate.
  u8g2.drawVLine(COLUMN_X, 41, 238);

  // Bottom row: loading badge or fetch error on the left, USB badge in the middle, fetch time on the right.
  if (loading) {
    drawBadge("Henter…", 10);
  } else if (error && error[0]) {
    drawWarning(10, 296, error, u8g2_font_helvR10_tf);
  }
  if (usbConnected) drawUsbIndicator();
  if (data.valid) {
    char fetched[8];
    formatClock(data.fetchedAt, fetched, sizeof(fetched));
    snprintf(text, sizeof(text), "hentet %s", fetched);
    u8g2.setFont(u8g2_font_helvR10_tf);
    drawRight(text, WIDTH - 10, 296);
  }

  u8g2.sendBuffer();
}

void drawMessage(const char *title, const char *detail) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB24_tf);
  drawCentered(title, 140);
  if (detail && detail[0]) {
    u8g2.setFont(u8g2_font_helvR14_tf);
    drawCentered(detail, 180);
  }
  u8g2.sendBuffer();
}
