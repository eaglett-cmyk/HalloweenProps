# Crystal Oracle — "Dragonglow: The Sleepy Candy Dragon"

A trick-or-treat storytelling station that replaces the Skull Oracle. Kids set their NFC **crystal**
on a crystal-crowned stone **plinth** (the *Wakestone*); a 7" touch screen (*the Dragon's Window*) tells the story,
greets returning kids by rank, reveals tonight's result and says which candy bowl to pick from — and
the result is written **onto the crystal**, so every kid's history travels with them, year after year.
Kids without a crystal **tap the window** for a dragon blessing (there is no big button this year).

| Doc | What's in it |
|---|---|
| [`STORY.md`](STORY.md) | The story bible: Butterscotch the sleepy candy dragon, Snuffwick, the Hoard Beacon meter, ranks, results/bowls, every scene and line, the parent sign |
| [`CHATGPT-ASSETS.md`](CHATGPT-ASSETS.md) | **The list of things to make** — 29 ChatGPT image prompts (16 must-have), 73 voice lines (42 must-have), sound effects, music |
| [`docs/stories/`](docs/stories/) | Archived story versions: Moonshards (bible + manifest), swappable back in |
| [`docs/DESIGN.md`](docs/DESIGN.md) | Technical design: hardware facts, rendering, audio, the show's timing, token layout, asset pipeline |
| [`docs/RESEARCH-NOTES.md`](docs/RESEARCH-NOTES.md) | Verified facts about the Waveshare board (pins, I²C map, gotchas) with sources |
| [`plinth/`](plinth/) | The plinth's 3D design: `crystal_plinth_v7.scad` (body, cover, NFC lid, pins, 11 crystal clusters), `crystal_plinth_REVIEW.md` (checks, print notes, what's still open) and the `v7_*.png` renders |
| [`firmware/crystal_plinth/README.md`](firmware/crystal_plinth/README.md) | Plinth wiring + flashing (and the optional ring / button) |
| [`tools/README.md`](tools/README.md) | The asset builder (ChatGPT images + voice → SD card) and the ElevenLabs voice script |

## How it works

```
   ┌──────────── the Dragon's Window ────────────┐        ┌──── Wakestone (plinth v7) ────┐
   │ Waveshare ESP32-S3-Touch-LCD-7C-BOX          │ ESP-NOW│ AITRIP ESP32 on a screw-      │
   │ 800×480 touch screen, speakers, TF card      │◄──────►│   terminal board (USB-C front)│
   │ plays the story, decides + reveals results   │wireless│ Elechouse PN532 NFC V3 under  │
   │ kids without a crystal tap it for a blessing │        │   a flush 2 mm lid            │
   └──────────────────────────────────────────────┘        └───────────────────────────────┘
```

* **Why two boards?** The 7" board uses every GPIO pin for the screen, and its only add-on bus is I²C —
  where its own IO-expander chip already sits at **0x24, the PN532's fixed address**. So the reader lives
  in the plinth on its own ESP32: the AITRIP ESP32-WROOM-32 that the `nfc_tag_bench` tool runs on. The two
  boards talk over ESP-NOW (no WiFi router needed). *Single-board alternative:* an ~$8 **M5Stack Unit
  RFID2** wired to the screen's I²C screw terminal (`reader = rfid2` in `config.ini`) — shorter range.
* **No ring, no button this year.** The plinth is the owner's v7 altar with just the ESP32 and the reader
  inside: the crystal and the screen carry the magic, and a tap on the screen stands in for the big button
  (`[station] button = no`, the default). Both can be added later (see the plinth README; `STORY.md` §2
  *Later: a big button or a light ring*).
