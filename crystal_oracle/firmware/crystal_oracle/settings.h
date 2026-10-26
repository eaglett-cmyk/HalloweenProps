#pragma once
// ============================================================================
//  settings.h — station settings from /oracle/config.ini (over built-in
//  defaults), on-screen words from /oracle/captions.ini (over story_defaults.h),
//  and persistent counters in /oracle/state/.
//
//  The crystal colours (color.N_hex: steady, 2-4 stop gradient or rainbow) are
//  parsed once into a table: settings::crystalRgb() / colorCount() ...
//
//  INI format: [section] then key = value; ';' or '#' starts a comment; keys are
//  looked up as "section.key" (case-insensitive). Unknown keys are ignored, so
//  old SD cards keep working with new firmware.
//
//  NOTHING here is written to the ESP32's internal flash (NVS/Preferences):
//  flash writes stall the PSRAM cache and make the RGB panel drift. All state
//  lives on the TF card.
// ============================================================================
#include <Arduino.h>

enum RushMode : uint8_t { RUSH_AUTO = 0, RUSH_ON = 1, RUSH_OFF = 2 };
enum ReaderKind : uint8_t { READER_PLINTH = 0, READER_RFID2 = 1, READER_NONE = 2 };

struct Settings {
  // [station]
  uint16_t eventYear      = 2026;   // which year slot this Halloween writes (bump yearly — or edit config.ini)
  uint16_t baseYear       = 2026;   // year mapped to tag page 18 (NEVER change after crystals exist)
  uint8_t  chapter        = 1;      // story chapter number written into the year slot
  uint8_t  reader         = READER_PLINTH;
  bool     hasButton      = false;  // the plinth has a big button (config.ini [station] button = yes); no = taps bless + words nobutton.*
  uint16_t group          = 1;      // ESP-NOW station id (must match the plinth)
  uint8_t  channel        = 1;      // WiFi channel for ESP-NOW (must match the plinth)
  uint8_t  volume         = 75;     // 0..100
  uint8_t  brightness     = 85;     // backlight 5..100
  uint8_t  ringBrightness = 120;    // plinth LED ring cap 0..255
  uint8_t  pclkMhz        = 12;     // screen pixel clock MHz (8..16): 12; 16 made the real panel jump (2026-10-22)
  uint8_t  rush           = RUSH_AUTO;
  bool     secondHelpings = false;  // replay (already played tonight) also points at the common bowl
  bool     allowFresh     = true;   // awaken BLANK tags that were never bench-provisioned
  bool     logVisits      = true;   // anonymised CSV on the SD card
  bool     showCounter    = true;   // station-wide "crystals awakened" counter on the attract screen
  char     ndefUrl[64]    = "halloween.ciancio.cloud";                 // crystals get <ndefUrl>/c/<anonId> (https:// prefix implied)
  // [odds]  (percent of crystal visits; trick+treat get the rest)
  uint8_t  trickPct       = 15;
  uint8_t  rarePct        = 18;
  uint8_t  legendPct      = 4;
  uint8_t  loyaltyRarePct = 3;      // + per PRIOR visit to the rare chance ...
  uint8_t  loyaltyCapPct  = 9;      // ... capped
  uint8_t  chargeRarePct  = 5;      // + when the optional charge meter is filled (scaled by fill)
  uint8_t  pityAfter      = 2;      // this many prior visits in a row without rare+ => guaranteed rare
  bool     firstNoTrick   = true;   // a crystal's very first visit is never a trick
  // [stock]  (0 = unlimited; pacing spreads limited prizes over the night)
  uint16_t rareStock      = 0;
  uint16_t legendStock    = 0;
  uint16_t expectedKids   = 200;    // expected crystal visits tonight (pacing)
  // [timing]
  uint16_t attractCallSec = 30;     // a teaser voice line every N seconds while idle
  uint16_t chapterEvery   = 4;      // play the chapter clip every N attract loops (0 = never)
  uint16_t chargeMs       = 2500;   // optional tap/press window for returning crystals
  uint16_t writeTimeoutMs = 1500;   // keep retrying the tag write this long while the crystal is present
  uint16_t milestoneEvery = 100;    // station counter milestones
  uint16_t rushGapSec     = 8;      // a new crystal within N s of the previous sequence => rush
  uint16_t rushQuietSec   = 45;     // rush ends after N s with no crystals
  uint16_t groupGapSec    = 45;     // crystals closer than this belong to one group ("circle")
  uint16_t tapCooldownSec = 6;      // button = no: taps are ignored this long after a sequence ends
  uint16_t tapSparkSec    = 20;     // button = no: a tap blessing adds a meter spark at most this often
  // [story]
  uint16_t moonGoal       = 50;     // meter points tonight (crystal visits + no-crystal blessings) for the finale
  uint8_t  keeperOneIn    = 3;      // top-rank crystals hear the keeper secret about 1 visit in N (0 = never)
};

