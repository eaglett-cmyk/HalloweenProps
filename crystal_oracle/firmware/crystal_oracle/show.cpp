/*
 * ============================================================================
 *  show.cpp — the Crystal Oracle's director (implements show.h)
 *  Story-neutral: the story (STORY.md) comes from the TF card - its art, voice
 *  and the captions.ini words (defaults: story_defaults.h, generated from the
 *  manifest). The words also pick the story features drawn here:
 *    meter.style / pos / name / color / on / zoom_<visual>   the communal meter
 *        (a Moon in the sky, or the glow of a lantern painted into the art),
 *        where it sits, its name in the host menu, and which visuals show it
 *    fx.<visual id>        device effects on a visual (trick gags: poof /
 *                          sparkles / burst at given places and times)
 *    glow.idle_color       the narrator's anchor glow with no crystal around
 *    color.N / color.N_hex the crystal colours: steady, a 2-4 stop gradient or
 *                          "rainbow" (settings::crystalRgb). An animated colour
 *                          is re-evaluated ONCE per frame (Seq::rgb, and the
 *                          step's rgb when it follows the crystal), so glows,
 *                          beam, sparkles, gems, aura and bursts all move with
 *                          it; the plinth gets LED_DUAL / LED_RAINBOW (crystalLed)
 * ----------------------------------------------------------------------------
 *  Everything a kid sees and hears is a chain of STEPS. One step =
 *    * a visual: clip (.omj) -> still (.jpg) -> procedural fallback (fx), looked
 *      up as <id>_<n> first (n = an explicit variant such as the crystal colour
 *      or the trick gag, else the variant the first voice part picked), then
 *      plain <id>, for up to three ids;
 *    * voice parts played back to back (awaken_a / _b_<colour> / _c,
 *      reveal + bowl line, moonfull_a / _b ...), each with an optional caption
 *      and an earliest start time, plus an sfx at the start (a generated chime
 *      when the .wav is missing);
 *    * live overlays: the narrator's glow at the clip's anchor (crystal colour,
 *      pulsing with the voice), the communal meter (on the visuals listed in
 *      meter.on), the gem meter / number / smudge / bowl icon ..., the
 *      visual's fx.* effects, sparkles, the caption band (with {year}
 *      {next_year} {rank} {color} tokens) and the host badges;
 *  held for max(min, voice + 250 ms), capped at max (DESIGN.md §5.1, using the
 *  rush column when rush mode is on). A sequenced part is never STARTED with
 *  < 1.5 s left; a line still running at the cap is cut by the next step.
 *
 *  Layout: when the step's art (clip / still) is on screen, everything the
 *  device writes lives in the bottom band (y >= 360, kept dark by the artist):
 *  caption centred, GLOW number (or the smudge cloud) on the right, bowl icon
 *  on the left (STORY.md §9). With the procedural fallback (no art) the number,
 *  titles and bowl icon use the middle of the screen instead.
 *
 *  The step runner polls touch + reader EVERY frame and routes events (§5.2):
 *    Placed     another crystal -> kept as the NEXT crystal (runs right after;
 *               the oracle's rush logic sees the short gap)
 *               the same crystal -> "re-presented": retries a failed write
 *               (or confirms it, if the new page image already holds our data)
 *    Removed    noted (the story finishes anyway); un-queues a waiting crystal
 *    WriteDone  the tag write result (matched by reqId); a failure while the
 *               crystal is still on the plinth is retried once
 *    Button     CHARGE: adds charge. Idle with no crystal on the reader: the
 *               no-crystal blessing. Otherwise ignored.
 *    tap        CHARGE: adds charge + a sparkle burst at the finger. Idle,
 *               outside the host's top-left corner (tapBlesses()):
 *                 button = yes  with no reader online: the blessing (the
 *                               screen alone is still fun)
 *                 button = no   (the plinth has no big button, DESIGN.md §5.7)
 *                               with no crystal on the reader: the blessing,
 *                               except for tap_cooldown_sec after a sequence;
 *                               a tap blessing adds a meter spark at most
 *                               every tap_spark_sec
 *               Otherwise a little sparkle burst.
 *    long-press top-left 100x100 for 3 s while idle -> service menu.
 *  No-button mode also swaps words (settings::text reads nobutton.<key> first)
 *  and voice lines (the word nobutton.voice, resolveVoice()).
 *
 *  Flows (show.h + DESIGN.md §5 + STORY.md §2):
 *    ATTRACT   attract (attract_full after the finale) looped + music bed
 *              (idle_full after the finale) + attract_call every N s + the
 *              chapter story (parts a..f) every N loops; captions alternate
 *              "Crystal here" / ui.or_press ("OR PRESS"; no button: the words'
 *              nobutton.ui.or_press, a tap prompt); the plinth ring cycles the defined
 *              crystal colours; the sidekick's "munch" sfx at ~5.4 s into each
 *              loop; the meter + the station counter under it.
 *    crystal   parse -> [first visit / rush: decide + WRITE now] -> ABSORB
 *              (beam of the crystal's colour into the glow anchor, its name if bench-set) ->
 *      Unreadable   SLEEPY (a re-placement ends it at once)
 *      Foreign      NOCRYSTAL (foreign visuals/voice if present) + bowl line
 *      played       REPLAY (tonight's number + tier dimmed, check mark)
 *                   [+ bowl line if secondHelpings]
 *      first        AWAKEN (awaken_<colour>; awaken_a -> awaken_b_<colour> ->
 *                   awaken_c; rush: awaken_c only) -> reveal chain
 *      returning    GREET r<n> -> [RECALL] -> [CHARGE -> decide + WRITE] -> reveal chain
 *      reveal chain [HOLD STILL until the write is in] -> record (+1 meter point) ->
 *                   REVEAL t<tier> with the bowl line over its end (rush: one
 *                   combined rush_t<tier> line, bowl icon from the start)
 *                   -> [KEEPER ~1 in keeper_one_in] [MILESTONE] [CIRCLE] -> [GOODBYE]
 *    no crystal NOCRYSTAL (+1 meter point, blessing, ring in a random crystal colour)
 *              + bowl line (common)
 *    MOONFULL  once per night, right after the sequence that fills the meter
 *              (or when the host menu's "Fill the <meter.name>" closes).
 *  After every sequence: visit logged, counters saved, plinth back to idle.
 *
 *  Robustness: every step works with an EMPTY TF card (procedural visuals +
 *  generated chimes), without audio (timings fall back to the min times, part
 *  captions advance by time), and without a reader (attract + tap-for-a-
 *  blessing). A tag write failure never blocks or punishes a kid: the result
 *  is shown anyway and the failure logged. No heap allocation per frame (one
 *  768 KB PSRAM freeze buffer at begin), no SD access per frame (media's name
 *  cache answers every lookup; anchors are parsed once per step).
 *
 *  Simulated crystals (service menu): UID byte 0 == 0xF5 (real NTAG UIDs start
 *  with NXP's 0x04). They run the full show, but no tag write is sent (it
 *  "succeeds" after 400 ms) and nothing is recorded in counters / stock / the
 *  meter / the visit CSV, so testing at the door never spends tonight's prizes.
 * ============================================================================
 */
#include "show.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_heap_caps.h"
#include "esp_random.h"

#include "audio.h"
#include "cues.h"
#include "display.h"
#include "fx.h"
#include "media.h"
#include "oracle.h"
#include "reader.h"
#include "service.h"
#include "settings.h"
#include "token.h"
#include "touch.h"
#include "vlog.h"

