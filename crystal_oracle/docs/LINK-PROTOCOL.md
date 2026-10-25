# Crystal link — the ESP-NOW protocol between the screen and the plinth

Version 1 (`LINK_VERSION 1`), 2026-10-22 (rev. 2: `TAG_PLACED.readMs`, plinths without ring /
button, the host menu's crystal prep). The wire format is defined in
`firmware/crystal_oracle/crystal_link.h`, and that header is the source of truth.
An identical copy lives in `firmware/crystal_plinth/`. This page explains how the
two boards use it. The screen side is implemented in
`firmware/crystal_oracle/reader.cpp` (plinth backend).

**Changing the protocol:** edit both copies of `crystal_link.h` together and bump
`LINK_VERSION`. `tools/build_assets.py --check` fails if the two copies differ. A
board ignores every packet whose version differs from its own, so a mismatched
pair simply never links. It never misreads a packet.

**Additive changes keep the version.** A change that older firmware already
ignores safely does not bump `LINK_VERSION`, so an old plinth still links. So far:
`LED_DUAL` (mode 9) and the second comet colour of `LED_CHARGING` (2026-10-22, plinth
firmware 1.1.0 = HELLO `fwVersion` 110). A plinth older than 1.1.0 drops mode 9 with
a log line and keeps its current look, and draws both CHARGING comets in `color`.
`LinkTagPlaced.readMs` (2026-10-22, plinth 1.2.0 = `fwVersion` 120): a `uint16_t` appended
to `LT_TAG_PLACED` (172 bytes instead of 170) = how long the plinth took to read pages
3..39, retries included. The screen accepts a TAG_PLACED that is at least
`offsetof(LinkTagPlaced, readMs)` long and reads a missing `readMs` as 0 (*unknown*);
an older screen simply ignores the extra two bytes.

---

## 1. Transport

| | |
|---|---|
| Radio | ESP-NOW v1 (payload ≤ 250 bytes), unencrypted |
| Wi-Fi | STA mode only. Never joins an AP, never scans, power save off (`WIFI_PS_NONE`), config kept in RAM (`WiFi.persistent(false)`, so no NVS/flash writes) |
| Channel | Fixed, the same on both boards: `config.ini [station] channel` (default **1**) |
| Station id | `group` in every header: `config.ini [station] group` (default **1**). Both sides drop packets from another group, so two stations can share a channel |
| Encoding | Packed structs, little-endian (both boards are ESP32) |
| Sizes | header 8 · HELLO 18 · TAG_PLACED 172 (170 from a plinth older than 1.2.0) · TAG_REMOVED 19 · WRITE_RESULT 13 · BUTTON 12 · HELLO_ACK/PING 16 · LED 22 · WRITE_REQ 112 bytes (checked with `static_assert` at build time; the comment in the header says 111 for WRITE_REQ, but it forgot the `count` byte) |

Every packet starts with `LinkHeader`:

```
'C' 'O' | version | type | seq (u16) | group (u16)
```

`seq` is the sender's running counter. It is for logs only, and nobody relies on it.
Receivers check `magic`, `version` and `group` (`linkHeaderOk()`) before anything
else. They also accept packets **longer** than the struct they expect, so a future
field can be appended without breaking anything.

### 1b. The Wi-Fi / WebSocket carrier (plinth `PLINTH_LINK_WIFI 1`, 2026-10-24)

For the **station web app** (`crystal_oracle/station/`, the show on an iPad) the plinth can
carry exactly these packets over a **WebSocket** instead of ESP-NOW (`firmware/crystal_plinth/
plinth_wifi.h`, plinth firmware 1.3.0): the plinth joins the house Wi-Fi as a station
(`wifi_secrets.h`), announces itself as `dragonglow-plinth.local` (mDNS) and serves
`ws://<plinth>:81/`; every packet below travels as one WebSocket **binary** message, byte for
byte the same struct, little-endian, same `LinkHeader` (version + group checks included). What
changes is only what the radio used to do:

| | ESP-NOW | WebSocket |
|---|---|---|
| Discovery / adoption | broadcast HELLO, HELLO_ACK, unicast peer | none: a connected socket is the link; up to 2 browsers at once (every plinth→screen packet goes to all, any may send screen→plinth) |
| Reliability | TAG_PLACED resent on a reported radio failure | TCP: never resent (`placedWatch` off) |
| Standalone | nothing from the screen for 3 s → broadcast HELLO again | nothing from any browser for 3 s (or no socket) → same `standalone` state; the browser PINGs every 1 s, sends HELLO_ACK on connect |
| Back in touch | the crystal still on the reader is re-read and announced again | identical (same code path) |
| Channel / group | both boards on the same channel | channel irrelevant; `group` still checked |
| Status | serial only | also a JSON status for a plain HTTP `GET http://<plinth>:81/` (CORS) |

The browser side is `station/js/link.js` (codec + the screen's `reader.cpp` behaviour: write
resends every 250 ms, 2 s give-up, presence resync from HELLO.tagPresent, ignore-until-lifted).
`station/fake_plinth.js` is a node stand-in for the plinth that speaks the same carrier.

## 2. Packet types

| Type | Dir | Struct | Meaning |
|---|---|---|---|
| `0x01 LT_HELLO` | P→S | `LinkHello` | Heartbeat every 1 s: PN532 ok, button state, tag present, board, firmware, uptime. **Broadcast** until the plinth has an ACK, then **unicast** to the screen. |
| `0x02 LT_TAG_PLACED` | P→S | `LinkTagPlaced` | A tag arrived, or was re-presented. Carries the UID, SAK, ATQA and the **whole page image 3..39**. `readOk = 0` means a partial or failed read. `readMs` (plinth 1.2.0+) = how long the read took. |
| `0x03 LT_TAG_REMOVED` | P→S | `LinkTagRemoved` | The tag left the field (4 missed polls, about 0.4 s). |
| `0x04 LT_WRITE_RESULT` | P→S | `LinkWriteResult` | The answer to a `LT_WRITE_REQ` (§4). |
| `0x05 LT_BUTTON` | P→S | `LinkButton` | Big button pressed (`pressed = 1`) or released (`pressed = 0`, `heldMs` set). Never sent by a plinth built without a button (§6). |
| `0x81 LT_HELLO_ACK` | S→P | `LinkHelloAck` | "I'm the screen, talk to me." Also carries the idle ring look: `idleMode`, `brightness` cap, `idleColor`. |
| `0x82 LT_WRITE_REQ` | S→P | `LinkWriteReq` | An ordered page-write list for the tag on the reader (§4). |
| `0x83 LT_LED` | S→P | `LinkLed` | Ring light mode (§5). |
| `0x84 LT_PING` | S→P | `LinkHelloAck` | Keep-alive from the screen, every 1 s while linked. Same body as HELLO_ACK. |

Types with bit 7 set always travel screen→plinth. The screen drops any of them it
hears, because they come from another screen in the same group.

## 3. Discovery, heartbeat, link state

```
plinth                                   screen
  | HELLO (broadcast)  ------------------->  | first valid packet from an unknown MAC
  |                                          | while no plinth is alive: adopt it
  |  <------------------- HELLO_ACK (unicast)| (add as ESP-NOW peer)
  | HELLO (unicast, every 1 s) ----------->  |
  |  <------------------- PING (every 1 s)   |
  | TAG_PLACED / REMOVED / BUTTON -------->  |
  |  <----------------- WRITE_REQ / LED      |
```

**Screen side (`reader.cpp`):**

* **Adoption.** The first valid packet (right group, plinth→screen type) from an
  unknown MAC makes that MAC *the* plinth, provided no plinth is currently alive.
  It does not have to be a HELLO. This covers a screen reboot: the plinth is still
  sending unicast HELLOs to the screen's MAC, and the screen adopts the sender and
  ACKs it.
* **ACKs.** Every **broadcast** HELLO from the plinth gets a HELLO_ACK. That covers
  first contact, a lost ACK, and a plinth restart. Unicast HELLOs are plain
  heartbeats. A broadcast HELLO from the plinth the screen already has means it
  went standalone (it missed the PINGs for 3 s), so the screen also re-sends the
  last *steady* LED command after the ACK.
* **Second plinth.** While the adopted plinth is alive (heard within 3 s), packets
  from another MAC in the same group are ignored and logged. If the plinth stays
  silent for more than 3 s, the next plinth that speaks is adopted. This is how a
  swapped plinth board takes over.
* **Online and link events.** *Online* means a valid packet was heard in the last
  **3 s** (three missed heartbeats). The show gets `LinkUp` / `LinkDown` events on
  each change. On `LinkUp` the screen re-sends the last *steady* LED command, so a
  plinth that rebooted shows what the show expects. It does the same when a
  heartbeat's `uptimeMs` goes backwards (the plinth restarted).
* **PING.** While linked, the screen sends a PING every 1 s.
* **Presence resync.** HELLO's `tagPresent` repairs a missed REMOVED. If two
  consecutive heartbeats (at least 1 s apart) say "no tag" while the screen thinks
  a tag is there, the screen posts the `Removed` event itself.

**Plinth side (expected):**

* Broadcast HELLO every 1 s until a HELLO_ACK arrives.
* Then send unicast to the ACK's sender, still every 1 s, as the heartbeat.
* Apply `idleMode`, `brightness` and `idleColor` from **both** HELLO_ACK and PING.
  The screen sends an extra HELLO_ACK whenever the idle look changes.
  `brightness` 0 there means a dark ring (`ring_brightness = 0` in config.ini is
  reachable from the service menu).
* **Standalone.** If nothing has arrived from the screen for more than 3 s, go
  standalone: flare locally on placement, breathe when idle (a plinth with a ring), and go
  back to broadcasting HELLO until a new ACK arrives.
* **Back in touch.** When the screen talks again, a tag that is still on the reader is
  re-read and announced again with a fresh `TAG_PLACED` (its standalone one may have been
  lost or dropped as stale). No REMOVED is sent: it never left.

**Re-announcements after the host menu.** The screen's host menu doesn't poll the reader
(except on its *Crystals* page), so after ~3 s in the menu the plinth goes standalone and,
when the menu closes, re-announces a crystal still sitting on it. The screen therefore
calls `reader::ignoreUntilLifted()` when the menu closes: the tag the plinth reported
present is kept **quiet** (its `TAG_PLACED` is not posted to the show again) until a
`TAG_REMOVED` for it, a `TAG_PLACED` of another UID, or a heartbeat saying the reader is
empty. So a crystal prepared on the *Crystals* page (or one that already played before the
menu opened) never plays by accident; lifting it and placing it again plays it normally.

## 4. Tag images and writes

**Reading.** The plinth reads the whole tag on placement: pages 3..39, 10 × 16-byte
READ. It sends the image in one `LT_TAG_PLACED`. It never interprets the token
schema. All game logic lives on the screen (`token.h`).

**Duplicates.** A tag that is removed and placed again produces a new `TAG_PLACED`
for the same UID. The screen just forwards it to the show (unless it is being kept quiet,
§3). A placement that sat unprocessed in the screen's receive queue for more than 15 s is
dropped. That only happens if `poll()` was starved, for example in the service menu.

**Read time.** `readMs` is measured on the plinth from the start of the page reads to the
complete image (each failed 16-byte READ costs a retry with a re-select, a gap a second
sweep after 100 ms), so a crystal that only just couples (glitter in the print, a thick
lid, a tag off-centre) reads noticeably slower before it starts failing. The host menu's
*Crystals* page shows the last one with its read-test tally (`DESIGN.md` §5.8).

**Write request (`LT_WRITE_REQ`).** The request holds the UID of the tag it is
meant for, a `reqId`, and up to 18 `{page, 4 bytes}` entries. The plinth executes
the entries **in order**. The screen always puts the marker page (16) **last**, so
it acts as the commit.

| Step | Rule |
|---|---|
| Validate | `count` must be 1..18 and every page must be 3..39, otherwise `WS_BAD_REQUEST`. Page 2 (lock bytes) and pages ≥ 40 (config / counter) can never be written. |
| UID check | Re-select the tag. No tag → `WS_NO_TAG`. A different UID → `WS_WRONG_TAG`. Nothing is written in either case. |
| Page 3 (CC) | One-way OTP bits. It is written only if the tag's current page-3 byte 0 is not `0xE1`. Otherwise the entry is **skipped**, which is not an error. |
| Each page | `WRITE (0xA2)`, up to **3 tries**, re-selecting between tries. On the first page that still fails, stop with `WS_WRITE_FAIL`, setting `failedPage` and `written` (the number of pages written so far). |
| Verify | Read every written page back and compare. Any difference gives `WS_VERIFY_FAIL` with `failedPage` set. |
| Done | `WS_OK` |

**Idempotency.** The screen may send the *same* request again; it re-sends every
250 ms until it gets an answer. The plinth caches the result by `reqId`, and should
also key the cache on the UID. When a re-sent request arrives, the plinth answers
from that cache and does **not** write again.

The screen starts `reqId` at a random value on every boot and never uses 0. That
way the cache cannot answer a fresh request with a result from before a screen
reboot.

**Screen timing.**

* The screen sends the full 112-byte struct; unused entries are zero.
* It re-sends every **250 ms** until a `LT_WRITE_RESULT` with the same `reqId`
  arrives.
* If no result has arrived **2 s** after the first send, the screen gives up and
  reports `WS_NO_TAG` to the show. The plinth should therefore finish a full
  18-page provisioning, including verify, well within about 1.5 s.
* Results for a `reqId` the screen is no longer waiting on are ignored. These are
  duplicate answers to re-sends.
* If the screen has not adopted a plinth yet, `write()` reports `WS_NO_TAG` at once.

| `WriteStatus` | Meaning |
|---|---|
| 0 `WS_OK` | Every page written and verified |
| 1 `WS_NO_TAG` | No tag on the reader. The screen also reports this when it gets no answer within 2 s. |
| 2 `WS_WRONG_TAG` | A different UID is on the reader |
| 3 `WS_WRITE_FAIL` | A page write failed after 3 tries (`failedPage`) |
| 4 `WS_VERIFY_FAIL` | Read-back mismatch (`failedPage`) |
| 5 `WS_BAD_REQUEST` | Page out of range, or `count` is 0 or larger than 18 |

The screen enforces the same validation locally before sending, so a bad request
never goes on air. The **rfid2** backend (M5 Unit RFID2 on the screen's own I²C
bus) follows exactly the same write rules and status codes without any radio.

**Longer lists (crystal prep).** The host menu's *Crystals* page may need more than 18
pages (*Format* zeroes up to 36: pages 4..39). It sends them as consecutive ordinary
requests, 18 pages each, in list order, waiting for each one's `LT_WRITE_RESULT` (2.5 s
backstop) before the next, and stops at the first failure. The marker page is the last
entry of the whole list, so it is in the last request. Nothing changes on the plinth.

