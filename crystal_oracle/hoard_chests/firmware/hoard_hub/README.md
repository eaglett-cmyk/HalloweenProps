# Hoard Hub firmware

Controller for the 15 pop-open treasure chests (Hoard Chests). The contract is
`../../docs/DESIGN.md`; this folder is the hub's firmware.

| File | What |
|---|---|
| `hoard_hub.ino` | everything that runs: servo queue, pixel/switch scanner, per-chest state machine, commands, ESP-NOW link, TEST button, serial console |
| `hoard_config.h` | pins, tuning knobs (`#define`s), shared types |
| `hoard_link.h` | the ESP-NOW packets (`LT_CHEST_BEACON` 0x91, `LT_CHEST_CMD` 0x90, `LT_CHEST_STATUS` 0x11), magic 'C','H', helpers `hoardFillHeader()` / `hoardHeaderOk()`. Self-contained; a **byte-identical copy lives in the plinth firmware** (`firmware/crystal_plinth/hoard_link.h`), included *after* `crystal_link.h` there (it then skips its own copy of `LinkHeader`) |

Board: ESP32-WROOM-32 30-pin DevKit (AITRIP), FQBN `esp32:esp32:esp32`, Espressif core 3.x
(built with 3.3.10). Library: **Adafruit NeoPixel** (built with 1.15.5).

## Wiring summary (from DESIGN.md)

| Function | GPIO | |
|---|---|---|
| Servo pulse (LEDC 50 Hz) | 25 | via 74AHCT125 to the servo mux common |
| Servo mux enable, active LOW | 26 | 10 k pull-up = off at boot; the hub enables it only while a servo moves |
| Servo mux S0..S3 | 16, 17, 18, 19 | channel = port - 1 |
| Scan S0..S3 (pixel mux + switch mux) | 32, 33, 27, 14 | channel = port - 1 |
| Pixel data | 4 | via 74AHCT125 + 100 R to the pixel mux common |
| Lid switch sense | 34 | 4.7 k pull-up to 3V3 on the board; LOW = lid closed |
| TEST button | 13 | to GND, internal pull-up |

Per chest (RJ45, a plain patch cable, never into a network port): 1 LED data, 2 GND,
3 servo signal, 4/5 +5 V, 6 GND, 7 lid switch, 8 GND.

**Servos are powered at 5 V all the time.** "Limp" in this firmware only means the hub
sends that servo no pulses (its mux channel is off and the 10 k pull-down holds the line low);
an analog SG90 then stops driving. A *digital* servo may keep holding its last position.

## Build / flash

arduino-cli is the Arduino IDE's bundled copy (not on PATH):

```powershell
$cli = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
& $cli lib install "Adafruit NeoPixel"          # once, if missing
& $cli compile --fqbn esp32:esp32:esp32 "...\hoard_chests\firmware\hoard_hub"
& $cli upload  --fqbn esp32:esp32:esp32 -p COMx "...\hoard_chests\firmware\hoard_hub"
```

The AITRIP board needs **BOOT held** while the upload starts. Serial monitor: 115200, line ending
newline (CR or LF both work).

## What it does

Per chest: **OPEN** (lid up, pixel off) -> lid pushed down -> **CLOSING** (300 ms, dim tier colour)
-> **LOCKING** (latch turning, brighter) -> **ARMED** (slow breathe in the tier colour; 4 s).
Award -> **FIRING** (flash 360 ms, unlatch, the switch must open within 800 ms of the unlock move
starting; if not: re-lock + unlatch, up to 3 unlatch tries in all) -> **OPEN**, or **STUCK** (fast red
blink; the award then uses the next ARMED chest of the tier). A STUCK chest clears itself when its lid
opens, or with `clear <port>`. An ARMED lid that opens without a command (forced) goes to OPEN and the
latch moves to unlock so it can be closed again.

Tier colours: 1 cool white (180,200,255), 2 amber (255,170,0), 3 ruby (255,0,30). Default tiers:
ports 1-5 = 1, 6-10 = 2, 11-15 = 3, 16 = 0 (unused). Award picks the ARMED chest of the tier that
has been ARMED longest (so they rotate), plays a 1.2 s tease (a highlight hops round all the tier's
ARMED chests, slows down and stops on the chosen one, the others fade), then fires it.

