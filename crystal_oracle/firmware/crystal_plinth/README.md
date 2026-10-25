# Crystal Plinth (the Wakestone)

The stone under the crystal. In 2026 it is the owner's **v7 plinth**
(`../../plinth/crystal_plinth_v7.scad`, review `../../plinth/crystal_plinth_REVIEW.md`, renders
`v7_*.png`): a crystal-crowned altar with an **AITRIP ESP32-WROOM-32** on its screw-terminal
board inside and an **Elechouse PN532 NFC V3** under a flush 2 mm printed lid on top. It talks to
the Crystal Oracle screen over ESP-NOW (`crystal_link.h`). It is deliberately dumb and fast: the
screen makes every game decision, the plinth does tag I/O.

**No LED ring and no big button this year** (owner, 2026-10-22). Both stay in the firmware as
compile-time options (`PLINTH_HAS_RING`, `PLINTH_HAS_BUTTON`, default 0, section 3); the screen runs
with `config.ini [station] button = no`, so kids without a crystal tap the screen instead.

* Sketch: `crystal_plinth.ino` (v1.2.0). All the tuning knobs are at the top; search for `TUNE`.
* Protocol: `crystal_link.h`. This must stay **byte-identical** to
  `../crystal_oracle/crystal_link.h`, and `tools/build_assets.py --check` fails if the two copies differ.
  If you change one, change both (and bump `LINK_VERSION` unless the change is additive).
* Protocol description: `../../docs/LINK-PROTOCOL.md`. The screen side is
  `../crystal_oracle/reader.cpp` (plinth backend); this sketch matches it.
* **Crystal prep happens on the screen**: the AITRIP board is the same one `nfc_tag_bench` runs on,
  and the screen's hidden host menu (hold the top-left corner 3 s) → **Crystals** reads, colours,
  provisions, clears and formats crystals *through this plinth* (`../../docs/DESIGN.md` §5.8). No
  web bench needed at the door.
* Status: compiles for the AITRIP and the Feather, with and without ring + button (esp32 core
  3.3.10, Adafruit PN532 1.3.4, Adafruit NeoPixel 1.15.5 for ring builds). **Not yet run on hardware.**

---

## 1. Parts (the 2026 build)