namespace show {

namespace {

using settings::cfg;

// ------------------------------------------------------------------ layout
// The bottom quarter of every clip/still (y >= 360) is kept dark by the artist;
// with art on screen the device writes there. Fallback (no art) uses the middle.
constexpr int16_t CX       = display::W / 2;   // 400
constexpr int16_t CY       = 222;              // centre of the art area
constexpr int16_t Y_TITLE  = 88;               // fallback: rank name / keeper title (top)
constexpr int16_t Y_POWER  = 118;              // fallback: "GLOW" above the big number
constexpr int16_t Y_NUMBER = 250;              // fallback: big-number baseline (92 px digits)
constexpr int16_t Y_TIER   = 334;              // fallback: tier name under the number
constexpr int16_t Y_METER  = 374;              // crystal meter, top of the band
constexpr int16_t NUMBER_MID = Y_NUMBER - 46;  // vertical middle of the big digits
// art layout: GLOW number at the right of the band, bowl icon at the left
constexpr int16_t NUM_ART_X     = 690;
constexpr int16_t NUM_ART_BASE  = 472;
constexpr int16_t NUM_ART_LABEL = 366;
constexpr int16_t NUM_ART_MID   = NUM_ART_BASE - 46;
constexpr int16_t BAND_BOWL_X = 104, BAND_BOWL_Y = 422, BAND_BOWL_SIZE = 88, MINI_BOWL_SIZE = 62;
constexpr int16_t FB_BOWL_X = 150, FB_BOWL_Y = 206, FB_BOWL_SIZE = 130;   // fallback layout
constexpr int16_t HINT_L = 132, HINT_R = 668;  // place-crystal hints flanking the caption
// The meter's place comes from the words (meter.pos); the station counter sits
// under it and last year's faded RECALL icon in the top corner AWAY from it.
constexpr int16_t RECALL_EDGE_X = 116, RECALL_Y = 150;
constexpr int16_t SERVICE_ZONE = 100;          // top-left square for the service long-press

// ------------------------------------------------------------------ colours
constexpr uint32_t SLEEPY_WHITE = 0x707888;    // "LEDs breathe a dim white"

// ------------------------------------------------------------------ timing
constexpr uint32_t FRAME_MS_STILL   = 50;      // stills / procedural: 20 fps
constexpr uint32_t TOUCH_POLL_MS    = 20;      // touch is also polled while waiting out a frame
constexpr uint32_t VOICE_TAIL_MS    = 250;     // a step lasts voice + 250 ms (if longer than its min)
constexpr uint32_t PART_MIN_ROOM_MS = 1500;    // don't start another sequenced part with less time left
constexpr uint32_t VOICE_STUCK_MS   = 1500;    // voiceBusy() may lag the returned duration by this much
constexpr uint32_t LONG_PRESS_MS    = 3000;    // service menu
constexpr uint32_t ATTRACT_LOOP_MS  = 20000;   // "one attract loop" when the attract visual isn't a clip
constexpr uint32_t MUSIC_RETRY_MS   = 15000;   // re-start the idle bed if it stopped
constexpr uint32_t SIM_WRITE_MS     = 400;     // simulated crystals: fake write latency
constexpr uint32_t CAPTION_SWAP_MS  = 2500;    // attract: "Crystal here" <-> "OR PRESS"
constexpr uint32_t LED_CYCLE_MS     = 6000;    // attract: plinth idle colour changes
constexpr uint32_t AURA_STEP_MS     = 250;     // a moving aura colour steps this often (the orb aura is cached per colour)
constexpr uint32_t MUNCH_AT_MS      = 5400;    // attract: the sidekick's "munch" sfx this far into each loop
constexpr uint32_t MOON_EASE_MS     = 1500;    // meter phase transition
constexpr uint32_t MOON_PULSE_UP_MS = 300, MOON_PULSE_DOWN_MS = 1500;   // gentle swell per point (< 1 Hz)
constexpr uint8_t  CHARGE_STEP_PCT  = 12;      // per tap / button press
constexpr uint8_t  MAX_EVENTS_PER_FRAME = 8;
constexpr uint8_t  SIM_UID0         = 0xF5;    // simulated crystal marker (service.cpp builds these)
constexpr uint8_t  MAX_GAGS         = 5;       // reveal_t0_1 .. _5 sfx preloaded
constexpr uint8_t  GROUP_MAX        = 8;       // crystals remembered for the circle orbit

// DESIGN.md §5.1 / STORY.md §2. A step lasts max(min, voice + 250 ms), capped at max.
// rushMax == 0 means "skipped in rush" (the flow checks this before the step).
// The caps only bite on long lines (a short line still ends its step at voice + 250 ms), and a
// step that hits its cap cuts the line off - so every cap that carries a line is at least LINE_MS.
// 2026-10-22: Butterscotch's slow rumbly voice runs 2.2-7 s a line (was 2-2.5 s), rush included.
struct Timing { uint16_t minMs, maxMs, rushMin, rushMax; };
constexpr uint16_t LINE_MS   = 7500;                       // one voice line (<= ~7 s) + VOICE_TAIL_MS
constexpr Timing T_ABSORB    = {1000, 1000,  600,  600};
constexpr Timing T_GREET     = {2000, LINE_MS, 1200, 2000};   // rush: no voice
constexpr Timing T_RECALL    = {1500, LINE_MS,    0,    0};
constexpr Timing T_AWAKEN    = {6000, 3 * LINE_MS, 1500, LINE_MS};   // awaken_a + _b + _c; rush: _c only
constexpr Timing T_REVEAL    = {2500, LINE_MS, 1500, LINE_MS};   // rush: the combined rush_t<n> line
constexpr Timing T_REVEAL3   = {3000, LINE_MS + 500, 1500, LINE_MS};   // legendary gets a moment longer
constexpr Timing T_BOWL      = {1500, LINE_MS,    0,    0};   // the bowl line over the end of a reveal / blessing
constexpr Timing T_BOWL_NC   = {1500, LINE_MS, 1200, LINE_MS};   // after the no-crystal blessing (also in rush)
constexpr Timing T_EXTRA     = {1500, LINE_MS, 1200, LINE_MS};   // keeper / milestone / circle
constexpr Timing T_GOODBYE   = {1500, LINE_MS,    0,    0};
constexpr Timing T_REPLAY    = {2000, LINE_MS, 1500, LINE_MS};
constexpr Timing T_NOCRYSTAL = {2500, LINE_MS, 1800, LINE_MS};
constexpr Timing T_SLEEPY    = {2000, LINE_MS, 2000, LINE_MS};
constexpr uint32_t CHAPTER_PART_MIN_MS = 4500;   // a part with no voice (a clip sets its own length)
constexpr uint32_t CHAPTER_PART_MAX_MS = 12000;
constexpr uint32_t MOONFULL_MIN_MS = 6000, MOONFULL_MAX_MS = 16000;

// ------------------------------------------------------------------ steps
enum class Mode : uint8_t {
  Attract, Chapter, Absorb, Awaken, Greet, Recall, Charge, Hold,
  Reveal, Extra, Goodbye, Replay, NoCrystal, Sleepy, MoonFull
};

using Overlay = void (*)(uint32_t t);   // draws on top of the visual (t = ms since step start)
using Tick    = bool (*)(uint32_t t);   // per-frame logic; return true to end the step now

constexpr uint8_t MAX_PARTS = 6;
constexpr uint8_t MAX_VIS = 3;
constexpr size_t  ID_LEN = 24;
constexpr uint8_t NO_PART = 0xFF;

struct Step {
  Mode     mode = Mode::Attract;
  char     vis[MAX_VIS][ID_LEN] = {};  // visual id chain: first that exists wins
  uint8_t  visVariant = 0;             // explicit visual variant (0 = the first voice part's variant)
  char     voice[MAX_PARTS][ID_LEN] = {};   // voice groups, played back to back
  uint8_t  voiceVar[MAX_PARTS] = {};   // explicit variant per part (0 = random / coherent)
  const char *partCap[MAX_PARTS] = {}; // caption while that part plays (nullptr = unchanged)
  uint32_t partNotBefore[MAX_PARTS] = {};   // earliest step time each part may start
  uint8_t  nVoice = 0;
  bool     coherent = true;            // later parts reuse the first part's variant number if they have it
  const char *caption = "";            // caption band ("" = none); may contain {tokens}
  const char *fbTitle = nullptr;       // title for fx::fallbackCue (nullptr = plain orb, overlay does the talking)
  const char *titleDup = nullptr;      // fallback layout draws this as a title: hide an identical caption
  uint32_t minMs = 0, maxMs = 0;
  bool     minFromClip = false;        // min = the clip's length (chapter / finale)
  bool     loop = false;               // loop the clip (else hold its last frame)
  bool     freeze = false;             // background = dimmed snapshot of the last frame (HOLD)
  bool     interruptible = false;      // a queued crystal / button / tap / service request ends it
  bool     rainbow = false;            // fallback colour cycles through the crystal colours
  bool     crystalTint = false;        // rgb = the crystal's colour, live (animated colours move every frame)
  // (the meter is drawn when the visual on screen is listed in meter.on: see loadStepMeter)
  uint8_t  energy = 120;               // fallback orb energy 0..255
  uint32_t rgb = 0x8B5CF6;             // fallback / accent colour
  uint8_t  ambient = 0;                // ambient sparkles per second
  int      sfx = -1;                   // at step start: sfx handle ...
  int8_t   chime = -1;                 // ... or this generated chime when the handle is -1
  // bowl icon: shown from part `bowlPart` on (when that line starts) or from step time `bowlAt`
  int8_t   bowlTier = -1;              // -1 = none
  bool     bowlMini = false;           // small reminder icon (after the reveal)
  uint8_t  bowlPart = NO_PART;
  uint32_t bowlAt = UINT32_MAX;
  Overlay  overlay = nullptr;          // drawn before the caption band
  Overlay  band = nullptr;             // drawn after the caption band (bottom-band elements)
  Tick     tick = nullptr;
};

// Per-step runtime state (voice sequencing + visual)
struct Run {
  uint8_t  part = 0;          // index of the part playing / to start next
  bool     speaking = false;
  bool     allDone = false;
  bool     anySpoke = false;
  int8_t   lastStarted = -1;  // highest part index that actually started
  uint32_t partStart = 0;     // step time the current part started
  uint32_t partDur = 0;
  uint32_t doneAt = 0;        // step time the last part finished (0 = no voice)
  uint8_t  variant = 0;       // variant number of the first part that played
  char     path[48] = {0};    // current voice file (for the cut note)
};

// Glow anchor of the visual on screen (captions.ini [anchor] "x,y,r"): where the
// narrator's voice-pulsed glow sits (Oona's lantern, Butterscotch's tummy ...)
struct Anchor { bool ok = false; int16_t x = 0, y = 0, r = 0; };

// The communal meter (words meter.*), read once at begin(). Tonight's points /
// moon_goal fill it (phase 0..8); "moon" = a Moon in the sky (Moonshards),
// "lantern" = the glow of a lantern painted into the art (Dragonglow).
enum class MeterStyle : uint8_t { Moon, Lantern };
constexpr uint8_t METER_ON_MAX = 32;
struct Meter {
  MeterStyle  style = MeterStyle::Moon;
  int16_t     x = 650, y = 110, r = 46;       // meter.pos "x,y,r" (r = Moon radius / glow radius; loadMeter sets each style's default)
  uint32_t    rgb = 0xE9EEFF;                  // meter.color
  const char *name = "Moon";                   // meter.name (host menu, boot screen)
  int16_t     counterBase = 194;               // station counter baseline under the meter
  int16_t     recallX = 116, recallY = 150;    // RECALL icon: the top corner away from the meter
  char        onBuf[256] = {0};                // meter.on, split in place into on[]
  const char *on[METER_ON_MAX] = {};
  uint8_t     nOn = 0;
};
// meter.on when the words don't say: the visuals that showed the Moon in Moonshards.
constexpr const char *METER_ON_DEFAULT =
    "attract,absorb,greet,recall,charge,reveal_t0,reveal_t1,replay,nocrystal,foreign,sleepy,goodbye,chapter_a,chapter_f";

// Device effects on a visual (words fx.<visual id>, e.g. fx.reveal_t0_2), parsed
// once per step: "kind,x,y,startMs,endMs[,jx,jy][,#RRGGBB]", several separated
// by ';'. kind: poof (a white puff at start), sparkles (3 sparkles around (x, y)
// +-jx/jy every 120 ms from start until end), burst (one sparkle burst at start),
// none. Colour: the crystal's (poof: white) unless #RRGGBB is given.
enum class FxKind : uint8_t { None, Poof, Sparkles, Burst };
struct StepFx {
  FxKind   kind = FxKind::None;
  int16_t  x = 0, y = 0, jx = 0, jy = 0;
  uint32_t startMs = 0, endMs = 0;
  bool     hasRgb = false;
  uint32_t rgb = 0;
  bool     fired = false;      // the one-shot part has happened
  uint32_t next = 0;           // sparkles: next spawn (step time)
};
constexpr uint8_t  STEP_FX_MAX = 4;
constexpr uint32_t FX_SPARKLE_EVERY_MS = 120;
constexpr uint32_t FX_POOF_MS = 500;

// ------------------------------------------------------------------ sequence
struct Seq {
  bool     active = false;
  bool     simulated = false;
  bool     rush = false;
  bool     first = false;        // first visit (awakening)
  bool     removed = false;      // the crystal left the plinth
  token::Crystal c;
  uint8_t  colorIdx = 1;         // 1..colorCount (token::displayColor, or random for no crystal)
  uint32_t rgb = 0x8B5CF6;       // crystal colour THIS frame (refreshCrystalRgb)
  bool     anim = false;         // the colour moves (a gradient or rainbow crystal)
  const char *path = nullptr;    // visit-log path name
  // outcome + tag write
  bool     decided = false;
  bool     recorded = false;
  Outcome  o;
  PageWrite writes[LINK_MAX_WRITES];
  uint8_t  nWrites = 0;
  bool     noWrites = false;     // decided but no write list could be built (year out of range)
  uint16_t writeReq = 0;
  bool     writePending = false;
  bool     writeOk = false;
  uint8_t  writeStatus = WS_OK;
  uint8_t  failedPage = 0;
  uint8_t  writeTries = 0;
  uint32_t writeSentMs = 0;
  uint32_t simDoneAt = 0;
  // story state used by overlays
  uint8_t  rankBefore = 1, rankAfter = 1, meterFrom = 1, meterTo = 2;
  uint32_t gemAt = 0, gemAt2 = 0;   // step times the new gem(s) light (greet: one, awaken: two)
  uint8_t  gemsLit = 0;
  bool     gemLit = false;
  uint8_t  chargeFill = 0;       // 0..100
  int32_t  chargeFullT = -1;     // step time the charge meter filled (-1 = not yet)
  uint8_t  chargeLedSent = 0xFF;
  uint32_t lockAt = 0;           // REVEAL: step time the rolling number locks
  bool     locked = false;
  uint8_t  gag = 0;              // trick gag 1..n (tier 0)
  int8_t   bowlTier = -1;        // tier whose bowl was shown (mini reminder icon), -1 = none
  int32_t  bowlShownAt = -1;     // step time the step's bowl icon appeared (pop-in)
  bool     milestone = false;
  bool     circle = false;
  uint32_t fxNext = 0;           // per-step effect scheduler (confetti, wisp trail)
  bool     fxDone = false;
  char     capRank[32] = {0};    // "Spark!"
  char     recallLine[16] = {0};
};

// ------------------------------------------------------------------ words + colours (cached once)
struct Words {
  const char *title = "The Crystal Oracle";
  const char *rank[8] = {};          // 1..7
  const char *tier[4] = {};
  uint32_t    tierRgb[4] = {0x84CC16, 0x22C55E, 0xA855F7, 0xF59E0B};
  const char *bowlName[4] = {};      // by tier (cue::BOWL_KEY)
  uint32_t    bowlRgb[4] = {0x22C55E, 0x22C55E, 0xA855F7, 0xF59E0B};
  fx::Shape   bowlShape[4] = {fx::Shape::Circle, fx::Shape::Circle, fx::Shape::Triangle, fx::Shape::Star};
  // (the crystal colours themselves: settings::crystalRgb(id, now) - they may move)
  const char *colorName[7] = {};     // 1..6
  const char *capAttract = "", *capAbsorb = "", *capAwaken = "", *capGreet = "", *capRecall = "";
  const char *capCharge = "", *capReveal[4] = {"", "", "", ""}, *capReplay = "", *capNoCrystal = "";
  const char *capForeign = "", *capSleepy = "", *capGoodbye = "", *capMilestone = "";
  const char *capCircle = "", *capKeeper = "", *capChapter = "", *capMoonFull = "", *capMoonFull2 = "";
  const char *capChapterPart[6] = {"", "", "", "", "", ""};
  const char *uiHold = "", *uiPower = "", *uiCounter = "", *uiPress = "", *uiAsleep = "";
  const char *uiRush = "", *uiTouch = "", *uiNoCard = "", *uiOrPress = "", *uiTonight = "", *uiPlace = "";
  uint32_t    glowIdle = 0xCFFFF2;   // glow.idle_color: the anchor glow with no crystal (Moonshards' mint)
};

struct SfxHandles {
  int absorb = -1, awaken = -1, tap = -1, full = -1, sleepy = -1, button = -1, whoosh = -1;
  int munch = -1, milestone = -1, moonfull = -1;
  int reveal[4] = {-1, -1, -1, -1};
  int gag[MAX_GAGS + 1] = {-1, -1, -1, -1, -1, -1};   // reveal_t0_<n>, 1..MAX_GAGS
};

// Remembers the last voice variant per group ("never the same line twice in a row").
struct VariantMemo { char group[ID_LEN]; uint8_t last; };
constexpr uint8_t MEMO_MAX = 64;

struct GroupMember { uint32_t anonId; uint8_t colorIdx; };

// ------------------------------------------------------------------ state
Words       s_w;
SfxHandles  s_sfx;
Step        s_step;
Run         s_run;
Seq         s_seq;
Mode        s_mode = Mode::Attract;
uint32_t    s_stepT0 = 0;
media::Visual s_vis;
char        s_visBase[ID_LEN] = {0};   // the step id whose visual is on screen ("" = fallback)
media::Clip s_clip;
uint8_t     s_clipFails = 0;
bool        s_stillFailed = false;
bool        s_endStep = false;
bool        s_art = false;             // this frame shows the step's art (clip / still)
char        s_shown[ID_LEN + 8] = {0}; // file stem of the visual on screen ("greet_r5", "awaken_3"; "" = fallback)
Anchor      s_anchor;
float       s_anchorLv = 0.0f;         // smoothed voice level for the anchor glow
int32_t     s_bowlFirstT = -1;         // step time the step's bowl icon first showed
Meter       s_meter;
bool        s_meterOn = false;         // this step draws the meter (its visual is in meter.on)
bool        s_meterZoom = false;       // ... and the visual's background zooms (meter.zoom_<id>)
float       s_meterZ0 = 1.0f, s_meterZ1 = 1.0f;
StepFx      s_fx[STEP_FX_MAX];         // this step's fx.<visual> effects
uint8_t     s_fxN = 0;

touch::Gestures s_g;           // this frame's gestures
ReaderEvent s_ev;              // big (~175 B): keep off the stack
TagImage    s_next;            // crystal waiting for its turn
bool        s_hasNext = false;
TagImage    s_cur;             // crystal of the running sequence
bool        s_buttonReq = false, s_tapReq = false, s_serviceReq = false, s_chapterReq = false;
// No-button mode ([station] button = no): taps bless (tapBlesses), with a cool-down after
// every sequence and at most one meter spark per tap_spark_sec from tap blessings.
uint32_t    s_seqEndMs = 0;            // millis() the last sequence / finale ended
bool        s_seqEnded = false;        // ... (false until the first one)
uint32_t    s_tapSparkMs = 0;          // millis() a tap blessing last added a spark
bool        s_tapSparked = false;

uint16_t   *s_freeze = nullptr;    // PSRAM snapshot for HOLD STILL (nullptr -> procedural)
bool        s_freezeOk = false;

uint32_t    s_idleRgb = 0x8B5CF6;
bool        s_musicFile = false, s_musicFullFile = false;
uint32_t    s_musicCheckMs = 0;
uint32_t    s_lastCallMs = 0;
uint32_t    s_attractLoopsSeen = 0;
uint32_t    s_loopsSinceChapter = 0;
uint32_t    s_munchLoop = UINT32_MAX;
uint32_t    s_ledCycleMs = 0;
uint8_t     s_ledIdx = 0;
bool        s_chapterAvail = false;
bool        s_begun = false;

// The meter's fill (the moon* names are the counters' - kept from Moonshards)
float       s_moonFrom = 0.0f, s_moonTo = 0.0f;
uint32_t    s_moonAt = 0;          // millis() the current phase transition started
uint32_t    s_moonPulseAt = 0;     // millis() the last point was added (0 = none yet)
bool        s_moonFinaleDue = false;

// The current group's crystals (circle orbit colours)
GroupMember s_group[GROUP_MAX];
uint8_t     s_groupN = 0;
uint32_t    s_groupLastMs = 0;

VariantMemo s_memo[MEMO_MAX];
uint8_t     s_memoN = 0;

char        s_badge[64];
char        s_capBuf[96];
char        s_titleBuf[96];

constexpr const char *MUSIC_PATH = "/oracle/music/idle.wav";
constexpr const char *MUSIC_FULL_PATH = "/oracle/music/idle_full.wav";

// ================================================================== helpers

inline uint8_t clampRank(uint8_t r) { return r < 1 ? 1 : (r > 7 ? 7 : r); }
inline uint8_t clampTier(uint8_t t) { return t > 3 ? 3 : t; }
// The defined crystal colours (words color.N_hex), at least 1 so modulo / random picks stay safe.
inline uint8_t nColors() {
  const uint8_t n = settings::colorCount();
  return n ? n : 1;
}
inline uint8_t clampColor(uint8_t n) { return (n < 1 || n > nColors()) ? 1 : n; }

uint32_t timingMin(const Timing &t) { return s_seq.rush ? t.rushMin : t.minMs; }
uint32_t timingMax(const Timing &t) { return s_seq.rush ? t.rushMax : t.maxMs; }

uint32_t lerpRgb(uint32_t a, uint32_t b, uint8_t f) {
  const uint32_t inv = 255u - f;
  const uint32_t r = (((a >> 16) & 0xFF) * inv + ((b >> 16) & 0xFF) * f) / 255u;
  const uint32_t g = (((a >> 8) & 0xFF) * inv + ((b >> 8) & 0xFF) * f) / 255u;
  const uint32_t bl = ((a & 0xFF) * inv + (b & 0xFF) * f) / 255u;
  return (r << 16) | (g << 8) | bl;
}

// Slow blend through the defined crystal colours (attract, fallback backgrounds,
// the place-crystal hints). Each colour is itself live: a gradient / rainbow
// crystal keeps moving while it is the one showing.
uint32_t colourCycle(uint32_t tMs, uint32_t periodMs) {
  const uint8_t n = nColors();
  if (n == 1) return settings::crystalRgb(1, tMs);
  const uint32_t seg = periodMs / n ? periodMs / n : 1;
  const uint32_t pos = tMs % (seg * n);
  const uint8_t i = (uint8_t)(pos / seg);
  const uint8_t f = (uint8_t)(((pos % seg) * 255u) / seg);
  return lerpRgb(settings::crystalRgb((uint8_t)(1 + i), tMs), settings::crystalRgb((uint8_t)(1 + (i + 1) % n), tMs), f);
}

// A crystal colour's "resting" shade (its first stop; a rainbow: its colour at t = 0),
// for things that take one fixed colour (the plinth's idle colour).
uint32_t steadyRgb(uint8_t id) {
  uint32_t s;
  return settings::crystalStops(id, &s, 1) ? s : settings::crystalRgb(id, 0);
}

// Once per frame (and at the start of every sequence): the crystal's colour now.
// Everything tinted by the crystal reads s_seq.rgb (and s_step.rgb when the step
// follows the crystal), so one evaluation per frame animates them all.
void refreshCrystalRgb(uint32_t now) {
  Seq &q = s_seq;
  if (!q.active || !q.anim) return;
  q.rgb = settings::crystalRgb(q.colorIdx, now);
  if (s_step.crystalTint) s_step.rgb = q.rgb;
}

// Picks the sequence's crystal colour (id 1..colorCount) and evaluates it now.
void setSeqColour(uint8_t id, uint32_t now) {
  Seq &q = s_seq;
  q.colorIdx = clampColor(id);
  q.anim = settings::crystalAnimated(q.colorIdx);
  q.rgb = settings::crystalRgb(q.colorIdx, now);
}

// The step's colour follows the crystal (live, for animated colours).
void tintCrystal(Step &st) {
  st.rgb = s_seq.rgb;
  st.crystalTint = true;
}

// Every crystal-coloured plinth light goes through here. `mode` is the look a
// steady colour gets. Animated colours (words color.N_hex):
//   IDLE / SOLID (steady)  rainbow -> LED_RAINBOW; 2+ stops -> LED_DUAL(stop 1, stop 2)
//   CHARGING               rainbow -> LED_RAINBOW; 2+ stops -> CHARGING, one comet per stop
//   anything else          the colour of this moment (FLARE / REVEAL / METER / SLEEPY
//                          callers pass s_seq.rgb straight to reader::led instead)
void crystalLed(LedMode mode, uint8_t id) {
  const bool steady = mode == LED_IDLE || mode == LED_SOLID;
  if ((steady || mode == LED_CHARGING) && settings::crystalRainbow(id)) {
    reader::led(LED_RAINBOW, 0);
    return;
  }
  uint32_t stop[2];
  if ((steady || mode == LED_CHARGING) && settings::crystalStops(id, stop, 2) >= 2) {
    reader::led(steady ? LED_DUAL : LED_CHARGING, stop[0], stop[1]);
    return;
  }
  reader::led(mode, settings::crystalRgb(id, millis()));
}

// 0..1 smooth pulse with the given period.
float pulse01(uint32_t t, uint32_t periodMs) {
  return 0.5f + 0.5f * sinf((float)(t % periodMs) * (6.2831853f / (float)periodMs));
}

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float easeOut(float f) { f = clamp01(f); return 1.0f - (1.0f - f) * (1.0f - f); }
inline uint32_t randBelow(uint32_t n) { return n ? (uint32_t)(((uint64_t)esp_random() * n) >> 32) : 0; }

bool sameUid(const uint8_t *a, uint8_t al, const uint8_t *b, uint8_t bl) {
  return al == bl && al > 0 && al <= TAG_UID_MAX && memcmp(a, b, al) == 0;
}

const char *statusName(uint8_t s) {
  switch (s) {
    case WS_OK:          return "ok";
    case WS_NO_TAG:      return "no_tag";
    case WS_WRONG_TAG:   return "wrong_tag";
    case WS_WRITE_FAIL:  return "write_fail";
    case WS_VERIFY_FAIL: return "verify_fail";
    case WS_BAD_REQUEST: return "bad_request";
    default:             return "error";
  }
}

void playSfx(int handle, audio::Chime fallback, uint8_t gain = 90) {
  if (handle >= 0) audio::sfx(handle, gain);
  else audio::chime(fallback);
}

const char *colourName(uint8_t n) { return s_w.colorName[clampColor(n)]; }

// ------------------------------------------------------------------ caption tokens
// {year} {next_year} {rank} {color} -> values. Returns src itself when it has no
// '{' (the common case: no copy), else `buf`.
const char *expandTokens(const char *src, char *buf, size_t n) {
  if (!src || !strchr(src, '{')) return src;
  size_t o = 0;
  const char *p = src;
  while (*p && o + 1 < n) {
    if (*p == '{') {
      const char *close = strchr(p, '}');
      if (close) {
        const size_t kl = (size_t)(close - p - 1);
        char val[40];
        val[0] = 0;
        bool known = true;
        if (kl == 4 && strncasecmp(p + 1, "year", 4) == 0) {
          snprintf(val, sizeof val, "%u", (unsigned)cfg.eventYear);
        } else if (kl == 9 && strncasecmp(p + 1, "next_year", 9) == 0) {
          snprintf(val, sizeof val, "%u", (unsigned)(cfg.eventYear + 1));
        } else if (kl == 4 && strncasecmp(p + 1, "rank", 4) == 0) {
          snprintf(val, sizeof val, "%s", s_w.rank[clampRank(s_seq.active ? s_seq.rankAfter : 1)]);
        } else if ((kl == 5 && strncasecmp(p + 1, "color", 5) == 0) ||
                   (kl == 6 && strncasecmp(p + 1, "colour", 6) == 0)) {
          snprintf(val, sizeof val, "%s", s_seq.active ? colourName(s_seq.colorIdx) : "");
        } else {
          known = false;
        }
        if (known) {
          for (const char *v = val; *v && o + 1 < n; v++) buf[o++] = *v;
          p = close + 1;
          continue;
        }
      }
    }
    buf[o++] = *p++;
  }
  buf[o] = 0;
  return buf;
}

// ------------------------------------------------------------------ voice
uint8_t &memoFor(const char *group) {
  static uint8_t overflow = 0;
  for (uint8_t i = 0; i < s_memoN; i++)
    if (strcmp(s_memo[i].group, group) == 0) return s_memo[i].last;
  if (s_memoN >= MEMO_MAX) { overflow = 0; return overflow; }
  VariantMemo &m = s_memo[s_memoN++];
  strncpy(m.group, group, ID_LEN - 1);
  m.group[ID_LEN - 1] = 0;
  m.last = 0;
  return m.last;
}

void voicePath(char *dst, size_t n, const char *group, uint8_t variant) {
  snprintf(dst, n, "/oracle/voice/%s_%u.wav", group, (unsigned)variant);
}

bool voiceFileExists(const char *group, uint8_t variant) {
  char p[48];
  voicePath(p, sizeof p, group, variant);
  return media::exists(p);   // name cache: no SD access
}

// 1..n, never `last` (unless n == 1); updates `last`.
uint8_t pickAvoiding(uint8_t n, uint8_t &last) {
  if (n == 0) return 0;
  uint8_t v;
  if (n == 1) {
    v = 1;
  } else if (last >= 1 && last <= n) {
    v = (uint8_t)(1 + randBelow(n - 1u));   // 1..n-1, then skip over `last`
    if (v >= last) v++;
  } else {
    v = (uint8_t)(1 + randBelow(n));
  }
  last = v;
  return v;
}

// ---- no-button voice swaps: the word nobutton.voice (DESIGN.md §5.7) ----
// Only with [station] button = no. "from=to;from=to;..." - each side a voice GROUP id
// ("charge") or a LINE id (<group>_<1..9>, "attract_call_2"). Parsed ONCE per words load
// (loadWords); a lookup is a scan of <= VOICE_SUB_MAX short strings, once per line started.
//   group=group  group `to` plays instead of group `from` (an explicit variant - the colour
//                line, a trick gag - is kept)
//   group=line   that one line plays instead of the group
//   line=line    the pick is line `from`: line `to` plays instead
//   line=group   the pick is line `from`: a variant of group `to` plays instead
//   from=        (empty) `from` never plays: a line is left out of its group's picks (another
//                variant plays; none left = nothing), a group plays nothing
// A replacement that isn't on the card counts like an empty one. One pass, no chains.
struct VoiceSub { char from[ID_LEN]; char to[ID_LEN]; bool fromLine; };
constexpr uint8_t VOICE_SUB_MAX = 24;
VoiceSub s_vsub[VOICE_SUB_MAX];
uint8_t  s_vsubN = 0;

// "<group>_<1..9>" -> group + variant. false for a group id ("charge", "greet_r2", "chapter_a").
bool lineId(const char *id, char *group, uint8_t &var) {
  const size_t n = strlen(id);
  if (n < 3 || n >= ID_LEN || id[n - 2] != '_' || id[n - 1] < '1' || id[n - 1] > '9') return false;
  if (group) {
    memcpy(group, id, n - 2);
    group[n - 2] = 0;
  }
  var = (uint8_t)(id[n - 1] - '0');
  return true;
}

const VoiceSub *findSub(const char *id, bool line) {
  for (uint8_t i = 0; i < s_vsubN; i++)
    if (s_vsub[i].fromLine == line && strcasecmp(s_vsub[i].from, id) == 0) return &s_vsub[i];
  return nullptr;
}

// The rule for line <group>_<v> (nullptr = none).
const VoiceSub *lineSub(const char *group, uint8_t v) {
  if (!s_vsubN) return nullptr;
  char id[ID_LEN + 4];
  snprintf(id, sizeof id, "%s_%u", group, (unsigned)v);
  return findSub(id, true);
}

// Can a swap's target play? (a line that exists / a group with at least one line)
bool subPlayable(const VoiceSub &r) {
  if (!r.to[0]) return false;
  char g[ID_LEN];
  uint8_t v;
  if (lineId(r.to, g, v)) return voiceFileExists(g, v);
  return media::countVariants("voice", r.to, "wav") > 0;
}

// Line <group>_<v> never plays: its swap has an empty / missing target.
bool lineBlocked(const char *group, uint8_t v) {
  const VoiceSub *r = lineSub(group, v);
  return r && !subPlayable(*r);
}

// How many variants 1..n the show counts for a voice group: like media::countVariants
// (contiguous from _1), except that a MISSING line named by a line swap still holds its
// slot. The button lines are NICE (never recorded for a no-button station): without this a
// card lacking attract_call_2.wav would stop counting at 1, so attract_call_3.. and its
// stand-in attract_tap_1 could never play. A held slot plays its stand-in (or, with an
// empty / missing target, is skipped by lineBlocked()).
uint8_t variantCount(const char *group) {
  uint8_t n = media::countVariants("voice", group, "wav");
  if (!s_vsubN) return n;
  while (n < 9) {
    const uint8_t v = (uint8_t)(n + 1);
    if (!voiceFileExists(group, v) && !lineSub(group, v)) break;
    n = v;
  }
  return n;
}

// Once per words load (loadWords). Bad entries are logged and skipped.
void loadVoiceSubs() {
  s_vsubN = 0;
  if (cfg.hasButton) return;
  const char *v = settings::text("nobutton.voice", "");
  uint8_t bad = 0;
  const char *p = v;
  while (*p) {
    while (*p == ';' || *p == ' ' || *p == '\t' || *p == '"') p++;
    if (!*p) break;
    const char *s = p;
    while (*p && *p != ';') p++;
    const char *e = p;                          // one "from=to" in [s, e)
    const char *eq = (const char *)memchr(s, '=', (size_t)(e - s));
    VoiceSub r = {};
    bool ok = eq != nullptr && s_vsubN < VOICE_SUB_MAX;
    for (int side = 0; ok && side < 2; side++) {
      const char *a = side ? eq + 1 : s;
      const char *b = side ? e : eq;
      while (a < b && (*a == ' ' || *a == '\t' || *a == '"')) a++;
      while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '"')) b--;
      char *dst = side ? r.to : r.from;
      const size_t len = (size_t)(b - a);
      if (len >= ID_LEN) { ok = false; break; }
      for (size_t i = 0; i < len; i++) {
        const char ch = (a[i] >= 'A' && a[i] <= 'Z') ? (char)(a[i] - 'A' + 'a') : a[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) { ok = false; break; }
        dst[i] = ch;
      }
      dst[len] = 0;
    }
    if (ok && r.from[0]) {
      uint8_t dummy;
      r.fromLine = lineId(r.from, nullptr, dummy);
      s_vsub[s_vsubN++] = r;
    } else {
      bad++;
    }
  }
  if (s_vsubN || bad)
    vlog::event("no-button voice swaps: %u in use%s (words nobutton.voice)", s_vsubN, bad ? ", some not understood" : "");
}

