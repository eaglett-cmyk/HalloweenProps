# Hoard Chests PCBs: notes (2026-11-12)

The two boards from `docs/DESIGN.md`: the **Hoard Hub** (one per show) and the **Hoard Node** (one per chest).
Both are 2-layer, 1.6 mm, generated from scratch by a script. There is no schematic, so the netlist tables below
take its place.

| File | What |
|---|---|
| `gen_boards.py` | Builds both boards: placement, own A* grid router, GND pours, GND repair, silkscreen. Run `"C:\Program Files\KiCad\9.0\bin\python.exe" gen_boards.py [hub\|node]`. It takes about 1 minute and gives the same result every time. |
| `make_outputs.py` | Runs DRC, exports Gerbers and drill files, zips them and renders the images. Run `"C:\Program Files\KiCad\9.0\bin\python.exe" make_outputs.py [hub\|node] [--drc-only]`. |
| `hub/hoard_hub.kicad_pcb` (+ `.kicad_pro`) | The hub board. The `.kicad_pro` holds the design rules, so keep the two files together. |
| `node/hoard_node.kicad_pcb` (+ `.kicad_pro`) | The node board. |
| `*/<name>-drc.rpt`, `-drc.json` | The acceptance DRC (errors only). |
| `*/<name>-drc-all.rpt` | The same DRC with warnings included. |
| `*/<name>_gerbers.zip` | What you upload to JLCPCB. |
| `*/<name>-top.png`, `-bottom.png` | 3D renders. |
| `*/<name>-top-layers.svg`, `-bottom-layers.svg` | 2D plots of the copper and silkscreen layers. |

**Coordinates.** All coordinates in these notes are in mm, with X to the right, Y **up** and the origin at the
board's bottom-left corner. Inside the `.kicad_pcb` the same corner sits at KiCad page (50, 50+H), and KiCad's Y
axis points down.

## Final DRC

Both runs used `kicad-cli pcb drc --severity-error`. In KiCad 9 schematic parity is off unless you pass
`--schematic-parity`, so the `--schematic-parity=false` form in the brief is not needed.

| Board | Violations (errors) | Unconnected items | Warnings (`--severity-all`) |
|---|---|---|---|
| Hub | **0** | **0** | 2 silk_overlap (cosmetic: C2 value, J_PWR2 value), 5 lib_footprint_issues (see note 1) |
| Node | **0** | **0** | 1 silk_overlap (cosmetic), 1 lib_footprint_issues |

No violation is excluded or suppressed. The rules are 0.25 mm clearance for the default netclass, 0.3 mm tracks,
0.8/0.4 mm vias, 0.3 mm copper to edge, 0.25 mm hole clearance and 0.25 mm hole-to-hole. Courtyard checks use
KiCad's defaults.

Note 1: lib_footprint_issues only means that the ESP32 socket and the mounting holes are custom footprints made by
the script, in a "hoard" library that is not installed. It is harmless.

## Parts and footprints

Every footprint was checked in the stock KiCad 9 `.kicad_mod` files, including pad numbers and positions.

