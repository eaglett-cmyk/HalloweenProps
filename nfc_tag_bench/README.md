# NFC Tag Bench — Skull Oracle + Crystal Oracle test rig (v3)

A bench tool for writing/reading Skull Oracle tags **in the prop's exact format**, plus the
**charge** value — and, since v3, the **crystals of the Crystal Oracle** (same tags, same
layout), including each crystal's **colour**. The ESP32 joins your WiFi and serves a control
page; hold a tag on the PN532 and read/write it from a phone or laptop.

> Lives in its own subfolder — it does not change the prop, **except** that the prop firmware
> (`../skull_oracle/`) was updated in lockstep to read & apply `charge`. The skull, this bench
> and the Crystal Oracle (`../crystal_oracle/`, see `firmware/crystal_oracle/token.h`) share
> one tag layout (schema `0x03`); change them together.

## What "charge" is

`charge` is a per-token modifier (0–255) **added to the random number the prop draws** when a
tag is tapped (on top of the in-mouth +10 button), clamped to 100. Set a charge here, tap the
token on the prop, and the printed number comes out higher. It's persistent (the prop reads it
but doesn't spend it).

## Crystal Oracle crystals (v3)

The Crystal Oracle (`../crystal_oracle/`) is a touch-screen storytelling station that uses the
**same tags** (NTAG inlays inside 3D-printed PLA crystals) and the **same schema `0x03`** — a crystal's
year slots count for the skull too, and vice versa (one roll per year across every station).
v3 of the bench adds:

- **Crystal colour** — page 17 byte 1: `0` = unset, then one id per **filament** the
  crystals are printed in (v3.1, 2026-10-22):

  | Id | Name | Filament | Swatch on the page | On the Oracle (`color.N_hex`) |
  |---|---|---|---|---|
  | `1` | **Moonspell** (full of dreams) | VOXELAB PLA Crystal Rainbow, *Deep Sea Realm*: alternating transparent purple and blue (each crystal differs) | purple → blue gradient | `#A35CFF,#2F7BFF` (a slow purple/blue cycle) |
  | `2` | **Starfrost** (full of wonder) | PLA HS Glitter, *Clear Sky Blue* | sky blue | `#5CD6FF` |
  | `3` | **Rainbow Fizz** (full of giggles) | FLASHFORGE Translucent Glitter PLA, Crystal *Rainbow Candy* (multi-colour gradient) | an animated rainbow | `rainbow` (the full hue cycle) |

  Ids `4`–`6` are free for future filaments; a crystal holding one of them (or anything
  else not in the table) shows as **unknown (n)**, and the Oracle treats it as unset (no
  colour line; a colour picked from its UID). The Oracle tints the crystal's story in its
  colour (an unset crystal gets a stable colour derived from its UID). The names, swatches
  and filament names live in **one table at the top of the sketch** (`CRYSTAL_COLORS`,
  marked TUNE): the names must match the Oracle's words `color.N`; the web page builds its
  picker from it. To add a filament, add a row here **and** `color.4` / `color.4_hex` to the
  Oracle's story words. The skull ignores this byte.
- **Charge keeps the colour** — *Set charge* now reads page 17 and changes only byte 0
  (before v3 it wrote `charge 0 0 0`, which would have wiped the colour). *Set colour* changes
  only byte 1. If page 17 can't be read, nothing is written.
- **Visit-history badges** — the Oracle writes extra bytes into each year slot (flags +
  story chapter). When they're non-zero the history table shows them as badges: `first`,
  `charged`, `pity`, `milestone`, `circle`, `oracle` (= written by the Oracle), and `ch N`.
- **CC write guard** — the capability container (page 3) is one-time-programmable. The link
  writer now writes it **only if byte 0 isn't already `0xE1`** (and never if page 3 can't be
  read): writing `E1 10 12 00` over an NTAG216's factory `E1 10 6D 00` would corrupt its size
  byte. The skull's `writeNdefUrl()` got the same guard.
