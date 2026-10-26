/*
 * ============================================================================
 *  service.cpp — the host's hidden menu (implements service.h)
 * ----------------------------------------------------------------------------
 *  Entered from the attract screen by holding the top-left corner for 3 s
 *  (show.cpp). Immediate-mode UI: every frame redraws the header, the page
 *  tabs and the current page with display::gfx() + u8g2 fonts; each button
 *  is drawn and hit-tested against this frame's tap in the same call. Work
 *  that blocks or draws its own frames (cue preview, SD benchmark, I2C scan,
 *  PCLK change, reboot) is deferred until after the frame is presented.
 *
 *  Pages
 *    Tonight        counters, tiers, stock left, write failures, station total,
 *                   the communal meter (named by the words meter.name: "Moon",
 *                   "Lantern" ...); reset tonight (tap twice), Fill the <name>
 *                   (the finale plays when the menu closes), Reset <name> (tap twice)
 *    Sound & light  volume / screen / ring brightness, test tone / voice / ring
 *    Mode           rush auto-on-off, second helpings, counter, blank tags,
 *                   rare / legend stock, expected kids
 *    Odds           legendary / rare / trick chance, loyalty bonus, pity (config.ini
 *                   [odds]; live for the next visit) + what a first visit gets
 *    Reader         backend, online, last packet, packets, crystal present;
 *                   simulate a new (bench-coloured) / returning / played /
 *                   foreign / unreadable crystal and the plinth button (leaves the menu so the show
 *                   runs it). Simulated crystals carry UID byte 0 = 0xF5: the
 *                   show never writes them and never counts them (show.cpp).
 *    Crystals       crystal prep through the reader (no bench needed): what's on
 *                   the reader (read OK / kind, UID, visits, colour + live swatch,
 *                   charge, this year's slot, years on it, link + CC), a read test
 *                   (good reads / placements since the page opened, last read
 *                   time), colour None / 1..N (words color.N), Provision, Clear
 *                   tonight, Format (tap twice), Sim blank. Every reader event is
 *                   consumed here while the page is open (none reaches the show).
 *    Media          TF card, asset count, missing MVP cues (STORY.md production
 *                   plan), read benchmark, rescan, cue browser (plays any cue
 *                   full screen with a random voice variant; tap = stop)
 *    System         I2C scan, free RAM/PSRAM, panel refresh, PCLK 16/12 MHz
 *                   (runtime only), uptime, firmware, reboot (tap twice),
 *                   credits + the AI-voice note
 *  Leaves on EXIT, after 60 s without a touch, or after a simulation.
 *  Changed settings are written to /oracle/config.ini on the way out (TF card
 *  only — never NVS/flash).
 * ============================================================================
 */
#include "service.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_system.h"

#include "audio.h"
#include "board.h"
#include "cues.h"
#include "display.h"
#include "fx.h"
#include "media.h"
#include "reader.h"
#include "settings.h"
#include "token.h"
#include "touch.h"
#include "vlog.h"

#ifndef FW_VERSION_STR
#define FW_VERSION_STR "1.0.0"     // the .ino's define isn't visible here; keep in step with it
#endif

