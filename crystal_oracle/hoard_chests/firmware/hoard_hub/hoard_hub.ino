// ============================================================================
//  hoard_hub.ino — the Hoard Hub: controller for 15 pop-open treasure chests.
//
//  Board : ESP32-WROOM-32 30-pin DevKit (AITRIP), FQBN esp32:esp32:esp32, core 3.x
//  Libs  : Adafruit NeoPixel
//  Spec  : ../../docs/DESIGN.md (pin table, chest cycle, link packets)
//  Docs  : README.md next to this file (wiring, serial commands, calibration, tests)
//
//  One chest = one port (1..15, 16 = spare). Each port has an SG90 latch, a lid
//  switch and one WS2812B pixel, reached through three 74HCT4067 muxes:
//    servo mux  : GPIO25 pulse, GPIO26 enable (active low), select 16/17/18/19
//    pixel mux  : GPIO4 data  \  both on the scan select lines 32/33/27/14
//    switch mux : GPIO34 sense/
//  Only one servo moves at a time (a queue); the servo mux is enabled only while
//  a move runs (~450 ms) so the servos go limp in between. The servos stay
//  powered at 5 V all the time; "limp" only means they get no pulses.
//  The pixels are refreshed one port at a time by a scanner that also reads the
//  lid switches.
//
//  Everything in loop() is non-blocking: the longest busy-wait is the switch
//  settle (40 us) plus one pixel's data (~30 us). No heap use after setup().
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include <driver/gpio.h>
#include <stdarg.h>
#include "hoard_config.h"
#include "hoard_link.h"

// ---------------------------------------------------------------------------
//  Globals
// ---------------------------------------------------------------------------
static Port ports[HOARD_NUM_PORTS];
static Preferences prefs;
static Adafruit_NeoPixel strip(1, PIN_PIXEL_DATA, PIXEL_ORDER + NEO_KHZ800);
static uint8_t brightCap = BRIGHT_CAP_DEFAULT;

static const uint32_t TIER_COLOR[4] = {
  0x000000,   // tier 0: unused
  0xB4C8FF,   // tier 1: cool white  (180,200,255)
  0xFFAA00,   // tier 2: amber       (255,170,0)
  0xFF001E,   // tier 3: ruby        (255,0,30)
};
static const char *STATE_NAME[] = {"DISABLED", "OPEN", "CLOSING", "LOCKING",
                                   "ARMED", "FIRING", "STUCK", "RELEASED", "PENDING"};
static uint8_t armMode = ARM_MODE_DEFAULT;
static const char *LATCH_NAME[] = {"?", "locked", "unlocked", "other"};

// ---------------------------------------------------------------------------
//  Logging (static buffer: Print::printf would malloc for long lines)
// ---------------------------------------------------------------------------
static char logBuf[200];
static void logf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(logBuf, sizeof(logBuf) - 2, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n > (int)sizeof(logBuf) - 3) n = sizeof(logBuf) - 3;
  logBuf[n++] = '\r';
  logBuf[n++] = '\n';
  Serial.write((const uint8_t *)logBuf, n);
}

static inline uint8_t portTierColorIdx(uint8_t p) { return ports[p].tier <= 3 ? ports[p].tier : 0; }

// ---------------------------------------------------------------------------
//  NVS (Preferences, namespace "hoard")
// ---------------------------------------------------------------------------
static void nvsLoad() {
  prefs.begin("hoard", false);
  uint16_t lk[HOARD_NUM_PORTS], ul[HOARD_NUM_PORTS];
  uint8_t tr[HOARD_NUM_PORTS];
  bool haveLk = prefs.getBytesLength("lock") == sizeof(lk) && prefs.getBytes("lock", lk, sizeof(lk)) == sizeof(lk);
  bool haveUl = prefs.getBytesLength("unlock") == sizeof(ul) && prefs.getBytes("unlock", ul, sizeof(ul)) == sizeof(ul);
  bool haveTr = prefs.getBytesLength("tier") == sizeof(tr) && prefs.getBytes("tier", tr, sizeof(tr)) == sizeof(tr);
  for (uint8_t p = 0; p < HOARD_NUM_PORTS; p++) {
    Port &P = ports[p];
    P.lockUs   = haveLk ? constrain(lk[p], SERVO_US_MIN, SERVO_US_MAX) : DEFAULT_LOCK_US;
    P.unlockUs = haveUl ? constrain(ul[p], SERVO_US_MIN, SERVO_US_MAX) : DEFAULT_UNLOCK_US;
    uint8_t def = p < 5 ? 1 : p < 10 ? 2 : p < 15 ? 3 : 0;
    P.tier = haveTr ? (tr[p] <= 3 ? tr[p] : def) : def;
  }
  brightCap = prefs.getUChar("bright", BRIGHT_CAP_DEFAULT);
  armMode = prefs.getUChar("arm", ARM_MODE_DEFAULT) == ARM_CONFIRM ? ARM_CONFIRM : ARM_AUTO;
}

static void nvsSaveCal() {
  uint16_t lk[HOARD_NUM_PORTS], ul[HOARD_NUM_PORTS];
  for (uint8_t p = 0; p < HOARD_NUM_PORTS; p++) { lk[p] = ports[p].lockUs; ul[p] = ports[p].unlockUs; }
  prefs.putBytes("lock", lk, sizeof(lk));
  prefs.putBytes("unlock", ul, sizeof(ul));
}
static void nvsSaveTiers() {
  uint8_t tr[HOARD_NUM_PORTS];
  for (uint8_t p = 0; p < HOARD_NUM_PORTS; p++) tr[p] = ports[p].tier;
  prefs.putBytes("tier", tr, sizeof(tr));
}

// ---------------------------------------------------------------------------
//  Mux helpers
// ---------------------------------------------------------------------------
static inline void setSelect(const uint8_t *pins, uint8_t ch) {
  for (uint8_t b = 0; b < 4; b++) digitalWrite(pins[b], (ch >> b) & 1);
}
static inline void servoMuxEnable(bool on) { digitalWrite(PIN_SERVO_EN_N, on ? LOW : HIGH); }

// ---------------------------------------------------------------------------
//  Servo engine: a queue of moves, one at a time
// ---------------------------------------------------------------------------
enum ServoPhase : uint8_t { SV_IDLE, SV_RUN, SV_QUIET, SV_GAP };
static ServoJob sq[SERVO_QUEUE_LEN];
static uint8_t sqHead = 0, sqCount = 0;
static ServoJob svCur;
static ServoPhase svPhase = SV_IDLE;
static uint32_t svT = 0;
static uint16_t nextJobId = 1;
static bool ledcOk = false;

static inline uint32_t dutyForUs(uint16_t us) {
  return (uint32_t)us * ((1UL << SERVO_LEDC_BITS) - 1) / 20000UL;   // 50 Hz = 20000 us period
}

