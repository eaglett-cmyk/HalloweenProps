/*
 * ============================================================================
 *  touch.cpp — GT911 capacitive touch, minimal polled driver
 * ----------------------------------------------------------------------------
 *  Implements touch.h. GT911 on the shared I2C bus (0x5D, or 0x14), INT on
 *  GPIO4, RST on expander EXIO1. No ISR and no config rewrite: the controller
 *  keeps Waveshare's factory configuration, we just read reports.
 *
 *  Reset / address select — Waveshare 06_TOUCH timing, proven on this board:
 *    INT output LOW, RST low, 100 ms, INT LOW, 100 ms, RST high, 200 ms,
 *    INT released to input. INT held low while RST rises selects 0x5D.
 *
 *  GT911 registers used (16-bit register addresses, big-endian on the wire):
 *    0x8048..0x804B  config: x max, y max (little-endian) — scaled to 800x480
 *                    if they differ (Waveshare uses no swap/mirror)
 *    0x8140..0x8145  product id "911\0" + firmware version
 *    0x814E          status: bit7 = a new report is ready, bits3..0 = points
 *    0x814F..        points, 8 bytes each: id, xL, xH, yL, yH, sizeL, sizeH, -
 *  A report stays in the buffer until the host writes 0 to 0x814E.
 *
 *  Polling: ONE 9-byte read per update() (status + first point — the only point
 *  the gestures use), plus the 1-byte clear when a report was ready. Reports
 *  with >= 3 points (rain drops, a palm) are ignored and spoil the current press,
 *  so it can't turn into a tap or a long press. With 2 points, point 1 (the
 *  older finger) is the primary.
 *
 *  Never blocks or aborts: if the GT911 is absent, update() returns a static
 *  all-false Gestures without touching the bus; if it stops answering, polling
 *  backs off to once a second until it answers again.
 * ============================================================================
 */
#include "touch.h"
#include "board.h"

