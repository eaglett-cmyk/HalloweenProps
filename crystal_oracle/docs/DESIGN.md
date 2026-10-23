# Crystal Oracle — technical design

Version 1.1 (2026-10-22: no-button mode, word overrides and voice swaps (§5.7), crystal
prep in the host menu (§5.8), the v7 plinth without ring or button (§8, §9)). Version 1.0
2026-10-21. This is the contract for the firmware, the plinth, the SD card and the asset tools. The code headers in `firmware/crystal_oracle/*.h`
are the authoritative interfaces; this document explains them and the facts
behind them. Research sources: `docs/RESEARCH-NOTES.md`.

---

## 1. System overview

```
                 ┌──────────────────────── Waveshare ESP32-S3-Touch-LCD-7C-BOX ───────────────────────┐
  TF card ──SDMMC│ media (clips .omj / stills .jpg)   show (director)   service (host menu)            │
  (assets)       │ audio (ES8389, mixer)  display (esp_lcd, 3 FBs)  fx (live overlays)  touch (GT911)  │
                 │ settings/counters (SD)  token (schema 0x03)  oracle (odds/ranks)  vlog (SD logs)    │
                 │ reader ──┬── plinth backend: ESP-NOW ◄──────────────┐                              │
                 │          └── rfid2 backend: M5 Unit RFID2 on P1 I2C │ (alternative, single board)   │
                 └─────────────────────────────────────────────────────┼──────────────────────────────┘
                                                                       │ ESP-NOW (channel 1, group 1)
                 ┌──────── plinth v7: AITRIP ESP32-WROOM-32 on a screw-terminal board (= the bench's) ─┐
                 │ Elechouse PN532 V3 (I2C 0x24, its own bus) under a 2 mm lid   USB-C at the front     │
                 │ (optional, off in 2026: NeoPixel ring under the crystal, big arcade button)          │
                 └────────────────────────────────────────────────────────────────────────────────────┘
```

**Why two boards:** the screen board exposes no free GPIO, and its only external
bus is the shared I²C on the P1 screw terminal. The board's CH32V006 IO expander
sits at 0x24 — the PN532's fixed address — and it controls the LCD power, the
backlight, the SD card and the amp, so a PN532 can never share that bus (a mux
doesn't help; an LTC4316 translator breaks the PN532's clock stretching). The
plinth ESP32 reuses the proven PN532 code (it is the very board `nfc_tag_bench` runs
on, so the host menu's *Crystals* page does the bench's crystal prep through it, §5.8)
and can drive a ring light and read a big button (both optional; the 2026 v7 plinth has
neither, §5.7). The **rfid2** backend is a single-board fallback (~$8 M5Stack Unit
RFID2 at 0x28; < 20 mm range; no ring light).

## 2. Screen board facts (Waveshare repo `73f8b90`, schematic netlist)

| Item | Value |
|---|---|
| Module | ESP32-S3-WROOM-2-**N32R16V**: 32 MB octal flash + 16 MB octal PSRAM, VDD_SPI 1.8 V (GPIO47/48 are 1.8 V; the bus is level-shifted to 3.3 V) |
| Panel | 800×480 IPS, ST7262, RGB565 parallel, PCLK **12 MHz** (16 MHz = the Waveshare demo value made the real panel jump, 2026-10-22), pulse 4 / bp 8 / fp 8 (H and V), `pclk_active_neg=1` → 29.3 Hz |
| RGB pins | DE 5, VSYNC 3, HSYNC 46, PCLK 7; data B3..B7 = 14,38,18,17,10; G2..G7 = 39,0,45,9,8,21; R3..R7 = 1,2,42,41,40 |
| I²C | SDA 47, SCL 48, 400 kHz. Devices: expander 0x24, ES8389 0x10, ES7210 0x40, RTC 0x51, BQ27220 0x55 (battery only), GT911 0x5D (0x14 alt) |
| Expander | regs 0x02 dir (1=out), 0x03 out, 0x04 in, 0x05 PWM, 16-bit writes `[reg,lo,hi]`. Dir mask **0x787F**. Not reset by ESP soft reset. |
| EXIO | 0 LCD_RST · 1 TP_RST · 2 BL_EN · 3 PA · 4 SD_D3 (keep high) · 5 LCD_VDD (bias) · 6 STA LED (active low) · 7–10 P1 inputs · 11–14 P1 outputs · 15 RTC_INT (input) |
| Touch | GT911, INT GPIO4, RST EXIO1; reset 100/100/200 ms with INT low → 0x5D |
| TF card | SD-bus **1-bit**: CLK 12, CMD 11, D0 13, D3 = EXIO4 high. No card detect. SDMMC can't DMA into PSRAM on S3 → read via internal buffers |
| Audio | I2S1: MCLK 6, BCLK 44, WS 16, DOUT 15, DIN 43. ES8389 slave, MCLK 256·fs, 48 kHz. Amps 2× NS4150B via EXIO3 (a 3.5 mm plug mutes them in hardware) |
| USB | native USB-Serial/JTAG on 19/20 → `Serial` needs **USB CDC On Boot = Enabled**. GPIO43/44 belong to I2S: never `Serial0.begin()` |
| P1 terminal | 1 VIN 7–36 V · 2 GND · 3 5V out · 4 GND · 5 3V3 · 6 GND · 7 SDA · 8 SCL · 9–16 EXIO7–14 |
| No-go | `LED_BUILTIN`/`RGB_BUILTIN` (redirects to GPIO48 = SCL); BOOT button (GPIO0 = LCD G3); NVS/Preferences/FFat writes while showing (stall the PSRAM cache → panel drift); bb_captouch (probes 0x24 and would write into the expander) |