static uint16_t servoEnqueue(uint8_t p, MoveKind kind, bool front, uint16_t holdMs) {
  if (sqCount >= SERVO_QUEUE_LEN) { logf("servo: queue full, move for chest %u dropped", p + 1); return 0; }
  ServoJob j;
  j.port = p; j.kind = kind; j.holdMs = holdMs;
  j.id = nextJobId++;
  if (nextJobId == 0) nextJobId = 1;
  if (front) {
    sqHead = (sqHead + SERVO_QUEUE_LEN - 1) % SERVO_QUEUE_LEN;
    sq[sqHead] = j;
  } else {
    sq[(sqHead + sqCount) % SERVO_QUEUE_LEN] = j;
  }
  sqCount++;
  ports[p].pendingMoves++;
  return j.id;
}

// Drop every queued (not yet running) move for port p.
static void servoCancelQueued(uint8_t p) {
  uint8_t kept = 0;
  ServoJob tmp[SERVO_QUEUE_LEN];
  for (uint8_t i = 0; i < sqCount; i++) {
    const ServoJob &j = sq[(sqHead + i) % SERVO_QUEUE_LEN];
    if (j.port == p) { if (ports[p].pendingMoves) ports[p].pendingMoves--; }
    else tmp[kept++] = j;
  }
  for (uint8_t i = 0; i < kept; i++) sq[i] = tmp[i];
  sqHead = 0; sqCount = kept;
}

static void servoUpdate(uint32_t now) {
  switch (svPhase) {
    case SV_IDLE:
      if (!sqCount || !ledcOk) return;
      svCur = sq[sqHead];
      sqHead = (sqHead + 1) % SERVO_QUEUE_LEN;
      sqCount--;
      {
        Port &P = ports[svCur.port];
        const uint16_t us = svCur.kind == MV_LOCK ? P.lockUs : P.unlockUs;
        // mux is disabled here: change the select lines, enable, then start the
        // pulses. LEDC applies a new duty at the next period, so every pulse that
        // reaches the servo is a whole one.
        setSelect(PIN_SERVO_SEL, svCur.port);
        delayMicroseconds(2);
        servoMuxEnable(true);
        ledcWrite(PIN_SERVO_PULSE, dutyForUs(us));
        P.startedJob = svCur.id;
        P.moveStartMs = now;
        logf("servo: chest %u -> %s (%u us)", svCur.port + 1, svCur.kind == MV_LOCK ? "lock" : "unlock", us);
      }
      svPhase = SV_RUN; svT = now;
      break;
    case SV_RUN:
      if (now - svT < SERVO_MOVE_MS) return;
      ledcWrite(PIN_SERVO_PULSE, 0);     // the current pulse finishes, the next period is empty
      svPhase = SV_QUIET; svT = now;
      break;
    case SV_QUIET:
      if (now - svT < SERVO_QUIET_MS) return;
      servoMuxEnable(false);             // 10 k pull-downs hold every servo line low: servos limp
      {
        Port &P = ports[svCur.port];
        P.latch = svCur.kind == MV_LOCK ? LATCH_LOCKED : LATCH_UNLOCKED;
        if (P.pendingMoves) P.pendingMoves--;
        P.holdUntil = now + svCur.holdMs;
      }
      svPhase = SV_GAP; svT = now;
      break;
    case SV_GAP:
      if (now - svT < SERVO_GAP_MS) return;
      svPhase = SV_IDLE;
      break;
  }
}
static inline bool servoBusy() { return svPhase != SV_IDLE || sqCount; }

// ---------------------------------------------------------------------------
//  Per-port state machine
// ---------------------------------------------------------------------------
static void setState(uint8_t p, PortState s, const char *why) {
  Port &P = ports[p];
  if (P.state == s) return;
  logf("chest %u (tier %u): %s -> %s%s%s", p + 1, P.tier, STATE_NAME[P.state], STATE_NAME[s],
       why ? " - " : "", why ? why : "");
  P.state = s;
  P.stateSince = millis();
  if (s == PS_ARMED) P.armedAt = P.stateSince;
}

// Move the latch towards `want` when nothing is queued and no hold is running.
static void reconcileLatch(uint8_t p, LatchPos want, uint32_t now) {
  Port &P = ports[p];
  if (P.pendingMoves || (int32_t)(now - P.holdUntil) < 0 || P.latch == want) return;
  servoEnqueue(p, want == LATCH_LOCKED ? MV_LOCK : MV_UNLOCK, false, 0);
}

static void startFire(uint8_t p, uint32_t color) {
  Port &P = ports[p];
  P.fireStep = 0;
  P.attempts = 0;
  P.fireColor = color ? color : TIER_COLOR[portTierColorIdx(p)];
  setState(p, PS_FIRING, "releasing the latch");
}

// Power-up / `clear` / tier change: decide from the switch alone.
static void initPortFromSwitch(uint8_t p) {
  Port &P = ports[p];
  if (P.tier == 0) { setState(p, PS_DISABLED, "tier 0 (unused)"); return; }
  if (P.closed) setState(p, PS_LOCKING, "lid is shut");
  else          setState(p, PS_OPEN, "lid is open");
}

