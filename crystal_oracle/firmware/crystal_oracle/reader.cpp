/*
 * ============================================================================
 *  reader.cpp — where crystals come from (implements reader.h)
 * ----------------------------------------------------------------------------
 *  Three interchangeable backends behind ONE FreeRTOS event queue (depth 8),
 *  chosen once at begin() from settings::cfg.reader:
 *
 *   plinth  ESP-NOW link to the plinth ESP32 (PN532 + LED ring + big button).
 *           Wire format = crystal_link.h; human description = docs/LINK-PROTOCOL.md.
 *             * WiFi in STA mode only: never connects to an AP, never scans,
 *               power-save off, fixed channel (config.ini [station] channel).
 *               WiFi.persistent(false) so nothing is ever written to NVS.
 *             * Discovery: the plinth broadcasts LT_HELLO; the first valid
 *               packet (our group) from an unknown MAC while no plinth is
 *               alive makes that MAC THE plinth (unicast peer) and we answer
 *               LT_HELLO_ACK. Every broadcast HELLO from it is ACKed again
 *               (the plinth stops broadcasting once it has an ACK). A unicast
 *               HELLO from an unknown MAC means the plinth already knew us
 *               (this screen rebooted) — adopted the same way.
 *             * Online = a packet heard in the last 3 s (heartbeat is 1 s).
 *               While online we send LT_PING every 1 s so the plinth knows the
 *               screen is alive (it goes standalone after 3 s of silence).
 *             * Writes: LT_WRITE_REQ unicast, resent every 250 ms until the
 *               matching LT_WRITE_RESULT arrives or 2 s pass (then WriteDone
 *               WS_NO_TAG). Results for unknown reqIds (duplicates of an
 *               answered request) are ignored. reqIds start at a random value
 *               each boot so the plinth's idempotency cache can't answer a new
 *               request with a result from before a screen reboot.
 *             * Re-announcements: a plinth that went standalone (the screen sat
 *               in its host menu) re-reads a crystal that is still on it when
 *               the screen talks again. After the menu the show calls
 *               ignoreUntilLifted(): that crystal's TAG_PLACED is then not
 *               posted again until a REMOVED (or an empty heartbeat) arrives,
 *               so a crystal prepared on the menu's Crystals page never plays.
 *             * TAG_PLACED from a plinth older than 1.2.0 lacks the trailing
 *               readMs (170 instead of 172 bytes): accepted, readMs = 0.
 *             * LED: identical consecutive LT_LED commands are not re-sent
 *               (the show may call led() every frame) unless the radio
 *               reported a failed send since, or a one-shot effect (FLARE /
 *               REVEAL) is re-triggered >= 250 ms later.
 *             The ESP-NOW receive callback runs on the WiFi task (core 0): it
 *             only filters the header and copies the packet into a small
 *             internal queue. All protocol work (events, ACKs, resends,
 *             timeouts) happens in poll() on the loop task (core 1).
 *
 *   rfid2   M5Stack Unit RFID2 (WS1850S, MFRC522-compatible) at I2C 0x28 on
 *           the shared bus, via RFID_MFRC522v2 2.0.6. A task pinned to core 0
 *           (prio 4) polls every ~100 ms: WUPA + SELECT, read pages 3..39 on
 *           arrival (10 x 16-byte READ) -> Placed; 3 consecutive misses ->
 *           Removed; the card is HALTed after every cycle so the next WUPA
 *           always gets an answer from a card that is still there. Write lists
 *           run on the same task with the plinth's guards (UID check, pages
 *           3..39 only, page 3 skipped if byte 0 is already 0xE1, 3 tries per
 *           page, read-back verify). LED commands are ignored.
 *           The library's own MFRC522DriverI2C is NOT used: on core 3.3.x its
 *           single-byte register read calls Wire.endTransmission() after
 *           requestFrom() without a beginTransmission(), which sends a stray
 *           zero-length write and gives back Wire's mutex this task no longer
 *           holds (racing every other Wire user and board::i2cRecover()). Our
 *           tiny driver (HalI2CDriver) talks through the same HAL calls Wire
 *           uses (::i2cWrite / ::i2cRead) with its own buffers — each call is
 *           one atomic, bus-locked transaction — and never calls Wire.begin().
 *
 *   none    only the queue: simulatePlaced()/simulateButton() still work.
 *
 *  Simulated crystals (service menu) behave like a perfect reader on every
 *  backend: a write() to the last simulated crystal's UID succeeds at once and
 *  updates the simulated page image, so simulating the same crystal again
 *  shows the replay path.
 *
 *  Threading: poll(), write(), led(), setIdleLight() and simulate*() are meant
 *  for the loop task (show + service menu); a mutex guards their shared state
 *  anyway. online(), tagPresent(), lastHeardMs(), packetsIn() and
 *  backendName() are safe from anywhere.
 *  Never aborts, never blocks for long: every queue/lock/I2C/radio call has a
 *  timeout, failures are logged and degrade to "no crystal".
 * ============================================================================
 */
#include "reader.h"
#include "settings.h"
#include "board.h"
#include "vlog.h"

#include <atomic>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_random.h>
#include "esp32-hal-i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <MFRC522v2.h>
#include <MFRC522Driver.h>
#include <MFRC522Constants.h>

namespace reader {

namespace {

// ============================================================================
//  common
// ============================================================================
constexpr UBaseType_t EVENT_DEPTH    = 8;
constexpr uint8_t     OUTBOX_SIZE    = 6;       // WriteDone results waiting for room in the event queue
constexpr uint32_t    SIM_PRESENT_MS = 10000;   // a simulated crystal counts as "on the reader" at most this long
constexpr TickType_t  LOCK_WAIT      = pdMS_TO_TICKS(200);
constexpr uint8_t     CC_MAGIC       = 0xE1;    // NDEF capability container byte 0 (OTP once set)

uint8_t  s_backend = READER_PLINTH;
bool     s_begun = false;
bool     s_beginOk = false;
QueueHandle_t s_events = nullptr;
StaticSemaphore_t s_lockBuf;
SemaphoreHandle_t s_lock = nullptr;
uint16_t s_nextReqId = 1;
bool     s_loggedOnline = false;
uint32_t s_lastStatsMs = 0;
constexpr uint32_t STATS_EVERY_MS = 300000;   // a counters line on the serial console every 5 min
std::atomic<uint32_t> s_eventsDropped{0};

struct Result {
  uint16_t reqId;
  uint8_t  status;
  uint8_t  failedPage;
};
Result  s_outbox[OUTBOX_SIZE];
uint8_t s_outboxN = 0;

struct SimTag {
  bool     valid;
  bool     present;    // counts as "on the reader" (cleared when its sequence ends)
  uint32_t placedMs;
  TagImage img;
};
SimTag s_sim = {};

// Timed take of the reader mutex (ok() == false -> the caller degrades).
class Guard {
 public:
  Guard() : _ok(s_lock != nullptr && xSemaphoreTake(s_lock, LOCK_WAIT) == pdTRUE) {}
  ~Guard() {
    if (_ok) xSemaphoreGive(s_lock);
  }
  bool ok() const { return _ok; }