**Boot order** (`board::begin` → `display::begin` → touch → SD → settings → audio → reader → show):
1. `Wire.begin(47, 48, 400000)` before anything else (Adafruit BusIO / MFRC522v2 call `Wire.begin()` with no pins; once the bus is up they're no-ops).
2. Expander: probe 0x24 (5 tries, 10 ms apart); OUT = 0x0050 (SD_D3 high, LED off, everything else low/off), DIR = 0x787F, OUT again.
3. LCD: EXIO5 (bias) on → 20 ms → EXIO0 (reset) high → create panel → fill all three buffers black → wait 2 refreshes → PWM → EXIO2 (backlight) on.
4. Touch reset sequence; SD (EXIO4 already high); config; I2S (MCLK running) → ES8389 init → volume → EXIO3 amp on; reader; show.

## 3. Rendering

* `esp_lcd_new_rgb_panel` with `num_fbs = 3`, `bounce_buffer_size_px = 800*10`, `dma_burst_size = 64`, `fb_in_psram = 1`, created from a core-0 task so the bounce ISR runs on core 0.
* Swap protocol: `on_frame_buf_complete` ISR promotes `queued → shown`; `beginFrame()` returns the buffer that is neither shown nor queued; `endFrame()` calls `esp_lcd_panel_draw_bitmap(fb[i])` (same pointer = no copy, just a switch) and marks it queued.
* Clips: OMJ1 (length-prefixed MJPEG, §7) decoded with **JPEGDEC 1.8.4** straight into the back buffer (`openRAM` → `setPixelType(RGB565_LITTLE_ENDIAN)` → `setFramebuffer(fb)` **after** openRAM → `decode(0,0,0)`), S3 SIMD colour conversion is automatic. Frames must be exactly 800×480, baseline, 4:2:0. Budget ≈ 45–70 ms/frame → author clips at **13 fps** (every 3rd refresh).
* Stills: decoded once into a PSRAM cache (5 × 768 KB LRU) and `memcpy`'d per frame.
* Text: `Arduino_Canvas` subclass whose `_framebuffer` is pointed at the back buffer (destructor nulls it), u8g2 fonts (`logisoso92_tn` digits, `fub30/25/20`, etc.), 8-direction outline.
* Every cue has a **procedural fallback** (`fx::fallbackCue`: dark vignette + breathing orb in the crystal's colour + the caption as a title) so the show runs with an empty TF card.

## 4. Audio

48 kHz fixed. Mixer task (core 0, prio 18, 5 ms blocks) = music stream (looped, ducked −10 dB under voice) + voice stream + 4 preloaded SFX + generated chime/tone. Streamer task (core 0, prio 6) fills two PSRAM stream buffers (~1 s each) with 8 KB reads through an internal DMA buffer. Files: WAV PCM s16le mono 48 kHz (chunk-walking parser; anything else = missing). Volume = ES8389 DAC regs 0x46/0x47 (0xBF = 0 dB, 0.5 dB/step; keep ≤ 0xBF). Mute = reg 0x40 bits[1:0] or EXIO3.

## 5. The show (director)

### 5.1 Timing (research: ≤ 6 s per kid, ≤ 3.5 s in rush)

| Step | Normal | Rush | Notes |
|---|---|---|---|
| plinth flare (ring builds) + absorb chime | 0–0.1 s | same | the ring flares locally, before the screen even knows; the chime is the screen's |
| ABSORB | 1.0 s | 0.6 s | classify the tag image meanwhile |
| GREET r<n> (returning) | 2.0 s | 1.2 s | rank name + meter |
| RECALL (nice) | 1.5 s | skip | last year's tier |
| CHARGE window | 2.5 s | skip | taps (and the plinth button, if fitted) fill the meter → small bonus |
| decide + WRITE | ≤ 1.5 s | same | "hold still" ring; retried while the crystal is present |
| AWAKEN (first visit) | 6–8 s | 4 s | written at placement (before the awakening plays) |
| REVEAL t<tier> | 2.5 s (t3: 3 s) | 1.5 s | big number + tier name + sparkles |
| BOWL | 1.5 s | 1.2 s | bowl icon (colour + shape) + voice |
| GOODBYE | 1.5 s | skip | |
| REPLAY | 2 s | 1.5 s | tonight's number/tier again, no re-roll |
| NOCRYSTAL | 3.5 s | 2.5 s | big button, or a tap on the screen (§5.7); still candy |
| SLEEPY | 2 s | 2 s | unreadable crystal: "place it again" |

A step lasts `max(min, voice length + 250 ms)`, capped at `max`. Visuals loop (attract, charge) or hold their last frame. The times above are the **minimums**. Since 2026-10-22 every cap that carries a line is at least `LINE_MS` = 7.5 s (`show.cpp`), in rush too, because the recorded Butterscotch voice runs 2.2–7 s a line and a step that hits its cap cuts the line off. The charge window stays open until its line ends; awaken allows 3 × `LINE_MS`. Short lines still end their steps early.

### 5.2 Events during a sequence
* `Removed` — noted; the sequence continues (animation finishes after the crystal is lifted). A write still pending after removal → logged as failed, result shown anyway.
* `Placed` (another crystal) — queued; starts right after the current sequence (rush logic counts it).
* `Button` — during CHARGE adds charge; otherwise ignored (or queued as no-crystal when idle).
* Touch tap — during CHARGE adds charge + sparkle burst at the tap. Idle (outside the host's
  top-left 100 × 100 corner): the no-crystal blessing when the reader is asleep, or — with
  `button = no` — whenever no crystal is on the reader (§5.7). Otherwise a sparkle burst.

### 5.3 Rush mode
Auto: a new crystal within `rushGapSec` (8 s) of the previous sequence ending, or ≥ 5 sequences within 60 s → rush on; off after `rushQuietSec` (45 s) idle. Forced by `[station] rush = on|off`. A small "RUSH" host badge shows top-left.

### 5.4 Ranks & meter
Visits after this visit → rank: 1, 2, 3, 4, 5, 6–7, 8+ → rank 1..7. Meter (8 gems) = min(visits + 1, 8) (first visit already shows 2).

### 5.5 Odds (oracle.h has the exact algorithm)
Legendary 4 % (not loyalty-linked) → rare 18 % + 3 %/prior visit (cap 9 %) + charge-meter bonus (≤ 5 %) + bench charge (%) → pity after 2 prior visits without rare → trick 15 % (never on a first visit) → treat. Limited stock (`[stock] rare / legend`) is paced over `expectedKids`. Number = inside the skull's band for the tier.

### 5.6 Words-driven story features (the firmware is story-neutral)
Everything story-specific is data: the art, the voice and the `captions.ini` words (`settings::text` / `settings::color`; built-in defaults in `story_defaults.h`, **generated** from the manifest words by `build_assets.py --gen-defaults`). Swapping stories = a new manifest + a new card; no firmware change. Besides the names and captions, the words pick these features (key reference: `assets/manifest.schema.md` → *Story features*):

| Words | What the firmware does | Moonshards (default) | Dragonglow |
|---|---|---|---|
| `meter.style` | the communal meter: `moon` = a pale Moon drawn in the sky (`fx::moonMeter`, a crescent → full disc); `lantern` = an additive warm glow over a lantern **painted into the art** (`fx::lanternMeter`: a faint ember → a blaze, candle flicker ≤ 2 Hz, slow rising sparks from phase ~3; no hard shapes) | `moon` | `lantern` |
| `meter.pos` | `x,y,r` in screen px: where it sits and its size (Moon radius / glow radius). The station counter sits under it (Moon: `y + r + 38`; lantern: `y + 0.6 r + 38`), the RECALL icon in the top corner **away** from it (x 116 or 684, y 150) | `650,110,46` | `150,110,70` |
| `meter.color` | the Moon's silver / the lantern's glow; also the finale bursts and the no-art blessing wisp | `#E9EEFF` | `#FFB347` |
| `meter.name` | host menu "Fill the ‹name›" / "Reset ‹name›", the Tonight page and the boot screen ("Lantern 12/50") | `Moon` | `Lantern` |
| `meter.on` | comma list of **visual ids** that show the meter. An id also covers its `_` variants (`greet` → `greet_r5`, `attract` → `attract_full`); matched against the visual on screen, or the step's own id when there is no art. HOLD and the finale never use it (the finale flares the meter itself) | the window scenes (built-in list) | every `bg_cave` clip but `chapter_b` (the lantern goes out), incl. `keeper` |
| `meter.zoom_<visual>` | `from,to`: that clip's centred, linear background zoom (the layers recipe's `bg.zoom`), so the meter follows the painted spot; the Dragonglow generator writes it for every listed clip with a zooming background | – | `meter.zoom_keeper = 1,1.15` |
| `fx.<visual>` | device effects on a visual, parsed once per step: `kind,x,y,startMs,endMs[,jx,jy][,#RRGGBB]`, several separated by `;`. `poof` = a white puff (flash + soft puffs, ~0.5 s) and a few sparkles at start; `sparkles` = 3 sparkles every 120 ms around (x, y) ± jx/jy (default 14/10) from start until end; `burst` = one 28-sparkle burst at start; `none`. Colour = the crystal's (poof: white) unless `#RRGGBB` is given. Looked up as `fx.<visual on screen>`, then `fx.<step id>_<variant>` (a gag's effects even without its clip), then `fx.<step id>`; a missing key = no effects | `fx.reveal_t0_1` polka-dot sparkles over the scene, `fx.reveal_t0_2` `none`, `fx.reveal_t0_3` a white burst | `poof,615,230,1100,1100` · `sparkles,440,170,600,1800` · `burst,420,150,1420,1420` |
| `glow.idle_color` | the narrator's anchor glow (lantern / tummy) when no crystal is around | `#CFFFF2` (mint) | `#FFC27A` (warm) |
| `color.N` / `color.N_hex` | the crystal colours; N = the bench colour id (tag page 17 byte 1). `_hex` = `#RRGGBB` (steady), `#RRGGBB,#RRGGBB[,#RRGGBB[,#RRGGBB]]` (2–4 stops blended smoothly round and round, ~4 s per cycle: a gradient filament) or `rainbow` (the hue wheel, ~4 s). In use = 1..N up to the first empty `_hex` (`settings::colorCount()`), parsed once into a table (`settings::crystalRgb(id, now)`, garbage → a built-in colour). A moving colour is evaluated once per frame, so every crystal-tinted glow, beam, gem, sparkle, aura (stepped every 250 ms for the orb cache) and burst moves with it; the plinth ring gets `LED_DUAL` (stops 1 + 2) or `LED_RAINBOW` for steady moments (`LINK-PROTOCOL.md` §5). A bench colour above the count = unset: no `awaken_b` line and no colour-name caption, a colour picked from the UID | six steady colours | `1` Moonspell `#A35CFF,#2F7BFF` · `2` Starfrost `#5CD6FF` · `3` Rainbow Fizz `rainbow` |