## 5. LED ring (`LT_LED`)

`LinkLed` has these fields:

| Field | Meaning |
|---|---|
| `mode` | One of the `LedMode` values below |
| `speed` | Mode-specific; 0 = default |
| `level` | Fill for `LED_METER`, 0..255 |
| `brightness` | 0 = keep the cap from HELLO_ACK / PING; non-zero caps this command only |
| `color`, `color2` | `0xRRGGBB` |
| `ms` | Duration of a timed effect; 0 = until the next command |

| Mode | Look |
|---|---|
| 0 `LED_OFF` | Dark |
| 1 `LED_IDLE` | Slow breathe in `color` (attract). The idle look from HELLO_ACK uses this mode. |
| 2 `LED_FLARE` | Instant bright flash in `color`, decaying over `ms`. The plinth also does this on its own the moment a tag lands, before the screen knows. |
| 3 `LED_CHARGING` | Swirling chase of two comets in `color`, speed from `speed`. `color2` ≠ 0: the second comet is in `color2` (a two-colour crystal; plinth 1.1.0+) |
| 4 `LED_REVEAL` | Burst in `color`, then settles to `color2` |
| 5 `LED_SLEEPY` | Slow dim blink in `color` ("place it again") |
| 6 `LED_RAINBOW` | Gentle rainbow drift (no-crystal blessing, milestone) |
| 7 `LED_SOLID` | Steady `color` |
| 8 `LED_METER` | Fills `level`/255 of the ring in `color` (charge meter) |
| 9 `LED_DUAL` | Two colours, slowly crossfading round the ring: one soft lobe of `color` and one of `color2` drift round once every ~3 s (`speed` 1..255 = 6 s .. 1.2 s), so every LED fades `color` → `color2` → `color`. `color2` = 0 means `color` only. For a two-colour (gradient) crystal. Plinth 1.1.0+ |