 private:
  bool _ok;
};

// Milliseconds since `then`, robust against `then` being a hair in the future
// (a callback on the other core stamped it after we read millis()).
uint32_t ageMs(uint32_t then, uint32_t now) {
  const int32_t d = (int32_t)(now - then);
  return d < 0 ? 0 : (uint32_t)d;
}

bool postEvent(const ReaderEvent &ev, TickType_t wait = 0) {
  if (!s_events) return false;
  if (xQueueSend(s_events, &ev, wait) == pdTRUE) return true;
  s_eventsDropped++;
  Serial.printf("[reader] event queue full - dropped event type %u\n", (unsigned)ev.type);
  return false;
}

bool postWriteDone(uint16_t reqId, uint8_t status, uint8_t failedPage, TickType_t wait = 0) {
  ReaderEvent ev{};
  ev.type = ReaderEvt::WriteDone;
  ev.reqId = reqId;
  ev.status = status;
  ev.failedPage = failedPage;
  return postEvent(ev, wait);
}

// Deliver a write result from the loop task. If the event queue is full the
// result waits in a small outbox that poll() flushes first, so a WriteDone is
// never lost just because the show hasn't polled yet. Caller holds s_lock.
void queueResult(uint16_t reqId, uint8_t status, uint8_t failedPage) {
  if (s_outboxN == 0 && s_events && uxQueueSpacesAvailable(s_events) > 0 &&
      postWriteDone(reqId, status, failedPage)) {
    return;
  }
  if (s_outboxN >= OUTBOX_SIZE) {
    Serial.printf("[reader] outbox full - dropped result of write #%u\n", s_outbox[0].reqId);
    memmove(&s_outbox[0], &s_outbox[1], sizeof(Result) * (OUTBOX_SIZE - 1));
    s_outboxN--;
  }
  s_outbox[s_outboxN++] = {reqId, status, failedPage};
}

void flushOutbox() {   // caller holds s_lock
  while (s_outboxN > 0 && s_events && uxQueueSpacesAvailable(s_events) > 0) {
    if (!postWriteDone(s_outbox[0].reqId, s_outbox[0].status, s_outbox[0].failedPage)) break;
    memmove(&s_outbox[0], &s_outbox[1], sizeof(Result) * (OUTBOX_SIZE - 1));
    s_outboxN--;
  }
}

uint16_t nextReqId() {
  uint16_t id = s_nextReqId++;
  if (id == 0) id = s_nextReqId++;   // 0 is never a valid request id
  return id;
}

const char *statusName(uint8_t s) {
  switch (s) {
    case WS_OK:          return "ok";
    case WS_NO_TAG:      return "no tag";
    case WS_WRONG_TAG:   return "wrong tag";
    case WS_WRITE_FAIL:  return "write failed";
    case WS_VERIFY_FAIL: return "verify failed";
    case WS_BAD_REQUEST: return "bad request";
    default:             return "?";
  }
}

// ---- simulated crystal (service menu) ----
bool simMatches(const uint8_t *uid, uint8_t uidLen) {
  return s_sim.valid && uidLen == s_sim.img.uidLen && uidLen <= TAG_UID_MAX &&
         memcmp(uid, s_sim.img.uid, uidLen) == 0;
}

void simApply(const PageWrite *writes, uint8_t count) {   // same guards as a real reader
  for (uint8_t i = 0; i < count; i++) {
    const uint8_t page = writes[i].page;
    if (page < TAG_FIRST_PAGE || page > TAG_LAST_PAGE) continue;
    uint8_t *dst = s_sim.img.pages[page - TAG_FIRST_PAGE];
    if (page == TAG_FIRST_PAGE && dst[0] == CC_MAGIC) continue;
    memcpy(dst, writes[i].data, 4);
  }
  s_sim.img.readOk = true;
}

// ============================================================================
//  plinth backend (ESP-NOW)
// ============================================================================
constexpr UBaseType_t RX_DEPTH         = 10;
constexpr uint32_t    LINK_TIMEOUT_MS  = 3000;    // 3 missed 1 s heartbeats
constexpr uint32_t    PING_EVERY_MS    = 1000;
constexpr uint32_t    WRITE_RESEND_MS  = 250;
constexpr uint32_t    WRITE_GIVEUP_MS  = 2000;
constexpr uint8_t     MAX_PENDING      = 4;       // write requests in flight
constexpr uint32_t    PLACED_STALE_MS  = 15000;   // older placements (poll() starved) are dropped
constexpr uint32_t    LED_RETRIGGER_MS = 250;     // identical one-shot LED effects re-sent after this
constexpr uint32_t    FOREIGN_LOG_MS   = 10000;
constexpr uint16_t    SCREEN_FW        = 100;     // 1.0.0, reported in HELLO_ACK / PING
constexpr uint32_t    DEFAULT_IDLE_RGB = 0x7C3AED;

const uint8_t BCAST_MAC[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct RxPkt {
  uint32_t ms;                 // millis() when it arrived
  uint8_t  mac[ESP_NOW_ETH_ALEN];
  bool     bcast;              // sent to FF:FF:FF:FF:FF:FF
  uint8_t  len;
  uint8_t  data[ESP_NOW_MAX_DATA_LEN];
};

struct PendingWrite {
  bool     used;
  uint32_t firstMs;
  uint32_t lastSendMs;
  uint8_t  sends;
  LinkWriteReq req;
};

struct PlinthState {
  bool     known;              // a plinth MAC has been adopted (and added as a peer)
  uint8_t  mac[ESP_NOW_ETH_ALEN];
  uint32_t lastMs;             // arrival time of the last packet from THE plinth
  bool     linkReported;       // LinkUp posted (and no LinkDown since)
  bool     tagPresent;
  uint8_t  tagUid[TAG_UID_MAX];
  uint8_t  tagUidLen;
  uint8_t  noTagHellos;        // consecutive heartbeats saying "no tag" while we think one is there
  uint32_t lastUptime;
  uint16_t seq;
  uint32_t lastPingMs;
  uint32_t lastForeignLogMs;
  LinkHello hello;             // last heartbeat (reader ok, board, fw)
  uint8_t  quietUid[TAG_UID_MAX];   // ignoreUntilLifted(): this crystal's re-announcement is not posted
  uint8_t  quietUidLen;        //   (0 = none) until it is lifted
  bool     haveLed;
  LinkLed  led;                // last LED command sent
  uint32_t ledMs;
  uint32_t ledTxFail;          // s_txFail when it was sent
  PendingWrite pend[MAX_PENDING];
};

QueueHandle_t s_rxq = nullptr;
bool          s_espnowOk = false;
std::atomic<uint16_t> s_group{LINK_DEFAULT_GROUP};
std::atomic<uint32_t> s_lastRxMs{0};
std::atomic<uint32_t> s_rxCount{0};
std::atomic<uint32_t> s_rxDropped{0};
std::atomic<uint32_t> s_rxIgnored{0};
std::atomic<uint32_t> s_txOk{0};
std::atomic<uint32_t> s_txFail{0};
std::atomic<uint32_t> s_txErr{0};

RxPkt s_cbPkt;            // receive-callback scratch (WiFi task only; the callback is never re-entered)
RxPkt s_pollPkt;          // poll() scratch (loop task, under s_lock)
LinkTagPlaced s_tagPkt;   // poll() scratch (loop task, under s_lock)
PlinthState s_pl = {};
uint32_t s_idleColor  = DEFAULT_IDLE_RGB;
uint8_t  s_idleBright = 120;
bool     s_idleSet    = false;   // setIdleLight() was called (begin() then keeps its values)
bool     s_helloSeen  = false;

// ---- radio callbacks (WiFi task, core 0): copy only, no protocol work ----
void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!info || !info->src_addr || !data || len <= 0 || !s_rxq) return;
  if (!linkHeaderOk(data, len, s_group.load(std::memory_order_relaxed))) {
    s_rxIgnored++;
    return;
  }
  if (data[offsetof(LinkHeader, type)] & 0x80) {   // screen -> plinth type: another screen in our group
    s_rxIgnored++;
    return;
  }
  RxPkt &p = s_cbPkt;
  p.ms = millis();
  memcpy(p.mac, info->src_addr, ESP_NOW_ETH_ALEN);
  p.bcast = info->des_addr && memcmp(info->des_addr, BCAST_MAC, ESP_NOW_ETH_ALEN) == 0;
  p.len = (uint8_t)(len > (int)sizeof(p.data) ? sizeof(p.data) : (size_t)len);
  memcpy(p.data, data, p.len);
  s_lastRxMs.store(p.ms);
  s_rxCount++;
  if (xQueueSend(s_rxq, &p, 0) != pdTRUE) s_rxDropped++;
}

void onEspNowSent(const esp_now_send_info_t *info, esp_now_send_status_t status) {
  (void)info;
  if (status == ESP_NOW_SEND_SUCCESS) {
    s_txOk++;
  } else {
    s_txFail++;
  }
}

bool plinthOnline() {
  const uint32_t last = s_lastRxMs.load();
  return s_espnowOk && last != 0 && ageMs(last, millis()) < LINK_TIMEOUT_MS;
}

void macStr(const uint8_t *m, char *out, size_t n) {
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

bool sendRaw(const uint8_t *mac, const void *data, size_t len) {
  const esp_err_t e = esp_now_send(mac, (const uint8_t *)data, len);
  if (e == ESP_OK) return true;
  const uint32_t n = ++s_txErr;
  if (n <= 5 || (n % 100) == 0) {
    Serial.printf("[reader] esp_now_send failed (%s), %lu so far\n", esp_err_to_name(e), (unsigned long)n);
  }
  return false;
}

// LT_HELLO_ACK and LT_PING share one body: who we are + how the ring idles.
void sendConfig(uint8_t type) {
  if (!s_pl.known) return;
  LinkHelloAck a = {};
  linkFillHeader(a.h, type, ++s_pl.seq, s_group.load());
  a.screenFw = SCREEN_FW;
  a.idleMode = LED_IDLE;
  a.brightness = s_idleBright;
  a.idleColor = s_idleColor;
  sendRaw(s_pl.mac, &a, sizeof(a));
}

bool ledOneShot(uint8_t mode) { return mode == LED_FLARE || mode == LED_REVEAL; }

bool sameLed(const LinkLed &a, const LinkLed &b) {
  return a.mode == b.mode && a.speed == b.speed && a.level == b.level && a.brightness == b.brightness &&
         a.color == b.color && a.color2 == b.color2 && a.ms == b.ms;
}

void sendLed(LinkLed &l, uint32_t now) {
  l.h.seq = ++s_pl.seq;
  s_pl.ledTxFail = s_txFail.load();   // a failure reported after this point re-arms identical commands
  if (sendRaw(s_pl.mac, &l, sizeof(l))) {
    s_pl.led = l;
    s_pl.haveLed = true;
    s_pl.ledMs = now;
  }
}

// After (re)linking or a plinth restart: put the ring back into the last
// steady mode the show asked for (one-shot effects are history by then, and so
// is a timed effect whose time has run out - the plinth falls back to idle).
void resendSteadyLed(uint32_t now) {
  if (!s_pl.known || !s_pl.haveLed || ledOneShot(s_pl.led.mode)) return;
  if (s_pl.led.ms != 0 && ageMs(s_pl.ledMs, now) >= s_pl.led.ms) return;
  LinkLed l = s_pl.led;
  sendLed(l, now);
}

bool adoptPlinth(const uint8_t *mac, uint32_t now) {
  char ms[18];
  macStr(mac, ms, sizeof(ms));
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
  peer.channel = 0;                 // = the channel we are on
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  const esp_err_t e = esp_now_add_peer(&peer);
  if (e != ESP_OK && e != ESP_ERR_ESPNOW_EXIST) {
    Serial.printf("[reader] can't add plinth %s as a peer: %s\n", ms, esp_err_to_name(e));
    return false;
  }
  if (s_pl.known && memcmp(s_pl.mac, mac, ESP_NOW_ETH_ALEN) != 0) {
    esp_now_del_peer(s_pl.mac);     // the old plinth has been silent for > 3 s
  }
  memcpy(s_pl.mac, mac, ESP_NOW_ETH_ALEN);
  s_pl.known = true;
  s_pl.lastMs = now;
  s_pl.tagPresent = false;
  s_pl.tagUidLen = 0;
  s_pl.noTagHellos = 0;
  s_pl.lastUptime = 0;
  s_pl.quietUidLen = 0;
  Serial.printf("[reader] plinth %s adopted (group %u)\n", ms, s_group.load());
  vlog::event("reader: plinth %s adopted", ms);
  sendConfig(LT_HELLO_ACK);   // linkTick() re-sends the last steady LED mode when it posts LinkUp
  return true;
}

void handleHello(const RxPkt &p, bool alreadyAcked, uint32_t now) {
  if (p.len < sizeof(LinkHello)) return;
  LinkHello h;
  memcpy(&h, p.data, sizeof(h));
  // A broadcast HELLO means "no screen has answered me yet" (first contact, a
  // lost ACK or a plinth restart): answer every one of them.
  // An already-linked plinth that broadcasts went standalone (it missed our
  // PINGs for 3 s) and shows its own idle look: give it the show's steady mode back.
  if (p.bcast && !alreadyAcked) {
    sendConfig(LT_HELLO_ACK);
    resendSteadyLed(now);
  }
  if (s_pl.lastUptime != 0 && h.uptimeMs + 1000 < s_pl.lastUptime) {
    Serial.printf("[reader] plinth restarted (uptime %lu ms)\n", (unsigned long)h.uptimeMs);
    resendSteadyLed(now);
  }
  s_pl.lastUptime = h.uptimeMs;
  if (!s_helloSeen || h.readerOk != s_pl.hello.readerOk) {
    Serial.printf("[reader] plinth heartbeat: PN532 %s, board %u, fw %u\n", h.readerOk ? "ok" : "NOT ANSWERING",
                  h.board, h.fwVersion);
    if (!h.readerOk) vlog::event("reader: plinth reports its PN532 is not answering");
  }
  s_helloSeen = true;
  s_pl.hello = h;
  // Resync the presence flag from the heartbeat. A missed TAG_REMOVED is only
  // repaired after TWO heartbeats (>= 1 s apart) say "no tag", so a heartbeat
  // racing a placement can never fake a removal.
  if (h.tagPresent) {
    s_pl.noTagHellos = 0;
    s_pl.tagPresent = true;
  } else if (!s_pl.tagPresent) {
    s_pl.quietUidLen = 0;            // the reader is empty: a crystal kept quiet has been lifted
  } else {
    if (++s_pl.noTagHellos >= 2) {
      s_pl.quietUidLen = 0;
      ReaderEvent ev{};
      ev.type = ReaderEvt::Removed;
      ev.tag.uidLen = s_pl.tagUidLen;
      memcpy(ev.tag.uid, s_pl.tagUid, s_pl.tagUidLen);
      postEvent(ev);
      s_pl.tagPresent = false;
      s_pl.noTagHellos = 0;
      Serial.println("[reader] plinth says no tag - posted the missed Removed");
    }
  }
}

void handleTagPlaced(const RxPkt &p, uint32_t now) {
  // A plinth older than 1.2.0 sends the packet without the trailing readMs (170 bytes):
  // accept it and read readMs as 0 (LINK-PROTOCOL.md §1, additive fields).
  if (p.len < offsetof(LinkTagPlaced, readMs)) return;
  memset(&s_tagPkt, 0, sizeof(s_tagPkt));
  memcpy(&s_tagPkt, p.data, p.len < sizeof(s_tagPkt) ? p.len : sizeof(s_tagPkt));
  const LinkTagPlaced &t = s_tagPkt;
  if (t.uidLen == 0 || t.uidLen > TAG_UID_MAX) return;
  const bool quiet = s_pl.quietUidLen == t.uidLen && memcmp(s_pl.quietUid, t.uid, t.uidLen) == 0;
  if (!quiet) s_pl.quietUidLen = 0;   // another crystal: the quiet one has left
  s_pl.tagPresent = true;
  s_pl.noTagHellos = 0;
  s_pl.tagUidLen = t.uidLen;
  memcpy(s_pl.tagUid, t.uid, t.uidLen);
  if (quiet) {
    Serial.println("[reader] crystal re-announced after a link gap - already handled (host menu), ignored "
                   "until it is lifted");
    return;
  }
  if (ageMs(p.ms, now) > PLACED_STALE_MS) {
    Serial.printf("[reader] dropped a placement %lu ms old (poll() was starved)\n",
                  (unsigned long)ageMs(p.ms, now));
    return;
  }
  ReaderEvent ev{};
  ev.type = ReaderEvt::Placed;
  memcpy(ev.tag.uid, t.uid, t.uidLen);
  ev.tag.uidLen = t.uidLen;
  ev.tag.readOk = t.readOk != 0;
  ev.tag.sak = t.sak;
  memcpy(ev.tag.pages, t.pages, sizeof(ev.tag.pages));
  ev.tag.readMs = t.readMs;
  postEvent(ev);
}

void handleTagRemoved(const RxPkt &p) {
  if (p.len < sizeof(LinkTagRemoved)) return;
  LinkTagRemoved r;
  memcpy(&r, p.data, sizeof(r));
  const uint8_t n = r.uidLen > TAG_UID_MAX ? TAG_UID_MAX : r.uidLen;
  s_pl.tagPresent = false;
  s_pl.noTagHellos = 0;
  s_pl.quietUidLen = 0;              // lifted: its next placement is a real one
  ReaderEvent ev{};
  ev.type = ReaderEvt::Removed;
  memcpy(ev.tag.uid, r.uid, n);
  ev.tag.uidLen = n;
  postEvent(ev);
}

void handleWriteResult(const RxPkt &p) {
  if (p.len < sizeof(LinkWriteResult)) return;
  LinkWriteResult r;
  memcpy(&r, p.data, sizeof(r));
  for (PendingWrite &w : s_pl.pend) {
    if (!w.used || w.req.reqId != r.reqId) continue;
    const uint8_t status = r.status <= WS_BAD_REQUEST ? r.status : (uint8_t)WS_WRITE_FAIL;
    Serial.printf("[reader] write #%u: %s (page %u, %u written, %u sends, %lu ms)\n", r.reqId,
                  statusName(status), r.failedPage, r.written, w.sends,
                  (unsigned long)ageMs(w.firstMs, millis()));
    queueResult(r.reqId, status, r.failedPage);
    w.used = false;
    return;
  }
  // Unknown reqId: a duplicate answer to a resend we already resolved — ignore.
}

void handleButton(const RxPkt &p) {
  if (p.len < sizeof(LinkButton)) return;
  LinkButton b;
  memcpy(&b, p.data, sizeof(b));
  ReaderEvent ev{};
  ev.type = ReaderEvt::Button;
  ev.button = b.button;
  ev.pressed = b.pressed != 0;
  ev.heldMs = b.heldMs;
  postEvent(ev);
}

void handlePacket(const RxPkt &p, uint32_t now) {
  const uint8_t type = p.data[offsetof(LinkHeader, type)];
  bool acked = false;
  const bool fromPlinth = s_pl.known && memcmp(p.mac, s_pl.mac, ESP_NOW_ETH_ALEN) == 0;
  if (!fromPlinth) {
    const bool currentAlive = s_pl.known && ageMs(s_pl.lastMs, p.ms) < LINK_TIMEOUT_MS;
    if (currentAlive) {
      // A second plinth in our group while ours is alive: ignore it (log now and then).
      if (s_pl.lastForeignLogMs == 0 || ageMs(s_pl.lastForeignLogMs, now) > FOREIGN_LOG_MS) {
        char ms[18];
        macStr(p.mac, ms, sizeof(ms));
        Serial.printf("[reader] ignoring a second plinth %s in group %u\n", ms, s_group.load());
        s_pl.lastForeignLogMs = now ? now : 1;
      }
      return;
    }
    if (!adoptPlinth(p.mac, p.ms)) return;
    acked = true;
  }
  s_pl.lastMs = p.ms;
  switch (type) {
    case LT_HELLO:        handleHello(p, acked, now); break;
    case LT_TAG_PLACED:   handleTagPlaced(p, now); break;
    case LT_TAG_REMOVED:  handleTagRemoved(p); break;
    case LT_WRITE_RESULT: handleWriteResult(p); break;
    case LT_BUTTON:       handleButton(p); break;
    default:              break;   // unknown plinth->screen type (newer plinth): heard, otherwise ignored
  }
}

void linkTick(uint32_t now) {
  const bool on = s_pl.known && plinthOnline();
  if (on && !s_pl.linkReported) {
    ReaderEvent ev{};
    ev.type = ReaderEvt::LinkUp;
    if (postEvent(ev)) {
      s_pl.linkReported = true;
      s_pl.lastPingMs = now;
      Serial.println("[reader] plinth link UP");
      resendSteadyLed(now);
    }
  } else if (!on && s_pl.linkReported) {
    ReaderEvent ev{};
    ev.type = ReaderEvt::LinkDown;
    if (postEvent(ev)) {
      s_pl.linkReported = false;
      Serial.println("[reader] plinth link DOWN (no heartbeat for 3 s)");
    }
  }
}

void writeTick(uint32_t now) {
  for (PendingWrite &w : s_pl.pend) {
    if (!w.used) continue;
    if (ageMs(w.firstMs, now) >= WRITE_GIVEUP_MS) {
      Serial.printf("[reader] write #%u: no answer from the plinth in %lu ms (%u sends)\n", w.req.reqId,
                    (unsigned long)WRITE_GIVEUP_MS, w.sends);
      queueResult(w.req.reqId, WS_NO_TAG, 0);
      w.used = false;
    } else if (ageMs(w.lastSendMs, now) >= WRITE_RESEND_MS) {
      w.req.h.seq = ++s_pl.seq;
      sendRaw(s_pl.mac, &w.req, sizeof(w.req));
      w.lastSendMs = now;
      w.sends++;
    }
  }
}

void pingTick(uint32_t now) {
  if (!s_pl.known || !s_pl.linkReported) return;
  if (ageMs(s_pl.lastPingMs, now) < PING_EVERY_MS) return;
  s_pl.lastPingMs = now;
  sendConfig(LT_PING);
}

void plinthService(uint32_t now) {   // poll(), caller holds s_lock
  // Keep two slots free for LinkUp/LinkDown and the packet's own event.
  while (uxQueueSpacesAvailable(s_events) >= 2 && xQueueReceive(s_rxq, &s_pollPkt, 0) == pdTRUE) {
    handlePacket(s_pollPkt, now);
  }
  linkTick(now);
  writeTick(now);
  pingTick(now);
}

void plinthWrite(uint16_t id, const uint8_t *uid, uint8_t uidLen, const PageWrite *writes, uint8_t count) {
  if (!s_espnowOk || !s_pl.known) {
    queueResult(id, WS_NO_TAG, 0);   // nobody to talk to yet
    return;
  }
  PendingWrite *slot = nullptr;
  for (PendingWrite &w : s_pl.pend) {
    if (!w.used) {
      slot = &w;
      break;
    }
  }
  if (!slot) {   // all in flight: the oldest one gives up early
    slot = &s_pl.pend[0];
    for (PendingWrite &w : s_pl.pend) {
      if ((int32_t)(w.firstMs - slot->firstMs) < 0) slot = &w;
    }
    queueResult(slot->req.reqId, WS_NO_TAG, 0);
  }
  PendingWrite &w = *slot;
  w = PendingWrite{};
  linkFillHeader(w.req.h, LT_WRITE_REQ, ++s_pl.seq, s_group.load());
  memcpy(w.req.uid, uid, uidLen);
  w.req.uidLen = uidLen;
  w.req.reqId = id;
  w.req.count = count;
  memcpy(w.req.writes, writes, sizeof(PageWrite) * count);
  // Always the full struct (unused entries zero): receivers may check len >= sizeof.
  sendRaw(s_pl.mac, &w.req, sizeof(w.req));
  w.used = true;
  w.firstMs = w.lastSendMs = millis();
  w.sends = 1;
}

void plinthLed(LedMode mode, uint32_t color, uint32_t color2, uint16_t ms, uint8_t speed, uint8_t level) {
  if (!s_espnowOk || !s_pl.known) return;
  LinkLed l = {};
  linkFillHeader(l.h, LT_LED, 0, s_group.load());
  l.mode = (uint8_t)mode;
  l.speed = speed;
  l.level = level;
  l.brightness = 0;   // keep the cap from HELLO_ACK / setIdleLight()
  l.color = color & 0xFFFFFF;
  l.color2 = color2 & 0xFFFFFF;
  l.ms = ms;
  const uint32_t now = millis();
  if (s_pl.haveLed && sameLed(l, s_pl.led)) {
    const bool failedSince = s_txFail.load() != s_pl.ledTxFail;
    const bool retrigger = ledOneShot(l.mode) && ageMs(s_pl.ledMs, now) >= LED_RETRIGGER_MS;
    if (!failedSince && !retrigger) return;
  }
  sendLed(l, now);
}

bool beginPlinth() {
  const Settings &c = settings::cfg;
  s_group.store(c.group);
  if (!s_idleSet) s_idleBright = c.ringBrightness;
  uint8_t ch = c.channel;
  if (ch < 1 || ch > 13) {
    Serial.printf("[reader] channel %u invalid - using %u\n", ch, (unsigned)LINK_DEFAULT_CHAN);
    ch = LINK_DEFAULT_CHAN;
  }

  s_rxq = xQueueCreate(RX_DEPTH, sizeof(RxPkt));
  if (!s_rxq) {
    Serial.println("[reader] no memory for the ESP-NOW receive queue");
    return false;
  }

  // STA only, never an AP, never connect, never scan. persistent(false) keeps
  // the WiFi driver's config in RAM: no NVS (flash) writes, ever.
  WiFi.persistent(false);
  WiFi.setSleep(false);        // stored now, applied by the core when STA starts
  if (!WiFi.mode(WIFI_STA)) {
    Serial.println("[reader] WiFi.mode(STA) failed - plinth link unavailable");
    return false;
  }
  WiFi.disconnect();
  esp_err_t e = esp_wifi_set_ps(WIFI_PS_NONE);   // low latency: radio always listening
  if (e != ESP_OK) Serial.printf("[reader] esp_wifi_set_ps: %s\n", esp_err_to_name(e));
  e = esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  if (e != ESP_OK) {
    Serial.printf("[reader] esp_wifi_set_channel(%u): %s - the plinth must use the radio's current channel\n",
                  ch, esp_err_to_name(e));
  }

  e = esp_now_init();
  if (e != ESP_OK) {
    Serial.printf("[reader] esp_now_init: %s - plinth link unavailable\n", esp_err_to_name(e));
    return false;
  }
  esp_now_register_recv_cb(onEspNowRecv);
  esp_now_register_send_cb(onEspNowSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BCAST_MAC, ESP_NOW_ETH_ALEN);
  peer.channel = 0;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  e = esp_now_add_peer(&peer);
  if (e != ESP_OK && e != ESP_ERR_ESPNOW_EXIST) {
    Serial.printf("[reader] broadcast peer: %s\n", esp_err_to_name(e));
  }
  s_espnowOk = true;

  uint8_t mac[ESP_NOW_ETH_ALEN] = {0};
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  char ms[18];
  macStr(mac, ms, sizeof(ms));
  uint8_t curCh = 0;
  wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&curCh, &sec);
  Serial.printf("[reader] ESP-NOW up: screen %s, channel %u, group %u - waiting for the plinth\n", ms, curCh,
                s_group.load());
  return true;
}

// ============================================================================
//  rfid2 backend (M5Stack Unit RFID2 / WS1850S / MFRC522 on the shared I2C bus)
// ============================================================================
using Uid = MFRC522Constants::Uid;

constexpr uint8_t     I2C_PORT            = 0;      // Wire == port 0 (started by board::begin())
constexpr uint32_t    I2C_TIMEOUT_MS      = 20;
constexpr uint32_t    RECOVER_GAP_MS      = 2000;
constexpr uint32_t    RFID_POLL_MS        = 100;
constexpr uint8_t     RFID_MISS_LIMIT     = 3;
constexpr uint32_t    RFID_CHECK_MS       = 1000;   // VersionReg alive check while online
constexpr uint32_t    RFID_REPROBE_MS     = 2000;   // look for a missing chip this often
constexpr uint8_t     RFID_CHIP_FAILS     = 3;
constexpr uint16_t    RFID_TIMER_SHORT_MS = 5;      // WUPA / SELECT answers come within ~0.1 ms
constexpr uint16_t    RFID_TIMER_LONG_MS  = 25;     // READ / WRITE (NTAG programming ~4 ms) — library default
constexpr uint8_t     RFID_TRIES          = 3;
constexpr UBaseType_t RFID_JOB_DEPTH      = 2;
constexpr uint32_t    RFID_STACK          = 6144;
constexpr UBaseType_t RFID_PRIO           = 4;
constexpr TickType_t  RFID_POST_WAIT      = pdMS_TO_TICKS(200);

// MFRC522 register access through the core's I2C HAL (see the header comment
// for why not MFRC522DriverI2C). Register reads are "write [reg] + STOP, then
// read" exactly like the library's own I2C driver; multi-byte reads/writes stay
// on the same register (FIFO access), per the MFRC522 datasheet §8.1.4.
class HalI2CDriver : public MFRC522Driver {
 public:
  explicit HalI2CDriver(uint8_t addr) : _addr(addr) {}

