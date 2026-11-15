#pragma once
// ============================================================================
//  hoard_config.h — pins, tuning knobs and shared types for the Hoard Hub.
//  Pin table = hoard_chests/docs/DESIGN.md "Hub board". Change both together.
// ============================================================================
#include <stdint.h>
#include <Adafruit_NeoPixel.h>
#include "hoard_link.h"

#define HUB_FW_VERSION      100            // 1.0.0

// ---- pins (DESIGN.md) ---------------------------------------------------------
#define PIN_SERVO_PULSE     25             // LEDC 50 Hz -> 74AHCT125 -> servo mux common
#define PIN_SERVO_EN_N      26             // servo mux enable, active LOW (10 k pull-up = off at boot)
static const uint8_t PIN_SERVO_SEL[4] = {16, 17, 18, 19};   // servo mux S0..S3
static const uint8_t PIN_SCAN_SEL[4]  = {32, 33, 27, 14};   // pixel mux + switch mux S0..S3
#define PIN_PIXEL_DATA      4              // -> 74AHCT125 + 100 R -> pixel mux common
#define PIN_SWITCH_SENSE    34             // switch mux common, ext 4.7 k pull-up; LOW = lid closed
#define PIN_TEST_BUTTON     13             // to GND, internal pull-up

// ---- link ---------------------------------------------------------------------
#define HUB_GROUP           LINK_DEFAULT_GROUP   // station id; must match the plinth
#define HUB_START_CHANNEL   LINK_DEFAULT_CHAN    // used until a beacon is heard (last good channel is kept in NVS)
#define BEACON_LOST_MS      20000          // no beacon this long -> hop channels
#define CHANNEL_HOP_MS      600            // dwell per channel while hopping (1..13)
#define STATUS_EVERY_MS     5000
#define CMD_DEDUPE_MS       10000          // same cmdId within this window = a repeat

// ---- pixels -------------------------------------------------------------------
#define PIXEL_ORDER         NEO_GRB        // NEO_RGB for the "bullet" pixels
#define BRIGHT_CAP_DEFAULT  102           // global cap 0..255 (~40 %); serial `bright` changes + saves it
#define PIXEL_SETTLE_US     40             // after changing the scan select lines, before reading the switch
#define PIXEL_LATCH_US      320            // line low this long after a pixel's data before re-selecting
#define PIXEL_REFRESH_MS    2000           // re-send every port's colour this often (heals glitches)

// ---- servo ----------------------------------------------------------------------
#define SERVO_LEDC_BITS     16             // 50 Hz, 16-bit: 1 tick = 0.305 us
#define SERVO_MOVE_MS       450            // pulses sent this long, then the servo goes limp
#define SERVO_QUIET_MS      25             // duty 0 this long before the mux is disabled (lets the last pulse finish)
#define SERVO_GAP_MS        40             // pause between two moves
#define SERVO_US_MIN        500
#define SERVO_US_MAX        2500
#define DEFAULT_LOCK_US     1900           // arm points at the partition (fit the arm at this pulse)
#define DEFAULT_UNLOCK_US   950            // ~90 degrees from lock on an SG90 (clockwise seen from above)
#define SERVO_QUEUE_LEN     40

// ---- chest cycle ----------------------------------------------------------------
#define SW_STABLE_MS        40             // raw switch must agree this long to change the debounced state
#define CLOSE_DEBOUNCE_MS   300            // CLOSING: lid held shut this long before the latch turns
#define FIRE_FLASH_MS       360            // flash before the latch releases
#define FIRE_EXPECT_OPEN_MS 800            // from the unlock move's start: the switch must open by then
#define FIRE_ATTEMPTS       3              // 1 unlatch + 2 re-lock/unlatch retries, then STUCK
#define TEASE_DEFAULT_MS    1200
#define LOCK_HOLD_MS        3000           // `lock` on an open lid: stay locked this long, then back to unlock
#define CAL_HOLD_MS         8000           // `cal` moves: stay there this long before the hub reconciles

// ---- types ----------------------------------------------------------------------
enum PortState : uint8_t {
  PS_DISABLED = 0,   // tier 0: not used (spare port 16 by default)
  PS_OPEN,           // lid open, pixel off, latch at unlock
  PS_CLOSING,        // switch closed, waiting CLOSE_DEBOUNCE_MS
  PS_LOCKING,        // lid shut, latch move queued / running
  PS_ARMED,          // latched, breathes in the tier colour
  PS_FIRING,         // flash, unlatch, wait for the lid to pop
  PS_STUCK,          // would not open: fast red blink
  PS_RELEASED,       // unlatched by reload/open-all but the lid is still shut
  PS_PENDING,        // arm mode "confirm": latched, waiting for the grown-up's confirm (not awardable)
};

enum ArmMode : uint8_t { ARM_AUTO = 0, ARM_CONFIRM = 1 };
#define ARM_MODE_DEFAULT    ARM_AUTO       // serial `arm auto|confirm` changes + saves it

enum LatchPos : uint8_t { LATCH_UNKNOWN = 0, LATCH_LOCKED, LATCH_UNLOCKED, LATCH_OTHER };
enum MoveKind : uint8_t { MV_LOCK = 0, MV_UNLOCK };

struct Port {
  PortState state;
  uint8_t   tier;               // 0..3 (NVS)
  uint16_t  lockUs, unlockUs;   // calibration (NVS)
  // switch
  bool      rawClosed;          // last raw read
  bool      closed;             // debounced
  uint32_t  rawSince;           // ms when rawClosed last changed
  // state timing
  uint32_t  stateSince;
  uint32_t  armedAt;            // ARMED order: oldest is awarded first
  // latch / servo bookkeeping
  LatchPos  latch;
  uint8_t   pendingMoves;       // jobs queued or running for this port
  uint32_t  holdUntil;          // don't reconcile the latch before this
  uint16_t  waitJob;            // FIRING: the unlock job we are waiting on
  uint16_t  startedJob;         // last job the servo engine started for this port
  uint32_t  moveStartMs;        // ... and when
  // firing
  uint8_t   fireStep;           // 0 flash, 1 wait for unlock start, 2 watch the switch
  uint8_t   attempts;
  uint32_t  fireColor;          // 0xRRGGBB used for the flash
  // pixel
  uint32_t  shownColor;         // last colour sent (after brightness), 0xRRGGBB
  uint32_t  lastSentMs;
  bool      everSent;
  // identify
  uint32_t  identUntil;         // blink-the-port-number effect runs until this
  uint32_t  identStart;
};

struct ServoJob {
  uint8_t  port;                // 0-based
  uint8_t  kind;                // MoveKind
  uint16_t id;
  uint16_t holdMs;              // after the move: holdUntil = now + holdMs
};

enum JobKind : uint8_t { JK_NONE = 0, JK_AWARD, JK_OPEN_PORT, JK_OPEN_ALL, JK_TEASE };

struct CmdJob {
  JobKind  kind;
  bool     fromLink;
  uint16_t cmdId;
  uint8_t  tier;                // award / tease tier (0 = all)
  uint8_t  chosen;              // 0-based port being fired (0xFF = none)
  uint8_t  phase;               // award: 0 tease, 1 firing
  uint32_t color;               // 0 = tier colour
  uint32_t t0;
  uint16_t ms;                  // tease length
  uint16_t teaseMask;           // ports shimmering
  uint16_t triedMask;           // ports that went STUCK during this award
  uint16_t lastJobId;           // open-all: finished when this servo job is done
  uint8_t  stuckCount;
};