static void updatePort(uint8_t p, uint32_t now) {
  Port &P = ports[p];
  switch (P.state) {
    case PS_DISABLED:
      break;
    case PS_OPEN:
      if (P.closed) { setState(p, PS_CLOSING, "lid shut"); break; }
      reconcileLatch(p, LATCH_UNLOCKED, now);
      break;
    case PS_CLOSING:
      if (!P.closed) { setState(p, PS_OPEN, "let go before it latched"); break; }
      if (now - P.stateSince >= CLOSE_DEBOUNCE_MS) {
        setState(p, PS_LOCKING, "turning the latch");
        P.holdUntil = 0;
        servoEnqueue(p, MV_LOCK, true, 0);      // the grown-up is holding the lid: go first
      }
      break;
    case PS_LOCKING:
      if (!P.closed) { setState(p, PS_OPEN, "lid opened before the latch turned"); break; }
      if (P.latch == LATCH_LOCKED && !P.pendingMoves) {
        if (armMode == ARM_CONFIRM) setState(p, PS_PENDING, "latched - waiting for the grown-up's confirm");
        else                        setState(p, PS_ARMED, "latched");
        break;
      }
      reconcileLatch(p, LATCH_LOCKED, now);
      break;
    case PS_PENDING:
    case PS_ARMED:
      if (!P.closed) {
        setState(p, PS_OPEN, "FORCED OPEN without a command - unlatching so it can close again");
        P.holdUntil = 0;
        break;
      }
      if (P.latch != LATCH_LOCKED && !P.pendingMoves && (int32_t)(now - P.holdUntil) >= 0) {
        logf("chest %u: latch not at lock while %s - re-locking", p + 1, STATE_NAME[P.state]);
        servoEnqueue(p, MV_LOCK, false, 0);
      }
      break;
    case PS_FIRING:
      if (!P.closed) { setState(p, PS_OPEN, "OPENED (prize out)"); break; }
      if (P.fireStep == 0) {
        if (now - P.stateSince >= FIRE_FLASH_MS) {
          P.waitJob = servoEnqueue(p, MV_UNLOCK, true, 0);
          P.attempts = 1;
          P.fireStep = 1;
        }
      } else if (P.fireStep == 1) {
        if (P.waitJob == 0) { P.fireStep = 2; P.moveStartMs = now; }   // queue was full: just watch
        else if (P.startedJob == P.waitJob) P.fireStep = 2;
      } else {
        if (now - P.moveStartMs >= FIRE_EXPECT_OPEN_MS) {
          if (P.attempts < FIRE_ATTEMPTS) {
            P.attempts++;
            logf("chest %u: lid did not pop - wiggling the latch (try %u of %u)", p + 1, P.attempts, FIRE_ATTEMPTS);
            P.waitJob = servoEnqueue(p, MV_UNLOCK, true, 0);   // pushed in front: runs second
            servoEnqueue(p, MV_LOCK, true, 0);                 // runs first
            P.fireStep = 1;
          } else {
            setState(p, PS_STUCK, "would not open after 3 tries - needs the grown-up");
          }
        }
      }
      break;
    case PS_STUCK:
      if (!P.closed) { setState(p, PS_OPEN, "stuck lid opened"); break; }
      reconcileLatch(p, LATCH_UNLOCKED, now);
      break;
    case PS_RELEASED:
      if (!P.closed) { setState(p, PS_OPEN, "lid opened"); break; }
      reconcileLatch(p, LATCH_UNLOCKED, now);
      break;
  }
}

// ---------------------------------------------------------------------------
//  Effects (identify, sweep, button hint, tease)
// ---------------------------------------------------------------------------
static uint32_t sweepStart = 0;     // identify sweep phase A (each port in order)
static bool sweepOn = false;
static uint8_t buttonHint = 0;      // 1 = "release now = reload", 2 = "release now = identify"

#define SWEEP_STEP_MS   150
#define ID_BLINK_MS     400         // one blink = 200 on + 200 off
#define ID_PAUSE_MS     1200

static inline uint32_t identCycle(uint8_t p) { return (uint32_t)(p + 1) * ID_BLINK_MS + ID_PAUSE_MS; }

static void identifyPort(uint8_t p, uint32_t now, uint32_t startDelay) {
  ports[p].identStart = now + startDelay;
  ports[p].identUntil = now + startDelay + 2 * identCycle(p);
}
static void identifySweep(uint32_t now) {
  sweepOn = true;
  sweepStart = now;
  for (uint8_t p = 0; p < HOARD_NUM_PORTS; p++) identifyPort(p, now, HOARD_NUM_PORTS * SWEEP_STEP_MS);
  logf("identify: sweep 1..16 in order, then every port blinks its number (twice)");
}
static void cancelEffects() {
  sweepOn = false;
  for (uint8_t p = 0; p < HOARD_NUM_PORTS; p++) ports[p].identUntil = 0;
}

static CmdJob job;                  // the one running command (award / open / open-all / tease)
#define TEASE_MAX_HOPS 48
static uint16_t hopAt[TEASE_MAX_HOPS];
static uint8_t hopCount = 0;
static uint8_t teaseList[HOARD_NUM_PORTS];
static uint8_t teaseN = 0, teaseChosenIdx = 0;

static void buildTeaseList() {
  teaseN = 0; teaseChosenIdx = 0;
  for (uint8_t p = 0; p < HOARD_NUM_PORTS; p++) {
    if (!(job.teaseMask & (1u << p))) continue;
    if (p == job.chosen) teaseChosenIdx = teaseN;
    teaseList[teaseN++] = p;
  }
}
// Roulette: a highlight hops round the candidates, slowing down, and stops on
// the chosen chest ~200 ms before the end; the others fade out.
static void buildTeaseHops(uint16_t ms) {
  hopCount = 0;
  float t = 0, iv = 60;
  const float end = ms > 400 ? ms - 220 : ms * 0.5f;
  while (hopCount < TEASE_MAX_HOPS) {
    t += iv;
    if (t > end) break;
    hopAt[hopCount++] = (uint16_t)t;
    iv *= 1.13f;
  }
}

static inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}
static inline float noise01(uint8_t p, uint32_t t, uint32_t stepMs) {
  return (hash32(p * 7919u + t / stepMs) & 0xFFFF) / 65535.0f;
}

// ---------------------------------------------------------------------------
//  Pixel colour per port (before the brightness cap). Returns 0xRRGGBB.
// ---------------------------------------------------------------------------
static inline uint32_t scaleColor(uint32_t c, float lvl) {
  if (lvl <= 0) return 0;
  if (lvl > 1) lvl = 1;
  uint32_t r = (uint32_t)(((c >> 16) & 0xFF) * lvl + 0.5f);
  uint32_t g = (uint32_t)(((c >> 8) & 0xFF) * lvl + 0.5f);
  uint32_t b = (uint32_t)((c & 0xFF) * lvl + 0.5f);
  return (r << 16) | (g << 8) | b;
}