An empty or garbled `meter.pos` falls back to the style's own spot (moon `650,110,46`, lantern `150,110,70`), an empty `meter.name` to Moon / Lantern (`settings::meterName()`, also used by the host menu and the boot screen). The puff that hides the GLOW number on a trick takes `tier.0_color` (Moonshards lavender, Dragonglow sage).

The meter's fill is tonight's points ÷ `[story] moon_goal` (phase 0..8, eased over 1.5 s, a gentle swell for each point added); the counters keep their Moonshards names (`moon_points`, `moon_full`). With style `lantern` the finale (`moonfull`) is a big warm flare at `meter.pos` that settles bright; with `moon` the Moon swells full. Per-frame cost stays bounded: the words are parsed once at `show::begin()` (meter, crystal colours) or once per step (`meter.on` match, `meter.zoom_*`, `fx.*`); nothing is parsed, allocated or read from the card per frame.

### 5.7 No-button mode (`config.ini [station] button = no`, the default)

The 2026 plinth (v7, §9) has **no big button** and **no LED ring**. `Settings::hasButton`
(`[station] button = yes | no`, default **no**) tells the screen; with `yes` everything
behaves exactly as before (the plinth's button blesses, `ui.or_press` / `caption.charge`
and the button voice lines play as written). With `no`:

