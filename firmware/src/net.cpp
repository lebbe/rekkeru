#include "net.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "config.h"
#include "secrets.h"

namespace {

bool request(NetData &data, char *error, size_t errorSize) {
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(SERVER_URL)) {
    snprintf(error, errorSize, "Ugyldig SERVER_URL");
    return false;
  }
  http.addHeader("Authorization", "Bearer " API_KEY);

  int status = http.GET();
  if (status != 200) {
    if (status == 418) snprintf(error, errorSize, "Feil API-nøkkel");
    else if (status < 0) snprintf(error, errorSize, "Fant ikke serveren");
    else snprintf(error, errorSize, "Serverfeil %d", status);
    http.end();
    return false;
  }

  JsonDocument doc;
  DeserializationError jsonError = deserializeJson(doc, http.getString());
  http.end();
  if (jsonError || !(doc["now"] | 0)) {
    snprintf(error, errorSize, "Ugyldig svar");
    return false;
  }

  NetData d = {};
  d.valid = true;
  d.fetchedAt = doc["now"];

  // An unavailable service is sent as { "error": "..." } instead of normal data.
  strlcpy(d.departuresError, doc["departures"]["error"] | "", sizeof(d.departuresError));
  strlcpy(d.weatherError, doc["weather"]["error"] | "", sizeof(d.weatherError));
  strlcpy(d.calendarError, doc["calendar"]["error"] | "", sizeof(d.calendarError));

  for (JsonObject dep : doc["departures"].as<JsonArray>()) {
    if (d.departureCount == MAX_DEPARTURES) break;
    Departure &out = d.departures[d.departureCount++];
    strlcpy(out.line, dep["line"] | "", sizeof(out.line));
    strlcpy(out.dest, dep["dest"] | "", sizeof(out.dest));
    out.time = dep["time"] | 0;
  }

  JsonObject weather = doc["weather"];
  if (!weather.isNull() && !d.weatherError[0]) {
    d.hasWeather = true;
    d.temp = lroundf(weather["temp"] | 0.0f);
    strlcpy(d.icon, weather["icon"] | "", sizeof(d.icon));
    strlcpy(d.weatherText, weather["text"] | "", sizeof(d.weatherText));
  }

  for (const char *line : doc["calendar"].as<JsonArray>()) {
    if (d.calendarCount == MAX_CALENDAR) break;
    strlcpy(d.calendar[d.calendarCount++], line ? line : "", sizeof(d.calendar[0]));
  }

  data = d;
  return true;
}

}  // namespace

bool wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) delay(50);
  return WiFi.status() == WL_CONNECTED;
}

void wifiOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool fetchScreen(NetData &data, char *error, size_t errorSize) {
  bool ok = false;
  if (wifiConnect()) {
    ok = request(data, error, errorSize);
  } else {
    snprintf(error, errorSize, "Fikk ikke koblet til Wi-Fi");
  }
  wifiOff();
  return ok;
}
