#pragma once
// ============================================================================
//  fx.h — everything the screen draws LIVE on top of (or instead of) the clips:
//  procedural backgrounds (so every cue works with zero art on the SD card),
//  glows, sparkles, the crystal meter, the big number, titles, bowl icons and
//  the caption band. All functions draw into the CURRENT back buffer (between
//  display::beginFrame() and display::endFrame()) and are cheap enough to run
//  every frame at 13-20 fps.
//
//  Layout contract with the art (CHATGPT-ASSETS.md): the bottom quarter of every
//  clip/still (y >= 360) is kept dark and empty by the artist; captions, numbers
//  and the meter are drawn there or in the centre by the device.
// ============================================================================
#include <Arduino.h>

namespace fx {

enum class Shape : uint8_t { Circle = 0, Triangle = 1, Star = 2 };
Shape shapeFromName(const char *name);     // "circle" | "triangle" | "star"

// ---- whole-frame procedural visuals (fallback when a clip/still is missing) ----
// Dark vignette with a slowly breathing orb of `rgb` in the centre and drifting motes.
// auraRgb (0 = rgb): colour of the wide cached aura only. Pass a slowly stepping
// colour for colour-cycling cues, so the aura cache hits between steps.
void orbBackground(uint32_t tMs, uint32_t rgb, uint8_t energy /*0..255*/, uint32_t auraRgb = 0);
// The fallback for a named cue: orbBackground + the cue's caption as a big title.
void fallbackCue(const char *title, uint32_t tMs, uint32_t rgb, uint8_t energy, uint32_t auraRgb = 0);

// ---- overlays ----
void glowPulse(int16_t cx, int16_t cy, int16_t r, uint32_t rgb, uint32_t tMs, uint16_t periodMs = 1600);
void ring(int16_t cx, int16_t cy, int16_t r, int16_t thick, uint32_t rgb, uint8_t level);
// Expanding shock ring for `durMs` after it starts (absorb / reveal bursts).
void burstRing(int16_t cx, int16_t cy, uint32_t rgb, uint32_t sinceMs, uint32_t durMs = 700);

// Crystal meter: `total` gem segments in an arc/row, `filled` lit in rgb, the
// newest one (index filled-1) pulsing when `pulseNewest`.
void crystalMeter(int16_t cx, int16_t cy, uint8_t filled, uint8_t total, uint32_t rgb, uint32_t tMs, bool pulseNewest);

// "Place your crystal here" hint: a faceted crystal icon bobbing over an arrow, bottom centre.
void placeHint(uint32_t tMs, uint32_t rgb);
// The same hint centred on x = cx (e.g. two hints flanking the caption).
void placeHintAt(int16_t cx, uint32_t tMs, uint32_t rgb);

// Bowl icon: shape filled with rgb + white outline; gentle pulse. `level` < 255
// draws it faded (dimmer colours + glow), e.g. last year's result in RECALL.
void bowlIcon(int16_t cx, int16_t cy, int16_t size, Shape shape, uint32_t rgb, uint32_t tMs, uint8_t level = 255);

// One faceted crystal (the meter's gem shape) of w x h centred at (cx, cy), lit or dark.
void gem(int16_t cx, int16_t cy, int16_t w, int16_t h, uint32_t rgb, bool lit);

// ---- story overlays (which ones a story uses comes from captions.ini words) ----
// The communal meter, style "moon" (words meter.style): a pale Moon in `rgb`
// (meter.color, Moonshards #E9EEFF) with soft craters. phase 0..8
// (fractional = mid-transition): 0 = thin crescent ... 8 = full; the unlit
// part is a dim translucent disc behind an elliptical terminator. glowLevel
// 0..255 drives the soft additive halo (the caller pulses it when a point is
// added); a slow 4 s breath is built in.
void moonMeter(int16_t cx, int16_t cy, int16_t r, float phase, uint8_t glowLevel, uint32_t rgb, uint32_t tMs);
// The communal meter, style "lantern": an additive warm glow over a lantern that
// is PAINTED into the art at (cx, cy) - no hard shapes are drawn. r = the glow
// radius at full blaze. phase 0..8 (fractional ok): 0 = a faint dim ember ...
// 8 = a blazing glow; a gentle candle flicker (<= 2 Hz, a few % - never a
// strobe) and, from about phase 3, a few slow sparks rising off it. glowLevel
// 0..255 swells it like moonMeter's halo (the caller pulses it per point).
void lanternMeter(int16_t cx, int16_t cy, int16_t r, float phase, uint8_t glowLevel, uint32_t rgb, uint32_t tMs);
// A cartoon "poof": a quick flash and a ring of soft puffs in `rgb` (usually
// white) that swell and fade over durMs after it starts (trick gag effects).
void poof(int16_t cx, int16_t cy, uint32_t sinceMs, uint32_t rgb = 0xFFFFFF, uint32_t durMs = 500);
// A fuzzy puff cloud of radius ~r in `rgb` (the caller passes the trick tier's
// colour, words tier.0_color): hides the GLOW number on trick results. Gently
// wobbles with tMs; `level` fades it.
void smudgeCloud(int16_t cx, int16_t cy, int16_t r, uint32_t tMs, uint8_t level = 255, uint32_t rgb = 0xB9A8D6);
// A soft additive column of light from (x0, y0) (base) to (x1, y1) (tip),
// halfWidth at the base, narrowing towards the tip, with a slow upward shimmer
// and a glow at the tip (ABSORB: crystal light rising into the narrator's glow anchor).
void beam(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t halfWidth, uint32_t rgb, uint8_t level, uint32_t tMs);
// A chunky outlined check mark (REPLAY "already glowing").
void checkMark(int16_t cx, int16_t cy, int16_t size, uint32_t rgb);

// Big outlined number (u8g2 logisoso 92 px digits) centred at cx with the baseline at `baseline`.
void bigNumber(int value, int16_t cx, int16_t baseline, uint32_t rgb);
// Title (large) / subtitle (medium), outlined, centred.
void title(const char *s, int16_t cx, int16_t baseline, uint32_t rgb);
void subtitle(const char *s, int16_t cx, int16_t baseline, uint32_t rgb);

// Caption band at the bottom: darkens y >= 400 a little and draws `text` (<= ~24 chars) centred.
void caption(const char *text, uint32_t rgb = 0xFFFFFF);

// Small status line for the host (top-left, tiny font): e.g. "RUSH", "READER ASLEEP".
void hostBadge(const char *text, uint32_t rgb);

// Station counter (top-right, small): "1,234 crystals awakened".
void counterBadge(uint32_t count, const char *label);
// The same counter centred on cx with its baseline at `baseline` (e.g. under the meter).
void counterBadgeAt(int16_t cx, int16_t baseline, uint32_t count, const char *label);

// ---- sparkles (one shared particle field) ----
void sparklesClear();
void sparklesBurst(int16_t cx, int16_t cy, uint8_t n, uint32_t rgb);
void sparklesAmbient(uint32_t rgb, uint8_t perSecond);   // gentle background drift
void sparklesStep(uint32_t dtMs);                         // update + draw (call once per frame)

}  // namespace fx
