#include "face.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <math.h>
#include <string.h>

#include "screen.h"

namespace {

// Ink is color 1 (dark), paper is color 0. Color 2 inverts, so it shows on both.
constexpr uint8_t INK = 1, PAPER = 0, INVERT = 2;
constexpr uint8_t UPPER = U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT;
constexpr uint8_t LOWER = U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT;

U8G2 &g() { return display(); }

int r(float value) { return (int)lroundf(value); }

// Quadratic Bezier stroke from (x0, y0) to (x1, y1), bending toward (cx, cy). The stroke tapers
// from radius r0 to r1; a radius below 1 gives a one-pixel line.
void stroke(float x0, float y0, float cx, float cy, float x1, float y1, float r0 = 0, float r1 = -1) {
  if (r1 < 0) r1 = r0;
  float length = hypotf(cx - x0, cy - y0) + hypotf(x1 - cx, y1 - cy);
  int steps = max(4, (int)(length / 1.5f));
  int px = r(x0), py = r(y0);
  for (int i = 1; i <= steps; i++) {
    float t = (float)i / steps, u = 1 - t;
    int x = r(u * u * x0 + 2 * u * t * cx + t * t * x1);
    int y = r(u * u * y0 + 2 * u * t * cy + t * t * y1);
    float radius = r0 + (r1 - r0) * t;
    if (radius < 1) g().drawLine(px, py, x, y);
    else g().drawDisc(x, y, r(radius));
    px = x;
    py = y;
  }
}

void ellipse(int x, int y, int rx, int ry, uint8_t color, uint8_t part = U8G2_DRAW_ALL) {
  g().setDrawColor(color);
  g().drawFilledEllipse(x, y, max(rx, 1), max(ry, 1), part);
  g().setDrawColor(INK);
}

// Filled shape with an ink outline.
void paperEllipse(int x, int y, int rx, int ry) {
  ellipse(x, y, rx, ry, PAPER);
  g().drawEllipse(x, y, rx, ry);
}

// Expression for one eyebrow and eye. Brow offsets are in pixels, positive is down.
struct Side {
  int browInner, browOuter;
};
struct Expression {
  Side left, right;
  float lid;    // Eyelid lowered this far, 0..1
  float scale;  // Eye size
};

Expression expression(const Face &face) {
  Expression e;
  switch (face.mood) {
    case Mood::Happy: e = {{-3, -3}, {-3, -3}, 0.0f, 1.0f}; break;
    case Mood::Sad: e = {{-7, 3}, {-7, 3}, 0.25f, 1.0f}; break;
    case Mood::Surprised: e = {{-9, -8}, {-9, -8}, 0.0f, 1.15f}; break;
    case Mood::Angry: e = {{7, -3}, {7, -3}, 0.3f, 1.0f}; break;
    case Mood::Sly: e = {{-6, -9}, {4, 1}, 0.4f, 1.0f}; break;
    default: e = {{0, 0}, {0, 0}, 0.0f, 1.0f}; break;
  }
  if (face.kind == FaceKind::Oracle) e.lid = max(e.lid, 0.3f);  // Heavy, mysterious lids.
  if (face.state == VoiceState::Hearing) {
    e.lid *= 0.5f;
    e.scale *= 1.08f;
  }
  if (face.state == VoiceState::Connecting) e.lid = 1;
  e.lid = max(e.lid, face.blink);
  return e;
}

// ---------------------------------------------------------------- Waifu

// Big anime eye. `outer` is -1 for the left eye and 1 for the right eye.
void waifuEye(const Face &face, int x, int y, float lid, float scale, int outer, bool happy) {
  int rx = r(15 * scale), ry = r(20 * scale);
  if (lid > 0.85f) {
    if (happy) stroke(x - rx, y + 3, x, y - 14, x + rx, y + 3, 1.5f);  // ^
    else stroke(x - rx, y - 2, x, y + 8, x + rx, y - 2, 1.5f);         // closed
    return;
  }
  int lx = r(face.lookX * 3), ly = r(face.lookY * 3);
  ellipse(x + lx, y + ly, rx, ry, INK);
  g().setDrawColor(PAPER);
  g().drawDisc(x + lx - 5, y + ly - 8, 5);
  g().drawDisc(x + lx + 5, y + ly + 8, 2);
  g().setDrawColor(INK);

  int top = r(y - ry + lid * 2 * ry);
  if (lid > 0) {
    g().setDrawColor(PAPER);
    g().drawBox(x - rx - 4, y - ry - 6, 2 * rx + 9, top - (y - ry) + 6);
    g().setDrawColor(INK);
  }
  // Upper lashes, with a flick at the outer corner.
  stroke(x - rx - 3, top + 3, x, top - 7, x + rx + 3, top + 3, 2);
  stroke(x + outer * (rx + 2), top + 2, x + outer * (rx + 6), top, x + outer * (rx + 9), top - 4, 1.5f, 0.5f);
}

void waifuBrow(int x, int y, Side side, int outer) {
  int inner = x - outer * 16, far = x + outer * 16;
  stroke(far, y + side.browOuter, x, y - 4 + (side.browInner + side.browOuter) / 2, inner, y + side.browInner, 1);
}

void waifuMouth(const Face &face, int oy) {
  const int x = 200, y = 224 + oy;
  if (face.mouth > 0.05f) {
    int w = r(8 + 5 * face.mouth), h = r(3 + 15 * face.mouth);
    ellipse(x, y - 2, w, h, INK, LOWER);
    g().drawHLine(x - w, y - 2, 2 * w + 1);
    return;
  }
  switch (face.mood) {
    case Mood::Happy: stroke(x - 11, y - 3, x, y + 8, x + 11, y - 3, 1); break;
    case Mood::Sad: stroke(x - 9, y + 4, x, y - 4, x + 9, y + 4, 1); break;
    case Mood::Surprised:
      g().drawEllipse(x, y + 1, 5, 7);
      g().drawEllipse(x, y + 1, 4, 6);
      break;
    case Mood::Angry: stroke(x - 10, y + 3, x, y - 2, x + 10, y + 3, 1); break;
    case Mood::Sly:  // Cat mouth
      stroke(x - 10, y - 2, x - 5, y + 5, x, y - 1, 1);
      stroke(x, y - 1, x + 5, y + 5, x + 10, y - 2, 1);
      break;
    default: stroke(x - 7, y, x, y + 2, x + 7, y, 1); break;
  }
}

void drawWaifu(const Face &face) {
  const int oy = r(face.bob);
  const Expression e = expression(face);

  // Long hair behind everything, down to the bottom of the screen.
  ellipse(200, 135 + oy, 122, 110, INK);
  g().drawBox(78, 135 + oy, 245, 200);

  // Shoulders with a sailor collar, and the neck.
  paperEllipse(200, 322, 104, 58);
  g().drawLine(172, 266, 200, 294);
  g().drawLine(228, 266, 200, 294);
  g().drawLine(166, 270, 200, 300);
  g().drawLine(234, 270, 200, 300);
  g().setDrawColor(PAPER);
  g().drawBox(186, 220 + oy, 29, 56 - oy);
  g().setDrawColor(INK);
  g().drawVLine(185, 228 + oy, 42 - oy);
  g().drawVLine(215, 228 + oy, 42 - oy);

  // Face, hair on top of the head, bangs, and locks along the cheeks.
  paperEllipse(200, 160 + oy, 80, 88);
  ellipse(200, 118 + oy, 90, 58, INK, UPPER);
  const int bangs[][3] = {{112, 150, 136}, {142, 180, 141}, {172, 206, 135}, {196, 230, 140}, {222, 258, 136}, {250, 288, 141}};
  for (const auto &b : bangs) g().drawTriangle(b[0], 117 + oy, b[1], 117 + oy, (b[0] + b[1]) / 2 + 4, b[2] + oy);
  g().drawTriangle(111, 117 + oy, 134, 117 + oy, 118, 248 + oy);
  g().drawTriangle(266, 117 + oy, 289, 117 + oy, 282, 248 + oy);

  // Hair bow.
  g().setDrawColor(PAPER);
  g().drawTriangle(262, 84 + oy, 246, 72 + oy, 246, 96 + oy);
  g().drawTriangle(262, 84 + oy, 278, 72 + oy, 278, 96 + oy);
  g().setDrawColor(INK);
  g().drawDisc(262, 84 + oy, 3);

  // Eyes and brows.
  const bool happy = face.mood == Mood::Happy && face.state != VoiceState::Connecting;  // ^^ when blinking
  waifuBrow(165, 152 + oy, e.left, -1);
  waifuBrow(235, 152 + oy, e.right, 1);
  waifuEye(face, 165, 182 + oy, e.lid, e.scale, -1, happy);
  waifuEye(face, 235, 182 + oy, e.lid, e.scale, 1, happy);

  // Blush, a tiny nose, and the mouth.
  if (face.mood != Mood::Angry) {
    for (int cx : {146, 254})
      for (int i = 0; i < 3; i++) g().drawLine(cx - 9 + i * 6, 216 + oy, cx - 5 + i * 6, 208 + oy);
  }
  g().drawLine(200, 204 + oy, 198, 208 + oy);
  waifuMouth(face, oy);
}

// ---------------------------------------------------------------- Oracle

void oracleEye(const Face &face, int x, int y, float lid, float scale) {
  int rx = r(14 * scale), ry = r(8 * scale);
  stroke(x - 9, y + 12, x, y + 16, x + 9, y + 12);  // Bags under the eyes
  if (lid > 0.85f) {
    stroke(x - rx, y, x, y + 5, x + rx, y, 1.5f);
    return;
  }
  g().drawEllipse(x, y, rx, ry);
  g().drawDisc(x + r(face.lookX * 6), y + r(face.lookY * 3), 5);
  int top = r(y - ry + lid * 2 * ry);
  g().setDrawColor(PAPER);
  g().drawBox(x - rx - 1, y - ry - 2, 2 * rx + 3, top - (y - ry) + 2);
  g().setDrawColor(INK);
  stroke(x - rx - 2, top + 2, x, top - 3, x + rx + 2, top + 2, 1.5f);
}

void oracleBrow(int x, int y, Side side, int outer) {
  int inner = x - outer * 16, far = x + outer * 20;
  stroke(inner, y + side.browInner, x, y - 6 + (side.browInner + side.browOuter) / 2, far, y + side.browOuter, 3.5f, 1.5f);
}

void oracleMouth(const Face &face, int oy) {
  const int x = 200, y = 196 + oy;
  if (face.mouth > 0.05f) {
    ellipse(x, y, r(10 + 3 * face.mouth), r(1 + 9 * face.mouth), INK);
    return;
  }
  switch (face.mood) {
    case Mood::Happy: stroke(x - 12, y - 2, x, y + 7, x + 12, y - 2, 1); break;
    case Mood::Sad: stroke(x - 12, y + 3, x, y - 5, x + 12, y + 3, 1); break;
    case Mood::Surprised: g().drawEllipse(x, y + 1, 5, 6); break;
    case Mood::Angry: stroke(x - 12, y + 2, x, y - 3, x + 12, y + 2, 1); break;
    case Mood::Sly: stroke(x - 12, y + 1, x + 2, y + 3, x + 13, y - 6, 1); break;  // Smirk
    default: g().drawHLine(x - 10, y, 21); break;
  }
}

void crystalBall(const Face &face) {
  const int cx = 200, cy = 262, radius = 36;
  const float t = face.ms / 1000.0f;
  const bool active = face.state == VoiceState::Speaking || face.state == VoiceState::Thinking;

  // Stand
  g().setDrawColor(PAPER);
  g().drawBox(168, 290, 65, 10);
  g().setDrawColor(INK);
  g().drawFrame(168, 290, 65, 10);

  g().setDrawColor(PAPER);
  g().drawDisc(cx, cy, radius);
  g().setDrawColor(INK);
  g().drawCircle(cx, cy, radius);
  g().drawCircle(cx, cy, radius - 1);

  // Swirling mist, faster and wider while speaking or thinking.
  const float speed = active ? 2.5f : 0.8f;
  const float swirl = 12 + (face.state == VoiceState::Speaking ? 10 * face.mouth : 0);
  for (int k = 0; k < 3; k++) {
    float a = t * speed + k * 2.094f;
    stroke(cx + swirl * cosf(a), cy + swirl * sinf(a), cx + 24 * cosf(a + 0.9f), cy + 24 * sinf(a + 0.9f),
           cx + swirl * cosf(a + 1.8f), cy + swirl * sinf(a + 1.8f), active ? 1 : 0);
  }
  for (int k = 0; k < 4; k++) {
    float a = -t * 1.3f + k * 1.571f;
    g().drawDisc(cx + r(27 * cosf(a)), cy + r(27 * sinf(a)), 1);
  }

  // Shine
  stroke(cx - 24, cy - 10, cx - 22, cy - 24, cx - 8, cy - 29, 1);

  // Rays while the spirits are talking.
  if (active) {
    g().setDrawColor(INVERT);
    for (int k = 0; k < 10; k++) {
      float a = t * 0.6f + k * 0.628f;
      float outer = radius + 7 + 8 * face.mouth + 3 * sinf(t * 7 + k);
      g().drawLine(cx + r((radius + 4) * cosf(a)), cy + r((radius + 4) * sinf(a)), cx + r(outer * cosf(a)),
                   cy + r(outer * sinf(a)));
    }
    g().setDrawColor(INK);
  }
}

void drawOracle(const Face &face) {
  const int oy = r(face.bob);
  const Expression e = expression(face);

  // Robe, neck, and ears (with an earring).
  ellipse(200, 332, 150, 88, INK);
  g().setDrawColor(PAPER);
  g().drawBox(182, 200 + oy, 37, 50);
  g().setDrawColor(INK);
  g().drawVLine(182, 205 + oy, 40);
  g().drawVLine(218, 205 + oy, 40);
  paperEllipse(132, 140 + oy, 9, 15);
  paperEllipse(268, 140 + oy, 9, 15);
  g().drawCircle(268, 162 + oy, 4);

  paperEllipse(200, 140 + oy, 68, 80);

  // Turban: a dome with folds, a wrapped band across the forehead, a jewel, and two plumes.
  paperEllipse(200, 70 + oy, 82, 58);
  stroke(200, 14 + oy, 146, 30 + oy, 126, 72 + oy);
  stroke(200, 14 + oy, 254, 30 + oy, 274, 72 + oy);
  stroke(200, 14 + oy, 170, 38 + oy, 162, 72 + oy);
  stroke(200, 14 + oy, 230, 38 + oy, 238, 72 + oy);
  paperEllipse(200, 88 + oy, 96, 18);
  stroke(112, 84 + oy, 150, 96 + oy, 184, 99 + oy);
  stroke(216, 76 + oy, 254, 78 + oy, 288, 90 + oy);
  stroke(203, 76 + oy, 190, 34 + oy, 226, 8 + oy, 4, 1);
  stroke(207, 76 + oy, 218, 40 + oy, 246, 20 + oy, 3, 1);
  g().drawTriangle(200, 74 + oy, 189, 88 + oy, 211, 88 + oy);
  g().drawTriangle(189, 88 + oy, 211, 88 + oy, 200, 102 + oy);
  g().setDrawColor(PAPER);
  g().drawLine(196, 83 + oy, 199, 80 + oy);
  g().setDrawColor(INK);

  oracleBrow(172, 116 + oy, e.left, -1);
  oracleBrow(228, 116 + oy, e.right, 1);
  oracleEye(face, 172, 132 + oy, e.lid, e.scale);
  oracleEye(face, 228, 132 + oy, e.lid, e.scale);

  // Hooked nose, mouth, handlebar moustache, and goatee.
  stroke(199, 130 + oy, 197, 158 + oy, 189, 167 + oy, 1);
  stroke(189, 167 + oy, 196, 175 + oy, 207, 168 + oy, 1);
  oracleMouth(face, oy);
  for (int side : {-1, 1}) {
    stroke(200 + side * 2, 182 + oy, 200 + side * 25, 176 + oy, 200 + side * 40, 186 + oy, 5, 2);
    stroke(200 + side * 40, 186 + oy, 200 + side * 52, 192 + oy, 200 + side * 50, 178 + oy, 2, 1);
  }
  g().drawTriangle(189, 208 + oy, 211, 208 + oy, 200, 238 + oy);

  crystalBall(face);
}

const char *defaultStatus(VoiceState state) {
  switch (state) {
    case VoiceState::Connecting: return "kobler til…";
    case VoiceState::Hearing: return "hører deg";
    case VoiceState::Thinking: return "tenker…";
    case VoiceState::Speaking: return "snakker";
    case VoiceState::Error: return "feil";
    default: return "lytter";
  }
}

}  // namespace

