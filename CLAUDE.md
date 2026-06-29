# CLAUDE.md — Skull Oracle

Project memory for Claude Code. This file is auto-loaded each session. Keep it current; put deeper human notes in `PROJECT-NOTES.md`. The authoritative code is `skull_oracle/skull_oracle.ino` — read it before changing behavior.

## What this is

A Halloween prop. A skull with LED eyes sits on a table. A kid puts a hand in its mouth; an IR proximity sensor fires; a thermal printer spits out a slip with a random number 1–100 and a prize tier. Kids also get an NFC token they keep and bring back in future years — each year's visit (number + prize) is written **onto the token itself**, so escalating rewards survive even if the controller board dies and loses its memory (which is what killed the original Arduino Zero build).

## Hardware

- **Controller:** Adafruit Feather ESP32-S3. On this board the silkscreen `Dn` labels equal the GPIO numbers. Three variants are interchangeable for this build; pick the FQBN that matches what you actually have:
  - plain 4MB/2MB (#5477) → `esp32:esp32:adafruit_feather_esp32s3`
  - plain 8MB no-PSRAM (#5323) → `esp32:esp32:adafruit_feather_esp32s3_nopsram`
  - TFT (#5483) / Reverse TFT (#5691) → `..._tft` / `..._reversetft` (TFT sits on the SPI bus → keep PN532 in I2C mode)
- **Hand sensor:** VCNL4010 IR proximity, I2C `0x13` (the original, reused). If it's dead, the modern drop-in is **VCNL4040** (`0x60`, different library) — swap requires a small code change.
- **NFC reader:** PN532 in **I2C mode**, address `0x24`. On the cheap "V3" clone set DIP switches: SW1 ON, SW2 OFF.
- **Printer:** DP-EH400/2 embedded thermal printer (CSN-A2 / Zijiang family → `Adafruit_Thermal` library). TTL serial, 3.3V logic. Baud is 9600 or 19200 — **confirm on first run** via the printer self-test (hold feed button while powering on; baud prints at the page edge). Needs its **own 5–9V / 2A+ supply**, ground shared with the Feather. DTR flow control is built into the library (pass the DTR pin).
- **Tokens:** NTAG203 NFC (144 bytes user memory). Use the 23 mm circles as keepsakes (better range). Test each on the ACR1252 bench reader before sealing in a shell.
- **Eyes:** existing LEDs on one GPIO (PWM breathe). **Buttons:** two momentary, each pin→GND, internal pull-up (pressed = LOW).

## Pin map (matches the .ino)

| Function | Pin |
|---|---|
| I2C SDA / SCL | board default (STEMMA QT or `SDA`/`SCL`) |
| PN532 IRQ / RST | GP6 / GP9 |
| Printer TX / RX | board `Serial1` TX / RX |
| Eyes (PWM) | GP10 |
| Printer DTR (optional) | GP11 |
| Mouth button (+10 to roll) | GP12 |
| Outside button (= second trigger) | GP13 |
| SPI fallback SS (if PN532 on SPI) | GP5 |

## Behavior (see .ino for the source of truth)

Idle: eyes breathe. Trigger = proximity over `baseline + PROX_MARGIN` **or** the outside button. On trigger, a ~700 ms window latches the mouth button (+10 to the roll, capped at 100) and reads the PN532; the token's stored `charge` value is also added to the roll. Branch: **fresh token** → write the repo NDEF link to pages 4–15 + record visit; **returning token** → append this year; **no token** → plain number. Veterans (3rd year+) get best-of-two rolls. Print: greeting tier + number + prize + charge bonus + full year history. Cooldown, re-arm when the hand clears and the button is released. Serial debug @115200 prints prox values and trigger source.

**Token layout (NTAG203 user pages 4–39, schema `0x03`):** pages 4–15 = NDEF URI record (repo link, phone-tappable); page 16 = marker `SK` + schema + visit count (written LAST = commit); page 17 = `charge` (per-token roll modifier, set via the `nfc_tag_bench/` tool); pages 18–39 = one page per year (~22-year capacity). NDEF and visit data never overlap. **Keep this layout in sync with `nfc_tag_bench/nfc_tag_bench.ino`.**

**Tuning knobs (top of .ino):** `PRINTER_BAUD`, `PROX_MARGIN`, `EVENT_YEAR`, `BASE_YEAR` (2024), `VETERAN_YEARS` (2), `PROP_WRITES_URL`. Prize tiers: 100 = JACKPOT, 90–99 = BIG WIN, 40–89 = TREAT, else trick (`prizeTier` / `prizeName`).

## Toolchain / flash

Arduino IDE or `arduino-cli`. Requires the Espressif **esp32 core ≥ 3.0.0** (needed for `analogWrite`). Libraries: `Adafruit PN532`, `Adafruit VCNL4010` (or VCNL4040), `Adafruit Thermal Printer`.

```bash
# one-time
arduino-cli core install esp32:esp32
arduino-cli lib install "Adafruit PN532" "Adafruit VCNL4010" "Adafruit Thermal Printer Library"

# compile + upload (set PORT and the FQBN that matches your board)
arduino-cli compile  --fqbn esp32:esp32:adafruit_feather_esp32s3 skull_oracle
arduino-cli upload -p PORT --fqbn esp32:esp32:adafruit_feather_esp32s3 skull_oracle
```

## Status & next steps

The sketch is written (v4) but has **never been flashed or bench-tested** — treat the first successful upload as v1 and commit fixes from there.

1. Flash to the Feather; open serial @115200; confirm the printer baud and set `PRINTER_BAUD`.
2. Bench-test proximity. The reused VCNL4010 is ~10 years old and was in the prop that died — if it doesn't read clean, swap to VCNL4040 (code change).
3. Verify a fresh NTAG203 round-trips: write NDEF + visit, power-cycle, read back the history.
4. Tune `PROX_MARGIN` to the real mounting distance; confirm both triggers and the +10 mouth button.

**Deferred (WiFi, board supports it, not wired up):** NTP auto-year (drops the manual `EVENT_YEAR` bump), visit logging, leaderboard.

## Repo / conventions

GitHub: `eaglett-cmyk/HalloweenProps`, branch `main`, GPL-3.0. No secrets in the repo or in this file. `push.sh "message"` is a one-command commit+push helper.
