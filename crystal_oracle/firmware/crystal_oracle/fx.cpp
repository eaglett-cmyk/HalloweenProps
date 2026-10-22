/*
 * ============================================================================
 *  fx.cpp — live procedural visuals for the Crystal Oracle screen
 * ----------------------------------------------------------------------------
 *  Implements fx.h: everything drawn on top of (or instead of) the SD-card
 *  clips — the fallback "orb" backgrounds, glows, rings, sparkles, the crystal
 *  meter, the place-your-crystal hint, bowl icons, the big number, titles and
 *  the caption band; and the story overlays a story picks with its captions.ini
 *  words: the communal meter (a Moon, or the glow of a lantern painted into
 *  the art), the trick smudge cloud, the gag poof, the absorb light beam, the
 *  replay check mark.
 *
 *  Look: a dark, spooky-cute screen read from 1-3 m away. Deep violet/navy
 *  vignette, soft additive glows in the crystal's colour, faceted gems, chunky
 *  outlined text (u8g2 "fub" / "logisoso" fonts) with a deep violet-black
 *  outline so words stay readable over any clip.
 *
 *  Cost: every call draws into the CURRENT back buffer (display::beginFrame()
 *  .. endFrame()) and is cheap enough for 13-20 fps:
 *    * the vignette is computed ONCE (first use) into an 800x480 PSRAM buffer
 *      and memcpy'd per frame;
 *    * glows/rings are additive with per-call colour ramps (no per-pixel
 *      float), ordered-dithered so gradients don't band in RGB565;
 *    * sparkles = a fixed pool of 64 particles in internal RAM.
 *  Outside a frame (or if the display failed) every function is a no-op:
 *  the canvas then has no framebuffer and the raw helpers return early.
 *
 *  Direct pixel access: display.cpp's gfx() IS an Arduino_Canvas whose
 *  framebuffer is the current back buffer (nullptr outside a frame), so
 *  Arduino_Canvas::getFramebuffer() gives us the buffer to add light into.
 * ============================================================================
 */
#include "fx.h"
#include "display.h"
#include <math.h>
#include <string.h>
#include <strings.h>
#include "esp_heap_caps.h"
#include "esp_random.h"

