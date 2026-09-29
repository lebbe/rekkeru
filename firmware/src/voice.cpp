#include "voice.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

#include "audio.h"
#include "config.h"
#include "face.h"
#include "net.h"
#include "screen.h"
#include "secrets.h"

namespace {

RTC_DATA_ATTR uint8_t persona;  // Last persona, remembered through deep sleep.

WebSocketsClient ws;
Face face;
bool connected;
VoiceState serverState;
uint32_t lastActivity;

struct Url {
  bool secure;
  char host[64];
  uint16_t port;
  char path[96];
};

// Accepts ws://, wss://, http://, and https:// URLs with an optional port.
bool parseUrl(const char *url, Url &out) {
  const char *scheme = strstr(url, "://");
  if (!scheme) return false;
  out.secure = !strncmp(url, "wss", 3) || !strncmp(url, "https", 5);
  const char *host = scheme + 3;
  const char *slash = strchr(host, '/');
  const char *end = slash ? slash : host + strlen(host);
  const char *colon = (const char *)memchr(host, ':', end - host);
  size_t hostLength = (colon ? colon : end) - host;
  if (hostLength == 0 || hostLength >= sizeof(out.host)) return false;
  memcpy(out.host, host, hostLength);
  out.host[hostLength] = '\0';
  out.port = colon ? atoi(colon + 1) : (out.secure ? 443 : 80);
  strlcpy(out.path, slash ? slash : "/", sizeof(out.path));
  return true;
}

bool voiceUrl(Url &url) {
#ifdef VOICE_URL
  if (!parseUrl(VOICE_URL, url)) return false;
#else
  // Same server as the network screen: /api/v1/screen -> /api/v1/voice.
  if (!parseUrl(SERVER_URL, url)) return false;
  char *screen = strstr(url.path, "/screen");
  if (screen) strlcpy(screen, "/voice", sizeof(url.path) - (screen - url.path));
#endif
  size_t length = strlen(url.path);
  snprintf(url.path + length, sizeof(url.path) - length, "?persona=%u", persona);
  return true;
}

VoiceState stateFromName(const char *name) {
  if (!strcmp(name, "connecting")) return VoiceState::Connecting;
  if (!strcmp(name, "hearing")) return VoiceState::Hearing;
  if (!strcmp(name, "thinking")) return VoiceState::Thinking;
  return VoiceState::Listening;
}

void onText(const uint8_t *payload, size_t length) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, length)) return;
  const char *type = doc["type"] | "";

  if (!strcmp(type, "persona")) {
    persona = doc["index"] | 0;
    face.kind = faceFromName(doc["face"]);
    face.mood = moodFromName(doc["mood"]);
    strlcpy(face.name, doc["name"] | "", sizeof(face.name));
  } else if (!strcmp(type, "state")) {
    serverState = stateFromName(doc["state"] | "");
    if (serverState == VoiceState::Hearing) lastActivity = millis();
  } else if (!strcmp(type, "mood")) {
    face.mood = moodFromName(doc["mood"]);
  } else if (!strcmp(type, "interrupted")) {
    audioFlush();
  } else if (!strcmp(type, "error")) {
    strlcpy(face.status, doc["text"] | "Feil", sizeof(face.status));
  }
}

void onEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      connected = true;
      face.status[0] = '\0';
      serverState = VoiceState::Connecting;
      break;
    case WStype_DISCONNECTED:
      connected = false;
      audioFlush();
      break;
    case WStype_TEXT:
      onText(payload, length);
      break;
    case WStype_BIN:
      audioPlay(payload, length);
      lastActivity = millis();
      break;
    default:
      break;
  }
}

VoiceState currentState() {
  if (!connected) return face.status[0] ? VoiceState::Error : VoiceState::Connecting;
  if (audioPlaying()) return VoiceState::Speaking;
  return serverState;
}

// True once per press. Buttons are active low.
bool pressed(int pin, bool &wasDown) {
  bool down = digitalRead(pin) == LOW;
  bool edge = down && !wasDown;
  wasDown = down;
  return edge;
}

}  // namespace

void runVoice() {
  face = Face();
  face.state = VoiceState::Connecting;
  strlcpy(face.name, "…", sizeof(face.name));
  screenAnimate(true);
  animateFace(face, millis(), 0);
  drawFace(face);

  Url url;
  if (!voiceUrl(url)) {
    drawMessage("Ugyldig VOICE_URL", "");
  } else if (!wifiConnect()) {
    drawMessage("Fikk ikke koblet til Wi-Fi", "");
  } else if (!audioBegin()) {
    audioEnd();
    drawMessage("Fant ikke lydbrikkene", "ES8311 / ES7210");
  } else {
    WiFi.setSleep(false);  // Power save adds latency to every audio packet.
    connected = false;
    serverState = VoiceState::Connecting;
    ws.onEvent(onEvent);
    ws.setExtraHeaders("Authorization: Bearer " API_KEY);
    ws.setReconnectInterval(3000);
    if (url.secure) ws.beginSSL(url.host, url.port, url.path, "", "rekkeru");
    else ws.begin(url.host, url.port, url.path, "rekkeru");

    pinMode(PIN_BOOT, INPUT_PULLUP);
    bool keyDown = true, bootDown = digitalRead(PIN_BOOT) == LOW;
    lastActivity = millis();
    uint32_t lastFrame = 0;
    static uint8_t mic[AUDIO_SAMPLE_RATE / 50 * 2];  // 20 ms

    while (true) {
      ws.loop();

      size_t bytes;
      while ((bytes = audioRecord(mic, sizeof(mic))) > 0)
        if (connected) ws.sendBIN(mic, bytes);

      uint32_t now = millis();
      if (pressed(PIN_KEY, keyDown)) break;
      if (pressed(PIN_BOOT, bootDown) && connected) {
        audioFlush();
        ws.sendTXT("{\"type\":\"next\"}");
        lastActivity = now;
      }
      if (now - lastActivity > VOICE_IDLE_SECONDS * 1000) break;

      if (now - lastFrame >= 66) {  // About 15 frames per second
        lastFrame = now;
        face.state = currentState();
        animateFace(face, now, audioLevel());
        drawFace(face);
      }
      delay(1);
    }

    ws.disconnect();
    audioEnd();
    wifiOff();
    screenAnimate(false);
    return;
  }

  wifiOff();
  screenAnimate(false);
  delay(3000);
}
