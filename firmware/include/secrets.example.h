#pragma once

// Copy this file to secrets.h and fill in the values. secrets.h is gitignored.

#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// 64 hexadecimal characters; use the same key as API_KEY in server/.env.
#define API_KEY ""

// Use the PC's IP address on the local network, not localhost.
#define SERVER_URL "http://192.168.1.10:3000/api/v1/screen"

// Optional WebSocket for the voice screen. By default it is derived from SERVER_URL
// (http -> ws, https -> wss, /api/v1/screen -> /api/v1/voice).
#define VOICE_URL "ws://192.168.1.10:3000/api/v1/voice"