namespace touch {

namespace {

constexpr uint16_t REG_X_MAX      = 0x8048;   // + y max at 0x804A
constexpr uint16_t REG_PRODUCT_ID = 0x8140;
constexpr uint16_t REG_STATUS     = 0x814E;   // followed by point 1 at 0x814F

constexpr int16_t  SCREEN_W = 800;
constexpr int16_t  SCREEN_H = 480;
constexpr uint32_t TAP_MAX_MS = 450;          // down->up shorter than this ...
constexpr int32_t  MOVE_PX = 40;              // ... and never further than this from the start = tap
constexpr uint32_t STUCK_MS = 1000;           // a press with no report for this long ...
constexpr uint8_t  STUCK_POLLS = 5;           // ... over this many polls is treated as released (lost release report)
constexpr uint8_t  FAILS_BEFORE_BACKOFF = 10; // consecutive failed polls before backing off
constexpr uint32_t BACKOFF_MS = 1000;

bool     s_ok = false;
uint8_t  s_addr = 0;
bool     s_scale = false;
uint16_t s_xMax = SCREEN_W, s_yMax = SCREEN_H;

Gestures s_g;                 // what update() returns
const Gestures s_none{};      // returned when there is no touch controller

// current press
bool     s_down = false;
bool     s_moved = false;     // went further than MOVE_PX from the start point
bool     s_spoiled = false;   // a >= 3-point report arrived during the press
bool     s_longFired = false;
uint32_t s_startMs = 0;
Point    s_start = {0, 0};
uint32_t s_lastReportMs = 0;
uint8_t  s_emptyPolls = 0;

// bus health
uint8_t  s_fails = 0;
uint32_t s_nextPollMs = 0;

bool readReg(uint16_t reg, uint8_t *d, size_t n) { return board::i2cReadReg16(s_addr, reg, d, n); }

bool clearStatus() {
  const uint8_t z = 0;
  return board::i2cWriteReg16(s_addr, REG_STATUS, &z, 1);
}

// Raw point record (8 bytes) -> screen coordinates, scaled if the controller's
// configured resolution isn't 800x480, clamped to the screen.
Point toScreen(const uint8_t *p) {
  int32_t x = (int32_t)(p[1] | (p[2] << 8));
  int32_t y = (int32_t)(p[3] | (p[4] << 8));
  if (s_scale) {
    x = x * SCREEN_W / s_xMax;
    y = y * SCREEN_H / s_yMax;
  }
  if (x < 0) x = 0;
  if (x >= SCREEN_W) x = SCREEN_W - 1;
  if (y < 0) y = 0;
  if (y >= SCREEN_H) y = SCREEN_H - 1;
  return Point{(int16_t)x, (int16_t)y};
}

void resetPress() {
  s_down = s_moved = s_spoiled = s_longFired = false;
  s_emptyPolls = 0;
  s_g.down = false;
  s_g.heldMs = 0;
}

// Finger lifted (or the press timed out): a short, still, clean press is a tap.
void endPress(uint32_t now, bool allowTap) {
  if (s_down && allowTap && !s_spoiled && !s_moved && !s_longFired && now - s_startMs < TAP_MAX_MS) {
    s_g.tap = true;
    s_g.tapAt = s_g.at;
  }
  resetPress();
}

void pointReport(const Point &p, uint32_t now) {
  if (!s_down) {
    s_down = true;
    s_moved = s_spoiled = s_longFired = false;
    s_startMs = now;
    s_start = p;
    s_g.pressStart = true;
  } else if (!s_moved) {
    const int32_t dx = p.x - s_start.x, dy = p.y - s_start.y;
    if (dx * dx + dy * dy > MOVE_PX * MOVE_PX) s_moved = true;
  }
  s_g.down = true;
  s_g.at = p;
}

}  // namespace

// ============================================================================
//  lifecycle
// ============================================================================
bool begin() {
  s_ok = false;
  s_addr = 0;
  s_scale = false;
  s_xMax = SCREEN_W;
  s_yMax = SCREEN_H;
  s_g = Gestures();
  resetPress();

  if (!board::expanderOk()) Serial.println("[touch] IO expander missing - GT911 reset line not driven");

  // Reset + address select (Waveshare timing): INT low while RST rises => 0x5D.
  pinMode(board::PIN_TP_INT, OUTPUT);
  digitalWrite(board::PIN_TP_INT, LOW);
  board::touchReset(true);    // EXIO1 low
  delay(100);
  digitalWrite(board::PIN_TP_INT, LOW);
  delay(100);
  board::touchReset(false);   // EXIO1 high
  delay(200);
  pinMode(board::PIN_TP_INT, INPUT);   // the GT911 drives INT from now on (we poll, no ISR)

  for (int i = 0; i < 3 && !s_addr; i++) {
    if (i) delay(20);
    if (board::i2cProbe(board::ADDR_GT911)) s_addr = board::ADDR_GT911;
    else if (board::i2cProbe(board::ADDR_GT911_B)) s_addr = board::ADDR_GT911_B;
  }
  if (!s_addr) {
    Serial.println("[touch] GT911 not found at 0x5D/0x14 - touch disabled");
    return false;
  }

  uint8_t id[6] = {0};
  if (!readReg(REG_PRODUCT_ID, id, sizeof(id))) {
    Serial.printf("[touch] GT911 at 0x%02X does not answer register reads - touch disabled\n", s_addr);
    s_addr = 0;
    return false;
  }
  char pid[5] = {0};
  for (int i = 0; i < 4 && id[i] >= 0x20 && id[i] < 0x7F; i++) pid[i] = (char)id[i];
  if (strncmp(pid, "911", 3) != 0)
    Serial.printf("[touch] unexpected product id \"%s\" - continuing as GT911\n", pid);

  // Configured output resolution; scale if it isn't the panel's 800x480.
  uint8_t cfg[4];
  if (readReg(REG_X_MAX, cfg, sizeof(cfg))) {
    const uint16_t xm = (uint16_t)(cfg[0] | (cfg[1] << 8));
    const uint16_t ym = (uint16_t)(cfg[2] | (cfg[3] << 8));
    if (xm >= 100 && xm <= 4096 && ym >= 100 && ym <= 4096) {
      if (xm != SCREEN_W || ym != SCREEN_H) {
        s_scale = true;
        s_xMax = xm;
        s_yMax = ym;
        Serial.printf("[touch] GT911 configured for %ux%u - scaling to 800x480%s\n", xm, ym,
                      xm < ym ? " (portrait config? axes NOT swapped)" : "");
      }
    } else {
      Serial.printf("[touch] GT911 config resolution %ux%u looks wrong - using raw coordinates\n", xm, ym);
    }
  }

  clearStatus();   // drop any report left over from before the reset
  s_ok = true;
  s_fails = 0;
  s_nextPollMs = 0;
  Serial.printf("[touch] GT911 ok at 0x%02X, id %s, fw 0x%04X\n", s_addr, pid, (unsigned)(id[4] | (id[5] << 8)));
  return true;
}

bool ok() { return s_ok; }

// ============================================================================
//  polling + gestures
// ============================================================================
const Gestures &update(uint32_t longPressMs) {
  if (!s_ok) return s_none;
  const uint32_t now = millis();
  s_g.tap = false;
  s_g.pressStart = false;
  s_g.longPress = false;

  if ((int32_t)(now - s_nextPollMs) < 0) return s_g;   // backing off after repeated bus failures

  uint8_t r[9];   // 0x814E status + the first point record
  if (!readReg(REG_STATUS, r, sizeof(r))) {
    if (s_fails < 255) s_fails++;
    if (s_fails >= FAILS_BEFORE_BACKOFF) {
      if (s_fails == FAILS_BEFORE_BACKOFF) Serial.println("[touch] GT911 stopped answering - polling once a second");
      s_nextPollMs = now + BACKOFF_MS;
    }
    if (s_down && now - s_lastReportMs > STUCK_MS) endPress(now, false);
    if (s_down) s_g.heldMs = now - s_startMs;
    return s_g;
  }
  if (s_fails >= FAILS_BEFORE_BACKOFF) Serial.println("[touch] GT911 answering again");
  s_fails = 0;
  s_nextPollMs = 0;

  const uint8_t status = r[0];
  if (status & 0x80) {
    const uint8_t n = status & 0x0F;
    s_lastReportMs = now;
    s_emptyPolls = 0;
    if (n >= 3) {
      if (s_down) s_spoiled = true;   // rain / palm: ignore the report, spoil the press
    } else if (n == 0) {
      if (s_down) endPress(now, true);   // finger lifted
    } else {
      pointReport(toScreen(r + 1), now);
    }
    clearStatus();   // let the controller post the next report
  } else if (s_down) {
    // No new report. The GT911 reports continuously while touched, so a long
    // silence means the release report was lost: end the press (no tap).
    if (s_emptyPolls < 255) s_emptyPolls++;
    if (s_emptyPolls >= STUCK_POLLS && now - s_lastReportMs > STUCK_MS) endPress(now, false);
  }

  if (s_down) {
    s_g.heldMs = now - s_startMs;
    if (!s_longFired && !s_moved && !s_spoiled && s_g.heldMs >= longPressMs) {
      s_g.longPress = true;
      s_g.longAt = s_g.at;
      s_longFired = true;
    }
  }
  return s_g;
}

}  // namespace touch