  bool init() override { return board::i2cProbe(_addr); }   // the bus itself belongs to board::begin()

  void PCD_WriteRegister(const PCD_Register reg, const byte value) override {
    const uint8_t d[2] = {(uint8_t)reg, value};
    xferWrite(d, sizeof(d));
  }

  void PCD_WriteRegister(const PCD_Register reg, const byte count, byte *const values) override {
    if (!values || count == 0) return;
    uint8_t d[1 + MAX_BURST];
    const size_t n = count > MAX_BURST ? MAX_BURST : count;
    d[0] = (uint8_t)reg;
    memcpy(d + 1, values, n);
    xferWrite(d, n + 1);
  }

  // On a bus error this returns 0xFF — the same thing the library sees from
  // Wire.read() = -1 — which makes its wait loops end at once instead of
  // polling a dead chip until their iteration limits.
  byte PCD_ReadRegister(const PCD_Register reg) override {
    uint8_t v = 0xFF;
    if (!readBytes((uint8_t)reg, &v, 1)) v = 0xFF;
    return v;
  }

  void PCD_ReadRegister(const PCD_Register reg, const byte count, byte *const values,
                        const byte rxAlign = 0) override {
    if (!values || count == 0) return;
    uint8_t buf[MAX_BURST];
    const size_t n = count > MAX_BURST ? MAX_BURST : count;
    if (!readBytes((uint8_t)reg, buf, n)) return;   // caller's buffer untouched (as the library does on no data)
    if (rxAlign) {   // only bit positions rxAlign..7 of values[0] are updated
      const uint8_t mask = (uint8_t)(0xFF << (rxAlign & 7));
      values[0] = (uint8_t)((values[0] & ~mask) | (buf[0] & mask));
    } else {
      values[0] = buf[0];
    }
    if (n > 1) memcpy(values + 1, buf + 1, n - 1);
  }