namespace service {

namespace {

using settings::cfg;

constexpr uint32_t IDLE_EXIT_MS = 60000;
constexpr uint32_t FRAME_MS     = 50;
constexpr uint32_t CONFIRM_MS   = 3000;      // second tap window for destructive buttons
constexpr uint8_t  SIM_UID0     = 0xF5;      // simulated crystal marker (show.cpp honours it)

// ---- layout (800 x 480)
constexpr int16_t HEADER_H = 60;
constexpr int16_t TAB_X = 8, TAB_W = 196, TAB_H = 46, TAB_Y0 = 66, TAB_STEP = 51;   // 8 tabs: 66 .. 469
constexpr int16_t X0 = 222;                  // content area x
constexpr int16_t XR = 792;                  // content area right edge
constexpr int16_t CW = XR - X0;
constexpr int16_t ROW_H = 48;                // stepper / toggle rows
constexpr int16_t TOAST_Y = 446;             // status strip at the bottom (pages keep y >= 446 free)

// ---- colours (RGB565)
constexpr uint16_t C_BG     = display::rgb565(0x0B, 0x10, 0x20);
constexpr uint16_t C_HEADER = display::rgb565(0x1E, 0x1B, 0x4B);
constexpr uint16_t C_BTN    = display::rgb565(0x33, 0x41, 0x55);
constexpr uint16_t C_BTN_HI = display::rgb565(0x64, 0x74, 0x8B);
constexpr uint16_t C_EDGE   = display::rgb565(0x94, 0xA3, 0xB8);
constexpr uint16_t C_ACCENT = display::rgb565(0x6D, 0x28, 0xD9);
constexpr uint16_t C_RED    = display::rgb565(0xB9, 0x1C, 0x1C);
constexpr uint16_t C_GREEN  = display::rgb565(0x15, 0x80, 0x3D);
constexpr uint16_t C_TEXT   = display::rgb565(0xF8, 0xFA, 0xFC);
constexpr uint16_t C_DIM    = display::rgb565(0x94, 0xA3, 0xB8);
constexpr uint16_t C_WARN   = display::rgb565(0xF5, 0x9E, 0x0B);
constexpr uint16_t C_BAD    = display::rgb565(0xF8, 0x71, 0x71);
constexpr uint16_t C_GOOD   = display::rgb565(0x4A, 0xDE, 0x80);

const uint8_t *const FONT_H = u8g2_font_fub20_tr;   // header
const uint8_t *const FONT_B = u8g2_font_fub14_tr;   // buttons + rows
const uint8_t *const FONT_S = u8g2_font_fub11_tr;   // small print

enum Page : uint8_t { P_TONIGHT = 0, P_SOUND, P_MODE, P_ODDS, P_READER, P_CRYSTALS, P_MEDIA, P_SYSTEM, P_COUNT };
const char *const PAGE_NAME[P_COUNT] = {"Tonight", "Sound & light", "Mode", "Odds", "Reader", "Crystals",
                                        "Media", "System"};

enum Style : uint8_t { S_NORMAL, S_ACCENT, S_DANGER, S_ON, S_OFF };

// Deferred actions (run after the frame is on screen)
enum Action : uint8_t { A_NONE, A_PREVIEW, A_BENCH, A_SCAN, A_RESCAN, A_PCLK16, A_PCLK12, A_REBOOT };

enum Confirm : uint8_t { CF_NONE, CF_RESET, CF_REBOOT, CF_MOON, CF_FORMAT };

// SIM_BLANK: a factory-blank crystal for the Crystals page (it stays in the menu)
enum SimKind : uint8_t { SIM_FRESH, SIM_RETURNING, SIM_PLAYED, SIM_FOREIGN, SIM_UNREADABLE, SIM_BLANK };

// Cue browser: visual id (+ fallback visual) and voice group. A voice variant
// n shows visual <vis>_<n> when it exists (awaken_b_3 -> awaken_3, reveal_t0_2
// -> reveal_t0_2), like the show does.
struct CueRef { const char *vis; const char *alt; const char *voice; };
const CueRef CUES[] = {
  {"attract", nullptr, "attract_call"}, {"attract_full", "attract", "attract_call"},
  {"chapter_a", "chapter", "chapter_a"}, {"chapter_b", "chapter", "chapter_b"}, {"chapter_c", "chapter", "chapter_c"},
  {"chapter_d", "chapter", "chapter_d"}, {"chapter_e", "chapter", "chapter_e"}, {"chapter_f", "chapter", "chapter_f"},
  {"absorb", nullptr, nullptr},
  {"awaken", nullptr, "awaken_a"}, {"awaken", nullptr, "awaken_b"}, {"awaken", nullptr, "awaken_c"},
  {"greet_r2", "greet", "greet_r2"}, {"greet_r3", "greet", "greet_r3"}, {"greet_r4", "greet", "greet_r4"},
  {"greet_r5", "greet", "greet_r5"}, {"greet_r6", "greet", "greet_r6"}, {"greet_r7", "greet", "greet_r7"},
  {"recall_t0", "greet", "recall_t0"}, {"recall_t1", "greet", "recall_t1"},
  {"recall_t2", "greet", "recall_t2"}, {"recall_t3", "greet", "recall_t3"},
  {"charge", nullptr, "charge"},
  {"reveal_t0", "reveal", "reveal_t0"}, {"reveal_t1", "reveal", "reveal_t1"},
  {"reveal_t2", "reveal", "reveal_t2"}, {"reveal_t3", "reveal", "reveal_t3"},
  {"reveal_t1", "reveal", "bowl_common"}, {"reveal_t2", "reveal", "bowl_rare"}, {"reveal_t3", "reveal", "bowl_legend"},
  {"reveal_t0", "reveal", "rush_t0"}, {"reveal_t1", "reveal", "rush_t1"},
  {"reveal_t2", "reveal", "rush_t2"}, {"reveal_t3", "reveal", "rush_t3"},
  {"replay", nullptr, "replay"}, {"nocrystal", nullptr, "nocrystal"}, {"foreign", "nocrystal", "foreign"},
  {"sleepy", nullptr, "sleepy"}, {"goodbye", nullptr, "goodbye"}, {"milestone", nullptr, "milestone"},
  {"circle", nullptr, "circle"}, {"keeper", nullptr, "keeper"},
  {"moonfull", nullptr, "moonfull_a"}, {"moonfull", nullptr, "moonfull_b"},
};
constexpr int N_CUES = sizeof(CUES) / sizeof(CUES[0]);

// MVP = what a first night needs (STORY.md §12; missing ones fall back to
// procedural visuals / chimes / silence). Visuals count with their variants.
const char *const MVP_VIS[] = {"attract", "absorb", "awaken", "greet", "charge", "reveal_t0", "reveal_t1",
                               "reveal_t2", "reveal_t3", "replay", "nocrystal", "sleepy", "goodbye",
                               "milestone", "circle"};
const char *const MVP_VOICE[] = {"attract_call", "awaken_a", "awaken_b", "awaken_c", "greet_r2", "charge",
                                 "reveal_t0", "reveal_t1", "reveal_t2", "reveal_t3", "bowl_common", "bowl_rare",
                                 "bowl_legend", "replay", "nocrystal", "sleepy", "goodbye"};
const char *const MVP_SFX[] = {"absorb", "awaken", "reveal_t0", "reveal_t1", "reveal_t2", "reveal_t3", "tap",
                               "button", "charge_full", "sleepy", "whoosh"};

constexpr int MISSING_LINES = 6;
constexpr int I2C_MAX = 16;

struct State {
  Page     page = P_TONIGHT;
  bool     exit = false;
  bool     dirty = false;              // settings changed -> save on exit
  bool     armed = false;              // the long-press finger has been lifted
  uint32_t lastActivity = 0;
  // this frame's touch
  bool     tap = false;
  touch::Point tapAt = {0, 0};
  bool     down = false;
  touch::Point at = {0, 0};
  // confirmations + deferred work
  Confirm  confirm = CF_NONE;
  uint32_t confirmAt = 0;
  Action   pending = A_NONE;
  // media page
  bool     mediaChecked = false;
  int      missingCount = 0;
  char     missing[MISSING_LINES][80] = {};
  int      missingLines = 0;
  int      cue = 0;
  bool     benchDone = false;
  float    benchMBps = 0;
  char     benchFile[sizeof(media::Visual::path)] = {0};
  // system page
  bool     i2cScanned = false;
  uint8_t  i2c[I2C_MAX] = {0};
  int      i2cN = 0;
  // one-line status message
  char     toast[64] = {0};
  uint32_t toastAt = 0;
  // the meter's name (words meter.name) and the Tonight buttons built from it, once per visit
  const char *meter = "Moon";
  char     fillLabel[40] = {0};
  char     resetLabel[40] = {0};
};

State s;

// ================================================================== drawing helpers

void text(int16_t x, int16_t baseline, const char *str, const uint8_t *font, uint16_t col) {
  Arduino_GFX &g = display::gfx();
  g.setTextWrap(false);
  g.setTextSize(1);
  g.setFont(font);
  g.setTextColor(col);
  g.setCursor(x, baseline);
  g.print(str);
}

void textf(int16_t x, int16_t baseline, const uint8_t *font, uint16_t col, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));
void textf(int16_t x, int16_t baseline, const uint8_t *font, uint16_t col, const char *fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  text(x, baseline, buf, font, col);
}

void centered(const char *str, int16_t cx, int16_t baseline, const uint8_t *font, uint16_t col) {
  display::textCentered(str, cx, baseline, font, col, 0, 0);
}

void toast(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void toast(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s.toast, sizeof s.toast, fmt, ap);
  va_end(ap);
  s.toastAt = millis();
}

// Immediate-mode button: draws itself, returns true if THIS frame's tap hit it. A label too
// wide for the button in the button font drops to the small font.
bool button(int16_t x, int16_t y, int16_t w, int16_t h, const char *label, Style style = S_NORMAL) {
  Arduino_GFX &g = display::gfx();
  const bool held = s.armed && s.down && touch::hit(s.at, x, y, w, h);
  uint16_t bg = C_BTN;
  switch (style) {
    case S_ACCENT: bg = C_ACCENT; break;
    case S_DANGER: bg = C_RED; break;
    case S_ON:     bg = C_GREEN; break;
    case S_OFF:    bg = C_BTN; break;
    default:       break;
  }
  if (held) bg = C_BTN_HI;
  g.fillRoundRect(x, y, w, h, 10, bg);
  g.drawRoundRect(x, y, w, h, 10, C_EDGE);
  const bool small = display::textWidth(label, FONT_B) > w - 12;
  centered(label, (int16_t)(x + w / 2), (int16_t)(y + h / 2 + (small ? 5 : 7)), small ? FONT_S : FONT_B, C_TEXT);
  if (s.armed && s.tap && touch::hit(s.tapAt, x, y, w, h)) {
    s.tap = false;                       // one tap = one button
    audio::chime(audio::CHIME_TAP, 25);
    return true;
  }
  return false;
}

// "label   [-]  value  [+]"  -> -1 / 0 / +1
int stepper(int16_t y, const char *label, const char *value) {
  text(X0, (int16_t)(y + 33), label, FONT_B, C_TEXT);
  int r = 0;
  if (button(X0 + 260, y, 76, ROW_H, "-")) r = -1;
  centered(value, X0 + 260 + 76 + 64, (int16_t)(y + 33), FONT_B, C_WARN);
  if (button(X0 + 260 + 76 + 128, y, 76, ROW_H, "+")) r = +1;
  return r;
}

bool toggleRow(int16_t y, const char *label, bool &v) {
  text(X0, (int16_t)(y + 33), label, FONT_B, C_TEXT);
  if (button(X0 + 300, y, 200, ROW_H, v ? "ON" : "OFF", v ? S_ON : S_OFF)) {
    v = !v;
    s.dirty = true;
    return true;
  }
  return false;
}

template <typename T>
void bump(T &v, int dir, int step, int lo, int hi) {
  int x = (int)v + dir * step;
  if (x < lo) x = lo;
  if (x > hi) x = hi;
  if ((T)x != v) {
    v = (T)x;
    s.dirty = true;
  }
}

bool confirmed(Confirm which, uint32_t now) {
  return s.confirm == which && now - s.confirmAt < CONFIRM_MS;
}

// ================================================================== simulation

uint8_t *page(TagImage &img, uint8_t p) { return img.pages[p - TAG_FIRST_PAGE]; }

void set4(uint8_t *d, uint8_t a, uint8_t b, uint8_t c, uint8_t e) {
  d[0] = a; d[1] = b; d[2] = c; d[3] = e;
}

void setSlot(TagImage &img, uint16_t year, uint8_t number, uint8_t tier) {
  if (year < cfg.baseYear) return;
  const uint32_t p = token::PAGE_YEAR0 + (year - cfg.baseYear);
  if (p > TAG_LAST_PAGE || year - cfg.baseYear >= token::MAX_YEARS) return;
  set4(page(img, (uint8_t)p), number, tier, token::SF_ORACLE, cfg.chapter);
}

// A bench colour for a simulated crystal: one of the defined colours (1 if none are).
uint8_t simColour(uint32_t r) {
  const uint8_t n = settings::colorCount();
  return (uint8_t)(1 + (n ? r % n : 0));
}

void buildSim(TagImage &img, SimKind kind) {
  memset(&img, 0, sizeof img);
  const uint32_t r1 = esp_random(), r2 = esp_random();
  img.uidLen = 7;
  img.uid[0] = SIM_UID0;
  img.uid[1] = (uint8_t)r1; img.uid[2] = (uint8_t)(r1 >> 8); img.uid[3] = (uint8_t)(r1 >> 16);
  img.uid[4] = (uint8_t)(r1 >> 24); img.uid[5] = (uint8_t)r2; img.uid[6] = (uint8_t)(r2 >> 8);
  img.readOk = true;
  img.sak = 0;
  set4(page(img, token::PAGE_CC), 0xE1, 0x10, 0x12, 0x00);      // factory NTAG CC
  set4(page(img, token::PAGE_NDEF), 0x03, 0x00, 0xFE, 0x00);    // empty NDEF message
  switch (kind) {
    case SIM_FRESH:
      // A new crystal as the bench makes it: provisioned (marker, 0 visits) with a
      // colour, so the awakening plays its colour line (awaken_b_<colour>). Any of
      // the colours the words define (1..colorCount: gradient / rainbow ones too).
      set4(page(img, token::PAGE_MARKER), token::MAGIC0, token::MAGIC1, token::SCHEMA, 0);
      set4(page(img, token::PAGE_CHARGE), 0, simColour(r2 >> 16), 0, 0);
      break;
    case SIM_RETURNING:
    case SIM_PLAYED: {
      const bool played = kind == SIM_PLAYED;
      set4(page(img, token::PAGE_MARKER), token::MAGIC0, token::MAGIC1, token::SCHEMA, played ? 3 : 2);
      set4(page(img, token::PAGE_CHARGE), 0, simColour(r2 >> 16), 0, 0);
      setSlot(img, (uint16_t)(cfg.eventYear - 2), 55, 1);       // treat two years ago
      setSlot(img, (uint16_t)(cfg.eventYear - 1), 94, 2);       // rare last year (tests RECALL)
      if (played) setSlot(img, cfg.eventYear, 72, 1);          // already played tonight
      break;
    }
    case SIM_FOREIGN:
      // Someone else's NFC sticker: an NDEF URI "https://bus.pass", no SK marker.
      set4(page(img, 4), 0x03, 0x0D, 0xD1, 0x01);
      set4(page(img, 5), 0x09, 0x55, 0x04, 'b');
      set4(page(img, 6), 'u', 's', '.', 'p');
      set4(page(img, 7), 'a', 's', 's', 0xFE);
      break;
    case SIM_UNREADABLE:
      img.readOk = false;
      memset(img.pages, 0, sizeof img.pages);
      break;
    case SIM_BLANK:                       // a factory NTAG: CC + empty NDEF message, nothing else
      break;
  }
}

void simulate(SimKind kind) {
  static TagImage img;                    // ~160 B: keep it off the loop stack
  buildSim(img, kind);
  reader::simulatePlaced(img);
  static const char *const NAMES[] = {"fresh", "returning", "played", "foreign", "unreadable", "blank"};
  vlog::event("service: simulated %s crystal", NAMES[kind]);
  s.exit = true;                          // leave the menu so the show runs it
}

// ================================================================== media checks

// <id>_1 or <id> (clip or still): variant-only cues (awaken_1..6, reveal_t0_1..3) count.
media::Visual anyVisual(const char *id) { return media::findVisual(id, 1); }

// Greedy word wrap of "Prefix: a, b, c" into s.missing[] lines.
void addMissingGroup(const char *prefix, const char *const *names, const bool *miss, int n) {
  int count = 0;
  for (int i = 0; i < n; i++) count += miss[i] ? 1 : 0;
  if (!count) return;
  s.missingCount += count;
  if (s.missingLines >= MISSING_LINES) return;
  char *line = s.missing[s.missingLines];
  if (count == n && n > 2) {
    snprintf(line, sizeof s.missing[0], "%s all %d missing", prefix, n);
    s.missingLines++;
    return;
  }
  snprintf(line, sizeof s.missing[0], "%s", prefix);
  bool firstOnLine = true;
  for (int i = 0; i < n; i++) {
    if (!miss[i]) continue;
    char cand[80];
    snprintf(cand, sizeof cand, "%s%s %s", line, firstOnLine ? "" : ",", names[i]);
    if (display::textWidth(cand, FONT_S) > CW - 10 && !firstOnLine) {
      if (++s.missingLines >= MISSING_LINES) return;
      line = s.missing[s.missingLines];
      snprintf(line, sizeof s.missing[0], "   %s", names[i]);
    } else {
      snprintf(line, sizeof s.missing[0], "%s", cand);
    }
    firstOnLine = false;
  }
  s.missingLines++;
}

void checkMedia() {
  s.missingCount = 0;
  s.missingLines = 0;
  for (auto &l : s.missing) l[0] = 0;
  constexpr int NV = sizeof(MVP_VIS) / sizeof(MVP_VIS[0]);
  constexpr int NO = sizeof(MVP_VOICE) / sizeof(MVP_VOICE[0]);
  constexpr int NS = sizeof(MVP_SFX) / sizeof(MVP_SFX[0]);
  bool mv[NV], mo[NO], ms[NS];
  for (int i = 0; i < NV; i++) mv[i] = anyVisual(MVP_VIS[i]).kind == media::Kind::None;
  for (int i = 0; i < NO; i++) mo[i] = media::countVariants("voice", MVP_VOICE[i], "wav") == 0;
  char path[48];
  for (int i = 0; i < NS; i++) {
    snprintf(path, sizeof path, "/oracle/sfx/%s.wav", MVP_SFX[i]);
    ms[i] = !media::exists(path);
  }
  addMissingGroup("Pictures:", MVP_VIS, mv, NV);
  addMissingGroup("Voice:", MVP_VOICE, mo, NO);
  addMissingGroup("Sfx:", MVP_SFX, ms, NS);
  const bool noMusic = !media::exists("/oracle/music/idle.wav");
  static const char *const MUSIC[] = {"idle"};
  addMissingGroup("Music:", MUSIC, &noMusic, 1);
  s.mediaChecked = true;
}


// Full-screen preview of one cue: its visual (clip / still / fallback) + a
// random voice variant. Tap to stop.
void previewCue(const CueRef &e) {
  uint8_t n = 0;
  uint32_t dur = 0;
  char vpath[48] = {0};
  if (e.voice) {
    const uint8_t cnt = media::countVariants("voice", e.voice, "wav");
    if (cnt) {
      n = (uint8_t)(1 + esp_random() % cnt);
      snprintf(vpath, sizeof vpath, "/oracle/voice/%s_%u.wav", e.voice, n);
      dur = audio::voice(vpath);
    }
  }
  media::Visual v = media::findVisual(e.vis, n);
  if (v.kind == media::Kind::None) v = anyVisual(e.vis);
  if (v.kind == media::Kind::None && e.alt) v = media::findVisual(e.alt, n);
  media::Clip clip;
  const bool clipOk = v.kind == media::Kind::Clip && clip.open(v.path);
  uint32_t len = dur ? dur + 300 : 0;
  if (clipOk) len = max(len, clip.durationMs());
  if (len < 2500) len = 2500;
  if (len > 30000) len = 30000;

  char info[200];
  snprintf(info, sizeof info, "%s  |  %s  |  %s  |  tap to stop", e.vis,
           v.kind == media::Kind::None ? "no picture: fallback" : v.path, vpath[0] ? vpath : "no voice");

  const uint32_t t0 = millis();
  bool stillBad = false;
  bool clipBad = !clipOk;
  for (;;) {
    const uint32_t frameStart = millis();
    const uint32_t t = frameStart - t0;
    if (t >= len) break;
    const touch::Gestures &g = touch::update(60000);
    if (g.tap && t > 300) break;
    uint16_t *fb = display::beginFrame();
    if (fb) {
      bool drew = false;
      if (!clipBad) {
        drew = clip.render(t, false, fb);
        if (!drew) clipBad = true;
      } else if (v.kind == media::Kind::Still && !stillBad) {
        drew = media::drawStill(v.path, fb);
        if (!drew) stillBad = true;
      }
      if (!drew) fx::fallbackCue(e.vis, t, 0x8B5CF6, 150);
      fx::hostBadge(info, 0xF59E0B);
      display::endFrame();
    }
    const uint32_t frameMs = !clipBad ? clip.frameMs() : FRAME_MS;
    const uint32_t spent = millis() - frameStart;
    delay(spent < frameMs ? frameMs - spent : 1);
  }
  clip.close();
  audio::voiceStop();
  s.lastActivity = millis();
}

// Sequential read speed of the biggest kind of file we have (a clip if any).
void runBenchmark() {
  s.benchFile[0] = 0;
  static const char *const IDS[] = {"attract", "awaken", "chapter", "greet", "reveal_t1", "reveal_t2", "nocrystal"};
  for (const char *id : IDS) {
    const media::Visual v = media::findVisual(id);
    if (v.kind == media::Kind::Clip) {
      snprintf(s.benchFile, sizeof s.benchFile, "%s", v.path);
      break;
    }
  }
  if (!s.benchFile[0] && media::exists("/oracle/music/idle.wav"))
    snprintf(s.benchFile, sizeof s.benchFile, "%s", "/oracle/music/idle.wav");
  if (!s.benchFile[0]) {
    toast("Nothing big enough to measure on the card");
    s.benchDone = false;
    return;
  }
  s.benchMBps = media::benchmarkReadMBps(s.benchFile);
  s.benchDone = true;
  vlog::event("service: SD read %.2f MB/s (%s)", (double)s.benchMBps, s.benchFile);
}

void scanI2c() {
  s.i2cN = board::i2cScan(s.i2c, I2C_MAX);
  if (s.i2cN < 0) s.i2cN = 0;
  if (s.i2cN > I2C_MAX) s.i2cN = I2C_MAX;
  s.i2cScanned = true;
}

const char *i2cName(uint8_t a) {
  switch (a) {
    case board::ADDR_ES8389:   return "codec";
    case board::ADDR_EXPANDER: return "expander";
    case board::ADDR_RFID2:    return "rfid2";
    case board::ADDR_ES7210:   return "mics";
    case board::ADDR_RTC:      return "rtc";
    case board::ADDR_FUEL:     return "battery";
    case board::ADDR_GT911:
    case board::ADDR_GT911_B:  return "touch";
    default:                   return "?";
  }
}

// ================================================================== pages

void pageTonight(uint32_t now) {
  const Counters &c = counters::c;
  int16_t y = 100;
  constexpr int16_t L = 34;
  constexpr int16_t X1 = X0 + 290;
  textf(X0, y, FONT_B, C_TEXT, "Visits tonight  %u", c.visits);
  textf(X1, y, FONT_B, C_TEXT, "Awakened  %u", c.awakened);
  y += L;
  textf(X0, y, FONT_B, C_TEXT, "Returning  %u", c.returning);
  textf(X1, y, FONT_B, C_TEXT, "Replays  %u", c.replays);
  y += L;
  textf(X0, y, FONT_B, C_TEXT, "No crystal  %u", c.noCrystal);
  textf(X1, y, FONT_B, C_TEXT, "Sleepy  %u", c.sleepy);
  y += L;
  textf(X0, y, FONT_B, C_TEXT, "Foreign tags  %u", c.foreign);
  textf(X1, y, FONT_B, c.writeFails ? C_BAD : C_TEXT, "Write fails  %u", c.writeFails);
  y += L + 6;
  textf(X0, y, FONT_B, C_WARN, "Trick %u   Treat %u   Rare %u   Legendary %u", c.tier[0], c.tier[1], c.tier[2],
        c.tier[3]);
  y += L;
  if (cfg.rareStock) {
    const int left = (int)cfg.rareStock - (int)c.rareGiven;
    textf(X0, y, FONT_B, left > 0 ? C_TEXT : C_BAD, "Rare stock: %d left of %u", left > 0 ? left : 0, cfg.rareStock);
  } else {
    textf(X0, y, FONT_B, C_TEXT, "Rare stock: unlimited (%u given)", c.rareGiven);
  }
  y += L;
  if (cfg.legendStock) {
    const int left = (int)cfg.legendStock - (int)c.legendGiven;
    textf(X0, y, FONT_B, left > 0 ? C_TEXT : C_BAD, "Legendary stock: %d left of %u", left > 0 ? left : 0,
          cfg.legendStock);
  } else {
    textf(X0, y, FONT_B, C_TEXT, "Legendary stock: unlimited (%u given)", c.legendGiven);
  }
  y += L + 6;
  textf(X0, y, FONT_B, C_DIM, "All time: %lu crystal visits, %lu awakened", (unsigned long)c.stationCrystals,
        (unsigned long)c.stationAwakened);
  const uint16_t goal = cfg.moonGoal ? cfg.moonGoal : 1;
  const uint32_t phase = c.moonFull ? 8u : (uint32_t)c.moonPoints * 8u / goal;
  textf(X0, (int16_t)(y + 26), FONT_S, c.moonFull ? C_GOOD : C_DIM,
        "Year %u  -  chapter %u   |   %s %u / %u  (phase %lu of 8)%s", cfg.eventYear, cfg.chapter, s.meter,
        c.moonPoints, goal, (unsigned long)(phase > 8 ? 8 : phase), c.moonFull ? "  FULL" : "");

  constexpr int16_t BW = 180, GAP = 15, BY = 392, BH = 50;
  const bool armedReset = confirmed(CF_RESET, now);
  if (button(X0, BY, BW, BH, armedReset ? "Tap again" : "Reset tonight", armedReset ? S_DANGER : S_NORMAL)) {
    if (armedReset) {
      counters::resetTonight();
      counters::save();
      s.confirm = CF_NONE;
      vlog::event("service: tonight's counters reset");
      toast("Tonight's counters reset (%s too)", s.meter);
    } else {
      s.confirm = CF_RESET;
      s.confirmAt = now;
    }
  }
  // Fill the <meter>: points = goal; the show plays the finale when this menu
  // closes (again, if it already played tonight).
  if (button(X0 + BW + GAP, BY, BW, BH, s.fillLabel, S_ACCENT)) {
    Counters &k = counters::c;
    if (k.moonPoints < goal) k.moonPoints = goal;
    k.moonFull = false;
    counters::save();
    s.confirm = CF_NONE;
    vlog::event("service: %s (%u points)", s.fillLabel, k.moonPoints);
    toast("%s filled - the finale plays when you EXIT", s.meter);
  }
  const bool armedMoon = confirmed(CF_MOON, now);
  if (button(X0 + 2 * (BW + GAP), BY, BW, BH, armedMoon ? "Tap again" : s.resetLabel, armedMoon ? S_DANGER : S_NORMAL)) {
    if (armedMoon) {
      counters::c.moonPoints = 0;
      counters::c.moonFull = false;
      counters::save();
      s.confirm = CF_NONE;
      vlog::event("service: %s reset", s.meter);
      toast("%s reset to empty", s.meter);
    } else {
      s.confirm = CF_MOON;
      s.confirmAt = now;
    }
  }
}

void pageSound(uint32_t now) {
  (void)now;
  char v[16];
  snprintf(v, sizeof v, "%u %%", cfg.volume);
  int d = stepper(76, "Volume", v);
  if (d) {
    bump(cfg.volume, d, 5, 0, 100);
    audio::setVolume(cfg.volume);
    audio::chime(audio::CHIME_TAP, 60);
  }
  snprintf(v, sizeof v, "%u %%", cfg.brightness);
  d = stepper(140, "Screen", v);
  if (d) {
    bump(cfg.brightness, d, 5, 5, 100);
    board::backlight(cfg.brightness);
  }
  snprintf(v, sizeof v, "%u", cfg.ringBrightness);
  d = stepper(204, "Ring light", v);
  if (d) {
    bump(cfg.ringBrightness, d, 15, 0, 255);
    uint32_t idle = 0;   // crystal colour 1 (its first stop: a gradient / rainbow one has no single hex)
    if (!settings::crystalStops(1, &idle, 1)) idle = settings::crystalRgb(1, 0);
    reader::setIdleLight(idle, cfg.ringBrightness);
    reader::led(LED_SOLID, idle);
  }
  if (button(X0, 290, 180, 60, "Test tone")) audio::chime(audio::CHIME_TEST, 70);
  if (button(X0 + 195, 290, 180, 60, "Test voice")) {
    const uint8_t cnt = media::countVariants("voice", cue::ATTRACT_CALL, "wav");
    if (cnt) {
      char path[48];
      snprintf(path, sizeof path, "/oracle/voice/%s_%u.wav", cue::ATTRACT_CALL, (unsigned)(1 + esp_random() % cnt));
      if (!audio::voice(path)) toast("Voice file won't play: %s", path);
    } else {
      audio::tone(440, 400, 60);
      toast("No attract_call voice on the card - played a tone");
    }
  }
  if (button(X0 + 390, 290, 180, 60, "Ring test")) reader::led(LED_RAINBOW, 0, 0, 3000);
  text(X0, 390, "A plug in the 3.5 mm jack mutes the built-in speakers.", FONT_S, C_DIM);
  text(X0, 416, "Sound chip:", FONT_S, C_DIM);
  text(X0 + 110, 416, audio::ok() ? "ok" : "NOT answering", FONT_S, audio::ok() ? C_GOOD : C_BAD);
}

void pageMode(uint32_t now) {
  (void)now;
  static const char *const RUSH[] = {"AUTO", "ON", "OFF"};
  text(X0, 70 + 33, "Rush mode", FONT_B, C_TEXT);
  const uint8_t r = cfg.rush <= RUSH_OFF ? cfg.rush : (uint8_t)RUSH_AUTO;
  if (button(X0 + 300, 70, 200, ROW_H, RUSH[r], r == RUSH_AUTO ? S_NORMAL : S_ACCENT)) {
    cfg.rush = (uint8_t)((r + 1) % 3);
    s.dirty = true;
  }
  toggleRow(124, "Second helpings", cfg.secondHelpings);
  toggleRow(178, "Counter on screen", cfg.showCounter);
  toggleRow(232, "Awaken blank tags", cfg.allowFresh);
  char v[24];
  if (cfg.rareStock) snprintf(v, sizeof v, "%u", cfg.rareStock);
  else snprintf(v, sizeof v, "no limit");
  int d = stepper(286, "Rare stock", v);
  if (d) bump(cfg.rareStock, d, 5, 0, 995);
  if (cfg.legendStock) snprintf(v, sizeof v, "%u", cfg.legendStock);
  else snprintf(v, sizeof v, "no limit");
  d = stepper(340, "Legendary stock", v);
  if (d) bump(cfg.legendStock, d, 1, 0, 99);
  snprintf(v, sizeof v, "%u", cfg.expectedKids);
  d = stepper(394, "Expected kids", v);
  if (d) bump(cfg.expectedKids, d, 25, 25, 2000);
}

// The [odds] that decide a visit (oracle.cpp). "Normal" = the silver-circle bowl = trick + treat.
void pageOdds(uint32_t now) {
  (void)now;
  char v[24];
  snprintf(v, sizeof v, "%u %%", cfg.legendPct);
  int d = stepper(70, "Legendary", v);
  if (d) bump(cfg.legendPct, d, 1, 0, 50);
  snprintf(v, sizeof v, "%u %%", cfg.rarePct);
  d = stepper(124, "Rare", v);
  if (d) bump(cfg.rarePct, d, 1, 0, 90);
  snprintf(v, sizeof v, "%u %%", cfg.trickPct);
  d = stepper(178, "Goblin trick", v);
  if (d) bump(cfg.trickPct, d, 1, 0, 100);
  snprintf(v, sizeof v, "+%u %%", cfg.loyaltyRarePct);
  d = stepper(232, "Rare per return", v);
  if (d) bump(cfg.loyaltyRarePct, d, 1, 0, 20);
  if (cfg.pityAfter) snprintf(v, sizeof v, "%u misses", cfg.pityAfter);
  else snprintf(v, sizeof v, "off");
  d = stepper(286, "Pity rare after", v);
  if (d) bump(cfg.pityAfter, d, 1, 0, 10);

  // What a crystal's first visit gets (no loyalty, no pity, no charge, unlimited stock)
  const int L = cfg.legendPct, R = cfg.rarePct;
  const int T = cfg.firstNoTrick ? 0 : cfg.trickPct;
  const int N = 100 - L - R;
  textf(X0, 362, FONT_B, N >= 0 ? C_WARN : C_BAD, "1st visit:  Legendary %d%%   Rare %d%%   Normal %d%%", L, R,
        N > 0 ? N : 0);
  if (N >= 0)
    textf(X0, 388, FONT_S, C_DIM, "Normal = %d%% hoard coin + %d%% goblin trick (%s)", N - (T < N ? T : N),
          T < N ? T : N, cfg.firstNoTrick ? "no tricks on a first visit" : "tricks on first visits too");
  else
    text(X0, 388, "Legendary + rare are over 100 % - lower one of them.", FONT_S, C_BAD);
  const int lo = (int)cfg.loyaltyRarePct * 3 < cfg.loyaltyCapPct ? cfg.loyaltyRarePct * 3 : cfg.loyaltyCapPct;
  textf(X0, 410, FONT_S, C_DIM, "Returning: rare +%u%% per earlier visit (max +%d%%)%s", cfg.loyaltyRarePct,
        lo, cfg.pityAfter ? "; pity = a sure rare" : "");
  textf(X0, 432, FONT_S, (cfg.rareStock || cfg.legendStock) ? C_WARN : C_DIM, "%s",
        (cfg.rareStock || cfg.legendStock) ? "Stock limits are on (Mode page): they pace these chances."
                                           : "Changes count from the next visit; saved on EXIT.");
}

void pageReader(uint32_t now) {
  int16_t y = 100;
  constexpr int16_t L = 34;
  textf(X0, y, FONT_B, C_TEXT, "Backend: %s", reader::backendName());
  y += L;
  const bool on = reader::online();
  text(X0, y, "Online:", FONT_B, C_TEXT);
  text(X0 + 110, y, on ? "yes" : "NO", FONT_B, on ? C_GOOD : C_BAD);
  y += L;
  const uint32_t heard = reader::lastHeardMs();
  if (heard) textf(X0, y, FONT_B, C_TEXT, "Last packet: %.1f s ago", (double)(now - heard) / 1000.0);
  else text(X0, y, "Last packet: never", FONT_B, C_TEXT);
  textf(X0 + 330, y, FONT_B, C_TEXT, "Packets: %lu", (unsigned long)reader::packetsIn());
  y += L;
  textf(X0, y, FONT_B, C_TEXT, "Crystal on the reader: %s", reader::tagPresent() ? "yes" : "no");
  y += 30;
  text(X0, y, cfg.hasButton ? "Plinth button: yes  |  crystal prep: the Crystals page"
                            : "Plinth button: none (taps bless)  |  crystal prep: the Crystals page",
       FONT_S, C_DIM);

  text(X0, 262, "Simulate (crystals: no tag write, nothing counted; the blessing counts):", FONT_S, C_DIM);
  constexpr int16_t BW = 180, BH = 60, GAP = 15;
  if (button(X0, 276, BW, BH, "New crystal", S_ACCENT)) simulate(SIM_FRESH);
  if (button(X0 + BW + GAP, 276, BW, BH, "Returning", S_ACCENT)) simulate(SIM_RETURNING);
  if (button(X0 + 2 * (BW + GAP), 276, BW, BH, "Played", S_ACCENT)) simulate(SIM_PLAYED);
  if (button(X0, 276 + BH + GAP, BW, BH, "Foreign")) simulate(SIM_FOREIGN);
  if (button(X0 + BW + GAP, 276 + BH + GAP, BW, BH, "Unreadable")) simulate(SIM_UNREADABLE);
  if (button(X0 + 2 * (BW + GAP), 276 + BH + GAP, BW, BH, cfg.hasButton ? "Button" : "Blessing")) {
    reader::simulateButton();
    vlog::event("service: simulated plinth button");
    s.exit = true;
  }
}

void pageMedia(uint32_t now) {
  (void)now;
  if (!s.mediaChecked) checkMedia();
  int16_t y = 96;
  if (media::sdOk()) {
    textf(X0, y, FONT_B, C_TEXT, "TF card: ok, %lu MB", (unsigned long)media::sdSizeMB());
    textf(X0 + 330, y, FONT_B, C_TEXT, "Assets: %u", media::assetCount());
  } else {
    text(X0, y, "TF card: MISSING (built-in visuals only)", FONT_B, C_BAD);
  }
  y += 30;
  if (s.missingCount == 0) {
    text(X0, y, "Every MVP cue is on the card.", FONT_S, C_GOOD);
  } else {
    textf(X0, y, FONT_S, C_WARN, "Missing MVP cues (%d) - fallbacks will be used:", s.missingCount);
    y += 22;
    for (int i = 0; i < s.missingLines; i++, y += 21) text(X0, y, s.missing[i], FONT_S, C_TEXT);
  }

  // Rescan / benchmark row
  constexpr int16_t ROW1 = 300;
  if (button(X0, ROW1, 150, 54, "Rescan")) s.pending = A_RESCAN;
  if (button(X0 + 165, ROW1, 170, 54, "Read test")) s.pending = A_BENCH;
  if (s.pending == A_BENCH) text(X0 + 350, ROW1 + 34, "measuring...", FONT_S, C_WARN);
  else if (s.benchDone) textf(X0 + 350, ROW1 + 34, FONT_S, C_TEXT, "%.1f MB/s", (double)s.benchMBps);

  // Cue browser row
  constexpr int16_t ROW2 = 380;
  if (button(X0, ROW2, 70, 60, "<")) s.cue = (s.cue + N_CUES - 1) % N_CUES;
  if (button(X0 + 290, ROW2, 70, 60, ">")) s.cue = (s.cue + 1) % N_CUES;
  const CueRef &e = CUES[s.cue];
  media::Visual v = anyVisual(e.vis);
  if (v.kind == media::Kind::None && e.alt) v = anyVisual(e.alt);
  const uint8_t nv = e.voice ? media::countVariants("voice", e.voice, "wav") : 0;
  centered(e.voice ? e.voice : e.vis, X0 + 70 + 110, ROW2 + 26, FONT_B, C_WARN);
  char sub[48];
  snprintf(sub, sizeof sub, "%s, %u voice",
           v.kind == media::Kind::Clip ? "clip" : (v.kind == media::Kind::Still ? "still" : "no picture"), nv);
  centered(sub, X0 + 70 + 110, ROW2 + 50, FONT_S, C_DIM);
  if (button(X0 + 375, ROW2, 195, 60, "Play", S_ACCENT)) s.pending = A_PREVIEW;
}

void pageSystem(uint32_t now) {
  if (!s.i2cScanned) scanI2c();
  char line[100];
  int len = snprintf(line, sizeof line, "I2C:");
  for (int i = 0; i < s.i2cN && len > 0 && len < (int)sizeof line - 16; i++)
    len += snprintf(line + len, sizeof line - (size_t)len, " %02X %s", s.i2c[i], i2cName(s.i2c[i]));
  if (s.i2cN == 0) snprintf(line, sizeof line, "I2C: nothing answered");
  text(X0, 92, line, FONT_S, C_TEXT);
  textf(X0, 120, FONT_S, C_TEXT, "Free RAM: internal %lu KB (largest %lu KB), PSRAM %lu KB",
        (unsigned long)(board::freeInternal() / 1024),
        (unsigned long)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
        (unsigned long)(board::freePsram() / 1024));
  textf(X0, 148, FONT_S, C_TEXT, "Panel: %.2f Hz, PCLK %lu MHz, %lu frames", (double)display::refreshHz(),
        (unsigned long)(display::pclkHz() / 1000000), (unsigned long)display::presentedFrames());
  if (button(X0, 160, 150, ROW_H, "16 MHz", display::pclkHz() == 16000000 ? S_ON : S_OFF)) s.pending = A_PCLK16;
  if (button(X0 + 165, 160, 150, ROW_H, "12 MHz", display::pclkHz() == 12000000 ? S_ON : S_OFF)) s.pending = A_PCLK12;
  if (button(X0 + 330, 160, 150, ROW_H, "Scan I2C")) s.pending = A_SCAN;
  text(X0, 230, "12 MHz if the picture ever drifts (saved in config.ini).", FONT_S, C_DIM);

  const uint32_t up = now / 1000;
  textf(X0, 258, FONT_S, C_TEXT, "Up %luh %02lum   Firmware %s (%s)", (unsigned long)(up / 3600),
        (unsigned long)((up / 60) % 60), FW_VERSION_STR, __DATE__);
  const bool armedBoot = confirmed(CF_REBOOT, now);
  if (button(X0, 270, 230, ROW_H, armedBoot ? "Tap again" : "Reboot", armedBoot ? S_DANGER : S_NORMAL)) {
    if (armedBoot) {
      s.pending = A_REBOOT;
    } else {
      s.confirm = CF_REBOOT;
      s.confirmAt = now;
    }
  }

  text(X0, 352, settings::text("title", "The Crystal Oracle"), FONT_S, C_TEXT);
  text(X0, 374, settings::text("credits", "Made for Halloween with ESP32, JPEGDEC, Arduino_GFX and U8g2."), FONT_S,
       C_DIM);
  text(X0, 396, "The voice you hear is AI-generated.", FONT_S, C_WARN);
}

// ================================================================== Crystals page (crystal prep)
//  Test-read, colour, provision, clear and format crystals through the reader (the plinth
//  = the bench's own AITRIP ESP32 + PN532, the rfid2 unit, or a simulated crystal).
//  * While the page is open EVERY reader event is consumed here - Placed (a fresh page image
//    + the read-test tally), Removed (lifted), WriteDone (our write's result), and Button /
//    LinkUp / LinkDown (ignored) - so nothing done here reaches the show afterwards. On the
//    way out the show calls reader::ignoreUntilLifted(): a crystal left on the plinth is not
//    re-announced into the show either.
//  * Writes: a token::build*Writes() list (the guards are there), sent LINK_MAX_WRITES pages
//    at a time with reader::write(); each part's WriteDone (or PREP_WRITE_TIMEOUT_MS) is
//    awaited before the next. Verified pages are copied into the page's image (the plinth
//    doesn't re-read by itself). After a failure the image is stale: lift + place to re-read.
//  * A write in flight keeps being serviced on other tabs and is finished before the menu
//    closes, so its WriteDone never leaks either.
constexpr uint32_t PREP_WRITE_TIMEOUT_MS = 2500;
constexpr uint8_t  PREP_EVENTS_PER_FRAME = 8;

struct Prep {
  bool     have = false;               // a crystal is on the reader, as far as this page knows
  bool     stale = false;              // a write failed: the image may be out of date
  bool     sim = false;                // it is the page's simulated crystal
  TagImage img = {};
  token::Crystal c;
  // read test, since the page was opened
  uint16_t placements = 0, goodReads = 0;
  uint16_t lastReadMs = 0;
  // the write in flight
  bool     busy = false;
  uint8_t  uid[TAG_UID_MAX] = {};
  uint8_t  uidLen = 0;
  PageWrite list[token::PREP_MAX_WRITES] = {};
  uint8_t  n = 0, done = 0, part = 0;  // entries in the list / confirmed / in the request in flight
  uint16_t reqId = 0;
  uint32_t sentAt = 0;
  char     what[40] = {0};
  // the last result
  char     result[80] = {0};
  bool     resultOk = false;
  // colour names (words color.N), cached when the page opens
  const char *colorName[settings::CRYSTAL_COLORS_MAX + 1] = {};
};
Prep p;

void prepResult(bool ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void prepResult(bool ok, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(p.result, sizeof p.result, fmt, ap);
  va_end(ap);
  p.resultOk = ok;
}

const char *kindLabel(token::Kind k) {
  switch (k) {
    case token::Kind::Fresh:   return "blank (not provisioned)";
    case token::Kind::Ours:    return "ours";
    case token::Kind::Foreign: return "FOREIGN tag (not ours)";
    default:                   return "unreadable";
  }
}

const char *writeReason(uint8_t st) {
  switch (st) {
    case WS_NO_TAG:      return "no crystal / no answer";
    case WS_WRONG_TAG:   return "another crystal is on it";
    case WS_WRITE_FAIL:  return "a page wouldn't write";
    case WS_VERIFY_FAIL: return "read-back differs";
    case WS_BAD_REQUEST: return "bad request";
    default:             return "error";
  }
}

bool sameUidAs(const TagImage &a, const uint8_t *uid, uint8_t len) {
  return a.uidLen == len && len > 0 && memcmp(a.uid, uid, len) == 0;
}

void prepParse() { token::parse(p.img, cfg.eventYear, cfg.baseYear, p.c); }

void prepPlaced(const TagImage &img, uint32_t now) {
  p.placements++;
  if (img.readOk) p.goodReads++;
  p.lastReadMs = img.readMs;
  p.img = img;
  p.have = true;
  p.stale = false;
  p.sim = img.uidLen && img.uid[0] == SIM_UID0;
  prepParse();
  s.lastActivity = now;                  // placing crystals keeps the menu open
  audio::chime(img.readOk ? audio::CHIME_TAP : audio::CHIME_ERROR, 45);
}

// A verified part of the list -> the page's image (CC page: only while byte 0 isn't 0xE1,
// exactly what the reader does). Idempotent, so a re-read that raced the write is fine too.
void prepApply(const PageWrite *w, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    if (w[i].page < TAG_FIRST_PAGE || w[i].page > TAG_LAST_PAGE) continue;
    uint8_t *dst = p.img.pages[w[i].page - TAG_FIRST_PAGE];
    if (w[i].page == TAG_FIRST_PAGE && dst[0] == 0xE1) continue;
    memcpy(dst, w[i].data, 4);
  }
  prepParse();
}

void prepSendPart(uint32_t now) {
  const uint8_t left = (uint8_t)(p.n - p.done);
  p.part = left > LINK_MAX_WRITES ? (uint8_t)LINK_MAX_WRITES : left;
  p.reqId = reader::write(p.uid, p.uidLen, p.list + p.done, p.part);
  p.sentAt = now;
}

// Starts sending p.list[0..n) (filled by a token builder; n = 0: `why` says why not).
void prepStart(const char *what, uint8_t n, const char *why, uint32_t now) {
  if (!n) {
    prepResult(false, "%s: %s", what, why ? why : "nothing to write");
    audio::chime(audio::CHIME_ERROR, 40);
    return;
  }
  p.n = n;
  p.done = 0;
  p.busy = true;
  memcpy(p.uid, p.img.uid, p.img.uidLen);
  p.uidLen = p.img.uidLen;
  strlcpy(p.what, what, sizeof p.what);
  p.result[0] = 0;
  vlog::event("service: crystal prep - %s (%u page(s))", what, n);
  prepSendPart(now);
}

void prepFail(const char *reason, uint8_t page) {
  p.busy = false;
  p.stale = true;                        // which pages landed is unknown: re-read before the next write
  if (page) prepResult(false, "%s FAILED: %s (page %u)", p.what, reason, page);
  else prepResult(false, "%s FAILED: %s", p.what, reason);
  audio::chime(audio::CHIME_ERROR, 50);
  vlog::event("service: crystal prep - %s failed: %s (page %u)", p.what, reason, page);
}

void prepWriteDone(const ReaderEvent &ev, uint32_t now) {
  if (!p.busy || ev.reqId != p.reqId) return;   // an old / unknown result: consumed, nothing else
  if (ev.status != WS_OK) {
    prepFail(writeReason(ev.status), ev.failedPage);
    return;
  }
  if (p.have && sameUidAs(p.img, p.uid, p.uidLen)) prepApply(p.list + p.done, p.part);
  p.done = (uint8_t)(p.done + p.part);
  if (p.done < p.n) {                    // the next part (Format: 2 parts)
    prepSendPart(now);
    return;
  }
  p.busy = false;
  prepResult(true, "Saved: %s", p.what);
  audio::chime(audio::CHIME_REVEAL, 50);
}

// Every frame while the page is open (or a write is in flight): ALL reader events land here.
void prepPoll(uint32_t now) {
  static ReaderEvent ev;                 // ~180 B: off the loop stack
  for (uint8_t i = 0; i < PREP_EVENTS_PER_FRAME && reader::poll(ev); i++) {
    switch (ev.type) {
      case ReaderEvt::Placed:
        prepPlaced(ev.tag, now);
        break;
      case ReaderEvt::Removed:
        if (p.have && sameUidAs(p.img, ev.tag.uid, ev.tag.uidLen)) {
          p.have = false;
          p.stale = false;
        }
        s.lastActivity = now;
        break;
      case ReaderEvt::WriteDone:
        prepWriteDone(ev, now);
        break;
      default:                           // Button / LinkUp / LinkDown: consumed (a press here never
        break;                           // turns into a blessing after the menu)
    }
  }
  if (p.busy && now - p.sentAt >= PREP_WRITE_TIMEOUT_MS) prepFail("no answer from the reader", 0);
}

void prepOpen() {
  p.placements = p.goodReads = 0;        // the read test counts from here
  p.lastReadMs = 0;
  for (uint8_t i = 1; i <= settings::CRYSTAL_COLORS_MAX; i++) {
    char key[16];
    snprintf(key, sizeof key, "color.%u", (unsigned)i);
    p.colorName[i] = settings::text(key, "");
  }
}

void prepLiftSim() {
  if (!p.sim) return;
  reader::simulateLifted();
  p.sim = false;
  p.have = false;
  p.stale = false;
}

// Can a write start now? (else a toast says why)
bool prepReady() {
  if (p.busy) { toast("Wait - still writing the crystal"); return false; }
  if (!p.have) { toast("Place a crystal on the reader first"); return false; }
  if (p.stale) { toast("Lift the crystal and place it again (re-read) first"); return false; }
  if (!p.img.readOk) { toast("The read failed - lift it and place it again"); return false; }
  return true;
}

// The crystal's bench colour if the words define it (1..colorCount), else 0.
uint8_t prepColour() {
  const uint8_t k = p.c.color;
  return (k >= 1 && k <= settings::colorCount()) ? k : 0;
}

void spinner(int16_t cx, int16_t cy, uint32_t now) {
  Arduino_GFX &g = display::gfx();
  const uint8_t head = (uint8_t)((now / 90) % 8);
  for (uint8_t i = 0; i < 8; i++) {
    const float a = (float)i * 0.785398f;
    const uint8_t age = (uint8_t)((head + 8 - i) % 8);
    const uint16_t col = age == 0 ? C_TEXT : (age < 3 ? C_EDGE : C_BTN);
    g.fillCircle((int16_t)(cx + 11.0f * cosf(a)), (int16_t)(cy + 11.0f * sinf(a)), age == 0 ? 3 : 2, col);
  }
}

void pageCrystals(uint32_t now) {
  Arduino_GFX &g = display::gfx();
  const token::Crystal &c = p.c;
  const bool read = p.have && p.img.readOk;
  constexpr int16_t SW_X = XR - 66, SW_Y = 160;   // colour swatch (lines left of it stay < x 668)

  // ---- what the reader saw
  int16_t y = 94;
  if (!p.have) {
    text(X0, y, "Place a crystal on the reader.", FONT_B, C_TEXT);
    if (reader::tagPresent())
      text(X0, (int16_t)(y + 28), "One is on it already: lift it and place it again to read it.", FONT_S, C_WARN);
    else if (cfg.reader != READER_NONE && !reader::online())
      text(X0, (int16_t)(y + 28), "The reader isn't answering (plinth off / out of range?).", FONT_S, C_BAD);
    else
      text(X0, (int16_t)(y + 28), "Everything the reader reports stays on this page (never played).", FONT_S, C_DIM);
  } else if (!p.img.readOk) {
    text(X0, y, "READ FAILED - lift it, place it again", FONT_B, C_BAD);
  } else {
    const uint16_t kc = c.kind == token::Kind::Ours ? C_GOOD : (c.kind == token::Kind::Fresh ? C_TEXT : C_BAD);
    textf(X0, y, FONT_B, kc, "Read OK - %s%s", kindLabel(c.kind), p.sim ? " (sim)" : "");
  }
  if (p.have) {
    char uid[TAG_UID_MAX * 3 + 1] = {0};
    for (uint8_t i = 0, o = 0; i < p.img.uidLen && o + 4 <= sizeof uid; i++)
      o += (uint8_t)snprintf(uid + o, sizeof uid - o, i ? ":%02X" : "%02X", p.img.uid[i]);
    textf(X0, (int16_t)(y + 26), FONT_S, C_TEXT, "UID %s   SAK %02X%s", uid, p.img.sak, p.stale ? "   (stale: re-read)" : "");
  }
  if (read) {
    y += 48;
    if (c.kind == token::Kind::Ours) {
      textf(X0, y, FONT_S, C_TEXT, "Visits %u   Charge %u   Years on it %u", c.visits, c.charge, c.historyCount);
    } else if (c.kind == token::Kind::Fresh) {
      text(X0, y, "Not ours yet: Provision, or pick a colour", FONT_S, C_TEXT);
    } else {
      text(X0, y, "Someone else's tag - Format reclaims it", FONT_S, C_WARN);
    }
    y += 22;
    if (c.kind == token::Kind::Ours) {
      if (c.playedThisYear) {
        const uint8_t t = c.thisYear.tier <= 3 ? c.thisYear.tier : 3;
        char key[12];
        snprintf(key, sizeof key, "tier.%u", (unsigned)t);
        textf(X0, y, FONT_S, C_WARN, "%u: played - GLOW %u, %s", cfg.eventYear, c.thisYear.number,
              settings::text(key, "?"));
      } else {
        textf(X0, y, FONT_S, C_TEXT, "%u: not played yet", cfg.eventYear);
      }
    }
    y += 22;
    textf(X0, y, FONT_S, C_DIM, "Link: %s   CC: %s   Page: /c/%08lx",
          c.hasOurLink ? "its own"
                       : (c.kind == token::Kind::Fresh ? "none yet"
                                                       : (c.kind == token::Kind::Ours ? "old - Provision updates it" : "not ours")),
          c.hasCC ? "E1 ok" : "missing", (unsigned long)c.anonId);
  }
  // read test: helps judge a print (glitter PLA, the 2 mm lid)
  if (p.placements) {
    char lat[24];
    if (p.lastReadMs) snprintf(lat, sizeof lat, "%u ms", p.lastReadMs);
    else snprintf(lat, sizeof lat, "n/a");
    textf(X0, 238, FONT_S, p.goodReads == p.placements ? C_GOOD : C_WARN,
          "Read test: %u good of %u placements  |  last read %s", p.goodReads, p.placements, lat);
  } else {
    text(X0, 238, "Read test: place a crystal a few times (good reads / placements)", FONT_S, C_DIM);
  }

  // ---- colour swatch (live: gradient / rainbow colours move)
  const uint8_t col = read && c.kind == token::Kind::Ours ? prepColour() : 0;
  if (col) {
    const uint32_t rgb = settings::crystalRgb(col, now);
    display::addGlow(SW_X, SW_Y, 48, rgb, 120);
    g.fillCircle(SW_X, SW_Y, 28, display::rgb565(rgb));
    g.drawCircle(SW_X, SW_Y, 29, C_TEXT);
    centered(p.colorName[col], SW_X, SW_Y + 54, FONT_S, C_TEXT);
  } else {
    g.drawCircle(SW_X, SW_Y, 28, C_EDGE);
    char nm[24];
    if (read && c.kind == token::Kind::Ours && c.color) snprintf(nm, sizeof nm, "colour %u ?", c.color);
    else snprintf(nm, sizeof nm, "no colour");
    centered(nm, SW_X, SW_Y + 54, FONT_S, C_DIM);
  }

  // ---- colour buttons: None + one per defined colour (words color.N)
  const uint8_t nCol = settings::colorCount();
  const uint8_t k = (uint8_t)(nCol + 1);
  constexpr int16_t CGAP = 8, CY = 248, CH = 52;
  const int16_t cw = (int16_t)((CW - (k - 1) * CGAP) / k);
  for (uint8_t i = 0; i < k; i++) {
    const int16_t bx = (int16_t)(X0 + i * (cw + CGAP));
    const char *label = i ? p.colorName[i] : "None";
    char num[4];
    if (i && (!label[0] || display::textWidth(label, FONT_S) > cw - 12)) {
      snprintf(num, sizeof num, "%u", (unsigned)i);
      label = num;
    }
    const bool current = read && c.kind == token::Kind::Ours && c.color == i;
    if (button(bx, CY, cw, CH, label, current ? S_ON : S_NORMAL) && prepReady()) {
      const char *why = nullptr;
      char what[40];
      if (c.kind == token::Kind::Fresh) snprintf(what, sizeof what, "provision + %s", i ? p.colorName[i] : "no colour");
      else snprintf(what, sizeof what, "colour %s", i ? p.colorName[i] : "none");
      const uint8_t n = token::buildColorWrite(p.img, i, cfg.ndefUrl, p.list, token::PREP_MAX_WRITES, &why);
      prepStart(what, n, why, now);
    }
    if (i) g.fillRect(bx + 10, CY + CH - 9, cw - 20, 4, display::rgb565(settings::crystalRgb(i, now)));
  }

  // ---- actions
  constexpr int16_t AGAP = 10, AY = 314, AH = 56;
  constexpr int16_t aw = (CW - 3 * AGAP) / 4;
  if (button(X0, AY, aw, AH, "Provision", S_ACCENT) && prepReady()) {
    const char *why = nullptr;
    // keeps a colour an interrupted provisioning already wrote (page 17 byte 1), if defined
    const uint8_t keep = p.img.pages[token::PAGE_CHARGE - TAG_FIRST_PAGE][1];
    const uint8_t colour = (keep >= 1 && keep <= nCol) ? keep : 0;
    const uint8_t n = token::buildProvisionWrites(p.img, colour, cfg.ndefUrl, p.list, token::PREP_MAX_WRITES, &why);
    prepStart("provision", n, why, now);
  }
  if (button(X0 + aw + AGAP, AY, aw, AH, "Clear tonight") && prepReady()) {
    const char *why = nullptr;
    const uint8_t n = token::buildClearYearWrites(p.img, cfg.eventYear, cfg.baseYear, p.list, token::PREP_MAX_WRITES, &why);
    char what[40];
    snprintf(what, sizeof what, "clear tonight (%u)", (unsigned)cfg.eventYear);
    prepStart(what, n, why, now);
  }
  const bool armed = confirmed(CF_FORMAT, now);
  if (button(X0 + 2 * (aw + AGAP), AY, aw, AH, armed ? "Tap again" : "Format", armed ? S_DANGER : S_NORMAL)) {
    if (!armed) {
      if (prepReady()) {
        s.confirm = CF_FORMAT;
        s.confirmAt = now;
      }
    } else {
      s.confirm = CF_NONE;
      if (prepReady()) {
        const char *why = nullptr;
        const uint8_t n = token::buildFormatWrites(p.img, p.list, token::PREP_MAX_WRITES, &why);
        prepStart("format", n, why, now);
      }
    }
  }
  if (button(X0 + 3 * (aw + AGAP), AY, aw, AH, p.sim ? "Lift sim" : "Sim blank")) {
    if (p.busy) {
      toast("Wait - still writing the crystal");
    } else if (p.sim) {
      prepLiftSim();
    } else {
      static TagImage img;
      buildSim(img, SIM_BLANK);
      reader::simulatePlaced(img);       // arrives as a Placed event next frame (consumed here)
      vlog::event("service: simulated a blank crystal (Crystals page)");
    }
  }

  // ---- the write in flight / the last result
  constexpr int16_t RY = 404;
  if (p.busy) {
    spinner(X0 + 14, RY - 7, now);
    if (p.n > LINK_MAX_WRITES)
      textf(X0 + 36, RY, FONT_B, C_WARN, "Writing: %s (%u of %u pages)...", p.what, p.done, p.n);
    else
      textf(X0 + 36, RY, FONT_B, C_WARN, "Writing: %s...", p.what);
  } else if (p.result[0]) {
    text(X0, RY, p.result, p.resultOk ? FONT_B : FONT_S, p.resultOk ? C_GOOD : C_BAD);
    if (p.stale && p.have) text(X0, RY + 22, "Lift the crystal and place it again to re-read it.", FONT_S, C_DIM);
  } else {
    textf(X0, RY, FONT_S, C_DIM, "Writes go to this crystal only (UID-checked). Year %u.", cfg.eventYear);
  }
}

// ================================================================== frame + deferred actions

void drawFrame(uint32_t now) {
  if (!display::beginFrame()) return;
  Arduino_GFX &g = display::gfx();
  display::fill(C_BG);

  // header + EXIT
  g.fillRect(0, 0, display::W, HEADER_H, C_HEADER);
  textf(16, 42, FONT_H, C_TEXT, "Service  -  %s", PAGE_NAME[s.page]);
  const uint32_t idle = now - s.lastActivity;
  if (idle > IDLE_EXIT_MS - 20000 && idle < IDLE_EXIT_MS)
    textf(470, 38, FONT_S, C_WARN, "closing in %lu s", (unsigned long)((IDLE_EXIT_MS - idle) / 1000 + 1));
  if (button(650, 6, 142, 48, "EXIT", S_DANGER)) s.exit = true;

  // tabs
  for (uint8_t i = 0; i < P_COUNT; i++) {
    if (button(TAB_X, (int16_t)(TAB_Y0 + i * TAB_STEP), TAB_W, TAB_H, PAGE_NAME[i], i == s.page ? S_ACCENT : S_NORMAL)) {
      if (s.page == P_CRYSTALS && i != P_CRYSTALS) prepLiftSim();   // a simulated crystal leaves with the page
      if (i == P_CRYSTALS && s.page != P_CRYSTALS) prepOpen();     // the read test counts from here
      s.page = (Page)i;
      s.confirm = CF_NONE;
      if (i == P_MEDIA) s.mediaChecked = false;   // fresh report each visit
    }
  }
  g.drawFastVLine(X0 - 10, HEADER_H + 6, display::H - HEADER_H - 12, C_EDGE);

  switch (s.page) {
    case P_TONIGHT: pageTonight(now); break;
    case P_SOUND:   pageSound(now); break;
    case P_MODE:    pageMode(now); break;
    case P_ODDS:    pageOdds(now); break;
    case P_READER:  pageReader(now); break;
    case P_CRYSTALS: pageCrystals(now); break;
    case P_MEDIA:   pageMedia(now); break;
    case P_SYSTEM:  pageSystem(now); break;
    default:        break;
  }

  if (s.toast[0] && now - s.toastAt < 4000) {
    g.fillRoundRect(X0, TOAST_Y, CW, 30, 8, C_HEADER);
    text(X0 + 12, TOAST_Y + 21, s.toast, FONT_S, C_WARN);
  }
  if (s.confirm != CF_NONE && now - s.confirmAt >= CONFIRM_MS) s.confirm = CF_NONE;
  display::endFrame();
}

void drawMessage(const char *msg) {
  if (!display::beginFrame()) return;
  display::fill(C_BG);
  centered(msg, display::W / 2, display::H / 2, FONT_H, C_TEXT);
  display::endFrame();
}

void runPending() {
  const Action a = s.pending;
  s.pending = A_NONE;
  switch (a) {
    case A_PREVIEW:
      previewCue(CUES[s.cue]);
      break;
    case A_BENCH:
      runBenchmark();
      break;
    case A_SCAN:
      scanI2c();
      break;
    case A_RESCAN: {
      const bool was = media::sdOk();
      const bool ok = media::rescan();
      media::flushStillCache();
      if (!was && media::sdOk()) {
        // A card that was missing at boot: settings, tonight's counters and the log
        // were never loaded, so the next save would overwrite them with defaults.
        // Restart to load everything in the normal boot order (no save first!).
        Serial.println("[service] TF card found by rescan - restarting");
        drawMessage("Card found - restarting...");
        delay(400);
        esp_restart();
      }
      if (was && !ok) {
        toast("Card was removed - tap Reboot");
        break;
      }
      checkMedia();
      toast("Card rescanned: %u assets", media::assetCount());
      break;
    }
    case A_PCLK16:
    case A_PCLK12:
      settings::cfg.pclkMhz = (a == A_PCLK16) ? 16 : 12;
      s.dirty = true;                      // kept across reboots (config.ini pclk_mhz)
      display::setPclk((uint32_t)settings::cfg.pclkMhz * 1000000u);
      vlog::event("service: PCLK %lu Hz", (unsigned long)display::pclkHz());
      break;
    case A_REBOOT:
      if (s.dirty) settings::save();
      counters::save();
      vlog::event("service: reboot");
      drawMessage("Rebooting...");
      delay(400);
      esp_restart();
      break;
    default:
      break;
  }
}

// The Tonight page's meter buttons, from the words (meter.name), once per visit:
// "Fill the Lantern" / "Reset Lantern", shortened if the name is too long for a button.
void buildMeterLabels() {
  s.meter = settings::meterName();
  constexpr int16_t FIT = 180 - 16;                  // pageTonight's button width, minus a margin
  snprintf(s.fillLabel, sizeof s.fillLabel, "Fill the %s", s.meter);
  if (display::textWidth(s.fillLabel, FONT_B) > FIT) snprintf(s.fillLabel, sizeof s.fillLabel, "Fill %s", s.meter);
  if (display::textWidth(s.fillLabel, FONT_B) > FIT) snprintf(s.fillLabel, sizeof s.fillLabel, "Fill meter");
  snprintf(s.resetLabel, sizeof s.resetLabel, "Reset %s", s.meter);
  if (display::textWidth(s.resetLabel, FONT_B) > FIT) snprintf(s.resetLabel, sizeof s.resetLabel, "Reset meter");
}

}  // namespace