static uint32_t computeColor(uint8_t p, uint32_t now) {
  const Port &P = ports[p];
  const uint32_t tierCol = TIER_COLOR[portTierColorIdx(p)];

  // 1. TEST button held: show what releasing will do
  if (buttonHint && P.state != PS_DISABLED) return buttonHint == 1 ? 0xFF6000 : 0xFFFFFF;

  // 2. identify sweep, phase A: each port lights in order
  if (sweepOn) {
    const uint32_t t = now - sweepStart;
    if (t < (uint32_t)HOARD_NUM_PORTS * SWEEP_STEP_MS) return (t / SWEEP_STEP_MS == p) ? 0xFFFFFF : 0;
  }
  // 3. identify: blink the port number
  if (P.identUntil && (int32_t)(now - P.identUntil) < 0) {
    if ((int32_t)(now - P.identStart) < 0) return 0;
    const uint32_t t = (now - P.identStart) % identCycle(p);
    if (t < (uint32_t)(p + 1) * ID_BLINK_MS && (t % ID_BLINK_MS) < ID_BLINK_MS / 2) return 0xFFFFFF;
    return 0;
  }

  // 4. tease (award phase 0, or CA_TEASE)
  const bool teasing = (job.kind == JK_TEASE || (job.kind == JK_AWARD && job.phase == 0)) &&
                       (job.teaseMask & (1u << p)) && (P.state == PS_ARMED);
  if (teasing) {
    const uint32_t col = job.color ? job.color : tierCol;
    const uint32_t t = now - job.t0;
    const float shimmer = 0.25f + 0.75f * noise01(p, now, 55);
    if (job.kind == JK_TEASE || teaseN == 0) return scaleColor(col, shimmer);
    // award roulette
    uint8_t done = 0;
    while (done < hopCount && t >= hopAt[done]) done++;
    const uint8_t remaining = hopCount - done;
    const uint8_t hi = teaseList[(teaseChosenIdx + teaseN * 8 - (remaining % teaseN)) % teaseN];
    const bool settled = remaining == 0;
    if (settled) {
      if (p == job.chosen) return col;
      const uint32_t since = hopCount ? t - hopAt[hopCount - 1] : t;
      const float fade = 1.0f - since / 220.0f;
      return scaleColor(col, 0.3f * shimmer * (fade > 0 ? fade : 0));
    }
    return p == hi ? col : scaleColor(col, 0.15f + 0.35f * shimmer);
  }

  // 5. state
  switch (P.state) {
    case PS_DISABLED:
    case PS_OPEN:
      return 0;
    case PS_CLOSING:
      return scaleColor(tierCol, 0.3f);
    case PS_LOCKING:
      return scaleColor(tierCol, 0.55f);
    case PS_ARMED: {
      const float ph = ((now + p * 260u) % 4000u) / 4000.0f;     // 4 s breath, gentle ripple across ports
      const float s = 0.5f - 0.5f * cosf(ph * 6.2831853f);
      return scaleColor(tierCol, 0.10f + 0.90f * s * s);
    }
    case PS_FIRING:
      if (P.fireStep == 0) return ((now - P.stateSince) / 60) & 1 ? 0xFFFFFF : P.fireColor;
      return P.fireColor;
    case PS_STUCK:
      return (now / 125) & 1 ? 0xFF0000 : 0;
    case PS_RELEASED:
      return 0x505050;     // soft white: "this lid is unlatched but still shut - open it"
    case PS_PENDING:
      return (now % 2000u) < 1000u ? 0x000050 : 0;   // dim blue slow blink: "confirm me"
  }
  return 0;
}

// ---------------------------------------------------------------------------
//  Scanner: one port per step — select, settle, read switch, maybe send pixel
// ---------------------------------------------------------------------------
static uint8_t scanIdx = 0;
static bool showPending = false;
static uint32_t lastShowEndUs = 0;

static void debounceSwitch(Port &P, bool raw, uint32_t now) {
  if (raw != P.rawClosed) { P.rawClosed = raw; P.rawSince = now; }
  else if (raw != P.closed && now - P.rawSince >= SW_STABLE_MS) P.closed = raw;
}

// Returns false when it has to wait for the previous pixel to latch.
static bool scanStep() {
  if (showPending && (uint32_t)(micros() - lastShowEndUs) < PIXEL_LATCH_US) return false;
  showPending = false;
  const uint8_t p = scanIdx;
  scanIdx = (scanIdx + 1) % HOARD_NUM_PORTS;

  setSelect(PIN_SCAN_SEL, p);
  delayMicroseconds(PIXEL_SETTLE_US);
  const bool raw = digitalRead(PIN_SWITCH_SENSE) == LOW;   // LOW = lid closed
  const uint32_t now = millis();
  Port &P = ports[p];
  debounceSwitch(P, raw, now);

  const uint32_t c = scaleColor(computeColor(p, now), brightCap / 255.0f);
  if (!P.everSent || c != P.shownColor || now - P.lastSentMs >= PIXEL_REFRESH_MS) {
    strip.setPixelColor(0, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    strip.show();
    lastShowEndUs = micros();
    showPending = true;
    P.shownColor = c;
    P.lastSentMs = now;
    P.everSent = true;
  }
  return true;
}

// ---------------------------------------------------------------------------
//  Status masks + link
// ---------------------------------------------------------------------------
static uint16_t maskOf(PortState s) {
  uint16_t m = 0;
  for (uint8_t p = 0; p < HOARD_NUM_PORTS; p++) if (ports[p].state == s) m |= 1u << p;
  return m;
}
static uint16_t openMask() {
  uint16_t m = 0;
  for (uint8_t p = 0; p < HOARD_NUM_PORTS; p++)
    if (ports[p].state != PS_DISABLED && !ports[p].closed) m |= 1u << p;
  return m;
}

static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
struct RxPkt { uint8_t len; uint8_t data[32]; };
static QueueHandle_t rxQ = nullptr;
static volatile uint32_t rxDropped = 0;
static bool espnowOk = false;
static uint16_t txSeq = 0;
static uint8_t curChannel = HUB_START_CHANNEL;
static uint8_t savedChannel = HUB_START_CHANNEL;
static bool hopping = false;
static uint32_t lastHeardMs = 0, lastHopMs = 0, lastStatusMs = 0;
static uint32_t rxBeacons = 0, rxCmds = 0, txStatus = 0;
static uint16_t lastCmdId = 0;
static uint8_t lastResult = CR_OK, lastPort = 0;

struct SeenCmd { uint16_t id; uint32_t t; bool used; };
static SeenCmd seen[8];
static uint8_t seenNext = 0;

static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!rxQ || !info || !data) return;
  if (len < (int)sizeof(LinkHeader)) return;
  if (data[0] != HOARD_MAGIC0 || data[1] != HOARD_MAGIC1) return;   // hoard packets only ('C','H')
  RxPkt pk;
  pk.len = len > (int)sizeof(pk.data) ? sizeof(pk.data) : (uint8_t)len;
  memcpy(pk.data, data, pk.len);
  if (xQueueSend(rxQ, &pk, 0) != pdTRUE) rxDropped = rxDropped + 1;
}

static void setChannel(uint8_t ch) {
  esp_err_t e = esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  if (e == ESP_OK) curChannel = ch;
}

static void linkBegin() {
  rxQ = xQueueCreate(8, sizeof(RxPkt));
  WiFi.persistent(false);
  WiFi.setSleep(false);
  if (!WiFi.mode(WIFI_STA)) { logf("link: WiFi STA start failed - running without the Oracle"); return; }
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);
  setChannel(savedChannel);
  if (esp_now_init() != ESP_OK) { logf("link: esp_now_init failed - running without the Oracle"); return; }
  esp_now_register_recv_cb(onRecv);
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, BCAST, 6);
  peer.channel = 0;                    // = whatever channel we are on (we hop)
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) { logf("link: add broadcast peer failed"); return; }
  espnowOk = true;
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  logf("link: ESP-NOW up, hub MAC %02X:%02X:%02X:%02X:%02X:%02X, channel %u, group %u",
       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], curChannel, (unsigned)HUB_GROUP);
}

