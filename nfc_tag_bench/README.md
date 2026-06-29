# NFC Tag Bench — Skull Oracle test rig

A bench tool for writing/reading Skull Oracle tags **in the prop's exact format**, plus the
new **charge** value. The ESP32 joins your WiFi and serves a control page; hold a tag on the
PN532 and read/write it from a phone or laptop.

> Lives in its own subfolder — it does not change the prop, **except** that the prop firmware
> (`../skull_oracle/`) was updated in lockstep to read & apply `charge`. The two share one
> tag layout (schema `0x03`); change them together.

## What "charge" is

`charge` is a per-token modifier (0–255) **added to the random number the prop draws** when a
tag is tapped (on top of the in-mouth +10 button), clamped to 100. Set a charge here, tap the
token on the prop, and the printed number comes out higher. It's persistent (the prop reads it
but doesn't spend it).

## Parts (on hand)

| Part | What it is | Amazon |
|------|------------|--------|
| Controller | **AITRIP 30-pin ESP32** (WROOM-32) — hosts the page over WiFi | [B0CR5Y2JVD](https://www.amazon.com/dp/B0CR5Y2JVD) |
| NFC reader/writer | **HiLetgo PN532 V3** — in **I²C mode** | [B01I1J17LC](https://www.amazon.com/dp/B01I1J17LC) |
| Tags | NTAG203 / NTAG213 (your existing tokens) | — |

## Wiring (PN532 I²C → ESP32 30-pin)

All six wires land on the **3V3 side** (the right-hand pin row) — no jumper crosses to the
other edge of the board:

| PN532 pin | ESP32 pin | Notes |
|-----------|-----------|-------|
| VCC | **3V3** | top of the 3V3 side; 3.3 V keeps the I²C lines safe (ESP32 GPIOs aren't 5 V-tolerant) |
| GND | **GND** | the GND right next to 3V3 (same side) |
| SDA | **GPIO21** | I²C data |
| SCL | **GPIO22** | I²C clock |
| RSTO | **`TX2`** (GPIO17) | reset |
| IRQ | **`RX2`** (GPIO16) | interrupt |

**On the PN532 module set the DIP switches to I²C: `SW1 ON`, `SW2 OFF`.**

> IRQ/RST use GPIO16/17 — silkscreened **`RX2`** and **`TX2`** on this board (it does *not*
> print "D16/D17" there). They're free on the ESP32-WROOM-32 and avoid the strapping pins
> **and** GPIO18/19, which the thermocouple build uses for hardware SPI on this same board.
> GPIO4 and GPIO23 are the other free pins on the 3V3 side if you ever need them.

## WiFi credentials (kept out of the repo)

1. Copy **`secrets.h.example`** → **`secrets.h`**.
2. Fill in `WIFI_SSID` / `WIFI_PASS`.

`secrets.h` is **gitignored**, so your password never gets committed to the (public) repo.
The board joins that network; if it can't in 15 s it falls back to an access point named
`SkullBench` (password `oracle1234`).

## Build & flash

1. Arduino IDE → install the **Espressif "esp32"** boards package (Boards Manager).
2. Library Manager → install **"Adafruit PN532"** (pulls in "Adafruit BusIO").
3. Board = **"ESP32 Dev Module"**, select the COM port, **Upload**.
4. Open **Serial Monitor @ 115200** — it prints the PN532 status and the device IP.

## Use

1. Browse to **http://skullbench.local** (or the IP shown on serial / `http://192.168.4.1`
   in AP-fallback mode).
2. Hold a tag on the reader. The page polls ~every 1.5 s and shows UID, whether it's a prop
   tag, **charge**, **visits**, whether the **phone link (NDEF)** is present, and **history**.
3. Controls:
   - **Provision fresh tag** — write the NDEF link + a blank `SK` marker (a brand-new prop tag).
   - **Set charge / +10 / −10** — the roll modifier.
   - **Log a visit** — year + number + prize → writes that year's slot. Use a *past* year to seed
     history; leave the prop's current `EVENT_YEAR` unplayed (see the test-flow note below).
   - **Erase / format** — clear user pages 4–39.

## Tag data layout (schema `0x03`, NTAG 4-byte pages) — matches the prop

| Page | Bytes | Contents |
|------|-------|----------|
| 3 | CC | NDEF capability container |
| 4–15 | NDEF URI | the phone-tappable GitHub link |
| 16 | `S K 0x03 visits` | marker + visit count |
| 17 | `charge 0 0 0` | per-token **charge** (roll modifier) |
| 18–39 | `number prize(0-3) 0 0` | one page per year (slot = 18 + (year−2024)), up to 22 |

Prize tiers: `0` trick · `1` treat · `2` big win · `3` jackpot.

## End-to-end test flow

1. **Provision fresh tag** (or just **Set charge** on a blank tag — it auto-provisions).
2. **Set charge** to e.g. 20.
3. (Optional) **Log a visit** for a *past* year (2024/2025) to seed history — but **leave the
   prop's current `EVENT_YEAR` (2026) unplayed.** The prop only *rolls* (and applies the charge)
   on a year the token hasn't played yet; a year already on the token is reprinted unchanged.
4. Flash the updated prop, tap the token → because 2026 is unplayed, the prop rolls, **adds the
   charge to the number**, and prints `Charge bonus +20`.

> Heads-up: **Log a visit** writes the raw number you type — it does *not* simulate the prop's
> roll, so it doesn't bake in the charge. If you log the *current* year and then tap the prop,
> the prop reprints that stored number unchanged (still showing the charge line), and it'll look
> like charge did nothing. Always test the boost on a year the token hasn't played.

## Status

Written, **not yet flashed or bench-tested** — treat the first successful upload as v1. The
prop and bench layouts are kept identical (schema `0x03`); if you change one, change the other.
