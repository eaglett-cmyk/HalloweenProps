#pragma once
// ============================================================================
//  crystal_link.h — ESP-NOW protocol between the Crystal Oracle screen and the
//  crystal plinth (the small ESP32 under the crystal that runs the PN532 reader,
//  the LED ring and the big button).
//
//  >>> This file exists TWICE, byte-for-byte identical:
//        firmware/crystal_oracle/crystal_link.h   (screen)
//        firmware/crystal_plinth/crystal_link.h   (plinth)
//      Change both together and bump LINK_VERSION (not for an ADDITIVE change
//      that old firmware safely ignores, like a new LedMode: an old plinth drops
//      an unknown mode and keeps its current look). tools/build_assets.py --check
//      fails if the two copies differ.
//
//  Transport: ESP-NOW v1 (<= 250-byte payloads), fixed WiFi channel (default 1)
//  on both boards. Discovery: the plinth broadcasts HELLO every second until the
//  screen answers HELLO_ACK; then both sides talk unicast. Both sides ignore
//  packets whose `group` differs from their own (lets two stations coexist).
//
//  Division of labour:
//    plinth = dumb, fast tag I/O + lights + button. It reads the WHOLE tag
//             (pages 3..39) on placement and executes ordered page-write lists.
//    screen = all game logic (token schema, odds, story, what to write).
//  The plinth never interprets the token schema except for two safety guards:
//    * it only writes pages 3..39 (page 3 = NDEF capability container, and only
//      if its byte 0 is not already 0xE1 — CC bits are one-way OTP);
//    * it refuses a write list whose UID doesn't match the tag on the reader.
// ============================================================================
#include <stdint.h>
#include <stddef.h>

#define LINK_MAGIC0        'C'
#define LINK_MAGIC1        'O'
#define LINK_VERSION       1
#define LINK_DEFAULT_CHAN  1          // WiFi channel used by both boards
#define LINK_DEFAULT_GROUP 1          // station id (config.ini [station] group=)

#define TAG_FIRST_PAGE     3          // pages mirrored in TagImage: 3..39
#define TAG_LAST_PAGE      39
#define TAG_NUM_PAGES      (TAG_LAST_PAGE - TAG_FIRST_PAGE + 1)   // 37
#define TAG_UID_MAX        10
#define LINK_MAX_WRITES    18         // enough for full provisioning (CC + 12 NDEF + charge + slot + marker)

enum LinkType : uint8_t {
  // plinth -> screen
  LT_HELLO        = 0x01,   // broadcast until acked; also sent as unicast heartbeat every 1 s
  LT_TAG_PLACED   = 0x02,   // a tag arrived (or was re-presented): full page image
  LT_TAG_REMOVED  = 0x03,   // the tag left the field (~300 ms of misses)
  LT_WRITE_RESULT = 0x04,   // answer to LT_WRITE_REQ
  LT_BUTTON       = 0x05,   // big plinth button pressed / released
  // screen -> plinth
  LT_HELLO_ACK    = 0x81,   // "I'm the screen, talk to me" (unicast)
  LT_WRITE_REQ    = 0x82,   // ordered page writes for the tag currently on the reader
  LT_LED          = 0x83,   // set the plinth light mode
  LT_PING         = 0x84,   // optional keep-alive from the screen
};

// Plinth light modes (LT_LED). Colours are 0xRRGGBB.
enum LedMode : uint8_t {
  LED_OFF       = 0,
  LED_IDLE      = 1,   // slow breathe in `color` (attract)
  LED_FLARE     = 2,   // instant bright flash in `color`, decays over `ms` (plinth also does this by itself on placement)
  LED_CHARGING  = 3,   // swirling chase in `color`, speed from `speed` (`color2` != 0: the second comet in color2)
  LED_REVEAL    = 4,   // burst in `color` then settle to `color2`
  LED_SLEEPY    = 5,   // slow dim blink in `color` ("place it again")
  LED_RAINBOW   = 6,   // gentle rainbow drift (no-crystal blessing / milestone)
  LED_SOLID     = 7,   // steady `color`
  LED_METER     = 8,   // fill `level`/255 of the ring in `color` (charge meter)
  LED_DUAL      = 9,   // slow smooth crossfade between `color` and `color2` round the ring (~3 s; a
                       // two-colour gradient crystal). Added after v1.0: an older plinth ignores it
};

#pragma pack(push, 1)
struct LinkHeader {
  uint8_t  magic0, magic1;   // 'C','O'
  uint8_t  version;          // LINK_VERSION
  uint8_t  type;             // LinkType
  uint16_t seq;              // sender's running sequence number
  uint16_t group;            // station id
};

// LT_HELLO (plinth -> screen)
struct LinkHello {
  LinkHeader h;
  uint8_t  readerOk;         // 1 = PN532 answered at boot
  uint8_t  buttonDown;       // current button state
  uint8_t  tagPresent;       // 1 = a tag is on the reader right now
  uint8_t  board;            // 1 = Feather ESP32-S3, 2 = ESP32 dev (AITRIP), 0 = other
  uint16_t fwVersion;        // plinth firmware version (e.g. 100 = 1.0.0)
  uint32_t uptimeMs;
};