**Taps bless.** While idle (attract or chapter), a **short tap** anywhere on the screen
(`touch::Gestures::tap`: down → up < 450 ms, < 40 px of travel) starts the no-crystal
blessing when no crystal is on the reader (`reader::tagPresent()` false). Not a tap:
the host's 3 s long-press, and a tap in the host's 100 × 100 top-left corner (so a
host's hold that flickers can never start a blessing). Taps are ignored for
`[timing] tap_cooldown_sec` (default **6 s**) after any sequence or the finale ends, so
the last kid's extra taps don't start a second one. A **tap blessing adds a Lantern
spark at most once every `[timing] tap_spark_sec`** (default **20 s**, anti-spam: the
`no_crystal` counter still counts every one); a crystal visit always adds its spark.
The CHARGE window stays (taps fill it). The *reader asleep* fallback keeps working:
with no reader online `tagPresent()` is false, so a tap blesses (after the cool-down).
(`show.cpp tapBlesses()`; with `button = yes` a tap blesses only while the reader is
asleep, as before.)

**Word overrides.** `settings::text(key)` (and `settings::color()`) first look up the
word **`nobutton.<key>`** — captions.ini, then `story_defaults.h` — and return it if it
exists, **even when it is empty** (an override can blank a word). captions.ini writes
them as a `[nobutton]` section whose keys keep their own section:
`[nobutton]` / `caption.charge = TAP! TAP!` is the word `nobutton.caption.charge`.
(The INI reader prefixes every key in `[nobutton]`, dots or not; in any other section a
dotted key is still used as written.) Keys that are themselves `nobutton.*` are never
prefixed twice. Words are cached at `show::begin()`, so an override costs nothing per
frame. Dragonglow overrides `caption.charge` (TAP! TAP!), `ui.press` (the charge prompt)
and `ui.or_press` (OR TAP ME, alternating with *Crystal here* in the attract loop while
touch works).

**Voice swaps.** The word **`nobutton.voice`** = `from=to;from=to;…` (semicolons; blanks
around ids are tolerated; the build quotes a value with ` ;`). Each side is a voice
**group** id (`charge`) or a full **line** id `<group>_<1..9>` (`attract_call_2`; an id
ending in `_` + one digit 1–9 is a line, anything else a group). Parsed **once per words
load** (`show.cpp loadVoiceSubs()`, ≤ 24 rules) and only with `button = no`; applied to
every line the show starts (`resolveVoice()`: sequenced parts, attract calls) and to the
"does this group exist" checks (`voiceExists()`):

| Rule | Effect |
|---|---|
| `group=group` | group `to` plays instead of group `from` (random pick from `to`; an explicit variant — the colour line, a trick gag — is kept) |
| `group=line` | that one line plays instead of the group |
| `line=line` | when the pick is line `from`, line `to` plays instead (any group) |
| `line=group` | when the pick is line `from`, a variant of group `to` plays instead |
| `from=` (empty) | `from` never plays: a line is left out of its group's random picks (another variant plays; none left = nothing); a group plays nothing |

A replacement that isn't on the card counts like an empty `to`. One pass: no chains.
A group's variants are counted from `_1` up to the first missing file, **except** that a
line named as a swap's `from` holds its slot even when its file is missing
(`variantCount()`): the NICE button line `attract_call_2` may never be recorded, and
without it `attract_call_3..` and its stand-in `attract_tap_1` would be unreachable.
The "never the same line twice" memory works on the group actually played. The host
menu's cue browser plays the raw lines (to audition every file). Dragonglow:
`attract_call_2=attract_tap_1;charge=charge_tap`.

### 5.8 Crystal prep (host menu → *Crystals*)

The plinth's AITRIP ESP32 + PN532 is the same hardware `nfc_tag_bench` runs on, so the
bench's crystal prep lives in the screen's hidden host menu, page **Crystals**
(`service.cpp`), and works with the plinth, the rfid2 unit and a simulated crystal
(*Sim blank*).

* **Events.** While the page is open **every** reader event goes to it: `Placed` (a new
  page image + the read-test tally), `Removed`, `WriteDone`; `Button` / `LinkUp` /
  `LinkDown` are swallowed. None reaches the show. A write in flight is serviced on any
  tab and finished before the menu closes. When the menu closes the show calls
  `reader::ignoreUntilLifted()`: a plinth that went standalone while the menu ran re-reads
  a crystal still sitting on it when the screen talks again, and that re-announcement is
  not posted as `Placed` until the crystal has been lifted (`LINK-PROTOCOL.md` §3).
* **Shown.** Read OK / kind (*blank* = Fresh, *ours*, *FOREIGN*, *unreadable*), UID + SAK,
  visits, charge, years on the crystal (filled slots), this year's slot (played? GLOW
  number + tier name), link (ours / none / not ours) and CC, the bench colour as a live
  swatch (`settings::crystalRgb`: gradients and the rainbow move), and the **read test**:
  good reads / placements since the page opened + the last read time (`TagImage.readMs`:
  the plinth's `LT_TAG_PLACED.readMs` from plinth 1.2.0, timed by the rfid2 task; *n/a*
  for older plinths and sims). Retries slow a read down, so a glitter-PLA print or the
  2 mm lid shows up as slow or failed reads.
