#pragma once

#include <stdint.h>

// Pins from Waveshare's RLCD guide and the ESPHome configuration for the board.
constexpr int PIN_LCD_SCK = 11;
constexpr int PIN_LCD_MOSI = 12;
constexpr int PIN_LCD_CS = 40;
constexpr int PIN_LCD_DC = 5;
constexpr int PIN_LCD_RST = 41;

constexpr int PIN_I2C_SDA = 13;
constexpr int PIN_I2C_SCL = 14;

constexpr int PIN_KEY = 18;     // Active low, RTC GPIO, wakes from deep sleep.
constexpr int PIN_BOOT = 0;     // Active low. Held at reset it enters download mode; free to use after boot.
constexpr int PIN_BATTERY = 4;  // ADC, 3x spenningsdeler.
constexpr int PIN_PA_CTRL = 46;  // NS4150B speaker amplifier enable. Held low so it cannot float on.


// A single-cell Li-ion/LiPo is not usable down to 2.5 V; the board browns out long before that,
// so treat a more realistic no-load cutoff as empty (see docs/waveshare-esp32-s3-rlcd-42-battery).
constexpr float BATTERY_EMPTY_VOLTS = 3.1f;  // 3.3 V showed 0 % for almost a day, so it is not empty.
constexpr float BATTERY_FULL_VOLTS = 4.2f;
// The default empty voltage is lowered by the logged minimum if the battery is ever read lower. The
// logged maximum is full once it is more than this above empty.
constexpr float BATTERY_MIN_LOGGED_SPAN_VOLTS = 0.5f;
constexpr float BATTERY_LOG_STEP_VOLTS = 0.01f;       // A new extreme must beat the old by this to be written.
constexpr float BATTERY_PLAUSIBLE_MIN_VOLTS = 2.8f;   // Readings outside this window are not logged.
constexpr float BATTERY_PLAUSIBLE_MAX_VOLTS = 4.5f;

// Audio, from Waveshare's XiaoZhi board configuration (boards/waveshare-s3-rlcd-4.2/config.h).
// ES8311 drives the speaker, ES7210 reads the two microphones. Both share I2S and the I2C bus above.
constexpr int PIN_I2S_MCLK = 16;
constexpr int PIN_I2S_BCLK = 9;
constexpr int PIN_I2S_WS = 45;
constexpr int PIN_I2S_DOUT = 8;   // To ES8311
constexpr int PIN_I2S_DIN = 10;   // From ES7210
constexpr int PIN_SPEAKER_PA = 46;
constexpr uint8_t ES8311_ADDR = 0x18;
constexpr uint8_t ES7210_ADDR = 0x40;

constexpr const char *TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";

// Departures, calendar and clock are fetched once a day at this local hour, or when BOOT is pressed.
constexpr int FETCH_HOUR = 7;
// After a failed fetch, wait this long before trying again.
constexpr uint32_t FETCH_RETRY_SECONDS = 15 * 60;

constexpr uint32_t WIFI_TIMEOUT_MS = 10000;
constexpr uint32_t HTTP_TIMEOUT_MS = 5000;

// Voice screen. Audio is PCM16 mono at this rate in both directions. ESP-SR's echo canceller only
// runs at 16 kHz, so the codec runs at 16 kHz too and the server resamples Gemini's 24 kHz speech.
constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;
constexpr uint8_t SPEAKER_VOLUME = 0xBF;  // ES8311 DAC volume: 0xBF = 0 dB, 0.5 dB per step.
constexpr uint8_t MIC_GAIN = 10;          // ES7210 PGA: 3 dB per step, so 10 = 30 dB (as XiaoZhi).
// The shaman lifts his drumstick this long before each hit, and a hit counts as "just landed" for this long.
constexpr uint32_t DRUM_RAISE_MS = 130;
constexpr uint32_t DRUM_IMPACT_MS = 100;
// Audio is heard a little after it is handed to the speaker. Delays the hand by this much to match.
constexpr uint32_t DRUM_VISUAL_DELAY_MS = 60;
// Return to the local screen after this long without any conversation.
constexpr uint32_t VOICE_IDLE_SECONDS = 3 * 60;
// Print voice diagnostics over USB once a second: level per TDM slot and after echo cancellation,
// bytes sent and received, and main loop timing. Read them with `python -m platformio device monitor`.
constexpr bool VOICE_LOG = false;