| Part | Notes |
|---|---|
| Controller | **AITRIP ESP32-WROOM-32** 30-pin ("ESP32 Dev Module") on its ~63 × 66 mm **screw-terminal carrier**, the board `nfc_tag_bench` uses |
| NFC reader | **Elechouse PN532 NFC Module V3** (42.7 × 40.4 mm; HiLetgo's "V3" clone is the same) in **I2C mode**: DIP **SW1 ON, SW2 OFF** |
| Wires | 6 jumpers from the reader to the terminal board, down the v7 wire slot |
| Plinth | the v7 print: `body`, `cover`, `nfc_lid` (2 mm), the 11 crystal clusters + their D-`pin`s (parts in the `.scad`) |
| Power | a 5 V USB supply (1 A is plenty without a ring) into the board's **USB-C**, through the plinth's front port |

Optional (not fitted in 2026): a WS2812B ring + 330 Ω + 1000 µF, and a momentary arcade button
(section 3).

## 2. Wiring: Elechouse PN532 V3 → AITRIP terminal board

Exactly the `nfc_tag_bench` wiring, so one board does both jobs. All six wires land on the 3V3
side of the 30-pin board; on the terminal carrier use the screw terminals marked with these GPIO
numbers.

| PN532 V3 pin | Terminal board | Notes |
|---|---|---|
| VCC | **3V3** | Keeps the reader's I2C lines at 3.3 V |
| GND | **GND** | |
| SDA | **GPIO21** | I2C data |
| SCL | **GPIO22** | I2C clock |
| IRQ | **GPIO16** [RX2] | Wired for compatibility only (I2C mode polls the RDY byte) |
| RSTO | **GPIO17** [TX2] | Reset (pulsed at start-up and when the reader stops answering) |
| DIP switches | **SW1 ON, SW2 OFF** | = I2C. Any other setting: `nfc: PN532 NOT found` on the serial console |

On the Elechouse V3, VCC / GND / SDA / SCL are on the 4-pin header and IRQ / RSTO on the 8-pin
(SPI-side) header; check the silk labels on your board.

* **Power:** the board's **USB-C** port, which sits behind the plinth's front port (the v7 cavity
  puts the board's USB edge 2 mm from the front wall). A 5 V phone charger is enough.
* **Status light:** the AITRIP's onboard **blue LED (GPIO2)**, no wiring. It glows through the
  closed plinth (first match wins):

  | LED | Meaning |
  |---|---|
  | fast blink (5 Hz) | the PN532 isn't answering (wiring, DIP switches) |
  | slow blink (1 Hz) | searching for the screen (standalone: screen off, out of range, or a channel / group mismatch) |
  | steady on | linked to the screen |
  | on, a short flicker every 0.5 s | linked, and a crystal is on the reader |

## 3. Build options: ring and button

At the top of the sketch (or on the command line):

```c
#define PLINTH_HAS_RING   0   // 1 = a WS2812B ring on PIN_RING (needs the "Adafruit NeoPixel" library)
#define PLINTH_HAS_BUTTON 0   // 1 = the big arcade button on PIN_BUTTON
```

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 \
  --build-property "compiler.cpp.extra_flags=-DPLINTH_HAS_RING=1 -DPLINTH_HAS_BUTTON=1" crystal_plinth
```

* **0 (the default, the v7 plinth):** no NeoPixel code runs and the NeoPixel library isn't even
  needed. `LT_LED` commands from the screen are accepted and ignored (the requested mode only shows
  up in the log and the status line). The button pin is never read, no `LT_BUTTON` is ever sent and
  HELLO's `buttonDown` is always 0.
* **With a button** also set the screen's `config.ini [station] button = yes`: the plinth's button
  then gives the no-crystal blessing and the screen shows / says the button words and lines.

### Pins for the optional parts

| | AITRIP | Adafruit Feather ESP32-S3 (the old Skull Oracle board) |
|---|---|---|
| PN532 VCC / GND | 3V3 / GND | **3V** / GND (or the STEMMA QT jack; the sketch switches on its power gate) |
| PN532 SDA / SCL | GPIO21 / GPIO22 | **SDA / SCL** (GP3 / GP4) |
| PN532 IRQ / RSTO | GPIO16 [RX2] / GPIO17 [TX2] | **GP6** [D6] / **GP9** [D9] |
| Ring DIN | **330 Ω → GPIO4** [D4] | **330 Ω → GP10** [D10] (the skull's old eyes pin) |
| Ring 5V / GND | VIN (5 V on USB) or the 5 V supply / GND, **1000 µF** across them at the ring | **USB** pin / GND, **1000 µF** at the ring |
| Button | **GPIO23** [D23] ↔ GND | **GP12** [D12] ↔ GND |
| Status light | onboard blue LED (GPIO2) | onboard NeoPixel: red fast blink = PN532 missing, violet = tag, blue blink = standalone, green = linked |

> ⚠ **On the Feather the button is on GP12, not the skull's GP13.** GP13 also drives the Feather's red
> LED (LED to GND). With a button wired to GND there, the LED clamps the pin's weak internal pull-up
> to about 1.6 V, which is neither HIGH nor LOW, so the button reads *pressed* all the time or
> chatters. Move the skull's outside-button wire from D13 to **D12**. If it really has to stay on
> GP13: wire the button between **GP13 and 3V** (not GND), `#define PIN_BUTTON 13` and
> `#define BUTTON_ACTIVE_HIGH 1`; the red LED then lights while the button is pressed. The serial
> line *"button: reads PRESSED at boot"* means stuck or miswired.

* **Ring:** a 24-px WS2812B ring (about 72 mm inside diameter) goes **around** the antenna, never
  over it. The ring's data input is driven at 3.3 V into a 5 V ring: usually fine with the 330 Ω and a
  short wire; if the first pixels flicker, add a 74AHCT125 level shifter. `RING_MAX_BRIGHTNESS`
  (120/255) and `RING_MAX_MA` (500 mA, a per-frame estimate) keep its current down; on the screen's
  P1 5V terminal (≤ 0.5 A for everything) lower `RING_MAX_MA` to about 150. Use a 2 A supply.
* **Moving pins:** `#define` any of `PIN_SDA`, `PIN_SCL`, `PIN_PN532_IRQ`, `PIN_PN532_RST`,
  `PIN_RING`, `PIN_BUTTON`, `PIN_STATUS` above the board block in the sketch.
* **Pin map selection:** the Feather map is used for any of the four Feather ESP32-S3 boards
  (`ARDUINO_ADAFRUIT_FEATHER_ESP32S3`, `_NOPSRAM`, `_TFT`, `_REVTFT`); every other classic ESP32
  board gets the AITRIP map; any other chip stops the build with an error.
* **Grounds:** all grounds common.

## 3b. Wi-Fi / WebSocket mode: the plinth for the iPad station app (2026-10-24)

The same plinth can serve the **station web app** (`../../station/`, the show in a browser on
an iPad) instead of the Waveshare screen. Nothing changes in the hardware or the wiring; the
firmware's link carrier does:

```c
#define PLINTH_LINK_WIFI  1   // top of crystal_plinth.ino (or -DPLINTH_LINK_WIFI=1)
```

1. Copy `wifi_secrets.example.h` to **`wifi_secrets.h`** (git-ignored) next to the sketch and
   fill in `WIFI_SSID` / `WIFI_PASS` (`PLINTH_HOSTNAME` = the mDNS name, default
   `dragonglow-plinth`). The build stops with a clear `#error` if the file is missing.
2. Flash as usual (hold BOOT on the AITRIP). The serial console prints `wifi: connected, IP ...`
   and the WebSocket URL; every 10 s status line shows the IP, "waiting for a browser" / "browser(s)
   via", and the client count.
3. Check from any browser on the same Wi-Fi: `http://<plinth IP>:81/` answers a JSON status
   (`readerOk`, `tagPresent`, `uid`, `clients`, `standalone`, uptime, heap).
4. In the station app enter the plinth address (`dragonglow-plinth.local:81` or the IP).

Implementation: `plinth_wifi.h` (a small RFC 6455 server on `WiFiServer`, no library to install;
`mbedtls` SHA-1/base64 for the handshake, ESPmDNS for the name). The packets are the unchanged
`crystal_link.h` structs as WebSocket binary messages (`../../docs/LINK-PROTOCOL.md` §1b). The
blue LED keeps its meaning (slow blink = no browser connected, steady = linked, flicker = a
crystal on it). `PLINTH_LINK_WIFI 0` (the default) is the 2026 ESP-NOW rig, unchanged. Both
builds compile (esp32 core 3.3.10; Wi-Fi build = 75 % of the 1.3 MB app partition).
**Not yet run on hardware** (2026-10-24): tested against `station/fake_plinth.js` only.

## 4. Physical build (v7)

* **Controller:** the terminal board sits in the 92 × 74 mm cavity, located by the four L-shaped
  corner pieces on the cover (a 66.6 mm frame: either orientation fits), its **USB-C edge toward
  the front port** (x ±11 mm, z 13–32 mm). The wired terminal edges get ~13 mm each side. There are
  no standoffs yet (the mounting holes aren't measured).
* **Reader:** in the fitted **NFC pocket** under the lid (60.5 × 58.2 mm, floor 7 mm below the top),
  lying flat, pushed against the pocket's **−x / −y walls** (front-left, seen from the front) by the
  two locating bars, its right-angle plugs facing **+x / +y**, where the 8 × 48 mm **wire slot**
  drops into the cavity. The plugs make it about 4 mm thick; the lid clears 5 mm: check it seats
  before closing.
* **Lid:** the **2 mm** `nfc_lid` sits flush on a 2 mm ledge (friction fit, a finger notch to lift
  it). The reader reads through it. Non-metallic only: no screws, foil, metallic paint or glitter
  over the coil.
* **Where the crystal goes:** the reader's centre is about **8.5 mm left of and 8.5 mm in front of
  the lid's centre**. Mark that spot on the lid (a painted swirl, a shallow ring) so every crystal
  lands over the antenna.
* **Test the range through the lid:** screen host menu → **Crystals** → place a finished crystal a
  few times: the page counts good reads / placements and shows the last read time. Every placement
  should be a good read; read times that climb mean a weak coupling (glitter PLA under the tag, a
  thick floor, off-centre). Do this before printing a batch of crystals.
* **Height:** the plinth top sits about 75–85 cm high.

## 5. Flashing

1. Board package: Espressif **esp32** core 3.x (built against 3.3.10). Library (Library Manager):
   **Adafruit PN532** (it pulls in Adafruit BusIO); **Adafruit NeoPixel** only for ring builds.
2. Board: **AITRIP: "ESP32 Dev Module"**, FQBN `esp32:esp32:esp32`, with the defaults.
   (Feather: "Adafruit Feather ESP32-S3 2MB PSRAM", FQBN `esp32:esp32:adafruit_feather_esp32s3`;
   USB CDC On Boot = Enabled, USB Mode = USB-OTG (TinyUSB); the `_nopsram`, `_tft` and `_reversetft`
   Feathers use the same pins. Its build prints *"ISO C++11 requires whitespace after the macro
   name"* for every file: that is the board's own `-DARDUINO_PARTITION_tinyuf2-partitions-4MB-noota`
   define, harmless.)
3. Compile and upload with arduino-cli:

   ```bash
   arduino-cli compile --fqbn esp32:esp32:esp32 crystal_plinth
   arduino-cli upload  -p COMx --fqbn esp32:esp32:esp32 crystal_plinth
   ```

If the upload won't connect: **AITRIP:** hold **BOOT** while the uploader says "Connecting...".
**Feather:** hold **BOOT**, tap **RESET**, release BOOT, upload again, then press RESET.

**The same board as the bench.** To use the web bench instead (e.g. a big batch of crystals at the
desk), flash `../../../nfc_tag_bench` onto this board, and flash the plinth back afterwards. Both
use the same PN532 wiring.

**Station settings.** `LINK_GROUP` and `LINK_CHANNEL` (default 1 and 1, from `crystal_link.h`)
must match the screen's `config.ini [station] group` and `channel`.

## 6. What it does

| Event | Plinth |
|---|---|
| Crystal placed | 1. (Ring builds: the ring **flares at once**, a white burst cooling to violet over ~0.6 s, before anything else.)<br>2. It reads pages 3–39 with ten 16-byte READs (about 0.2 s). A chunk that fails is retried 3 times with a re-select in between, then once more after 100 ms.<br>3. It sends `LT_TAG_PLACED` (the full 172-byte struct) with the UID, `readOk`, SAK, ATQA low byte, the page image and **`readMs`** (how long the read took, retries included). If the radio reports that this unicast wasn't delivered, it sends it again (up to 2 more times). |
| Crystal lifted | After 4 polls in a row without the tag (about 0.4 s) it sends `LT_TAG_REMOVED`. If one crystal is swapped straight for another, it sends REMOVED and then PLACED. |
| `LT_WRITE_REQ` | 1. It checks the request: `count` 1..18, every page 3..39, UID length 1..10. Otherwise `BAD_REQUEST` (`failedPage` = the bad page).<br>2. It re-selects the tag and compares the UID: no tag → `NO_TAG`, another UID → `WRONG_TAG`. Nothing is written in either case.<br>3. It writes each page in order (`WRITE 0xA2`), with 3 tries each and a re-select between tries. Page 3 first gets the one-time-programmable check (below).<br>4. It reads every written page back (16-byte READs, one per 4 pages) and compares it.<br>5. It replies `LT_WRITE_RESULT`: `OK` / `NO_TAG` / `WRONG_TAG` / `WRITE_FAIL` / `VERIFY_FAIL` / `BAD_REQUEST`, plus `failedPage` and `written`. |
| `LT_LED` | Ring builds: the ring mode (below). Without a ring: accepted and ignored. |
| Button | Button builds only: debounced at 30 ms; `LT_BUTTON` on press, and again on release with `heldMs`; a ring sparkles at once. A button held for more than 60 s is treated as stuck (a release is sent, and it is ignored until it really lets go). |
| Heartbeat | `LT_HELLO` every second: broadcast until the screen answers, then unicast to the screen. It carries `readerOk`, `buttonDown` (always 0 without a button), `tagPresent`, the board id, the firmware version (120 = 1.2.0) and `uptimeMs` (the screen uses `tagPresent` to repair a lost REMOVED, and `uptimeMs` to notice a plinth restart). |
| Screen answers | Any packet from the screen in our group (`HELLO_ACK`, `PING`, `LED`, `WRITE_REQ`) makes it the unicast peer. `HELLO_ACK` and the once-a-second `PING` both carry the idle look (`idleMode`, `idleColor`, `brightness` cap). |
| No screen | If nothing arrives from the screen for **3 s** (or it was never found), the plinth goes standalone (status LED: slow blink; a ring breathes violet, flares on a crystal and gives a sleepy blink for 3 s if a tag can't be read). HELLO goes back to broadcast until the screen answers again; then a crystal still on the reader is read and announced again (the screen keeps a crystal it handled in its host menu quiet until it is lifted). |

**Write-request details:**

* **Page 3 is protected.** Page 3 is the NDEF capability container, and its bits are
  one-time-programmable. When the list contains page 3, the plinth reads it first and **skips
  the write if byte 0 is already 0xE1** (not an error, and not verified). If page 3 can't be
  read, it isn't touched (`WRITE_FAIL`, page 3).
* **`written`** counts the pages actually written before the plinth stopped (a skipped page 3
  doesn't count). If the tag leaves or changes during the writes or the read-back, the result
  is `NO_TAG` or `WRONG_TAG` with `failedPage` set, the same codes as the screen's rfid2 backend.
* **Time limit.** A write list that is still running after 1.8 s stops with `WRITE_FAIL` (or
  `VERIFY_FAIL` during the read-back). The screen stops waiting at 2 s. A full 18-page
  provisioning normally takes about 0.4–0.5 s. (The host menu's *Format* sends up to 36 pages as
  two such requests, one after the other.)
* **Results are cached.** The last 4 results are cached by `reqId` together with a hash of the
  UID and the page list. The screen re-sends a request every 250 ms until it has an answer;
  those resends are answered from the cache without writing again. A rebooted screen that
  reuses a `reqId` for a different request is not mistaken for a resend.

### Ring modes (`LT_LED`, ring builds only)

For every mode, `speed` 0 means the default; 1..255 runs from slow to fast. A non-zero `ms`
makes the effect fall back to the idle look (from HELLO_ACK / PING) when it ends. `brightness`
0 keeps the cap from HELLO_ACK / PING; a non-zero value caps this command only.

| Mode | Look |
|---|---|
| `LED_OFF` | Dark |
| `LED_IDLE` | Slow breathe in `color` (4 s by default), with a faint brighter patch wandering around the ring |
| `LED_FLARE` | Instant full flash, white cooling to `color`, decaying over `ms` (default 600 ms), then idle |
| `LED_CHARGING` | Two comets swirl around the ring with white-hot heads; `speed` sets the laps per second (0.35–3.5, default 1.1). A non-zero `color2` paints the second comet in `color2` (a two-colour crystal) |
| `LED_REVEAL` | White-hot burst and sparkles, then a ripple runs both ways from the top, then it settles into a soft breathe in `color2` (`color2` = 0 means use `color`) |
| `LED_SLEEPY` | Slow, dim, drowsy blink in `color` with a dark pause |
| `LED_RAINBOW` | Gentle rainbow drifting around the ring |
| `LED_SOLID` | Steady `color` |
| `LED_METER` | Fills `level`/255 of the ring clockwise from the top. Empty slots glow faintly. The fill slides smoothly to new levels, and a full meter pulses. |
| `LED_DUAL` | Two colours crossfading slowly round the ring (a two-colour gradient crystal such as Dragonberry's deep/light purple): one soft lobe of `color` and one of `color2` drift round once every 3 s by default (`speed` 1..255 = 6 s .. 1.2 s), so each LED fades `color` → `color2` → `color`. `color2` = 0 means `color` only. Added in 1.1.0 |

* **Repeats are refreshes.** A command identical to the one running (same mode, colours, speed,
  level, brightness and `ms`) changes nothing: a steady animation doesn't restart, and a timed
  one keeps its original end. FLARE and REVEAL are one-shots: the screen only repeats them on
  purpose, so a repeat plays them again.
* **Parameter changes are smooth.** The same continuous mode with new parameters (CHARGING
  with a new `speed`, METER with a new `level`, IDLE in a new colour) keeps the animation
  phase, so nothing jumps.
* **New looks crossfade (1.1.0).** A new steady look (another mode, or new colours) fades in
  from what the ring was showing over `RING_XFADE_MS` (450 ms; 0 = hard cut), so the attract
  cycle through the crystal colours (IDLE breathe → DUAL → RAINBOW) never snaps. FLARE and
  REVEAL still start at once.
* **Unknown modes are ignored.** A mode number this firmware doesn't know (a newer screen) is
  logged (`led: unknown mode N ignored`) and the ring keeps its look; as an idle mode it
  means IDLE.
* **The idle look.** HELLO_ACK and every PING carry `idleMode`, `idleColor` and the
  `brightness` cap. While the ring is showing the idle look (after a timed effect, after
  standalone, at boot) it follows a change at once; a commanded mode stays until the next
  command. `idleMode` FLARE or REVEAL is treated as IDLE, and an `idleColor` of 0 means violet.
  A `brightness` of 0 really means a dark ring (the screen's `ring_brightness = 0`).
* **Brightness and gamma.** Brightness is applied *after* gamma: 2.6, the same curve as
  `Adafruit_NeoPixel::gamma32`, computed in float. That way the cap is a true current ceiling
  and dim fades don't round away.

## 7. Serial monitor (115200)

Typical lines (the 2026 build: no ring, no button):

```
    0.214  Crystal Plinth v1.2.0 on ESP32 dev (AITRIP) | no ring | no button (screen: button = no) | group 1, channel 1
    0.301  nfc: PN532 ready (firmware 1.6)
    0.355  link: ESP-NOW up, plinth MAC 34:85:18:xx:xx:xx, channel 1, group 1 - broadcasting HELLO until the screen answers
    2.004  link: screen 7C:DF:A1:xx:xx:xx - unicast from now on
    2.005  link: idle look from the screen (fw 100): IDLE #7C3AED, brightness cap 120 (plinth max 120)
   14.120  tag: placed 04:A1:B2:C3:D4:E5:80 (ATQA 0044 SAK 00) read OK in 186 ms | p16 53 4B 03 02
   15.902  write #4711: 2 page(s) -> OK (written 2, failed page 0) in 74 ms
   16.140  write #4711: resend - answered from cache (status 0)
   19.660  tag: removed 04:A1:B2:C3:D4:E5:80
   30.000  status: up 30 s | link screen 7C:DF:... (heard 412 ms ago) | reader ok | tag none | polls 341 ... | ring none, asked IDLE cap 120 ...
```

Messages to know:

* `nfc: PN532 NOT found - check wiring + DIP switches (SW1 ON, SW2 OFF)`: the plinth keeps
  running and looks for the reader again every 3 s.
* `nfc: PN532 stopped answering polls - resetting it`: 15 polls in a row got no valid answer,
  so the plinth reset and re-initialised the PN532. If most of them were timeouts (the PN532
  is alive but slow), `nfc: poll timeout raised to N ms` follows (it doubles, up to 200 ms).
* `tag: placed ... read OK in N ms`: the read time the screen's *Crystals* page shows. Reads that
  take much longer than ~200 ms, or `read INCOMPLETE`, mean weak coupling.
* `link: nothing from the screen for 3 s - standalone ring, broadcasting HELLO again`: the
  screen is off, rebooting, out of range, or not polling (its host menu, except the *Crystals*
  page). It re-links on its own.
* `link: TAG_PLACED not acknowledged - sending it again (N)`: the radio reported a failed
  delivery right after a placement. Occasional ones are harmless; many mean poor range.
* `link: ignoring packets with link version N`: the screen and plinth were built from
  different `crystal_link.h` versions.
* `led: <MODE> ...`: printed when the screen switches the ring to another mode (also without a
  ring: it shows what the screen asked for; set `DEBUG 2` to see every LED command).
* `button: reads PRESSED at boot` (button builds): stuck or miswired (on the Feather: still on
  GP13?). See the button note in section 3.

Single-letter commands (type them in the serial monitor):

| Key | Does |
|---|---|
| `s` | Status line now |
| `d` | Hex/ASCII dump of the last tag image (pages 3..39) |
| `f` | Local flare (ring builds) |
| `m` | Step the ring through every LedMode (ring builds; a ring test without a screen) |
| `b` | Send a fake button press and release to the screen (it gives the blessing, button or not) |
| `h` | Help |

## 8. Behaviour the screen relies on (and vice versa)

Both sides follow `docs/LINK-PROTOCOL.md`; the screen side is `reader.cpp`.

* **The screen must keep talking.** The screen PINGs every second while linked. More than 3 s
  without *any* packet from the screen = standalone + broadcast HELLO (the screen's own link
  timeout is also 3 s).
* **Re-linking.** The screen ACKs every *broadcast* HELLO, so a plinth that restarted or went
  standalone re-links within a second of the screen hearing it. After a screen reboot the
  plinth's unicast HELLOs (or, after 3 s, its broadcasts) get the screen to adopt it again.
* **Re-announcing.** Back in touch after standalone, a crystal still on the reader is read and
  sent again (`TAG_PLACED`). The screen keeps a crystal it already handled in its host menu quiet
  (`reader::ignoreUntilLifted()`) until it is lifted, so a prepared crystal never plays by itself.
* **Every screen packet adopts the sender.** Any screen-to-plinth packet from a screen in the
  plinth's group makes that MAC the plinth's unicast peer (covers a lost HELLO_ACK). There is
  one screen per group.
* **Ignored packets.** Packets from other groups, packets with another `LINK_VERSION`, and
  plinth-to-screen types from other plinths are ignored. Longer packets than expected are
  accepted (future fields): a screen older than this plinth reads TAG_PLACED without `readMs`, and
  this screen reads an older plinth's 170-byte TAG_PLACED with `readMs` = 0.
* **Write timing.** Writes run inside `loop()`. A full provisioning (18 pages plus read-back)
  takes about 0.4–0.5 s, well inside the screen's 2 s window; the plinth stops by itself at 1.8 s.
  The screen's 250 ms resends that queue up meanwhile are answered from the cache.
* **LED dedupe on both ends.** The screen doesn't re-send an identical command unless a send
  failed, a one-shot is re-triggered, or the link came back; the plinth treats an identical
  steady command as a refresh, so either side's repeat never restarts an animation.

## 9. Not verified on hardware yet

* **PN532 frame handling.** The frame-level PN532 code has only been compiled, never run. It
  handles InListPassiveTarget, the 16-byte READ through InDataExchange to target 1, and WRITE
  with a status check. It sends commands with `Adafruit_PN532::sendCommandCheckAck()` (which in
  1.3.4 waits for the ACK *and* for the response to be ready) and reads + checks the response
  frame itself (length checksum, data checksum, D5 / command+1, status byte, 17-byte READ).
  * Why not the library's helpers: in Adafruit PN532 1.3.4, `inDataExchange()` addresses
    `_inListedTag`, which only `inListPassiveTarget()` sets, never `readPassiveTargetID()`
    (so after a normal poll it would address target 0). `readPassiveTargetID()` also hides
    ATQA/SAK and doesn't bound the UID copy, and `mifareultralight_WritePage()` ignores the
    write status (a failed write would look like success).
  * The first bench session should check: placement and removal timing, a full read in about
    0.2 s, a crystal that stays put being seen on every poll, and a provisioning write list
    followed by a read-back (the screen's *Crystals* page shows it; `d` on the serial console
    dumps the image).
* **Through the lid.** Read range and read times of the printed crystals through the 2 mm lid
  (the *Crystals* page read test), and that the reader seats under the lid.
* **Poll timeout.** The poll timeout (50 ms) assumes that `MxRtyPassiveActivation = 2` makes
  an empty poll return quickly. It adapts upward if that assumption is wrong.
* **Ring builds.** Ring colours, levels and the current estimate should be checked with a USB
  power meter, and the read range with the ring fitted around the antenna.
* **Radio.** First boot of each board writes the radio's calibration data to its own NVS
  (ESP-IDF does this by itself, once); the sketch itself never writes flash.
