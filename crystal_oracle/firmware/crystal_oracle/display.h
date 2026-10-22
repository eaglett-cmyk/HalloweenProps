#pragma once
// ============================================================================
//  display.h — 800x480 RGB565 panel (ST7262) driven directly with esp_lcd
//  (bundled in ESP32 Arduino core 3.3.x): THREE framebuffers in PSRAM + bounce
//  buffers in internal RAM => tear-free swaps; JPEG frames decode straight into
//  the hidden buffer. GFX Library for Arduino (Arduino_Canvas) is used only as a
//  drawing engine bound to whichever buffer is being drawn (text + shapes).
//
//  Timings (Waveshare 7C demo): pulse 4 / back porch 8 / front porch 8 both axes,
//  pclk_active_neg = 1, 16 MHz PCLK => 16e6 / (820*500) = 39.02 Hz refresh.
//  Clip frame rates that divide it evenly: 13 fps (every 3rd refresh), 19.5 fps.
//
//  Frame cycle (ONE drawing task only — the Arduino loop task on core 1):
//      uint16_t *fb = display::beginFrame();   // back buffer, gfx() bound to it
//      ... decode / draw ...
//      display::endFrame();                      // queued; shown at next frame start
// ============================================================================
#include <Arduino.h>
#include <U8g2lib.h>               // font data only — must precede Arduino_GFX_Library.h
#include <Arduino_GFX_Library.h>

namespace display {

constexpr int W = 800;
constexpr int H = 480;
constexpr uint32_t PCLK_HZ = 16000000;

// Powers the panel through the IO expander (board::lcdPower), creates the RGB
// panel (on a short-lived core-0 task so the bounce-buffer ISR lands on core 0),
// fills all three buffers black, waits two refreshes, then enables the backlight
// at `backlightPct`. Returns false if the panel could not be created.
bool begin(uint8_t backlightPct);
bool ok();

uint16_t *beginFrame();          // never returns the buffer on screen or the one queued
void endFrame();                 // queue the buffer from beginFrame(); waits only if a previous frame is still queued
Arduino_GFX &gfx();              // canvas bound to the current back buffer (valid between begin/endFrame)
uint16_t *shownBuffer();         // buffer currently being scanned out (read-only; e.g. copy as next background)
void waitRefresh(uint32_t timeoutMs = 50);   // block until the next frame-buffer-complete interrupt
float refreshHz();
void setPclk(uint32_t hz);       // runtime PCLK change (e.g. 12 MHz if the image ever drifts)
uint32_t pclkHz();               // the PCLK in use (config.ini pclk_mhz is applied at boot)
uint32_t presentedFrames();

// ---- colour helpers ----
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
inline uint16_t rgb565(uint32_t rgb888) {
  return rgb565((uint8_t)(rgb888 >> 16), (uint8_t)(rgb888 >> 8), (uint8_t)rgb888);
}
uint32_t scaleRgb(uint32_t rgb888, uint8_t level);          // level 0..255
uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t alpha); // alpha 0..255 (fg weight)

// ---- raw-buffer operations on the CURRENT back buffer ----
void fill(uint16_t c);
void copyFrom(const uint16_t *src);                           // full-frame memcpy into the back buffer
void darkenRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t keep);  // multiply by keep/255 (caption band)
void addGlow(int16_t cx, int16_t cy, int16_t radius, uint32_t rgb888, uint8_t intensity);  // additive radial glow, clipped
void blitRgb565(const uint16_t *src, int16_t x, int16_t y, int16_t w, int16_t h);            // opaque sprite
void blitRgb565A8(const uint16_t *src, const uint8_t *alpha, int16_t x, int16_t y, int16_t w, int16_t h);  // alpha sprite

// ---- text (u8g2 fonts via the canvas; transparent background) ----
// Draws `s` horizontally centred on cx with its baseline at `baseline`, with an
// `outlinePx` outline (0 = none). Returns the drawn width in pixels.
int16_t textCentered(const char *s, int16_t cx, int16_t baseline, const uint8_t *u8g2Font,
                     uint16_t fill, uint16_t outline, uint8_t outlinePx);
int16_t textWidth(const char *s, const uint8_t *u8g2Font);

}  // namespace display