static void sendStatus() {
  if (!espnowOk) return;
  LinkChestStatus s;
  memset(&s, 0, sizeof(s));
  hoardFillHeader(s.h, LT_CHEST_STATUS, txSeq++, HUB_GROUP);
  s.cmdId = lastCmdId;
  s.result = lastResult;
  s.port = lastPort;
  s.armedMask = maskOf(PS_ARMED);
  s.openMask = openMask();
  s.stuckMask = maskOf(PS_STUCK);
  s.fwVersion = HUB_FW_VERSION;
  s.uptimeS = millis() / 1000;
  if (esp_now_send(BCAST, (const uint8_t *)&s, sizeof(s)) == ESP_OK) txStatus++;
  lastStatusMs = millis();
}

// ---------------------------------------------------------------------------
//  Commands (shared by the link, the serial console and the TEST button)
// ---------------------------------------------------------------------------
static const char *RESULT_NAME[] = {"ok", "TIER EMPTY", "STUCK", "busy"};

static void reportResult(uint16_t cmdId, uint8_t result, uint8_t port1) {
  lastCmdId = cmdId; lastResult = result; lastPort = port1;
  sendStatus();
}

static void finishJob(uint8_t result, uint8_t port1) {
  static const char *KIND[] = {"-", "award", "open", "open-all", "tease"};
  if (port1) logf("%s done: %s, chest %u opened", KIND[job.kind], RESULT_NAME[result], port1);
  else       logf("%s done: %s", KIND[job.kind], RESULT_NAME[result]);
  reportResult(job.cmdId, result, port1);
  job.kind = JK_NONE;
}

// Oldest ARMED chest of `tier` (0 = any) not in `skip`. Ties: lowest port.
static uint8_t pickArmed(uint8_t tier, uint16_t skip) {
  uint8_t best = 0xFF;
  for (uint8_t p = 0; p < HOARD_NUM_PORTS; p++) {
    const Port &P = ports[p];
    if (P.state != PS_ARMED || (skip & (1u << p))) continue;
    if (tier && P.tier != tier) continue;
    if (best == 0xFF || (int32_t)(P.armedAt - ports[best].armedAt) < 0) best = p;
  }
  return best;
}
static uint16_t armedMaskTier(uint8_t tier) {
  uint16_t m = 0;
  for (uint8_t p = 0; p < HOARD_NUM_PORTS; p++)
    if (ports[p].state == PS_ARMED && (!tier || ports[p].tier == tier)) m |= 1u << p;
  return m;
}

static void beginJob(JobKind k, uint16_t cmdId, bool fromLink) {
  memset(&job, 0, sizeof(job));
  job.kind = k; job.cmdId = cmdId; job.fromLink = fromLink;
  job.chosen = 0xFF;
  job.t0 = millis();
}

static void cmdAward(uint16_t cmdId, bool fromLink, uint8_t tier, uint8_t port1, uint32_t color, uint16_t ms) {
  if (job.kind != JK_NONE) { logf("award tier %u: busy", tier); reportResult(cmdId, CR_BUSY, 0); return; }
  if (tier > 3) tier = 0;
  uint8_t chosen = 0xFF;
  if (port1 >= 1 && port1 <= HOARD_NUM_PORTS) {
    if (ports[port1 - 1].state == PS_ARMED) { chosen = port1 - 1; tier = ports[chosen].tier; }
  } else {
    chosen = pickArmed(tier, 0);
  }
  if (chosen == 0xFF) {
    logf("award tier %u%s: TIER EMPTY - no armed chest (ask the grown-up)", tier, port1 ? " (port given)" : "");
    reportResult(cmdId, CR_TIER_EMPTY, 0);
    return;
  }
  beginJob(JK_AWARD, cmdId, fromLink);
  job.tier = tier; job.chosen = chosen; job.color = color;
  job.ms = ms ? ms : TEASE_DEFAULT_MS;
  job.teaseMask = armedMaskTier(tier) | (1u << chosen);
  buildTeaseList();
  buildTeaseHops(job.ms);
  logf("award tier %u: %u armed, chose chest %u (armed longest) - teasing %u ms", tier, teaseN, chosen + 1, job.ms);
}

static void cmdOpenPort(uint16_t cmdId, bool fromLink, uint8_t port1) {
  if (port1 < 1 || port1 > HOARD_NUM_PORTS) { logf("open: bad port"); return; }
  if (job.kind != JK_NONE) { logf("open %u: busy", port1); reportResult(cmdId, CR_BUSY, 0); return; }
  const uint8_t p = port1 - 1;
  Port &P = ports[p];
  if (P.closed && P.state != PS_DISABLED) {
    beginJob(JK_OPEN_PORT, cmdId, fromLink);
    job.chosen = p; job.phase = 1;
    startFire(p, 0);
  } else {
    servoCancelQueued(p);
    servoEnqueue(p, MV_UNLOCK, true, 0);
    logf("open %u: lid already open (or port unused) - unlock move only", port1);
    reportResult(cmdId, CR_OK, 0);
  }
}

static void cmdOpenAll(uint16_t cmdId, bool fromLink) {
  if (job.kind != JK_NONE) { logf("open-all: busy"); reportResult(cmdId, CR_BUSY, 0); return; }
  beginJob(JK_OPEN_ALL, cmdId, fromLink);
  cancelEffects();
  uint8_t n = 0;
  for (uint8_t p = 0; p < HOARD_NUM_PORTS; p++) {
    Port &P = ports[p];
    if (P.state == PS_DISABLED) continue;
    servoCancelQueued(p);
    P.holdUntil = 0;
    if (P.closed) setState(p, PS_RELEASED, "reload: unlatched");
    else if (P.state != PS_OPEN) setState(p, PS_OPEN, "reload");
    job.lastJobId = servoEnqueue(p, MV_UNLOCK, false, 0);
    n++;
  }
  logf("reload: unlatching %u chests one by one; soft white = lid still shut, open it", n);
}

static void cmdTease(uint16_t cmdId, bool fromLink, uint8_t tier, uint32_t color, uint16_t ms) {
  if (job.kind != JK_NONE) { reportResult(cmdId, CR_BUSY, 0); return; }
  const uint16_t m = armedMaskTier(tier > 3 ? 0 : tier);
  if (!m) { logf("tease tier %u: nothing armed", tier); reportResult(cmdId, CR_TIER_EMPTY, 0); return; }
  beginJob(JK_TEASE, cmdId, fromLink);
  job.tier = tier; job.color = color; job.ms = ms ? ms : TEASE_DEFAULT_MS;
  job.teaseMask = m;
}