  bool readReg(uint8_t reg, uint8_t &v) { return readBytes(reg, &v, 1); }
  uint32_t errors() const { return _errors; }

 private:
  static constexpr size_t MAX_BURST = 64;   // MFRC522 FIFO size

  bool xferWrite(const uint8_t *d, size_t n) {
    for (int i = 0; i < 2; i++) {
      const esp_err_t e = ::i2cWrite(I2C_PORT, _addr, d, n, I2C_TIMEOUT_MS);
      if (e == ESP_OK) return true;
      noteError(e);
    }
    return false;
  }

  bool readBytes(uint8_t reg, uint8_t *out, size_t n) {
    for (int i = 0; i < 2; i++) {
      esp_err_t e = ::i2cWrite(I2C_PORT, _addr, &reg, 1, I2C_TIMEOUT_MS);
      if (e == ESP_OK) {
        size_t got = 0;
        e = ::i2cRead(I2C_PORT, _addr, out, n, I2C_TIMEOUT_MS, &got);
        if (e == ESP_OK && got == n) return true;
        if (e == ESP_OK) e = ESP_FAIL;
      }
      noteError(e);
    }
    return false;
  }

  // A timeout means the bus itself is stuck (a slave holding SDA): let the
  // board clear it, at most every RECOVER_GAP_MS. A plain NACK just counts.
  void noteError(esp_err_t e) {
    _errors++;
    if (e != ESP_ERR_TIMEOUT) return;
    const uint32_t now = millis();
    if (_lastRecoverMs == 0 || ageMs(_lastRecoverMs, now) >= RECOVER_GAP_MS) {
      _lastRecoverMs = now ? now : 1;
      board::i2cRecover();
    }
  }

