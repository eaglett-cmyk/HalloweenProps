#pragma once
// ============================================================================
//  touch.h — GT911 capacitive touch (I2C 0x5D, or 0x14), INT = GPIO4,
//  RST = expander EXIO1. Minimal polled driver (no ISR, no config rewrite).
//  Reset/address-select (Waveshare 06_TOUCH timing): INT output low, RST low
//  100 ms, INT low 100 ms, RST high, 200 ms, then INT released to input.
//  Coordinates are native 800x480 landscape (Waveshare uses no swap/mirror);
//  begin() reads the GT911 config (0x8048..0x804B) and scales if it differs.
//
//  Touch is ALWAYS optional in the show (gloves don't work on capacitive glass);
//  it's used for the bonus "charge" taps and the host's service menu.
// ============================================================================
#include <Arduino.h>

namespace touch {

struct Point { int16_t x, y; };

bool begin();
bool ok();

// Call once per frame. Polls the controller (one or two I2C transactions) and
// derives simple gestures. Reports with >= 3 simultaneous points are ignored
// (rain drops / a whole palm).
struct Gestures {
  bool    down = false;       // a finger is on the glass now
  Point   at = {0, 0};        // current (or last) primary point
  bool    tap = false;        // a tap completed THIS frame (down->up < 450 ms, moved < 40 px)
  Point   tapAt = {0, 0};
  bool    pressStart = false; // a new touch began THIS frame
  bool    longPress = false;  // held >= longPressMs without moving (fires once per press)
  Point   longAt = {0, 0};
  uint32_t heldMs = 0;        // how long the current touch has been held
};
const Gestures &update(uint32_t longPressMs = 2500);

// True if p is inside the rectangle.
inline bool hit(const Point &p, int16_t x, int16_t y, int16_t w, int16_t h) {
  return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
}

}  // namespace touch