**Arming mode** (`arm auto|confirm`, saved, default `auto`):
* `auto`: a lid held shut 300 ms is latched and ARMED.
* `confirm`: a shut lid is latched but waits as **PENDING** (dim blue slow blink, never awarded) until
  the grown-up short-presses TEST or types `confirm`; that arms every PENDING chest. In this mode a
  short TEST press awards a test prize only when nothing is PENDING. (Stops kids closing empty chests.)
  Also applies after power-up: shut lids come back as PENDING. Switching to `auto` arms all PENDING.

**Power-up:** the hub does not know where the latches are. Shut lids get a lock move (then ARMED, or
PENDING in confirm mode); open ones get an unlock move. One servo at a time, in port order (~0.5 s
each, so up to ~8 s for all 15).

**Reload** (TEST held 2 s, `openall`, or `CA_OPEN_ALL`): every chest is unlatched one by one. Lids that
pop go OPEN; lids still shut glow **soft white** (RELEASED) until someone opens them. Close each
refilled lid for ~0.5 s to re-arm it.

Status masks sent to the plinth: `armedMask` = ARMED, `openMask` = lid switch open, `stuckMask` =
STUCK. PENDING / RELEASED / LOCKING chests are in none of them (the documented STATUS struct has no
room for a pending mask, so it was left exactly as DESIGN.md lists it).

## TEST button (GPIO13)

| Press | Action |
|---|---|
| short | confirm mode with PENDING chests: arm them. Otherwise: award the next tier in rotation 1, 2, 3 |
| hold 2 s (all pixels turn orange) | reload: unlatch all |
| hold 6 s (all pixels turn white) | identify sweep: ports light 1..16 in order, then every port blinks its number twice |

The action happens on release; the colour while holding tells you which one you will get.

## Serial commands (115200)

| Command | |
|---|---|
| `status` | table of ports (tier, state, lid, latch, cal values, how long ARMED) + link info |
| `award <tier>` | award tier 1..3 (0 = any tier) |
| `open <port>` | unlatch one chest (with the stuck/retry logic if its lid is shut) |
| `lock <port>` | latch one chest. On an open lid it locks for 3 s so you can look, then goes back to unlock |
| `openall` | reload mode |
| `cal <port> lock\|open <us>` | set that position (clamped 500..2500), save to NVS, move there now; the hub leaves it there 8 s before tidying up |
| `tier <port> <0-3>` | set + save the tier (0 = port unused) |
| `id <port>` | blink the port number twice (`id 0` = sweep) |
| `bright <0-255>` | pixel brightness cap, saved (default 102 = ~40 %) |
| `clear <port>` | clear STUCK etc.: re-read the switch, shut = lock + arm, open = OPEN |
| `arm auto\|confirm` | arming mode (saved) |
| `confirm` | arm every PENDING chest |
| `help` | list |

Everything is logged in plain words: `chest 3 (tier 1): CLOSING -> LOCKING - turning the latch`,
`servo: chest 3 -> lock (1900 us)`, `award tier 1: 4 armed, chose chest 2 ...`.

## Calibration (each chest once)

Defaults: lock **1900 us**, unlock **950 us** (about 90 degrees apart on an SG90).

1. **Fit the arm** with the servo at its LOCK pulse: plug the chest in, lid open, type `lock <port>`;
   while the servo holds the lock position (it is driven for ~0.45 s, then stays where it is), push the
   arm onto the spline so it **points at the partition**. Seen from above, the arm releases **clockwise**.
2. `cal <port> open 950`, watch the arm swing clear of the lid tab; adjust in 50 us steps
   (`cal <port> open 900` ...) until the tab passes freely.
3. Close the lid by hand with the servo at unlock, then `cal <port> lock 1900` and adjust until the arm
   tip goes fully through the tab window into the slot without straining (a buzzing servo = too far).
4. `status` shows the saved values. They survive power-off (NVS, namespace `hoard`).

## Tunable `#define`s (hoard_config.h)