FaceKind faceFromName(const char *name) {
  return name && !strcmp(name, "oracle") ? FaceKind::Oracle : FaceKind::Waifu;
}

Mood moodFromName(const char *name) {
  const char *const names[] = {"neutral", "happy", "sad", "surprised", "angry", "sly"};
  for (int i = 0; i < 6; i++)
    if (name && !strcmp(name, names[i])) return (Mood)i;
  return Mood::Neutral;
}

void animateFace(Face &face, uint32_t now, float level) {
  face.ms = now;

  // Blink every 2-6 seconds, sometimes twice in a row. A blink lasts 160 ms.
  if (!face.nextBlink) face.nextBlink = now + random(2000, 6000);
  if (now >= face.nextBlink) {
    face.blinkStart = now;
    face.nextBlink = now + (random(5) == 0 ? 300 : random(2000, 6000));
  }
  float p = (now - face.blinkStart) / 160.0f;
  face.blink = p < 1 ? 1 - fabsf(2 * p - 1) : 0;

  // Glance around now and then. Look up while thinking and at the user while talking.
  if (face.state == VoiceState::Thinking) {
    face.targetX = -0.8f;
    face.targetY = -1;
  } else if (now >= face.nextLook) {
    bool attentive = face.state == VoiceState::Hearing || face.state == VoiceState::Speaking;
    face.nextLook = now + random(1200, 4000);
    face.targetX = attentive || random(3) == 0 ? 0 : random(-100, 101) / 100.0f;
    face.targetY = attentive ? 0 : random(-60, 61) / 100.0f;
  }
  face.lookX += (face.targetX - face.lookX) * 0.5f;
  face.lookY += (face.targetY - face.lookY) * 0.5f;

  // Open quickly, close a little slower, so the mouth does not flicker.
  float target = face.state == VoiceState::Speaking ? min(1.0f, level * 1.4f) : 0;
  face.mouth += (target - face.mouth) * (target > face.mouth ? 0.7f : 0.4f);
  face.bob = 2 * sinf(now / 700.0f);
}

void drawFace(const Face &face) {
  U8G2 &d = g();
  d.clearBuffer();
  d.setDrawColor(INK);
  if (face.kind == FaceKind::Oracle) drawOracle(face);
  else drawWaifu(face);

  // Name and status in the top corners, and a hint for the buttons.
  d.setDrawColor(INVERT);
  d.setFont(u8g2_font_helvB12_tf);
  d.drawUTF8(8, 18, face.name);
  d.setFont(u8g2_font_helvR10_tf);
  const char *status = face.status[0] ? face.status : defaultStatus(face.state);
  d.drawUTF8(392 - d.getUTF8Width(status), 16, status);
  d.setFont(u8g2_font_helvR08_tf);
  d.drawUTF8(8, 32, "BOOT: bytt");
  d.setDrawColor(INK);
  d.sendBuffer();
}
