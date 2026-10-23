# Halloween Props — the Crystal Oracle

**Found this page by tapping a crystal with your phone?** (Newer crystals open the storybook at
[halloween.ciancio.cloud](https://halloween.ciancio.cloud) instead; its source is in [`site/`](site/).) Hello! That crystal belongs to a trick-or-treater
who visited *Butterscotch, the Sleepy Candy Dragon*. It remembers every Halloween they come back, so keep it
safe and bring it next year. Nothing on it is personal: it stores how many visits it has had, each year's
glow number and treat, and its colour. The voices in the prop are AI-generated (ElevenLabs).

---

## Dragonglow: the Sleepy Candy Dragon (2026 →)

A trick-or-treat storytelling station. Kids set their **NFC crystal** on a crystal-crowned stone plinth (the
*Wakestone*). A 7" touch screen (*the Dragon's Window*) wakes Butterscotch, a cozy old candy dragon. He greets
returning kids by rank, rolls tonight's result and tells them which candy bowl to pick from:

| Result | Bowl |
|---|---|
| **Hoard Coin** (most kids) | silver circle |
| **Dragon Gem** (rare) | red triangle |
| **Golden Star** (legendary) | gold star, held by the grown-up |
| **Goblin trick** (Snuffwick's prank) | silver circle, candy anyway |

The result is written **onto the crystal**, so each kid's history travels with them year after year, up the
ranks: Sparkfinder → Apprentice → Glowcaster → Hoardguard → Wizard → Dragon Rider → Keeper of the Hoard.
Every visit also adds a spark to the **Hoard Beacon**; when the neighbourhood fills it, there's a finale.
Kids without a crystal **tap the window** for a dragon blessing and still get candy.

**Everything lives in [`crystal_oracle/`](crystal_oracle/)**. Start with its
[README](crystal_oracle/README.md) for setup, flashing, the TF card and the host menu.

| Part | Hardware | Code |
|---|---|---|
| Screen | Waveshare ESP32-S3-Touch-LCD-7C-BOX (800×480 touch, speakers, TF card) | [`crystal_oracle/firmware/crystal_oracle/`](crystal_oracle/firmware/crystal_oracle/) |
| Plinth | AITRIP ESP32-WROOM-32 + Elechouse PN532 NFC V3, in a 3D-printed altar ([`plinth/`](crystal_oracle/plinth/)) | [`crystal_oracle/firmware/crystal_plinth/`](crystal_oracle/firmware/crystal_plinth/) |
| Link | ESP-NOW between the two boards (no WiFi router needed) | `crystal_link.h` |
| Crystals | 3D-printed translucent PLA with an NTAG inlay: Dragonberry, Starfrost, Rainbow Fizz | set up on the screen's *Crystals* page |
| Story + assets | ChatGPT stills, ElevenLabs voice / sound effects / music → TF card | [`STORY.md`](crystal_oracle/STORY.md), [`tools/`](crystal_oracle/tools/) |

More docs: [`docs/DESIGN.md`](crystal_oracle/docs/DESIGN.md) (technical design),
[`docs/RESEARCH-NOTES.md`](crystal_oracle/docs/RESEARCH-NOTES.md) (verified board facts),
[`docs/ODDS.md`](crystal_oracle/docs/ODDS.md) (result odds), and [`PROJECT-NOTES.md`](PROJECT-NOTES.md)
(dated build log and status).

**Status (Oct 2026):** both boards flashed and the first power-on checklist passes; every story asset is
made (29 stills, 31 clips, 73 voice lines, 17 sound effects, 2 music loops). Next: the full test with the
TF card, then the dress rehearsal.

## The crystal's data (NTAG203, schema `0x03`)

| Pages | Contents |
|---|---|
| 4–15 | NDEF link to this page (why a phone tap lands here) |
| 16 | marker `SK` + schema + visit count (written last = commit) |
| 17 | byte 0 = per-tag roll bonus (`charge`), byte 1 = crystal colour (1 Dragonberry, 2 Starfrost, 3 Rainbow Fizz) |
| 18–39 | one page per year: `[number, result tier, flags, chapter]` — about 22 years |

The same layout is used by the retired Skull Oracle and the bench tool, so old tokens still work.

## Also in this repo

- [`nfc_tag_bench/`](nfc_tag_bench/) — a bench tool (ESP32 + PN532) to read, test and set up tags off the prop.
- [`skull_oracle/`](skull_oracle/) — **retired before it ran.** The earlier plan: a skull with LED eyes, an IR
  sensor for a hand in its mouth, and a thermal printer for each kid's number and prize. The Crystal Oracle
  replaced it for 2026; it's kept for reference and because it defined the tag layout.
  [`docs/wiring-diagram.svg`](docs/wiring-diagram.svg) is its wiring.

## License

[GPL-3.0](LICENSE).
