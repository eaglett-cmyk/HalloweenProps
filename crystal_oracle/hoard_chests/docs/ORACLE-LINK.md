# Oracle link: the show opens the prize chests (2026-11-12)

The Crystal Oracle show (the iPad station app, or the Waveshare screen) can tell the **Hoard Hub**
to pop open one prize chest per crystal visit. It is **opt-in** (`[station] chests = no` by
default) and the show never waits for the hub: with the key off nothing is sent and the show
runs exactly as before.

```
 show (station app / screen) --LT_CHEST 0x85--> plinth --LT_CHEST_CMD 0x90 x3, LT_CHEST_BEACON 0x91 every 2 s--> Hoard Hub
 show <--LT_CHEST_INFO 0x06-- plinth <--LT_CHEST_STATUS 0x11 (every 5 s + after each command)-- Hoard Hub
```

Hub side (`../firmware/hoard_hub/`): only the follow-up magic change below ('C','H' for every
hoard packet); its behaviour is unchanged.

## When an award is sent

* Once per **crystal visit that rolls a prize** (first visit / returning), at the moment the
  reveal's **bowl line** starts (the same moment the bowl icon pops in). In rush mode the bowl
  is shown from the start of the reveal, so the award goes out at the reveal start.
* Prize tier → chest tier: trick (0) and Hoard Coin (1) → **1** (Silver Coin chests),
  Gold Star (2) → **2**, Dragon Gem (3) → **3**.
* If the reveal step ends before its bowl showed (interrupted), the award is sent right after it,
  so a recorded visit always gets exactly one.
* **No award** for: a replay (already played tonight, also with `second_helpings`), a no-crystal
  blessing (tap / button), a foreign tag, an unreadable crystal, a simulated crystal from the host
  menu. Reason: 15 chests (5 per tier) cannot serve taps that repeat every 6 s, and a test must not
  spend a real prize. The host menu's Test buttons are the way to fire a chest by hand.
* No TEASE is sent at the reveal start: the hub treats a command that arrives while a TEASE job
  runs as `busy` (`hoard_hub.ino` `cmdAward`), so a tease followed by the award would lose the
  award. The hub's AWARD already plays its own 1.2 s tease.

## What changed, file by file

