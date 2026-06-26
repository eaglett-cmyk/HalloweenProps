# Halloween Props — Skull Oracle

A motion-and-NFC Halloween prop. A skull with glowing LED eyes: when a
trick-or-treater puts a hand in its mouth, an IR proximity sensor triggers it
to draw a random number (1–100) and print it on a thermal label. Kids who
carry an NFC token get a personalized, escalating experience that the prop
**remembers across years** — because the history lives on the token, not on
the controller.

## How it works

- A **VCNL4010** IR proximity sensor detects a hand in the skull's mouth.
- A **PN532** NFC reader checks for a token in that hand:
  - *First-time token* — the prop writes the link to this repo onto it (so a
    phone tap opens the repo) and records the first visit.
  - *Returning token* — the prop appends this year to the token's history;
    3rd-year-and-up visitors get a bonus roll, so loyalty pays off.
- A **DP-EH400** thermal printer prints the label: a greeting tier, the number
  drawn, the prize won, and the visitor's full year-by-year history.
- The eyes breathe in idle and flare on a hit.

All per-visitor state is stored on the NTAG203 token, so replacing or
reflashing the controller never erases anyone's streak.

## Hardware

- Adafruit Feather ESP32-S3 (4MB Flash / 2MB PSRAM, #5477) — controller, with WiFi/BLE
- Adafruit VCNL4010 IR proximity sensor (I²C)
- PN532 NFC reader in I²C mode (Adafruit breakout or Elechouse V3)
- DP-EH400 embedded thermal printer (TTL serial)
- NTAG203 NFC tokens (e.g. SMARTRAC dry inlays embedded in 3D-printed shells)
- LEDs for the eyes
- A separate 5–9 V / 2 A+ supply for the printer, **ground shared** with the Feather

Full pin-by-pin wiring is in the header comment of
[`skull_oracle/skull_oracle.ino`](skull_oracle/skull_oracle.ino).

## Firmware

- Board package: **Espressif "esp32" v3.0+** via Boards Manager (board: "Adafruit Feather ESP32-S3 2MB PSRAM")
- Libraries (Library Manager): **Adafruit VCNL4010**, **Adafruit PN532**,
  **Adafruit Thermal Printer Library**
- Open the sketch, set the values marked `TUNE` (printer baud, proximity
  threshold, `EVENT_YEAR`), and flash.

## NFC token data layout (NTAG203 — 144 bytes of user memory)

| Pages | Bytes | Contents |
|-------|-------|----------|
| 4–15  | 48    | NDEF URI record — the link to this repo |
| 16    | 4     | marker `SK` + schema version + visit count |
| 17–39 | 92    | one page per year: `[number, prize, …]` |

A 5-year visitor uses ~70 bytes; the layout holds 23 years of history.

## Tag prep

Test every token before sealing it into a shell — read it, write a page, read
it back, and confirm range. An ACR1252 reader + NFC Tools on a computer is
ideal for this, and you can pre-write the repo link during that same step (set
`PROP_WRITES_URL 0` in the sketch if you do).

## License

[GPL-3.0](LICENSE).
