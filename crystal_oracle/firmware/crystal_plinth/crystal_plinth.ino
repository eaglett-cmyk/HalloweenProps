/*
 * ============================================================================
 *  CRYSTAL PLINTH  --  the reader half of the Crystal Oracle (the "Wakestone")
 *  v1.2.0 (2026-10-22: the owner's v7 plinth has NO LED ring and NO big button:
 *          both are compile-time options, PLINTH_HAS_RING / PLINTH_HAS_BUTTON,
 *          default 0; TAG_PLACED reports how long the read took (readMs); the
 *          AITRIP board's blue LED shows the link)
 *  v1.1.0 (LED_DUAL for two-colour gradient crystals, CHARGING comets in two
 *          colours, smooth crossfades between ring looks)
 *  Board  : the AITRIP ESP32-WROOM-32 30-pin board ("ESP32 Dev Module", FQBN
 *           esp32:esp32:esp32) on its ~63 x 66 mm screw-terminal carrier - the
 *           SAME board nfc_tag_bench runs on - in the v7 plinth
 *           (../../plinth/crystal_plinth_v7.scad). Optional alternative: the
 *           Adafruit Feather ESP32-S3 (the old Skull Oracle controller) with a
 *           NeoPixel ring and a big button. The pin map is picked automatically
 *           from the board you compile for.
 *  Partner: the Crystal Oracle screen (../crystal_oracle) over ESP-NOW, using
 *           crystal_link.h (a byte-identical copy lives in both sketch folders;
 *           tools/build_assets.py --check fails if they differ).
 * ----------------------------------------------------------------------------
 *  WHAT IT DOES
 *    The plinth is the stone under the crystal. It is deliberately dumb and
 *    fast -- the screen owns every game decision (and the crystal prep: the
 *    screen's host menu -> Crystals reads, colours, provisions and formats
 *    crystals through this plinth, so no bench is needed at the door).
 *
 *    Crystal arrives : the PN532 is polled every ~80 ms. A new tag (with a
 *        ring: the ring FLARES at once, ~0.6 s) is read whole (pages 3..39,
 *        ten 16-byte READs, retried) and sent to the screen (LT_TAG_PLACED,
 *        with readMs = how long the read took). 4 misses in a row (~0.3 s)
 *        = LT_TAG_REMOVED.
 *    Writes : the screen sends an ordered page list (LT_WRITE_REQ). The plinth
 *        re-selects the tag, checks the UID, writes each page (3 tries), reads
 *        every written page back, and answers LT_WRITE_RESULT. Two guards that
 *        never move to the screen: only pages 3..39, and page 3 (the NDEF
 *        capability container, one-time-programmable) is skipped when its
 *        byte 0 is already 0xE1. The last 4 results are cached by reqId + a
 *        hash of the UID and page list, so the screen's resends (every
 *        250 ms) are answered without writing twice. A write list gives up
 *        after 1.8 s (the screen stops waiting at 2 s).
 *    Ring (PLINTH_HAS_RING 1 only): every LedMode in crystal_link.h at ~60 fps,
 *        gamma-corrected, brightness-capped and current-limited (RING_*). A
 *        repeated identical steady command is a refresh. A new steady look
 *        crossfades in over RING_XFADE_MS; FLARE / REVEAL hit at once. An
 *        unknown mode is ignored. Without a ring (the default) LT_LED is
 *        accepted and ignored: no NeoPixel code runs at all.
 *    Button (PLINTH_HAS_BUTTON 1 only): debounced, sent as LT_BUTTON (press,
 *        and release + held time). Without one (the default) the pin is never
 *        read and HELLO.buttonDown is always 0 - the screen runs with
 *        config.ini [station] button = no (kids tap the screen instead).
 *    Link : HELLO is broadcast every second until the screen answers; after
 *        that HELLO is a unicast heartbeat (it carries tagPresent, so the
 *        screen can repair a lost REMOVED). The screen PINGs every second.
 *        Both HELLO_ACK and PING carry the idle look (mode, colour, brightness
 *        cap). If nothing arrives from the screen for 3 s the plinth goes
 *        standalone (a ring breathes violet on its own) and HELLO goes back to
 *        broadcast until the screen answers again.
 *    It never reboots itself on an error: a missing PN532 is retried every 3 s,
 *    a PN532 that stops answering is reset and re-initialised, every wait has
 *    a timeout.
 *
 *  HARDWARE / WIRING  (firmware/crystal_plinth/README.md has the full tables)
 *  --------------------------------------------------------------------------
 *    PN532: Elechouse PN532 NFC Module V3 (HiLetgo's clone works the same) in
 *    I2C MODE -> DIP switches SW1 ON, SW2 OFF. Power it from 3V3 so its I2C
 *    lines stay at 3.3 V. In the v7 plinth it lies in the fitted pocket under
 *    the flush 2 mm lid, against the pocket's -x/-y walls, plugs toward the
 *    wire slot (+x/+y).
 *
 *    AITRIP ESP32-WROOM-32 on the screw-terminal board  (silk label in [brackets])
 *      PN532 VCC  -> 3V3            PN532 GND  -> GND
 *      PN532 SDA  -> GPIO21         PN532 SCL  -> GPIO22
 *      PN532 IRQ  -> GPIO16 [RX2]   PN532 RSTO -> GPIO17 [TX2]
 *            (exactly the nfc_tag_bench wiring: one board does both jobs)
 *      Power      -> the board's USB-C port, behind the plinth's front port
 *      Status     -> the onboard blue LED (GPIO2, no wiring)
 *      Optional ring DIN -> 330R -> GPIO4 [D4] (PLINTH_HAS_RING 1);
 *      optional button   -> GPIO23 [D23] and GND (PLINTH_HAS_BUTTON 1)
 *
 *    Adafruit Feather ESP32-S3  (optional alternative; Dn = GPIOn)
 *      PN532 VCC -> 3V    GND -> GND    SDA -> SDA    SCL -> SCL
 *            (or simply the STEMMA QT jack -- the sketch powers it)
 *      PN532 IRQ -> GP6 [D6]           PN532 RSTO -> GP9 [D9]
 *      Ring  DIN -> 330R -> GP10 [D10] Ring 5V -> USB    Ring GND -> GND
 *            (330 ohm in series with DIN, 1000 uF across the ring's 5V/GND)
 *      Button    -> GP12 [D12] and GND
 *            NOT GP13 [D13] (the skull's old outside-button pin): D13 also
 *            drives the Feather's red LED (to GND), which clamps the weak
 *            internal pull-up to ~1.6 V -- neither HIGH nor LOW, so a button
 *            there reads "pressed at boot" or chatters. (If it must stay on
 *            GP13: #define PIN_BUTTON 13 and BUTTON_ACTIVE_HIGH 1 and wire it
 *            to 3V instead of GND; the red LED then lights while pressed.)
 *      Status    -> the onboard NeoPixel (no wiring)
 *      Build it with ring + button: #define PLINTH_HAS_RING 1 and
 *      PLINTH_HAS_BUTTON 1 below (or -D on the command line).
 *
 *  STATUS LIGHT (first match wins) - visible through the closed plinth
 *      AITRIP blue LED  : fast blink (5 Hz) = PN532 missing | slow blink (1 Hz)
 *        = searching for the screen (standalone) | steady on = linked to the
 *        screen | on with a short flicker every 0.5 s = linked + a crystal on it.
 *      Feather NeoPixel : red fast blink = PN532 missing | violet = tag on the
 *        reader | blue blink once a second = standalone (no screen) | green =
 *        talking to the screen.
 *
 *  LIBRARIES (Arduino Library Manager): "Adafruit PN532" (pulls in "Adafruit
 *    BusIO"); "Adafruit NeoPixel" only with PLINTH_HAS_RING 1. WiFi / ESP-NOW /
 *    Wire ship with the core.
 *
 *  BOARD SETTINGS: Espressif "esp32" core 3.x (built against 3.3.10).
 *      AITRIP  : "ESP32 Dev Module"   FQBN esp32:esp32:esp32   (defaults)
 *      Feather : "Adafruit Feather ESP32-S3 2MB PSRAM"
 *                FQBN esp32:esp32:adafruit_feather_esp32s3  (defaults are
 *                fine: USB CDC On Boot = Enabled, USB Mode = USB-OTG/TinyUSB;
 *                the _nopsram / _tft / _reversetft Feathers use the same pins)
 *
 *  SERIAL: 115200 baud. Boot banner, link / tag / write / button events, a
 *    status line every 10 s. Type  h  for the few one-letter test commands.
 *
 *  THINGS TO TUNE: search this file for "TUNE".
 *  ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>                // Adafruit_PN532.h includes Adafruit_SPIDevice.h
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Adafruit_PN532.h>
#include <atomic>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include "crystal_link.h"

// ============================ configuration (TUNE) ==========================
#define FW_VERSION          120        // 1.2.0 -> LinkHello.fwVersion (1.1 = understands LED_DUAL,
                                       //   1.2 = TAG_PLACED carries readMs)
#define DEBUG               1          // 1 = log to Serial @115200

// --- what this plinth has (TUNE) ---
// The owner's v7 plinth (2026) has neither: the screen runs with config.ini
// [station] button = no and kids tap the screen. 1 = fitted (the Feather build
// with the skull's ring + button, or an AITRIP with them wired up). Override on
// the command line too, e.g. arduino-cli compile --build-property
//   "compiler.cpp.extra_flags=-DPLINTH_HAS_RING=1 -DPLINTH_HAS_BUTTON=1" ...
#ifndef PLINTH_HAS_RING
  #define PLINTH_HAS_RING   0          // TUNE: 1 = a WS2812B ring on PIN_RING (needs "Adafruit NeoPixel")
#endif
#ifndef PLINTH_HAS_BUTTON
  #define PLINTH_HAS_BUTTON 0          // TUNE: 1 = the big arcade button on PIN_BUTTON
#endif
#if PLINTH_HAS_RING
  #include <Adafruit_NeoPixel.h>
#endif

// --- LED ring ---
#define RING_LEDS           24         // TUNE: pixels on the ring
#define RING_MAX_BRIGHTNESS 120        // TUNE: brightness ceiling 0..255, linear (after gamma).
                                       //   The screen can only lower it (LT_LED / HELLO_ACK brightness).
#define RING_MAX_MA         500        // TUNE: estimated ring current limit in mA (0 = no limiter).
                                       //   Every frame is scaled down if it would draw more.
#define RING_MA_PER_CHANNEL 20         //   WS2812B: ~20 mA per colour channel at full PWM
#define RING_TOP_PIXEL      0          // TUNE: pixel index at "12 o'clock" (meter / ripple start)
#define RING_CCW            0          // TUNE: 1 = animations and the meter run the other way round
#define RING_TYPE           (NEO_GRB + NEO_KHZ800)   // TUNE: WS2812B / SK6812 RGB rings
#define RING_GAMMA          2.6f       // same curve as Adafruit_NeoPixel::gamma8 / gamma32
#define RING_FPS            60
#define RING_XFADE_MS       450        // TUNE: a new steady look fades in over this long (0 = cut)

// --- colours (0xRRGGBB) ---
#define FLARE_COLOR         0xA78BFA   // TUNE: placement flare starts white and cools to this violet
#define FLARE_MS            600        // TUNE: flare decay time
#define STANDALONE_COLOR    0x8B5CF6   // TUNE: ring colour without a screen ("Violet", crystal colour 1)
#define SPARKLE_COLOR       0xFFE7C2   // TUNE: button sparkle (warm white)
#define STANDALONE_SLEEPY_MS 3000      // standalone: sleepy blink this long after an unreadable tag

// --- link ---
#define LINK_GROUP          LINK_DEFAULT_GROUP   // TUNE: = config.ini [station] group on the screen
#define LINK_CHANNEL        LINK_DEFAULT_CHAN    // TUNE: = config.ini [station] channel on the screen
#define HELLO_MS            1000       // heartbeat period (LINK-PROTOCOL.md §3)
#define SCREEN_TIMEOUT_MS   3000       // nothing from the screen this long -> standalone ring +
                                       //   broadcast HELLO again (the screen PINGs every 1 s)
#define RX_QUEUE_LEN        12         // received packets waiting for loop()
#define PLACED_WATCH_MS     400        // a radio failure reported this soon after a unicast
#define PLACED_RESENDS      2          //   TAG_PLACED -> send it again (at most this often)

// --- NFC (PN532) ---
#define I2C_HZ              100000     // TUNE: PN532 I2C clock; 400000 is fine with short wires
#define POLL_MS             80         // poll period
#define POLL_TIMEOUT_MS     50         // one poll (InListPassiveTarget); doubled automatically (max
                                       //   200) if polls keep timing out on a PN532 that is alive
#define SELECT_TIMEOUT_MS   100        // re-select before / during writes
#define IO_TIMEOUT_MS       100        // one READ or WRITE exchange
#define REMOVE_MISSES       4          // consecutive misses -> LT_TAG_REMOVED
#define PN532_ACTIVATION_RETRIES 2     // extra activation tries per poll, so "no tag" is answered at
                                       //   once. Never 0xFF (the PN532 default: search forever).
#define READER_RECOVER_FAILS 15        // consecutive missing/garbled PN532 answers -> reset + re-init
#define READER_RETRY_MS     3000       // PN532 not found -> look again this often
#define WRITE_BUDGET_MS     1800       // a write list stops (WRITE_FAIL) after this long: the screen
                                       //   gives up at 2 s (a full provisioning takes ~0.4 s)

// --- button ---
#ifndef BUTTON_ACTIVE_HIGH
  #define BUTTON_ACTIVE_HIGH 0         // TUNE: 0 = button to GND (pull-up), 1 = button to 3V3 (pull-down)
#endif
#define BUTTON_DEBOUNCE_MS  30
#define BUTTON_STUCK_MS     60000      // held longer = stuck / miswired: report a release, then ignore
                                       //   it until it really lets go

// ------------------------------- board pins --------------------------------
//  To move a pin, #define it HERE (above the board block), e.g.
//      #define PIN_BUTTON 11
//
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3) || defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_NOPSRAM) || \
    defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT) || defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_REVTFT)
  #define BOARD_ID    1                // LinkHello.board: Feather ESP32-S3
  #define BOARD_NAME  "Feather ESP32-S3"
  #ifndef PIN_SDA
    #define PIN_SDA   SDA              // GP3 (STEMMA QT)
  #endif
  #ifndef PIN_SCL
    #define PIN_SCL   SCL              // GP4 (STEMMA QT)
  #endif
  #ifndef PIN_PN532_IRQ
    #define PIN_PN532_IRQ 6            // GP6 [D6]   (same as the Skull Oracle)
  #endif
  #ifndef PIN_PN532_RST
    #define PIN_PN532_RST 9            // GP9 [D9]
  #endif
  #ifndef PIN_RING
    #define PIN_RING  10               // GP10 [D10] (the skull's old eyes pin)
  #endif
  #ifndef PIN_BUTTON
    #define PIN_BUTTON 12              // GP12 [D12] (NOT GP13: that pin carries the red LED)
  #endif
  #define STATUS_IS_PIXEL 1
  #ifndef PIN_STATUS
    #define PIN_STATUS PIN_NEOPIXEL    // onboard NeoPixel (GPIO33)
  #endif
#else
  // AITRIP ESP32-WROOM-32 ("ESP32 Dev Module") -- also the default for other classic ESP32 boards
  #if !defined(CONFIG_IDF_TARGET_ESP32)
    #error "No pin map for this board: add one to the board block in crystal_plinth.ino"
  #endif
  #if defined(ARDUINO_ESP32_DEV)
    #define BOARD_ID  2                // LinkHello.board: ESP32 dev (AITRIP)
  #else
    #define BOARD_ID  0                // other
  #endif
  #define BOARD_NAME  "ESP32 dev (AITRIP)"
  #ifndef PIN_SDA
    #define PIN_SDA   21
  #endif
  #ifndef PIN_SCL
    #define PIN_SCL   22
  #endif
  #ifndef PIN_PN532_IRQ
    #define PIN_PN532_IRQ 16           // silk RX2 (same as nfc_tag_bench)
  #endif
  #ifndef PIN_PN532_RST
    #define PIN_PN532_RST 17           // silk TX2
  #endif
  #ifndef PIN_RING
    #define PIN_RING  4                // D4
  #endif
  #ifndef PIN_BUTTON
    #define PIN_BUTTON 23              // D23
  #endif
  #define STATUS_IS_PIXEL 0
  #ifndef PIN_STATUS
    #define PIN_STATUS 2               // onboard blue LED, active HIGH
  #endif
#endif

#if RING_LEDS < 1 || RING_LEDS > 255
  #error "RING_LEDS must be 1..255"
#endif
#if RING_MAX_BRIGHTNESS < 1 || RING_MAX_BRIGHTNESS > 255
  #error "RING_MAX_BRIGHTNESS must be 1..255"
#endif

// ================================= types ===================================
//  (all types live up here, before the first function: the Arduino builder
//   inserts its function prototypes just above the first function body)

struct RGBf { float r, g, b; };        // perceptual 0..1 per channel (before gamma)

struct TagId {                         // what InListPassiveTarget reports about a tag
  uint8_t uid[TAG_UID_MAX];
  uint8_t uidLen;
  uint8_t atqaHi, atqaLo;              // SENS_RES, 0x00 0x44 for NTAG
  uint8_t sak;                         // SEL_RES, 0x00 for NTAG / Ultralight
};

enum TagIo : uint8_t { IO_OK, IO_FAIL, IO_NO_TAG, IO_WRONG_TAG };

struct RingState {
  uint8_t  mode;                       // LedMode
  uint8_t  speed, level;
  uint8_t  bright;                     // per-command brightness cap (LT_LED), 0 = the screen's cap
  uint32_t color, color2;
  uint16_t ms;                         // as commanded (0 = until the next command)
  bool     timed;                      // falls back to the idle look at endAt
  bool     idleLook;                   // showing the idle look (HELLO_ACK / PING may restyle it)
  uint32_t endAt;
  uint32_t since;                      // millis() when this mode (re)started
  float    phase;                      // speed-scaled animation cycles since `since`
};

struct RxPacket {                      // one ESP-NOW packet, copied out of the WiFi task
  uint8_t mac[6];
  uint8_t len;
  uint8_t data[ESP_NOW_MAX_DATA_LEN];
};

struct CachedResult {                  // last write results, for idempotent resends
  bool     used;
  uint16_t reqId;
  uint32_t hash;                       // of the request body: a reused reqId with new content misses
  uint8_t  status, failedPage, written;
};

// =============================== globals ===================================
Adafruit_PN532    nfc(PIN_PN532_IRQ, PIN_PN532_RST, &Wire);   // I2C mode, address 0x24
#if PLINTH_HAS_RING
Adafruit_NeoPixel ring(RING_LEDS, PIN_RING, RING_TYPE);
#endif

static const uint8_t BCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const char *const MODE_NAMES[] = { "OFF", "IDLE", "FLARE", "CHARGING", "REVEAL",
                                          "SLEEPY", "RAINBOW", "SOLID", "METER", "DUAL" };
static const uint8_t LED_MODE_LAST = LED_DUAL;   // highest LedMode this firmware draws; anything above is ignored
static_assert(sizeof(MODE_NAMES) / sizeof(MODE_NAMES[0]) == LED_MODE_LAST + 1, "a name for every LedMode");
static const float kTau = 6.2831853f;
static const RGBf  kWhite = { 1.0f, 1.0f, 1.0f };

// --- reader ---
static bool     readerOk = false;
static uint32_t readerFw = 0;          // GetFirmwareVersion: IC, Ver, Rev, Support
static uint8_t  pnTg = 1;              // logical target number from the last InListPassiveTarget
static uint16_t pnFails = 0;           // consecutive polls without a valid PN532 answer
static uint16_t pnTimeouts = 0;        //   ... of which "no answer in time"
static uint16_t pollTimeoutMs = POLL_TIMEOUT_MS;   // grows if the PN532 turns out to answer slowly
static bool     tagPresent = false;
static TagId    curTag;
static uint8_t  missCount = 0;
static uint32_t lastPollMs = 0, lastReaderTryMs = 0;
static uint8_t  lastImage[TAG_NUM_PAGES][4];
static bool     lastImageOk = false, haveImage = false;
static uint32_t statPolls = 0, statPlaced = 0, statReadFails = 0, statResets = 0, statWrites = 0;

// --- write-result cache ---
static CachedResult wcache[4];
static uint8_t      wcacheNext = 0;

// --- link ---
static QueueHandle_t rxQ = nullptr;
static RxPacket rxScratch;             // receive-callback scratch (WiFi task; the callback is never re-entered)
static bool     espnowOk = false;
static bool     linked = false;        // the screen's MAC is our unicast peer
static bool     standalone = true;     // nothing from the screen for SCREEN_TIMEOUT_MS: the ring runs on its own
static uint8_t  screenMac[6];
static uint16_t screenFw = 0;
static uint32_t lastHeardMs = 0, lastHelloMs = 0, lastLinkTryMs = 0;
static uint16_t txSeq = 0;
static std::atomic<uint32_t> rxCount{0}, rxDropped{0}, txOk{0}, txFail{0};
static uint32_t txErr = 0, rxIgnored = 0;

// --- TAG_PLACED resend after a reported radio failure (the screen can't recover a lost one) ---
static LinkTagPlaced placedPkt;
static bool     placedWatch = false;
static uint8_t  placedResends = 0;
static uint32_t placedSentMs = 0, placedFailMark = 0;

// --- ring ---
static RingState rs;
static uint8_t  idleMode  = LED_IDLE;          // where timed effects fall back to
static uint32_t idleColor = STANDALONE_COLOR;
static uint8_t  screenCap = 255;               // brightness cap from HELLO_ACK / PING (255 until one arrives)
static bool     flareOn = false;
static uint32_t flareAt = 0;
static float    sparkle[RING_LEDS];            // 0..1 per pixel, decays
static uint32_t sparkleTint = SPARKLE_COLOR;
static float    meterShown = 0.0f;             // eased LED_METER fill 0..1
static uint32_t lastFrameMs = 0;
static uint8_t  testMode = LED_IDLE;           // serial 'm' command
static RGBf     lastBase[RING_LEDS];           // the base image shown last frame (after any crossfade)
static RGBf     fadeFrom[RING_LEDS];           // what a new steady look fades in from
static bool     fadeOn = false;
static uint32_t fadeAt = 0;

// --- button ---
static bool     btnRaw = false, btnDown = false, btnStuck = false;
static uint32_t btnChangeMs = 0, btnPressMs = 0;

// --- status light ---
static uint32_t statusShown = 0xFFFFFFFF;

// ============================== little helpers =============================
static void logLine(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logLine(const char *fmt, ...) {
#if DEBUG
  char buf[320];                       // the status line is ~260 characters
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  uint32_t ms = millis();
  Serial.printf("%5lu.%03lu  %s\n", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000), buf);
#else
  (void)fmt;
#endif
}

static const char *modeName(uint8_t m) { return m <= LED_MODE_LAST ? MODE_NAMES[m] : "?"; }

static void hexStr(const uint8_t *b, uint8_t n, char *out, size_t outLen) {
  size_t o = 0;
  if (outLen) out[0] = 0;
  for (uint8_t i = 0; i < n && o + 4 <= outLen; i++)
    o += snprintf(out + o, outLen - o, i ? ":%02X" : "%02X", b[i]);
}

static bool sameUid(const TagId &a, const TagId &b) {
  return a.uidLen == b.uidLen && memcmp(a.uid, b.uid, a.uidLen) == 0;
}

static inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
static inline float fracf(float x)   { return x - floorf(x); }
static inline RGBf  rgbOf(uint32_t c) {
  RGBf o = { ((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f };
  return o;
}
static inline RGBf scaleRgb(RGBf c, float k) { RGBf o = { c.r * k, c.g * k, c.b * k }; return o; }
static inline RGBf mixRgb(RGBf a, RGBf b, float t) {
  RGBf o = { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
  return o;
}
static RGBf hsvRgb(float h, float s, float v) {          // h 0..1 (wraps), s/v 0..1
  h = fracf(h) * 6.0f;
  int   sec = (int)h;
  float f = h - (float)sec;
  float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
  RGBf o;
  switch (sec) {
    case 0:  o = { v, t, p }; break;
    case 1:  o = { q, v, p }; break;
    case 2:  o = { p, v, t }; break;
    case 3:  o = { p, q, v }; break;
    case 4:  o = { t, p, v }; break;
    default: o = { v, p, q }; break;
  }
  return o;
}
// speed byte -> value: 0 = the mode's default, 1..255 = slow..fast
static float speedPick(uint8_t speed, float dflt, float slow, float fast) {
  if (!speed) return dflt;
  return slow + (fast - slow) * (float)(speed - 1) / 254.0f;
}

// ================================ LED ring =================================
//  Each frame: the current mode draws a base image in perceptual 0..1 RGB,
//  then two local overlays go on top (the placement flare and button
//  sparkles), then gamma -> brightness cap -> current limiter -> pixels.
//  The cap is applied AFTER gamma (it is a PWM/current ceiling); gamma is done
//  in float with the same 2.6 curve as Adafruit_NeoPixel::gamma32 so a capped
//  ring keeps its dim end instead of rounding it away.

static inline uint16_t physPixel(int i) {                // logical 0 = top, clockwise
#if RING_CCW
  return (uint16_t)((RING_TOP_PIXEL + RING_LEDS - (i % RING_LEDS)) % RING_LEDS);
#else
  return (uint16_t)((RING_TOP_PIXEL + i) % RING_LEDS);
#endif
}

static void ringSparkleBurst(int count, uint32_t tint) {
  sparkleTint = tint;
  for (int k = 0; k < count; k++) {
    int i = (int)random(RING_LEDS);
    float s = 0.55f + 0.45f * (float)random(1000) / 1000.0f;
    if (s > sparkle[i]) sparkle[i] = s;
  }
}

// Switch the ring to a mode. ms = 0: until the next command (FLARE always
// decays, over FLARE_MS by default); otherwise the idle look takes over when
// ms runs out. bright = this command's brightness cap (0 = the screen's cap).
// idleLook = this IS the idle look, so a new HELLO_ACK / PING may restyle it.
static void ringSetMode(uint8_t mode, uint32_t color, uint32_t color2, uint8_t speed, uint8_t level, uint16_t ms,
                        uint8_t bright, bool idleLook) {
  if (mode > LED_MODE_LAST) return;              // unknown (a newer screen): keep the current look
  color &= 0xFFFFFFu;
  color2 &= 0xFFFFFFu;
  const bool oneShot = mode == LED_FLARE || mode == LED_REVEAL;
  // The screen may repeat a command as a refresh (LINK-PROTOCOL.md §5): an
  // identical steady command changes nothing -- no restart, no new end time.
  // One-shots (FLARE / REVEAL) are only repeated on purpose, so they replay.
  if (!oneShot && mode == rs.mode && color == rs.color && color2 == rs.color2 && speed == rs.speed &&
      level == rs.level && ms == rs.ms && bright == rs.bright) {
    rs.idleLook = idleLook;
    return;
  }
  uint32_t now = millis();
  // the same continuous mode with new parameters (CHARGING with a new speed,
  // METER with a new level, IDLE in a new colour) keeps its phase: nothing jumps
  bool restart = (mode != rs.mode) || oneShot;
  // A new steady look (mode or colours) fades in from what the ring shows now;
  // FLARE / REVEAL hit at once, and a METER level change already slides.
  const bool newLook = mode != rs.mode || color != rs.color || color2 != rs.color2;
  if (RING_XFADE_MS > 0 && !oneShot && newLook) {
    memcpy(fadeFrom, lastBase, sizeof(fadeFrom));
    fadeOn = true;
    fadeAt = now;
  } else if (oneShot) {
    fadeOn = false;
  }
  if (mode == LED_METER && rs.mode != LED_METER) meterShown = 0.0f;
  rs.mode = mode;
  rs.color = color;
  rs.color2 = color2;
  rs.speed = speed;
  rs.level = level;
  rs.bright = bright;
  rs.ms = ms;
  rs.timed = (ms != 0) || mode == LED_FLARE;
  rs.idleLook = idleLook;
  rs.endAt = now + (ms ? ms : FLARE_MS);
  if (restart) { rs.since = now; rs.phase = 0.0f; }
  if (mode == LED_REVEAL)                                // sparkles in the colour, half-way to white
    ringSparkleBurst(RING_LEDS, ((rs.color & 0xFEFEFEu) >> 1) + 0x7F7F7Fu);
}

static void ringShowIdle() { ringSetMode(idleMode, idleColor, 0, 0, 0, 0, 0, true); }

static void ringFlare() { flareOn = true; flareAt = millis(); }
static void ringSparkle() { ringSparkleBurst(7, SPARKLE_COLOR); }

static float modeRate() {                                // animation cycles per second
  switch (rs.mode) {
    case LED_IDLE:     return 1.0f / speedPick(rs.speed, 4.0f, 8.0f, 1.2f);   // breaths
    case LED_CHARGING: return speedPick(rs.speed, 1.1f, 0.35f, 3.5f);          // laps
    case LED_SLEEPY:   return 1.0f / speedPick(rs.speed, 2.6f, 4.5f, 1.2f);   // blinks
    case LED_RAINBOW:  return 1.0f / speedPick(rs.speed, 9.0f, 20.0f, 2.5f);  // hue laps
    case LED_DUAL:     return 1.0f / speedPick(rs.speed, 3.0f, 6.0f, 1.2f);   // crossfade cycles
    case LED_REVEAL:   return 1.0f / 3.2f;                                     // settle breathe
    case LED_METER:    return 1.4f;                                            // "full" pulse
    default:           return 0.0f;
  }
}

static void renderBase(RGBf *px, uint32_t now) {
  const int   N  = RING_LEDS;
  const float t  = (float)(now - rs.since) / 1000.0f;    // seconds in this mode
  const float ph = rs.phase;
  const RGBf  c  = rgbOf(rs.color);
  const RGBf  off = { 0.0f, 0.0f, 0.0f };

  switch (rs.mode) {
    case LED_IDLE: {            // slow breathe, with a faint brighter patch wandering round
      float breath = 0.5f - 0.5f * cosf(kTau * ph);
      float lvl = 0.30f + 0.70f * breath;
      for (int i = 0; i < N; i++) {
        float wander = 0.80f + 0.20f * sinf(kTau * ((float)i / N - t / 7.0f));
        px[i] = scaleRgb(c, lvl * wander);
      }
      break;
    }
    case LED_FLARE: {           // instant full flash, white cooling to `color`, decays over ms
      float dur = (float)(rs.ms ? rs.ms : FLARE_MS) / 1000.0f;
      float x = clamp01(t / dur);
      float e = (1.0f - x) * (1.0f - x);
      RGBf col = mixRgb(kWhite, c, clamp01(x * 3.0f));
      for (int i = 0; i < N; i++) px[i] = scaleRgb(col, e);
      break;
    }
    case LED_CHARGING: {        // two comets swirling round, white-hot heads, gentle pulse
      // color2 != 0: the second comet is in color2 (a two-colour crystal), else both in color
      const RGBf c2 = rs.color2 ? rgbOf(rs.color2) : c;
      float head = fracf(ph) * N;
      float pulse = 0.88f + 0.12f * sinf(kTau * t * 2.0f);
      for (int i = 0; i < N; i++) {
        float v[2];
        for (int k = 0; k < 2; k++) {
          float d = fmodf(head + (float)k * (N * 0.5f) - (float)i + 2.0f * N, (float)N);  // px behind the head
          v[k] = (d > N - 1.0f) ? (d - (N - 1.0f))        // pixel just ahead: anti-aliased leading edge
                                : expf(-d / 3.2f);        // the tail
        }
        float best = v[0] > v[1] ? v[0] : v[1];
        float sum = v[0] + v[1];
        RGBf col = (rs.color2 && sum > 1e-4f) ? mixRgb(c, c2, v[1] / sum) : c;
        float hot = best * best * best * best;
        px[i] = scaleRgb(mixRgb(col, kWhite, 0.45f * hot), (0.16f + 0.84f * best) * pulse);
      }
      break;
    }
    case LED_REVEAL: {          // white-hot burst, a ripple running both ways round, settle into color2
      RGBf settle = rgbOf(rs.color2 ? rs.color2 : rs.color);
      float rest = 0.62f + 0.14f * sinf(kTau * ph);
      if (t < 0.12f) {
        RGBf hot = mixRgb(c, kWhite, 0.55f);
        for (int i = 0; i < N; i++) px[i] = hot;
      } else if (t < 1.1f) {
        float x = (t - 0.12f) / 0.98f;
        float burst = (1.0f - x) * (1.0f - x);
        float front = x * (N * 0.5f);
        RGBf from = mixRgb(c, kWhite, 0.3f);
        for (int i = 0; i < N; i++) {
          float a = (float)(i <= N / 2 ? i : N - i);     // pixels from the top
          float ripple = expf(-(a - front) * (a - front) / 2.5f) * (1.0f - x);
          RGBf base = mixRgb(scaleRgb(settle, rest), from, burst);
          px[i] = mixRgb(base, kWhite, 0.6f * ripple);
        }
      } else {
        for (int i = 0; i < N; i++) px[i] = scaleRgb(settle, rest);
      }
      break;
    }
    case LED_SLEEPY: {          // slow, dim, drowsy blink with a dark pause
      float x = fracf(ph) / 0.7f;
      float v = 0.0f;
      if (x < 1.0f) { v = sinf(0.5f * kTau * x); v = v * v; }
      float lvl = 0.06f + 0.42f * v;
      for (int i = 0; i < N; i++) px[i] = scaleRgb(c, lvl);
      break;
    }
    case LED_RAINBOW: {         // gentle rainbow drifting round the ring
      for (int i = 0; i < N; i++) px[i] = hsvRgb((float)i / N + ph, 0.80f, 0.85f);
      break;
    }
    case LED_SOLID: {
      for (int i = 0; i < N; i++) px[i] = c;
      break;
    }
    case LED_DUAL: {            // two colours (a gradient crystal): one soft lobe of each round the
                                //   ring, every pixel crossfading color -> color2 -> color (~3 s)
      const RGBf c2 = rgbOf(rs.color2 ? rs.color2 : rs.color);
      for (int i = 0; i < N; i++) {
        float w = 0.5f - 0.5f * cosf(kTau * (ph + (float)i / N));
        px[i] = mixRgb(c, c2, w);
      }
      break;
    }
    case LED_METER: {           // fill level/255 clockwise from the top; empty slots glow faintly
      float lit = meterShown * N;
      bool full = rs.level >= 255 && meterShown > 0.995f;
      float pulse = full ? (0.85f + 0.15f * sinf(kTau * ph)) : 1.0f;
      int edge = (int)lit;
      for (int i = 0; i < N; i++) {
        float f = clamp01(lit - (float)i);
        float lvl = 0.16f + 0.84f * f;
        if (i == edge && f > 0.0f && f < 1.0f) lvl += 0.15f * (0.5f + 0.5f * sinf(kTau * t * 3.0f));
        px[i] = scaleRgb(c, clamp01(lvl) * pulse);
      }
      break;
    }
    default:                    // LED_OFF
      for (int i = 0; i < N; i++) px[i] = off;
      break;
  }
}

static void ringOverlays(RGBf *px, uint32_t now, float dt) {
  // the plinth's own placement flare: an instant white burst that cools to
  // violet and fades into whatever the ring was doing underneath
  if (flareOn) {
    float x = (float)(now - flareAt) / (float)FLARE_MS;
    if (x >= 1.0f) {
      flareOn = false;
    } else {
      float e = (1.0f - x) * (1.0f - x);
      RGBf col = mixRgb(kWhite, rgbOf(FLARE_COLOR), clamp01(x * 2.5f));
      for (int i = 0; i < RING_LEDS; i++) px[i] = mixRgb(px[i], col, e);
    }
  }
  // sparkles (button press, REVEAL burst): added on top, decaying
  float decay = expf(-dt / 0.16f);
  RGBf tint = rgbOf(sparkleTint);
  for (int i = 0; i < RING_LEDS; i++) {
    float s = sparkle[i];
    if (s < 0.004f) { sparkle[i] = 0.0f; continue; }
    px[i].r = clamp01(px[i].r + tint.r * s);
    px[i].g = clamp01(px[i].g + tint.g * s);
    px[i].b = clamp01(px[i].b + tint.b * s);
    sparkle[i] = s * decay;
  }
}

static uint8_t ringCap() {                               // the brightness ceiling right now
  uint8_t c = rs.bright ? rs.bright : screenCap;
  return c < RING_MAX_BRIGHTNESS ? c : (uint8_t)RING_MAX_BRIGHTNESS;
}

static void ringOutput(const RGBf *px) {
#if !PLINTH_HAS_RING
  (void)px;                            // no ring fitted: nothing to drive
#else
  const float cap = (float)ringCap();
  float lin[RING_LEDS][3];
  float sum = 0.0f;
  for (int i = 0; i < RING_LEDS; i++) {
    lin[i][0] = powf(clamp01(px[i].r), RING_GAMMA) * cap;
    lin[i][1] = powf(clamp01(px[i].g), RING_GAMMA) * cap;
    lin[i][2] = powf(clamp01(px[i].b), RING_GAMMA) * cap;
    sum += lin[i][0] + lin[i][1] + lin[i][2];
  }
#if RING_MAX_MA > 0
  // estimated draw: ~20 mA per channel at full PWM + ~1 mA idle per pixel
  float ma = sum / 255.0f * (float)RING_MA_PER_CHANNEL + (float)RING_LEDS;
  if (ma > (float)RING_MAX_MA && ma > (float)RING_LEDS) {
    float k = ((float)RING_MAX_MA - (float)RING_LEDS) / (ma - (float)RING_LEDS);
    if (k < 0.0f) k = 0.0f;
    for (int i = 0; i < RING_LEDS; i++) { lin[i][0] *= k; lin[i][1] *= k; lin[i][2] *= k; }
  }
#endif
  for (int i = 0; i < RING_LEDS; i++)
    ring.setPixelColor(physPixel(i), (uint8_t)(lin[i][0] + 0.5f), (uint8_t)(lin[i][1] + 0.5f),
                       (uint8_t)(lin[i][2] + 0.5f));
  ring.show();
#endif
}

static void ringRender(uint32_t now) {
  float dt = (float)(now - lastFrameMs) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  lastFrameMs = now;

  if (rs.timed && (int32_t)(now - rs.endAt) >= 0)        // timed effect over -> the idle look
    ringShowIdle();

  rs.phase += dt * modeRate();
  if (rs.phase > 1000.0f) rs.phase -= 1000.0f;           // keep float precision (all rates are periodic)
  if (rs.mode == LED_METER) {
    float target = rs.level / 255.0f;
    meterShown += (target - meterShown) * (1.0f - expf(-dt / 0.12f));
  }

  RGBf px[RING_LEDS];
  renderBase(px, now);
  if (fadeOn) {                                          // a new steady look fading in
    float x = (float)(now - fadeAt) / (float)(RING_XFADE_MS > 0 ? RING_XFADE_MS : 1);
    if (x >= 1.0f) {
      fadeOn = false;
    } else {
      float e = x * x * (3.0f - 2.0f * x);               // smoothstep
      for (int i = 0; i < RING_LEDS; i++) px[i] = mixRgb(fadeFrom[i], px[i], e);
    }
  }
  memcpy(lastBase, px, sizeof(lastBase));
  ringOverlays(px, now, dt);
  ringOutput(px);
}

static void ringService(uint32_t now) {
  if (!PLINTH_HAS_RING) return;        // no ring: LED commands only update rs (logged), nothing is drawn
  if (now - lastFrameMs >= 1000u / RING_FPS) ringRender(now);
}

static void ringBegin() {
#if PLINTH_HAS_RING
  ring.begin();
  ring.clear();
  ring.show();
#endif
  memset(sparkle, 0, sizeof(sparkle));
  memset(lastBase, 0, sizeof(lastBase));   // dark: the first look fades in
  memset(&rs, 0, sizeof(rs));          // LED_OFF
  ringShowIdle();                      // standalone violet breathe until the screen answers
  lastFrameMs = millis();
}

// ============================ status light =================================
static void statusBegin() {
#if STATUS_IS_PIXEL
  #if defined(NEOPIXEL_POWER)
  pinMode(NEOPIXEL_POWER, OUTPUT);
    #if defined(NEOPIXEL_POWER_ON)
  digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);
    #else
  digitalWrite(NEOPIXEL_POWER, HIGH);
    #endif
  #endif
  rgbLedWrite(PIN_STATUS, 0, 0, 0);    // core RMT driver: its own channel, never fights the ring's
#else
  pinMode(PIN_STATUS, OUTPUT);
  digitalWrite(PIN_STATUS, LOW);
#endif
}

static void statusService(uint32_t now) {
#if STATUS_IS_PIXEL
  uint32_t c;
  if (!readerOk)       c = ((now / 100) & 1) ? 0x300000 : 0;        // red, fast blink
  else if (tagPresent) c = 0x18082A;                                 // violet
  else if (standalone) c = (now % 1000 < 120) ? 0x000030 : 0;        // blue blink: looking for the screen
  else                 c = 0x001400;                                 // green: linked
  if (c != statusShown) {
    statusShown = c;
    rgbLedWrite(PIN_STATUS, (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
  }
#else
  // The AITRIP's blue LED (GPIO2) glows through the closed v7 plinth: the link at a glance.
  bool on;
  if (!readerOk)       on = (now / 100) & 1;               // fast blink (5 Hz): PN532 missing
  else if (standalone) on = (now % 1000) < 500;            // slow blink (1 Hz): searching for the screen
  else if (tagPresent) on = (now % 500) >= 70;             // linked + a crystal on it: a flicker every 0.5 s
  else                 on = true;                          // steady: linked to the screen
  if ((uint32_t)on != statusShown) {
    statusShown = on;
    digitalWrite(PIN_STATUS, on ? HIGH : LOW);
  }
#endif
}

// =============================== ESP-NOW link ==============================
//  The receive callback runs in the WiFi task: it only copies the packet into
//  a queue; loop() does the real work.

static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!rxQ || !info || !info->src_addr || !data) return;
  if (len < (int)sizeof(LinkHeader) || len > ESP_NOW_MAX_DATA_LEN) return;
  if (data[0] != LINK_MAGIC0 || data[1] != LINK_MAGIC1) return;      // not Crystal Oracle traffic
  RxPacket &p = rxScratch;
  memcpy(p.mac, info->src_addr, 6);
  p.len = (uint8_t)len;
  memcpy(p.data, data, (size_t)len);
  rxCount++;
  if (xQueueSend(rxQ, &p, 0) != pdTRUE) rxDropped++;
}

// Delivery report from the radio. Broadcasts always "succeed"; a unicast fails
// when the screen's radio didn't acknowledge it (after the MAC-level retries).
static void onSent(const esp_now_send_info_t *info, esp_now_send_status_t status) {
  (void)info;
  if (status == ESP_NOW_SEND_SUCCESS) txOk++;
  else txFail++;
}

static bool addPeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;                    // 0 = the channel we're on (LINK_CHANNEL)
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  esp_err_t e = esp_now_add_peer(&peer);
  if (e != ESP_OK) logLine("link: add peer failed (%s)", esp_err_to_name(e));
  return e == ESP_OK;
}

// Same radio set-up as the screen (reader.cpp beginPlinth): STA only, never
// connects, never scans, power save off, fixed channel, config kept in RAM.
static void linkBegin() {
  if (!rxQ) rxQ = xQueueCreate(RX_QUEUE_LEN, sizeof(RxPacket));
  if (!rxQ) { logLine("link: no memory for the receive queue"); return; }
  WiFi.persistent(false);              // WiFi config in RAM only: no NVS (flash) writes
  WiFi.setSleep(false);                // stored now, applied by the core when STA starts
  if (!WiFi.mode(WIFI_STA)) { logLine("link: WiFi STA start failed - retrying in 5 s"); return; }
  WiFi.disconnect();                   // never join an AP (that would move the radio off our channel)
  esp_err_t e = esp_wifi_set_ps(WIFI_PS_NONE);   // radio always listening: ESP-NOW answers at once
  if (e != ESP_OK) logLine("link: esp_wifi_set_ps: %s", esp_err_to_name(e));
  e = esp_wifi_set_channel(LINK_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (e != ESP_OK) logLine("link: set channel %d failed (%s)", LINK_CHANNEL, esp_err_to_name(e));
  e = esp_now_init();
  if (e != ESP_OK) { logLine("link: esp_now_init failed (%s) - retrying in 5 s", esp_err_to_name(e)); return; }
  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);
  if (!addPeer(BCAST)) { esp_now_deinit(); return; }
  espnowOk = true;
}

// Unicast to the screen once we know it, broadcast before. Every packet is
// sent with its full struct size (the screen checks len >= sizeof).
static bool linkSend(const void *pkt, size_t len) {
  if (!espnowOk) return false;
  const uint8_t *dst = linked ? screenMac : BCAST;
  esp_err_t e = esp_now_send(dst, (const uint8_t *)pkt, len);
  if (e != ESP_OK) {
    if (++txErr <= 5 || txErr % 100 == 0) logLine("link: esp_now_send failed (%s)", esp_err_to_name(e));
    return false;
  }
  return true;
}

// TAG_PLACED is the one packet the screen can't recover if it is lost (a lost
// REMOVED is repaired from HELLO.tagPresent): if the radio reports a failed
// unicast within PLACED_WATCH_MS, linkService() sends it again.
static void sendPlaced() {
  placedPkt.h.seq = txSeq++;
  placedFailMark = txFail.load();
  const bool queued = linkSend(&placedPkt, sizeof(placedPkt));
  placedWatch = linked;                // broadcasts get no delivery report
  placedSentMs = millis();
  if (!queued && linked) placedFailMark--;   // not even queued: treat as a failure (resend)
}

static void sendHello() {
  LinkHello m;
  memset(&m, 0, sizeof(m));
  linkFillHeader(m.h, LT_HELLO, txSeq++, LINK_GROUP);
  m.readerOk   = readerOk ? 1 : 0;
  m.buttonDown = (PLINTH_HAS_BUTTON && btnDown && !btnStuck) ? 1 : 0;   // always 0 without a button
  m.tagPresent = tagPresent ? 1 : 0;
  m.board      = BOARD_ID;
  m.fwVersion  = FW_VERSION;
  m.uptimeMs   = millis();
  linkSend(&m, sizeof(m));
}

static void sendButton(bool pressed, uint32_t heldMs) {
  LinkButton m;
  memset(&m, 0, sizeof(m));
  linkFillHeader(m.h, LT_BUTTON, txSeq++, LINK_GROUP);
  m.button  = 0;
  m.pressed = pressed ? 1 : 0;
  m.heldMs  = (uint16_t)(heldMs > 65535u ? 65535u : heldMs);
  linkSend(&m, sizeof(m));
}

static void sendRemoved(const TagId &t) {
  LinkTagRemoved m;
  memset(&m, 0, sizeof(m));
  linkFillHeader(m.h, LT_TAG_REMOVED, txSeq++, LINK_GROUP);
  memcpy(m.uid, t.uid, t.uidLen);
  m.uidLen = t.uidLen;
  linkSend(&m, sizeof(m));
}

static void sendWriteResult(uint16_t reqId, uint8_t status, uint8_t failedPage, uint8_t written) {
  LinkWriteResult m;
  memset(&m, 0, sizeof(m));
  linkFillHeader(m.h, LT_WRITE_RESULT, txSeq++, LINK_GROUP);
  m.reqId = reqId;
  m.status = status;
  m.failedPage = failedPage;
  m.written = written;
  linkSend(&m, sizeof(m));
}

// The screen's MAC becomes our unicast peer. Any screen->plinth packet in our
// group counts, not just HELLO_ACK: the screen only ever sends unicast to the
// plinth it adopted, so a PING / LED / WRITE_REQ reaching us proves it knows us
// (covers a lost HELLO_ACK). There is one screen per group.
static void adoptScreen(const uint8_t *mac) {
  if (linked && memcmp(mac, screenMac, 6) == 0) return;
  if (!addPeer(mac)) return;           // keep broadcasting; the screen ACKs every broadcast HELLO
  if (linked) esp_now_del_peer(screenMac);
  memcpy(screenMac, mac, 6);
  linked = true;
  char s[24];
  hexStr(mac, 6, s, sizeof(s));
  logLine("link: screen %s - unicast from now on", s);
}

// Nothing from the screen for SCREEN_TIMEOUT_MS: the ring runs its own look
// (violet breathe, local flare, sleepy blink) and HELLO goes back to broadcast
// until the screen answers again. The brightness cap from the screen is kept
// (it is the owner's setting).
static void enterStandalone() {
  if (linked) {
    esp_now_del_peer(screenMac);
    linked = false;
  }
  placedWatch = false;
  if (standalone) return;
  standalone = true;
  idleMode = LED_IDLE;
  idleColor = STANDALONE_COLOR;
  ringShowIdle();
  logLine("link: nothing from the screen for %d s - standalone ring, broadcasting HELLO again",
          SCREEN_TIMEOUT_MS / 1000);
}

// The idle look rides on every HELLO_ACK and PING (LINK-PROTOCOL.md §3). If the
// ring is showing the idle look it follows at once; a commanded mode stays.
static void applyIdleConfig(const LinkHelloAck &a, bool wasStandalone) {
  uint8_t im = a.idleMode;
  if (im > LED_MODE_LAST || im == LED_FLARE || im == LED_REVEAL) im = LED_IDLE;   // unknown / one-shots can't be "idle"
  uint32_t ic = a.idleColor & 0xFFFFFFu;
  if (ic == 0 && im != LED_OFF) ic = STANDALONE_COLOR;                       // 0 = not set
  const bool changed = im != idleMode || ic != idleColor || a.brightness != screenCap || a.screenFw != screenFw;
  idleMode = im;
  idleColor = ic;
  screenCap = a.brightness;            // 0..255; 0 really means "ring dark" (config ring_brightness = 0)
  screenFw = a.screenFw;
  if (rs.idleLook || wasStandalone) ringShowIdle();
  if (changed || wasStandalone)
    logLine("link: idle look from the screen (fw %u): %s #%06lX, brightness cap %u (plinth max %u)",
            (unsigned)screenFw, modeName(idleMode), (unsigned long)idleColor, (unsigned)screenCap,
            (unsigned)RING_MAX_BRIGHTNESS);
}

// ================================ PN532 ====================================
//  Tag I/O goes through Adafruit_PN532's public sendCommandCheckAck() (writes
//  the frame, waits for the PN532's ACK, then for "response ready", all with a
//  timeout) and reads + validates the response frame here. The library's own
//  helpers are not used for tag I/O because (Adafruit PN532 1.3.4):
//    * inDataExchange() addresses target `_inListedTag`, which only
//      inListPassiveTarget() sets -- readPassiveTargetID() never does, so after
//      a normal poll it would address target 0 instead of 1;
//    * readPassiveTargetID() hides ATQA/SAK and copies NFCIDLength bytes into
//      the caller's buffer with no bound check (a garbled frame = overflow);
//    * mifareultralight_WritePage() never looks at the status byte, so a
//      failed write reports success (no retry would ever happen);
//    * none of them check the frame checksums.

#define PN_SELECT_MAX 64               // InListPassiveTarget payload we accept (NTAG, 7-byte UID = 13
                                       //   bytes; a phone or bank card adds its ATS, typically < 30)
#define PN_TIMEOUT   (-1)              // pnCommand(): no ACK / no answer in time
#define PN_BADFRAME  (-2)              // pnCommand(): an answer, but not a valid frame for this command

// Host -> PN532 ACK frame = "abort the command you are running" (PN532 user
// manual; libnfc does the same after a timeout). Ignored by an idle PN532.
static void pnAbort() {
  static const uint8_t ack[6] = { 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00 };
  Wire.beginTransmission((uint8_t)PN532_I2C_ADDRESS);
  Wire.write(ack, sizeof(ack));
  Wire.endTransmission();
  delay(1);
}

// Send one command and return its validated response payload (the bytes after
// D5 <cmd+1>). Returns the payload length, PN_TIMEOUT if the PN532 didn't ACK or
// didn't answer within timeoutMs, or PN_BADFRAME for a corrupt / unexpected frame.
// In Adafruit PN532 1.3.4 (I2C), sendCommandCheckAck() writes the frame, polls
// the RDY byte (10 ms steps, up to timeoutMs), reads + checks the 6-byte ACK,
// then polls RDY again until the RESPONSE is ready -- so the read below gets a
// complete response frame.
static int pnCommand(uint8_t *cmd, uint8_t cmdLen, uint8_t *out, uint8_t outMax, uint16_t timeoutMs) {
  if (!nfc.sendCommandCheckAck(cmd, cmdLen, timeoutMs)) {
    pnAbort();                         // it may still be busy with this command: make it stop
    return PN_TIMEOUT;
  }

  // I2C read = RDY byte (0x01) + frame:
  //   00 00 FF LEN LCS | D5 <cmd+1> payload... | DCS 00      (LEN counts D5..payload)
  uint8_t raw[1 + 7 + PN_SELECT_MAX + 2];
  size_t want = 1 + 7 + (size_t)outMax + 2;
  if (want > sizeof(raw)) want = sizeof(raw);
  size_t got = Wire.requestFrom((uint8_t)PN532_I2C_ADDRESS, want);
  size_t n = 0;
  while (Wire.available() > 0) {
    int b = Wire.read();
    if (n < sizeof(raw)) raw[n++] = (uint8_t)b;
  }
  if (got < 1 + 9 || n < 1 + 9) return PN_BADFRAME;
  if (raw[0] != 0x01) return PN_BADFRAME;                // RDY lost (shouldn't happen)
  const uint8_t *f = raw + 1;
  size_t flen = n - 1;
  if (f[0] != PN532_PREAMBLE || f[1] != PN532_STARTCODE1 || f[2] != PN532_STARTCODE2) return PN_BADFRAME;
  uint8_t len = f[3];
  if ((uint8_t)(len + f[4]) != 0 || len < 2) return PN_BADFRAME;   // length checksum (also: error frame)
  if (6u + len > flen) return PN_BADFRAME;               // frame (through DCS) longer than we read
  if (f[5] != PN532_PN532TOHOST || f[6] != (uint8_t)(cmd[0] + 1)) return PN_BADFRAME;
  uint8_t sum = 0;
  for (uint8_t i = 0; i <= len; i++) sum = (uint8_t)(sum + f[5 + i]);   // D5..payload..DCS
  if (sum != 0) return PN_BADFRAME;
  uint8_t plen = (uint8_t)(len - 2);
  if (plen > outMax) return PN_BADFRAME;
  memcpy(out, f + 7, plen);
  return plen;
}

// RFConfiguration item 5 (MaxRetries): limit passive activation tries so a
// poll with no tag gets a prompt "0 targets" answer instead of searching forever.
static bool pnConfigure() {
  uint8_t cmd[5] = { PN532_COMMAND_RFCONFIGURATION, 0x05, 0xFF, 0x01, PN532_ACTIVATION_RETRIES };
  uint8_t resp[4];
  return pnCommand(cmd, sizeof(cmd), resp, sizeof(resp), 100) >= 0;
}

// InListPassiveTarget, 1 target, 106 kbps type A.
// 1 = tag (t filled in), 0 = the PN532 answered "no tag", PN_TIMEOUT / PN_BADFRAME.
static int pnSelect(TagId &t, uint16_t timeoutMs) {
  uint8_t cmd[3] = { PN532_COMMAND_INLISTPASSIVETARGET, 1, PN532_MIFARE_ISO14443A };
  uint8_t p[PN_SELECT_MAX];
  int n = pnCommand(cmd, sizeof(cmd), p, sizeof(p), timeoutMs);
  if (n < 0) return n;
  if (n < 1) return PN_BADFRAME;
  if (p[0] == 0) return 0;                               // NbTg = 0
  // NbTg, Tg, SENS_RES(2), SEL_RES, NFCIDLength, NFCID... [ATS]
  if (n < 6) return PN_BADFRAME;
  uint8_t idLen = p[5];
  if (idLen == 0 || idLen > TAG_UID_MAX || 6 + idLen > n) return PN_BADFRAME;
  pnTg = p[1] ? p[1] : 1;
  t.atqaHi = p[2];
  t.atqaLo = p[3];
  t.sak    = p[4];
  t.uidLen = idLen;
  memset(t.uid, 0, sizeof(t.uid));
  memcpy(t.uid, p + 6, idLen);
  return 1;
}

// One NTAG/Ultralight READ (0x30): 16 bytes = pages page..page+3 (wraps at the end).
// InDataExchange answer payload = Status + DataIn: Status bits 0..5 = error code
// (0 = ok; 0x01 = the tag didn't answer, 0x02 = CRC, ...), bit 6 = MI, bit 7 = NAD.
// A good READ is therefore exactly 17 bytes: 0x00 + 16 data bytes.
static bool pnReadChunk(uint8_t page, uint8_t out[16]) {
  uint8_t cmd[4] = { PN532_COMMAND_INDATAEXCHANGE, pnTg, MIFARE_CMD_READ, page };
  uint8_t p[17];
  int n = pnCommand(cmd, sizeof(cmd), p, sizeof(p), IO_TIMEOUT_MS);
  if (n != 17 || (p[0] & 0x3F) != 0) return false;       // status: tag timeout / NAK / CRC
  memcpy(out, p + 1, 16);
  return true;
}

// One Ultralight/NTAG WRITE (0xA2) of a 4-byte page.
static bool pnWritePage(uint8_t page, const uint8_t data[4]) {
  uint8_t cmd[8] = { PN532_COMMAND_INDATAEXCHANGE, pnTg, MIFARE_ULTRALIGHT_CMD_WRITE, page,
                     data[0], data[1], data[2], data[3] };
  uint8_t p[4];
  int n = pnCommand(cmd, sizeof(cmd), p, sizeof(p), IO_TIMEOUT_MS);
  return n >= 1 && (p[0] & 0x3F) == 0;
}

// Reset + wake the PN532 and set it up. tries = GetFirmwareVersion attempts.
static bool readerInit(int tries) {
  nfc.begin();                         // RST pulse + SAMConfig (I2C wake-up)
  uint32_t fw = 0;
  for (int i = 0; i < tries && !fw; i++) {
    fw = nfc.getFirmwareVersion();
    if (!fw && i + 1 < tries) delay(100);
  }
  if (!fw) { readerOk = false; return false; }
  readerFw = fw;
  bool sam = nfc.SAMConfig();
  bool cfg = pnConfigure();
  if (!sam || !cfg) logLine("nfc: PN532 set-up incomplete (SAMConfig %s, RFConfig %s)",
                         sam ? "ok" : "FAILED", cfg ? "ok" : "FAILED");
  readerOk = true;
  pnFails = 0;
  return true;
}

static void pump();                    // keeps ring + button alive during long tag jobs

// Re-select the tag and make sure it is the one we expect. With two tags in
// the field the one selected last ignores the next REQA, so another tag can win
// one select and ours the next: only WRONG_TAG if ours never answered.
static TagIo reselect(const TagId &want) {
  TagId t;
  bool other = false;
  for (int i = 0; i < 2; i++) {
    int rc = pnSelect(t, SELECT_TIMEOUT_MS);
    if (rc == 1) {
      if (sameUid(t, want)) return IO_OK;
      other = true;
    }
    pump();
  }
  return other ? IO_WRONG_TAG : IO_NO_TAG;
}

// READ with up to 3 tries; between tries the tag is re-selected (a failed
// command drops an NTAG back to IDLE). Fails fast if the tag left or changed.
static TagIo readChunkRetry(const TagId &t, uint8_t page, uint8_t out[16]) {
  for (int attempt = 0; attempt < 3; attempt++) {
    if (attempt > 0) {
      TagIo s = reselect(t);
      if (s != IO_OK) return s;
    }
    bool ok = pnReadChunk(page, out);
    pump();
    if (ok) return IO_OK;
  }
  return IO_FAIL;
}

static TagIo writePageRetry(const TagId &t, uint8_t page, const uint8_t data[4]) {
  for (int attempt = 0; attempt < 3; attempt++) {
    if (attempt > 0) {
      TagIo s = reselect(t);
      if (s != IO_OK) return s;
    }
    bool ok = pnWritePage(page, data);
    pump();
    if (ok) return IO_OK;
  }
  return IO_FAIL;
}

// Read pages 3..39 into pages[][4] (zeros where a read failed): ten 16-byte
// READs starting at 3, 7, ... 39 (the last one returns 39..42; 40+ are dropped).
// One extra sweep for chunks that failed while the tag stayed put.
static bool readImage(const TagId &t, uint8_t (*pages)[4]) {
  const uint8_t CHUNKS = (TAG_NUM_PAGES + 3) / 4;        // 10
  bool have[CHUNKS];
  memset(have, 0, sizeof(have));
  memset(pages, 0, TAG_NUM_PAGES * 4);
  for (int sweep = 0; sweep < 2; sweep++) {
    bool missing = false;
    for (uint8_t c = 0; c < CHUNKS; c++) {
      if (have[c]) continue;
      uint8_t start = (uint8_t)(TAG_FIRST_PAGE + 4 * c);
      uint8_t buf[16];
      TagIo s = readChunkRetry(t, start, buf);
      if (s == IO_NO_TAG || s == IO_WRONG_TAG) return false;   // gone: stop at once
      if (s != IO_OK) { missing = true; continue; }
      have[c] = true;
      for (uint8_t k = 0; k < 4; k++) {
        uint8_t pg = (uint8_t)(start + k);
        if (pg > TAG_LAST_PAGE) break;
        memcpy(pages[pg - TAG_FIRST_PAGE], buf + 4 * k, 4);
      }
    }
    if (!missing) return true;
    if (sweep == 0) {                                     // short breather, then retry the gaps
      uint32_t t0 = millis();
      while (millis() - t0 < 100) { pump(); delay(5); }
      if (reselect(t) != IO_OK) return false;
    }
  }
  return false;
}

// A tag arrived (new UID, or the same one after a removal).
static void onPlaced(const TagId &t) {
  ringFlare();                         // INSTANT local flare, before anything else (with a ring)
  if (PLINTH_HAS_RING) ringRender(millis());
  tagPresent = true;
  curTag = t;
  missCount = 0;
  statPlaced++;

  uint32_t t0 = millis();
  LinkTagPlaced &m = placedPkt;
  memset(&m, 0, sizeof(m));
  bool ok = readImage(t, m.pages);
  const uint32_t readMs = millis() - t0;   // incl. retries: a weak crystal / thick lid reads slower
  linkFillHeader(m.h, LT_TAG_PLACED, 0, LINK_GROUP);
  memcpy(m.uid, t.uid, t.uidLen);
  m.uidLen = t.uidLen;
  m.readOk = ok ? 1 : 0;
  m.sak    = t.sak;
  m.atqaLo = t.atqaLo;
  m.readMs = (uint16_t)(readMs > 65535u ? 65535u : (readMs ? readMs : 1u));
  placedResends = 0;
  sendPlaced();                        // the full 172-byte struct, unicast once linked

  memcpy(lastImage, m.pages, sizeof(lastImage));
  lastImageOk = ok;
  haveImage = true;
  if (!ok) statReadFails++;

  char u[TAG_UID_MAX * 3 + 1];
  hexStr(t.uid, t.uidLen, u, sizeof(u));
  const uint8_t *mk = lastImage[16 - TAG_FIRST_PAGE];
  logLine("tag: placed %s (ATQA %02X%02X SAK %02X) read %s in %lu ms | p16 %02X %02X %02X %02X%s",
       u, t.atqaHi, t.atqaLo, t.sak, ok ? "OK" : "INCOMPLETE", (unsigned long)readMs,
       mk[0], mk[1], mk[2], mk[3], linked ? "" : " (no screen: broadcast)");

  // without a screen nobody else will say "place it again"
  if (!ok && standalone) ringSetMode(LED_SLEEPY, STANDALONE_COLOR, 0, 0, 0, STANDALONE_SLEEPY_MS, 0, false);
}

static void nfcService(uint32_t now) {
  if (!readerOk) {
    if (now - lastReaderTryMs >= READER_RETRY_MS) {
      lastReaderTryMs = now;
      if (readerInit(1))
        logLine("nfc: PN532 found (firmware %lu.%lu)", (unsigned long)((readerFw >> 16) & 0xFF),
             (unsigned long)((readerFw >> 8) & 0xFF));
    }
    return;
  }
  if (now - lastPollMs < POLL_MS) return;
  lastPollMs = now;

  TagId t;
  int rc = pnSelect(t, pollTimeoutMs);
  statPolls++;
  if (rc < 0) {
    if (rc == PN_TIMEOUT) pnTimeouts++;
    if (++pnFails >= READER_RECOVER_FAILS) {
      const bool slow = pnTimeouts * 2 >= pnFails;      // mostly "no answer in time", not garbage
      pnFails = 0;
      pnTimeouts = 0;
      statResets++;
      logLine("nfc: PN532 stopped answering polls - resetting it");
      if (!readerInit(2)) {
        logLine("nfc: PN532 lost - will keep looking every %d s", READER_RETRY_MS / 1000);
        lastReaderTryMs = millis();
        if (tagPresent) {
          sendRemoved(curTag);
          tagPresent = false;
          missCount = 0;
        }
        return;
      }
      // it answers GetFirmwareVersion, so maybe its "no tag" answers are just
      // slower than we wait: give polls more time (up to 200 ms)
      if (slow && pollTimeoutMs < 200) {
        pollTimeoutMs = (uint16_t)(pollTimeoutMs * 2 > 200 ? 200 : pollTimeoutMs * 2);
        logLine("nfc: poll timeout raised to %u ms", (unsigned)pollTimeoutMs);
      }
    }
  } else {
    pnFails = 0;
    pnTimeouts = 0;
  }
  if (rc == PN_BADFRAME) return;       // a garbled answer says nothing about the tag: not a miss

  if (rc == 1) {
    if (tagPresent && !sameUid(t, curTag)) {
      // Another tag answered. Two tags in the field alternate poll by poll (the
      // one selected last ignores the next REQA), so this only counts as a miss
      // for the current tag; a real swap completes after REMOVE_MISSES polls.
      if (++missCount < REMOVE_MISSES) return;
      sendRemoved(curTag);
      char u[TAG_UID_MAX * 3 + 1];
      hexStr(curTag.uid, curTag.uidLen, u, sizeof(u));
      logLine("tag: removed %s (swapped)", u);
      tagPresent = false;
      missCount = 0;
      onPlaced(t);
      return;
    }
    missCount = 0;
    if (!tagPresent) onPlaced(t);
  } else if (tagPresent && ++missCount >= REMOVE_MISSES) {
    sendRemoved(curTag);
    tagPresent = false;
    missCount = 0;
    char u[TAG_UID_MAX * 3 + 1];
    hexStr(curTag.uid, curTag.uidLen, u, sizeof(u));
    logLine("tag: removed %s", u);
  }
}

// ============================== page writes ================================
static uint32_t fnv1a(uint32_t h, const uint8_t *b, size_t n) {
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
  return h;
}

static uint32_t writeReqHash(const LinkWriteReq &r) {
  uint8_t cnt = r.count > LINK_MAX_WRITES ? LINK_MAX_WRITES : r.count;
  uint8_t ul  = r.uidLen > TAG_UID_MAX ? TAG_UID_MAX : r.uidLen;
  uint32_t h = 2166136261u;
  h = fnv1a(h, &ul, 1);
  h = fnv1a(h, r.uid, ul);
  h = fnv1a(h, &cnt, 1);
  h = fnv1a(h, (const uint8_t *)r.writes, (size_t)cnt * sizeof(PageWrite));
  return h;
}

// Tag-I/O outcome -> WriteStatus (same mapping as the screen's rfid2 backend):
// the tag left -> NO_TAG, another tag answered -> WRONG_TAG, else `fail`.
static uint8_t ioStatus(TagIo s, uint8_t fail) {
  if (s == IO_NO_TAG) return WS_NO_TAG;
  if (s == IO_WRONG_TAG) return WS_WRONG_TAG;
  return fail;
}

// Execute an ordered write list (LINK-PROTOCOL.md §4). Returns a WriteStatus;
// fills failedPage and written (= pages actually written before it stopped).
static uint8_t executeWrites(const LinkWriteReq &req, int len, uint8_t &failedPage, uint8_t &written) {
  failedPage = 0;
  written = 0;
  // validate: count 1..18, every page 3..39, sane UID, packet long enough
  const int hdr = (int)offsetof(LinkWriteReq, writes);
  if (req.count == 0 || req.count > LINK_MAX_WRITES || len < hdr + (int)req.count * (int)sizeof(PageWrite))
    return WS_BAD_REQUEST;
  if (req.uidLen == 0 || req.uidLen > TAG_UID_MAX) return WS_BAD_REQUEST;
  for (uint8_t i = 0; i < req.count; i++) {
    uint8_t pg = req.writes[i].page;
    if (pg < TAG_FIRST_PAGE || pg > TAG_LAST_PAGE) { failedPage = pg; return WS_BAD_REQUEST; }
  }
  if (!readerOk) return WS_NO_TAG;

  const uint32_t t0 = millis();
  TagId want;
  memset(&want, 0, sizeof(want));
  memcpy(want.uid, req.uid, req.uidLen);
  want.uidLen = req.uidLen;

  // UID check: nothing is written unless the requested tag is the one on the reader
  TagIo s = reselect(want);
  if (s != IO_OK) return ioStatus(s, WS_NO_TAG);

  // 1. write, in order (the screen puts the marker page last = commit)
  bool skipped[LINK_MAX_WRITES];
  memset(skipped, 0, sizeof(skipped));
  for (uint8_t i = 0; i < req.count; i++) {
    const PageWrite &w = req.writes[i];
    if (millis() - t0 > WRITE_BUDGET_MS) { failedPage = w.page; return WS_WRITE_FAIL; }   // the screen has given up
    if (w.page == TAG_FIRST_PAGE) {    // NDEF CC: one-time-programmable bits -> only if byte 0 isn't 0xE1 yet
      uint8_t cur[16];
      s = readChunkRetry(want, TAG_FIRST_PAGE, cur);
      if (s != IO_OK) { failedPage = TAG_FIRST_PAGE; return ioStatus(s, WS_WRITE_FAIL); }   // can't see it: don't touch it
      if (cur[0] == 0xE1) { skipped[i] = true; continue; }                               // skipped, not an error
    }
    s = writePageRetry(want, w.page, w.data);
    if (s != IO_OK) { failedPage = w.page; return ioStatus(s, WS_WRITE_FAIL); }
    written++;
  }

  // 2. read every written page back (one 16-byte READ covers up to 4 pages)
  uint8_t chunk[16];
  int chunkStart = -100;
  for (uint8_t i = 0; i < req.count; i++) {
    if (skipped[i]) continue;
    const PageWrite &w = req.writes[i];
    bool later = false;                // the same page written again later: the last write wins
    for (uint8_t j = (uint8_t)(i + 1); j < req.count; j++)
      if (req.writes[j].page == w.page) { later = true; break; }
    if (later) continue;
    if (!(chunkStart <= w.page && w.page < chunkStart + 4)) {
      if (millis() - t0 > WRITE_BUDGET_MS) { failedPage = w.page; return WS_VERIFY_FAIL; }
      s = readChunkRetry(want, w.page, chunk);
      if (s != IO_OK) { failedPage = w.page; return ioStatus(s, WS_VERIFY_FAIL); }
      chunkStart = w.page;
    }
    if (memcmp(chunk + 4 * (w.page - chunkStart), w.data, 4) != 0) { failedPage = w.page; return WS_VERIFY_FAIL; }
  }

  // keep the serial 'd' dump current
  if (haveImage && sameUid(want, curTag)) {
    for (uint8_t i = 0; i < req.count; i++)
      if (!skipped[i]) memcpy(lastImage[req.writes[i].page - TAG_FIRST_PAGE], req.writes[i].data, 4);
  }
  return WS_OK;
}

static void handleWriteReq(const uint8_t *data, int len) {
  if (len < (int)offsetof(LinkWriteReq, writes)) {
    logLine("write: request too short (%d bytes) - ignored", len);
    return;
  }
  LinkWriteReq req;
  memset(&req, 0, sizeof(req));
  memcpy(&req, data, (size_t)len < sizeof(req) ? (size_t)len : sizeof(req));
  uint16_t reqId = req.reqId;
  uint32_t hash = writeReqHash(req);

  for (const CachedResult &c : wcache) {               // a resend: answer, don't write again
    if (c.used && c.reqId == reqId && c.hash == hash) {
      sendWriteResult(reqId, c.status, c.failedPage, c.written);
      logLine("write #%u: resend - answered from cache (status %u)", (unsigned)reqId, (unsigned)c.status);
      return;
    }
  }

  uint32_t t0 = millis();
  uint8_t failedPage = 0, written = 0;
  uint8_t status = executeWrites(req, len, failedPage, written);
  statWrites++;

  CachedResult &c = wcache[wcacheNext];
  wcacheNext = (uint8_t)((wcacheNext + 1) % (sizeof(wcache) / sizeof(wcache[0])));
  c.used = true;
  c.reqId = reqId;
  c.hash = hash;
  c.status = status;
  c.failedPage = failedPage;
  c.written = written;
  sendWriteResult(reqId, status, failedPage, written);

  static const char *const WS_NAMES[] = { "OK", "NO_TAG", "WRONG_TAG", "WRITE_FAIL", "VERIFY_FAIL", "BAD_REQUEST" };
  logLine("write #%u: %u page(s) -> %s (written %u, failed page %u) in %lu ms", (unsigned)reqId,
       (unsigned)req.count, status <= WS_BAD_REQUEST ? WS_NAMES[status] : "?", (unsigned)written,
       (unsigned)failedPage, (unsigned long)(millis() - t0));
}

// ============================ packet handling ==============================
static void handlePacket(const RxPacket &p) {
  const LinkHeader *h = (const LinkHeader *)p.data;
  if (!linkHeaderOk(p.data, p.len, LINK_GROUP)) {
    rxIgnored++;
    if (h->version != LINK_VERSION) {
      static uint32_t lastWarn = 0;
      if (millis() - lastWarn > 10000) {
        lastWarn = millis();
        logLine("link: ignoring packets with link version %u (this plinth speaks %u)",
             (unsigned)h->version, (unsigned)LINK_VERSION);
      }
    }
    return;                            // other group = another station: silently ignore
  }
  if (!(h->type & 0x80)) return;       // plinth->screen traffic from some other plinth

  const bool wasStandalone = standalone;
  adoptScreen(p.mac);
  lastHeardMs = millis();
  standalone = false;
  // Back in touch (screen rebooted, or out of its service menu): a crystal that
  // is still sitting here is announced again -- its standalone TAG_PLACED may
  // have been lost or dropped as stale. The next poll re-reads it and unicasts
  // TAG_PLACED (watched + resent). No REMOVED: it never left.
  if (wasStandalone && tagPresent) {
    tagPresent = false;
    missCount = 0;
  }

  switch (h->type) {
    case LT_HELLO_ACK:                 // "talk to me" + the idle look
    case LT_PING: {                    // keep-alive every 1 s, same body
      if (p.len < sizeof(LinkHelloAck)) break;
      LinkHelloAck a;
      memcpy(&a, p.data, sizeof(a));
      applyIdleConfig(a, wasStandalone);
      break;
    }
    case LT_LED: {
      if (p.len < sizeof(LinkLed)) break;
      LinkLed m;
      memcpy(&m, p.data, sizeof(m));
      if (m.mode > LED_MODE_LAST) { logLine("led: unknown mode %u ignored", (unsigned)m.mode); break; }   // a newer screen
      if (m.mode != rs.mode || DEBUG > 1)
        logLine("led: %s #%06lX / #%06lX speed %u level %u brightness %u %u ms", modeName(m.mode),
                (unsigned long)(m.color & 0xFFFFFFu), (unsigned long)(m.color2 & 0xFFFFFFu), (unsigned)m.speed,
                (unsigned)m.level, (unsigned)m.brightness, (unsigned)m.ms);
      // brightness 0 = keep the cap from HELLO_ACK / PING; non-zero caps this command only
      ringSetMode(m.mode, m.color, m.color2, m.speed, m.level, m.ms, m.brightness, false);
      break;
    }
    case LT_WRITE_REQ:
      handleWriteReq(p.data, p.len);
      break;
    default:
      logLine("link: unknown packet type 0x%02X ignored", (unsigned)h->type);
      break;
  }
}

static void linkService(uint32_t now) {
  if (!espnowOk) {
    if (now - lastLinkTryMs >= 5000) { lastLinkTryMs = now; linkBegin(); }
    return;
  }
  static RxPacket p;
  for (int budget = 0; budget < 8 && xQueueReceive(rxQ, &p, 0) == pdTRUE; budget++) handlePacket(p);

  now = millis();                      // packet handling (writes) can take a while
  if (!standalone && now - lastHeardMs > SCREEN_TIMEOUT_MS) enterStandalone();

  // resend TAG_PLACED if the radio reported a failed unicast right after it
  if (placedWatch) {
    if (!linked || !tagPresent || placedPkt.uidLen != curTag.uidLen ||
        memcmp(placedPkt.uid, curTag.uid, curTag.uidLen) != 0) {
      placedWatch = false;             // gone, swapped or no screen: nothing to repair
    } else if (txFail.load() != placedFailMark) {
      if (placedResends < PLACED_RESENDS) {
        placedResends++;
        logLine("link: TAG_PLACED not acknowledged - sending it again (%u)", (unsigned)placedResends);
        sendPlaced();
      } else {
        placedWatch = false;
      }
    } else if (now - placedSentMs > PLACED_WATCH_MS) {
      placedWatch = false;
    }
  }

  if (now - lastHelloMs >= HELLO_MS) { lastHelloMs = now; sendHello(); }
}

// ================================= button ==================================
//  Debounced: a level must hold BUTTON_DEBOUNCE_MS before it counts. One
//  LT_BUTTON on press (pressed = 1) and one on release (pressed = 0, heldMs).
static inline bool buttonRaw() {
#if BUTTON_ACTIVE_HIGH
  return digitalRead(PIN_BUTTON) == HIGH;      // button to 3V3, internal pull-down
#else
  return digitalRead(PIN_BUTTON) == LOW;       // button to GND, internal pull-up
#endif
}

static void buttonBegin() {
  if (!PLINTH_HAS_BUTTON) return;      // no button fitted: the pin is never touched, buttonDown stays 0
  pinMode(PIN_BUTTON, BUTTON_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  delay(5);
  btnRaw = btnDown = buttonRaw();
  btnChangeMs = millis();
  if (btnDown) {
    btnStuck = true;
    logLine("button: reads PRESSED at boot - ignored until it releases. Stuck, miswired, or on a pin with "
            "an LED (Feather GP13 = red LED): see the button notes at the top of the sketch / README");
  }
}

static void buttonService(uint32_t now) {
  if (!PLINTH_HAS_BUTTON) return;      // no button fitted: never read
  bool raw = buttonRaw();
  if (raw != btnRaw) { btnRaw = raw; btnChangeMs = now; }
  if (raw != btnDown && now - btnChangeMs >= BUTTON_DEBOUNCE_MS) {
    btnDown = raw;
    if (btnStuck) {
      if (!raw) { btnStuck = false; logLine("button: released - back in service"); }
      return;
    }
    if (raw) {
      btnPressMs = now;
      ringSparkle();                   // instant local feedback
      sendButton(true, 0);
      logLine("button: pressed");
    } else {
      uint32_t held = now - btnPressMs;
      sendButton(false, held);
      logLine("button: released after %lu ms", (unsigned long)held);
    }
  }
  if (btnDown && !btnStuck && now - btnPressMs > BUTTON_STUCK_MS) {
    btnStuck = true;
    sendButton(false, BUTTON_STUCK_MS);
    logLine("button: held over %d s - treating it as stuck", BUTTON_STUCK_MS / 1000);
  }
}

static void pump() {
  uint32_t now = millis();
  buttonService(now);
  ringService(now);
}

// ============================ serial console ===============================
static void printStatus() {
  char mac[24] = "-";
  if (linked) hexStr(screenMac, 6, mac, sizeof(mac));
  char u[TAG_UID_MAX * 3 + 1] = "none";
  if (tagPresent) hexStr(curTag.uid, curTag.uidLen, u, sizeof(u));
  uint32_t now = millis();
  logLine("status: up %lu s | link %s %s (heard %ld ms ago) | reader %s | tag %s | polls %lu placed %lu "
       "readfail %lu resets %lu writes %lu | rx %lu drop %lu ign %lu | tx ok %lu fail %lu err %lu | "
       "ring %s%s cap %u%s | heap %lu",
       (unsigned long)(now / 1000), linked ? "screen" : "broadcast", mac,
       lastHeardMs ? (long)(now - lastHeardMs) : -1L, readerOk ? "ok" : "MISSING", u,
       (unsigned long)statPolls, (unsigned long)statPlaced, (unsigned long)statReadFails,
       (unsigned long)statResets, (unsigned long)statWrites, (unsigned long)rxCount.load(),
       (unsigned long)rxDropped.load(), (unsigned long)rxIgnored, (unsigned long)txOk.load(),
       (unsigned long)txFail.load(), (unsigned long)txErr, PLINTH_HAS_RING ? "" : "none, asked ",
       modeName(rs.mode), (unsigned)ringCap(),
       standalone ? " standalone" : "", (unsigned long)ESP.getFreeHeap());
}

static void dumpImage() {
  if (!haveImage) { logLine("dump: no tag read yet"); return; }
  logLine("dump: last tag image (pages 3..39, read %s)", lastImageOk ? "OK" : "INCOMPLETE");
  for (uint8_t i = 0; i < TAG_NUM_PAGES; i++) {
    const uint8_t *b = lastImage[i];
    Serial.printf("   page %2u: %02X %02X %02X %02X  %c%c%c%c\n", (unsigned)(i + TAG_FIRST_PAGE),
                  b[0], b[1], b[2], b[3], isprint(b[0]) ? b[0] : '.', isprint(b[1]) ? b[1] : '.',
                  isprint(b[2]) ? b[2] : '.', isprint(b[3]) ? b[3] : '.');
  }
}

static void serialService() {
  while (Serial.available() > 0) {
    int ch = Serial.read();
    switch (ch) {
      case 'h': case '?':
        Serial.println(F("commands: s = status | d = dump last tag | f = flare | m = next ring mode "
                         "(test) | b = fake button press"));
        break;
      case 's': printStatus(); break;
      case 'd': dumpImage(); break;
      case 'f': ringFlare(); break;
      case 'm':
        if (!PLINTH_HAS_RING) { logLine("ring test: no ring fitted (PLINTH_HAS_RING 0)"); break; }
        testMode = (uint8_t)((testMode + 1) % (LED_MODE_LAST + 1));
        ringSetMode(testMode, testMode == LED_FLARE ? FLARE_COLOR : 0xF97316, 0x38BDF8, 0, 170, 0, 0, false);
        logLine("ring test: %s (the screen's next LED command takes over again)", modeName(testMode));
        break;
      case 'b':
        ringSparkle();
        sendButton(true, 0);
        sendButton(false, 120);
        logLine("button: fake press sent");
        break;
      default: break;
    }
  }
}

// ================================== setup ==================================
void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);            // never stall on a USB host that isn't reading
#endif
  delay(200);
  char ringInfo[40], btnInfo[48];
  if (PLINTH_HAS_RING) snprintf(ringInfo, sizeof(ringInfo), "ring %d px on GPIO%d", RING_LEDS, PIN_RING);
  else snprintf(ringInfo, sizeof(ringInfo), "no ring");
  if (PLINTH_HAS_BUTTON)
    snprintf(btnInfo, sizeof(btnInfo), "button GPIO%d (to %s)", PIN_BUTTON, BUTTON_ACTIVE_HIGH ? "3V3" : "GND");
  else snprintf(btnInfo, sizeof(btnInfo), "no button (screen: button = no)");
  logLine("Crystal Plinth v%d.%d.%d on %s | %s | %s | group %d, channel %d", FW_VERSION / 100,
       (FW_VERSION / 10) % 10, FW_VERSION % 10, BOARD_NAME, ringInfo, btnInfo, LINK_GROUP, LINK_CHANNEL);

  // Feather ESP32-S3: the STEMMA QT / I2C power gate (initVariant() usually did it already)
#if defined(PIN_I2C_POWER)
  pinMode(PIN_I2C_POWER, OUTPUT);
  digitalWrite(PIN_I2C_POWER, HIGH);
  delay(10);
#elif defined(TFT_I2C_POWER)
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);
#endif

  statusBegin();
  ringBegin();
  buttonBegin();

  Wire.begin(PIN_SDA, PIN_SCL, I2C_HZ);
  if (readerInit(5))
    logLine("nfc: PN532 ready (firmware %lu.%lu)", (unsigned long)((readerFw >> 16) & 0xFF),
         (unsigned long)((readerFw >> 8) & 0xFF));
  else
    logLine("nfc: PN532 NOT found - check wiring + DIP switches (SW1 ON, SW2 OFF); retrying every %d s",
         READER_RETRY_MS / 1000);
  lastReaderTryMs = millis();

  linkBegin();
  lastLinkTryMs = millis();
  uint8_t mac[6] = { 0 };
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  char ms[24];
  hexStr(mac, 6, ms, sizeof(ms));
  uint8_t ch = 0;
  wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&ch, &sec);
  logLine("link: %s, plinth MAC %s, channel %u, group %d - broadcasting HELLO until the screen answers",
       espnowOk ? "ESP-NOW up" : "ESP-NOW FAILED (retrying every 5 s)", ms, (unsigned)ch, LINK_GROUP);
  logLine("type h for serial commands");
}

// ================================== loop ===================================
void loop() {
  uint32_t now = millis();
  linkService(now);                    // incoming packets (LED, ACK, writes), heartbeat, timeouts
  buttonService(millis());
  nfcService(millis());                // poll the PN532 (may place / remove a tag)
  statusService(millis());
  ringService(millis());
  serialService();

#if DEBUG
  static uint32_t lastStatusMs = 0;
  if (millis() - lastStatusMs >= 10000) { lastStatusMs = millis(); printStatus(); }
#endif
  delay(1);                            // let the idle task run
}