* **The crystal remembers, not the board.** Same tag format as the Skull Oracle and `nfc_tag_bench`
  (schema `0x03`): visits, each year's GLOW number and result, plus the crystal's colour (1 Dragonberry,
  2 Starfrost, 3 Rainbow Fizz; set in the host menu's *Crystals* page). A crystal plays
  **once per year** across all stations; re-tapping shows tonight's result again.
* **Everything is tweakable without reflashing** — `config.ini` and `captions.ini` on the TF card.

## What you need

| Part | Status |
|---|---|
| Waveshare **ESP32-S3-Touch-LCD-7C-BOX** (7" 800×480, ES8389 audio, TF slot) | have |
| **Plinth: the owner's v7 design** — [`plinth/crystal_plinth_v7.scad`](plinth/crystal_plinth_v7.scad) (parts `body`, `cover`, `nfc_lid`, `pin`, `cluster` 0–10; checks and print notes in [`plinth/crystal_plinth_REVIEW.md`](plinth/crystal_plinth_REVIEW.md)): a crystal-crowned stone altar with the controller in a cavity, the reader in a fitted pocket under a flush 2 mm lid, and the USB-C port at the front | print |
| **AITRIP ESP32-WROOM-32** (30-pin, FQBN `esp32:esp32:esp32`) on its ~63 × 66 mm **screw-terminal carrier**, USB-C edge facing the plinth's front port — the same board the `nfc_tag_bench` tool runs on | have |
| **Elechouse PN532 NFC V3** in I²C mode: DIP **SW1 ON, SW2 OFF** (wiring: [`firmware/crystal_plinth/README.md`](firmware/crystal_plinth/README.md)) | have |
| LED ring / big button | **not this year** (both optional later: the plinth firmware supports them; see the plinth README) |
| **TF card** (microSD, 8–32 GB, FAT32) | |
| Power: **12 V 2 A** barrel→screw terminal (or 5 V 3 A USB-C) for the screen; **5 V USB-C** into the plinth's front port | |
| Crystals: **3D-printed PLA**, **≥ 40 mm every way**, flat base with an NTAG203/213 inlay sealed in a pocket ≈ 0.6–1.2 mm above the bottom face (see step 5). Three filaments = three colours: VOXELAB Crystal Rainbow *Deep Sea Realm* (deep to light purple) = **Dragonberry**, PLA HS Glitter *Clear Sky Blue* = **Starfrost**, FLASHFORGE Translucent Glitter *Rainbow Candy* = **Rainbow Fizz** | making (3 colours) |
| Optional: powered speaker on the 3.5 mm jack (louder than the onboard amps; plugging in mutes them) | |
| Bowls: **silver circle** (common), **red triangle** (Dragon Gem), **gold star** (host holds it) | |

## Setup

### 1. Arduino (one time)

Arduino IDE 2 with the Espressif **esp32** core **3.3.x** (3.3.10 is installed). Libraries (Library Manager):
**GFX Library for Arduino** 1.6.9, **JPEGDEC** 1.8.4, **U8g2** 2.36.x, **RFID_MFRC522v2** 2.0.6,
**Adafruit NeoPixel**, **Adafruit PN532** (+ Adafruit BusIO). All are installed on the hosting PC.

### 2. Flash the screen — `firmware/crystal_oracle/`

| Setting | Value |
|---|---|
| Board | **ESP32S3 Dev Module Octal (WROOM2)** |
| Flash Mode / Size | **OPI 80MHz** / **32MB** |
| PSRAM | **OPI PSRAM** |
| Partition Scheme | **32M Flash (4.8MB APP/22MB FATFS)** |
| USB CDC On Boot | **Enabled** (the serial console is the USB port) |
| USB Mode | Hardware CDC and JTAG |

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3-octal:CDCOnBoot=cdc,FlashMode=opi,FlashSize=32M,PSRAM=opi,PartitionScheme=app5M_fat24M_32MB,CPUFreq=240,USBMode=hwcdc" firmware/crystal_oracle
arduino-cli upload  -p COMx --fqbn "<same>" firmware/crystal_oracle
```

If it boot-loops, use Waveshare's own setting instead: board **ESP32S3 Dev Module**, Flash Mode **QIO 80MHz**
(the octal flash is auto-detected). If the board won't take an upload, hold **BOOT**, tap **RESET**, release BOOT.

### 3. Build and flash the plinth — `plinth/` + `firmware/crystal_plinth/`

* **Print** the v7 parts from `plinth/crystal_plinth_v7.scad` (`-D 'part="body"'`, `cover`, `nfc_lid`, `pin`,
  `cluster` with `-D idx=0..10`). Print the body **upside down** (top face on the bed; only the NFC pocket floor
  needs light supports); the crystal clusters print root-down without supports; the D-pins print lying on
  their flat side. Before printing, check the review's *Still open* list: confirm the terminal board's outer
  size and height, and that the PN532 with its plugs seats under the lid (it clears 5 mm).
* **Wire** the PN532 to the ESP32's screw terminals as in [`firmware/crystal_plinth/README.md`](firmware/crystal_plinth/README.md)
  (the same pins as `nfc_tag_bench`). The PN532 DIP switches must be **SW1 ON, SW2 OFF** (I²C).
* **Flash** the AITRIP ESP32: FQBN `esp32:esp32:esp32` (hold **BOOT** while the uploader says "Connecting...").
  The USB-C port at the front of the plinth is both its power and its flashing port. (A Feather ESP32-S3 plinth
  still works too: `esp32:esp32:adafruit_feather_esp32s3`.)
* **Range check:** with the 2 mm lid on, a finished crystal must read reliably when set on the lid (step 5).

### 4. Build the TF card

```bash
cd crystal_oracle
python -I tools/build_assets.py --check          # what's missing → assets/ASSET-STATUS.md
python -I tools/build_assets.py --placeholders   # → sd_card/oracle/ (Windows voice + synth sounds fill gaps)
```

Copy **`sd_card/oracle`** to the root of the TF card (so the card has `/oracle/config.ini`, `/oracle/clips/…`).
**Format the card FAT32** (exFAT is not readable: a 64 GB card has to be repartitioned to one ≤32 GB FAT32 partition first). The screen also runs with **no card at all** (built-in glowing visuals and chimes, no voice) — handy for a first test.

### 5. Making the crystals (3D-printed PLA)

Each crystal is a translucent PLA print with an NTAG203/213 inlay sealed inside its base. **Print one test
crystal first** and check its read range before printing a batch.

**Crystal prep happens in the host menu's *Crystals* page**, through the plinth (hold the screen's top-left
corner for 3 s → *Crystals*): put a tag or a crystal on the Wakestone, **test-read** it, **set its colour**
(1 Dragonberry / 2 Starfrost / 3 Rainbow Fizz) and, if you like, **provision** it. The `nfc_tag_bench` web tool
(`../nfc_tag_bench/`) does the same jobs and still works if a spare ESP32 is ever free (it runs on the same
AITRIP + PN532 wiring as the plinth).

1. **Design.** At least **40 mm in every direction** (small-parts rule: nothing a toddler could swallow), with
   a **flat base** so it sits steady on the Wakestone and the tag lies flat over the reader's antenna.
2. **Tag pocket.** Model a pocket in the base for the inlay: the inlay's size plus ~0.5 mm all round, and as
   deep as the inlay is thick (+0.1–0.2 mm) so the nozzle can't catch it. Put it low, so **≈ 0.6–1.2 mm of
   plastic sits under the tag**: the tag then ends up as close to the reader as it can be.
3. **Pause, drop, resume.** In the slicer, add a **pause at the layer that closes the pocket** (the first
   layer printed over it; *Pause at height* in Cura, *Add pause* on the layer slider in PrusaSlicer /
   OrcaSlicer / Bambu Studio). When the printer stops, drop the inlay in **flat**, and resume. **Test-read each
   inlay on the Crystals page before it goes in**: once it's sealed, a dud tag means a new crystal.
   PLA print temperatures are fine for the usual PET-based dry inlays, but **test one** (print a test crystal,
   then read it).
4. **Glitter and range.** Glitter filaments (Starfrost's HS Glitter, Rainbow Fizz's glitter PLA) can contain
   metallic flakes that may weaken NFC a little. Test the **read range of a finished crystal on the plinth,
   through its 2 mm lid**, before printing a batch. If the range is poor, keep glitter-heavy plastic out from
   directly under the tag: a thinner floor (0.6 mm), or print the floor under the tag in plain clear PLA and
   swap to the glitter filament at the pause.
5. **Keep it clear.** Print with a **hollow or low-infill core** (0–15 %, 2–3 walls): dense infill scatters
   light, so the translucent plastic catches the porch light less — and a hollow core is what a light ring
   under the crystal would need in a later year.
6. **Test it and set its colour on the Crystals page** right after the tag test, while it's still on the
   Wakestone: **1 Dragonberry** (Deep Sea Realm, deep to light purple) · **2 Starfrost** (Clear Sky Blue glitter) ·
   **3 Rainbow Fizz** (Rainbow Candy). Butterscotch then names the right colour, and the screen glows to match:
   Dragonberry blends deep and light purple, Starfrost is a steady sky blue, Rainbow Fizz runs through the rainbow.
   (Ids 4–6 are free for future filaments, see `STORY.md` §5.)
7. Optional: **Provision** it on the Crystals page (writes the parent-info link + marker). Blank,
   never-provisioned crystals also work: the station "awakens" them on first contact.

### 6. Voice lines with ElevenLabs

`tools/make_voice_elevenlabs.py` records every line in `assets/manifest.json` with an ElevenLabs voice and
saves it as `assets/source/voice/<line id>.mp3`; `build_assets.py` then turns them into the WAVs the screen
plays (and pitches up Snuffwick's two lines). Python standard library only.

1. **API key** (never commit it, never paste it into a chat): put the key alone in **`tools/.elevenlabs_key`**
   (git-ignored), or set the **`ELEVENLABS_API_KEY`** environment variable.
2. **Pick the voice:** `python -I tools/make_voice_elevenlabs.py --list-voices`. Butterscotch wants a gentle,
   older male storyteller (`STORY.md` §3). A library voice has to be added to *My Voices* in your ElevenLabs
   account first.
3. **Try one line and listen:** `python -I tools/make_voice_elevenlabs.py --voice "<name or id>" --only attract_call_1 --force`.
   Tune with `--stability`, `--similarity`, `--style`, `--speed` (default 1.07; lines should fit about 2 s).
   The voice, model and settings are remembered in `tools/.elevenlabs_voice.json`, so later runs and re-takes match.
4. **The night-one set:** `python -I tools/make_voice_elevenlabs.py --priority MVP` (42 lines). Later, no
   option records everything still missing; `--dry-run` shows the lines and the character count first.
   Lines that already have a file are skipped; `--only <id>,<id> --force` re-takes single lines.
5. **Build:** `python -I tools/build_assets.py --placeholders`, then copy `sd_card/oracle` to the card.

*Alternative:* in a fresh Claude session with the **ElevenLabs connector** enabled on claude.ai, Claude can
generate the lines through the connector instead; save each file as `assets/source/voice/<line id>.mp3` and
build as above.

The button lines (`attract_call_2`, `charge_1`, `charge_2`) are NICE: with no button they never play
(`attract_tap_1` and `charge_tap_1/2` play instead).

## First power-on checklist

1. Screen only, no TF card: boot screen lists Display / Touch / TF card / Sound / Crystal reader → then the
   glowing-orb attract screen. Hold the **top-left corner for 3 s** → host menu → *System → I²C scan* should
   show `0x10 0x24 0x40 0x51 0x5D` (+ `0x55` with a battery).
2. *Sound & light → test tone*. No sound? Check the speakers on the MX1.25 connectors or use the 3.5 mm jack.
3. Power the plinth through its front USB-C: the host menu's *Reader* page shows **online**.
4. Host menu → *Crystals*: put a blank tag or crystal on the Wakestone, **test-read** it, set a colour.
5. Close the menu and place a blank crystal: the chime plays at once, Butterscotch awakens it, the result is
   written. Lift it and place it again: **"Already glowed"** (no second roll). Check it on the *Crystals* page:
   visits = 1, this year's slot filled.
6. Power off, insert the TF card (built with `--placeholders`), power on and repeat: now Butterscotch talks (Windows voice
   until the real lines exist). (A card inserted while running: *Media → Rescan* restarts the screen to load it.)
7. **Tap the screen** with no crystal on the stone: the dragon-blessing / no-crystal path → silver circle bowl.
   The attract caption alternates CRYSTAL HERE / OR TAP ME. (With a big button wired and `button = yes`:
   press it instead.)

## The hidden host menu

Hold the **top-left corner** of the screen for **3 seconds** while it's idle. Pages: *Tonight* (counts, prizes
given), *Sound & light* (volume, brightness, ring brightness — only matters with a ring — test tone), *Mode*
(rush auto/on/off, second helpings, prize stock), **Odds** (legendary / rare / goblin-trick chance, rare bonus
per return visit, pity; shows what a first visit gets; live from the next visit), *Reader* (status, simulate a crystal/button), **Crystals**
(crystal prep through the plinth: test read, set colour 1 Dragonberry / 2 Starfrost / 3 Rainbow Fizz, provision),
*Media* (what's missing, play any scene, card speed), *System* (I²C scan, memory, reboot, credits). Changes are
saved to `config.ini`.

## config.ini highlights (TF card `/oracle/config.ini`)

| Key | Default | Meaning |
|---|---|---|
| `[station] event_year` | 2026 | **Bump every Halloween** (which year slot is written) |
| `[station] reader` | plinth | `plinth` · `rfid2` · `none` |
| `[station] button` | no | `no` = no big button: kids tap the window for a blessing, and the tap lines/words play (`nobutton.*` in `captions.ini`); `yes` = a button is wired to the plinth (the button lines, OR PRESS / PRESS! come back) |
| `[timing] tap_cooldown_sec` / `tap_spark_sec` | 6 / 20 | with `button = no`: taps start nothing for N s after a sequence ends / a tap blessing adds a beacon spark at most every N s (so drumming on the window can't light the beacon alone) |
| `[station] volume` / `brightness` | 75 / 85 | |
| `[station] pclk_mhz` | 12 | screen pixel clock; 16 made the real panel jump on first boot (2026-10-22), so 12 is the default (host menu → *System*; kept across reboots) |
| `[station] rush` | auto | shorter sequences when kids pile up (`on` / `off` to force) |
| `[station] second_helpings` | 0 | 1 = "already played tonight" also points at the silver bowl |
| `[odds] trick_pct / rare_pct / legend_pct` | 15 / 18 / 4 | see `docs/ODDS.md` |
| `[stock] rare / legend` | 0 / 0 | 0 = unlimited; otherwise paced across `expected_kids` |
| `[story] moon_goal` | 50 | Hoard Beacon sparks (crystal visits + no-crystal blessings) to light the beacon and trigger the finale (the key keeps its old name) |

## Plan to Halloween (Sat Oct 31)

| When | Do |
|---|---|
| Oct 22–23 | Flash both boards, test a blank tag on the *Crystals* page; build the TF card with placeholders. **Print one test crystal** with a tag (step 5) and check its read range. Start the v7 plinth body print. ChatGPT: Butterscotch base + 4 edits. |
| Oct 24–25 | ChatGPT: Snuffwick + props + backgrounds (the 16 MVP images). Voice: record the 42 MVP lines (`tools/make_voice_elevenlabs.py --priority MVP`, step 6). |
| Oct 26 | Build the real TF card; tune clip positions/anchors and the beacon's glow spot (`meter.pos`) from `assets/previews/`. Sounds + music. |
| Oct 27–28 | Assemble the v7 plinth (board on its corner frame, PN532 in its pocket, 2 mm lid on); re-check a crystal's range through the lid; print the crystal batch (test + colour each on the *Crystals* page); bowl signs. |
| Oct 29–30 | Full dress rehearsal at the door at dusk (brightness, volume, glare, rush mode); print the parent sign. |
| Oct 31 | Start by 5:30 pm. `event_year = 2026`. **Before the first kid: host menu → *Tonight* → *Reset tonight* (tap twice)** — clears the rehearsal's beacon sparks, finale and stock (the boot screen shows "Tonight so far: ..." when there is something to clear). Keep spare crystals handy; a doubtful one gets a test read on the *Crystals* page. |

## Repo notes

Backed up with the rest of `eaglett-cmyk/HalloweenProps` (GPL-3.0). Deploying = flashing the boards and
copying `sd_card/oracle` to the TF card — `git push` only backs up the source. Generated output
(`sd_card/`, build caches, previews) is git-ignored; the source art in `assets/source/` is committed.