// Does the group have a line that can play (after the swaps)?
bool voiceExists(const char *group) {
  if (!group || !group[0]) return false;
  const char *g = group;
  if (s_vsubN) {
    const VoiceSub *r = findSub(group, false);
    if (r) {
      char tg[ID_LEN];
      uint8_t tv;
      if (!r->to[0]) return false;
      if (lineId(r->to, tg, tv)) return voiceFileExists(tg, tv);
      g = r->to;
    }
  }
  const uint8_t n = variantCount(g);
  for (uint8_t v = 1; v <= n; v++)
    if (!lineBlocked(g, v)) return true;
  return false;
}

// Picks a variant 1..n of a voice group (0 = none can play), never the one used
// last time for that group (unless it's the only one left). `prefer` (if it can
// play) wins: sequenced parts keep the same story variant. Lines a no-button
// swap blocks are left out.
uint8_t pickVariant(const char *group, uint8_t prefer) {
  const uint8_t n = variantCount(group);
  if (n == 0) return 0;
  uint16_t ok = 0;                       // bit v set = variant v may play
  uint8_t nOk = 0;
  for (uint8_t v = 1; v <= n; v++) {
    if (lineBlocked(group, v)) continue;
    ok |= (uint16_t)(1u << v);
    nOk++;
  }
  if (!nOk) return 0;
  uint8_t &last = memoFor(group);
  if (prefer >= 1 && prefer <= n && (ok & (1u << prefer))) {
    last = prefer;
    return prefer;
  }
  // uniformly among the allowed variants, skipping `last` while another one is allowed
  const bool skipLast = nOk > 1 && last >= 1 && last <= n && (ok & (1u << last));
  uint8_t k = (uint8_t)randBelow(skipLast ? nOk - 1u : nOk);
  for (uint8_t v = 1; v <= n; v++) {
    if (!(ok & (1u << v)) || (skipLast && v == last)) continue;
    if (k-- == 0) {
      last = v;
      return v;
    }
  }
  return 0;
}

// One voice line to play for `group`: the group's own pick (explicit variant, or
// random / `prefer`), through the no-button swaps. Writes the group to play from
// into outGroup (ID_LEN) and returns the variant (0 = nothing plays).
uint8_t resolveVoice(const char *group, uint8_t explicitVar, uint8_t prefer, char *outGroup) {
  strlcpy(outGroup, group, ID_LEN);
  char tg[ID_LEN];
  uint8_t tv = 0;
  if (s_vsubN) {
    const VoiceSub *r = findSub(group, false);
    if (r) {
      if (!r->to[0]) return 0;                     // this group never plays
      if (lineId(r->to, tg, tv)) {                 // group -> one line
        if (!voiceFileExists(tg, tv)) return 0;
        strlcpy(outGroup, tg, ID_LEN);
        memoFor(tg) = tv;
        return tv;
      }
      strlcpy(outGroup, r->to, ID_LEN);            // group -> group
    }
  }
  uint8_t v;
  if (explicitVar) {                               // an explicit variant (colour line, trick gag)
    v = (voiceFileExists(outGroup, explicitVar) || lineSub(outGroup, explicitVar)) ? explicitVar : 0;
    if (v) memoFor(outGroup) = v;
  } else {
    v = pickVariant(outGroup, prefer);
  }
  if (!v) return 0;
  const VoiceSub *ls = lineSub(outGroup, v);       // line -> line / group
  if (!ls) return v;
  if (!subPlayable(*ls)) return 0;                 // only an explicit pick gets here (random picks skip it)
  if (lineId(ls->to, tg, tv)) {
    strlcpy(outGroup, tg, ID_LEN);
    memoFor(tg) = tv;
    return tv;
  }
  strlcpy(outGroup, ls->to, ID_LEN);
  return pickVariant(outGroup, 0);
}

// Fire-and-forget voice line (attract calls). Returns its duration (0 = none).
uint32_t sayGroup(const char *group) {
  char g[ID_LEN];
  const uint8_t v = resolveVoice(group, 0, 0, g);
  if (!v) return 0;
  char path[48];
  voicePath(path, sizeof path, g, v);
  return audio::voice(path);
}

// ------------------------------------------------------------------ music
const char *musicPath() {
  if (counters::c.moonFull && s_musicFullFile) return MUSIC_FULL_PATH;
  if (s_musicFile) return MUSIC_PATH;
  return nullptr;
}

void startMusic() {
  const char *p = musicPath();
  if (p) audio::music(p);   // same bed already playing = no restart
}

// ------------------------------------------------------------------ the meter (words meter.*)
float moonTargetPhase() {
  const Counters &k = counters::c;
  if (k.moonFull) return 8.0f;
  const uint32_t goal = cfg.moonGoal ? cfg.moonGoal : 1;
  const uint32_t ph = (uint32_t)k.moonPoints * 8u / goal;
  return ph >= 8 ? 8.0f : (float)ph;
}

float moonPhaseNow(uint32_t now) {
  const int32_t dt = (int32_t)(now - s_moonAt);   // signed: a frame's 'now' may be a hair older
  const float f = easeOut(dt <= 0 ? 0.0f : (float)dt / (float)MOON_EASE_MS);
  return s_moonFrom + (s_moonTo - s_moonFrom) * f;
}

// Starts a smooth transition whenever the target changes (points, finale, host menu).
void updateMoonTarget(uint32_t now) {
  const float target = moonTargetPhase();
  if (target == s_moonTo) return;
  s_moonFrom = moonPhaseNow(now);
  s_moonTo = target;
  s_moonAt = now;
}

void moonSnap() {
  s_moonFrom = s_moonTo = moonTargetPhase();
  s_moonAt = millis();
}

// The finale is due once points reach the goal (and it hasn't played tonight).
bool moonFinalePending() {
  const Counters &k = counters::c;
  const uint32_t goal = cfg.moonGoal ? cfg.moonGoal : 1;
  return !k.moonFull && k.moonPoints >= goal;
}

// +1 meter point (a crystal visit that rolled, or a no-crystal blessing).
void moonAddPoint() {
  Counters &k = counters::c;
  if (k.moonPoints < 0xFFFF) k.moonPoints++;
  s_moonPulseAt = millis();
  if (!s_moonPulseAt) s_moonPulseAt = 1;
  updateMoonTarget(s_moonPulseAt);
  if (moonFinalePending()) s_moonFinaleDue = true;
}

uint8_t moonGlow(uint32_t now) {
  float base = counters::c.moonFull ? 150.0f : 100.0f;
  float p = 0.0f;
  const int32_t sdt = (int32_t)(now - s_moonPulseAt);
  if (s_moonPulseAt && sdt >= 0) {
    const uint32_t dt = (uint32_t)sdt;
    if (dt < MOON_PULSE_UP_MS) p = (float)dt / MOON_PULSE_UP_MS;
    else if (dt < MOON_PULSE_UP_MS + MOON_PULSE_DOWN_MS) p = 1.0f - (float)(dt - MOON_PULSE_UP_MS) / MOON_PULSE_DOWN_MS;
  }
  const float g = base + (255.0f - base) * p;
  return (uint8_t)(g > 255.0f ? 255.0f : g);
}

// Reads up to maxN integers from p (blanks / commas / quotes between them),
// stopping at the end, a ';' or a '#'. Advances p; returns how many were read.
uint8_t readInts(const char *&p, long *out, uint8_t maxN) {
  uint8_t got = 0;
  while (*p && got < maxN) {
    while (*p == ' ' || *p == '\t' || *p == ',' || *p == '"') p++;
    if (!*p || *p == ';' || *p == '#') break;
    char *end = nullptr;
    const long v = strtol(p, &end, 10);
    if (end == p) break;             // not a number: the caller skips the rest
    out[got++] = v;
    p = end;
  }
  return got;
}