static void jobUpdate(uint32_t now) {
  switch (job.kind) {
    case JK_NONE: return;
    case JK_TEASE:
      if (now - job.t0 >= job.ms) finishJob(CR_OK, 0);
      return;
    case JK_OPEN_ALL:
      if (!servoBusy()) finishJob(CR_OK, 0);
      return;
    case JK_AWARD:
      if (job.phase == 0) {
        const uint16_t before = job.teaseMask;
        job.teaseMask &= armedMaskTier(0);        // drop anything that stopped being ARMED
        if (ports[job.chosen].state == PS_ARMED && job.teaseMask != before) buildTeaseList();
        if (ports[job.chosen].state != PS_ARMED) {
          const uint8_t c = pickArmed(job.tier, job.triedMask);
          if (c == 0xFF) { logf("award: the chosen chest was forced open and none is left"); finishJob(CR_TIER_EMPTY, 0); return; }
          logf("award: chest %u left ARMED during the tease - switching to chest %u", job.chosen + 1, c + 1);
          job.chosen = c;
          job.teaseMask |= 1u << c;
          buildTeaseList();
        }
        if (now - job.t0 >= job.ms) {
          job.phase = 1;
          startFire(job.chosen, job.color);
        }
        return;
      }
      // phase 1 is the same as open-port, plus "use another"
      [[fallthrough]];
    case JK_OPEN_PORT: {
      const PortState s = ports[job.chosen].state;
      if (s == PS_FIRING) return;
      if (s == PS_OPEN) { finishJob(CR_OK, job.chosen + 1); return; }   // ok even if an earlier pick went STUCK
      if (s == PS_STUCK && job.kind == JK_AWARD) {
        job.triedMask |= 1u << job.chosen;
        job.stuckCount++;
        const uint8_t c = pickArmed(job.tier, job.triedMask);
        if (c != 0xFF) {
          logf("award: chest %u is STUCK - using chest %u instead", job.chosen + 1, c + 1);
          job.chosen = c;
          startFire(c, job.color);
          return;
        }
        finishJob(CR_STUCK, 0);
        return;
      }
      finishJob(CR_STUCK, 0);   // stuck (open-port), or the port changed under us (e.g. tier 0)
      return;
    }
  }
}

// Arm every PENDING chest (arm mode "confirm"). Returns how many.
static uint8_t confirmPending(const char *why) {
  uint8_t n = 0;
  for (uint8_t p = 0; p < HOARD_NUM_PORTS; p++)
    if (ports[p].state == PS_PENDING) { setState(p, PS_ARMED, why); n++; }
  return n;
}

static void cmdIdle(uint16_t cmdId) {
  cancelEffects();
  if (job.kind == JK_TEASE) { job.kind = JK_NONE; }
  logf("idle: effects stopped");
  if (job.kind == JK_NONE) reportResult(cmdId, CR_OK, 0);
}

static void cmdIdentify(uint16_t cmdId, uint8_t port1) {
  const uint32_t now = millis();
  if (port1 >= 1 && port1 <= HOARD_NUM_PORTS) {
    identifyPort(port1 - 1, now, 0);
    logf("identify: chest %u blinks %u times (twice)", port1, port1);
  } else {
    identifySweep(now);
  }
  if (job.kind == JK_NONE) reportResult(cmdId, CR_OK, 0);
}

// ---------------------------------------------------------------------------
//  Link: receive, de-duplicate, dispatch; channel hop
// ---------------------------------------------------------------------------
static bool isRepeat(uint16_t id, uint32_t now) {
  for (uint8_t i = 0; i < 8; i++)
    if (seen[i].used && seen[i].id == id && now - seen[i].t < CMD_DEDUPE_MS) return true;
  seen[seenNext].id = id; seen[seenNext].t = now; seen[seenNext].used = true;
  seenNext = (seenNext + 1) % 8;
  return false;
}

static void heardPlinth(uint32_t now) {
  lastHeardMs = now;
  if (hopping) {
    hopping = false;
    logf("link: found the plinth on channel %u", curChannel);
  }
  if (curChannel != savedChannel) {
    savedChannel = curChannel;
    prefs.putUChar("chan", savedChannel);
  }
}

static void linkPoll(uint32_t now) {
  if (!rxQ) return;
  RxPkt pk;
  while (xQueueReceive(rxQ, &pk, 0) == pdTRUE) {
    if (!hoardHeaderOk(pk.data, pk.len, HUB_GROUP)) continue;
    const LinkHeader *h = (const LinkHeader *)pk.data;
    if (h->type == LT_CHEST_BEACON) {
      rxBeacons++;
      heardPlinth(now);
    } else if (h->type == LT_CHEST_CMD && pk.len >= sizeof(LinkChestCmd)) {
      LinkChestCmd c;
      memcpy(&c, pk.data, sizeof(c));
      heardPlinth(now);
      if (isRepeat(c.cmdId, now)) continue;
      rxCmds++;
      logf("link: cmd %u action %u tier %u port %u color %06lX ms %u", c.cmdId, c.action, c.tier, c.port,
           (unsigned long)c.color, c.ms);
      switch (c.action) {
        case CA_AWARD:     cmdAward(c.cmdId, true, c.tier, c.port, c.color, c.ms); break;
        case CA_TEASE:     cmdTease(c.cmdId, true, c.tier, c.color, c.ms); break;
        case CA_IDLE:      cmdIdle(c.cmdId); break;
        case CA_IDENTIFY:  cmdIdentify(c.cmdId, c.port); break;
        case CA_OPEN_ALL:  cmdOpenAll(c.cmdId, true); break;
        case CA_OPEN_PORT: cmdOpenPort(c.cmdId, true, c.port); break;
        default:           logf("link: unknown action %u ignored", c.action); break;
      }
    }
  }
  if (!espnowOk) return;
  // channel hop while no beacon
  if (!hopping && now - lastHeardMs >= BEACON_LOST_MS) {
    hopping = true;
    lastHopMs = now;
    logf("link: no beacon for %u s - hopping channels 1..13", BEACON_LOST_MS / 1000);
  }
  if (hopping && now - lastHopMs >= CHANNEL_HOP_MS) {
    lastHopMs = now;
    setChannel(curChannel >= 13 ? 1 : curChannel + 1);
  }
  if (now - lastStatusMs >= STATUS_EVERY_MS) sendStatus();
}

// ---------------------------------------------------------------------------
//  TEST button
// ---------------------------------------------------------------------------
static bool btnRaw = false, btnDown = false;
static uint32_t btnRawSince = 0, btnDownAt = 0;
static uint8_t nextTestTier = 1;

static void buttonUpdate(uint32_t now) {
  const bool raw = digitalRead(PIN_TEST_BUTTON) == LOW;
  if (raw != btnRaw) { btnRaw = raw; btnRawSince = now; }
  if (raw != btnDown && now - btnRawSince >= 30) {
    btnDown = raw;
    if (btnDown) {
      btnDownAt = now;
    } else {
      const uint32_t held = now - btnDownAt;
      buttonHint = 0;
      if (held >= 6000) {
        logf("button: held %lu ms - identify sweep", (unsigned long)held);
        identifySweep(now);
      } else if (held >= 2000) {
        logf("button: held %lu ms - reload mode (unlatch all)", (unsigned long)held);
        cmdOpenAll(0, false);
      } else if (armMode == ARM_CONFIRM && maskOf(PS_PENDING)) {
        const uint8_t n = confirmPending("confirmed (TEST button)");
        logf("button: short press - confirmed %u pending chest%s", n, n == 1 ? "" : "s");
      } else {
        const uint8_t t = nextTestTier;
        nextTestTier = nextTestTier >= 3 ? 1 : nextTestTier + 1;
        logf("button: short press - award tier %u", t);
        cmdAward(0, false, t, 0, 0, 0);
      }
    }
  }
  if (btnDown) {
    const uint32_t held = now - btnDownAt;
    buttonHint = held >= 6000 ? 2 : held >= 2000 ? 1 : 0;
  }
}