namespace settings {

extern Settings cfg;

bool load();     // defaults + /oracle/config.ini (missing file = defaults, returns false)
bool save();     // rewrite /oracle/config.ini from cfg (service-menu changes); keeps a .bak

// /oracle/captions.ini lookups, e.g. text("rank.3", "Glow"), text("caption.awaken", "Awake!")
// Falls back to story_defaults.h, then to `fallback`. Returned pointers stay valid.
// No-button mode (cfg.hasButton == false): the word "nobutton.<key>" (captions.ini, then
// story_defaults.h) is returned instead of <key> when it exists - even when it is empty.
// captions.ini writes them as a [nobutton] section ("caption.charge = TAP! TAP!" there is the
// key "nobutton.caption.charge"). Keys that start with "nobutton." are looked up as they are.
const char *text(const char *key, const char *fallback = "");
uint32_t color(const char *key, uint32_t fallbackRgb);   // values like "#7C3AED" (same override)
// The communal meter's name (words meter.name; if empty: "Lantern" or "Moon" by meter.style).
const char *meterName();

// ---- crystal colours: words color.N_hex, N = the bench colour id (tag page 17 byte 1) ----
// A value is one of
//   "#RRGGBB"                                   a steady colour
//   "#RRGGBB,#RRGGBB[,#RRGGBB[,#RRGGBB]]"       2-4 stops blended smoothly round and round,
//                                               ~4 s per full cycle (a gradient filament)
//   "rainbow"                                   the full hue wheel, ~4 s per turn
// Parsed once from the words (again after every load()); never per frame. A value that
// can't be read (or a stop of it) is logged and replaced by a built-in colour - never fatal.
constexpr uint8_t  CRYSTAL_COLORS_MAX = 6;
constexpr uint8_t  CRYSTAL_STOPS_MAX  = 4;
constexpr uint32_t CRYSTAL_CYCLE_MS   = 4000;
// Defined colours, 0..6: the highest N with a non-empty color.N_hex, counting from 1 without gaps.
uint8_t  colorCount();
// Crystal `id`'s colour at time tMs (pass millis()): steady, the blend of its stops, or the
// rainbow hue (saturated but not neon, for the RGB565 panel). Ids outside 1..6 use colour 1.
uint32_t crystalRgb(uint8_t id, uint32_t tMs);
bool     crystalAnimated(uint8_t id);   // 2+ stops or rainbow (the colour changes with time)
bool     crystalRainbow(uint8_t id);
// Copies up to `max` stops into out[] and returns how many: steady = 1, gradient = 2..4,
// rainbow = 0 (no fixed stops: use crystalRgb()).
uint8_t  crystalStops(uint8_t id, uint32_t *out, uint8_t max);

}  // namespace settings

// Persistent counters (TF card). Tonight = this eventYear; station = all time.
struct Counters {
  uint32_t stationCrystals = 0;   // all-time crystal visits (the station-wide counter shown on screen)
  uint32_t stationAwakened = 0;   // all-time first visits
  uint16_t visits = 0;            // tonight: crystal visits that rolled
  uint16_t awakened = 0, returning = 0, replays = 0, noCrystal = 0, sleepy = 0, foreign = 0;
  uint16_t tier[4] = {0, 0, 0, 0};
  uint16_t rareGiven = 0, legendGiven = 0;
  uint16_t writeFails = 0;
  uint16_t moonPoints = 0;        // tonight's communal meter (words meter.*): +1 per crystal visit that rolled, +1 per no-crystal / foreign blessing
  bool     moonFull = false;      // the moonfull finale has played tonight (attract switches to attract_full)
};

namespace counters {
extern Counters c;
bool load(uint16_t eventYear);   // /oracle/state/station.ini + /oracle/state/tonight_<year>.ini
bool save();                     // cheap; called after every visit
void resetTonight();             // service menu
}  // namespace counters
