#pragma once
#ifndef HOARD_LINK_H
#define HOARD_LINK_H
// ============================================================================
//  hoard_link.h — ESP-NOW packets between the crystal plinth and the Hoard Hub
//  (the board that drives the 15 pop-open treasure chests).
//
//  Contract: hoard_chests/docs/DESIGN.md, "Link to the Oracle".
//
//  Self-contained, plain C++ (stdint only). Written for the hub
//  (firmware/hoard_hub/) and meant to be COPIED into the plinth firmware.
//
//  The 8-byte header is the SAME layout as crystal_link.h (magic, version, type,
//  seq, group) but with its OWN MAGIC 'C','H' (crystal_link.h = 'C','O'). Why:
//  the screen and plinth firmware drop anything whose magic is not 'C','O' at
//  their first header check, so a hoard packet can never be taken for screen /
//  plinth traffic by any firmware already flashed (the hub status type 0x11 has
//  bit 7 clear and, with 'C','O', an older screen adopted the hub as its plinth).
//  Only a relay that knows this file (plinth 1.4+) looks at 'C','H' packets.
//
//  This file carries its own copy of LinkHeader so the hub does not depend on
//  the plinth folder. In a sketch that also uses crystal_link.h: include
//  crystal_link.h FIRST — this file then sees LINK_MAGIC0 already defined and
//  skips its LinkHeader copy (otherwise it would be defined twice). If
//  crystal_link.h's header ever changes, change the copy below too. Always use
//  hoardFillHeader() / hoardHeaderOk() for hoard packets (never linkFillHeader /
//  linkHeaderOk: those write / expect 'C','O').
//
//  All packets are BROADCAST (FF:FF:FF:FF:FF:FF); nobody needs an address set
//  up. Receivers check magic / version / group with hoardHeaderOk() and accept a
//  packet that is at least sizeof(the struct) long (longer = a newer sender
//  added fields at the end; ignore the tail).
// ============================================================================
#include <stdint.h>
#include <stddef.h>

#define HOARD_MAGIC0       'C'
#define HOARD_MAGIC1       'H'        // NOT 'O': see above
#define HOARD_VERSION      1

// ---- copy of the crystal_link.h header (skipped when crystal_link.h is in) --
#ifndef LINK_MAGIC0
#define HOARD_LINK_OWN_HEADER 1
#define LINK_DEFAULT_CHAN  1
#define LINK_DEFAULT_GROUP 1

#pragma pack(push, 1)
struct LinkHeader {
  uint8_t  magic0, magic1;   // here 'C','H' (HOARD_MAGIC0/1)
  uint8_t  version;          // HOARD_VERSION
  uint8_t  type;             // packet type (HoardLinkType here)
  uint16_t seq;              // sender's running sequence number
  uint16_t group;            // station id
};
#pragma pack(pop)
#endif  // LINK_MAGIC0

static inline void hoardFillHeader(LinkHeader &h, uint8_t type, uint16_t seq, uint16_t group) {
  h.magic0 = HOARD_MAGIC0; h.magic1 = HOARD_MAGIC1; h.version = HOARD_VERSION;
  h.type = type; h.seq = seq; h.group = group;
}
static inline bool hoardHeaderOk(const uint8_t *data, int len, uint16_t group) {
  if (len < (int)sizeof(LinkHeader)) return false;
  const LinkHeader *h = (const LinkHeader *)data;
  return h->magic0 == HOARD_MAGIC0 && h->magic1 == HOARD_MAGIC1 &&
         h->version == HOARD_VERSION && h->group == group;
}

// ---- hoard packet types ------------------------------------------------------
enum HoardLinkType : uint8_t {
  LT_CHEST_STATUS = 0x11,   // hub -> broadcast: after every command and every 5 s
  LT_CHEST_CMD    = 0x90,   // plinth -> broadcast: sent 3x, 40 ms apart, same cmdId
  LT_CHEST_BEACON = 0x91,   // plinth -> broadcast every 2 s (lets the hub find the channel)
};

// LT_CHEST_CMD actions
enum ChestAction : uint8_t {
  CA_AWARD     = 1,   // open one ARMED chest of `tier` (port != 0: that chest)
  CA_TEASE     = 2,   // shimmer the tier's ARMED chests for `ms`
  CA_IDLE      = 3,   // stop tease / identify effects, back to normal
  CA_IDENTIFY  = 4,   // blink `port` (0 = identify sweep of all ports)
  CA_OPEN_ALL  = 5,   // reload mode: unlatch everything
  CA_OPEN_PORT = 6,   // unlatch `port`
};

// LT_CHEST_STATUS results
enum ChestResult : uint8_t {
  CR_OK         = 0,
  CR_TIER_EMPTY = 1,  // no ARMED chest in that tier ("ask the grown-up")
  CR_STUCK      = 2,  // chest(s) would not open and are now STUCK
  CR_BUSY       = 3,  // another command is still running
};

#define HOARD_NUM_PORTS 16          // 15 chests + the spare header (port 16)

#pragma pack(push, 1)
// LT_CHEST_BEACON (plinth -> broadcast). Header only.
struct LinkChestBeacon {
  LinkHeader h;
};

// LT_CHEST_CMD (plinth -> broadcast). 8 + 11 = 19 bytes.
struct LinkChestCmd {
  LinkHeader h;
  uint16_t cmdId;            // sender-chosen; the 3 repeats share it (hub de-duplicates)
  uint8_t  action;           // ChestAction
  uint8_t  tier;             // 1..3, 0 = all
  uint8_t  port;             // 1..16, 0 = hub picks
  uint32_t color;            // 0xRRGGBB, 0 = tier colour
  uint16_t ms;               // effect length (CA_TEASE; CA_AWARD tease length), 0 = default
};

// LT_CHEST_STATUS (hub -> broadcast). 8 + 16 = 24 bytes.
// Masks: bit (port - 1).
struct LinkChestStatus {
  LinkHeader h;
  uint16_t cmdId;            // last command's cmdId echoed (0 = none yet / local command)
  uint8_t  result;           // ChestResult of that command
  uint8_t  port;             // the chest that opened (0 = none)
  uint16_t armedMask;        // ARMED (latched, prize inside, ready)
  uint16_t openMask;         // lid switch reads open
  uint16_t stuckMask;        // STUCK (would not open; needs the grown-up)
  uint16_t fwVersion;        // hub firmware (100 = 1.0.0)
  uint32_t uptimeS;
};
#pragma pack(pop)

static_assert(sizeof(LinkHeader) == 8, "header must stay 8 bytes");
static_assert(sizeof(LinkChestCmd) == 19, "LinkChestCmd layout");
static_assert(sizeof(LinkChestStatus) == 24, "LinkChestStatus layout");

#endif  // HOARD_LINK_H