* **Buttons.** Colour **None / 1…N** (N = `settings::colorCount()`, labelled with the
  words `color.N`, a live colour stripe each; the crystal's colour is highlighted),
  **Provision**, **Clear tonight**, **Format** (a second tap within 3 s confirms),
  **Sim blank / Lift sim**.
* **Write lists** (`token.h`): `buildColorWrite` (Ours: a read-modify-write of page 17
  keeping byte 0 = charge and bytes 2..3; Fresh: provisions first with that colour, like
  the bench's `ensureProvisioned`), `buildProvisionWrites` (Fresh only: CC only if missing
  and only with a link, NDEF 4..15 unless already ours, page 17 `{0, colour, 0, 0}`,
  marker `S K 03 00` last; an Ours crystal is refused so its visits stay),
  `buildClearYearWrites` (this year's slot → 0; only if it was filled on an Ours crystal,
  the marker with visits − 1, last), `buildFormatWrites` (every non-zero page of 4..39 → 0,
  the marker last; the only list allowed on a Foreign tag). Every builder refuses an image
  that wasn't read OK and runs a final guard: pages 3..39 only, page 3 only while its byte 0
  isn't `0xE1`, the marker page only as the last entry.
* **Sending.** `reader::write()` with the crystal's UID, `LINK_MAX_WRITES` (18) pages per
  request (Format: up to 36 pages = 2 requests, in order), each request's `WriteDone`
  awaited (timeout 2.5 s, a spinner meanwhile) before the next. On success the verified
  pages are copied into the page's image (the plinth doesn't re-read by itself) and
  *Saved* is shown; on a failure the `WriteStatus` reason (and page) is shown and the
  image is marked stale until the crystal is lifted and placed again.