  const uint8_t _addr;
  uint32_t _errors = 0;
  uint32_t _lastRecoverMs = 0;
};

HalI2CDriver s_drv(board::ADDR_RFID2);
MFRC522      s_rfid(s_drv);

struct WriteJob {
  uint16_t  reqId;
  uint8_t   uid[TAG_UID_MAX];
  uint8_t   uidLen;
  uint8_t   count;
  PageWrite writes[LINK_MAX_WRITES];
};

enum class Io : uint8_t { Ok, Fail, Gone, Other };   // tag op result: done / failed / no tag / different tag

QueueHandle_t s_jobs = nullptr;
TaskHandle_t  s_rfidTask = nullptr;
std::atomic<bool>     s_chipOk{false};
std::atomic<bool>     s_rfidPresent{false};
std::atomic<uint32_t> s_rfidLastOkMs{0};
std::atomic<uint32_t> s_rfidPolls{0};

// ---- reader-task-only state ----
bool        t_present = false;
Uid         t_cur = {};
uint8_t     t_miss = 0;
uint32_t    t_lastCheckMs = 0;
uint8_t     t_chipFails = 0;
uint16_t    t_timerMs = 0;
ReaderEvent t_ev;   // big (TagImage): kept off the task stack

bool sameUid(const Uid &a, const Uid &b) {
  return a.size == b.size && a.size <= TAG_UID_MAX && memcmp(a.uidByte, b.uidByte, a.size) == 0;
}

// The MFRC522 timer (TPrescaler 0x0A9 from PCD_Init -> 25 us per tick) ends a
// transceive with "no answer". Short for polling, so an empty reader occupies
// the shared I2C bus for ~5 ms per cycle instead of 25 ms.
void rfidTimer(uint16_t ms) {
  if (t_timerMs == ms) return;
  const uint16_t reload = (uint16_t)(ms * 40);
  s_drv.PCD_WriteRegister(MFRC522Constants::TReloadRegH, (byte)(reload >> 8));
  s_drv.PCD_WriteRegister(MFRC522Constants::TReloadRegL, (byte)(reload & 0xFF));
  t_timerMs = ms;
}

// WUPA (wakes IDLE and HALTed cards) + anticollision/SELECT. A card left
// ACTIVE ignores the first WUPA and drops to IDLE, so a second WUPA follows.
bool rfidSelect(Uid &u) {
  rfidTimer(RFID_TIMER_SHORT_MS);
  byte atqa[2];
  byte n = sizeof(atqa);
  MFRC522Constants::StatusCode st = s_rfid.PICC_WakeupA(atqa, &n);
  if (st != MFRC522Constants::STATUS_OK && st != MFRC522Constants::STATUS_COLLISION) {
    n = sizeof(atqa);
    st = s_rfid.PICC_WakeupA(atqa, &n);
  }
  bool ok = false;
  if (st == MFRC522Constants::STATUS_OK || st == MFRC522Constants::STATUS_COLLISION) {
    u = Uid{};
    ok = s_rfid.PICC_Select(&u) == MFRC522Constants::STATUS_OK && u.size >= 4 && u.size <= TAG_UID_MAX;
  }
  rfidTimer(RFID_TIMER_LONG_MS);
  return ok;
}

Io rfidReselect(const Uid &want) {
  Uid u;
  if (!rfidSelect(u)) return Io::Gone;
  return sameUid(u, want) ? Io::Ok : Io::Other;
}

// READ (0x30) at `page` -> 16 bytes (4 pages). A failed command drops an NTAG
// back to IDLE, so every retry re-selects first (and notices if it's gone).
Io rfidRead16(uint8_t page, uint8_t *out16, const Uid &want) {
  for (uint8_t t = 0; t < RFID_TRIES; t++) {
    if (t) {
      const Io r = rfidReselect(want);
      if (r != Io::Ok) return r;
    }
    byte buf[18];   // 16 data + CRC_A
    byte n = sizeof(buf);
    if (s_rfid.MIFARE_Read(page, buf, &n) == MFRC522Constants::STATUS_OK && n >= 16) {
      memcpy(out16, buf, 16);
      return Io::Ok;
    }
  }
  return Io::Fail;
}

Io rfidWrite4(uint8_t page, const uint8_t *data4, const Uid &want) {
  for (uint8_t t = 0; t < RFID_TRIES; t++) {
    if (t) {
      const Io r = rfidReselect(want);
      if (r != Io::Ok) return r;
    }
    byte d[4];
    memcpy(d, data4, 4);
    if (s_rfid.MIFARE_Ultralight_Write(page, d, sizeof(d)) == MFRC522Constants::STATUS_OK) return Io::Ok;
  }
  return Io::Fail;
}

uint8_t ioStatus(Io r, uint8_t failStatus) {
  if (r == Io::Gone) return WS_NO_TAG;
  if (r == Io::Other) return WS_WRONG_TAG;
  return failStatus;
}

void rfidReadImage(const Uid &u, TagImage &img) {
  memset(&img, 0, sizeof(img));
  img.uidLen = u.size;
  memcpy(img.uid, u.uidByte, u.size);
  img.sak = u.sak;
  bool ok = true;
  for (uint8_t p = TAG_FIRST_PAGE; p <= TAG_LAST_PAGE; p += 4) {
    uint8_t d[16];
    if (rfidRead16(p, d, u) != Io::Ok) {
      ok = false;   // partial image: the show asks the kid to place it again
      break;
    }
    for (uint8_t i = 0; i < 4 && p + i <= TAG_LAST_PAGE; i++) {
      memcpy(img.pages[p + i - TAG_FIRST_PAGE], d + 4 * i, 4);
    }
  }
  img.readOk = ok;
}

void rfidPostRemoved(const Uid &u) {
  ReaderEvent ev{};
  ev.type = ReaderEvt::Removed;
  ev.tag.uidLen = u.size;
  memcpy(ev.tag.uid, u.uidByte, u.size);
  postEvent(ev, RFID_POST_WAIT);
}

void rfidPostLink(bool up) {
  ReaderEvent ev{};
  ev.type = up ? ReaderEvt::LinkUp : ReaderEvt::LinkDown;
  postEvent(ev, RFID_POST_WAIT);
}

bool rfidChipInit() {
  if (!board::i2cProbe(board::ADDR_RFID2)) return false;
  // PCD_Init: soft reset, 25 ms transceive timer, 100 % ASK, CRC preset, antenna
  // on. Its bool only recognises NXP/Fudan version bytes, so check ourselves.
  s_rfid.PCD_Init();
  uint8_t v = 0;
  if (!s_drv.readReg(MFRC522Constants::VersionReg, v) || v == 0x00 || v == 0xFF) {
    Serial.printf("[reader] RFID2 at 0x28 answers but VersionReg = 0x%02X - not usable\n", v);
    return false;
  }
  s_rfid.PCD_SetAntennaGain(MFRC522Constants::RxGain_max);
  t_timerMs = RFID_TIMER_LONG_MS;   // what PCD_Init programmed
  s_rfidLastOkMs.store(millis());
  Serial.printf("[reader] RFID2 ready at 0x28 (VersionReg 0x%02X)\n", v);
  return true;
}

void rfidChipCheck(uint32_t now) {
  if (s_chipOk.load()) {
    if (ageMs(t_lastCheckMs, now) < RFID_CHECK_MS) return;
    t_lastCheckMs = now;
    uint8_t v = 0;
    if (s_drv.readReg(MFRC522Constants::VersionReg, v) && v != 0x00 && v != 0xFF) {
      t_chipFails = 0;
      s_rfidLastOkMs.store(now);
      return;
    }
    if (++t_chipFails < RFID_CHIP_FAILS) return;
    t_chipFails = 0;
    s_chipOk.store(false);
    Serial.printf("[reader] RFID2 stopped answering (%lu I2C errors so far)\n", (unsigned long)s_drv.errors());
    if (t_present) {
      rfidPostRemoved(t_cur);
      t_present = false;
      s_rfidPresent.store(false);
    }
    rfidPostLink(false);
  } else {
    if (ageMs(t_lastCheckMs, now) < RFID_REPROBE_MS) return;
    t_lastCheckMs = now;
    if (!rfidChipInit()) return;
    s_chipOk.store(true);
    rfidPostLink(true);
  }
}

void rfidPresence(uint32_t now) {
  Uid u;
  if (rfidSelect(u)) {
    t_miss = 0;
    s_rfidLastOkMs.store(now);
    if (!t_present || !sameUid(u, t_cur)) {
      if (t_present) rfidPostRemoved(t_cur);   // swapped between two polls
      t_ev = ReaderEvent{};
      t_ev.type = ReaderEvt::Placed;
      const uint32_t t0 = millis();
      rfidReadImage(u, t_ev.tag);
      const uint32_t took = millis() - t0;
      t_ev.tag.readMs = (uint16_t)(took ? (took > 65535u ? 65535u : took) : 1u);
      t_present = true;
      t_cur = u;
      s_rfidPresent.store(true);
      Serial.printf("[reader] RFID2 crystal placed (uid %u bytes, read %s)\n", u.size,
                    t_ev.tag.readOk ? "ok" : "PARTIAL");
      postEvent(t_ev, RFID_POST_WAIT);
    }
    s_rfid.PICC_HaltA();   // so the next cycle's WUPA always gets an answer
  } else if (t_present && ++t_miss >= RFID_MISS_LIMIT) {
    rfidPostRemoved(t_cur);
    t_present = false;
    t_miss = 0;
    s_rfidPresent.store(false);
    Serial.println("[reader] RFID2 crystal removed");
  }
  s_rfidPolls++;
}

void rfidRunWrite(const WriteJob &job) {
  uint8_t status = WS_OK;
  uint8_t failed = 0;
  Uid want = {};
  want.size = job.uidLen;
  memcpy(want.uidByte, job.uid, job.uidLen);

  do {
    if (!s_chipOk.load()) {
      status = WS_NO_TAG;
      break;
    }
    Io r = rfidReselect(want);
    if (r != Io::Ok) {
      status = ioStatus(r, WS_NO_TAG);
      break;
    }
    // Page 3 (capability container) is one-way OTP: only written when byte 0
    // isn't 0xE1 yet; otherwise the entry is skipped (not an error).
    bool skipCC = false;
    for (uint8_t i = 0; i < job.count; i++) {
      if (job.writes[i].page != TAG_FIRST_PAGE) continue;
      uint8_t d[16];
      r = rfidRead16(TAG_FIRST_PAGE, d, want);
      if (r != Io::Ok) {
        status = ioStatus(r, WS_WRITE_FAIL);
        failed = TAG_FIRST_PAGE;
      } else {
        skipCC = d[0] == CC_MAGIC;
      }
      break;
    }
    if (status != WS_OK) break;

    // Writes in the given order (the marker page comes last = commit).
    for (uint8_t i = 0; i < job.count; i++) {
      const PageWrite &w = job.writes[i];
      if (w.page == TAG_FIRST_PAGE && skipCC) continue;
      r = rfidWrite4(w.page, w.data, want);
      if (r != Io::Ok) {
        status = ioStatus(r, WS_WRITE_FAIL);
        failed = w.page;
        break;
      }
    }
    if (status != WS_OK) break;

    // Read-back verify (one 16-byte READ covers up to 4 consecutive pages).
    uint8_t blk = 0;
    bool haveBlk = false;
    uint8_t d[16];
    for (uint8_t i = 0; i < job.count; i++) {
      const PageWrite &w = job.writes[i];
      if (w.page == TAG_FIRST_PAGE && skipCC) continue;
      if (!haveBlk || w.page < blk || w.page > blk + 3) {
        r = rfidRead16(w.page, d, want);
        if (r != Io::Ok) {
          status = ioStatus(r, WS_VERIFY_FAIL);
          failed = w.page;
          break;
        }
        blk = w.page;
        haveBlk = true;
      }
      if (memcmp(d + 4 * (w.page - blk), w.data, 4) != 0) {
        status = WS_VERIFY_FAIL;
        failed = w.page;
        break;
      }
    }
  } while (false);

  if (s_chipOk.load()) s_rfid.PICC_HaltA();
  Serial.printf("[reader] RFID2 write #%u: %s (page %u)\n", job.reqId, statusName(status), failed);
  postWriteDone(job.reqId, status, failed, RFID_POST_WAIT);
}

void rfidTask(void *) {
  WriteJob job;
  for (;;) {
    // The job queue doubles as the poll timer: a write wakes the task at once.
    if (xQueueReceive(s_jobs, &job, pdMS_TO_TICKS(RFID_POLL_MS)) == pdTRUE) rfidRunWrite(job);
    const uint32_t now = millis();
    rfidChipCheck(now);
    if (s_chipOk.load()) rfidPresence(now);
  }
}

void rfidWrite(uint16_t id, const uint8_t *uid, uint8_t uidLen, const PageWrite *writes, uint8_t count) {
  if (!s_jobs) {
    queueResult(id, WS_NO_TAG, 0);
    return;
  }
  WriteJob job = {};
  job.reqId = id;
  memcpy(job.uid, uid, uidLen);
  job.uidLen = uidLen;
  job.count = count;
  memcpy(job.writes, writes, sizeof(PageWrite) * count);
  if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
    Serial.printf("[reader] RFID2 busy - write #%u refused\n", id);
    queueResult(id, WS_NO_TAG, 0);
  }
}