// ================================================================== public API

void run() {
  if (!display::ok() || !touch::ok()) return;   // nothing to show / nothing to tap with
  s = State();
  p = Prep();
  prepOpen();
  buildMeterLabels();
  s.lastActivity = millis();

  for (;;) {
    const uint32_t frameStart = millis();
    const touch::Gestures &g = touch::update(60000);   // long-press unused here
    if (!s.armed && !g.down) s.armed = true;          // wait for the entry long-press to end
    s.tap = g.tap;
    s.tapAt = g.tapAt;
    s.down = g.down;
    s.at = g.at;
    if (g.down || g.tap || p.busy) s.lastActivity = frameStart;
    if (frameStart - s.lastActivity >= IDLE_EXIT_MS) break;

    // The Crystals page takes every reader event (and a write in flight is serviced on any page)
    if (s.page == P_CRYSTALS || p.busy) prepPoll(frameStart);

    drawFrame(frameStart);
    if (s.exit) break;
    if (s.pending != A_NONE) runPending();

    const uint32_t spent = millis() - frameStart;
    delay(spent < FRAME_MS ? FRAME_MS - spent : 1);
  }

  // Leaving with a crystal write in flight: finish it here (its result must not reach the show).
  const uint32_t t0 = millis();
  while (p.busy && millis() - t0 < 3 * PREP_WRITE_TIMEOUT_MS) {
    prepPoll(millis());
    drawMessage("Finishing the crystal write...");
    delay(FRAME_MS);
  }
  if (s.page == P_CRYSTALS) prepPoll(millis());   // the page was open: its last events stay here too
  prepLiftSim();

  if (s.dirty) {
    const bool ok = settings::save();
    vlog::event("service: settings %s", ok ? "saved to config.ini" : "NOT saved (TF card?)");
  }
}

}  // namespace service