## 6. Token data (schema 0x03 + extensions)

| Page | Bytes | Written by |
|---|---|---|
| 3 | CC `E1 10 12 00` | fresh provisioning only, and only if byte 0 ≠ 0xE1 (OTP) |
| 4–15 | NDEF URI (`https://` + `ndefUrl`) + `FE` | fresh provisioning |
| 16 | `S K 03 visits` | every new-year visit, **last** (commit) |
| 17 | `charge colour 0 0` | bench or host menu *Crystals* (charge + colour); fresh provisioning writes zeros (*Crystals* → *Provision*: `0 colour 0 0`) |
| 18–39 | `number tier flags chapter` (slot = 18 + year − 2024) | the station, once per year |

* **Fresh** = no SK marker AND (NDEF area empty — `03 00 FE` / zeros / factory lock-control TLV + empty NDEF — OR already our link). Anything else without the marker = **Foreign** (never written; kid gets the no-crystal blessing).
* Already played this year = slot byte 0 ≠ 0 → REPLAY, no write.
* Write order for a returning crystal: slot page, then marker. Fresh: CC (if needed), NDEF 4–15, charge page, slot, marker.
* The plinth only writes pages 3–39, checks the UID first, retries each page 3×, verifies by read-back.
* `nfc_tag_bench` is updated to set the crystal **colour** (page 17 byte 1) and to preserve byte 1 when setting charge.
* The host menu's *Crystals* page (§5.8) builds its lists with `token::buildProvisionWrites` / `buildColorWrite` / `buildClearYearWrites` / `buildFormatWrites`: the same page map, CC rule and marker-last commit; Format (zero 4..39) is the only write ever made to a Foreign tag.

