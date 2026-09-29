#pragma once

#include <stdint.h>

// Animated faces for the voice screen, drawn with U8g2 primitives. The server picks the face and
// mood by name; unknown names fall back to the first value.

enum class FaceKind : uint8_t { Waifu, Oracle };
enum class Mood : uint8_t { Neutral, Happy, Sad, Surprised, Angry, Sly };
enum class VoiceState : uint8_t { Connecting, Listening, Hearing, Thinking, Speaking, Error };

FaceKind faceFromName(const char *name);
Mood moodFromName(const char *name);

struct Face {
  FaceKind kind = FaceKind::Waifu;
  Mood mood = Mood::Happy;
  VoiceState state = VoiceState::Connecting;
  char name[24] = "";
  char status[48] = "";  // Overrides the default status text, e.g. for errors.

  // Updated by animateFace().
  float blink = 0;  // 0 open, 1 closed
  float mouth = 0;  // 0 closed, 1 wide open
  float lookX = 0, lookY = 0;  // -1..1
  float bob = 0;    // Vertical head offset in pixels
  uint32_t ms = 0;

  // Animation timers.
  uint32_t nextBlink = 0, blinkStart = 0, nextLook = 0;
  float targetX = 0, targetY = 0;
};

// Advance blinking, eye movement, and mouth. `level` is the loudness of the audio playing now (0..1).
void animateFace(Face &face, uint32_t now, float level);
void drawFace(const Face &face);
