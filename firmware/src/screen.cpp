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

void screenSleep() {
  gpio_hold_en((gpio_num_t)PIN_LCD_CS);
  gpio_hold_en((gpio_num_t)PIN_LCD_RST);
  gpio_deep_sleep_hold_en();
}

void drawLocalScreen(const Climate &climate, float batteryVolts) {
  time_t now = time(nullptr);
  char text[48];
  u8g2.clearBuffer();

  if (timeIsValid()) {
    tm local;
    localtime_r(&now, &local);
    snprintf(text, sizeof(text), "%s %d. %s", WEEKDAYS[local.tm_wday], local.tm_mday, MONTHS[local.tm_mon]);
    u8g2.setFont(u8g2_font_helvB14_tf);
    u8g2.drawUTF8(12, 28, text);
  }
  drawBattery(batteryVolts, WIDTH - 10, 13);

  formatClock(now, text, sizeof(text));
  u8g2.setFont(u8g2_font_logisoso92_tn);
  drawCentered(text, 170);

  u8g2.setFont(u8g2_font_helvR12_tf);
  u8g2.drawUTF8(24, 218, "Inne");
  u8g2.drawUTF8(224, 218, "Luftfuktighet");

  u8g2.setFont(u8g2_font_logisoso32_tf);
  if (climate.valid) {
    snprintf(text, sizeof(text), "%.1f °C", climate.temperature);
    decimalComma(text);
    u8g2.drawUTF8(24, 266, text);
    snprintf(text, sizeof(text), "%.0f %%", climate.humidity);
    u8g2.drawUTF8(224, 266, text);
  } else {
    u8g2.drawUTF8(24, 266, "--");
    u8g2.drawUTF8(224, 266, "--");
  }

  u8g2.sendBuffer();
}

void drawNetScreen(const NetData &data, const char *error) {
  time_t now = time(nullptr);
  char text[64];
  u8g2.clearBuffer();

  // Header
  u8g2.setFont(u8g2_font_helvB18_tf);
  u8g2.drawUTF8(10, 28, "T-bane");
  formatClock(now, text, sizeof(text));
  drawRight(text, WIDTH - 10, 28);
  u8g2.drawHLine(0, 38, WIDTH);

  // Departures. Calculate minutes here and skip departures that have already passed.
  int y = 74;
  int shown = 0;
  for (int i = 0; i < data.departureCount && shown < 4; i++) {
    const Departure &dep = data.departures[i];
    long seconds = (long)dep.time - (long)now;
    if (seconds < 0) continue;

    u8g2.setFont(u8g2_font_helvB18_tf);
    int lineWidth = u8g2.getUTF8Width(dep.line);
    int boxWidth = max(32, lineWidth + 12);
    u8g2.drawRBox(10, y - 22, boxWidth, 28, 4);
    u8g2.setDrawColor(0);
    u8g2.drawUTF8(10 + (boxWidth - lineWidth) / 2, y, dep.line);
    u8g2.setDrawColor(1);

    u8g2.drawUTF8(10 + boxWidth + 12, y, dep.dest);

    long minutes = seconds / 60;
    if (minutes < 1) snprintf(text, sizeof(text), "nå");
    else snprintf(text, sizeof(text), "%ld min", minutes);
    drawRight(text, WIDTH - 10, y);

    y += 38;
    shown++;
  }
  if (data.departuresError[0]) {
    drawWarning(10, 74, data.departuresError, u8g2_font_helvB14_tf);
  } else if (shown == 0) {
    u8g2.setFont(u8g2_font_helvR14_tf);
    u8g2.drawUTF8(10, 74, "Ingen avganger");
  }
  u8g2.drawHLine(0, 205, WIDTH);

  // Weather on the left, using only as much width as its content needs.
  int weatherRight = 0;
  if (data.hasWeather) {
    drawWeatherIcon(data.icon, 10, 216);
    u8g2.setFont(u8g2_font_helvB24_tf);
    snprintf(text, sizeof(text), "%d°", data.temp);
    u8g2.drawUTF8(54, 246, text);
    weatherRight = 54 + u8g2.getUTF8Width(text);
    u8g2.setFont(u8g2_font_helvR14_tf);
    u8g2.drawUTF8(10, 276, data.weatherText);
    weatherRight = max(weatherRight, 10 + u8g2.getUTF8Width(data.weatherText));
  } else if (data.weatherError[0]) {
    u8g2.setFont(u8g2_font_open_iconic_embedded_4x_t);
    u8g2.drawGlyph(10, 248, WARNING_GLYPH);
    u8g2.setFont(u8g2_font_helvR14_tf);
    u8g2.drawUTF8(10, 276, data.weatherError);
    weatherRight = 10 + u8g2.getUTF8Width(data.weatherError);
  }

  int calendarX = 10;
  if (weatherRight > 0) {
    calendarX = weatherRight + 25;
    if (data.calendarCount > 0 || data.calendarError[0]) u8g2.drawVLine(weatherRight + 12, 214, 68);
  }

  // Calendar in the remaining width, up to four lines.
  if (data.calendarError[0]) drawWarning(calendarX, 232, data.calendarError, u8g2_font_helvR12_tf);
  u8g2.setFont(u8g2_font_helvR12_tf);
  for (int i = 0; i < data.calendarCount; i++) u8g2.drawUTF8(calendarX, 228 + i * 17, data.calendar[i]);

  // Footer
  char fetched[8];
  formatClock(data.fetchedAt, fetched, sizeof(fetched));
  if (error && error[0]) snprintf(text, sizeof(text), "%s · hentet %s", error, fetched);
  else snprintf(text, sizeof(text), "hentet %s", fetched);
  u8g2.setFont(u8g2_font_helvR10_tf);
  u8g2.drawUTF8(10, 297, text);

  u8g2.sendBuffer();
}

void drawServerDown(const char *detail) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_logisoso92_tn);
  drawCentered("501", 130);
  u8g2.setFont(u8g2_font_helvB18_tf);
  drawCentered("Får ikke kontakt med serveren", 185);
  u8g2.setFont(u8g2_font_helvR14_tf);
  drawCentered(detail, 220);
  u8g2.setFont(u8g2_font_helvR10_tf);
  drawCentered("Trykk KEY for å gå tilbake", 290);
  u8g2.sendBuffer();
}

void drawTeapot() {
  const int cx = 200, cy = 85;  // midten av kroppen
  u8g2.clearBuffer();
  for (int r = 0; r < 3; r++) u8g2.drawEllipse(cx + 48, cy - 2, 16 - r, 20 - r);  // hank
  u8g2.drawTriangle(cx - 30, cy + 2, cx - 30, cy + 22, cx - 72, cy - 26);         // tut
  u8g2.drawFilledEllipse(cx, cy, 45, 32);                                          // kropp
  u8g2.drawBox(cx - 28, cy + 30, 56, 6);                                           // fot
  u8g2.drawFilledEllipse(cx, cy - 32, 22, 8, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);  // lokk
  u8g2.drawDisc(cx, cy - 43, 5);                                                   // knott

  u8g2.setFont(u8g2_font_helvB24_tf);
  drawCentered("418 I'm a teapot", 165);
  u8g2.setFont(u8g2_font_helvR14_tf);
  drawCentered("Serveren min oppfører seg som en tekanne", 200);
  u8g2.setFont(u8g2_font_helvR12_tf);
  drawCentered("Ingen av tjenestene svarer akkurat nå", 228);
  u8g2.setFont(u8g2_font_helvR10_tf);
  drawCentered("Trykk KEY for å gå tilbake", 290);
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