## 7. Asset pipeline

`assets/manifest.json` is the single source of truth: stills (ChatGPT images), clips (recipes that turn stills into OMJ animations), voice lines (text + caption + delivery), sfx, music, and the story words (ranks, tiers, bowls, colours, captions, and the story features of §5.6: `meter.*`, `fx.*`, `glow.*`). `tools/build_assets.py`:

* `--check` validates the manifest (including the format of the §5.6 story-feature words), the two `crystal_link.h` copies, and reports which sources exist (writes `assets/ASSET-STATUS.md`).
* builds `sd_card/oracle/…` from whatever sources exist in `assets/source/` (images, voice, sfx, music) using ffmpeg:
  * clip recipes: `still` (→ .jpg), `kenburns`, `float` (sprite bobbing over a background, loop-exact), `crossfade` (sequence of stills with fades; optional float), `video` (any MP4 → crop 5:3, denoise, 13 fps, optional loop crossfade), and `layers` (`co_layers.py`, used by the whole story manifest: a timeline of keyed/tinted sprites over a background with fades, paths, zoom and spin; colour variants as separate clips `awaken_1..N`, one per crystal colour the words define (N = 3 in Dragonglow), the tint being the colour spec: one colour = a flat multiply, 2–4 stops or `rainbow` = a gradient across the sprite (`assets/manifest.schema.md` → *Crystal colours*); writes each clip's lantern **anchor** to `captions.ini [anchor]`);
  * every clip → MJPEG (`-q:v 5`, `hqdn3d`, yuvj420p, +bitexact) → packed OMJ1 with a max-frame check (≤ 96 KB);
  * audio → WAV s16le mono 48 kHz, loudnorm (voice/sfx −16 LUFS, music −20 LUFS), 120 Hz high-pass, 5 ms fades, `+bitexact` (clean 44-byte header);
* `--placeholders` fills gaps: labelled animated test clips (ffmpeg lavfi), synthesized SFX/music beds, and **voice lines spoken by Windows' built-in TTS** (System.Speech) so the prop talks before the real voice exists;
* writes `captions.ini` and a `config.ini` template (never overwrites an existing one), and `--gen-defaults` regenerates `firmware/crystal_oracle/story_defaults.h`.

OMJ1 = `"OMJ1"`, u16 w, u16 h, u16 fps×100, u16 0, u32 frames, u32 maxFrameBytes, then per frame u32 len + JPEG + pad to 4.

## 8. Plinth firmware (`firmware/crystal_plinth`, v1.2.0)

**Board (2026): the AITRIP ESP32-WROOM-32** ("ESP32 Dev Module", FQBN `esp32:esp32:esp32`) on
its ~63 × 66 mm screw-terminal carrier, the same board `nfc_tag_bench` runs on, with an
**Elechouse PN532 NFC V3** in I²C mode (DIP **SW1 ON, SW2 OFF**): VCC → 3V3, GND → GND,
SDA → GPIO21, SCL → GPIO22, IRQ → GPIO16 [RX2], RSTO → GPIO17 [TX2]. Power through the
board's USB-C port, at the plinth's front port. The onboard blue LED (GPIO2) shows the link
through the closed plinth: fast blink = PN532 missing, slow 1 Hz blink = searching for the
screen, steady = linked, a flicker every 0.5 s = linked with a crystal on it.

**Ring and button are compile-time options**, `PLINTH_HAS_RING` and `PLINTH_HAS_BUTTON`
(`#ifndef` guards, default **0**: the v7 plinth has neither; `-D…=1` or a `#define` turns
them on). With 0 no NeoPixel code runs and the NeoPixel library isn't even included;
`LT_LED` commands are accepted and ignored (only logged); the button pin is never read
and `HELLO.buttonDown` is always 0 (the screen runs with `config.ini button = no`, §5.7).
The optional alternative is the **Adafruit Feather ESP32-S3** (the Skull Oracle's
controller: PN532 on STEMMA I²C, IRQ GP6, RST GP9; ring data **GP10**; big button **GP12**
→ GND, not GP13: that pin carries the red LED) built with both options at 1; an AITRIP
with them wired uses ring data GPIO4 and the button on GPIO23 → GND. Pin sets are selected
by `ARDUINO_ADAFRUIT_FEATHER_ESP32S3` (+ the `_NOPSRAM` / `_TFT` / `_REVTFT` variants) /
default.

Loop: poll the PN532 every ~80 ms (InListPassiveTarget through the sketch's own checked
frame code, 50 ms timeout, raised automatically for a slow PN532). New UID → (with a ring:
an instant local white-violet flare) → read pages 3–39 with ten 16-byte READs (0x30) via
InDataExchange, each retried 3× with a re-select, one more sweep for gaps → `LT_TAG_PLACED`
with `readMs` (how long that took, incl. retries). 4 consecutive misses → `LT_TAG_REMOVED`.
`LT_WRITE_REQ` → re-select, compare the UID, write in order (0xA2), retry ×3, read back +
compare, answer `LT_WRITE_RESULT` (cached by `reqId` + a hash for resends). Ring (option):
Adafruit_NeoPixel, every `LedMode`, brightness cap, gamma, current limit, crossfades,
~60 fps. Button (option): debounced, `LT_BUTTON`. Heartbeat `LT_HELLO` every 1 s
(broadcast until `LT_HELLO_ACK`, then unicast). Standalone: if the screen is silent > 3 s
the plinth broadcasts HELLO again (a ring still flares on placement and breathes when
idle); when the screen talks again a crystal still on the reader is re-read and
announced again (the screen keeps a crystal handled in its host menu quiet, §5.8).

## 9. Physical build notes

* **The plinth (2026) is the owner's v7 design** (`plinth/crystal_plinth_v7.scad`, review and
  renders in `plinth/crystal_plinth_REVIEW.md`, `v7_*.png`): a crystal-crowned altar (11
  printed crystal clusters on D-pins round the rim, top at z = 47 mm). Inside: a 92 × 74 mm
  controller cavity with a corner frame on the cover for the 63–66 mm terminal board, its
  USB-C edge at the **front port** (x ±11 mm). On top: a fitted **NFC pocket** (60.5 × 58.2
  mm, floor at z = 40) for the Elechouse PN532 V3 (42.7 × 40.4 mm), held against the
  pocket's −x / −y walls by two locating bars, its right-angle plugs toward +x / +y and the
  8 × 48 mm wire slot down into the cavity; a **flush 2 mm printed lid** (part `nfc_lid`)
  on a 2 mm ledge hides it. **No LED ring and no button.**