// LT_TAG_PLACED (plinth -> screen). 8 + 14 + 148 + 2 = 172 bytes.
struct LinkTagPlaced {
  LinkHeader h;
  uint8_t  uid[TAG_UID_MAX];
  uint8_t  uidLen;           // 4, 7 or 10 (NTAG = 7)
  uint8_t  readOk;           // 1 = all pages 3..39 read; 0 = partial / failed (pages may be zero)
  uint8_t  sak;              // ISO14443A SAK (0x00 for NTAG/Ultralight)
  uint8_t  atqaLo;           // ATQA low byte (0x44 for NTAG)
  uint8_t  pages[TAG_NUM_PAGES][4];   // pages 3..39, 4 bytes each
  // ---- appended 2026-10-22 (plinth 1.2.0; additive, LINK_VERSION unchanged). An older
  //      plinth sends the 170-byte packet without it: receivers accept a packet that is at
  //      least offsetof(LinkTagPlaced, readMs) long and treat the missing field as 0.
  uint16_t readMs;           // how long reading pages 3..39 took, incl. retries (0 = unknown);
                             // the host menu's Crystals page shows it (weak coupling = slow reads)
};

// LT_TAG_REMOVED (plinth -> screen)
struct LinkTagRemoved {
  LinkHeader h;
  uint8_t  uid[TAG_UID_MAX];
  uint8_t  uidLen;
};

struct PageWrite {
  uint8_t page;              // 3..39 only (plinth rejects anything else)
  uint8_t data[4];
};

// LT_WRITE_REQ (screen -> plinth). 8 + 11 + 2 + 18*5 = 111 bytes.
// The plinth executes `writes` IN ORDER (the screen puts the marker page LAST =
// commit), retries each page up to 3x, then reads every written page back and
// compares. Re-sent requests with the same `reqId` are answered from cache
// (idempotent), so the screen may resend freely if no result arrives.
struct LinkWriteReq {
  LinkHeader h;
  uint8_t  uid[TAG_UID_MAX];
  uint8_t  uidLen;
  uint16_t reqId;            // screen-chosen id, echoed in LinkWriteResult
  uint8_t  count;            // number of valid entries in `writes`
  PageWrite writes[LINK_MAX_WRITES];
};

enum WriteStatus : uint8_t {
  WS_OK           = 0,       // all pages written and verified
  WS_NO_TAG       = 1,       // no tag on the reader
  WS_WRONG_TAG    = 2,       // a different UID is on the reader
  WS_WRITE_FAIL   = 3,       // a page write failed after retries (see failedPage)
  WS_VERIFY_FAIL  = 4,       // read-back mismatch (see failedPage)
  WS_BAD_REQUEST  = 5,       // page out of range / count too big
};

// LT_WRITE_RESULT (plinth -> screen)
struct LinkWriteResult {
  LinkHeader h;
  uint16_t reqId;
  uint8_t  status;           // WriteStatus
  uint8_t  failedPage;       // page that failed (0 if none)
  uint8_t  written;          // pages written successfully before stopping
};

// LT_BUTTON (plinth -> screen)
struct LinkButton {
  LinkHeader h;
  uint8_t  button;           // 0 = the big plinth button
  uint8_t  pressed;          // 1 = pressed, 0 = released
  uint16_t heldMs;           // on release: how long it was held
};

// LT_HELLO_ACK / LT_PING (screen -> plinth)
struct LinkHelloAck {
  LinkHeader h;
  uint16_t screenFw;         // screen firmware version
  uint8_t  idleMode;         // LedMode to fall back to when idle (normally LED_IDLE)
  uint8_t  brightness;       // ring brightness cap 0..255
  uint32_t idleColor;        // 0xRRGGBB
};

// LT_LED (screen -> plinth)
struct LinkLed {
  LinkHeader h;
  uint8_t  mode;             // LedMode
  uint8_t  speed;            // mode-specific (0 = default)
  uint8_t  level;            // LED_METER fill 0..255
  uint8_t  brightness;       // 0 = keep current cap
  uint32_t color;            // 0xRRGGBB
  uint32_t color2;           // 0xRRGGBB (LED_REVEAL settle colour, LED_DUAL second colour, LED_CHARGING 2nd comet)
  uint16_t ms;               // duration for timed effects (0 = until next command)
};
#pragma pack(pop)

static_assert(sizeof(LinkTagPlaced) <= 250, "ESP-NOW v1 payload limit");
static_assert(sizeof(LinkWriteReq)  <= 250, "ESP-NOW v1 payload limit");

static inline void linkFillHeader(LinkHeader &h, uint8_t type, uint16_t seq, uint16_t group) {
  h.magic0 = LINK_MAGIC0; h.magic1 = LINK_MAGIC1; h.version = LINK_VERSION;
  h.type = type; h.seq = seq; h.group = group;
}
static inline bool linkHeaderOk(const uint8_t *data, int len, uint16_t group) {
  if (len < (int)sizeof(LinkHeader)) return false;
  const LinkHeader *h = (const LinkHeader *)data;
  return h->magic0 == LINK_MAGIC0 && h->magic1 == LINK_MAGIC1 &&
         h->version == LINK_VERSION && h->group == group;
}