`PIXEL_ORDER` (NEO_GRB; NEO_RGB for the bullet pixels), `BRIGHT_CAP_DEFAULT`, `HUB_GROUP` (must match
the plinth's group, default 1), `ARM_MODE_DEFAULT`, `DEFAULT_LOCK_US` / `DEFAULT_UNLOCK_US`,
`SERVO_MOVE_MS` (450), `CLOSE_DEBOUNCE_MS` (300), `FIRE_EXPECT_OPEN_MS` (800), `FIRE_ATTEMPTS` (3),
`TEASE_DEFAULT_MS` (1200), `PIXEL_REFRESH_MS` (2000), `BEACON_LOST_MS` (20000), `CHANNEL_HOP_MS` (600).

## Link (ESP-NOW, all broadcast)

Listens for `LT_CHEST_BEACON` / `LT_CHEST_CMD` with magic **'C','H'** (`HOARD_MAGIC0/1`, its own:
not crystal_link.h's 'C','O', so no screen or plinth firmware can take a hoard packet for its own
traffic; 2026-11-12), version 1 and its group; its STATUS goes out with the same magic. Starts on
the last channel it heard the plinth on (saved in NVS; channel 1 the first time); no beacon (or command)
for 20 s -> hops channels 1..13, 600 ms each, until it hears one. Commands are de-duplicated by `cmdId`
(the same id within 10 s is a repeat). `LT_CHEST_STATUS` goes out when a command finishes (an award:
when the chest is confirmed open, or STUCK / TIER EMPTY) and every 5 s, echoing the last `cmdId` and
result. Local commands (serial, button) send a status with `cmdId` 0.

Command details the DESIGN left open (choices made here):
* `CA_AWARD`: `port` != 0 = open that chest if it is ARMED (else TIER EMPTY); `color` != 0 replaces the
  tier colour for the tease and flash; `ms` != 0 = tease length. A second command while one runs = `busy`.
* `CA_TEASE`: shimmer the tier's ARMED chests (`tier` 0 = all) for `ms` (0 = 1.2 s).
* `CA_IDLE`: stops a tease and identify blinks; never interrupts an award in progress.
* `CA_IDENTIFY`: `port` 0 = the sweep.
* `CA_OPEN_ALL`: status is sent when the last unlock move has finished.

The hub is fully usable with no plinth: the TEST button and the serial console do everything.

## Test procedure without the Oracle

1. Flash, open the serial monitor: the banner, `link: ESP-NOW up ...` and a state line per port.
   With nothing plugged in, every port reads `lid is open` and gets one unlock move (listen for the queue
   ticking through: `servo: chest N -> unlock`).
2. Plug in one chest on port 1. `id 1`: its emblem blinks once (twice over). `id 0`: sweep.
3. Calibrate it (above).
4. Lid open: `status` shows OPEN / unlocked. Put a prize in, hold the lid down ~0.5 s: CLOSING ->
   LOCKING -> ARMED, emblem breathes cool white.
5. Short TEST press (or `award 1`): tease, flash, unlatch, lid pops, `award done: ok, chest 1 opened`.
6. Stuck test: hold the lid down with a finger and `award 1`: three unlatch tries, then STUCK (fast red);
   let go: it goes OPEN. With two armed tier-1 chests, the award moves on to the second one.
7. Forced-open test: with the chest ARMED, `cal 1 open 950` (unlatches without a command) -> lid pops ->
   `FORCED OPEN ...` -> OPEN, latch at unlock.
8. Hold TEST 2 s -> reload; hold 6 s -> identify sweep.
9. `arm confirm`, close a lid: PENDING (dim blue blink); `award 1` -> TIER EMPTY; short TEST -> ARMED.
10. Link without a plinth: after 20 s `link: no beacon for 20 s - hopping channels 1..13`; `status`
    shows the channel changing. (A second ESP32 broadcasting a `LinkChestCmd` from `hoard_link.h` is the
    way to test commands before the plinth firmware has them.)

## Untested (as of 2026-11-12: compiled only, never run on hardware)

* Nothing has run on a board: pixel latching through the 4067 mux (the 320 us low gap, 40 us settle),
  the switch reads through the mux and 1 k series resistor, the LEDC pulse width accuracy.
* Whole-pulse servo starts/stops (relies on LEDC applying a new duty at the next period).
* Default lock/unlock pulses (1900 / 950 us) against the real printed arm: calibrate every chest.
* The 800 ms open window against the real rubber band; the tease/breathe look; brightness cap.
* ESP-NOW against the real plinth (no plinth firmware sends these packets yet) and channel hopping
  against the iPad rig's Wi-Fi channel.
* Brown-out with a servo starting while 15 pixels are lit (only one servo moves at a time; pixel cap 40 %).