bool beginRfid2() {
  s_jobs = xQueueCreate(RFID_JOB_DEPTH, sizeof(WriteJob));
  if (!s_jobs) {
    Serial.println("[reader] no memory for the RFID2 job queue");
    return false;
  }
  // First init here, before the task exists, so begin() can report the chip.
  const bool chip = rfidChipInit();
  s_chipOk.store(chip);
  t_lastCheckMs = millis();
  if (chip) {
    rfidPostLink(true);
  } else {
    Serial.println("[reader] RFID2 not found at 0x28 - will keep looking every 2 s");
  }
  if (xTaskCreatePinnedToCore(rfidTask, "reader_rfid2", RFID_STACK, nullptr, RFID_PRIO, &s_rfidTask, 0) != pdPASS) {
    Serial.println("[reader] can't start the RFID2 task");
    s_rfidTask = nullptr;
    s_chipOk.store(false);
    return false;
  }
  return chip;
}

const char *nameOf(uint8_t backend) {
  switch (backend) {
    case READER_RFID2: return "rfid2";
    case READER_NONE:  return "none";
    default:           return "plinth";
  }
}

// Serial-only health line (every STATS_EVERY_MS from poll()).
void logStats() {
  if (s_backend == READER_PLINTH && s_espnowOk) {
    Serial.printf("[reader] plinth stats: rx %lu (queue-dropped %lu, ignored %lu), tx ok %lu fail %lu err %lu, "
                  "events dropped %lu, link %s\n",
                  (unsigned long)s_rxCount.load(), (unsigned long)s_rxDropped.load(),
                  (unsigned long)s_rxIgnored.load(), (unsigned long)s_txOk.load(), (unsigned long)s_txFail.load(),
                  (unsigned long)s_txErr.load(), (unsigned long)s_eventsDropped.load(),
                  plinthOnline() ? "up" : "down");
  } else if (s_backend == READER_RFID2) {
    Serial.printf("[reader] rfid2 stats: chip %s, polls %lu, I2C errors %lu, events dropped %lu\n",
                  s_chipOk.load() ? "ok" : "MISSING", (unsigned long)s_rfidPolls.load(),
                  (unsigned long)s_drv.errors(), (unsigned long)s_eventsDropped.load());
  }
}

}  // namespace