| Ref | Part | KiCad footprint | Notes |
|---|---|---|---|
| J1-J15 (hub), J1 (node) | **Amphenol 54602-908LF** 8P8C RJ45, unshielded, horizontal, through-hole | `Connector_RJ:RJ45_Amphenol_54602-x08_Horizontal` | Pins 1/3/5/7 sit in the row nearer the mouth (x = 0, 2.54, 5.08, 7.62) and pins 2/4/6/8 in the row 2.54 mm further back. Two 3.2 mm NPTH locating pegs. KiCad has **no 3D model** for this jack, so the renders show only its outline. |
| U1 (hub) | AITRIP / DOIT ESP32 DevKit V1 30-pin on 2 x 15 female headers | custom `ESP32_DevKitV1_30pin_dual_width` | 1.7 mm pads with 1.0 mm drill. The left row is fixed; the right row is there twice (22.86 and 25.4 mm away) and each pin's two pads are joined by a 0.5 mm track. Pad numbers: 1-15 = EN, VP, VN, 34, 35, 32, 33, 25, 26, 27, 14, 12, 13, GND, VIN (from the antenna end). 16-30 = 23, 22, TX0, RX0, 21, 19, 18, 5, 17, 16, 4, 2, 15, GND, 3V3. |
| U2, U3, U4 (hub) | **CD74HCT4067M96**, SOIC-24 wide, top side | `Package_SO:SOIC-24W_7.5x15.4mm_P1.27mm` | **Changed from DIP-24** at the coordinator's request, because the DIP is no longer sold. The library has no "HandSolder" version; the standard 2.05 x 0.6 mm pads are fine with an iron. |
| U5 (hub) | SN74AHCT125N, DIP-14 | `Package_DIP:DIP-14_W7.62mm_Socket_LongPads` | Long pads and a socket-sized courtyard. |
| RN1-RN4 (hub) | 10 k x 8 bussed SIP-9 (pin 1 common) | `Resistor_THT:R_Array_SIP9` | |
| R1-R4 (hub) | 1/4 W axial | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal` | R1 10 k, R2 4k7, R3 1 k, R4 100 R |
| C3-C6 (hub) | 100 nF ceramic disc | `Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` | The library has no 5.08 mm-pitch disc. Parts with 5.08 mm leads fit the 5.00 mm holes. |
| C1 (hub) | 1000 uF 10 V radial, 10 mm diameter, 5 mm pitch | `Capacitor_THT:CP_Radial_D10.0mm_P5.00mm` | |
| C2 (hub) | 100 uF 10 V radial, 6.3 mm diameter, 2.5 mm pitch | `Capacitor_THT:CP_Radial_D6.3mm_P2.50mm` | |
| C1 (node) | **470 uF 10 V** radial, **8 mm diameter, 3.5 mm pitch** | `Capacitor_THT:CP_Radial_D8.0mm_P3.50mm` | Coordinator change. It fits inside 42 x 20 mm, so the board did not grow. |
| SW1 (hub) | 6 mm tactile switch "TEST" | `Button_Switch_THT:SW_PUSH_6mm` | |
| J_PWR (hub) | CUI PJ-102AH 5.5 x 2.1 mm barrel jack | `Connector_BarrelJack:BarrelJack_CUI_PJ-102AH_Horizontal` | Pad 1 = centre pin = +5V, pad 2 = sleeve = GND, pad 3 = switch, tied to GND. |
| J_PWR2 (hub) | 2-pin 5.08 mm screw terminal (Phoenix MKDS 1,5/2-5,08) | `TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal` | Pin 1 = +5V, pin 2 = GND. |
| J3 (node) | 5-pin 3.5 mm screw terminal (Phoenix PT 1,5/5-3,5-H) | `TerminalBlock_Phoenix:TerminalBlock_Phoenix_PT-1,5-5-3.5-H_1x05_P3.50mm_Horizontal` | See "Node pin order" below. |
| J16, J_I2C (hub), J2 (node) | 2.54 mm pin headers (1x5, 1x4, 1x3) | `Connector_PinHeader_2.54mm:PinHeader_1x0N_P2.54mm_Vertical` | |
| H1-H4 (hub), H1 (node) | 3.4 mm NPTH mounting hole | custom | Each hole has a copper keep-out zone: 6 mm radius on the hub, 3.5 mm on the node. |

### ESP32 web check

The brief's expected pin order is the common DOIT 30-pin pinout, and I used it unchanged. Two things I could not
confirm from a text source:

- **Pin order:** the pages I opened put the pin labels only in images.
- **Row spacing:** no page states it. One page gives the DOIT board as about 28.2 x 51.8 mm. That width only fits
  rows 25.4 mm (1.0 in) apart. Narrower clones (about 23.4-25 mm wide) use 22.86 mm (0.9 in) rows.

The dual-width socket takes either width. **Check your own board against the silkscreen before soldering.**

With the left row fixed at y = 68.58, the USB centre sits at y = **55.88** for a 25.4 mm board and y = **57.15**
for a 22.86 mm board. The brief asked for 56. The VIN pin is at x = 5.08, so the USB end of the dev board sits near
x = 1-2 and the connector reaches the board edge.

## Netlist (stands in for the schematic)

Each pad is listed as Ref.pad. For U1, see the pad numbering above.

### Hub

| Net | Pads |
|---|---|
| `+5V` | J_PWR.1, J_PWR2.1, C1.1, C2.1, C3.1, C4.1, C5.1, C6.1, U1.15 (VIN), U2.24, U3.24, U4.24, U5.14, J16.1, Jn.4 + Jn.5 for n = 1..15 |
| `GND` | J_PWR.2, J_PWR.3, J_PWR2.2, C1-C6 pin 2, U1.14 + U1.29 (both GND), U2.12, U3.12, U3.15 (/E), U4.12, U4.15 (/E), U5.1/4/7/9/10/12/13, RN1-RN4 pin 1, SW1.2, J16.2, J_I2C.2, Jn.2/6/8 for n = 1..15 |
| `+3V3` | U1.30 (3V3), R1.2, R2.2, J_I2C.1 |
| `SERVO_n` (n = 1..15) | Jn.3, U2 channel I(n-1), pull-down RN1 (n = 1-8) or RN2 (n = 9-15) |
| `SERVO_16` | J16.4, U2.16 (I15), RN2.9 |
| `LED_n` (n = 1..15) | Jn.1, U3 channel I(n-1), RN3 (n = 1-8) or RN4 (n = 9-15) |
| `LED_16` | J16.3, U3.16, RN4.9 |
| `SW_n` (n = 1..15) | Jn.7, U4 channel I(n-1) |
| `SW_16` | J16.5, U4.16 |
| mux channel pins | I0..I7 = pins 9..2, I8..I15 = pins 23..16. So RN1.2..9 = SERVO_1..8 and RN2.2..9 = SERVO_9..16, and likewise RN3/RN4 for LED. |
| `SERVO_5V` | U5.3 (1Y), U2.1 (COM) |
| `SERVO_EN` | U1.9 (GPIO26), U2.15 (/E), R1.1 |
| `GPIO16` / `GPIO17` / `GPIO18` / `GPIO19` | U1.25 / U1.24 / U1.22 / U1.21 to U2.10 (S0) / U2.11 (S1) / U2.14 (S2) / U2.13 (S3) |
| `SCAN0` / `SCAN1` / `SCAN2` / `SCAN3` | U1.6 (GPIO32) / U1.7 (GPIO33) / U1.10 (GPIO27) / U1.11 (GPIO14) to U3 and U4 pins 10 / 11 / 14 / 13 |
| `GPIO25` | U1.8, U5.2 (1A); U5.1 (/1OE) = GND |
| `GPIO4` | U1.26, U5.5 (2A); U5.4 (/2OE) = GND |
| `PIXEL_RAW` | U5.6 (2Y), R4.1 |
| `PIXEL_COM` | R4.2, U3.1 (COM); U3.15 (/E) = GND |
| `SW_COM` | U4.1 (COM), R2.1 (4k7 to +3V3), R3.1 (1 k) |
| `GPIO34` | R3.2, U1.4 |
| `SDA` / `SCL` | U1.20 (GPIO21) / U1.17 (GPIO22) to J_I2C.3 / J_I2C.4 |
| `GPIO13` | U1.13, SW1.1 (both pads 1) |
| U5 gates 3 and 4 | Inputs 9 and 12 and /OE 10 and 13 go to GND. Outputs 8 and 11 are not connected. |
| U1 unused pins | Each has its own `U1_<name>` net (EN, VP, VN, IO35, IO12, IO23, TX0, RX0, IO5, IO2, IO15), so the two pads of each dual right-row pin stay joined. |
| RJ45 pin use (each port) | 1 LED_n, 2 GND, 3 SERVO_n, 4 +5V, 5 +5V, 6 GND, 7 SW_n, 8 GND |
| J16 order | 1 +5V, 2 GND, 3 LED_16, 4 SERVO_16, 5 SW_16. The silkscreen reads "5V G LED SRV SW". |

### Node

| Net | Pads |
|---|---|
| `+5V` | J1.4, J1.5, J2.2, J3.5, C1.1 |
| `GND` | J1.2, J1.6, J1.8, J2.1, J3.1, J3.3, C1.2 |
| `SERVO` | J1.3, J2.3 |
| `LED` | J1.1, J3.4 |
| `SW` | J1.7, J3.2 |

**Node pin order.** J2 (servo header) is GND, +5V, SERVO from left to right (servo plug brown, red, orange).

J3's wires enter facing +X. From the back edge (y = 0) the order is **+5V, LED, GND, SW, GND**, which is footprint
pads 5, 4, 3, 2, 1. The PT footprint numbers its pins away from the back edge when the block is turned to face +X,
so I assigned the nets in reverse; the order the user sees is the one the brief asks for.

The silkscreen has "5V", "D", "G", "SW", "G" next to each top-side pin, and the string "5V D G SW G" on the bottom
silkscreen beside the pins. The top-side labels sit inside the 3.5 mm circle around the mounting hole. That is ink
only, no copper and no part.

## How the boards were laid out and routed

### +5V

- **Hub trunk:** a 1.5 mm track on B.Cu forming a U. It runs **under the jack bodies**, 2.75 mm in front of the
  front pin row (between the pins and the locating pegs) along the top, right and bottom edges, so the signal
  escapes from the jacks never have to cross it.
- **Jack stubs:** each jack has a 1.2 mm stub from pin 5 to the trunk and a 1.2 mm link from pin 4 to pin 5 (the
  brief asks for at least 0.8 mm).
- **Feeds:** a 1.5 mm feed runs from J_PWR2 to the top trunk. Another runs from the barrel jack through port 6's
  +5V pins to the bottom trunk. A 1.5 mm track also joins the barrel jack to C1.
- **Branches to the chips** are 0.8 mm. On the node all +5V is 0.8 mm or wider, with 1.2 mm inside the jack.

### GND

- **Pours:** GND zones on both layers with about 140 stitching vias. GND pads of the RJ45s use a solid zone
  connection.
- **Explicit GND tree on the hub:** a 0.3 mm track tree joins the GND pads of the interior parts (U1-U5, RN1-RN4,
  C1-C6, J16, J_I2C, SW1). It is routed before the signals so that no pad gets boxed in by tracks.
- **Repair pass:** after the pours are filled, a check pass connects any GND island it finds.
- **Solid pads where the thermal was starved:** pads where DRC found a thermal relief starved of spokes were
  switched to a solid connection. In the final build these are C1.2, RN1.1 and RN2.1. They need a hot iron.

### Signals

- 0.3 mm tracks, routed by the script's own maze router: 0.635 mm grid, two layers, 45-degree moves, vias,
  through-hole pads usable as layer changes.
- It uses rip-up and retry. The final pass routes all 70 nets.
- Signal pins of the jacks leave through fixed straight stubs that pass between the back-row pads.

## Measurements for the printed parts

### RJ45 (both boards, Amphenol 54602-908LF)

| Item | Value | Source |
|---|---|---|
| Body width | 15.30 mm | KiCad fab outline. An RS listing agrees (15.3 x 17.74 x 12.7 mm). |
| Body depth (front face to rear) | 17.74 mm | Same sources. |
| Height above the board | **about 12.7 mm, not verified** | RS listing. Amphenol's drawing (c-bmj-0102) could not be fetched (HTTP 403). Allow at least 13.5 mm. |
| Front face | **3.00 mm beyond the board edge** on every jack | |
| Inside the board | 14.74 mm of the body | |
| Locating pegs | 3.2 mm NPTH, 7.62 mm behind the front face (4.62 mm inside the edge), ±5.715 mm either side of the jack centre line | |
| Pin rows | 13.97 mm and 16.51 mm behind the front face | |
| Latch window | Orientation follows the KiCad footprint. **Not verified**, because there is no 3D model. | |

Body outline position by edge:

| Board | Edge | Body outline (from the edge) | Peg centres |
|---|---|---|---|
| Hub | top | x = cx ± 7.65, y = 97.26 to 115.00 | (cx ± 5.715, 107.38) |
| Hub | bottom | x = cx ± 7.65, y = -3.00 to 14.74 | (cx ± 5.715, 4.62) |
| Hub | right | x = 109.26 to 127.00, y = cy ± 7.65 | (119.38, cy ± 5.715) |
| Node | J1, y = 0 edge | x = 1.85 to 17.15, y = -3.00 to 14.74 | (3.785, 4.62) and (15.215, 4.62) |

Hub jack centres: cx = 16, 35, 54, 73, 92 for the top (ports 1-5) and bottom (ports 6-10) edges; cy = 18, 37, 56,
75, 94 for the right edge (ports 11-15).

### Hub (124.0 x 112.0 mm, 3 mm corner radius)

| Item | Position / size |
|---|---|
| Mounting holes | 3.4 mm NPTH at (4, 4), (120, 4), (4, 108), (120, 108). No copper within 6 mm. |
| Plastic near the holes | The bodies of **J1 and J6 come within 4.35 mm** of the left-hand hole centres. Their positions are fixed by the brief. An M3 head (5.5 mm) still clears them. |
| Barrel jack J_PWR | Mouth flush with the left edge (x = 0), centred at **y = 22**. Body 9.0 x 14.4 mm (x = 0 to 14.4, y = 17.5 to 26.5), height about 11 mm per CUI (not measured here). |
| Screw terminal J_PWR2 | Wire entry facing -X, front at x = 0.5. Pins at x = 5.715, y = 87.63 (+5V) and 92.71 (GND). The block covers y = 85.1 to 95.3. |
| ESP32 sockets | Rows at y = 68.58 (fixed) and y = 43.18 / 45.72. Pins at x = 5.08 (VIN / 3V3) to 40.64 (EN / IO23). The dev board outline covers about x = 1.6 to 53.4, y = 41.8 to 70.0. |
| TEST button SW1 | Pads at (10.16 / 16.66, 72.97 / 77.47). |
| Spare header J16 | x = 86.36 to 96.52 at y = 21.59. |
| I2C header J_I2C | x = 41.91 to 49.53 at y = 26.67. |

Tallest parts on the hub:

| Part | Height above the board |
|---|---|
| C1 1000 uF / 10 V | Typically 16-20 mm, depending on the brand (10 x 16 or 10 x 20 mm). |
| ESP32 on female headers | About 14 mm (8.5 mm socket + board + module), plus the USB plug. |
| RJ45 | About 12.7-13.5 mm. |
| MKDS terminal | The KiCad model is about 10-14 mm. |

### Node (42.0 x 20.0 mm, 1 mm corner radius; the board did not grow)

| Item | Position / size |
|---|---|
| Back edge | The y = 0 edge sits against the chest's back wall, and the jack mouth sticks 3.0 mm through it. |
| Mounting hole | 3.4 mm NPTH at (30.0, 5.0). No part or copper within 3.5 mm of its centre; only silkscreen labels sit inside that circle. |
| J2 servo header | Pins at x = 21.59 (GND), 24.13 (+5V), 26.67 (SERVO), y = 16.51. The pins stand 8.5 mm above the board, and the servo plug sits on them. |
| J3 terminal | Wire-entry face at x = 42.0 (flush with the right edge). Body x = 34.4 to 42.0, y = 1.25 to 18.75. Pins at x = 38.9, y = 3.0 / 6.5 / 10.0 / 13.5 / 17.0 (+5V, LED, GND, SW, GND). The KiCad model is about 9.2 mm tall. |
| C1 | 8 mm diameter, centre (22.07, 5.715). Typically 11.5-12.5 mm tall (8 x 11.5 is common for 470 uF / 10 V). |

The tallest part on the node is the RJ45 (about 12.7-13.5 mm) or C1 (about 12 mm), depending on the part you buy.

## Ordering from JLCPCB

1. Go to jlcpcb.com, choose "Order now" and add a Gerber file:
   - **Hub:** `hub/hoard_hub_gerbers.zip`
   - **Node:** `node/hoard_node_gerbers.zip`

   Each zip holds F/B Cu, F/B Mask, F/B Silkscreen, Edge.Cuts, and separate PTH and NPTH Excellon drill files
   (plus drill maps and a job file, which JLCPCB ignores).
2. Settings:
   - Base material FR-4
   - **2 layers**
   - Leave the dimensions as detected: 124 x 112 mm for the hub, 42 x 20 mm for the node.
   - **PCB thickness 1.6 mm**
   - Any colour, HASL (lead-free if you like)
   - 1 oz outer copper (see the caveat below)
   - Remove order number: "Specify a location" or "No" (paid) if you care. There is no reserved spot for it on these
     boards.
3. Quantity: **5 hubs** and **20 nodes** (two separate line items).
4. No assembly. Every part is hand-soldered; the three SOIC-24 muxes are the only SMD parts.

## Things to know or double-check

- **Copper current.** A 1.5 mm 1 oz trunk carries about 3 A comfortably. All 15 ports at 0.7 A each (10.5 A) would
  overload it and the barrel jack. The design counts on firmware: one servo moves at a time and pixel brightness is
  capped. Order 2 oz copper if you want margin.
- **RJ45 height and latch side** are not verified. KiCad has no 3D model of the 54602, and the Amphenol drawing was
  blocked.
- **Terminal-block wire-entry side (J_PWR2, node J3).** I took it from the KiCad footprint geometry (the open "front"
  is the footprint's -Y side). The 3D models do not show the wire openings clearly. Check one real part before the
  printed walls are fixed.
- **ESP32 pin order and row spacing** were not confirmed from a dimensioned source (see "ESP32 web check").
- Some silkscreen labels sit close to others. Two values (C2, J_PWR2) touch neighbouring silkscreen; this is a
  cosmetic warning only.
- **Coordinator changes made in this round:**
  - Hub muxes are SOIC-24W CD74HCT4067M96 instead of DIP-24.
  - Node C1 is 470 uF / 8 mm / 3.5 mm pitch instead of 100 uF / 6.3 mm.
- **My own departures from the brief:**
  - The barrel jack is centred at y = 22 instead of 20, because at y = 20 its courtyard overlapped J6's.
  - J_PWR2 is centred near y = 90 instead of 92, so it clears J1.
  - The ESP32 USB centre is at y = 55.88 instead of 56 (grid-aligned).
  - The 100 nF capacitors use 5.00 mm pitch instead of 5.08.

## Changelog

- **2026-11-12:** First version. Both boards generated, DRC 0/0, Gerbers zipped. Mux package changed to SOIC-24W
  and node C1 to 470 uF / 8 mm, both at the coordinator's request.