| File | Change |
|---|---|
| `firmware/crystal_plinth/crystal_link.h` + `firmware/crystal_oracle/crystal_link.h` (byte-identical) | Additive, `LINK_VERSION` stays 1: `LT_CHEST_INFO = 0x06` (P→S), `LT_CHEST = 0x85` (S→P), `struct LinkChest` (17 bytes: action, tier, port, color, ms), `struct LinkChestInfo` (20 bytes: cmdId, result, port, armedMask, openMask, stuckMask, ageS; ageS 0xFFFF = never heard), `CHEST_QUERY = 0` (only answer LT_CHEST_INFO), a note that hoard packets use magic 'C','H' |
| `hoard_chests/firmware/hoard_hub/hoard_link.h` (follow-up, 2026-11-12) | hoard packets get their own magic **'C','H'** (`HOARD_MAGIC0/1`, `HOARD_VERSION 1`) and helpers `hoardFillHeader()` / `hoardHeaderOk()`; the `LinkHeader` copy stays (skipped after crystal_link.h) |
| `hoard_chests/firmware/hoard_hub/hoard_hub.ino` | receive filter, STATUS send and command check use the hoard helpers / magic (3 lines) |
| `firmware/crystal_plinth/hoard_link.h` | new: a byte-identical copy of the hub's file (included after `crystal_link.h`) |
| `firmware/crystal_plinth/crystal_plinth.ino` | v1.4.0. `#define CHEST_RELAY 1` (default; `-DCHEST_RELAY=0` removes all of it). `LT_CHEST` → `LT_CHEST_CMD` broadcast 3× 40 ms apart, fresh `cmdId` (random start, never 0); `LT_CHEST_BEACON` every 2 s; each hub `LT_CHEST_STATUS` → `LT_CHEST_INFO` to the screen/browser (only while linked). All scheduled from `loop()` and `pump()` (no `delay()`; repeats keep their 40 ms spacing during tag reads/writes). ESP-NOW mode reuses the screen link's radio: its receive callback also lets 'C','H' packets in, and `handlePacket()` hands a hub STATUS to the relay before any screen check (it never adopts the hub). Hoard packets are sent with `hoardFillHeader()` ('C','H'). Wi-Fi mode starts ESP-NOW next to the station (`esp_now_init` after `WiFi.mode(STA)`, broadcast peer on channel 0 = the AP's channel), with its own receive callback + queue; broadcasts only while Wi-Fi is connected. Status line gains `hub heard N s ago, cmds N`; serial `c` = test award tier 1 |
| `firmware/crystal_plinth/plinth_wifi.h` | unchanged |
| `station/js/link.js` | `LT.CHEST`, `LT.CHEST_INFO`, `CHEST` actions, `link.chest(action, tier, port, color, ms)`, `link.chestInfo()` |
| `station/js/settings.js` | `cfg.chests` (default false), key `station.chests` |
| `station/js/show.js` | `chestAward()`; called in `runStep()` when `cfg.chests` and the Reveal step's bowl becomes visible, and once after the reveal step as the fallback; `seq.chestSent` makes it once per visit |
| `station/js/service.js` | host menu → *Reader*: a *Chests (Hoard Hub)* block, only when `chests = yes`: hub heard N s ago, armed / open / stuck per tier, last result, buttons Test Silver / Gold / Gem (AWARD 1/2/3) and Open all (reload) (two-tap). Opening the tab sends a `CHEST_QUERY` |
| `station/js/main.js` | `BUILD = '2026-11-12a'` |
| `station/fake_plinth.js` | accepts `LT_CHEST`, logs `chest: <ACTION> tier N ...`, a pretend hub (15 chests, all armed) answers `LT_CHEST_INFO` (award after 1.2 s, tier empty when out); console `refill` |
| `firmware/crystal_oracle/settings.h/.cpp` | `cfg.chests` (default false), `[station] chests` in the FIELDS table (so the menu's config save keeps it) |
| `firmware/crystal_oracle/reader.h/.cpp` | `reader::chest(...)` (plinth backend only, one unicast LT_CHEST, fire and forget); `LT_CHEST_INFO` logged on Serial on a change |
| `firmware/crystal_oracle/show.cpp` | `chestAward()` + the same two call sites as the station app; `Seq.chestSent` |
| `docs/LINK-PROTOCOL.md`, `station/README.md`, `firmware/crystal_plinth/README.md` | short notes |

Old firmware against new: an old plinth logs `link: unknown packet type 0x85 ignored` and does
nothing (checked in `handlePacket()`: the `default:` branch); an old screen ignores `0x06`
(`reader.cpp handlePacket` `default:`); the old station app's `onPacket` switch has no case for it.

## Voice lines (2026-11-15, station app only)

With `chests = yes` the station app (`station/js/show.js`) swaps Butterscotch's bowl lines for chest lines
(recorded, 5 takes each; text in `tools/gen_manifest_dragonglow.py`):

| When | Group | Was |
|---|---|---|
| crystal visit, at the award | `chest_common` / `chest_rare` / `chest_legend`, caption `<bowl name> Chest` (words `chest.<key>`) | `bowl_*` |
| rush mode | `chest_rush_t0..3` | `rush_t*` |
| blessing (tap / button / foreign) | `chest_none`, no bowl icon (no chest opens) | `bowl_common` |
| replay (already played tonight: they have their candy, no more) | `replay_done` after the replay line, `second_helpings` ignored | nothing, or `bowl_common` with `second_helpings` |
| the hub answers this visit's AWARD with result 1/2/3 (empty / stuck / busy) | `chest_empty` as its own short step after the reveal, else before goodbye; caption `caption.chest_empty` | - |

`chest_empty` needs a reply with a **new** cmdId after the award (`chestFailed()`): the hub's 5-s status repeats the
last cmdId, so an old failure never counts, and no reply at all (hub off) says nothing. A group falls back to the
bowl line when its files are missing. **The Waveshare firmware (`show.cpp`) still plays the bowl lines.**
Headless check (scratch copy, 2026-11-15): chests off = bowl lines as before; on = `chest_rare_5`,
`chest_legend_3`, `chest_none` ×3; `FAKE_HUB_EMPTY=1` = `chest_empty_2` / `chest_empty_1` after each reveal; forced
rush = `chest_rush_t2_4/2/1`, `chest_rush_t0_2` (trick → tier 1 award). 0 page errors in all four runs.

## Turning it on

**iPad station (the 2026 rig):** the station reads `/oracle/config.ini` from the card it serves
(`sd_card_ipad/oracle/config.ini` when that exists, else `sd_card/oracle/config.ini`). Add under
`[station]`:

```ini
[station]
chests = yes
```

Then reload the page. (Without editing the file: the browser console
`station.settings.setConfig('station.chests', 'yes')`, remembered in that browser.)
Note `tools/build_assets.py` may regenerate `config.ini` only when it is missing; add the line
again if the card is rebuilt from scratch.

**Waveshare screen (TF card):** `/oracle/config.ini` on the card, same line under `[station]`.
The host menu's config save keeps it (it is in the firmware's FIELDS table).

