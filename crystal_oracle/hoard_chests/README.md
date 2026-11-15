# Hoard Chests — pop-open prize chests for the Crystal Oracle

Fifteen small treasure chests, five per prize tier (Silver Coin, Gold Star, Dragon Gem). When the
Oracle awards a tier, one loaded chest of that tier glows, unlatches and pops open. The grown-up
reloads and closes chests between groups; a closed lid tells the hub the chest is ready again.

**Status (2026-11-12): designed, nothing built or tested on hardware.** Print and wire ONE chest
and cycle it a couple of hundred times before printing fifteen. See "Not verified" at the end.

| Folder | What |
|---|---|
| `docs/DESIGN.md` | the contract: how a chest works, hub pins, link packets |
| `cad/hoard_chest.scad` + `cad/stl/` | parametric chest (small + long), latch arm, three emblems |
| `cad/hoard_hub_case.scad` + STLs | case for the hub board |
| `pcb/` | KiCad boards, Gerber zips, `PCB-NOTES.md` (netlist, how to order) |
| `firmware/hoard_hub/` | hub firmware (Arduino, ESP32) + its README (serial commands, calibration) |
| `docs/PARTS-RESEARCH.md` | parts list with part numbers and where to buy, verified dimensions |
| `docs/ORACLE-LINK.md` | how the show tells the hub (plinth relay, config key) |
| `docs/codex/` | Codex's critique of the concept |

## What to print (per chest)

| File | Qty | Filament | How |
|---|---|---|---|
| `chest_small_body.stl` or `chest_long_body.stl` | 1 | wood / stone colour | upright, no supports |
| `chest_small_lid.stl` or `chest_long_lid.stl` | 1 | same | as exported: flat top on the bed, no supports |
| `chest_small_deck.stl` or `chest_long_deck.stl` | 1 | same as the body, or black | as exported (top face on the bed) |
| `chest_backplate.stl` | 1 | same as the body | flat (same plate for both sizes) |
| `latch_arm.stl` | 1 | any PLA/PETG, 100 % infill | flat |
| `emblem_coin.stl` / `emblem_star.stl` / `emblem_gem.stl` | 1 | translucent (Starfrost / a clear gold / Dragonberry or red) | face up |

**What the kids see (2026-11-13):** the **deck** is a flush cover over the back compartment, so an
open chest shows only the prize bay and a plain shelf behind it. The servo, switch, node board and
wiring are underneath; only two narrow slots (for the lid's latch tongue and switch finger) and one
small hole (manual release) are left. The **backplate** closes the channel on the back that holds
the rubber band and the lid's tail. The pixel and its wires were already hidden (behind the emblem
and under the floor). Still in view from behind: the hinge and the patch cable, so stand the chests
with their backs to a wall or backdrop.

**Screws: #4 x 1/2" flat-head** (the ones bought for the Wakestone plinth cover) everywhere a
screw goes into printed plastic: 7 per chest (1 node board, 2 deck, 4 backplate) and 4 in the hub
case, so **109 for the full set of 15**. Pilot holes are 2.5 mm and deep enough for the full
1/2". The only other screws are the two tiny ones in each servo's bag (a #4 does not fit through
the servo's 2 mm tab holes).

Small = bay 110 x 58 x 40 mm (outside 114 x 86 x 62 with the lid). Long = bay 160 x 58 x 40 mm for
full-size bars (outside 164 x 86 x 62). Suggested set: 10 small (Silver, Gold) + 5 long (Gem).
0.2 mm layers, 3 walls, 15 % infill is plenty. Rough guess 70 to 90 g per small chest.

Change sizes in the `.scad` (`bay_w`, `bay_d`, `bay_h`) and re-export with
`openscad -o out.stl -D "part=\"body_print\"" -D "size=\"small\"" hoard_chest.scad`.
`part="check"` must stay (almost) empty: it tests every moving part against every fixed one.

**Measure your servo first.** The latch height depends on two SG90 numbers that vary between
makers: `sv_tab_z` (bottom of the body to the underside of the mounting tabs, drawn as 16.0) and
`sv_boss` (bottom of the body to the top of the round gear boss, drawn as 26.7). If yours differ by
more than 0.5 mm, change them and re-export the body.

