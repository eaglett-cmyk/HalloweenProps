# Hoard Chests — design contract (2026-11-12, rev 2: pop-open boxes)

Owner's decision (2026-11-12): **15 pop-open treasure chests, 5 per prize tier**, one prize in each,
reloaded by the grown-up between groups. Each chest **detects when its lid is closed**, so the
system knows it has been reloaded and is available again. (Rev 1, three bulk hopper dispensers,
was dropped before anything was drawn.)

| Ports | Tier | Prize | Glow |
|---|---|---|---|
| 1–5 | 1 | Silver Coin (common, also goblin tricks) | cool white |
| 6–10 | 2 | Gold Star (rare) | amber gold |
| 11–15 | 3 | Dragon Gem (legendary) | ruby red |
| 16 | spare | header on the hub | — |

## One chest ("box")

3D-printed PLA, two sizes from one parametric file: **small** (bay 110 x 58 x 40 mm: fun-size candy,
small toys) and **long** (bay 160 x 58 x 40 mm: full-size bars).

* **Lid** hinged at the back on a pin (1.75 mm filament or a paper clip). A **rubber band** between
  a tail on the lid and a hook low on the back wall pulls the lid open and holds it open.
* **Latch**: an SG90 micro servo stands in a compartment at the back of the box. Its printed arm
  swings 90 degrees in a horizontal plane (clockwise from above = release). Locked, the arm passes
  through a window in a tab that hangs from the lid and its tip sits in a slot in the box, so a pull
  on the lid goes into the box, not into the servo. The lower edge of the window is a ramp that takes
  up the play as the arm turns in. The servo only gets pulses while it moves (about half a second);
  it stays powered. **Manual release:** a small Phillips screwdriver through the keyhole in the back
  slope of the lid, on the horn screw, turned clockwise a quarter turn.
* **Lid switch**: a lever microswitch (Omron SS-01GL preferred: gold contacts for the tiny sense
  current; SS-5GL / KW12-3 are the same size, 19.8 x 6.4 x 10.2 mm) in the same
  compartment, pressed by a finger on the lid. Closed lid = switch closed to GND.
* **Light**: one pre-wired WS2812B pixel disc (10 mm) behind a translucent emblem (coin / star /
  gem) on the front of the chest; its three wires run in a groove under the floor to the back.
* **Node board** (42 x 20 mm, in the back compartment): RJ45 jack out through the back wall, 3-pin
  servo header, 5-position screw terminal (pixel 5V / DATA / GND, switch SW / GND), 470 uF.
* **Covers** (2026-11-13, so the kids never see the works): a flush **deck** over the back
  compartment (only the latch-tab slot, the switch-finger slot and the manual-release hole are open)
  and a **backplate** over the rubber band and the lid's tail.
* **Screws**: #4 x 1/2" flat-head (82 degrees) for every screw into printed plastic, the size the
  owner bought for the plinth cover. Pilot 2.5 mm x 11.5 mm deep, clearance 3.4 mm. The servo keeps
  its own two bag screws.
* One ordinary **Ethernet patch cable** per chest to the hub. NOT Ethernet: never plug into a
  network port. Pins: 1 LED data, 2 GND, 3 servo signal, 4 +5V, 5 +5V, 6 GND, 7 lid switch, 8 GND.

Cycle: grown-up puts a prize in, pushes the lid down and holds it about half a second; the hub sees
the switch close, turns the latch, and the emblem starts to breathe in the tier colour = ARMED.
When the Oracle awards that tier, the hub picks one ARMED chest of the tier, flashes it, releases
the latch, the band pops the lid, the switch opens = confirmed OPEN. If the switch does not open
the hub wiggles the latch and retries, then marks that chest STUCK (fast red blink) and uses another.
No ARMED chest left in the tier = it reports TIER EMPTY (the show can say "ask the grown-up").
Optional `arm confirm` mode: a closed chest waits (dim blue blink) until the grown-up presses the
hub's TEST button, so kids cannot re-arm empty chests by closing them.

## Hub board (one), 124 x 112 mm, 5 V only