// ---------------------------------------------------------------------------
//  Serial console
// ---------------------------------------------------------------------------
static char lineBuf[64];
static uint8_t lineLen = 0;

static void printHelp() {
  logf("commands:");
  logf("  status                     ports, latches, link");
  logf("  award <tier>               open the longest-armed chest of tier 1..3 (0 = any)");
  logf("  open <port> | lock <port>  unlatch / latch one chest (1..16)");
  logf("  openall                    reload mode: unlatch every chest");
  logf("  cal <port> lock|open <us>  set + save a latch position (500..2500) and move there now");
  logf("  tier <port> <0-3>          set + save a port's tier (0 = unused)");
  logf("  id <port>                  blink the port number (0 = sweep all)");
  logf("  bright <0-255>             pixel brightness cap (saved)");
  logf("  clear <port>               clear STUCK (re-read the switch: shut = re-lock, open = OPEN)");
  logf("  arm auto|confirm           arming mode (saved); confirm = shut lids wait as PENDING");
  logf("  confirm                    arm every PENDING chest (same as a short TEST press)");
  logf("  help");
}

static void printStatus() {
  const uint32_t now = millis();
  logf("Hoard Hub v%u.%u.%u  up %lus  bright %u  arm %s  job %u  servo queue %u%s", HUB_FW_VERSION / 100,
       (HUB_FW_VERSION / 10) % 10, HUB_FW_VERSION % 10, (unsigned long)(now / 1000), brightCap,
       armMode == ARM_CONFIRM ? "confirm" : "auto", job.kind, sqCount, servoBusy() ? " (moving)" : "");
  logf("port tier state     lid    latch     lock  open  armed-for");
  for (uint8_t p = 0; p < HOARD_NUM_PORTS; p++) {
    const Port &P = ports[p];
    char armed[12] = "";
    if (P.state == PS_ARMED) snprintf(armed, sizeof(armed), "%lus", (unsigned long)((now - P.armedAt) / 1000));
    logf(" %2u    %u   %-9s %-6s %-9s %4u  %4u  %s", p + 1, P.tier, STATE_NAME[P.state], P.closed ? "shut" : "open",
         LATCH_NAME[P.latch], P.lockUs, P.unlockUs, armed);
  }
  logf("link: %s, channel %u%s, beacons %lu, cmds %lu, status sent %lu, rx dropped %lu",
       espnowOk ? "up" : "DOWN", curChannel, hopping ? " (hopping)" : "",
       (unsigned long)rxBeacons, (unsigned long)rxCmds, (unsigned long)txStatus, (unsigned long)rxDropped);
  if (rxBeacons || rxCmds) logf("      last plinth packet %lus ago", (unsigned long)((now - lastHeardMs) / 1000));
  else                     logf("      no plinth heard since power-up (fine: the hub works on its own)");
  logf("last cmd %u: %s, port %u", lastCmdId, RESULT_NAME[lastResult & 3], lastPort);
}

static bool parsePort(const char *s, uint8_t &p) {
  if (!s) return false;
  long v = strtol(s, nullptr, 10);
  if (v < 1 || v > HOARD_NUM_PORTS) { logf("port must be 1..%u", HOARD_NUM_PORTS); return false; }
  p = (uint8_t)(v - 1);
  return true;
}