// ============================================================================
//  public API
// ============================================================================
bool begin() {
  if (s_begun) return s_beginOk;
  s_begun = true;
  if (!s_lock) s_lock = xSemaphoreCreateMutexStatic(&s_lockBuf);

  const uint8_t want = settings::cfg.reader;
  s_backend = (want == READER_RFID2 || want == READER_NONE) ? want : (uint8_t)READER_PLINTH;
  s_nextReqId = (uint16_t)esp_random();
  if (s_nextReqId == 0) s_nextReqId = 1;
  if (!s_idleSet) s_idleBright = settings::cfg.ringBrightness;

  s_events = xQueueCreate(EVENT_DEPTH, sizeof(ReaderEvent));
  if (!s_events) {
    Serial.println("[reader] no memory for the event queue - reader disabled");
    s_beginOk = false;
    return false;
  }

  switch (s_backend) {
    case READER_RFID2:
      s_beginOk = beginRfid2();
      break;
    case READER_NONE:
      Serial.println("[reader] no crystal reader configured (simulate crystals from the service menu)");
      s_beginOk = true;
      break;
    default:
      s_beginOk = beginPlinth();
      break;
  }
  return s_beginOk;
}

bool poll(ReaderEvent &ev) {
  if (!s_events) return false;
  {
    Guard g;
    if (g.ok()) {
      const uint32_t now = millis();
      flushOutbox();
      if (s_backend == READER_PLINTH && s_espnowOk && s_rxq) plinthService(now);
      flushOutbox();
      // One readable line in events.log whenever the reader comes or goes.
      const bool on = online();
      if (on != s_loggedOnline && s_backend != READER_NONE) {
        s_loggedOnline = on;
        vlog::event("reader %s %s", nameOf(s_backend), on ? "online" : "OFFLINE");
      }
      if (ageMs(s_lastStatsMs, now) >= STATS_EVERY_MS) {
        s_lastStatsMs = now;
        logStats();
      }
    }
  }
  return xQueueReceive(s_events, &ev, 0) == pdTRUE;
}