- **No blind re-provisioning** — if a tag's marker page can't be read, *Set charge / Set
  colour / Log visit* now report an error instead of treating it as a brand-new tag (which
  would have reset its visits, charge and colour). Hold the tag still and try again.

## Parts (on hand)

| Part | What it is | Amazon |
|------|------------|--------|
| Controller | **AITRIP 30-pin ESP32** (WROOM-32) — hosts the page over WiFi | [B0CR5Y2JVD](https://www.amazon.com/dp/B0CR5Y2JVD) |
| NFC reader/writer | **HiLetgo PN532 V3** — in **I²C mode** | [B01I1J17LC](https://www.amazon.com/dp/B01I1J17LC) |
| Tags | NTAG203 / NTAG213 (your existing tokens) and Crystal Oracle crystals | — |

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
   tag, **charge**, **crystal colour**, **visits**, whether the **phone link (NDEF)** is
   present, and **history** (with the Crystal Oracle badges, when present).
3. Controls:
   - **Provision fresh tag** — write the NDEF link + a blank `SK` marker (a brand-new prop tag;
     also zeroes the charge and colour).
   - **Set charge / +10 / −10** — the roll modifier (keeps the colour).
   - **Crystal colour** — one swatch button per colour (Moonspell's is a purple→blue
     gradient, Rainbow Fizz's cycles through the rainbow), with the filament's name under
     it, plus **none** (= unset). Keeps the charge. Auto-provisions a blank tag, like *Set
     charge*.
   - **Log a visit** — year + number + prize → writes that year's slot. Use a *past* year to seed
     history; leave the prop's current `EVENT_YEAR` unplayed (see the test-flow note below).
   - **Erase / format** — clear user pages 4–39.

HTTP API (what the page calls): `/api/read` (JSON incl. `charge`, `color`, and per-year
`flags`/`chapter`), `/api/provision`, `/api/charge?value=0..255`, `/api/color?value=0..3`
(0 = unset; the top is the number of table rows), `/api/colors` (the colour table: `value`,
`name`, `css` = the swatch's CSS background, `rainbow`, `filament`),
`/api/log?year=&number=&prize=`, `/api/format`.

## Tag data layout (schema `0x03`, NTAG 4-byte pages) — matches the prop + the Crystal Oracle

| Page | Bytes | Contents |
|------|-------|----------|
| 3 | CC `E1 10 12 00` | NDEF capability container — written **only if byte 0 ≠ `0xE1`** (one-time-programmable) |
| 4–15 | NDEF URI | the phone-tappable GitHub link |
| 16 | `S K 0x03 visits` | marker + visit count (written **last** = commit) |
| 17 | `charge colour 0 0` | per-token **charge** (roll modifier, 0–255) + crystal **colour** (0 = unset, 1–3 in use, 4–6 free for future filaments; Crystal Oracle only) |
| 18–39 | `number prize(0-3) flags chapter` | one page per year (slot = 18 + (year−2024)), up to 22. `flags`/`chapter` are Crystal Oracle extensions — the skull and this bench write `0` |

Prize tiers: `0` trick · `1` treat · `2` big win · `3` jackpot (the Crystal Oracle calls 2/3
*rare* / *legendary*; same numbers, same bands).

Slot `flags` bits (Crystal Oracle): `0x01` first visit (awakening) · `0x02` charge meter
filled · `0x04` pity (bad-luck protection) · `0x08` station milestone · `0x10` crystal circle
(3+ together) · `0x20` reserved · `0x80` written by the Crystal Oracle. `chapter` = the story
chapter told that year (0 = none).

Colours (page 17 byte 1): `1` Moonspell · `2` Starfrost · `3` Rainbow Fizz (`4`–`6` free).

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

Written, **not yet flashed or bench-tested** — treat the first successful upload as v1 (v3 adds
the Crystal Oracle colour/badges and the CC guard, v3.1 the three PLA filament colours; also
untested on hardware). The prop, bench
and Crystal Oracle layouts are kept identical (schema `0x03`); if you change one, change the others.