* **Where the crystal goes.** The reader's centre is about **8.5 mm left of and 8.5 mm in
  front of** the lid's centre (seen from the front: −x / −y). Mark that spot on the lid (a
  painted swirl or a shallow ring) so kids set the crystal over the antenna. Check it seats
  (the plugs make the reader about 4 mm thick; the lid clears 5 mm) before closing.
* **Lid and range.** Non-metallic, 2 mm (≤ 3 mm). Use the host menu's *Crystals* page read
  test (§5.8) to check a finished crystal through the lid: every placement a good read,
  read times staying low. Keep metal away from the coil (screws, foil tape, metallic
  paint, glitter right under the antenna).
* LED ring (optional build only): **around** the antenna, not over it (a ring PCB lying
  over the coil detunes it): a 24-LED ring (≈ 72 mm inner diameter) encircles a 40 × 40 mm
  PN532 antenna. Measure read range with and without the ring.
* Crystals (since 2026-10-22): **3D-printed translucent PLA**, ≥ 40 mm in every dimension (small-parts rule), flat base, the inlay laid flat in a base pocket during a print pause with ≈ 0.6–1.2 mm of plastic under it. Glitter filaments may hold metallic flakes: measure a finished crystal's read range on the plinth through its lid (host menu → *Crystals* read test) before printing a batch, and keep glitter-heavy plastic out from directly under the tag if it is short. A hollow / low-infill core lets light through (it matters only with a ring). Full steps: `README.md` step 5.
* Screen: shade hood, don't face west (sunset 6:38 pm), centre ≈ 100–110 cm high, tilted back 15–20°. Plinth top ≈ 75–85 cm.
* Power: screen from 12 V 2 A on P1 (VIN/GND) or 5 V 3 A USB-C; plinth from its own USB 5 V into the board's USB-C (or P1 5V/GND, ≤ 0.5 A: fine without a ring).
* Audio: the BOX's speaker connectors are MX1.25 (check whether speakers are fitted); a powered speaker on the 3.5 mm jack is louder (and mutes the onboard amps).