bool online() {
  switch (s_backend) {
    case READER_PLINTH: return s_begun && plinthOnline();
    case READER_RFID2:  return s_chipOk.load();
    default:            return false;
  }
}

bool tagPresent() {
  if (s_sim.valid && s_sim.present && ageMs(s_sim.placedMs, millis()) < SIM_PRESENT_MS) return true;
  switch (s_backend) {
    case READER_PLINTH: return plinthOnline() && s_pl.tagPresent;
    case READER_RFID2:  return s_chipOk.load() && s_rfidPresent.load();
    default:            return false;
  }
}

const char *backendName() { return nameOf(s_begun ? s_backend : settings::cfg.reader); }

uint16_t write(const uint8_t *uid, uint8_t uidLen, const PageWrite *writes, uint8_t count) {
  Guard g;
  const uint16_t id = nextReqId();
  if (!s_events) return id;   // begin() never ran / failed: nobody would read a result
  if (!g.ok()) {
    postWriteDone(id, WS_NO_TAG, 0);
    return id;
  }

  // Same request guards as the plinth, answered locally (faster, and identical on every backend).
  bool ok = uid && uidLen > 0 && uidLen <= TAG_UID_MAX && writes && count > 0 && count <= LINK_MAX_WRITES;
  uint8_t badPage = 0;
  for (uint8_t i = 0; ok && i < count; i++) {
    if (writes[i].page < TAG_FIRST_PAGE || writes[i].page > TAG_LAST_PAGE) {
      ok = false;
      badPage = writes[i].page;
    }
  }
  if (!ok) {
    Serial.printf("[reader] write #%u refused: bad request (count %u, page %u)\n", id, count, badPage);
    queueResult(id, WS_BAD_REQUEST, badPage);
    return id;
  }

  if (simMatches(uid, uidLen)) {   // the service menu's simulated crystal: a perfect reader
    simApply(writes, count);
    queueResult(id, WS_OK, 0);
    return id;
  }

  switch (s_backend) {
    case READER_PLINTH: plinthWrite(id, uid, uidLen, writes, count); break;
    case READER_RFID2:  rfidWrite(id, uid, uidLen, writes, count); break;
    default:            queueResult(id, WS_NO_TAG, 0); break;
  }
  return id;
}

void led(LedMode mode, uint32_t color, uint32_t color2, uint16_t ms, uint8_t speed, uint8_t level) {
  if (s_backend != READER_PLINTH) return;   // rfid2 / none have no light
  Guard g;
  if (!g.ok()) return;
  plinthLed(mode, color, color2, ms, speed, level);
}

void setIdleLight(uint32_t color, uint8_t brightness) {
  Guard g;
  if (!g.ok()) return;
  const bool changed = (color & 0xFFFFFF) != s_idleColor || brightness != s_idleBright;
  s_idleColor = color & 0xFFFFFF;
  s_idleBright = brightness;
  s_idleSet = true;
  // An extra HELLO_ACK (unicast) carries the new idle colour + brightness cap.
  if (changed && s_backend == READER_PLINTH && s_espnowOk && s_pl.known) sendConfig(LT_HELLO_ACK);
}

void ignoreUntilLifted() {
  if (s_backend != READER_PLINTH) return;   // rfid2 posts Placed only for a NEW arrival; sims are lifted by the menu
  Guard g;
  if (!g.ok()) return;
  if (!s_pl.known || !s_pl.tagPresent || !s_pl.tagUidLen) {
    s_pl.quietUidLen = 0;
    return;
  }
  memcpy(s_pl.quietUid, s_pl.tagUid, s_pl.tagUidLen);
  s_pl.quietUidLen = s_pl.tagUidLen;
  Serial.println("[reader] the crystal on the plinth stays quiet until it is lifted");
}

void simulatePlaced(const TagImage &img) {
  {
    Guard g;
    if (g.ok()) {
      s_sim.img = img;
      if (s_sim.img.uidLen > TAG_UID_MAX) s_sim.img.uidLen = TAG_UID_MAX;
      s_sim.valid = true;
      s_sim.present = true;
      s_sim.placedMs = millis();
    }
  }
  ReaderEvent ev{};
  ev.type = ReaderEvt::Placed;
  ev.tag = img;
  if (ev.tag.uidLen > TAG_UID_MAX) ev.tag.uidLen = TAG_UID_MAX;
  postEvent(ev);
  Serial.println("[reader] simulated crystal placed");
}

void simulateLifted() {
  Guard g;
  if (g.ok()) s_sim.present = false;   // keeps the image: simulating it again still replays
}

void simulateButton() {
  ReaderEvent ev{};
  ev.type = ReaderEvt::Button;
  ev.button = 0;
  ev.pressed = true;
  postEvent(ev);
  ev.pressed = false;
  ev.heldMs = 120;
  postEvent(ev);
}

uint32_t lastHeardMs() {
  switch (s_backend) {
    case READER_PLINTH: return s_lastRxMs.load();
    case READER_RFID2:  return s_rfidLastOkMs.load();
    default:            return 0;
  }
}

uint32_t packetsIn() {
  switch (s_backend) {
    case READER_PLINTH: return s_rxCount.load();
    case READER_RFID2:  return s_rfidPolls.load();   // completed poll cycles (a liveness counter)
    default:            return 0;
  }
}

}  // namespace reader