ESP32 30-pin DevKit (the AITRIP ESP32-WROOM-32 already used for the plinth) on sockets. Three
CD74HCT4067M96 16-channel analog multiplexers (SOIC-24 wide, the only surface-mount parts: the
DIP version is no longer made; must be the HCT type so 3.3 V select lines work at 5 V) fan one servo-pulse pin, one pixel-data pin and one
switch-sense pin out to the 15 RJ45 ports, so there is no chain to break: any port can be empty.
WS2812 pixels keep their colour between updates, so the hub updates them one after another.

| Function | ESP32 GPIO | Notes |
|---|---|---|
| Servo pulse | 25 | through 74AHCT125 (5 V) to the servo mux common |
| Servo mux enable (active low) | 26 | 10 k pull-up to 3V3 = off at boot |
| Servo mux select S0..S3 | 16, 17, 18, 19 | channel = port - 1 |
| Scan select S0..S3 (pixel mux + switch mux) | 32, 33, 27, 14 | channel = port - 1 |
| Pixel data | 4 | through 74AHCT125 (5 V) + 100 R to the pixel mux common |
| Switch sense | 34 | switch mux common, 4.7 k pull-up to 3V3, 1 k series; LOW = lid closed |
| TEST button | 13 | to GND, internal pull-up |
| I2C expansion header | 21 SDA / 22 SCL | 3V3 GND SDA SCL |

10 k pull-downs (SIP-9 bussed arrays) on every servo line and pixel line so idle lines sit low.
Power: 5 V DC, 3 A or more, barrel jack 5.5 x 2.1 centre positive, plus a 2-pin screw terminal in
parallel; 1000 uF bulk capacitor. Only one servo moves at a time; pixels are capped in firmware.

## Link to the Oracle

ESP-NOW broadcast packets with the same 8-byte header layout as `crystal_link.h` (magic, version
1, type, seq, group) but their **own magic 'C','H'** (`HOARD_MAGIC0/1`; crystal_link.h uses
'C','O'). Why (2026-11-12): the screen and plinth firmware drop any packet whose magic is not
'C','O' at their first header check, so no screen or plinth already flashed can mistake a hoard
packet for its own traffic (with 'C','O' the status type 0x11, bit 7 clear, looked like plinth
traffic and a screen whose plinth had gone quiet adopted the hub as its plinth). Only the plinth's
relay (firmware 1.4+, `CHEST_RELAY`) reads and writes 'C','H'. The show (screen firmware or the iPad
station app) tells the plinth at the "bowl" step (`LT_CHEST`, see `ORACLE-LINK.md`); the plinth
broadcasts to the hub. The hub needs no address set up.

* `LT_CHEST_BEACON = 0x91` plinth -> broadcast every 2 s (lets the hub find the channel: on the
  iPad rig the plinth sits on the home Wi-Fi channel, not channel 1). No beacon for 20 s: the hub
  hops channels 1..13, 600 ms each, until it hears one.
* `LT_CHEST_CMD = 0x90` plinth -> broadcast, sent 3 times 40 ms apart with the same `cmdId`.
  Fields: `cmdId` u16, `action` u8, `tier` u8 (1..3, 0 = all), `port` u8 (0 = hub picks),
  `color` u32 (0 = tier colour), `ms` u16.
  Actions: `CA_AWARD` 1 (open one ARMED chest of `tier`), `CA_TEASE` 2 (shimmer the tier's ARMED
  chests for `ms`), `CA_IDLE` 3, `CA_IDENTIFY` 4 (blink `port`), `CA_OPEN_ALL` 5 (reload mode:
  unlatch everything), `CA_OPEN_PORT` 6.
* `LT_CHEST_STATUS = 0x11` hub -> broadcast after every command and every 5 s: `cmdId` echoed,
  `result` u8 (0 ok, 1 tier empty, 2 stuck, 3 busy), `port` u8 (the chest that opened),
  `armedMask` u16, `openMask` u16, `stuckMask` u16, `fwVersion` u16, `uptimeS` u32.

Tier -> chest: trick and coin -> tier 1, star -> tier 2, gem -> tier 3.