## Parts per chest

SG90 servo, lever microswitch (Omron SS-01GL, or SS-5GL / KW12-3), one pre-wired WS2812B pixel
disc (10 mm) with its 10 cm lead, one Hoard Node board (RJ45, 3-pin header, 5-way screw terminal,
470 uF), one Ethernet patch cable, a #10 or #12 rubber band, a hinge pin (1.75 mm filament or a
straightened large paper clip, as long as the chest is wide), seven #4 x 1/2" flat-head screws, two
short wires for the switch. Full list with part numbers: `docs/PARTS-RESEARCH.md`.

## Assembling a chest

1. **Pixel**: push the pixel disc into the round cavity in the lock plate (LEDs facing out), feed
   its three input wires down the channel, along the groove under the floor and up through the
   hole into the back compartment. A dab of hot glue holds the disc; tape over the groove. Press
   the emblem cap into the cavity. (Long chest: the 10 cm lead is about 3 cm short. Splice on
   an extension.)
2. **Switch**: solder two wires to the switch's COM and NO lugs, drop the switch into its pocket
   against the partition, lever up.
3. **Node board**: wires into the screw terminal in this order from the back of the chest:
   `5V` (pixel +), `D` (pixel data in), `G` (pixel ground), `SW` and `G` (the two switch wires).
   Nose the RJ45 jack into the window in the back wall, lower the board onto its post, one #4 x 1/2" screw (stop when it is snug: the flat head bears on the board).
4. **Servo**: drop it between the two brackets, shaft end towards the node board
   (the shaft must line up with the slot block on the partition; the cable leaves under the bracket), two servo screws down through the
   tabs. Plug it into the 3-pin header: brown = GND, red = 5V, orange = signal.
5. **Arm**: do not fit it yet. Plug the chest into the hub, run `lock <port>` from the serial
   console so the servo sits at its lock position, then press the arm onto the spline pointing
   straight at the partition (tip in the slot) and fit the horn screw. `open <port>` must swing
   it a quarter turn clockwise (seen from above) to lie over the servo. Fine-tune with
   `cal <port> lock <us>` / `cal <port> open <us>`.
6. **Deck**: tuck the wires down, lay the deck in (countersinks up, the round hole over the horn
   screw, the notch round the slot block), two #4 x 1/2" screws into the corner bosses. Run
   `open` / `lock` once more: the arm must swing freely under it.
7. **Lid**: hold the lid in place, push the hinge pin through all the knuckles and the tail.
8. **Rubber band**: lark's-head the band through the hole in the lid's tail, hook the free loop
   into one of the two slanted slots in the fin on the back wall (lower slot = stronger). The lid
   must snap fully open and stay open. Then screw the backplate onto the two cheeks (four
   #4 x 1/2"). Take it off to remove the bands for storage.
9. Close the lid and hold it half a second: the latch turns, the emblem starts to breathe.

Manual release (dead servo, no power): small Phillips screwdriver through the hole in the back
slope of the lid (and the hole in the deck under it), onto the horn screw, a quarter turn clockwise.

## The hub

Build the board from `pcb/PCB-NOTES.md`, flash `firmware/hoard_hub` (see its README), 5 V 4 A
supply. Ports 1-5 Silver, 6-10 Gold, 11-15 Gem. **The patch cables are not Ethernet: never plug a
chest or the hub into a network port.** TEST button: short press = award the next tier, hold
2 s = open everything for reloading, hold 6 s = identify sweep. `arm confirm` on the serial console
makes closed chests wait for a TEST press before they count as loaded (stops kids re-arming
empty chests).

## Not verified

* Nothing has been printed, soldered, flashed or run. The firmware compiles; the boards pass
  KiCad's design-rule check; the CAD passes its own collision check. That is all.
* Latch height against a real SG90 (see "Measure your servo first"), the press fit of the arm on
  the spline (`spline_d`, 4.7 mm), the switch operating point (`sw_op`), the RJ45 window against
  the real jack, the deck's clearance over the arm and horn screw (0.8 mm drawn), and whether a #10/#12 band pops a loaded lid crisply.
* Pixel data and servo pulses through the multiplexers over 1 to 2 m of patch cable.
* The link from the show to the hub has only been tested against the fake plinth.