**Flash:**
* **Plinth** (needed for either rig): `crystal_plinth.ino` 1.4.0, `CHEST_RELAY 1` (default).
  iPad rig: build with `PLINTH_LINK_WIFI 1` as before. Hold BOOT on the AITRIP.
* **Screen**: only if the Waveshare rig is used with chests (an un-reflashed screen ignores the
  hub: its packets carry magic 'C','H', see below).
* **Hub**: as its own README says; group 1 = the plinth's `LINK_GROUP`. Hub and plinth must both
  be built from the 'C','H' `hoard_link.h` (the two copies are byte-identical).

## Tests run (2026-11-12, no hardware)

```powershell
# link headers identical
python -I crystal_oracle/tools/build_assets.py --check
#   -> "check: 0 error(s), 7 warning(s), 69 note(s) ... [crystal_link.h: both copies identical (LINK_VERSION 1)]"
#      (the 7 warnings are the existing chapter2_* clip notes)

$cli = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
& $cli compile --fqbn esp32:esp32:esp32 firmware\crystal_plinth
#   -> Sketch uses 947207 bytes (72%)                 (ESP-NOW mode, relay on)
& $cli compile --fqbn esp32:esp32:esp32 --build-property "compiler.cpp.extra_flags=-DPLINTH_LINK_WIFI=1" firmware\crystal_plinth
#   -> Sketch uses 1002651 bytes (76%)                (Wi-Fi mode, relay on)
& $cli compile --fqbn esp32:esp32:esp32 --build-property "compiler.cpp.extra_flags=-DCHEST_RELAY=0" firmware\crystal_plinth
#   -> Sketch uses 944843 bytes (72%)                 (relay compiled out)
& $cli compile --fqbn "esp32:esp32:esp32s3-octal:CDCOnBoot=cdc,FlashMode=opi,FlashSize=32M,PSRAM=opi,PartitionScheme=app5M_fat24M_32MB,CPUFreq=240,USBMode=hwcdc" firmware\crystal_oracle
#   -> Sketch uses 1273379 bytes (26%), no warnings matched (--warnings default)
```

Headless station test: `tools/station_e2e.js` run on a **scratch copy** (station folder + the
tool copied to the session scratchpad, ports changed to 8471 / fake plinth 8191 / CDP 9343,
`STATION_ASSETS` = `sd_card_ipad\oracle`, read only). The owner's server on 8460 and its
`state/` / `logs/` were not touched. The scratch copy only adds `E2E_CHESTS=1` →
`setConfig('station.chests','yes')` after start, and a click on *Test Gold* in the menu.

