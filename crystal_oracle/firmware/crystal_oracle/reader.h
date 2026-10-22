#pragma once
// ============================================================================
//  reader.h — where crystals come from. Two interchangeable backends, chosen by
//  config.ini [station] reader = plinth | rfid2 | none:
//
//   plinth (default): a separate ESP32 in the plinth runs the PN532 + LED ring +
//     big button and talks ESP-NOW (crystal_link.h). Needed because the PN532's
//     fixed I2C address 0x24 collides with this board's IO expander.
//   rfid2: an M5Stack Unit RFID2 (WS1850S / MFRC522-compatible, I2C 0x28) wired
//     to the P1 screw terminal (5V, GND, SDA, SCL). Single board, short range
//     (< 20 mm). No ring light or button (LED commands are ignored).
//   none: no reader (demo / bench: crystals can be simulated from the service menu).
//
//  All backends deliver the same events through one FreeRTOS queue, so the show
//  never knows which one is active.
// ============================================================================
#include <Arduino.h>
#include "crystal_link.h"

struct TagImage {
  uint8_t uid[TAG_UID_MAX];
  uint8_t uidLen;
  bool    readOk;                     // every page 3..39 was read
  uint8_t sak;
  uint8_t pages[TAG_NUM_PAGES][4];    // pages 3..39 (index = page - TAG_FIRST_PAGE)
  uint16_t readMs;                    // how long the reader took to read pages 3..39 (0 = unknown:
                                      // a plinth older than 1.2.0, or a simulated crystal)
};

enum class ReaderEvt : uint8_t { None = 0, Placed, Removed, WriteDone, Button, LinkUp, LinkDown };

struct ReaderEvent {
  ReaderEvt type = ReaderEvt::None;
  TagImage  tag;              // Placed / Removed (uid only)
  uint16_t  reqId = 0;        // WriteDone
  uint8_t   status = 0;       // WriteDone: WriteStatus
  uint8_t   failedPage = 0;   // WriteDone
  uint8_t   button = 0;       // Button
  bool      pressed = false;  // Button
  uint16_t  heldMs = 0;       // Button (on release)
};

namespace reader {

bool begin();                 // starts the backend chosen in settings::cfg.reader
bool poll(ReaderEvent &ev);   // non-blocking: true if an event was dequeued
bool online();                // plinth heard within 3 s / RFID2 chip answering
bool tagPresent();            // a tag is on the reader right now (best knowledge)
const char *backendName();    // "plinth", "rfid2", "none"

// Queue an ordered write list for the tag with this UID. Returns a request id;
// the result arrives later as a WriteDone event with the same reqId. The plinth
// backend resends automatically until a result arrives or 2 s pass (then it
// posts WriteDone with status WS_NO_TAG).
uint16_t write(const uint8_t *uid, uint8_t uidLen, const PageWrite *writes, uint8_t count);

// Plinth light (ignored by rfid2/none). Colours 0xRRGGBB.
void led(LedMode mode, uint32_t color, uint32_t color2 = 0, uint16_t ms = 0, uint8_t speed = 0, uint8_t level = 0);
void setIdleLight(uint32_t color, uint8_t brightness);   // what the ring does when idle

// The crystal on the reader right now has been dealt with (the host menu closed: e.g. it was
// prepared on the Crystals page, or it already played before the menu opened). A plinth that
// went standalone while the menu ran re-reads a crystal that is still sitting there when the
// screen talks to it again; that re-announcement is NOT posted as Placed until the crystal has
// been lifted (Removed, or the heartbeat says the reader is empty). Plinth backend only:
// rfid2 never re-announces a crystal that stays put, simulated crystals are lifted by the menu.
void ignoreUntilLifted();

// Service menu / testing: inject a fake placement / button press as if a reader sent it.
void simulatePlaced(const TagImage &img);
void simulateLifted();        // the simulated crystal's sequence ended: it no longer blocks the idle button
void simulateButton();
uint32_t lastHeardMs();       // millis() of the last packet from the plinth (0 = never)
uint32_t packetsIn();

}  // namespace reader