inline long clampL(long v, long lo, long hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Is `id` (a visual id) in the list? An entry matches the id itself and its
// variants: "greet" matches greet, greet_r5, greet_2; "attract" matches attract_full.
bool idListed(const char *id, const char *const *list, uint8_t n) {
  if (!id || !*id) return false;
  for (uint8_t i = 0; i < n; i++) {
    const size_t l = strlen(list[i]);
    if (l && strncasecmp(id, list[i], l) == 0 && (id[l] == 0 || id[l] == '_')) return true;
  }
  return false;
}

// Once at begin(): the meter's style, place, colour, name and visual list.
void loadMeter() {
  Meter &m = s_meter;
  m = Meter();
  const char *style = settings::text("meter.style", "moon");
  while (*style == ' ' || *style == '\t') style++;
  m.style = strncasecmp(style, "lantern", 7) == 0 ? MeterStyle::Lantern : MeterStyle::Moon;
  const bool lantern = m.style == MeterStyle::Lantern;
  // Each style's own spot when meter.pos is missing / empty / garbled: a lantern
  // never falls back to the Moon's place in the sky (or vice versa).
  m.x = lantern ? 150 : 650;
  m.y = 110;
  m.r = lantern ? 70 : 46;
  const char *posText = settings::text("meter.pos", "");
  const char *p = posText;
  long v[3];
  const uint8_t got = readInts(p, v, 3);
  if (got >= 2 && v[0] >= 0 && v[0] < display::W && v[1] >= 0 && v[1] < display::H) {
    m.x = (int16_t)v[0];
    m.y = (int16_t)v[1];
    if (got >= 3) m.r = (int16_t)clampL(v[2], 8, 200);
  } else {
    vlog::event("meter.pos \"%s\" not understood - using %d,%d,%d", posText, m.x, m.y, m.r);
  }
  m.rgb = settings::color("meter.color", lantern ? 0xFFB347 : 0xE9EEFF);
  m.name = settings::meterName();
  // The counter under it: below the Moon's disc, or under a painted lantern
  // (whose body is about 0.6 x the glow radius below its centre).
  m.counterBase = (int16_t)clampL(m.y + (lantern ? m.r * 6 / 10 : m.r) + 38, 30, 350);
  m.recallX = m.x > display::W / 2 ? RECALL_EDGE_X : (int16_t)(display::W - RECALL_EDGE_X);
  m.recallY = RECALL_Y;
  // meter.on: split the list in place (blanks / commas / semicolons between ids)
  strlcpy(m.onBuf, settings::text("meter.on", METER_ON_DEFAULT), sizeof m.onBuf);
  char *s = m.onBuf;
  for (;;) {
    while (*s == ' ' || *s == '\t' || *s == ',' || *s == ';' || *s == '"') *s++ = 0;
    if (!*s || m.nOn >= METER_ON_MAX) break;
    m.on[m.nOn++] = s;
    while (*s && *s != ' ' && *s != '\t' && *s != ',' && *s != ';' && *s != '"') s++;
  }
  vlog::event("meter: %s at %d,%d r %d, %u visual id(s)", lantern ? "lantern" : "moon", m.x, m.y, m.r, m.nOn);
}

// The meter's place at step time t: meter.pos, followed through the visual's
// centred background zoom (meter.zoom_<id> = "from,to", linear from the first
// to the last frame like the layers recipe's bg zoom; a looping clip starts
// over) so it stays on the painted spot.
void meterAt(uint32_t t, int16_t &x, int16_t &y, int16_t &r) {
  x = s_meter.x;
  y = s_meter.y;
  r = s_meter.r;
  if (!s_meterZoom || !s_clip.isOpen()) return;
  const uint32_t frames = s_clip.frames();
  const uint32_t dur = s_clip.durationMs();          // frames x the exact frame time
  if (s_step.loop && dur) t %= dur;
  const float span = (frames > 1 && dur) ? (float)dur * (float)(frames - 1) / (float)frames : 0.0f;
  const float f = span > 0.0f ? clamp01((float)t / span) : 1.0f;
  const float z = s_meterZ0 + (s_meterZ1 - s_meterZ0) * f;
  constexpr float MX = display::W / 2, MY = display::H / 2;
  x = (int16_t)lroundf(MX + ((float)x - MX) * z);
  y = (int16_t)lroundf(MY + ((float)y - MY) * z);
  r = (int16_t)lroundf((float)r * z);
}

void drawMeter(uint32_t t, uint32_t now) {
  updateMoonTarget(now);
  int16_t x, y, r;
  meterAt(t, x, y, r);
  if (s_meter.style == MeterStyle::Lantern)
    fx::lanternMeter(x, y, r, moonPhaseNow(now), moonGlow(now), s_meter.rgb, now);
  else
    fx::moonMeter(x, y, r, moonPhaseNow(now), moonGlow(now), s_meter.rgb, now);
}

// ------------------------------------------------------------------ group colours (circle)
void noteGroupColour(uint32_t anonId, uint8_t colorIdx, uint32_t now) {
  const uint32_t gapMs = (uint32_t)cfg.groupGapSec * 1000u;
  if (gapMs == 0 || s_groupN == 0 || now - s_groupLastMs >= gapMs) s_groupN = 0;
  s_groupLastMs = now;
  for (uint8_t i = 0; i < s_groupN; i++)
    if (s_group[i].anonId == anonId) return;
  if (s_groupN >= GROUP_MAX) {   // keep the newest
    memmove(s_group, s_group + 1, sizeof(GroupMember) * (GROUP_MAX - 1));
    s_groupN = GROUP_MAX - 1;
  }
  s_group[s_groupN++] = {anonId, clampColor(colorIdx)};
}

// ------------------------------------------------------------------ write handling
void issueWrite(uint32_t now) {
  Seq &q = s_seq;
  if (!q.nWrites) return;
  q.writeTries++;
  q.writePending = true;
  q.writeOk = false;
  q.writeSentMs = now;
  if (q.simulated) {               // never touch a real tag for a simulated crystal
    q.writeReq = 0;
    q.simDoneAt = now + SIM_WRITE_MS;
    return;
  }
  q.writeReq = reader::write(q.c.uid, q.c.uidLen, q.writes, q.nWrites);
}

// True if a fresh page image of the crystal already holds every page we wanted
// written (a "failed" verify whose data actually landed, seen on re-presentation).
bool imageHasWrites(const TagImage &img) {
  const Seq &q = s_seq;
  if (!img.readOk || !q.nWrites) return false;
  for (uint8_t i = 0; i < q.nWrites; i++) {
    const PageWrite &w = q.writes[i];
    if (w.page < TAG_FIRST_PAGE || w.page > TAG_LAST_PAGE) return false;
    // The CC page is OTP: the reader skips it when byte 0 is already 0xE1, so a
    // formatted CC that differs from ours is not a missing write.
    if (w.page == 3 && img.pages[3 - TAG_FIRST_PAGE][0] == 0xE1) continue;
    if (memcmp(img.pages[w.page - TAG_FIRST_PAGE], w.data, 4) != 0) return false;
  }
  return true;
}

void onWriteDone(const ReaderEvent &ev, uint32_t now) {
  Seq &q = s_seq;
  if (!q.active || q.simulated || !q.writePending || ev.reqId != q.writeReq) {
    Serial.printf("[show] late/unknown WriteDone req=%u status=%s\n", ev.reqId, statusName(ev.status));
    return;
  }
  q.writePending = false;
  q.writeStatus = ev.status;
  q.failedPage = ev.failedPage;
  if (ev.status == WS_OK) {
    q.writeOk = true;
    return;
  }
  vlog::event("write failed: crystal %08lx status=%s page=%u try=%u%s",
              (unsigned long)q.c.anonId, statusName(ev.status), ev.failedPage, q.writeTries,
              q.removed ? " (removed)" : "");
  // Retry once with the same writes while the crystal is still on the plinth.
  if (!q.removed && ev.status != WS_BAD_REQUEST && q.writeTries < 2) issueWrite(now);
}

// The crystal of the running sequence was placed again.
void onRepresented(const TagImage &img, uint32_t now) {
  Seq &q = s_seq;
  q.removed = false;
  if (!q.nWrites || q.writeOk || q.writePending) return;
  if (imageHasWrites(img)) {             // the data is there after all
    q.writeOk = true;
    q.writeStatus = WS_OK;
    vlog::event("write confirmed on re-presentation: crystal %08lx", (unsigned long)q.c.anonId);
    return;
  }
  if (q.writeTries < 3) issueWrite(now); // one more chance now that it's back
}

// Simulated crystals: complete the fake write.
void pumpSequence(uint32_t now) {
  Seq &q = s_seq;
  if (q.active && q.simulated && q.writePending && (int32_t)(now - q.simDoneAt) >= 0) {
    q.writePending = false;
    q.writeOk = true;
    q.writeStatus = WS_OK;
  }
}

// ------------------------------------------------------------------ charge
void addCharge(int16_t x, int16_t y, uint32_t now) {
  Seq &q = s_seq;
  if (q.chargeFill >= 100) {
    fx::sparklesBurst(x, y, 6, q.rgb);
    return;
  }
  q.chargeFill = (uint8_t)min<int>(100, q.chargeFill + CHARGE_STEP_PCT);
  fx::sparklesBurst(x, y, 12, q.rgb);
  if (s_meterOn) {                     // "each press sends a sparkle up to the meter"
    int16_t mx, my, mr;
    meterAt(now - s_stepT0, mx, my, mr);
    fx::sparklesBurst(mx, (int16_t)(my + mr / 2), 5, q.rgb);
  }
  playSfx(s_sfx.tap, audio::CHIME_TAP, 70);
  const uint8_t level = (uint8_t)(q.chargeFill * 255u / 100u);
  if (level != q.chargeLedSent) {
    reader::led(LED_METER, q.rgb, 0, 0, 0, level);
    q.chargeLedSent = level;
  }
  if (q.chargeFill >= 100) {
    q.chargeFullT = (int32_t)(now - s_stepT0);
    playSfx(s_sfx.full, audio::CHIME_RARE);
    fx::sparklesBurst(CX, CY, 40, q.rgb);
  }
}

// ------------------------------------------------------------------ input routing
bool idleMode() { return s_mode == Mode::Attract || s_mode == Mode::Chapter; }

void handleReader(const ReaderEvent &ev, uint32_t now) {
  Seq &q = s_seq;
  switch (ev.type) {
    case ReaderEvt::Placed: {
      const bool same = q.active && sameUid(ev.tag.uid, ev.tag.uidLen, q.c.uid, q.c.uidLen);
      if (same && q.c.kind != token::Kind::Unreadable) {
        onRepresented(ev.tag, now);
        break;
      }
      // Another crystal (or an unreadable one placed again): it's next.
      s_next = ev.tag;
      s_hasNext = true;
      break;
    }
    case ReaderEvt::Removed:
      if (q.active && sameUid(ev.tag.uid, ev.tag.uidLen, q.c.uid, q.c.uidLen)) q.removed = true;
      if (s_hasNext && sameUid(ev.tag.uid, ev.tag.uidLen, s_next.uid, s_next.uidLen)) s_hasNext = false;
      break;
    case ReaderEvt::WriteDone:
      onWriteDone(ev, now);
      break;
    case ReaderEvt::Button:
      if (!ev.pressed) break;
      if (s_mode == Mode::Charge) {
        addCharge(CX, 333, now);
      } else if (idleMode()) {
        if (!reader::tagPresent()) s_buttonReq = true;
        else fx::sparklesBurst(CX, 420, 10, s_idleRgb);   // a crystal is still sitting there
      }
      break;
    case ReaderEvt::LinkUp:
      vlog::event("plinth online (%s)", reader::backendName());
      reader::setIdleLight(s_idleRgb, cfg.ringBrightness);
      if (!q.active) reader::led(LED_IDLE, s_idleRgb);
      break;
    case ReaderEvt::LinkDown:
      vlog::event("plinth offline");
      break;
    default:
      break;
  }
}

// Does an idle tap (a short tap, outside the host's top-left corner) start the
// no-crystal blessing?
//   button = yes  only while the reader is asleep (the screen alone is still fun)
//   button = no   whenever no crystal is on the reader (an asleep reader included),
//                 except during tap_cooldown_sec after a sequence ended - so the
//                 last kid's extra taps don't start a second one (DESIGN.md §5.7)
bool tapBlesses(uint32_t now) {
  if (cfg.hasButton) return !reader::online();
  if (reader::tagPresent()) return false;
  return !s_seqEnded || now - s_seqEndMs >= (uint32_t)cfg.tapCooldownSec * 1000u;
}

void handleTouch(uint32_t now) {
  const bool idle = idleMode();
  if (s_g.tap) {
    const bool inCorner = touch::hit(s_g.tapAt, 0, 0, SERVICE_ZONE, SERVICE_ZONE);
    if (s_mode == Mode::Charge) {
      addCharge(s_g.tapAt.x, s_g.tapAt.y, now);
    } else if (idle && !inCorner && tapBlesses(now)) {
      s_tapReq = true;                               // the blessing (no crystal / no button / asleep reader)
    } else {
      fx::sparklesBurst(s_g.tapAt.x, s_g.tapAt.y, 8, idle ? colourCycle(now, 18000) : s_step.rgb);
    }
  }
  if (idle && s_g.longPress && touch::hit(s_g.longAt, 0, 0, SERVICE_ZONE, SERVICE_ZONE)) s_serviceReq = true;
}

void pollTouch(uint32_t now) {
  if (touch::ok()) s_g = touch::update(LONG_PRESS_MS);
  else s_g = touch::Gestures();
  handleTouch(now);
}

// Waits out the rest of a frame, polling touch every ~20 ms so quick taps
// (charge!) aren't lost between 77 ms clip frames.
void paceFrame(uint32_t frameStart, uint32_t frameMs) {
  for (;;) {
    const uint32_t spent = millis() - frameStart;
    if (spent >= frameMs) {
      delay(1);                      // always yield a tick
      return;
    }
    const uint32_t left = frameMs - spent;
    delay(left < TOUCH_POLL_MS ? left : TOUCH_POLL_MS);
    if (left > TOUCH_POLL_MS && touch::ok()) pollTouch(millis());
  }
}

void pollInputs(uint32_t now) {
  pollTouch(now);
  for (uint8_t i = 0; i < MAX_EVENTS_PER_FRAME && reader::poll(s_ev); i++) handleReader(s_ev, now);
  if (s_step.interruptible) {
    if (s_hasNext || (idleMode() && (s_buttonReq || s_tapReq || s_serviceReq))) s_endStep = true;
  }
}

// ------------------------------------------------------------------ voice sequencing
// Starts the next playable part (skipping missing ones). Returns early (nothing
// started, not done) while the next part's earliest start time is still ahead.
void startNextPart(uint32_t t) {
  Step &st = s_step;
  while (s_run.part < st.nVoice) {
    const uint8_t i = s_run.part;
    if (st.partNotBefore[i] > t) return;   // not yet: advanceVoice() asks again next frame
    // An explicit variant (colour line, trick gag) or a random / coherent pick, through
    // the no-button voice swaps (resolveVoice; none with a plinth button).
    const uint8_t prefer = (!st.voiceVar[i] && st.coherent && i > 0) ? s_run.variant : 0;
    char group[ID_LEN];
    const uint8_t v = resolveVoice(st.voice[i], st.voiceVar[i], prefer, group);
    if (v) {
      voicePath(s_run.path, sizeof s_run.path, group, v);
      const uint32_t d = audio::voice(s_run.path);
      if (d) {
        if (!s_run.variant) s_run.variant = v;
        s_run.speaking = true;
        s_run.anySpoke = true;
        s_run.lastStarted = (int8_t)i;
        s_run.partStart = t;
        s_run.partDur = d;
        if (st.partCap[i]) st.caption = st.partCap[i];
        return;
      }
    }
    s_run.part++;                    // missing / bad: skip to the next part
  }
  s_run.allDone = true;
}

void advanceVoice(uint32_t t) {
  if (s_run.allDone) return;
  if (s_run.speaking) {
    const uint32_t el = t - s_run.partStart;
    const bool finished = (el >= s_run.partDur && !audio::voiceBusy()) || el >= s_run.partDur + VOICE_STUCK_MS;
    if (!finished) return;
    s_run.speaking = false;
    s_run.doneAt = t;
    s_run.part++;
  }
  if (s_run.part >= s_step.nVoice) { s_run.allDone = true; return; }
  if (s_step.maxMs != UINT32_MAX && t + PART_MIN_ROOM_MS > s_step.maxMs) { s_run.allDone = true; return; }
  startNextPart(t);
}

// No voice at all (no card / no audio): the part captions advance by time.
void timedPartCaptions(uint32_t t) {
  Step &st = s_step;
  if (s_run.anySpoke || !s_run.allDone || st.nVoice < 2 || !st.minMs) return;
  uint8_t n = 0;
  for (uint8_t i = 0; i < st.nVoice; i++)
    if (st.partCap[i]) n++;
  if (n < 2) return;
  uint8_t want = (uint8_t)min<uint32_t>(n - 1u, t * n / st.minMs);
  for (uint8_t i = 0; i < st.nVoice; i++) {
    if (!st.partCap[i]) continue;
    if (want == 0) { st.caption = st.partCap[i]; return; }
    want--;
  }
}

bool stepFinished(uint32_t t) {
  const Step &st = s_step;
  if (t >= st.maxMs) return true;
  if (!s_run.allDone) return false;
  const uint32_t voiceEnd = s_run.doneAt ? s_run.doneAt + VOICE_TAIL_MS : 0;
  return t >= max(st.minMs, voiceEnd);
}

// ------------------------------------------------------------------ glow anchor
// "x,y,r" (also "x y r"); r optional (default 40). Off-screen values are refused.
bool parseAnchor(const char *v, Anchor &a) {
  a = Anchor();
  if (!v || !*v) return false;
  long n[3] = {0, 0, 40};
  int got = 0;
  const char *p = v;
  while (*p && got < 3) {
    while (*p == ' ' || *p == '\t' || *p == ',' || *p == ';' || *p == '"') p++;
    if (!*p) break;
    char *end = nullptr;
    const long x = strtol(p, &end, 10);
    if (end == p) break;
    n[got++] = x;
    p = end;
  }
  if (got < 2) return false;
  if (n[0] < -50 || n[0] > display::W + 50 || n[1] < -50 || n[1] > display::H + 50) return false;
  if (n[2] < 6) n[2] = 6;
  if (n[2] > 300) n[2] = 300;
  a.ok = true;
  a.x = (int16_t)n[0];
  a.y = (int16_t)n[1];
  a.r = (int16_t)n[2];
  return true;
}

// Once per step: the file stem of the visual on screen ("" = the fallback).
void setShown() {
  s_shown[0] = 0;
  if (s_vis.kind == media::Kind::None) return;
  const char *slash = strrchr(s_vis.path, '/');
  const char *nm = slash ? slash + 1 : s_vis.path;
  size_t i = 0;
  for (; nm[i] && nm[i] != '.' && i + 1 < sizeof s_shown; i++) s_shown[i] = nm[i];
  s_shown[i] = 0;
}

// Once per step: anchor.<shown id> (e.g. awaken_3), then anchor.<base id> (awaken).
void loadAnchor() {
  s_anchor = Anchor();
  if (!s_shown[0]) return;
  char key[48];
  snprintf(key, sizeof key, "anchor.%s", s_shown);
  if (parseAnchor(settings::text(key, ""), s_anchor)) return;
  if (s_visBase[0] && strcasecmp(s_visBase, s_shown) != 0) {
    snprintf(key, sizeof key, "anchor.%s", s_visBase);
    parseAnchor(settings::text(key, ""), s_anchor);
  }
}

// Once per step: is the meter drawn on this visual (meter.on lists the visual
// on screen - or, with no art, the step's own id), and does the visual's
// background zoom under it (meter.zoom_<shown id>, then meter.zoom_<base id>)?
// HOLD (a frozen snapshot) and the finale (which flares the meter itself) never do.
void loadStepMeter(const Step &st) {
  s_meterOn = false;
  s_meterZoom = false;
  s_meterZ0 = s_meterZ1 = 1.0f;
  if (st.freeze || st.mode == Mode::MoonFull) return;
  s_meterOn = idListed(s_shown[0] ? s_shown : st.vis[0], s_meter.on, s_meter.nOn);
  if (!s_meterOn || s_vis.kind != media::Kind::Clip || !s_shown[0]) return;
  char key[48];
  snprintf(key, sizeof key, "meter.zoom_%s", s_shown);
  const char *z = settings::text(key, "");
  if (!z[0] && s_visBase[0] && strcasecmp(s_visBase, s_shown) != 0) {
    snprintf(key, sizeof key, "meter.zoom_%s", s_visBase);
    z = settings::text(key, "");
  }
  if (!z[0]) return;
  char *end = nullptr;
  const float z0 = strtof(z, &end);
  const char *p = end;
  while (p && (*p == ' ' || *p == ',' || *p == '\t' || *p == ';')) p++;
  const float z1 = p ? strtof(p, &end) : 0.0f;
  if (end == p || !(z0 >= 0.5f && z0 <= 4.0f && z1 >= 0.5f && z1 <= 4.0f)) {
    Serial.printf("[show] %s = \"%s\" not understood (want \"from,to\", 0.5..4)\n", key, z);
    return;
  }
  s_meterZoom = true;
  s_meterZ0 = z0;
  s_meterZ1 = z1;
}

// Parses an fx.* value into s_fx[] (bad effects are skipped with a serial note).
void parseStepFx(const char *v, const char *key) {
  s_fxN = 0;
  const char *p = v;
  while (*p && s_fxN < STEP_FX_MAX) {
    while (*p == ' ' || *p == '\t' || *p == ';' || *p == '"') p++;
    if (!*p) break;
    const char *k = p;
    while (*p && *p != ',' && *p != ';' && *p != ' ' && *p != '\t') p++;
    const size_t kl = (size_t)(p - k);
    FxKind kind = FxKind::None;
    bool known = true;
    if (kl == 4 && strncasecmp(k, "poof", 4) == 0) kind = FxKind::Poof;
    else if (kl == 8 && strncasecmp(k, "sparkles", 8) == 0) kind = FxKind::Sparkles;
    else if (kl == 5 && strncasecmp(k, "burst", 5) == 0) kind = FxKind::Burst;
    else if (!(kl == 4 && strncasecmp(k, "none", 4) == 0)) known = false;
    long n[6];
    const uint8_t got = readInts(p, n, 6);   // x, y, start, end, jx, jy
    bool hasRgb = false;
    uint32_t rgb = 0;
    while (*p == ' ' || *p == '\t' || *p == ',') p++;
    if (*p == '#') {
      char *end = nullptr;
      const unsigned long c = strtoul(p + 1, &end, 16);
      if (end == p + 7) {
        hasRgb = true;
        rgb = (uint32_t)c & 0xFFFFFF;
      }
    }
    while (*p && *p != ';') p++;             // the rest of this effect
    if (!known) {
      Serial.printf("[show] %s: unknown effect \"%.*s\" skipped\n", key, (int)kl, k);
      continue;
    }
    if (kind == FxKind::None) continue;
    if (got < 3) {
      Serial.printf("[show] %s: \"%.*s\" needs at least kind,x,y,startMs - skipped\n", key, (int)kl, k);
      continue;
    }
    StepFx &e = s_fx[s_fxN++];
    e = StepFx();
    e.kind = kind;
    e.x = (int16_t)clampL(n[0], -100, display::W + 100);
    e.y = (int16_t)clampL(n[1], -100, display::H + 100);
    e.startMs = (uint32_t)clampL(n[2], 0, 600000);
    e.endMs = got >= 4 ? (uint32_t)clampL(n[3], (long)e.startMs, 600000) : e.startMs;
    e.jx = (int16_t)(got >= 5 ? clampL(n[4], 0, 400) : 14);
    e.jy = (int16_t)(got >= 6 ? clampL(n[5], 0, 300) : (got >= 5 ? e.jx : 10));
    e.hasRgb = hasRgb;
    e.rgb = rgb;
  }
}

// Once per step: the effects of fx.<shown id>, else fx.<step id>_<variant>
// (a gag's effects even when its clip is missing), else fx.<step id>.
void loadStepFx(const Step &st) {
  s_fxN = 0;
  if (st.freeze) return;
  char key[48];
  const char *v = "";
  if (s_shown[0]) {
    snprintf(key, sizeof key, "fx.%s", s_shown);
    v = settings::text(key, "");
  }
  if (!v[0] && st.vis[0][0]) {
    const uint8_t vv = st.visVariant ? st.visVariant : s_run.variant;
    if (vv) {
      snprintf(key, sizeof key, "fx.%s_%u", st.vis[0], (unsigned)vv);
      v = settings::text(key, "");
    }
    if (!v[0]) {
      snprintf(key, sizeof key, "fx.%s", st.vis[0]);
      v = settings::text(key, "");
    }
  }
  if (v[0]) parseStepFx(v, key);
}

// Every frame: spawn / draw this step's effects (t = step time).
void runStepFx(uint32_t t) {
  const uint32_t crystal = s_seq.active ? s_seq.rgb : s_step.rgb;
  for (uint8_t i = 0; i < s_fxN; i++) {
    StepFx &e = s_fx[i];
    if (t < e.startMs) continue;
    const uint32_t rgb = e.hasRgb ? e.rgb : (e.kind == FxKind::Poof ? 0xFFFFFF : crystal);
    switch (e.kind) {
      case FxKind::Poof: {
        const uint32_t span = e.endMs - e.startMs;
        const uint32_t dur = span ? (span < 200 ? 200 : (span > 1500 ? 1500 : span)) : FX_POOF_MS;
        if (!e.fired) {
          e.fired = true;
          fx::sparklesBurst(e.x, e.y, 16, rgb);
        }
        fx::poof(e.x, e.y, t - e.startMs, rgb, dur);
        break;
      }
      case FxKind::Burst:
        if (!e.fired) {
          e.fired = true;
          fx::sparklesBurst(e.x, e.y, 28, rgb);
        }
        break;
      case FxKind::Sparkles:
        if (!e.fired) {
          e.fired = true;
          e.next = e.startMs;
        }
        while (e.next <= t && (e.next < e.endMs || e.next == e.startMs)) {
          const int16_t x = (int16_t)(e.x + (int32_t)randBelow(2u * e.jx + 1u) - e.jx);
          const int16_t y = (int16_t)(e.y + (int32_t)randBelow(2u * e.jy + 1u) - e.jy);
          fx::sparklesBurst(x, y, 3, rgb);
          e.next += FX_SPARKLE_EVERY_MS;
        }
        break;
      default:
        break;
    }
  }
}

// The anchor is where the anchor layer sits at its FIRST visible moment
// (manifest.schema.md "Anchors"). In the clips where the narrator flies in /
// out (both stories use these timings), the glow is only drawn while that
// layer is actually there (0..1):
//   awaken   the anchor layer (the delighted face) fades in at 3.0 s (+0.25 s);
//            before that the surprised one is still flying in (rush never gets there)
//   goodbye  the narrator floats off over the whole clip: fade out with it
//   keeper   a slow zoom (the anchor drifts ~20 px): kept as is
float lanternVisibility(uint32_t t) {
  if (s_vis.kind != media::Kind::Clip || !s_clip.isOpen()) return 1.0f;   // stills don't move
  if (strcasecmp(s_visBase, cue::AWAKEN) == 0) return clamp01(((float)t - 3000.0f) / 250.0f);
  if (strcasecmp(s_visBase, cue::GOODBYE) == 0) {
    const uint32_t d = s_clip.durationMs();
    return d ? 1.0f - clamp01((float)t / (float)d) : 0.0f;
  }
  return 1.0f;
}

// The narrator's glow (Oona's lantern, Butterscotch's tummy): an additive glow
// at the anchor in the crystal's colour (glow.idle_color when idle), swelling
// gently with the voice (smoothed: no flicker).
void drawAnchorGlow(uint32_t t) {
  if (!s_anchor.ok) return;
  const float target = audio::voiceLevel() / 255.0f;
  s_anchorLv += (target - s_anchorLv) * (target > s_anchorLv ? 0.45f : 0.12f);
  const float vis = lanternVisibility(t);
  if (vis <= 0.0f) return;
  const float k = clamp01(s_anchorLv);
  const uint32_t rgb = s_seq.active ? s_seq.rgb : s_w.glowIdle;
  const float breath = pulse01(t, 3000);
  const float r = (float)s_anchor.r;
  display::addGlow(s_anchor.x, s_anchor.y, (int16_t)(r * (1.0f + 0.30f * k + 0.05f * breath)), rgb,
                   (uint8_t)(vis * (85.0f + 20.0f * breath + 130.0f * k)));
  display::addGlow(s_anchor.x, s_anchor.y, (int16_t)(r * 0.45f), lerpRgb(rgb, 0xFFFFFF, 140),
                   (uint8_t)(vis * (60.0f + 120.0f * k)));
}

// Where light goes "into the narrator" (beam tip, blessing-wisp start ...).
void anchorPoint(int16_t &x, int16_t &y) {
  if (s_anchor.ok) {
    x = s_anchor.x;
    y = s_anchor.y;
  } else {
    x = CX;
    y = CY;
  }
}

// ------------------------------------------------------------------ freeze (HOLD STILL background)
// Copies the frame on screen into the freeze buffer at ~44 % brightness and
// blacks out the old caption band, once per HOLD step (~768 KB, a few ms).
bool snapshotFreeze() {
  if (!s_freeze || !display::ok()) return false;
  const uint16_t *src = display::shownBuffer();
  if (!src) return false;
  const int bandY = 392;
  const size_t keepPx = (size_t)display::W * bandY;
  for (size_t i = 0; i < keepPx; i++) {
    const uint16_t p = src[i];
    const uint16_t r = (uint16_t)((((p >> 11) & 0x1F) * 7u) >> 4);
    const uint16_t g = (uint16_t)((((p >> 5) & 0x3F) * 7u) >> 4);
    const uint16_t b = (uint16_t)(((p & 0x1F) * 7u) >> 4);
    s_freeze[i] = (uint16_t)((r << 11) | (g << 5) | b);
  }
  memset(s_freeze + keepPx, 0, ((size_t)display::W * display::H - keepPx) * sizeof(uint16_t));
  return true;
}

// ------------------------------------------------------------------ drawing
void drawBadges(uint32_t now) {
  s_badge[0] = 0;
  uint32_t colour = 0xF59E0B;
  const bool rushNow = s_seq.active ? s_seq.rush : oracle::rush(now);
  if (rushNow) strlcat(s_badge, s_w.uiRush, sizeof s_badge);
  if (cfg.reader != READER_NONE && !reader::online()) {
    if (s_badge[0]) strlcat(s_badge, " | ", sizeof s_badge);
    strlcat(s_badge, s_w.uiAsleep, sizeof s_badge);
    colour = 0xEF4444;
  }
  if (!media::sdOk()) {
    if (s_badge[0]) strlcat(s_badge, " | ", sizeof s_badge);
    strlcat(s_badge, s_w.uiNoCard, sizeof s_badge);
    colour = 0xEF4444;
  }
  if (s_badge[0]) fx::hostBadge(s_badge, colour);
}

// The step's bowl icon: band-left with art, left-centre on the fallback; a
// small reminder after the reveal. Pops in over 250 ms when it first appears.
bool bowlVisible(uint32_t t) {
  const Step &st = s_step;
  if (st.bowlTier < 0) return false;
  if (t >= st.bowlAt) return true;
  return st.bowlPart != NO_PART && s_run.lastStarted >= (int8_t)st.bowlPart;
}

void drawStepBowl(uint32_t t) {
  const Step &st = s_step;
  if (!bowlVisible(t)) return;
  if (s_bowlFirstT < 0) {
    s_bowlFirstT = (int32_t)t;
    if (!st.bowlMini) fx::sparklesBurst(s_art ? BAND_BOWL_X : FB_BOWL_X, s_art ? BAND_BOWL_Y : FB_BOWL_Y, 14,
                                        s_w.bowlRgb[st.bowlTier]);
  }
  const int8_t b = st.bowlTier > 3 ? 3 : st.bowlTier;
  const float pop = st.bowlMini ? 1.0f : 0.6f + 0.4f * easeOut((float)(t - (uint32_t)s_bowlFirstT) / 250.0f);
  if (st.bowlMini) {
    fx::bowlIcon(BAND_BOWL_X - 20, BAND_BOWL_Y + 2, MINI_BOWL_SIZE, s_w.bowlShape[b], s_w.bowlRgb[b], t);
  } else if (s_art) {
    fx::bowlIcon(BAND_BOWL_X, BAND_BOWL_Y, (int16_t)(BAND_BOWL_SIZE * pop), s_w.bowlShape[b], s_w.bowlRgb[b], t);
  } else {
    fx::bowlIcon(FB_BOWL_X, FB_BOWL_Y, (int16_t)(FB_BOWL_SIZE * pop), s_w.bowlShape[b], s_w.bowlRgb[b], t);
  }
}

void drawFrame(uint32_t t, uint32_t dt, uint32_t now) {
  if (!display::ok()) return;
  uint16_t *fb = display::beginFrame();
  if (!fb) return;
  const Step &st = s_step;
  const uint32_t rgb = st.rainbow ? colourCycle(t, 6000) : st.rgb;
  // The wide aura is cached per colour: on colour-cycling steps (and for an
  // animated crystal colour) it steps every 250 ms - a few cache misses a
  // second instead of one every frame.
  uint32_t auraRgb = st.rgb;
  if (st.rainbow) auraRgb = colourCycle(t - t % AURA_STEP_MS, 6000);
  else if (st.crystalTint && s_seq.active && s_seq.anim)
    auraRgb = settings::crystalRgb(s_seq.colorIdx, now - now % AURA_STEP_MS);

  bool drew = false;
  if (st.freeze && s_freezeOk) {
    display::copyFrom(s_freeze);
    drew = true;
  } else if (s_vis.kind == media::Kind::Clip && s_clip.isOpen() && s_clipFails < 3) {
    if (s_clip.render(t, st.loop, fb)) drew = true;
    else if (++s_clipFails >= 3) vlog::event("clip %s failed, using the fallback", s_vis.path);
  } else if (s_vis.kind == media::Kind::Still && !s_stillFailed) {
    if (media::drawStill(s_vis.path, fb)) drew = true;
    else { s_stillFailed = true; vlog::event("still %s failed, using the fallback", s_vis.path); }
  }
  s_art = drew && !st.freeze;

  const char *cap = expandTokens(st.caption, s_capBuf, sizeof s_capBuf);
  const char *title = expandTokens(st.fbTitle, s_titleBuf, sizeof s_titleBuf);
  bool captionInTitle = false;
  if (!drew) {
    if (title && title[0]) {
      fx::fallbackCue(title, t, rgb, st.energy, auraRgb);
      captionInTitle = (cap && strcmp(cap, title) == 0);
    } else {
      fx::orbBackground(t, rgb, st.energy, auraRgb);
    }
  }

  if (s_art) drawAnchorGlow(t);
  if (s_meterOn) drawMeter(t, now);
  fx::sparklesAmbient(rgb, st.ambient);
  if (st.overlay) st.overlay(t);
  if (s_fxN) runStepFx(t);
  fx::sparklesStep(dt);
  bool showCap = cap && cap[0] && !captionInTitle;
  if (showCap && !s_art && st.titleDup && strcasecmp(cap, st.titleDup) == 0) showCap = false;
  if (showCap) fx::caption(cap);
  if (st.band) st.band(t);
  drawStepBowl(t);
  drawBadges(now);
  display::endFrame();
}

// ------------------------------------------------------------------ the step runner
Step &newStep(Mode m) {
  s_step = Step();
  s_step.mode = m;
  return s_step;
}

void setId(char *dst, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void setId(char *dst, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(dst, ID_LEN, fmt, ap);
  va_end(ap);
}

// Adds a voice part: explicit variant (0 = random/coherent), caption while it
// plays, earliest start time. Returns its index (NO_PART if the list is full).
uint8_t addVoice(Step &st, const char *group, uint8_t variant = 0, const char *cap = nullptr, uint32_t notBefore = 0) {
  if (st.nVoice >= MAX_PARTS || !group || !group[0]) return NO_PART;
  const uint8_t i = st.nVoice++;
  strncpy(st.voice[i], group, ID_LEN - 1);
  st.voice[i][ID_LEN - 1] = 0;
  st.voiceVar[i] = variant;
  st.partCap[i] = cap;
  st.partNotBefore[i] = notBefore;
  return i;
}

// The bowl line as the step's next part: the icon appears when it starts (or,
// if there's no bowl line on the card, at `fallbackAt`; at worst in the last 1.2 s).
void addBowlPart(Step &st, uint8_t tier, uint32_t notBefore, uint32_t fallbackAt) {
  tier = clampTier(tier);
  s_seq.bowlTier = (int8_t)tier;
  st.bowlTier = (int8_t)tier;
  char g[ID_LEN];
  snprintf(g, sizeof g, "bowl_%s", cue::BOWL_KEY[tier]);
  if (voiceExists(g)) {
    st.bowlPart = addVoice(st, g, 0, s_w.bowlName[tier], notBefore);
    st.bowlAt = st.maxMs > 1200 ? st.maxMs - 1200 : 0;
  } else {
    st.bowlAt = fallbackAt;
  }
}

void setTiming(Step &st, const Timing &tm) {
  st.minMs = timingMin(tm);
  st.maxMs = max(st.minMs, timingMax(tm));
}

// Runs s_step until it finishes. Returns false if an event interrupted it.
bool runStep() {
  Step &st = s_step;
  s_mode = st.mode;
  s_endStep = false;
  s_run = Run();
  s_clipFails = 0;
  s_stillFailed = false;
  s_bowlFirstT = -1;
  s_seq.fxNext = 0;
  s_seq.fxDone = false;
  s_stepT0 = millis();

  // Voice first: the variant it picks chooses the visual variant (unless explicit).
  startNextPart(0);

  s_vis = media::Visual();
  s_visBase[0] = 0;
  const uint8_t vv = st.visVariant ? st.visVariant : s_run.variant;
  for (int i = 0; i < MAX_VIS; i++) {
    if (!st.vis[i][0]) continue;
    const media::Visual v = media::findVisual(st.vis[i], vv);
    if (v.kind != media::Kind::None) {
      s_vis = v;
      strlcpy(s_visBase, st.vis[i], sizeof s_visBase);
      break;
    }
  }
  if (s_vis.kind == media::Kind::Clip) {
    if (!s_clip.open(s_vis.path)) {
      vlog::event("clip %s won't open, using the fallback", s_vis.path);
      s_vis.kind = media::Kind::None;
    } else if (st.minFromClip) {
      st.minMs = max(st.minMs, s_clip.durationMs());
      if (st.maxMs < st.minMs) st.maxMs = st.minMs;
    }
  }
  if (s_vis.kind == media::Kind::None) s_visBase[0] = 0;
  setShown();
  loadAnchor();
  loadStepMeter(st);
  loadStepFx(st);

  if (st.sfx >= 0) audio::sfx(st.sfx, 90);
  else if (st.chime >= 0) audio::chime((audio::Chime)st.chime);

  if (st.freeze) s_freezeOk = snapshotFreeze();

  bool completed = true;
  uint32_t lastFrame = s_stepT0;
  for (;;) {
    const uint32_t frameStart = millis();
    const uint32_t t = frameStart - s_stepT0;
    refreshCrystalRgb(frameStart);     // an animated crystal colour: once per frame, for everything
    pollInputs(frameStart);
    pumpSequence(frameStart);
    if (s_endStep) { completed = false; break; }
    advanceVoice(t);
    timedPartCaptions(t);
    if (st.tick && st.tick(t)) break;
    if (stepFinished(t)) break;

    drawFrame(t, frameStart - lastFrame, frameStart);
    lastFrame = frameStart;

    const bool clipPacing = s_vis.kind == media::Kind::Clip && s_clip.isOpen() && s_clipFails < 3 && !st.freeze;
    paceFrame(frameStart, clipPacing ? s_clip.frameMs() : FRAME_MS_STILL);
  }

  if (s_clip.isOpen()) s_clip.close();
  if (!completed) {
    audio::voiceStop();               // a new crystal / request: start it crisply
  } else if (s_run.speaking && audio::voiceBusy()) {
    Serial.printf("[show] voice %s cut by the %lu ms cap (%lu ms left)\n", s_run.path,
                  (unsigned long)st.maxMs, (unsigned long)audio::voiceRemainingMs());
  }
  return completed;
}

// ================================================================== overlays + ticks

// ---- GLOW number / smudge cloud (reveal + replay), art (band) or fallback (centre) layout
void drawGlowNumber(int shown, uint8_t tier, uint32_t rgb, uint32_t t, uint8_t smudgeLevel, const char *label) {
  const int16_t nx = s_art ? NUM_ART_X : CX;
  const int16_t base = s_art ? NUM_ART_BASE : Y_NUMBER;
  const int16_t mid = s_art ? NUM_ART_MID : NUMBER_MID;
  if (label && label[0]) fx::subtitle(label, nx, s_art ? NUM_ART_LABEL : Y_POWER, 0xFFFFFF);
  if (tier == 0) {   // a trick: no public low score - a puff in the trick colour hides the number
    if (smudgeLevel) fx::smudgeCloud(nx, mid, s_art ? 66 : 100, t, smudgeLevel, s_w.tierRgb[0]);
  } else {
    fx::bigNumber(shown, nx, base, rgb);
  }
}

// ---- attract
void attractOverlay(uint32_t t) {
  if (cfg.showCounter) {
    if (s_meterOn) fx::counterBadgeAt(s_meter.x, s_meter.counterBase, counters::c.stationCrystals, s_w.uiCounter);
    else fx::counterBadge(counters::c.stationCrystals, s_w.uiCounter);
  }
  // Host feedback while holding the service corner.
  if (s_g.down && touch::hit(s_g.at, 0, 0, SERVICE_ZONE, SERVICE_ZONE) && s_g.heldMs > 500) {
    const uint32_t lvl = min<uint32_t>(255, s_g.heldMs * 255u / LONG_PRESS_MS);
    fx::ring(50, 50, 34, 6, 0xFFFFFF, (uint8_t)lvl);
  }
  (void)t;
}

// Two place-crystal hints flanking the caption (after the caption band, so they stay bright).
void hintsBand(uint32_t t) {
  const bool tapMode = !reader::online() && touch::ok();
  if (tapMode && s_mode == Mode::Attract) return;
  const uint32_t rgb = colourCycle(t, 18000);
  fx::placeHintAt(HINT_L, t, rgb);
  fx::placeHintAt(HINT_R, t + 800, rgb);
}

bool attractTick(uint32_t t) {
  const uint32_t now = s_stepT0 + t;
  // Captions: "touch me" without a reader; else "Crystal here" <-> ui.or_press, when there is
  // another way in: the plinth's button (button = yes), or a tap (button = no: the words
  // nobutton.ui.or_press turn "OR PRESS" into a tap prompt).
  const bool orPress = cfg.hasButton ? cfg.reader == READER_PLINTH : touch::ok();
  if (!reader::online() && touch::ok()) {
    s_step.caption = s_w.uiTouch;
  } else if (orPress && s_w.uiOrPress[0]) {
    s_step.caption = ((t / CAPTION_SWAP_MS) & 1) ? s_w.uiOrPress : s_w.capAttract;
  } else {
    s_step.caption = s_w.capAttract;
  }

  // The plinth breathes through the defined crystal colours (the reader drops
  // repeats): a gradient crystal's turn = LED_DUAL, a rainbow one's = LED_RAINBOW.
  if (now - s_ledCycleMs >= LED_CYCLE_MS) {
    s_ledCycleMs = now;
    s_ledIdx = (uint8_t)((s_ledIdx + 1) % nColors());
    crystalLed(LED_IDLE, (uint8_t)(1 + s_ledIdx));
  }

  // The idle music bed (re-started if it ever stops)
  if (now - s_musicCheckMs >= MUSIC_RETRY_MS) {
    s_musicCheckMs = now;
    if (!audio::musicPlaying()) startMusic();
  }

  // A teaser line every attractCallSec
  if (cfg.attractCallSec && now - s_lastCallMs >= (uint32_t)cfg.attractCallSec * 1000u) {
    s_lastCallMs = now;
    if (!audio::voiceBusy()) sayGroup(cue::ATTRACT_CALL);
  }

  // Count attract loops; the sidekick's "munch" sfx ~5.4 s into each loop of the attract clip.
  uint32_t loopLen = ATTRACT_LOOP_MS;
  const bool clip = s_vis.kind == media::Kind::Clip && s_clip.isOpen() && s_clipFails < 3;
  if (clip) loopLen = max<uint32_t>(s_clip.durationMs(), 4000);
  const uint32_t loops = t / loopLen;
  if (loops > s_attractLoopsSeen) {
    s_loopsSinceChapter += loops - s_attractLoopsSeen;
    s_attractLoopsSeen = loops;
  }
  if (clip && s_sfx.munch >= 0 && strcasecmp(s_visBase, cue::ATTRACT) == 0 && loopLen > MUNCH_AT_MS + 300 &&
      t % loopLen >= MUNCH_AT_MS && loops != s_munchLoop) {
    s_munchLoop = loops;
    audio::sfx(s_sfx.munch, 80);
  }

  if (s_moonFinaleDue) return true;   // e.g. set from the host menu: the loop runs it
  if (cfg.chapterEvery && s_chapterAvail && s_loopsSinceChapter >= cfg.chapterEvery && !audio::voiceBusy()) {
    s_chapterReq = true;
    return true;
  }
  return false;
}

// ---- absorb: a column of the crystal's light rises into the glow anchor
void absorbOverlay(uint32_t t) {
  const Seq &q = s_seq;
  int16_t tx, ty;
  anchorPoint(tx, ty);
  const uint32_t rise = q.rush ? 360 : 600;
  const float f = easeOut((float)t / (float)rise);
  const int16_t tip = (int16_t)(display::H + 10 + (ty - display::H - 10) * f);
  const uint8_t lvl = (uint8_t)(t < rise ? 230 : max<int32_t>(120, 230 - (int32_t)(t - rise) / 4));
  fx::beam(CX, display::H + 10, tx, tip, 44, q.rgb, lvl, t);
  if (t >= rise) {
    fx::burstRing(tx, ty, q.rgb, t - rise, 700);
    display::addGlow(tx, ty, 70, q.rgb, 150);
  }
}

// ---- awaken: two facets pop on (with a chime each); rank name with the second
void awakenOverlay(uint32_t t) {
  Seq &q = s_seq;
  const uint8_t want = (t >= q.gemAt2) ? 2 : (t >= q.gemAt ? 1 : 0);
  while (q.gemsLit < want) {
    q.gemsLit++;
    fx::sparklesBurst((int16_t)(CX + (q.gemsLit == 1 ? -182 : -130)), Y_METER, 22, q.rgb);
    audio::chime(audio::CHIME_TAP, 75);
  }
  fx::crystalMeter(CX, Y_METER, q.gemsLit, 8, q.rgb, t, q.gemsLit > 0);
  if (!s_art && q.gemsLit >= 2) {   // fallback layout: the rank name as a title (art: the caption says it)
    fx::burstRing(CX, Y_TITLE - 14, q.rgb, t - q.gemAt2, 700);
    fx::title(s_w.rank[clampRank(q.rankAfter)], CX, Y_TITLE, q.rgb);
  }
}

// ---- greet: rank name + meter; the new facet pops on (and a rank-up flips the name)
void greetOverlay(uint32_t t) {
  Seq &q = s_seq;
  const bool rankUp = q.rankAfter != q.rankBefore;
  if (!q.gemLit && t >= q.gemAt) {
    q.gemLit = true;
    fx::sparklesBurst(CX, Y_METER, rankUp ? 36 : 16, q.rgb);
    if (rankUp) playSfx(s_sfx.whoosh, audio::CHIME_RARE, 70);
    else audio::chime(audio::CHIME_TAP, 70);
  }
  if (!s_art) {
    const uint8_t rank = clampRank(q.gemLit ? q.rankAfter : q.rankBefore);
    fx::title(s_w.rank[rank], CX, Y_TITLE, q.rgb);
    if (q.gemLit && rankUp) fx::burstRing(CX, Y_TITLE - 14, q.rgb, t - q.gemAt, 800);
  }
  fx::crystalMeter(CX, Y_METER, q.gemLit ? q.meterTo : q.meterFrom, 8, q.rgb, t, q.gemLit);
}

// ---- recall: last year's result as a faded icon floating up in the top
// corner away from the meter (Moon top-right -> left; lantern top-left -> right)
void recallOverlay(uint32_t t) {
  const Seq &q = s_seq;
  const uint8_t lt = clampTier((uint8_t)q.c.lastTier);
  const float f = easeOut((float)t / 900.0f);
  const int16_t rx = s_meter.recallX;
  const int16_t y = (int16_t)(s_meter.recallY + 40 - 40 * f);
  fx::bowlIcon(rx, y, 96, s_w.bowlShape[lt], s_w.bowlRgb[lt], t, (uint8_t)(40 + 110 * f));
  fx::subtitle(s_w.tier[lt], (int16_t)(rx + 4), (int16_t)(y + 88), display::scaleRgb(s_w.tierRgb[lt], (uint8_t)(60 + 140 * f)));
  if (!s_art) {
    fx::subtitle(q.recallLine, CX, Y_TITLE - 30, 0xFFFFFF);
    fx::title(s_w.tier[lt], CX, Y_TITLE + 30, s_w.tierRgb[lt]);
  }
}

// ---- charge window
void chargeOverlay(uint32_t t) {
  const Seq &q = s_seq;
  const uint8_t f = q.chargeFill;
  if (!s_art) {   // fallback: a big ring that grows with the fill
    display::addGlow(CX, CY, (int16_t)(70 + f * 13 / 10), q.rgb, (uint8_t)(60 + f * 3 / 2));
    fx::ring(CX, CY, (int16_t)(56 + f / 2), 8, q.rgb, (uint8_t)(80 + f * 175 / 100));
    if (q.chargeFullT >= 0) fx::burstRing(CX, CY, 0xFFFFFF, t - (uint32_t)q.chargeFullT, 700);
  } else if (q.chargeFullT >= 0 && s_meterOn) {   // the full charge bursts at the meter
    int16_t mx, my, mr;
    meterAt(t, mx, my, mr);
    fx::burstRing(mx, my, q.rgb, t - (uint32_t)q.chargeFullT, 700);
  }
}

// The fill bar: in the band with art (above the caption), mid-screen on the fallback.
void chargeBand(uint32_t t) {
  (void)t;
  const Seq &q = s_seq;
  const uint8_t f = q.chargeFill;
  Arduino_GFX &g = display::gfx();
  const int16_t bx = 200, bw = 400;
  const int16_t by = s_art ? 372 : 318, bh = s_art ? 22 : 30;
  g.drawRoundRect(bx - 4, by - 4, bw + 8, bh + 8, bh / 2 + 4, display::rgb565(0xFFFFFF));
  g.drawRoundRect(bx - 3, by - 3, bw + 6, bh + 6, bh / 2 + 3, display::rgb565(0xFFFFFF));
  const int16_t w = (int16_t)(bw * f / 100);
  if (w > 0) g.fillRoundRect(bx, by, max<int16_t>(w, bh), bh, bh / 2, display::rgb565(q.rgb));
}

bool chargeTick(uint32_t t) {
  const Seq &q = s_seq;
  // Caption: alternate "PRESS!" and "Press the button!" when both ways work. No button: both
  // words are tap prompts (nobutton.caption.charge / nobutton.ui.press) and alternate while
  // touch works.
  const bool canTap = touch::ok();
  const bool canPress = cfg.hasButton ? (cfg.reader == READER_PLINTH && reader::online()) : canTap;
  if (canTap && canPress) s_step.caption = ((t / 1300) & 1) ? s_w.uiPress : s_w.capCharge;
  else s_step.caption = canTap ? s_w.capCharge : s_w.uiPress;
  // Full: let the burst play, then move on early.
  return q.chargeFullT >= 0 && t >= (uint32_t)q.chargeFullT + 700;
}

// ---- hold still (waiting for the tag write)
void holdOverlay(uint32_t t) {
  const float p = pulse01(t, 1100);
  fx::glowPulse(CX, CY, 120, s_seq.rgb, t, 1100);
  fx::ring(CX, CY, (int16_t)(96 + 10 * p), 10, s_seq.rgb, (uint8_t)(140 + 110 * p));
}

bool holdTick(uint32_t t) {
  (void)t;
  const Seq &q = s_seq;
  if (!q.writePending || q.removed) return true;
  return millis() - q.writeSentMs >= cfg.writeTimeoutMs;
}

// ---- reveal: the number rolls up and locks with a burst (tier 0: the smudge
// puffs in instead); tier name = title on the fallback / caption with art.
void revealCommon(uint32_t t) {
  Seq &q = s_seq;
  const uint8_t tier = clampTier(q.o.tier);
  const uint32_t trgb = s_w.tierRgb[tier];
  if (!q.locked && t >= q.lockAt) {
    q.locked = true;
    static const audio::Chime CH[4] = {audio::CHIME_TRICK, audio::CHIME_REVEAL, audio::CHIME_RARE, audio::CHIME_LEGEND};
    int h = s_sfx.reveal[tier];
    if (tier == 0 && q.gag >= 1 && q.gag <= MAX_GAGS && s_sfx.gag[q.gag] >= 0) h = s_sfx.gag[q.gag];
    playSfx(h, CH[tier]);
    reader::led(LED_REVEAL, trgb, q.rgb);   // settles into the crystal's colour (an animated one: its shade now)
    fx::sparklesBurst(s_art ? NUM_ART_X : CX, s_art ? NUM_ART_MID : NUMBER_MID, (uint8_t)(18 + tier * 14), trgb);
  }
  // (a trick gag's device effects come from the words fx.reveal_t0_<gag>: runStepFx)
}

void revealOverlay(uint32_t t) {
  revealCommon(t);
  if (s_art) return;   // the band overlay draws the number
  Seq &q = s_seq;
  const uint8_t tier = clampTier(q.o.tier);
  const uint32_t trgb = s_w.tierRgb[tier];
  int shown = q.o.number;
  if (!q.locked && q.lockAt) {
    const float e = easeOut((float)t / (float)q.lockAt);
    shown = max(1, (int)(q.o.number * e));
  }
  const uint8_t smudge = (uint8_t)(255.0f * easeOut((float)t / 300.0f));
  drawGlowNumber(shown, tier, q.locked ? trgb : 0xFFFFFF, t, smudge, s_w.uiPower);
  if (q.locked) {
    fx::burstRing(CX, NUMBER_MID, trgb, t - q.lockAt, 800);
    fx::title(s_w.tier[tier], CX, Y_TIER, trgb);
  }
}

void revealBand(uint32_t t) {
  if (!s_art) return;
  Seq &q = s_seq;
  const uint8_t tier = clampTier(q.o.tier);
  const uint32_t trgb = s_w.tierRgb[tier];
  int shown = q.o.number;
  if (!q.locked && q.lockAt) {
    const float e = easeOut((float)t / (float)q.lockAt);
    shown = max(1, (int)(q.o.number * e));
  }
  const uint8_t smudge = (uint8_t)(255.0f * easeOut((float)t / 300.0f));
  drawGlowNumber(shown, tier, q.locked ? trgb : 0xFFFFFF, t, smudge, s_w.uiPower);
  if (q.locked) fx::burstRing(NUM_ART_X, NUM_ART_MID, trgb, t - q.lockAt, 700);
}

// ---- replay: tonight's stored result, dimmed, with a check mark and TONIGHT
void replayDraw(uint32_t t) {
  const Seq &q = s_seq;
  const uint8_t tier = clampTier(q.o.tier);
  const uint32_t dim = display::scaleRgb(s_w.tierRgb[tier], 150);
  drawGlowNumber(q.o.number, tier, dim, t, 150, s_w.uiTonight);
  if (s_art) {
    fx::checkMark(NUM_ART_X - 105, NUM_ART_LABEL - 6, 34, 0x7CF29C);   // left of TONIGHT, above the caption
    fx::subtitle(s_w.tier[tier], 150, 372, dim);   // top-left of the band (a bowl icon may sit below)
  } else {
    fx::checkMark(CX + 170, NUMBER_MID, 56, 0x7CF29C);
    fx::title(s_w.tier[tier], CX, Y_TIER, dim);
  }
}
void replayOverlay(uint32_t t) { if (!s_art) replayDraw(t); }
void replayBand(uint32_t t) { if (s_art) replayDraw(t); }

// ---- no crystal: a blessing wisp (meter colour) drifts from the narrator to the
// kid (drawn here only on the fallback: with art, the clip shows the blessing)
void nocrystalOverlay(uint32_t t) {
  if (s_art) return;
  Seq &q = s_seq;
  int16_t x0, y0;
  anchorPoint(x0, y0);
  const float f = easeOut((float)t / 2200.0f);
  const int16_t x = (int16_t)(x0 + (CX - x0) * f + 60.0f * sinf((float)t / 380.0f) * (1.0f - f));
  const int16_t y = (int16_t)(y0 + (360 - y0) * f);
  display::addGlow(x, y, 46, s_meter.rgb, 200);
  display::addGlow(x, y, 16, 0xFFFFFF, 220);
  if (t < 2400) {
    if (!q.fxNext) q.fxNext = 1;
    while (q.fxNext <= t) {
      fx::sparklesBurst(x, y, 2, s_meter.rgb);
      q.fxNext += 90;
    }
  }
}

// ---- extras
void keeperOverlay(uint32_t t) {
  if (!s_art) fx::title(s_w.rank[7], CX, Y_TITLE, s_w.tierRgb[3]);
  fx::crystalMeter(CX, Y_METER, 8, 8, s_seq.rgb, t, false);
}

void milestoneOverlay(uint32_t t) {
  Seq &q = s_seq;
  const uint32_t now = s_stepT0 + t;
  // confetti bursts in the crystal colours (a few per second)
  if (!q.fxNext) q.fxNext = 1;
  while (q.fxNext <= t) {
    const uint8_t c = (uint8_t)(1 + randBelow(nColors()));
    fx::sparklesBurst((int16_t)(80 + randBelow(640)), (int16_t)(50 + randBelow(260)), 9, settings::crystalRgb(c, now));
    q.fxNext += 260;
  }
  fx::bigNumber((int)counters::c.stationCrystals, CX, Y_NUMBER, s_w.tierRgb[3]);
  fx::subtitle(s_w.uiCounter, CX, Y_TIER, 0xFFFFFF);
}

void circleOverlay(uint32_t t) {
  // small faceted crystals in the group's colours orbiting the centre (each one
  // live: a gradient / rainbow crystal keeps shifting as it orbits)
  const uint32_t now = s_stepT0 + t;
  uint8_t n = s_groupN;
  if (n < 3) n = 3;
  const float a0 = (float)t * (6.2831853f / 4200.0f);
  for (uint8_t i = 0; i < n; i++) {
    const float a = a0 + (float)i * (6.2831853f / (float)n);
    const int16_t x = (int16_t)(CX + 190.0f * cosf(a));
    const int16_t y = (int16_t)(CY + 84.0f * sinf(a));
    const uint8_t id = i < s_groupN ? clampColor(s_group[i].colorIdx) : (uint8_t)(1 + (i * 2) % nColors());
    const uint32_t rgb = settings::crystalRgb(id, now);
    const float depth = 0.75f + 0.25f * sinf(a);   // nearer = bigger
    display::addGlow(x, y, (int16_t)(40 * depth), rgb, 140);
    fx::gem(x, y, (int16_t)(26 * depth), (int16_t)(40 * depth), rgb, true);
  }
}

// ---- the finale: the meter flares full at meter.pos (the Moon swells; a
// lantern blazes with a big warm flare that settles bright). <= 1 flash a second.
void moonfullOverlay(uint32_t t) {
  const float f = easeOut((float)t / 1500.0f);
  const uint32_t now = s_stepT0 + t;
  const int16_t mx = s_meter.x, my = s_meter.y;
  const uint32_t rgb = s_meter.rgb;
  const uint8_t glow = (uint8_t)(t < 2500 ? 255 : 200);
  if (s_meter.style == MeterStyle::Lantern) {
    // swells in 0.4 s, then eases down to a steady bright flood by ~3 s
    const float flare = t < 400 ? (float)t / 400.0f : max(0.35f, 1.0f - (float)(t - 400) / 2600.0f);
    display::addGlow(mx, my, (int16_t)(s_meter.r * (1.4f + 1.6f * f)), rgb, (uint8_t)(60.0f + 150.0f * flare));
    fx::lanternMeter(mx, my, (int16_t)(s_meter.r * (1.0f + 0.30f * f)), moonPhaseNow(now), glow, rgb, now);
  } else {
    fx::moonMeter(mx, my, (int16_t)(s_meter.r * (1.0f + 0.22f * f)), moonPhaseNow(now), glow, rgb, now);
  }
  fx::burstRing(mx, my, rgb, t, 900);
  if (t >= 1500) fx::burstRing(mx, my, 0xFFFFFF, t - 1500, 900);
  Seq &q = s_seq;
  if (!q.fxDone && t >= 200) {
    q.fxDone = true;
    fx::sparklesBurst(mx, my, 40, rgb);
  }
}

// ================================================================== flows

void beginSequence(uint32_t now) {
  s_seq = Seq();
  s_seq.active = true;
  oracle::sequenceStarted(now);
  s_seq.rush = oracle::rush(now);
  s_buttonReq = s_tapReq = false;
}

void endSequence() {
  Seq &q = s_seq;
  const uint32_t now = millis();
  const char *wr = "-";
  if (q.noWrites) wr = "nowrite";
  else if (q.nWrites) {
    if (q.writeOk) wr = q.simulated ? "sim" : "ok";
    else if (q.writePending) wr = q.removed ? "removed" : "timeout";
    else wr = statusName(q.writeStatus);
  }
  const bool failed = (q.nWrites || q.noWrites) && !q.writeOk;

  if (q.simulated) {
    vlog::event("simulated %s: kind=%s number=%u tier=%u rank=%u write=%s", q.path ? q.path : "?",
                token::kindName(q.c.kind), q.o.number, q.o.tier, q.o.rank, wr);
    reader::simulateLifted();      // the bench's idle button works again right away
  } else {
    if (failed) counters::c.writeFails++;
    if (cfg.logVisits && q.path) vlog::visit(q.path, q.c, q.o, wr);
    counters::save();
  }
  reader::led(LED_IDLE, s_idleRgb);
  // A SLEEPY "set it down again" or the no-crystal button is usually followed at
  // once by the same kid's crystal: that must not switch rush on (it would cut a
  // first visit's awakening). Real crowds still trigger rush (visits, 5 starts).
  const bool armsGap = !(q.path && (strcmp(q.path, "sleepy") == 0 || strcmp(q.path, "nocrystal") == 0));
  oracle::sequenceEnded(now, armsGap);
  q.active = false;
  s_lastCallMs = now;              // next attract call a full interval from now
  s_ledCycleMs = now;              // and the ring keeps the idle colour for a moment
  s_seqEndMs = now;                // no-button mode: taps rest for tap_cooldown_sec
  s_seqEnded = true;
}

// Decide the outcome now and send the tag write (marker page last).
void decideAndWrite(uint8_t fill) {
  Seq &q = s_seq;
  const uint32_t now = millis();
  q.circle = oracle::circleDue(now);
  q.o = oracle::decide(q.c, fill);
  q.o.flags |= token::SF_ORACLE;
  if (q.first) q.o.flags |= token::SF_FIRST;
  if (q.circle) q.o.flags |= token::SF_CIRCLE;
  if (fill >= 100) q.o.flags |= token::SF_CHARGED;
  q.decided = true;
  q.rankAfter = clampRank(q.o.rank);
  noteGroupColour(q.c.anonId, q.colorIdx, now);
  q.nWrites = token::buildVisitWrites(q.c, cfg.eventYear, cfg.baseYear, q.o.number, q.o.tier, q.o.flags,
                                      cfg.chapter, cfg.ndefUrl, q.writes, LINK_MAX_WRITES);
  if (!q.nWrites) {
    q.noWrites = true;
    vlog::event("no tag write built (year %u, base %u, crystal %08lx) - shown anyway",
                cfg.eventYear, cfg.baseYear, (unsigned long)q.c.anonId);
    return;
  }
  issueWrite(now);
}

void recordVisit() {
  Seq &q = s_seq;
  if (q.recorded || !q.decided) return;
  q.recorded = true;
  if (q.simulated) {               // tests never spend prizes, counters or meter points
    q.milestone = false;
    return;
  }
  moonAddPoint();                  // saved with the counters in endSequence()
  q.milestone = oracle::record(q.o, q.first) || q.o.milestone;
}

// Gag n for a trick, shared by visual / voice / sfx: chosen among the voice
// lines (else the gag clips, else the gag sfx), never the same twice in a row.
uint8_t pickGag() {
  uint8_t n = media::countVariants("voice", "reveal_t0", "wav");
  if (!n) n = max(media::countVariants("clips", "reveal_t0", "omj"), media::countVariants("clips", "reveal_t0", "jpg"));
  if (!n) n = media::countVariants("sfx", "reveal_t0", "wav");
  if (!n) n = 3;   // no art / voice: still vary (the fx.reveal_t0_<n> effects differ)
  return pickAvoiding(n, memoFor("gag:t0"));
}

// ---- single steps
void stepAbsorb(bool showColour) {
  Seq &q = s_seq;
  if (showColour) crystalLed(LED_SOLID, q.colorIdx);   // the plinth flared on its own; settle on the colour
  Step &st = newStep(Mode::Absorb);
  setId(st.vis[0], "%s", cue::ABSORB);
  // the colour NAME only for a bench colour the words define (like awaken_b): a crystal
  // with no / an unknown bench colour glows in a UID-picked colour that may not be its
  // filament, so naming it would mislabel the kid's crystal
  st.caption = (showColour && token::benchColor(q.c)) ? colourName(q.colorIdx) : s_w.capAbsorb;
  setTiming(st, T_ABSORB);
  tintCrystal(st);
  st.energy = 200;
  st.sfx = s_sfx.absorb;
  st.chime = audio::CHIME_ABSORB;
  st.overlay = absorbOverlay;
  runStep();
}

void stepAwaken() {
  Seq &q = s_seq;
  crystalLed(LED_CHARGING, q.colorIdx);
  q.rankBefore = 1;
  q.rankAfter = clampRank(q.o.rank ? q.o.rank : 1);
  q.gemsLit = 0;
  snprintf(q.capRank, sizeof q.capRank, "%s!", s_w.rank[q.rankAfter]);
  Step &st = newStep(Mode::Awaken);
  setId(st.vis[0], "%s", cue::AWAKEN);
  st.visVariant = clampColor(q.colorIdx);       // awaken_<colour> (the tool builds awaken_1..N)
  st.coherent = false;
  if (!q.rush) {
    addVoice(st, "awaken_a", 0, s_w.capAwaken);
    // the colour line only for a bench colour the words define (a future
    // filament's id, or none, skips it; the picture still uses displayColor)
    const uint8_t bench = token::benchColor(q.c);
    if (bench) addVoice(st, "awaken_b", bench, s_w.colorName[bench]);
    addVoice(st, "awaken_c", 0, q.capRank);
    st.caption = s_w.capAwaken;
  } else {                                      // rush: "You're a Spark!" only
    addVoice(st, "awaken_c", 0, q.capRank);
    st.caption = q.capRank;
  }
  setTiming(st, T_AWAKEN);
  q.gemAt = st.minMs * 3 / 10;
  q.gemAt2 = st.minMs * 5 / 10;
  tintCrystal(st);
  st.energy = 190;
  st.ambient = 10;
  st.sfx = s_sfx.awaken;
  st.chime = audio::CHIME_REVEAL;
  st.overlay = awakenOverlay;
  runStep();
}

void stepGreet() {
  Seq &q = s_seq;
  crystalLed(LED_SOLID, q.colorIdx);
  const uint8_t v = q.c.visits;
  const uint8_t va = v < 255 ? v + 1 : 255;
  q.rankBefore = clampRank(oracle::rankFor(v));
  q.rankAfter = clampRank(oracle::rankFor(va));
  q.meterFrom = oracle::meterFor(v);
  q.meterTo = oracle::meterFor(va);
  q.gemLit = false;

  Step &st = newStep(Mode::Greet);
  setId(st.vis[0], "greet_r%u", q.rankAfter);
  setId(st.vis[1], "%s", cue::GREET);
  if (!q.rush) {                       // rush: "the rank appears as a caption only"
    char g[ID_LEN];
    snprintf(g, sizeof g, "rankup_r%u", q.rankAfter);
    if (q.rankAfter == q.rankBefore || !voiceExists(g)) {
      snprintf(g, sizeof g, "greet_r%u", q.rankAfter);
      if (!voiceExists(g)) snprintf(g, sizeof g, "%s", cue::GREET);
    }
    addVoice(st, g);
  }
  st.caption = s_w.rank[q.rankAfter];
  st.titleDup = st.caption;            // the fallback layout shows it as the title
  setTiming(st, T_GREET);
  q.gemAt = st.minMs * 2 / 5;
  tintCrystal(st);
  st.energy = 150;
  st.ambient = 6;
  st.sfx = s_sfx.whoosh;
  st.chime = audio::CHIME_TAP;
  st.overlay = greetOverlay;
  runStep();
}

void stepRecall() {
  Seq &q = s_seq;
  Step &st = newStep(Mode::Recall);
  const uint8_t lt = clampTier((uint8_t)q.c.lastTier);
  setId(st.vis[0], "recall_t%u", lt);
  setId(st.vis[1], "%s", cue::RECALL);
  setId(st.vis[2], "%s", cue::GREET);
  char g[ID_LEN];
  snprintf(g, sizeof g, "recall_t%u", lt);
  addVoice(st, g);
  snprintf(q.recallLine, sizeof q.recallLine, "%u", q.c.lastYear);
  st.caption = s_w.capRecall;
  setTiming(st, T_RECALL);
  tintCrystal(st);
  st.energy = 110;
  st.overlay = recallOverlay;
  runStep();
}

void stepCharge() {
  Seq &q = s_seq;
  q.chargeFill = 0;
  q.chargeFullT = -1;
  q.chargeLedSent = 0;
  reader::led(LED_METER, q.rgb, 0, 0, 0, 0);   // (an animated colour: its shade now; each press re-sends)
  Step &st = newStep(Mode::Charge);
  setId(st.vis[0], "%s", cue::CHARGE);
  addVoice(st, cue::CHARGE);
  st.caption = s_w.capCharge;
  st.minMs = cfg.chargeMs;                     // the tap window stays open until the charge line ends
  st.maxMs = max(cfg.chargeMs, LINE_MS);
  st.loop = true;
  tintCrystal(st);
  st.energy = 140;
  st.ambient = 4;
  st.overlay = chargeOverlay;
  st.band = chargeBand;
  st.tick = chargeTick;
  runStep();
}

// The crystal left before its write landed (a returning crystal is written
// after the charge; a quick lift): ask for it back for a few seconds, so the
// visit is saved on the crystal (else it would roll again tonight). Ends as
// soon as it's back (onRepresented() re-issues or confirms the write), or when
// another crystal is waiting.
bool putBackTick(uint32_t t) {
  (void)t;
  return !s_seq.removed || s_seq.writeOk;   // back, or the write landed after all
}

void stepPutBack() {
  Seq &q = s_seq;
  vlog::event("crystal %08lx lifted before its write: asking for it back", (unsigned long)q.c.anonId);
  reader::led(LED_SLEEPY, q.rgb);   // (an animated colour: its shade now)
  Step &st = newStep(Mode::Sleepy);
  setId(st.vis[0], "%s", cue::SLEEPY);
  addVoice(st, cue::SLEEPY);
  st.caption = s_w.uiPlace;
  st.fbTitle = s_w.uiPlace;
  st.minMs = st.maxMs = LINE_MS;   // held after the line ends: putBackTick ends it early
  tintCrystal(st);
  st.energy = 60;
  st.chime = audio::CHIME_ERROR;
  st.band = hintsBand;
  st.tick = putBackTick;
  st.interruptible = true;          // the next crystal is waiting: don't hold it up
  runStep();
  if (!q.removed || q.writeOk) {
    audio::voiceStop();             // it's back: no "sleepy" line over HOLD / the reveal
    crystalLed(LED_CHARGING, q.colorIdx);
  }
}

// Make sure the tag write result is in before the reveal (DESIGN.md §5.1).
void stepHold() {
  Seq &q = s_seq;
  if (q.nWrites && !q.writeOk && q.removed && !q.simulated && !s_hasNext && q.writeTries < 3) stepPutBack();
  if (!q.writePending || q.removed) return;
  if (millis() - q.writeSentMs >= cfg.writeTimeoutMs) return;   // long overdue: don't stall the kid
  crystalLed(LED_CHARGING, q.colorIdx);
  Step &st = newStep(Mode::Hold);
  st.freeze = true;
  st.caption = s_w.uiHold;
  st.minMs = st.maxMs = 2u * cfg.writeTimeoutMs + 500u;   // hard cap; holdTick ends it
  tintCrystal(st);
  st.energy = 170;
  st.overlay = holdOverlay;
  st.tick = holdTick;
  runStep();
}

// REVEAL t<tier> with the bowl line over its end. Rush: the combined rush_t<tier>
// line (else the bowl line alone) and the bowl icon from the start.
void stepReveal() {
  Seq &q = s_seq;
  const uint8_t tier = clampTier(q.o.tier);
  Step &st = newStep(Mode::Reveal);
  q.gag = 0;
  if (tier == 0) {
    q.gag = pickGag();
    st.visVariant = q.gag;                 // reveal_t0_<n>
  }
  setId(st.vis[0], "reveal_t%u", tier);
  setId(st.vis[1], "%s", cue::REVEAL);
  st.coherent = false;
  const Timing &rt = tier == 3 ? T_REVEAL3 : T_REVEAL;
  char g[ID_LEN];
  if (q.rush) {
    setTiming(st, rt);
    q.bowlTier = (int8_t)tier;
    st.bowlTier = (int8_t)tier;
    st.bowlAt = 0;                         // icon from the start
    snprintf(g, sizeof g, "%s_t%u", cue::RUSH, tier);
    if (!voiceExists(g)) snprintf(g, sizeof g, "bowl_%s", cue::BOWL_KEY[tier]);
    addVoice(st, g);
  } else {
    st.minMs = rt.minMs + T_BOWL.minMs;
    st.maxMs = rt.maxMs + T_BOWL.maxMs;
    snprintf(g, sizeof g, "reveal_t%u", tier);
    addVoice(st, g, q.gag);                // trick: the gag's own line
    addBowlPart(st, tier, rt.minMs > 800 ? rt.minMs - 800 : 0, rt.minMs);
  }
  st.caption = s_w.capReveal[tier][0] ? s_w.capReveal[tier] : s_w.tier[tier];
  st.titleDup = s_w.tier[tier];
  st.rgb = s_w.tierRgb[tier];
  st.energy = (uint8_t)(150 + tier * 35);
  st.ambient = tier >= 2 ? 18 : 4;
  q.locked = false;
  q.lockAt = q.rush ? 350 : 650;
  st.overlay = revealOverlay;
  st.band = revealBand;
  runStep();
}

void stepExtra(Mode m, const char *id, const char *caption, Overlay ov, LedMode led, int sfx, audio::Chime chime) {
  if (led == LED_RAINBOW) reader::led(LED_RAINBOW, 0);
  else if (led != LED_OFF) crystalLed(led, s_seq.colorIdx);
  Step &st = newStep(m);
  setId(st.vis[0], "%s", id);
  addVoice(st, id);
  st.caption = caption;
  setTiming(st, T_EXTRA);
  tintCrystal(st);
  st.energy = 210;
  st.ambient = 16;
  st.sfx = sfx;
  st.chime = (int8_t)chime;
  st.overlay = ov;
  if (s_seq.bowlTier >= 0) {   // little reminder of which bowl
    st.bowlTier = s_seq.bowlTier;
    st.bowlMini = true;
    st.bowlAt = 0;
  }
  runStep();
}

void stepGoodbye() {
  Step &st = newStep(Mode::Goodbye);
  setId(st.vis[0], "%s", cue::GOODBYE);
  addVoice(st, cue::GOODBYE);
  st.caption = s_w.capGoodbye;
  st.fbTitle = s_w.capGoodbye;
  setTiming(st, T_GOODBYE);
  tintCrystal(st);
  st.energy = 90;
  st.ambient = 4;
  if (s_seq.bowlTier >= 0) {
    st.bowlTier = s_seq.bowlTier;
    st.bowlMini = true;
    st.bowlAt = 0;
  }
  runStep();
}

void stepReplay() {
  Seq &q = s_seq;
  const uint8_t tier = clampTier(q.o.tier);
  reader::led(LED_SOLID, display::scaleRgb(s_w.tierRgb[tier], 150));   // no reveal swirl: nothing new was won
  Step &st = newStep(Mode::Replay);
  setId(st.vis[0], "%s", cue::REPLAY);
  addVoice(st, cue::REPLAY);
  st.caption = s_w.capReplay;
  setTiming(st, T_REPLAY);
  if (cfg.secondHelpings) {        // second helpings come from the common bowl
    const uint32_t partMin = st.minMs;
    st.minMs += timingMin(T_BOWL_NC);
    st.maxMs += timingMax(T_BOWL_NC);
    addBowlPart(st, 0, partMin > 600 ? partMin - 600 : 0, partMin);
  }
  tintCrystal(st);
  st.energy = 130;
  st.ambient = 6;
  st.sfx = (tier >= 2) ? -1 : s_sfx.reveal[tier];   // never the rare / legendary sting (= the host's cue)
  st.chime = audio::CHIME_REVEAL;
  st.overlay = replayOverlay;
  st.band = replayBand;
  runStep();
}

// The star-wisp blessing (no crystal / foreign tag) + the common bowl line.
void stepNoCrystal(bool foreign, bool viaButton) {
  Seq &q = s_seq;
  crystalLed(LED_SOLID, q.colorIdx);   // a crystal colour (random for the button)
  Step &st = newStep(Mode::NoCrystal);
  if (foreign) {
    setId(st.vis[0], "%s", cue::FOREIGN);
    setId(st.vis[1], "%s", cue::NOCRYSTAL);
    addVoice(st, voiceExists(cue::FOREIGN) ? cue::FOREIGN : cue::NOCRYSTAL);
    st.caption = s_w.capForeign;
  } else {
    setId(st.vis[0], "%s", cue::NOCRYSTAL);
    addVoice(st, cue::NOCRYSTAL);
    st.caption = s_w.capNoCrystal;
  }
  st.fbTitle = st.caption;
  const uint32_t partMin = timingMin(T_NOCRYSTAL);
  st.minMs = partMin + timingMin(T_BOWL_NC);
  st.maxMs = timingMax(T_NOCRYSTAL) + timingMax(T_BOWL_NC);
  addBowlPart(st, 0, partMin > 500 ? partMin - 500 : 0, partMin);
  tintCrystal(st);
  st.energy = 170;
  st.ambient = 14;
  st.sfx = viaButton ? s_sfx.button : s_sfx.whoosh;
  st.chime = audio::CHIME_REVEAL;
  st.overlay = nocrystalOverlay;
  runStep();
}

void stepSleepy() {
  reader::led(LED_SLEEPY, SLEEPY_WHITE);
  Step &st = newStep(Mode::Sleepy);
  setId(st.vis[0], "%s", cue::SLEEPY);
  addVoice(st, cue::SLEEPY);
  st.caption = s_w.capSleepy;
  st.fbTitle = s_w.capSleepy;
  setTiming(st, T_SLEEPY);
  tintCrystal(st);
  st.energy = 40;
  st.sfx = s_sfx.sleepy;
  st.chime = audio::CHIME_ERROR;
  st.band = hintsBand;
  st.interruptible = true;          // placing it again starts over at once
  runStep();
}

// ---- reveal chain (first + returning visits)
void revealChain() {
  Seq &q = s_seq;
  stepHold();
  recordVisit();
  stepReveal();
  if (clampRank(q.o.rank) >= 7 && !q.rush && cfg.keeperOneIn && randBelow(cfg.keeperOneIn) == 0)
    stepExtra(Mode::Extra, cue::KEEPER, s_w.capKeeper, keeperOverlay, LED_SOLID, -1, audio::CHIME_TAP);
  if (q.milestone)
    stepExtra(Mode::Extra, cue::MILESTONE, s_w.capMilestone, milestoneOverlay, LED_RAINBOW, s_sfx.milestone,
              audio::CHIME_LEGEND);
  if (q.circle)
    stepExtra(Mode::Extra, cue::CIRCLE, s_w.capCircle, circleOverlay, LED_RAINBOW, s_sfx.whoosh, audio::CHIME_RARE);
  if (!q.rush && !s_hasNext) stepGoodbye();   // a crystal is waiting: skip the farewell
}

// ---- whole sequences
void runCrystal() {
  const uint32_t now = millis();
  beginSequence(now);
  Seq &q = s_seq;
  token::parse(s_cur, cfg.eventYear, cfg.baseYear, q.c);
  q.simulated = s_cur.uidLen > 0 && s_cur.uid[0] == SIM_UID0;
  setSeqColour(token::displayColor(q.c), now);
  fx::sparklesClear();

  token::Kind kind = q.c.kind;
  if (kind == token::Kind::Fresh && !cfg.allowFresh) kind = token::Kind::Foreign;
  // NTAG / Ultralight always answer SAK 0x00. A bank card, phone or bus pass
  // (SAK 0x20 / 0x08 ...) can never be read as a crystal: star-wisp, not SLEEPY.
  if (kind == token::Kind::Unreadable && s_cur.uidLen && s_cur.sak != 0x00) kind = token::Kind::Foreign;
  Serial.printf("[show] crystal %08lx kind=%s visits=%u played=%d colour=%u rush=%d%s\n", (unsigned long)q.c.anonId,
                token::kindName(q.c.kind), q.c.visits, q.c.playedThisYear, q.c.color, q.rush,
                q.simulated ? " (simulated)" : "");

  if (kind == token::Kind::Unreadable) {
    q.path = "sleepy";
    if (!q.simulated) counters::c.sleepy++;
    stepAbsorb(false);
    stepSleepy();
  } else if (kind == token::Kind::Foreign) {
    q.path = "foreign";
    if (!q.simulated) {
      counters::c.foreign++;
      moonAddPoint();                      // every visitor counts (STORY.md §9 nocrystal)
    }
    stepAbsorb(false);
    stepNoCrystal(true, false);
  } else if (q.c.playedThisYear) {
    q.path = "replay";
    if (!q.simulated) counters::c.replays++;
    q.o = oracle::replay(q.c);
    q.rankAfter = clampRank(q.o.rank);
    noteGroupColour(q.c.anonId, q.colorIdx, now);
    stepAbsorb(true);
    stepReplay();
  } else if (q.c.firstVisit()) {
    q.path = "awaken";
    q.first = true;
    decideAndWrite(0);                     // written at placement, before the story plays
    stepAbsorb(true);
    stepAwaken();
    revealChain();
  } else {
    q.path = "return";
    q.rankAfter = clampRank(oracle::rankFor(q.c.visits < 255 ? q.c.visits + 1 : 255));
    const bool canCharge = !q.rush && cfg.chargeMs > 0 &&
                           (touch::ok() || (cfg.hasButton && cfg.reader == READER_PLINTH && reader::online()));
    if (!canCharge) decideAndWrite(0);     // nothing to wait for: write while the crystal is surely there
    stepAbsorb(true);
    stepGreet();
    if (!q.rush && !s_hasNext && q.c.lastTier >= 0) {   // "only when nobody is waiting"
      char g[ID_LEN];
      snprintf(g, sizeof g, "recall_t%u", clampTier((uint8_t)q.c.lastTier));
      if (voiceExists(g)) stepRecall();
    }
    if (canCharge) {
      if (!s_hasNext && !q.removed) stepCharge();   // else: no bonus (chargeFill 0); stepHold asks for it back
      decideAndWrite(q.chargeFill);
    }
    revealChain();
  }
  endSequence();
}

void runNoCrystal(bool viaButton) {
  const uint32_t now = millis();
  beginSequence(now);
  Seq &q = s_seq;
  q.path = "nocrystal";
  setSeqColour((uint8_t)(1 + randBelow(nColors())), now);   // "LEDs glow a random crystal colour"
  counters::c.noCrystal++;
  // This kid helped too: +1 spark. No-button mode: a TAP blessing adds one at most every
  // tap_spark_sec (anti-spam: taps are free, a crystal visit always adds its spark).
  if (viaButton || cfg.hasButton || !s_tapSparked || now - s_tapSparkMs >= (uint32_t)cfg.tapSparkSec * 1000u) {
    moonAddPoint();
    if (!viaButton && !cfg.hasButton) {
      s_tapSparked = true;
      s_tapSparkMs = now;
    }
  }
  fx::sparklesClear();
  stepNoCrystal(false, viaButton);
  endSequence();
}

// The chapter story in parts a..f (each: visual chapter_<p> or "chapter", voice
// chapter_<p>, caption caption.chapter_<p>). Any input ends it.
void runChapter() {
  uint8_t played = 0;
  for (uint8_t i = 0; i < 6; i++) {
    const char p = (char)('a' + i);
    char id[ID_LEN];
    snprintf(id, sizeof id, "%s_%c", cue::CHAPTER, p);
    const bool hasVoice = voiceExists(id);
    const bool hasVis = media::findVisual(id).kind != media::Kind::None;
    if (!hasVoice && !hasVis) continue;
    Step &st = newStep(Mode::Chapter);
    setId(st.vis[0], "%s", id);
    if (hasVoice) setId(st.vis[1], "%s", cue::CHAPTER);
    if (hasVoice) addVoice(st, id);
    st.caption = s_w.capChapterPart[i];
    st.fbTitle = st.caption[0] ? st.caption : nullptr;
    st.minMs = (hasVoice && audio::ok()) ? 0 : CHAPTER_PART_MIN_MS;   // silent host: voice files can't set the length
    st.maxMs = CHAPTER_PART_MAX_MS;
    st.minFromClip = true;
    st.rgb = s_idleRgb;
    st.rainbow = true;
    st.energy = 90;
    st.ambient = 6;
    st.interruptible = true;
    played++;
    if (!runStep()) break;                 // a crystal / button / service request
  }
  if (!played && media::findVisual(cue::CHAPTER).kind != media::Kind::None) {   // one-piece chapter clip
    Step &st = newStep(Mode::Chapter);
    setId(st.vis[0], "%s", cue::CHAPTER);
    st.caption = s_w.capChapter;
    st.fbTitle = s_w.capChapter[0] ? s_w.capChapter : nullptr;
    st.minMs = 8000;
    st.maxMs = 120000;
    st.minFromClip = true;
    st.rgb = s_idleRgb;
    st.rainbow = true;
    st.energy = 90;
    st.ambient = 6;
    st.interruptible = true;
    runStep();
  }
  s_loopsSinceChapter = 0;
  s_lastCallMs = millis();
}

// MOONFULL: once per night when the meter reaches the goal (or the host's
// "Fill the <meter.name>"). Marked + saved first, so a reset mid-finale can't loop it.
void runMoonFull() {
  s_moonFinaleDue = false;
  Counters &k = counters::c;
  k.moonFull = true;
  counters::save();
  vlog::event("%s full: finale (points %u, goal %u)", s_meter.name, k.moonPoints, cfg.moonGoal);
  updateMoonTarget(millis());
  reader::led(LED_RAINBOW, 0);
  s_seq = Seq();                          // not a visit: no crystal colour (anchor glow = glow.idle_color)
  fx::sparklesClear();
  Step &st = newStep(Mode::MoonFull);
  setId(st.vis[0], "%s", cue::MOONFULL);
  st.coherent = false;
  addVoice(st, "moonfull_a", 0, s_w.capMoonFull);
  addVoice(st, "moonfull_b", 0, s_w.capMoonFull2);
  st.caption = s_w.capMoonFull;
  st.fbTitle = s_w.capMoonFull;
  st.minMs = (audio::ok() && voiceExists("moonfull_a")) ? 0 : MOONFULL_MIN_MS;
  st.maxMs = MOONFULL_MAX_MS;
  st.minFromClip = true;
  st.rgb = s_meter.rgb;
  st.rainbow = true;
  st.energy = 230;
  st.ambient = 20;
  st.sfx = s_sfx.moonfull;
  st.chime = audio::CHIME_LEGEND;
  st.overlay = moonfullOverlay;
  runStep();
  startMusic();                           // the brighter bed from now on
  reader::led(LED_IDLE, s_idleRgb);
  s_lastCallMs = millis();
  s_ledCycleMs = millis();
  s_seqEndMs = millis();                  // no-button mode: the cheering crowd's taps rest a moment too
  s_seqEnded = true;
}

bool maybeMoonFinale() {
  if (!s_moonFinaleDue && !moonFinalePending()) return false;
  runMoonFull();
  return true;
}

void runAttract() {
  Step &st = newStep(Mode::Attract);
  if (counters::c.moonFull) {
    setId(st.vis[0], "%s", cue::ATTRACT_FULL);
    setId(st.vis[1], "%s", cue::ATTRACT);
  } else {
    setId(st.vis[0], "%s", cue::ATTRACT);
  }
  st.caption = s_w.capAttract;
  st.fbTitle = s_w.title;
  st.minMs = st.maxMs = UINT32_MAX;
  st.loop = true;
  st.rgb = s_idleRgb;
  st.rainbow = true;
  st.energy = 70;
  st.ambient = 4;
  st.interruptible = true;
  st.overlay = attractOverlay;
  st.band = hintsBand;
  st.tick = attractTick;
  s_attractLoopsSeen = 0;
  s_munchLoop = UINT32_MAX;
  startMusic();
  runStep();
}

// ---- setup helpers
void loadWords() {
  char key[32];
  s_w.title = settings::text("title", "The Crystal Oracle");
  static const char *const RANK_FALLBACK[8] = {"", "Spark", "Glimmer", "Glow", "Radiant", "Luminous", "Starborn", "Keeper"};
  for (int r = 1; r <= 7; r++) {
    snprintf(key, sizeof key, "rank.%d", r);
    s_w.rank[r] = settings::text(key, RANK_FALLBACK[r]);
  }
  s_w.rank[0] = s_w.rank[1];
  static const char *const TIER_FALLBACK[4] = {"TRICK!", "TREAT!", "RARE!", "LEGENDARY!"};
  for (int t = 0; t < 4; t++) {
    snprintf(key, sizeof key, "tier.%d", t);
    s_w.tier[t] = settings::text(key, TIER_FALLBACK[t]);
    snprintf(key, sizeof key, "tier.%d_color", t);
    s_w.tierRgb[t] = settings::color(key, s_w.tierRgb[t]);
    const char *bk = cue::BOWL_KEY[t];
    snprintf(key, sizeof key, "bowl.%s", bk);
    s_w.bowlName[t] = settings::text(key, bk);
    snprintf(key, sizeof key, "bowl.%s_color", bk);
    s_w.bowlRgb[t] = settings::color(key, s_w.bowlRgb[t]);
    snprintf(key, sizeof key, "bowl.%s_shape", bk);
    s_w.bowlShape[t] = fx::shapeFromName(settings::text(key, t == 2 ? "triangle" : (t == 3 ? "star" : "circle")));
    snprintf(key, sizeof key, "caption.reveal_t%d", t);
    s_w.capReveal[t] = settings::text(key, "");
  }
  // Colour NAMES here; the colours (steady / gradient / rainbow) are parsed by
  // settings (crystalRgb) - the names of ids past colorCount() are never shown.
  static const char *const COLOR_FALLBACK[7] = {"", "Violet", "Ember", "Frost", "Moss", "Rose", "Moon"};
  for (int n = 1; n <= 6; n++) {
    snprintf(key, sizeof key, "color.%d", n);
    s_w.colorName[n] = settings::text(key, COLOR_FALLBACK[n]);
  }
  s_w.colorName[0] = s_w.colorName[1];
  s_w.capAttract   = settings::text("caption.attract", "Place your crystal");
  s_w.capAbsorb    = settings::text("caption.absorb", "");
  s_w.capAwaken    = settings::text("caption.awaken", "Awake!");
  s_w.capGreet     = settings::text("caption.greet", "Welcome back!");
  s_w.capRecall    = settings::text("caption.recall", "LAST YEAR");
  s_w.capCharge    = settings::text("caption.charge", "Tap! Tap! Tap!");
  s_w.capReplay    = settings::text("caption.replay", "Already glowing!");
  s_w.capNoCrystal = settings::text("caption.nocrystal", "A blessing!");
  s_w.capForeign   = settings::text("caption.foreign", s_w.capNoCrystal);
  s_w.capSleepy    = settings::text("caption.sleepy", "Try again");
  s_w.capGoodbye   = settings::text("caption.goodbye", "See you next year!");
  s_w.capMilestone = settings::text("caption.milestone", "Milestone!");
  s_w.capCircle    = settings::text("caption.circle", "Circle complete!");
  s_w.capKeeper    = settings::text("caption.keeper", "Keeper!");
  s_w.capChapter   = settings::text("caption.chapter", "");
  s_w.capMoonFull  = settings::text("caption.moonfull", "WE DID IT!");
  s_w.capMoonFull2 = settings::text("caption.moonfull_b", s_w.capMoonFull);
  for (int i = 0; i < 6; i++) {
    snprintf(key, sizeof key, "caption.chapter_%c", 'a' + i);
    s_w.capChapterPart[i] = settings::text(key, s_w.capChapter);
  }
  s_w.uiHold    = settings::text("ui.hold", "Hold still...");
  s_w.uiPower   = settings::text("ui.power", "POWER");
  s_w.uiCounter = settings::text("ui.counter", "crystals awakened");
  s_w.uiPress   = settings::text("ui.press", "Press the button!");
  s_w.uiAsleep  = settings::text("ui.reader_asleep", "READER ASLEEP");
  s_w.uiRush    = settings::text("ui.rush", "RUSH");
  s_w.uiTouch   = settings::text("ui.touch", "Touch me for a blessing!");
  s_w.uiNoCard  = settings::text("ui.no_card", "NO TF CARD");
  s_w.uiOrPress = settings::text("ui.or_press", "OR PRESS");
  s_w.uiTonight = settings::text("ui.tonight", "TONIGHT");
  s_w.uiPlace   = settings::text("ui.place", "Crystal on the stone");
  s_w.glowIdle  = settings::color("glow.idle_color", 0xCFFFF2);
  s_idleRgb = steadyRgb(1);   // the plinth's idle colour (one fixed shade: HELLO_ACK carries one colour)
  loadMeter();
  loadVoiceSubs();            // words nobutton.voice (button = no only)
  if (!cfg.hasButton)
    vlog::event("no plinth button (config.ini button = no): taps bless, words nobutton.* in use");
}

// Missing files are skipped without touching the card (name cache), so the
// audio engine never logs "missing" for optional sounds.
int loadSfx(const char *id) {
  char path[48];
  snprintf(path, sizeof path, "/oracle/sfx/%s.wav", id);
  if (!media::exists(path)) return -1;
  return audio::sfxLoad(path);
}

// Most important first, in case the audio engine's clip cache ever fills up
// (a -1 handle simply means "use the generated chime").
void loadAllSfx() {
  s_sfx.absorb = loadSfx(cue::SFX_ABSORB);
  s_sfx.tap    = loadSfx(cue::SFX_TAP);
  s_sfx.reveal[1] = loadSfx(cue::SFX_REVEAL[1]);
  s_sfx.reveal[2] = loadSfx(cue::SFX_REVEAL[2]);
  s_sfx.reveal[3] = loadSfx(cue::SFX_REVEAL[3]);
  s_sfx.reveal[0] = loadSfx(cue::SFX_REVEAL[0]);
  s_sfx.awaken = loadSfx(cue::SFX_AWAKEN);
  s_sfx.full   = loadSfx(cue::SFX_FULL);
  s_sfx.whoosh = loadSfx(cue::SFX_WHOOSH);
  s_sfx.button = loadSfx(cue::SFX_BUTTON);
  s_sfx.sleepy = loadSfx(cue::SFX_SLEEPY);
  char id[ID_LEN];
  for (uint8_t n = 1; n <= MAX_GAGS; n++) {
    snprintf(id, sizeof id, "%s_%u", cue::SFX_REVEAL[0], n);
    s_sfx.gag[n] = loadSfx(id);
    if (s_sfx.gag[n] < 0) s_sfx.gag[n] = s_sfx.reveal[0];   // the generic trick sting
  }
  s_sfx.munch     = loadSfx(cue::SFX_MUNCH);
  s_sfx.milestone = loadSfx(cue::SFX_MILESTONE);
  s_sfx.moonfull  = loadSfx(cue::SFX_MOONFULL);
}

// Media-dependent facts (re-checked after the service menu, which can rescan).
void refreshMediaFacts() {
  bool chapter = media::findVisual(cue::CHAPTER).kind != media::Kind::None;
  for (char p = 'a'; p <= 'f' && !chapter; p++) {
    char id[ID_LEN];
    snprintf(id, sizeof id, "%s_%c", cue::CHAPTER, p);
    chapter = voiceExists(id) || media::findVisual(id).kind != media::Kind::None;
  }
  s_chapterAvail = chapter;
  s_musicFile = media::exists(MUSIC_PATH);
  s_musicFullFile = media::exists(MUSIC_FULL_PATH);
}

void enterService() {
  vlog::event("service menu opened");
  reader::led(LED_SOLID, 0xFFFFFF);
  service::run();
  // A crystal still on the plinth has been dealt with (prepared on the Crystals page, or it
  // played before the menu opened): its re-announcement after the menu must not play it.
  reader::ignoreUntilLifted();
  refreshMediaFacts();
  loadAllSfx();                     // same path -> same handle; picks up newly added files
  startMusic();                     // (switches beds if the host filled / reset the meter)
  reader::setIdleLight(s_idleRgb, cfg.ringBrightness);
  reader::led(LED_IDLE, s_idleRgb);
  const uint32_t now = millis();
  s_lastCallMs = now;
  s_musicCheckMs = now;
  s_ledCycleMs = now;
  s_buttonReq = s_tapReq = s_serviceReq = false;
  updateMoonTarget(now);
  if (moonFinalePending()) s_moonFinaleDue = true;   // "Fill the <meter>": the finale plays now
  vlog::event("service menu closed (%s %u/%u%s)", s_meter.name, counters::c.moonPoints, cfg.moonGoal,
              counters::c.moonFull ? " full" : "");
}

// Runs whatever is waiting. Returns false if nothing was.
bool dispatchPending() {
  if (s_hasNext) {
    s_cur = s_next;
    s_hasNext = false;
    runCrystal();
    maybeMoonFinale();
    return true;
  }
  if (s_buttonReq || s_tapReq) {
    const bool viaButton = s_buttonReq;
    s_buttonReq = s_tapReq = false;
    runNoCrystal(viaButton);
    maybeMoonFinale();
    return true;
  }
  if (s_serviceReq) {
    s_serviceReq = false;
    enterService();
    maybeMoonFinale();
    return true;
  }
  if (s_chapterReq) {
    s_chapterReq = false;
    runChapter();
    return true;
  }
  return false;
}

}  // namespace

// ================================================================== public API

void begin() {
  loadWords();
  loadAllSfx();
  refreshMediaFacts();
  if (!s_freeze) {
    s_freeze = (uint16_t *)heap_caps_malloc((size_t)display::W * display::H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!s_freeze) vlog::event("show: no PSRAM for the hold-still snapshot (procedural instead)");
  }
  moonSnap();
  if (moonFinalePending()) s_moonFinaleDue = true;   // e.g. reached before a power cut
  startMusic();
  reader::setIdleLight(s_idleRgb, cfg.ringBrightness);
  reader::led(LED_IDLE, s_idleRgb);
  fx::sparklesClear();
  const uint32_t now = millis();
  s_lastCallMs = now;
  s_musicCheckMs = now;
  s_ledCycleMs = now;
  s_begun = true;
  vlog::event("show ready: chapter=%d music=%d/%d sfx absorb=%d reveal=%d/%d/%d/%d meter=%u/%u%s", s_chapterAvail,
              s_musicFile, s_musicFullFile, s_sfx.absorb, s_sfx.reveal[0], s_sfx.reveal[1], s_sfx.reveal[2],
              s_sfx.reveal[3], counters::c.moonPoints, cfg.moonGoal, counters::c.moonFull ? " full" : "");
}

void loop() {
  if (!s_begun) begin();
  if (dispatchPending()) return;
  if (maybeMoonFinale()) return;
  runAttract();
  dispatchPending();
}

void bootScreen(const char *const *lines, uint8_t n) {
  if (!display::ok()) return;
  if (!display::beginFrame()) return;
  display::fill(0);
  display::addGlow(CX, 120, 260, 0x5B21B6, 110);
  const uint16_t white = display::rgb565(0xFFFFFF);
  const uint16_t black = 0;
  display::textCentered(settings::text("title", "The Crystal Oracle"), CX, 120, u8g2_font_fub30_tr,
                        display::rgb565(0xE9D5FF), black, 2);
  if (n > 8) n = 8;
  for (uint8_t i = 0; i < n; i++) {
    if (!lines || !lines[i]) continue;
    display::textCentered(lines[i], CX, (int16_t)(196 + i * 34), u8g2_font_fub14_tr, white, black, 1);
  }
  display::endFrame();
}

}  // namespace show