namespace fx {

namespace {

constexpr int W = display::W;
constexpr int H = display::H;
constexpr float TWO_PI_F = 6.28318531f;

// Every text outline: deep violet-black (reads as "black" but softer on the purple palette).
constexpr uint16_t OUTLINE = display::rgb565(14, 6, 24);
constexpr uint32_t WHITE = 0xFFFFFF;

constexpr uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// ---- small helpers -------------------------------------------------------------
inline uint16_t *backFb() {
  // display::gfx() is display.cpp's Arduino_Canvas bound to the back buffer.
  return static_cast<Arduino_Canvas &>(display::gfx()).getFramebuffer();
}

inline uint16_t c565(uint32_t rgb) { return display::rgb565(rgb); }

inline uint8_t clamp8(float v) { return v <= 0.0f ? 0 : (v >= 255.0f ? 255 : (uint8_t)(v + 0.5f)); }

inline int16_t clampCoord(float v) {
  if (v < -4000.0f) return -4000;
  if (v > 4000.0f) return 4000;
  return (int16_t)lroundf(v);
}

// a + (b - a) * t/255, per channel.
uint32_t mixRgb(uint32_t a, uint32_t b, uint8_t t) {
  uint32_t out = 0;
  for (int sh = 0; sh <= 16; sh += 8) {
    const int32_t ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
    const int32_t c = ca + ((cb - ca) * (int32_t)t + 127) / 255;
    out |= (uint32_t)c << sh;
  }
  return out;
}

// 0..1 sine wave with the given period (ms) and phase (radians).
inline float wave(uint32_t tMs, uint32_t periodMs, float phase = 0.0f) {
  if (periodMs == 0) periodMs = 1;
  return 0.5f + 0.5f * sinf(TWO_PI_F * (float)(tMs % periodMs) / (float)periodMs + phase);
}

// Cheap PRNG (xorshift32), seeded from the hardware RNG on first use.
uint32_t s_rng = 0;
uint32_t rnd() {
  if (s_rng == 0) s_rng = esp_random() | 1u;
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 17;
  s_rng ^= s_rng << 5;
  return s_rng;
}
inline float rndf() { return (rnd() >> 8) * (1.0f / 16777216.0f); }   // [0,1)

// Deterministic per-index hash (motes keep their "personality" frame to frame).
inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

// Saturating additive pixel (8-bit light amounts), clipped.
inline void addPix(uint16_t *fb, int32_t x, int32_t y, uint32_t r8, uint32_t g8, uint32_t b8) {
  if ((uint32_t)x >= (uint32_t)W || (uint32_t)y >= (uint32_t)H) return;
  uint16_t &p = fb[(size_t)y * W + x];
  uint32_t r = (p >> 11) + ((r8 + 4) >> 3);
  uint32_t g = ((p >> 5) & 0x3F) + ((g8 + 2) >> 2);
  uint32_t b = (p & 0x1F) + ((b8 + 4) >> 3);
  if (r > 31) r = 31;
  if (g > 63) g = 63;
  if (b > 31) b = 31;
  p = (uint16_t)((r << 11) | (g << 5) | b);
}

// Additive horizontal span [xa, xb] on row y (8-bit light amounts, dithered).
void addSpan(uint16_t *fb, int32_t y, int32_t xa, int32_t xb, uint32_t r8, uint32_t g8, uint32_t b8) {
  if ((uint32_t)y >= (uint32_t)H) return;
  if (xa < 0) xa = 0;
  if (xb > W - 1) xb = W - 1;
  if (xa > xb) return;
  uint16_t *row = fb + (size_t)y * W;
  const uint8_t *bay = BAYER4[y & 3];
  for (int32_t x = xa; x <= xb; x++) {
    const uint32_t d = bay[x & 3];
    const uint32_t ar = (r8 + (d >> 1)) >> 3, ag = (g8 + (d >> 2)) >> 2, ab = (b8 + (d >> 1)) >> 3;
    const uint32_t p = row[x];
    uint32_t r = (p >> 11) + ar, g = ((p >> 5) & 0x3F) + ag, b = (p & 0x1F) + ab;
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    row[x] = (uint16_t)((r << 11) | (g << 5) | b);
  }
}

// Additive filled triangle (scanline), used for glowing arrows.
void addTriangle(uint16_t *fb, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgb) {
  // sort by y
  if (y1 < y0) { float t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
  if (y2 < y0) { float t = y0; y0 = y2; y2 = t; t = x0; x0 = x2; x2 = t; }
  if (y2 < y1) { float t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
  if (y2 - y0 < 0.5f) return;
  const uint32_t r8 = (rgb >> 16) & 0xFF, g8 = (rgb >> 8) & 0xFF, b8 = rgb & 0xFF;
  int32_t ys = (int32_t)ceilf(y0), ye = (int32_t)floorf(y2);
  if (ys < 0) ys = 0;
  if (ye > H - 1) ye = H - 1;
  for (int32_t y = ys; y <= ye; y++) {
    const float fy = (float)y;
    const float xl = x0 + (x2 - x0) * (fy - y0) / (y2 - y0);   // long edge
    float xr;
    if (fy < y1) xr = (y1 - y0) > 0.001f ? x0 + (x1 - x0) * (fy - y0) / (y1 - y0) : x1;
    else xr = (y2 - y1) > 0.001f ? x1 + (x2 - x1) * (fy - y1) / (y2 - y1) : x1;
    const float a = xl < xr ? xl : xr, b = xl < xr ? xr : xl;
    addSpan(fb, y, (int32_t)ceilf(a), (int32_t)floorf(b), r8, g8, b8);
  }
}

// Additive soft annulus: brightest at radius r, fading to zero at r +- thick/2
// (parabolic profile). Per-call 128-step ramp indexed by d^2, so no sqrt per pixel.
void annulusAdd(uint16_t *fb, int32_t cx, int32_t cy, int32_t r, int32_t thick, uint32_t rgb, uint8_t level) {
  if (!fb || r <= 0 || level == 0) return;
  if (thick < 2) thick = 2;
  if (r > 1500) r = 1500;
  const float half = thick * 0.5f;
  const float ro = r + half, ri = (r - half) > 0.0f ? (r - half) : 0.0f;
  const int32_t ro2 = (int32_t)(ro * ro), ri2 = (int32_t)(ri * ri);
  if (ro2 <= ri2) return;
  const int32_t roI = (int32_t)ceilf(ro);
  if (cx + roI < 0 || cx - roI >= W || cy + roI < 0 || cy - roI >= H) return;

  constexpr int STEPS = 128;
  uint8_t addR[STEPS + 1], addG[STEPS + 1], addB[STEPS + 1];
  const float R = (float)((rgb >> 16) & 0xFF), G = (float)((rgb >> 8) & 0xFF), B = (float)(rgb & 0xFF);
  const float lv = level / 255.0f;
  for (int k = 0; k <= STEPS; k++) {
    const float d = sqrtf((float)ri2 + (float)(ro2 - ri2) * k / STEPS);
    float u = fabsf(d - r) / half;
    float f = u >= 1.0f ? 0.0f : 1.0f - u * u;
    f *= lv;
    addR[k] = clamp8(R * f);
    addG[k] = clamp8(G * f);
    addB[k] = clamp8(B * f);
  }
  const uint32_t inv = ((uint32_t)STEPS << 16) / (uint32_t)(ro2 - ri2);

  int32_t y0 = cy - roI, y1 = cy + roI;
  if (y0 < 0) y0 = 0;
  if (y1 > H - 1) y1 = H - 1;
  for (int32_t y = y0; y <= y1; y++) {
    const int32_t dy = y - cy, dy2 = dy * dy;
    if (dy2 > ro2) continue;
    const int32_t so = (int32_t)sqrtf((float)(ro2 - dy2));
    const int32_t si = dy2 < ri2 ? (int32_t)sqrtf((float)(ri2 - dy2)) : -1;
    uint16_t *row = fb + (size_t)y * W;
    const uint8_t *bay = BAYER4[y & 3];
    // one or two segments: [cx-so, cx-si] and [cx+si, cx+so]
    for (int seg = 0; seg < 2; seg++) {
      int32_t xa, xb;
      if (si < 0) {
        if (seg) break;
        xa = cx - so;
        xb = cx + so;
      } else if (seg == 0) {
        xa = cx - so;
        xb = cx - si;
      } else {
        xa = cx + si;
        xb = cx + so;
      }
      if (xa < 0) xa = 0;
      if (xb > W - 1) xb = W - 1;
      for (int32_t x = xa; x <= xb; x++) {
        const int32_t dx = x - cx;
        const int32_t d2 = dx * dx + dy2;
        if (d2 < ri2 || d2 > ro2) continue;
        uint32_t idx = ((uint32_t)(d2 - ri2) * inv) >> 16;
        if (idx > STEPS) idx = STEPS;
        const uint32_t d = bay[x & 3];
        const uint32_t ar = (addR[idx] + (d >> 1)) >> 3, ag = (addG[idx] + (d >> 2)) >> 2, ab = (addB[idx] + (d >> 1)) >> 3;
        if ((ar | ag | ab) == 0) continue;
        const uint32_t p = row[x];
        uint32_t pr = (p >> 11) + ar, pg = ((p >> 5) & 0x3F) + ag, pb = (p & 0x1F) + ab;
        if (pr > 31) pr = 31;
        if (pg > 63) pg = 63;
        if (pb > 31) pb = 31;
        row[x] = (uint16_t)((pr << 11) | (pg << 5) | pb);
      }
    }
  }
}

// ---- the static vignette (computed once into PSRAM) ------------------------------
uint16_t *s_vignette = nullptr;
bool s_vignetteTried = false;

const uint16_t *vignette() {
  if (s_vignette || s_vignetteTried) return s_vignette;
  s_vignetteTried = true;
  s_vignette = (uint16_t *)heap_caps_malloc((size_t)W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_vignette) {
    Serial.println("[fx] no PSRAM for the vignette - using a flat background");
    return nullptr;
  }
  const uint32_t t0 = millis();
  // Colour ramp over the squared elliptical distance from the orb centre:
  // violet glow in the middle -> deep navy -> almost black corners.
  constexpr int RAMP = 256;
  constexpr float T_MAX = 2.6f;               // corners reach ~2.4
  uint8_t rr[RAMP], gg[RAMP], bb[RAMP];
  for (int i = 0; i < RAMP; i++) {
    const float t = T_MAX * i / (RAMP - 1);
    // smooth falloff: 1 at the centre, ~0 at the corners
    const float f = 1.0f / (1.0f + 1.9f * t * t);
    // centre (34,16,62)  edge (3,3,12)
    rr[i] = clamp8(3.0f + 31.0f * f);
    gg[i] = clamp8(3.0f + 13.0f * f);
    bb[i] = clamp8(12.0f + 50.0f * f);
  }
  for (int y = 0; y < H; y++) {
    const float ny = (y - 225) / 300.0f;
    const float topTint = y < 200 ? (200 - y) / 200.0f : 0.0f;   // a whisper of navy sky at the top
    const uint8_t *bay = BAYER4[y & 3];
    uint16_t *row = s_vignette + (size_t)y * W;
    for (int x = 0; x < W; x++) {
      const float nx = (x - 400) / 430.0f;
      int idx = (int)((nx * nx + ny * ny) * ((RAMP - 1) / T_MAX));
      if (idx > RAMP - 1) idx = RAMP - 1;
      const uint32_t d = bay[x & 3];
      const uint32_t r8 = rr[idx];
      const uint32_t g8 = gg[idx] + (uint32_t)(4.0f * topTint);
      const uint32_t b8 = bb[idx] + (uint32_t)(10.0f * topTint);
      uint32_t r5 = (r8 + (d >> 1)) >> 3, g6 = (g8 + (d >> 2)) >> 2, b5 = (b8 + (d >> 1)) >> 3;
      if (r5 > 31) r5 = 31;
      if (g6 > 63) g6 = 63;
      if (b5 > 31) b5 = 31;
      row[x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
    }
  }
  Serial.printf("[fx] vignette ready in %lu ms\n", (unsigned long)(millis() - t0));
  return s_vignette;
}

// Vignette + aura for the last colour, so the big aura glow is drawn once per colour.
uint16_t *s_orbBase = nullptr;
bool s_orbBaseTried = false;
uint32_t s_orbBaseKey = 0xFFFFFFFFu;

// ---- gems -------------------------------------------------------------------------
// A faceted crystal: pointed crown, straight sides, longer pointed base.
// Lit = colour facets + bright edges; unlit = near-black body with a dim edge.
void drawGem(int16_t gx, int16_t gy, float w, float h, uint32_t rgb, bool lit) {
  Arduino_GFX &g = display::gfx();
  const float hw = w * 0.5f;
  const int16_t tx = gx, ty = clampCoord(gy - h * 0.50f);       // crown tip
  const int16_t sy = clampCoord(gy - h * 0.18f);                // upper shoulders
  const int16_t ly = clampCoord(gy + h * 0.20f);                // lower shoulders
  const int16_t by = clampCoord(gy + h * 0.50f);                // base tip
  const int16_t lx = clampCoord(gx - hw), rx = clampCoord(gx + hw);
  const int16_t cy = clampCoord(gy - h * 0.04f);                // facet meeting point on the centre line

  uint16_t crownL, crownR, faceL, faceR, edge;
  if (lit) {
    crownL = c565(mixRgb(rgb, WHITE, 165));
    crownR = c565(mixRgb(rgb, WHITE, 80));
    faceL = c565(rgb);
    faceR = c565(display::scaleRgb(rgb, 150));
    edge = c565(mixRgb(rgb, WHITE, 210));
  } else {
    crownL = c565(display::scaleRgb(rgb, 46));
    crownR = c565(display::scaleRgb(rgb, 34));
    faceL = c565(display::scaleRgb(rgb, 26));
    faceR = c565(display::scaleRgb(rgb, 18));
    edge = c565(display::scaleRgb(rgb, 105));
  }
  // body faces (left lit, right shaded)
  g.fillTriangle(lx, sy, lx, ly, tx, cy, faceL);
  g.fillTriangle(lx, ly, tx, by, tx, cy, faceL);
  g.fillTriangle(rx, sy, rx, ly, tx, cy, faceR);
  g.fillTriangle(rx, ly, tx, by, tx, cy, faceR);
  // crown facets
  g.fillTriangle(tx, ty, lx, sy, tx, cy, crownL);
  g.fillTriangle(tx, ty, rx, sy, tx, cy, crownR);
  // outline
  g.drawLine(tx, ty, rx, sy, edge);
  g.drawLine(rx, sy, rx, ly, edge);
  g.drawLine(rx, ly, tx, by, edge);
  g.drawLine(tx, by, lx, ly, edge);
  g.drawLine(lx, ly, lx, sy, edge);
  g.drawLine(lx, sy, tx, ty, edge);
  if (lit && w >= 18) {
    // inner facet lines + a tiny specular glint
    const uint16_t facet = c565(mixRgb(rgb, WHITE, 120));
    g.drawLine(lx, sy, tx, cy, facet);
    g.drawLine(rx, sy, tx, cy, facet);
    g.drawLine(tx, cy, tx, by, facet);
    uint16_t *fb = backFb();
    if (fb) {
      const int32_t sx = gx - (int32_t)(w * 0.22f), syy = gy - (int32_t)(h * 0.22f);
      addPix(fb, sx, syy, 255, 255, 255);
      addPix(fb, sx - 1, syy, 120, 120, 120);
      addPix(fb, sx + 1, syy, 120, 120, 120);
      addPix(fb, sx, syy - 1, 120, 120, 120);
      addPix(fb, sx, syy + 1, 120, 120, 120);
    }
  }
}

// ---- bowl shapes ------------------------------------------------------------------
void fillStar(int16_t cx, int16_t cy, float ro, float ri, uint16_t color) {
  Arduino_GFX &g = display::gfx();
  int16_t px[10], py[10];
  for (int k = 0; k < 10; k++) {
    const float a = -TWO_PI_F / 4.0f + k * (TWO_PI_F / 10.0f);   // first point straight up
    const float rad = (k & 1) ? ri : ro;
    px[k] = clampCoord(cx + rad * cosf(a));
    py[k] = clampCoord(cy + rad * sinf(a));
  }
  for (int k = 0; k < 10; k++) {
    const int n = (k + 1) % 10;
    g.fillTriangle(cx, cy, px[k], py[k], px[n], py[n], color);
  }
}

void fillUpTriangle(int16_t cx, int16_t cy, float circR, uint16_t color) {
  // equilateral, pointing up, centroid at (cx, cy)
  const float s = circR * 0.8660254f;   // sin 60
  display::gfx().fillTriangle(cx, clampCoord(cy - circR), clampCoord(cx - s), clampCoord(cy + circR * 0.5f),
                              clampCoord(cx + s), clampCoord(cy + circR * 0.5f), color);
}

// ---- text with a font that fits -------------------------------------------------
// Picks the first font whose width fits maxW and draws it outlined & centred.
void fittedText(const char *s, int16_t cx, int16_t baseline, uint32_t rgb, const uint8_t *const *fonts,
                const uint8_t *outlines, int nFonts, int16_t maxW) {
  if (!s || !*s) return;
  int pick = nFonts - 1;
  for (int i = 0; i < nFonts; i++) {
    if (display::textWidth(s, fonts[i]) + 2 * outlines[i] <= maxW) {
      pick = i;
      break;
    }
  }
  display::textCentered(s, cx, baseline, fonts[pick], c565(rgb), OUTLINE, outlines[pick]);
}

// ---- sparkles ---------------------------------------------------------------------
struct Spark {
  float x, y, vx, vy;      // px, px per ms
  uint16_t age, life;      // ms
  uint8_t r, g, b;
  uint8_t size;            // arm length 1..4
  uint8_t ambient;         // 1 = slow twinkling drift (no drag/gravity)
  uint8_t phase;           // twinkle phase
  bool active;
};
constexpr int MAX_SPARKS = 64;
Spark s_sparks[MAX_SPARKS];          // ~2 KB, internal RAM (.bss)
float s_ambientAcc = 0.0f;
uint32_t s_ambientLastMs = 0;

Spark *allocSpark() {
  int best = 0;
  int32_t bestLeft = INT32_MAX;
  for (int i = 0; i < MAX_SPARKS; i++) {
    if (!s_sparks[i].active) return &s_sparks[i];
    const int32_t left = (int32_t)s_sparks[i].life - s_sparks[i].age;
    if (left < bestLeft) {
      bestLeft = left;
      best = i;
    }
  }
  return &s_sparks[best];   // pool full: recycle the one closest to dying
}

void setSparkColour(Spark *s, uint32_t rgb) {
  s->r = (rgb >> 16) & 0xFF;
  s->g = (rgb >> 8) & 0xFF;
  s->b = rgb & 0xFF;
}

void drawSparkle(uint16_t *fb, float fx, float fy, uint8_t size, uint8_t r, uint8_t g, uint8_t b, uint32_t level) {
  if (level == 0) return;
  const int32_t x = (int32_t)lroundf(fx), y = (int32_t)lroundf(fy);
  if (x < -8 || x >= W + 8 || y < -8 || y >= H + 8) return;
  const uint32_t r8 = r * level / 255, g8 = g * level / 255, b8 = b * level / 255;
  // soft halo for the bigger ones
  if (size >= 2) display::addGlow((int16_t)x, (int16_t)y, (int16_t)(size * 3), ((uint32_t)r << 16) | ((uint32_t)g << 8) | b, (uint8_t)(level / 3));
  // white-hot centre (colour pulled towards white), coloured arms fading outwards (a 4-point star)
  auto hot = [level](uint32_t c8) -> uint32_t {
    const uint32_t v = (c8 + level) / 2 + 40;
    return v > 255 ? 255 : v;
  };
  addPix(fb, x, y, hot(r8), hot(g8), hot(b8));
  for (int a = 1; a <= size; a++) {
    const uint32_t k = (uint32_t)(size + 1 - a) * 255 / (size + 1);
    const uint32_t ar = r8 * k / 255, ag = g8 * k / 255, ab = b8 * k / 255;
    addPix(fb, x - a, y, ar, ag, ab);
    addPix(fb, x + a, y, ar, ag, ab);
    addPix(fb, x, y - a, ar, ag, ab);
    addPix(fb, x, y + a, ar, ag, ab);
  }
  if (size >= 3) {
    const uint32_t ar = r8 * 2 / 5, ag = g8 * 2 / 5, ab = b8 * 2 / 5;
    addPix(fb, x - 1, y - 1, ar, ag, ab);
    addPix(fb, x + 1, y - 1, ar, ag, ab);
    addPix(fb, x - 1, y + 1, ar, ag, ab);
    addPix(fb, x + 1, y + 1, ar, ag, ab);
  }
}

// "1,234 crystals awakened" (thousands separators) into out.
void counterText(char *out, size_t n, uint32_t count, const char *label) {
  char digits[12], num[16];
  snprintf(digits, sizeof(digits), "%lu", (unsigned long)count);
  const int len = (int)strlen(digits);
  int o = 0;
  for (int i = 0; i < len && o < (int)sizeof(num) - 1; i++) {
    if (i > 0 && (len - i) % 3 == 0 && o < (int)sizeof(num) - 2) num[o++] = ',';
    num[o++] = digits[i];
  }
  num[o] = 0;
  if (label && *label) snprintf(out, n, "%s %s", num, label);
  else snprintf(out, n, "%s", num);
}

// Soft opaque-ish disc: blends rgb over the frame with full `alpha` (0..255)
// out to ~60 % of the radius, fading to nothing at the edge. vScale squashes it
// vertically (1 = round). Used for the smudge cloud's puffs.
void softDisc(uint16_t *fb, float cx, float cy, float r, float vScale, uint32_t rgb, uint8_t alpha) {
  if (!fb || r < 1.0f || alpha == 0) return;
  if (vScale < 0.2f) vScale = 0.2f;
  const float ry = r * vScale;
  const int32_t y0 = (int32_t)floorf(cy - ry), y1 = (int32_t)ceilf(cy + ry);
  const int32_t x0 = (int32_t)floorf(cx - r), x1 = (int32_t)ceilf(cx + r);
  if (x1 < 0 || x0 >= W || y1 < 0 || y0 >= H) return;
  const uint16_t col = c565(rgb);
  const float inv2 = 1.0f / (r * r);
  const float vs2 = 1.0f / (vScale * vScale);
  for (int32_t y = y0 < 0 ? 0 : y0; y <= y1 && y < H; y++) {
    const float dy = (float)y - cy;
    const float dy2 = dy * dy * vs2;
    uint16_t *row = fb + (size_t)y * W;
    for (int32_t x = x0 < 0 ? 0 : x0; x <= x1 && x < W; x++) {
      const float dx = (float)x - cx;
      const float q = (dx * dx + dy2) * inv2;   // 0 centre .. 1 edge
      if (q >= 1.0f) continue;
      float a = (1.0f - q) * 1.6f;
      if (a > 1.0f) a = 1.0f;
      row[x] = display::blend565(row[x], col, (uint8_t)(a * alpha));
    }
  }
}

// Thick line as two triangles (+ round caps), for the check mark.
void thickLine(float x0, float y0, float x1, float y1, float w, uint16_t col) {
  Arduino_GFX &g = display::gfx();
  const float dx = x1 - x0, dy = y1 - y0;
  const float len = sqrtf(dx * dx + dy * dy);
  if (len < 0.5f) return;
  const float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
  const int16_t ax = clampCoord(x0 + nx), ay = clampCoord(y0 + ny), bx = clampCoord(x0 - nx), by = clampCoord(y0 - ny);
  const int16_t cx = clampCoord(x1 - nx), cy = clampCoord(y1 - ny), ex = clampCoord(x1 + nx), ey = clampCoord(y1 + ny);
  g.fillTriangle(ax, ay, bx, by, cx, cy, col);
  g.fillTriangle(ax, ay, cx, cy, ex, ey, col);
  const int16_t rr = (int16_t)(w * 0.5f);
  g.fillCircle(clampCoord(x0), clampCoord(y0), rr, col);
  g.fillCircle(clampCoord(x1), clampCoord(y1), rr, col);
}

}  // namespace

// ============================================================================
//  names
// ============================================================================
Shape shapeFromName(const char *name) {
  if (!name) return Shape::Circle;
  while (*name == ' ' || *name == '\t') name++;
  if (strncasecmp(name, "tri", 3) == 0) return Shape::Triangle;
  if (strncasecmp(name, "star", 4) == 0) return Shape::Star;
  return Shape::Circle;   // "circle" and anything unknown
}

// ============================================================================
//  whole-frame procedural visuals
// ============================================================================
void orbBackground(uint32_t tMs, uint32_t rgb, uint8_t energy, uint32_t auraRgb) {
  uint16_t *fb = backFb();
  if (!fb) return;
  const int16_t ox = 400, oy = 220;   // orb centre (slightly above the middle: captions live below)

  // 1) Static part = vignette + the wide, soft aura in this colour. The aura
  //    covers most of the screen (expensive), so it is rendered once per
  //    colour/energy step into a cached PSRAM frame and memcpy'd afterwards.
  const uint8_t eStep = energy >> 5;                                  // 0..7
  const uint32_t aura = auraRgb ? auraRgb : rgb;
  const uint32_t key = (aura & 0xFFFFFF) | ((uint32_t)eStep << 24);
  if (s_orbBase && key == s_orbBaseKey) {
    display::copyFrom(s_orbBase);
  } else {
    const uint16_t *v = vignette();
    if (v) display::copyFrom(v);
    else display::fill(display::rgb565(10, 6, 26));
    const float es = eStep / 7.0f;
    display::addGlow(ox, oy, (int16_t)(300.0f + 50.0f * es), aura, clamp8(42.0f + 34.0f * es));
    if (!s_orbBase && !s_orbBaseTried) {
      s_orbBaseTried = true;
      s_orbBase = (uint16_t *)heap_caps_malloc((size_t)W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!s_orbBase) Serial.println("[fx] no PSRAM for the orb cache - aura drawn every frame");
    }
    if (s_orbBase) {
      memcpy(s_orbBase, fb, (size_t)W * H * sizeof(uint16_t));
      s_orbBaseKey = key;
    }
  }

  // 2) The breathing orb body + hot core (smaller, so cheap enough per frame).
  const float e = energy / 255.0f;
  const float breath = wave(tMs, (uint32_t)(4200.0f - 2000.0f * e));   // calm = slow, charged = quicker
  const float R = 95.0f + 35.0f * e + 16.0f * breath;
  display::addGlow(ox, oy, (int16_t)R, rgb, clamp8(100.0f + 70.0f * e + 50.0f * breath));
  display::addGlow(ox, oy, (int16_t)(R * 0.45f), mixRgb(rgb, WHITE, 150), clamp8(60.0f + 60.0f * e + 70.0f * breath));

  // 3) Drifting motes, each with a fixed "personality" from its index hash.
  const int motes = 10 + (int)(10.0f * e);
  const uint32_t moteCol = mixRgb(rgb, WHITE, 120);
  for (int i = 0; i < motes; i++) {
    const uint32_t hsh = hash32(0x9E3779B9u * (uint32_t)(i + 1));
    const uint32_t speed = 9 + hsh % 15 + (uint32_t)(14.0f * e);        // px per second, upwards
    const uint32_t start = (hsh >> 5) % 560;
    // integer time maths so hours of uptime don't lose float precision
    const float travel = (float)(((uint64_t)tMs * speed + (uint64_t)start * 1000u) % 560000u) / 1000.0f;
    const float y = 500.0f - travel;                                    // 500 -> -60, then wraps
    const float amp = 8.0f + (float)((hsh >> 9) % 24);
    const uint32_t swayMs = 2600 + ((hsh >> 14) % 3200);
    const float x = (float)(hsh % W) + amp * sinf(TWO_PI_F * (float)(tMs % swayMs) / (float)swayMs + (float)i);
    const float tw = wave(tMs, 900 + (hsh >> 20) % 900, (float)i);
    const uint8_t lvl = clamp8((60.0f + 90.0f * tw) * (0.7f + 0.3f * e));
    display::addGlow(clampCoord(x), clampCoord(y), (int16_t)(5 + (hsh >> 24) % 5), moteCol, lvl);
    addPix(fb, (int32_t)lroundf(x), (int32_t)lroundf(y), lvl, lvl, lvl);
  }
}

void fallbackCue(const char *titleText, uint32_t tMs, uint32_t rgb, uint8_t energy, uint32_t auraRgb) {
  orbBackground(tMs, rgb, energy, auraRgb);
  if (titleText && *titleText) title(titleText, W / 2, 236, mixRgb(rgb, WHITE, 200));
}

// ============================================================================
//  overlays
// ============================================================================
void glowPulse(int16_t cx, int16_t cy, int16_t r, uint32_t rgb, uint32_t tMs, uint16_t periodMs) {
  if (r <= 0) return;
  const float w = wave(tMs, periodMs ? periodMs : 1600);
  display::addGlow(cx, cy, (int16_t)(r * (0.88f + 0.12f * w)), rgb, clamp8(70.0f + 150.0f * w));
}

void ring(int16_t cx, int16_t cy, int16_t r, int16_t thick, uint32_t rgb, uint8_t level) {
  annulusAdd(backFb(), cx, cy, r, thick, rgb, level);
}

void burstRing(int16_t cx, int16_t cy, uint32_t rgb, uint32_t sinceMs, uint32_t durMs) {
  if (durMs == 0 || sinceMs >= durMs) return;
  uint16_t *fb = backFb();
  if (!fb) return;
  const float p = (float)sinceMs / (float)durMs;            // 0..1
  const float q = 1.0f - p;
  const float ease = 1.0f - q * q * q;                       // fast out, slow in
  const float r = 24.0f + 300.0f * ease;
  const float thick = 6.0f + 28.0f * q;
  const uint8_t lvl = clamp8(255.0f * powf(q, 1.3f));
  // initial flash
  if (p < 0.3f) display::addGlow(cx, cy, (int16_t)(90.0f + 140.0f * p), mixRgb(rgb, WHITE, 140), clamp8(200.0f * (1.0f - p / 0.3f)));
  annulusAdd(fb, cx, cy, (int32_t)r, (int32_t)thick, rgb, lvl);
  // a thinner, whiter echo inside the main ring
  if (p < 0.65f) annulusAdd(fb, cx, cy, (int32_t)(r * 0.62f), (int32_t)(thick * 0.5f), mixRgb(rgb, WHITE, 150), (uint8_t)(lvl * 6 / 10));
}

void crystalMeter(int16_t cx, int16_t cy, uint8_t filled, uint8_t total, uint32_t rgb, uint32_t tMs, bool pulseNewest) {
  if (total == 0 || !backFb()) return;
  if (total > 16) total = 16;
  if (filled > total) filled = total;
  float spacing = 52.0f;
  if (total * spacing > 720.0f) spacing = 720.0f / total;
  const float gw = spacing * 0.5f, gh = gw * 1.55f;
  const float mid = (total - 1) * 0.5f;
  const float pulse = wave(tMs, 900);

  int16_t gx[16], gy[16];
  for (int i = 0; i < total; i++) {
    const float u = mid > 0.0f ? (i - mid) / mid : 0.0f;   // -1..1
    gx[i] = clampCoord(cx + (i - mid) * spacing);
    gy[i] = clampCoord(cy - 12.0f * u * u);                 // gentle arc: ends a little higher
  }
  // glows first, so no glow paints over a neighbouring gem
  for (int i = 0; i < filled; i++) {
    const bool newest = pulseNewest && i == filled - 1;
    const float k = newest ? (0.8f + 0.6f * pulse) : 1.0f;
    display::addGlow(gx[i], gy[i], (int16_t)(gh * 0.95f * (newest ? 1.25f : 1.0f)), rgb, clamp8(70.0f * k));
  }
  for (int i = 0; i < total; i++) {
    const bool lit = i < filled;
    const bool newest = lit && pulseNewest && i == filled - 1;
    const float s = newest ? 1.0f + 0.14f * pulse : 1.0f;
    drawGem(gx[i], gy[i], gw * s, gh * s, rgb, lit);
  }
}

void placeHint(uint32_t tMs, uint32_t rgb) { placeHintAt(W / 2, tMs, rgb); }

void placeHintAt(int16_t cx, uint32_t tMs, uint32_t rgb) {
  uint16_t *fb = backFb();
  if (!fb) return;
  const float bob = 7.0f * sinf(TWO_PI_F * (float)(tMs % 1600) / 1600.0f);
  const int16_t gy = clampCoord(392.0f + bob);
  const float w = wave(tMs, 1600);
  display::addGlow(cx, gy, 78, rgb, clamp8(80.0f + 60.0f * w));
  drawGem(cx, gy, 46.0f, 70.0f, rgb, true);

  // Down arrow (towards the plinth), sliding down a little and pulsing.
  const float ph = (float)(tMs % 1100) / 1100.0f;           // 0..1
  const float slide = 7.0f * ph;
  const float lvl = sinf(3.14159265f * ph);                  // fade in/out over the slide
  const uint32_t col = display::scaleRgb(mixRgb(rgb, WHITE, 110), clamp8(110.0f + 145.0f * lvl));
  const float top = 436.0f + slide;
  // stem rows [top, top+12), head from top+12 down: no row is lit twice (additive)
  const int32_t stemY0 = (int32_t)ceilf(top), stemY1 = (int32_t)ceilf(top + 12.0f);
  for (int32_t y = stemY0; y < stemY1; y++) addSpan(fb, y, cx - 7, cx + 7, (col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF);
  addTriangle(fb, cx - 21.0f, top + 12.0f, cx + 21.0f, top + 12.0f, (float)cx, top + 32.0f, col);
}

void bowlIcon(int16_t cx, int16_t cy, int16_t size, Shape shape, uint32_t rgb, uint32_t tMs, uint8_t level) {
  if (size <= 0 || level == 0 || !backFb()) return;
  const float w = wave(tMs, 1400);
  const float R = size * 0.5f * (1.0f + 0.05f * w);         // gentle "breathing" scale
  display::addGlow(cx, cy, (int16_t)(R * 1.6f), rgb, clamp8((70.0f + 70.0f * w) * level / 255.0f));

  Arduino_GFX &g = display::gfx();
  const uint16_t white = c565(display::scaleRgb(WHITE, level)), fillc = c565(display::scaleRgb(rgb, level));
  constexpr int OUT = 3;   // white outline thickness (px)
  switch (shape) {
    case Shape::Triangle: {
      // a triangle looks smaller than a circle of the same radius: scale it up a bit.
      // Growing the circumradius by 2*OUT moves every edge outwards by OUT (inradius = R/2).
      const float rt = R * 1.18f;
      fillUpTriangle(cx, (int16_t)(cy + rt * 0.12f), rt + 2 * OUT, white);   // nudged down: optical centre
      fillUpTriangle(cx, (int16_t)(cy + rt * 0.12f), rt, fillc);
      break;
    }
    case Shape::Star: {
      const float rs = R * 1.08f, ri = rs * 0.47f;   // chubby, friendly star
      static const int8_t off[8][2] = {{-OUT, 0}, {OUT, 0}, {0, -OUT}, {0, OUT}, {-2, -2}, {2, -2}, {-2, 2}, {2, 2}};
      for (const auto &o : off) fillStar((int16_t)(cx + o[0]), (int16_t)(cy + o[1]), rs, ri, white);
      fillStar(cx, cy, rs, ri, fillc);
      break;
    }
    case Shape::Circle:
    default: {
      const int16_t r = (int16_t)R;
      g.fillCircle(cx, cy, r + OUT, white);
      g.fillCircle(cx, cy, r, fillc);
      // little glossy highlight, upper left
      g.fillCircle(clampCoord(cx - R * 0.38f), clampCoord(cy - R * 0.38f), (int16_t)(R * 0.16f + 1),
                   c565(display::scaleRgb(mixRgb(rgb, WHITE, 170), level)));
      break;
    }
  }
}

void gem(int16_t cx, int16_t cy, int16_t w, int16_t h, uint32_t rgb, bool lit) {
  if (w <= 0 || h <= 0 || !backFb()) return;
  drawGem(cx, cy, (float)w, (float)h, rgb, lit);
}

// ============================================================================
//  story overlays: Moon / lantern meter, poof, smudge cloud, light beam, check mark
// ============================================================================
void moonMeter(int16_t cx, int16_t cy, int16_t r, float phase, uint8_t glowLevel, uint32_t rgb, uint32_t tMs) {
  uint16_t *fb = backFb();
  if (!fb || r < 4) return;
  if (r > 200) r = 200;
  if (!(phase >= 0.0f)) phase = 0.0f;   // also catches NaN
  if (phase > 8.0f) phase = 8.0f;
  const uint32_t SILVER = rgb & 0xFFFFFF;   // the lit Moon (meter.color; Moonshards #E9EEFF)
  constexpr uint32_t DARK = 0x283050;   // the unlit part: a dim slate disc ...
  constexpr float DARK_A = 0.55f;       // ... letting the night sky show through a little
  const float lit = 0.10f + 0.90f * (phase / 8.0f);   // lit share of every row: thin crescent .. full
  const float gl = glowLevel / 255.0f;
  const float breath = wave(tMs, 4000);

  // 1) soft halo, stronger as the Moon fills (and when the caller pulses it)
  const float halo = (24.0f + 96.0f * lit) * (0.5f + 0.5f * gl) * (0.92f + 0.08f * breath);
  display::addGlow(cx, cy, (int16_t)(r * (1.75f + 0.45f * gl)), mixRgb(SILVER, 0xBFD4FF, 90), clamp8(halo));
  if (gl > 0.55f) display::addGlow(cx, cy, (int16_t)(r * 1.25f), SILVER, clamp8((gl - 0.55f) * 160.0f));

  // 2) the disc, row by row: lit (right) side of an elliptical terminator,
  //    limb darkening, a few soft craters, anti-aliased edges.
  struct Crater { float u, v, rad, k; };
  static const Crater CR[5] = {{0.30f, -0.28f, 0.17f, 0.80f}, {-0.22f, 0.30f, 0.13f, 0.84f},
                               {0.46f, 0.32f, 0.10f, 0.82f}, {-0.36f, -0.30f, 0.09f, 0.86f},
                               {0.06f, 0.06f, 0.07f, 0.88f}};
  const float rf = (float)r;
  const float SR = (float)((SILVER >> 16) & 0xFF), SG = (float)((SILVER >> 8) & 0xFF), SB = (float)(SILVER & 0xFF);
  const float DR = (float)((DARK >> 16) & 0xFF), DG = (float)((DARK >> 8) & 0xFF), DB = (float)(DARK & 0xFF);
  const int32_t y0 = cy - r - 1 < 0 ? 0 : cy - r - 1;
  const int32_t y1 = cy + r + 1 > H - 1 ? H - 1 : cy + r + 1;
  for (int32_t y = y0; y <= y1; y++) {
    const float dy = (float)(y - cy);
    const float hh = (rf + 1.0f) * (rf + 1.0f) - dy * dy;
    if (hh <= 0.0f) continue;
    const float hw = sqrtf(hh);                               // half-width (incl. the soft edge) on this row
    const float core = rf * rf - dy * dy;
    const float hwc = core > 0.0f ? sqrtf(core) : 0.0f;       // half-width of the disc proper
    const float xt = hwc * (1.0f - 2.0f * lit);               // terminator (lit to its right)
    int32_t xa = (int32_t)floorf((float)cx - hw), xb = (int32_t)ceilf((float)cx + hw);
    if (xa < 0) xa = 0;
    if (xb > W - 1) xb = W - 1;
    uint16_t *row = fb + (size_t)y * W;
    for (int32_t x = xa; x <= xb; x++) {
      const float dx = (float)(x - cx);
      const float d2 = dx * dx + dy * dy;
      // coverage of the disc edge (1 px soft)
      const float d = sqrtf(d2);
      float cov = rf + 0.5f - d;
      if (cov <= 0.0f) continue;
      if (cov > 1.0f) cov = 1.0f;
      // lit amount across the terminator (~3 px soft)
      float la = (dx - xt) / 3.0f + 0.5f;
      if (lit >= 0.999f) la = 1.0f;
      if (la < 0.0f) la = 0.0f;
      if (la > 1.0f) la = 1.0f;
      // shading: limb darkening + craters
      float sh = 1.0f - 0.20f * (d2 / (rf * rf));
      for (const Crater &c : CR) {
        const float ex = dx - c.u * rf, ey = dy - c.v * rf, crr = c.rad * rf;
        const float e2 = ex * ex + ey * ey;
        if (e2 < crr * crr) {
          const float q = e2 / (crr * crr);              // soft crater edge
          sh *= c.k + (1.0f - c.k) * q * q;
        }
      }
      const float lr = SR * sh, lg = SG * sh, lb = SB * sh;
      const float pr = DR + (lr - DR) * la, pg = DG + (lg - DG) * la, pb = DB + (lb - DB) * la;
      const float a = cov * (DARK_A + (1.0f - DARK_A) * la);
      const uint16_t col = display::rgb565(clamp8(pr), clamp8(pg), clamp8(pb));
      row[x] = display::blend565(row[x], col, clamp8(a * 255.0f));
    }
  }
}

void lanternMeter(int16_t cx, int16_t cy, int16_t r, float phase, uint8_t glowLevel, uint32_t rgb, uint32_t tMs) {
  uint16_t *fb = backFb();
  if (!fb || r < 6) return;
  if (r > 220) r = 220;
  if (!(phase >= 0.0f)) phase = 0.0f;   // also catches NaN
  if (phase > 8.0f) phase = 8.0f;
  rgb &= 0xFFFFFF;
  if (!rgb) rgb = 0xFFB347;
  const float f = phase / 8.0f;          // 0 = a faint ember .. 1 = a blaze
  const float gl = glowLevel / 255.0f;   // ~0.4 resting, 1.0 at the peak of a "point added" swell
  const float rf = (float)r;

  // Candle flicker: three slow sines (0.7 / 1.3 / 1.9 Hz, all <= 2 Hz) moving the
  // light by a few percent. Time wraps every 600 s, where every sine is whole.
  const float ts = (float)(tMs % 600000u) * 0.001f;
  const float flick = 0.5f * sinf(TWO_PI_F * 1.3f * ts) + 0.3f * sinf(TWO_PI_F * 1.9f * ts + 1.7f) +
                      0.2f * sinf(TWO_PI_F * 0.7f * ts + 0.4f);   // -1..1
  const float fl = 1.0f + 0.06f * flick;      // intensity
  const float fr = 1.0f + 0.025f * flick;     // radius
  const float lvl = 0.55f + 0.6f * gl;        // the caller's swell
  const float rad = (0.9f + 0.25f * gl) * fr;

  // 1) the wide warm halo on the cave wall (ember: small and dim; blaze: wide)
  display::addGlow(cx, cy, (int16_t)(rf * (0.55f + 0.85f * f) * rad), rgb, clamp8((40.0f + 150.0f * f) * lvl * fl));
  // 2) the glowing globe: a warmer, whiter inner light
  const uint32_t warm = mixRgb(rgb, 0xFFF1D6, (uint8_t)(40.0f + 90.0f * f));
  display::addGlow(cx, cy, (int16_t)(rf * (0.28f + 0.32f * f) * rad), warm, clamp8((55.0f + 140.0f * f) * lvl * fl));
  // 3) a hot core once it's properly burning
  if (f > 0.35f)
    display::addGlow(cx, cy, (int16_t)(rf * (0.10f + 0.08f * f)), mixRgb(rgb, WHITE, 200),
                     clamp8((f - 0.35f) / 0.65f * 170.0f * fl));
  // 4) the "point added" swell: a soft bright bloom (like the Moon's)
  if (gl > 0.55f) display::addGlow(cx, cy, (int16_t)(rf * 1.25f), mixRgb(rgb, WHITE, 90), clamp8((gl - 0.55f) * 170.0f));

  // 5) a few slow sparks rising off it (none as an ember, up to 5 at a blaze)
  const int n = f < 0.3f ? 0 : (1 + (int)((f - 0.3f) * 6.0f));
  const uint32_t sparkCol = mixRgb(rgb, WHITE, 120);
  for (int i = 0; i < n && i < 5; i++) {
    const uint32_t h = hash32(0xA511E9B3u * (uint32_t)(i + 1));
    const uint32_t period = 2600u + h % 1700u;                     // 2.6-4.3 s per rise
    const float u = (float)((tMs + (h >> 8) % period) % period) / (float)period;   // 0..1
    const float rise = rf * (1.0f + 0.35f * (float)((h >> 16) % 100u) / 100.0f);
    const float x = (float)cx + (float)((int)((h >> 20) % 41u) - 20) * rf * (0.6f / 70.0f) +
                    6.0f * sinf(TWO_PI_F * 1.5f * u + (float)i);
    const float y = (float)cy - rf * 0.15f - u * rise;
    const float b = sinf(3.14159265f * u) * (110.0f + 100.0f * f);  // fade in, fade out
    display::addGlow(clampCoord(x), clampCoord(y), (int16_t)(5 + (h >> 28) % 3), sparkCol, clamp8(b * 0.6f));
    const uint8_t c = clamp8(b);
    addPix(fb, (int32_t)lroundf(x), (int32_t)lroundf(y), c, c, (uint32_t)c * 3 / 4);
  }
}

void poof(int16_t cx, int16_t cy, uint32_t sinceMs, uint32_t rgb, uint32_t durMs) {
  if (durMs == 0 || sinceMs >= durMs) return;
  uint16_t *fb = backFb();
  if (!fb) return;
  rgb &= 0xFFFFFF;
  const float p = (float)sinceMs / (float)durMs;   // 0..1
  const float q = 1.0f - p;
  const float grow = 1.0f - q * q;                 // swells fast, then settles
  const float R = 22.0f + 50.0f * grow;
  const uint8_t a = clamp8(235.0f * powf(q, 1.5f));
  // a quick flash first
  if (p < 0.35f) display::addGlow(cx, cy, (int16_t)(50.0f + 90.0f * p), rgb, clamp8(210.0f * (1.0f - p / 0.35f)));
  // a ring of soft puffs (every other one a little shaded) around a centre puff
  const uint32_t shade = mixRgb(rgb, 0xC8CCDA, 90);
  for (int i = 0; i < 6; i++) {
    const float ang = (float)i * (TWO_PI_F / 6.0f) + 0.5f;
    softDisc(fb, (float)cx + R * 0.58f * cosf(ang), (float)cy + R * 0.42f * sinf(ang), R * 0.40f, 0.85f,
             (i & 1) ? shade : rgb, a);
  }
  softDisc(fb, (float)cx, (float)cy - R * 0.05f, R * 0.55f, 0.85f, rgb, a);
}

void smudgeCloud(int16_t cx, int16_t cy, int16_t r, uint32_t tMs, uint8_t level, uint32_t rgb) {
  uint16_t *fb = backFb();
  if (!fb || r < 6 || level == 0) return;
  const uint32_t LAV = (rgb & 0xFFFFFF) ? (rgb & 0xFFFFFF) : 0xB9A8D6;   // the puff colour (the trick tier's)
  const float rf = (float)r;
  const float t = (float)(tMs % 600000u);
  // seven wobbling puffs around a big centre puff (wider than tall)
  for (int i = 0; i < 7; i++) {
    const float a = i * (TWO_PI_F / 7.0f) + 0.25f * sinf(t / 1700.0f + i);
    const float dist = rf * (0.55f + 0.05f * sinf(t / 1300.0f + i * 2.1f));
    const float pr = rf * (0.42f + 0.05f * sinf(t / 900.0f + i * 1.7f));
    const uint32_t col = mixRgb(LAV, WHITE, (uint8_t)(20 + (i % 3) * 18));
    softDisc(fb, cx + dist * cosf(a), cy + dist * 0.62f * sinf(a), pr, 0.85f, col, level);
  }
  softDisc(fb, cx, cy, rf * 0.66f, 0.8f, LAV, level);
  // fluffy highlight (upper left) and a deeper core
  softDisc(fb, cx - rf * 0.22f, cy - rf * 0.20f, rf * 0.30f, 0.8f, mixRgb(LAV, WHITE, 120), (uint8_t)(level / 2));
  softDisc(fb, cx + rf * 0.10f, cy + rf * 0.12f, rf * 0.28f, 0.8f, display::scaleRgb(LAV, 190), (uint8_t)(level / 3));
}

void beam(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t halfWidth, uint32_t rgb, uint8_t level, uint32_t tMs) {
  uint16_t *fb = backFb();
  if (!fb || halfWidth <= 0 || level == 0) return;
  if (halfWidth > 200) halfWidth = 200;
  const int32_t yTop = y1 < y0 ? y1 : y0, yBot = y1 < y0 ? y0 : y1;
  const float span = (float)(yBot - yTop);
  // colour ramp over the squared cross-section distance (64 steps): tinted body + whiter core
  constexpr int STEPS = 64;
  uint8_t aR[STEPS + 1], aG[STEPS + 1], aB[STEPS + 1];
  const uint32_t core = mixRgb(rgb, WHITE, 170);
  const float lv = level / 255.0f;
  for (int k = 0; k <= STEPS; k++) {
    const float q = (float)k / STEPS;          // (dx / hw)^2
    const float body = 1.0f - q;               // parabolic
    const float hot = body * body * body;
    aR[k] = clamp8((((rgb >> 16) & 0xFF) * body * 0.75f + ((core >> 16) & 0xFF) * hot * 0.55f) * lv);
    aG[k] = clamp8((((rgb >> 8) & 0xFF) * body * 0.75f + ((core >> 8) & 0xFF) * hot * 0.55f) * lv);
    aB[k] = clamp8(((rgb & 0xFF) * body * 0.75f + (core & 0xFF) * hot * 0.55f) * lv);
  }
  for (int32_t y = yTop < 0 ? 0 : yTop; y <= yBot && y < H; y++) {
    // v: 0 at the tip (x1, y1) .. 1 at the base (x0, y0)
    const float v = span > 0.5f ? (float)(y1 < y0 ? (y - yTop) : (yBot - y)) / span : 1.0f;
    const float xc = (float)x1 + (float)(x0 - x1) * v;
    const float hw = (float)halfWidth * (0.45f + 0.55f * v);
    if (hw < 1.0f) continue;
    // slow upward shimmer (bands drift up at ~250 px/s, +-14 %) and soft ends
    float s = 0.86f + 0.14f * sinf(0.06f * ((float)y + (float)(tMs % 100000u) * 0.25f));
    if (v < 0.06f) s *= v / 0.06f;
    if (v > 0.92f) s *= (1.0f - v) / 0.08f;
    if (s <= 0.0f) continue;
    const uint32_t sc = (uint32_t)(s * 256.0f);
    int32_t xa = (int32_t)ceilf(xc - hw), xb = (int32_t)floorf(xc + hw);
    if (xa < 0) xa = 0;
    if (xb > W - 1) xb = W - 1;
    const float inv = 1.0f / (hw * hw);
    uint16_t *row = fb + (size_t)y * W;
    const uint8_t *bay = BAYER4[y & 3];
    for (int32_t x = xa; x <= xb; x++) {
      const float dx = (float)x - xc;
      int idx = (int)(dx * dx * inv * STEPS);
      if (idx > STEPS) idx = STEPS;
      const uint32_t d = bay[x & 3];
      const uint32_t ar = (((aR[idx] * sc) >> 8) + (d >> 1)) >> 3;
      const uint32_t ag = (((aG[idx] * sc) >> 8) + (d >> 2)) >> 2;
      const uint32_t ab = (((aB[idx] * sc) >> 8) + (d >> 1)) >> 3;
      if ((ar | ag | ab) == 0) continue;
      const uint32_t p = row[x];
      uint32_t pr = (p >> 11) + ar, pg = ((p >> 5) & 0x3F) + ag, pb = (p & 0x1F) + ab;
      if (pr > 31) pr = 31;
      if (pg > 63) pg = 63;
      if (pb > 31) pb = 31;
      row[x] = (uint16_t)((pr << 11) | (pg << 5) | pb);
    }
  }
  // the bright tip where the light arrives
  display::addGlow(x1, y1, (int16_t)(halfWidth * 1.6f), mixRgb(rgb, WHITE, 120), clamp8(150.0f * lv));
}

void checkMark(int16_t cx, int16_t cy, int16_t size, uint32_t rgb) {
  if (size <= 4 || !backFb()) return;
  const float s = (float)size;
  const float ax = cx - 0.50f * s, ay = cy + 0.02f * s;
  const float bx = cx - 0.15f * s, by = cy + 0.36f * s;
  const float ex = cx + 0.52f * s, ey = cy - 0.40f * s;
  const float w = s * 0.17f + 2.0f;
  thickLine(ax, ay, bx, by, w + 6.0f, OUTLINE);   // dark outline first
  thickLine(bx, by, ex, ey, w + 6.0f, OUTLINE);
  const uint16_t col = c565(rgb);
  thickLine(ax, ay, bx, by, w, col);
  thickLine(bx, by, ex, ey, w, col);
}

void bigNumber(int value, int16_t cx, int16_t baseline, uint32_t rgb) {
  if (!backFb()) return;
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", value);
  display::addGlow(cx, (int16_t)(baseline - 46), 125, rgb, 64);   // soft halo; logisoso92 digits are ~92 px tall
  display::textCentered(buf, cx, baseline, u8g2_font_logisoso92_tn, c565(rgb), OUTLINE, 4);
}

void title(const char *s, int16_t cx, int16_t baseline, uint32_t rgb) {
  static const uint8_t *const fonts[] = {u8g2_font_fub30_tr, u8g2_font_fub25_tr, u8g2_font_fub20_tr, u8g2_font_fub14_tr};
  static const uint8_t outlines[] = {3, 3, 2, 2};
  fittedText(s, cx, baseline, rgb, fonts, outlines, 4, W - 24);
}

void subtitle(const char *s, int16_t cx, int16_t baseline, uint32_t rgb) {
  static const uint8_t *const fonts[] = {u8g2_font_fub20_tr, u8g2_font_fub17_tr, u8g2_font_fub14_tr, u8g2_font_fub11_tr};
  static const uint8_t outlines[] = {2, 2, 2, 1};
  fittedText(s, cx, baseline, rgb, fonts, outlines, 4, W - 24);
}

void caption(const char *text, uint32_t rgb) {
  if (!backFb()) return;
  // soft top edge (12 rows fading in) then the band itself
  for (int i = 0; i < 12; i++) display::darkenRect(0, (int16_t)(388 + i), W, 1, (uint8_t)(255 - (i + 1) * 105 / 12));
  display::darkenRect(0, 400, W, H - 400, 150);
  if (!text || !*text) return;
  static const uint8_t *const fonts[] = {u8g2_font_fub25_tr, u8g2_font_fub20_tr, u8g2_font_fub14_tr};
  static const uint8_t outlines[] = {3, 2, 2};
  fittedText(text, W / 2, 453, rgb, fonts, outlines, 3, W - 24);
}

void hostBadge(const char *text, uint32_t rgb) {
  if (!text || !*text || !backFb()) return;
  const uint8_t *font = u8g2_font_helvB12_tr;
  const int16_t w = display::textWidth(text, font);
  display::darkenRect(4, 4, (int16_t)(w + 18), 24, 60);
  display::textCentered(text, (int16_t)(13 + w / 2), 22, font, c565(rgb), OUTLINE, 1);
}

void counterBadge(uint32_t count, const char *label) {
  if (!backFb()) return;
  char line[64];
  counterText(line, sizeof(line), count, label);
  const uint8_t *font = u8g2_font_fub14_tr;
  const int16_t w = display::textWidth(line, font);
  const int16_t right = W - 12;
  display::darkenRect((int16_t)(right - w - 10), 6, (int16_t)(w + 18), 30, 80);
  display::textCentered(line, (int16_t)(right - w / 2), 29, font, display::rgb565(210, 190, 255), OUTLINE, 2);
}

void counterBadgeAt(int16_t cx, int16_t baseline, uint32_t count, const char *label) {
  if (!backFb()) return;
  char line[64];
  counterText(line, sizeof(line), count, label);
  const uint8_t *font = u8g2_font_fub14_tr;
  const int16_t w = display::textWidth(line, font);
  int16_t x = (int16_t)(cx - w / 2);                 // keep the whole badge on screen
  if (x + w > W - 8) x = (int16_t)(W - 8 - w);
  if (x < 8) x = 8;
  display::darkenRect((int16_t)(x - 9), (int16_t)(baseline - 23), (int16_t)(w + 18), 30, 80);
  display::textCentered(line, (int16_t)(x + w / 2), baseline, font, display::rgb565(210, 190, 255), OUTLINE, 2);
}

// ============================================================================
//  sparkles
// ============================================================================
void sparklesClear() {
  for (auto &s : s_sparks) s.active = false;
  s_ambientAcc = 0.0f;
  s_ambientLastMs = 0;
}

void sparklesBurst(int16_t cx, int16_t cy, uint8_t n, uint32_t rgb) {
  for (int i = 0; i < n; i++) {
    Spark *s = allocSpark();
    const float a = rndf() * TWO_PI_F;
    const float sp = 0.06f + rndf() * 0.30f;   // px per ms
    s->x = cx + 4.0f * cosf(a);
    s->y = cy + 4.0f * sinf(a);
    s->vx = sp * cosf(a);
    s->vy = sp * sinf(a) - 0.04f;              // a slight upward bias
    s->age = 0;
    s->life = (uint16_t)(450 + rnd() % 700);
    s->size = (uint8_t)(1 + rnd() % 3);
    s->ambient = 0;
    s->phase = (uint8_t)rnd();
    // one in three is white-hot, the rest are tints of the crystal colour
    setSparkColour(s, (rnd() % 3 == 0) ? mixRgb(rgb, WHITE, 210) : mixRgb(rgb, WHITE, (uint8_t)(50 + rnd() % 90)));
    s->active = true;
  }
}

void sparklesAmbient(uint32_t rgb, uint8_t perSecond) {
  const uint32_t now = millis();
  uint32_t dt = (s_ambientLastMs == 0) ? 33 : now - s_ambientLastMs;
  if (dt > 250) dt = 250;   // after a pause, don't dump a pile of sparkles at once
  s_ambientLastMs = now;
  if (perSecond == 0) return;
  s_ambientAcc += perSecond * (float)dt / 1000.0f;
  if (s_ambientAcc > 8.0f) s_ambientAcc = 8.0f;
  while (s_ambientAcc >= 1.0f) {
    s_ambientAcc -= 1.0f;
    Spark *s = allocSpark();
    s->x = 20.0f + (float)(rnd() % (W - 40));
    s->y = 20.0f + (float)(rnd() % 360);       // stay above the caption band
    s->vx = (rndf() - 0.5f) * 0.012f;
    s->vy = -(0.006f + rndf() * 0.018f);       // slow upward drift
    s->age = 0;
    s->life = (uint16_t)(1400 + rnd() % 1600);
    s->size = (uint8_t)(1 + rnd() % 3);
    s->ambient = 1;
    s->phase = (uint8_t)rnd();
    setSparkColour(s, mixRgb(rgb, WHITE, (uint8_t)(80 + rnd() % 120)));
    s->active = true;
  }
}

void sparklesStep(uint32_t dtMs) {
  if (dtMs > 100) dtMs = 100;   // a long stall shouldn't teleport particles
  const float dt = (float)dtMs;
  uint16_t *fb = backFb();
  for (int i = 0; i < MAX_SPARKS; i++) {
    Spark &s = s_sparks[i];
    if (!s.active) continue;
    const uint32_t age = (uint32_t)s.age + dtMs;
    if (age >= s.life) {
      s.active = false;
      continue;
    }
    s.age = (uint16_t)age;
    if (!s.ambient) {
      float drag = 1.0f - 0.0022f * dt;   // bursts slow down and drift a little downward
      if (drag < 0.0f) drag = 0.0f;
      s.vx *= drag;
      s.vy = s.vy * drag + 0.00012f * dt;
    }
    s.x += s.vx * dt;
    s.y += s.vy * dt;
    if (s.x < -12.0f || s.x > W + 12.0f || s.y < -12.0f || s.y > H + 12.0f) {
      s.active = false;
      continue;
    }
    if (!fb) continue;   // keep simulating even when nothing is being drawn
    // brightness by life: quick fade-in (15 %), slow fade-out
    const float f = (float)s.age / (float)s.life;
    float b = f < 0.15f ? f / 0.15f : (1.0f - f) / 0.85f;
    if (s.ambient) b *= 0.55f + 0.45f * sinf(s.age * 0.012f + s.phase * (TWO_PI_F / 256.0f));
    drawSparkle(fb, s.x, s.y, s.size, s.r, s.g, s.b, clamp8(255.0f * b));
  }
}

}  // namespace fx