| Run | Result |
|---|---|
| `chests = no` | `cfg.chests = false`; `done: 0 page error(s)`; **0** `chest:` lines in the fake plinth log; visits 2 (fresh + returning), replay 1, blessings 2, foreign 1, sleepy 1 - same flow as before |
| `chests = yes` | `cfg.chests = true`; `done: 0 page error(s)`; fake plinth: `chest: AWARD tier 1` at the fresh crystal's bowl line (event `chests: award tier 1 (prize tier 1)`), `chest: AWARD tier 2` for the returning crystal (`prize tier 2`); nothing for the replay, the button / tap blessings, the foreign tag, the unreadable crystal or the crystal placed in the host menu = **exactly one AWARD per rolled visit (2 visits, 2 awards), tiers right**. Host menu: `QUERY` on opening *Reader*, *Test Gold* → `AWARD tier 2`, pretend hub opened chest 7, the block showed Silver 4/1/0, Gold 3/2/0, Gem 5/0/0, "ok, chest 7 opened" |

Prize tiers 0 (trick) and 3 (gem) did not come up in the random rolls; their mapping is the same
one-line expression (`tier <= 1 ? 1 : tier`).

## Untested on hardware

* Nothing here has run on a board: the plinth relay, the hub hearing it, the status coming back.
* **Wi-Fi mode ESP-NOW next to the station connection**: broadcasts on the AP's channel, the hub's
  channel hopping finding it, and whether the extra radio traffic disturbs the WebSocket. If the
  router changes channel, the hub loses the beacon for up to 20 s and then hops.
* The 40 ms repeat spacing while a crystal is read (scheduled from `pump()`; long PN532 commands
  can still stretch it to ~100 ms).
* The Waveshare screen sending `LT_CHEST` (compiled only) and its Serial log of `LT_CHEST_INFO`.
* Per-tier counts in the host menu assume the hub's **default port tiers** (1-5 / 6-10 / 11-15):
  the hub's STATUS has masks, not its tier map (`tier <port> <t>` on the hub changes it silently).

## Why the hoard packets use magic 'C','H' (fixed 2026-11-12)

With the original 'C','O' magic, `LT_CHEST_STATUS = 0x11` (bit 7 **clear**) looked like plinth →
screen traffic. The already-flashed Waveshare screen firmware accepted it: when its plinth had
been silent for 3 s (boot, plinth unplugged, reboot) it **adopted the hub as its plinth**, and the
hub's 5-s status then kept `reader::online()` true and hid "READER ASLEEP" while the real plinth
was gone. The hub starts on channel 1, the screen's channel, so any hub near the Waveshare rig
would have done it.

Fix: every hoard packet (beacon, command, status) now carries magic **'C','H'**
(`hoard_link.h` `HOARD_MAGIC0/1`, `hoardFillHeader()` / `hoardHeaderOk()`). Every screen and plinth
firmware ever flashed drops a packet whose magic is not 'C','O' at its first check
(`linkHeaderOk()`; the plinth's receive callback too), so no reflash of the screen is needed for
safety. Only the plinth relay (1.4+) reads and writes 'C','H'. The hub had never been flashed, so
no hub with the old magic exists. A first version of this change made the new screen firmware
drop type 0x11 explicitly; that drop was **removed** as dead code (a 'C','H' packet never gets
that far). The station app only sees the WebSocket and was never affected.

### Results after the magic change (2026-11-12)

```
hub    compile --fqbn esp32:esp32:esp32 --warnings all   -> Sketch uses 944052 bytes (72%), no warnings
plinth ESP-NOW mode                                       -> Sketch uses 947275 bytes (72%)
plinth -DPLINTH_LINK_WIFI=1                               -> Sketch uses 1002695 bytes (76%)
screen (FQBN as above)                                    -> Sketch uses 1273371 bytes (26%)
build_assets.py --check -> check: 0 error(s), 7 warning(s), 69 note(s) ... [crystal_link.h: both copies identical (LINK_VERSION 1)]
hoard_link.h hub copy == plinth copy (SHA-256 B8B12098...8467A6)
station e2e, chests = yes (scratch copy) -> done: 0 page error(s); AWARD tier 1 (prize tier 1),
  AWARD tier 2 (prize tier 2), menu Test Gold -> AWARD tier 2; nothing else
```