static void runCommand(char *line) {
  char *save = nullptr;
  char *cmd = strtok_r(line, " \t", &save);
  if (!cmd) return;
  for (char *c = cmd; *c; c++) *c = tolower(*c);
  char *a1 = strtok_r(nullptr, " \t", &save);
  char *a2 = strtok_r(nullptr, " \t", &save);
  char *a3 = strtok_r(nullptr, " \t", &save);
  uint8_t p;
  const uint32_t now = millis();

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) { printHelp(); }
  else if (!strcmp(cmd, "status")) { printStatus(); }
  else if (!strcmp(cmd, "award")) {
    long t = a1 ? strtol(a1, nullptr, 10) : -1;
    if (t < 0 || t > 3) { logf("usage: award <tier 1..3, 0 = any>"); return; }
    cmdAward(0, false, (uint8_t)t, 0, 0, 0);
  }
  else if (!strcmp(cmd, "open")) { if (parsePort(a1, p)) cmdOpenPort(0, false, p + 1); }
  else if (!strcmp(cmd, "lock")) {
    if (!parsePort(a1, p)) return;
    Port &P = ports[p];
    servoCancelQueued(p);
    if (P.state == PS_FIRING || P.state == PS_DISABLED) { logf("lock %u: port is %s - not now", p + 1, STATE_NAME[P.state]); return; }
    if (P.closed) {
      if (P.state != PS_ARMED && P.state != PS_PENDING) setState(p, PS_LOCKING, "lock command");
      servoEnqueue(p, MV_LOCK, true, 0);
    } else {
      servoEnqueue(p, MV_LOCK, true, LOCK_HOLD_MS);
      logf("lock %u: lid is open - locking for %u s to look, then back to unlock", p + 1, LOCK_HOLD_MS / 1000);
    }
  }
  else if (!strcmp(cmd, "openall")) { cmdOpenAll(0, false); }
  else if (!strcmp(cmd, "cal")) {
    if (!parsePort(a1, p) || !a2 || !a3) { logf("usage: cal <port> lock|open <us>"); return; }
    long us = strtol(a3, nullptr, 10);
    if (us < SERVO_US_MIN) us = SERVO_US_MIN;
    if (us > SERVO_US_MAX) us = SERVO_US_MAX;
    Port &P = ports[p];
    MoveKind k;
    if (!strcmp(a2, "lock")) { P.lockUs = (uint16_t)us; k = MV_LOCK; }
    else if (!strcmp(a2, "open") || !strcmp(a2, "unlock")) { P.unlockUs = (uint16_t)us; k = MV_UNLOCK; }
    else { logf("usage: cal <port> lock|open <us>"); return; }
    nvsSaveCal();
    servoCancelQueued(p);
    servoEnqueue(p, k, true, CAL_HOLD_MS);
    logf("cal %u: %s = %ld us (saved), moving there now; holding %u s before the hub tidies up",
         p + 1, k == MV_LOCK ? "lock" : "open", us, CAL_HOLD_MS / 1000);
  }
  else if (!strcmp(cmd, "tier")) {
    if (!parsePort(a1, p) || !a2) { logf("usage: tier <port> <0-3>"); return; }
    long t = strtol(a2, nullptr, 10);
    if (t < 0 || t > 3) { logf("tier must be 0..3"); return; }
    if (ports[p].state == PS_FIRING) { logf("tier %u: chest is firing - try again in a moment", p + 1); return; }
    ports[p].tier = (uint8_t)t;
    nvsSaveTiers();
    logf("tier: chest %u is now tier %ld (saved)", p + 1, t);
    if (t == 0) { servoCancelQueued(p); setState(p, PS_DISABLED, "tier 0"); }
    else if (ports[p].state == PS_DISABLED) { ports[p].latch = LATCH_UNKNOWN; initPortFromSwitch(p); }
  }
  else if (!strcmp(cmd, "id")) {
    long v = a1 ? strtol(a1, nullptr, 10) : 0;
    cmdIdentify(0, (uint8_t)(v >= 0 && v <= HOARD_NUM_PORTS ? v : 0));
  }
  else if (!strcmp(cmd, "bright")) {
    long v = a1 ? strtol(a1, nullptr, 10) : -1;
    if (v < 0 || v > 255) { logf("usage: bright <0-255> (now %u)", brightCap); return; }
    brightCap = (uint8_t)v;
    prefs.putUChar("bright", brightCap);
    logf("bright: cap %u (saved)", brightCap);
  }
  else if (!strcmp(cmd, "clear")) {
    if (!parsePort(a1, p)) return;
    if (ports[p].state == PS_FIRING) { logf("clear %u: chest is firing", p + 1); return; }
    ports[p].holdUntil = 0;
    logf("clear %u: was %s", p + 1, STATE_NAME[ports[p].state]);
    initPortFromSwitch(p);
  }
  else if (!strcmp(cmd, "arm")) {
    if (a1 && !strcmp(a1, "auto"))         armMode = ARM_AUTO;
    else if (a1 && !strcmp(a1, "confirm")) armMode = ARM_CONFIRM;
    else { logf("usage: arm auto|confirm (now %s)", armMode == ARM_CONFIRM ? "confirm" : "auto"); return; }
    prefs.putUChar("arm", armMode);
    logf("arm: mode %s (saved)", armMode == ARM_CONFIRM ? "confirm" : "auto");
    if (armMode == ARM_AUTO) {
      const uint8_t n = confirmPending("arm mode auto");
      if (n) logf("arm: %u pending chest%s armed", n, n == 1 ? "" : "s");
    }
  }
  else if (!strcmp(cmd, "confirm")) {
    const uint8_t n = confirmPending("confirmed (serial)");
    logf("confirm: %u pending chest%s armed", n, n == 1 ? "" : "s");
  }
  else { logf("unknown command '%s' - type help", cmd); }
  (void)now;
}

static void serialPoll() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\r' || c == '\n') {
      if (lineLen) { lineBuf[lineLen] = 0; runCommand(lineBuf); lineLen = 0; }
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = (char)c;
    }
  }
}

// ---------------------------------------------------------------------------
//  setup / loop
// ---------------------------------------------------------------------------
void setup() {
  // servo mux off before anything else (the board's pull-up already holds it off)
  gpio_set_level((gpio_num_t)PIN_SERVO_EN_N, 1);
  pinMode(PIN_SERVO_EN_N, OUTPUT);
  digitalWrite(PIN_SERVO_EN_N, HIGH);

  Serial.begin(115200);
  delay(200);
  logf("");
  logf("Hoard Hub v%u.%u.%u - 15 chests, group %u", HUB_FW_VERSION / 100, (HUB_FW_VERSION / 10) % 10,
       HUB_FW_VERSION % 10, (unsigned)HUB_GROUP);

  for (uint8_t b = 0; b < 4; b++) {
    pinMode(PIN_SERVO_SEL[b], OUTPUT); digitalWrite(PIN_SERVO_SEL[b], LOW);
    pinMode(PIN_SCAN_SEL[b], OUTPUT);  digitalWrite(PIN_SCAN_SEL[b], LOW);
  }
  pinMode(PIN_SWITCH_SENSE, INPUT);          // input-only pin; pull-up is on the board
  pinMode(PIN_TEST_BUTTON, INPUT_PULLUP);

  memset(ports, 0, sizeof(ports));
  memset(&job, 0, sizeof(job));
  nvsLoad();
  savedChannel = prefs.getUChar("chan", HUB_START_CHANNEL);
  if (savedChannel < 1 || savedChannel > 13) savedChannel = HUB_START_CHANNEL;

  ledcOk = ledcAttach(PIN_SERVO_PULSE, 50, SERVO_LEDC_BITS);
  if (ledcOk) ledcWrite(PIN_SERVO_PULSE, 0);
  else logf("servo: LEDC attach on GPIO%u failed - latches will not move", PIN_SERVO_PULSE);

  strip.begin();
  strip.clear();

  // First read of every lid switch (5 samples, majority). After power-up the
  // hub does not know where the latches are: shut lids get a lock move and
  // become ARMED, open ones an unlock move — one at a time, in port order.
  for (uint8_t p = 0; p < HOARD_NUM_PORTS; p++) {
    setSelect(PIN_SCAN_SEL, p);
    uint8_t lows = 0;
    for (uint8_t i = 0; i < 5; i++) { delayMicroseconds(PIXEL_SETTLE_US); if (digitalRead(PIN_SWITCH_SENSE) == LOW) lows++; delay(1); }
    Port &P = ports[p];
    P.rawClosed = P.closed = lows >= 3;
    P.rawSince = millis();
    P.latch = LATCH_UNKNOWN;
    P.state = PS_DISABLED;     // so setState logs the first real state
    initPortFromSwitch(p);
  }

  linkBegin();
  lastHeardMs = millis();      // listen on the saved channel for BEACON_LOST_MS before hopping
  lastStatusMs = millis();

  printHelp();
  enableLoopWDT();             // loop() must return within the task-watchdog timeout
}

void loop() {
  const uint32_t now = millis();
  serialPoll();
  buttonUpdate(now);
  linkPoll(now);
  servoUpdate(now);
  for (uint8_t p = 0; p < HOARD_NUM_PORTS; p++) updatePort(p, now);
  jobUpdate(now);
  if (sweepOn && now - sweepStart > (uint32_t)HOARD_NUM_PORTS * SWEEP_STEP_MS + 2 * identCycle(HOARD_NUM_PORTS - 1))
    sweepOn = false;
  // pixels + switches: as many ports as the latch gaps allow, max one full pass
  for (uint8_t i = 0; i < HOARD_NUM_PORTS; i++) if (!scanStep()) break;
}