**Crystal colours on the ring.** A crystal colour (words `color.N_hex`) is a steady
`#RRGGBB`, a 2–4 stop gradient (`#A35CFF,#2F7BFF`) or `rainbow`. The screen sends
every crystal-coloured command through one helper (`crystalLed()` in `show.cpp`):

| Moment | Steady colour | Gradient (2–4 stops) | Rainbow |
|---|---|---|---|
| Steady (`LED_IDLE` attract cycle, `LED_SOLID` absorb / greet / blessing / keeper) | that mode | `LED_DUAL`(stop 1, stop 2) | `LED_RAINBOW` |
| Charging (`LED_CHARGING` awaken / hold still) | `LED_CHARGING` | `LED_CHARGING`, comets in stop 1 and stop 2 | `LED_RAINBOW` |
| One-shots and the rest (`LED_REVEAL` settle colour, `LED_METER`, `LED_SLEEPY`) | the colour | the colour of that moment (the screen's live blend) | the hue of that moment |

The plinth's idle colour (HELLO_ACK / PING `idleColor`) stays one fixed colour:
crystal colour 1's first stop.

**Screen rules.** The show may call `reader::led()` every frame, so the screen
does **not** re-send a command identical to the last one it sent, with three
exceptions:

* the radio reported a failed send since then;
* the command is a one-shot (`LED_FLARE` / `LED_REVEAL`) re-triggered at least
  250 ms later;
* the link just came up, the plinth restarted, or it broadcast a HELLO (went
  standalone). In that case the last *steady* command is re-sent; a timed command
  (`ms` ≠ 0) only if its time hasn't run out yet.

**Plinth without a ring** (`PLINTH_HAS_RING 0`, the 2026 v7 plinth): `LT_LED` packets are
accepted and ignored (the requested mode is only remembered for the log and the status
line); HELLO_ACK / PING still carry and set the idle look. The screen doesn't know or care:
it sends the same commands either way.

**Plinth rules.** A command identical to the current one must not restart a steady
animation; it may arrive as a refresh. Timed effects end in the idle look. A mode
the plinth doesn't know (a newer screen) is ignored: the ring keeps its look. Since
plinth 1.1.0 a new steady look (another mode or colour) crossfades in over ~450 ms
(`RING_XFADE_MS`), so the attract cycle's IDLE → DUAL → RAINBOW changes never snap;
`LED_FLARE` / `LED_REVEAL` still start at once. Before any plinth is adopted,
`led()` does nothing.

## 6. Button (`LT_BUTTON`)

The plinth sends one packet on press (`pressed = 1`) and one on release
(`pressed = 0`, `heldMs` = how long it was held), debounced on the plinth.
`button` 0 is the big plinth button. The show uses the press: it charges the
crystal during CHARGE, and when idle it starts the no-crystal blessing.

**Plinth without a button** (`PLINTH_HAS_BUTTON 0`, the 2026 v7 plinth): no `LT_BUTTON` is
ever sent and HELLO's `buttonDown` is always 0. The screen is told by its own setting,
`config.ini [station] button = no` (the default): a tap on the screen then gives the
blessing (`DESIGN.md` §5.7). The screen still handles an `LT_BUTTON` if one arrives (the
service menu's simulated *Blessing*, the plinth's serial `b` test).

## 7. Troubleshooting

* **Boot log.** The screen prints its own MAC, channel and group:
  `[reader] ESP-NOW up: screen XX:..., channel 1, group 1`. When the plinth is
  adopted it prints `[reader] plinth XX:... adopted`; this also goes to
  `/oracle/logs/events.log`.
* **Counters.** Every 5 minutes the serial console shows rx and tx counters:
  `[reader] plinth stats: ...`. Many `ignored` packets point to a group/version
  mismatch or a second screen. Many `tx fail` mean the plinth is out of range or
  off.
* **Never links.**
  * Check that both boards use the same channel and group.
  * Check that both `crystal_link.h` copies are identical (`build_assets.py --check`).
  * Check that nothing on either board connects to Wi-Fi or scans; either one
    moves the radio off the channel.
* **Service menu.** The *Reader* page shows online, last-packet age and the packet
  count. *Simulate crystal* injects a placement that behaves like a perfect reader:
  writes to its UID succeed and update the simulated image. The *Crystals* page polls the
  reader itself, so the link stays up while it is open; it shows each placement's read
  result and `readMs`, and its writes go through the normal `LT_WRITE_REQ` path.
* **Plinth status LED** (AITRIP, GPIO2, visible through the closed v7 plinth): fast blink
  = PN532 missing, slow 1 Hz blink = searching for the screen, steady = linked, a flicker
  every 0.5 s = linked with a crystal on it.
