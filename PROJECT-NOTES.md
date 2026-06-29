# Halloween Props — Skull Oracle — Project Notes

> **Source (git):** GitHub repo `eaglett-cmyk/HalloweenProps` (branch `main`, GPL-3.0).
> Firmware + docs added 2026-06-25. ⚠ **"Deploy" is NOT git-based** — pushing to GitHub
> backs up the source only. "Deploying" means flashing `skull_oracle/skull_oracle.ino` to
> the board from the Arduino IDE. (This repo exists because the original prop's code was
> lost when its Arduino Zero died — git is the backup that prevents a repeat.)

> **Local working copy (user's PC):** `C:\Users\Ciancio\Desktop\Claude projects\Halloween`
> — canonical location; Claude Code reads/writes here. Mirrors the GitHub repo contents.

Orientation file for future Claude Code sessions (or developers). The firmware version
lives in the sketch's header comment; note the date when you change behavior.

## What this is

A motion-and-NFC Halloween prop ("Skull Oracle"): a skull with LED eyes. A hand in its
mouth triggers a thermal-printed label with a random number (1–100). Visitors carrying an
NFC token get a personalized, escalating experience the prop remembers across years —
because the per-visitor history lives **on the token**, not the controller (so a dead or
reflashed board never erases anyone's streak).

## Hardware

- **Controller:** Adafruit Feather ESP32-S3 (4MB Flash / 2MB PSRAM, #5477) — replaces the
  failed Arduino Zero / SAMD21. ROM/UF2 bootloader (hard to brick) + **WiFi/BLE** on board
  for future networked features. ~$17.50.
- **Hand sensor:** Adafruit VCNL4010 IR proximity sensor (I²C 0x13). The original prop's IR
  sensor; "best at 10–150 mm" = skull-mouth geometry. If it ever dies, the modern STEMMA-QT
  equivalent is the VCNL4040 (needs its own library — the VCNL4xxx chips aren't drop-in).
- **NFC:** PN532 in **I²C mode** (0x24), Adafruit breakout or Elechouse V3. SPI fallback is
  wired into the sketch (`PN532_USE_SPI 1`) if I²C ever gets flaky.
- **Printer:** DP-EH400/2 embedded thermal printer — CSN-A2 / "Zijiang" command family, so
  the Adafruit_Thermal library drives it. TTL serial, **3.3 V logic, 5 V-tolerant** (the "5V"
  on its label is the power rail). Self-test = hold the feed button while powering on (prints
  the CP437 table; firmware/baud, if shown, sit at the page edges). Baud is 9600 or 19200 —
  the sketch starts at 9600.
- **Tokens:** NTAG203 NFC tags (SMARTRAC dry inlays on hand) embedded in 3D-printed shells.
  144 bytes user memory — plenty (see layout). Clones are fine here (non-secure, a few writes
  per year); **test every tag on the ACR1252 before sealing it in a shell.**
- **Eyes:** existing LEDs on one GPIO (PWM breathe + flare on a hit).
- **Buttons:** two momentary buttons, each wired between its pin and GND (internal pull-up,
  pressed = LOW). *Inside the mouth* (GP12) adds **+10** to the roll, capped at 100 — checked
  during a ~700 ms window at trigger so the kid has a moment to press it. *Outside the skull*
  (GP13) is a **second way to trigger a roll/print** — the same sequence as a hand in the
  mouth (the hand/proximity sensor remains the primary trigger).
- **Power:** a separate 5–9 V / 2 A+ supply for the printer, **ground shared with the
  Feather** (printers brown out the board mid-print otherwise).
- **Bench tool, not in the prop:** the ACR1252-M1 is a USB/PC-SC reader — desk tag-prep only
  (test + pre-write links). It can't drive the prop; that's the PN532's job.

## Architecture / firmware

- Single Arduino sketch: `skull_oracle/skull_oracle.ino` (C++). Board package: **Espressif
  "esp32" v3.0+** (board: "Adafruit Feather ESP32-S3 2MB PSRAM"). Libraries: **Adafruit
  VCNL4010, Adafruit PN532, Adafruit Thermal Printer Library**.
- Pins (Feather ESP32-S3, where D-label = GPIO #): I²C via STEMMA QT / SDA+SCL (Wire default,
  no pins hardcoded); PN532 IRQ=GP6, RST=GP9; printer on Serial1 using the board's RX/TX pins
  (DTR optional on GP11); eyes=GP10; mouth button=GP12 (+10); outside button=GP13 (2nd trigger).
- Flow: idle eyes breathe → trigger (VCNL4010 proximity over baseline + `PROX_MARGIN`, **or**
  the outside button GP13) → ~700 ms window latches the in-mouth +10 button and reads the
  PN532 for a token → branch (fresh / returning / none) → print → cooldown, re-arm once the
  hand clears and the button is released.
- **Values to set on deploy (`TUNE` in the sketch):** `PRINTER_BAUD`, `PROX_MARGIN`,
  `EVENT_YEAR` (bump every Halloween), `BASE_YEAR`, `PROP_WRITES_URL`.

### Token data layout (NTAG203, user pages 4–39 = 144 bytes)

| Pages | Bytes | Contents |
|-------|-------|----------|
| 4–15  | 48    | NDEF URI record = link to this repo (phone-tappable) |
| 16    | 4     | marker `SK` + schema ver (`0x03`) + visit count (written **last** = commit) |
| 17    | 4     | `charge` = per-token roll modifier (set via the `nfc_tag_bench/` tool) |
| 18–39 | 88    | one page per year: `[number, prizeTier, …]` (22 years) |

The repo link (NDEF) and the prop's private visit data live in **separate** page ranges and
never collide: phones stop at the NDEF terminator; the prop reads/writes raw pages past it.
`PROP_WRITES_URL 1` = the prop writes the link onto a fresh token on first contact (needs a
steady ~1 s hold); set 0 to pre-write links at the bench with the ACR1252 (instant taps).

## Build / deploy

- **Edit:** `skull_oracle/skull_oracle.ino`. **Compile + flash:** Arduino IDE (board =
  "Adafruit Feather ESP32-S3 2MB PSRAM", Espressif esp32 core v3.0+) or arduino-cli. There
  is no separate build artifact — the .ino is the deliverable.
- **Back up to GitHub** (source only): see the README. From a local clone, `./push.sh
  "what changed"` does add + commit + push. Auth is the machine's GitHub credential
  (PAT / SSH / gh CLI) — **not stored in this file.**

## Conventions / gotchas (hard-won)

- The code is in git now *because the original was lost to a dead board* — push every change.
- The printer was useless until it read the printer's real status instead of blind per-line
  delays. That DTR flow control is now built into Adafruit_Thermal (pass the DTR pin);
  blind timing is the fallback when DTR isn't wired.
- The Feather's GPIOs are **not 5 V-tolerant** (true on ESP32-S3) — fine here because the
  printer's logic is 3.3 V, but meter any new signal before landing it on a pin.
- **ESP32-S3 STEMMA QT power gate:** the QT port / I²C pull-ups are powered through a control
  pin (`PIN_I2C_POWER`) — `setup()` drives it HIGH before `Wire.begin()`. If I²C ever reads
  nothing, that pin (or an out-of-date Espressif core) is the first suspect.
- `analogWrite()` (eye PWM) needs Espressif core **v3.0+**; older v2.x would need `ledc*` calls.
- NTAG clones honor the standard Ultralight read/write-page commands (that's why they work);
  the marker bytes (`SK`) are how the prop tells its own tokens from blanks/foreign tags.
- Test (and optionally pre-write) every token on the ACR1252 **before sealing it in a shell** —
  a dud found after sealing is a wasted print and a let-down kid.
- **No secrets in this file.** GitHub auth lives in the machine's credential store; the only
  identifier kept here is the repo name. (Same rule the 444camera notes follow.)

## Known follow-ups (not yet done)

- **Flash to real hardware and bench-test** — the sketch is written and structured but not yet
  flashed; treat the first upload as v1, fix what surfaces, push the fix.
- Confirm printer baud (9600 vs 19200) on first run.
- Optional: NeoPixel eyes that shift color by prize tier (green = treat, red strobe = jackpot).
- ~~Store the repo link + per-year history on the token~~ — **done 2026-06-25.**
- ~~Outside-skull button as a 2nd trigger~~ — **done 2026-06-25.**
- **(Optional, WiFi):** NTP auto-year (drop the manual `EVENT_YEAR` bump), per-visit logging,
  or a live web leaderboard — board supports it; not wired up yet.
