# Crystal plinth: review of v4 and fixes in v5 and v6

This report covers a review of `crystal_plinth_v4.scad` and its 7 STL exports, and a revised `crystal_plinth_v5.scad` that fixes the problems found. Every v5 STL was re-exported with OpenSCAD 2021.01 and checked with the mesh tests described below.

Files:
- `crystal_plinth_v5.scad`: the fixed source
- `crystal_plinth_v5_stl/`: body, cover, and `crystal_00`–`crystal_16` (one per entry in `spots`)

## How v4 was checked

- **Mesh integrity:** I parsed each ASCII STL and confirmed that every edge is shared by exactly two faces, in opposite directions (watertight and correctly oriented). I also computed the volume, bounding box, area touching the bed, and downward-facing area steeper than 45°.
- **Body geometry:** I cast vertical rays through `body.stl` at each socket centre and at 12 points around each socket's rim (radius 4.5 mm). This gives the socket floor height, the surface height around the hole, and whether any part of the rim has no material under it. I also probed just inside each corner of the reader pocket.

All 7 v4 meshes are clean and watertight. The problems are in the design itself.

## Issues found in v4

| # | Severity | Issue | Evidence |
|---|---|---|---|
| 1 | High | **The base isn't flat.** The lower rock ring (`translate([55,0,11])`, z-scale 13) and the r=51 rocks go below z=0. | Body bbox z min = **−1.3 mm**. Only **914 mm²** touches the bed. The cover can't sit flush. |
| 2 | High | **6 of 17 sockets break out of the side of the body.** Spots at r ≈ 59–61 (indices 1, 2, 8, 9, 12, 14) are past the edge of the lower d=120 tier, which is a 13-sided polygon whose flats sit at r = 58.25. | Between 5 and 7 of the 12 rim samples have no material under them. Socket 8 is where `crystal_right` goes. |
| 3 | High | **Socket depth varies.** Most sockets straddle the step between the z=39 tier (d=120) and the z=47 tier (d=107). The peg is 8 mm, so a crystal on the lower side floats about 5 mm up with the peg showing. | The surface around a single socket ranges from about z 17–39 up to 47. |
| 4 | Medium | **12 sockets have no crystal.** The body cuts all 17 `spots`, but only 5 clusters are exported (indices 4, 0, 8, 13, 3). | — |
| 5 | Medium | **Socket 0 at (−40, 40) cuts into the corner of the 76 mm reader pocket.** | Distance from the socket centre to the pocket corner (−38, 38) is 2.83 mm, less than the 3.1 mm hole radius. |
| 6 | Medium | **The reader pocket's corners break through the upper rim.** The pocket corner is at r = 53.74, but the d=107 rim (`$fn=64`) has its flats at r = 53.44. | Just inside every corner there's no material above z=39, so each corner is a notch down to the lower ledge. |
| 7 | Low | **The cavity's square corners leave the outer wall about 1–1.4 mm thick** against the 13-sided outer wall (cavity corner at r ≈ 57.5). | Worked out from the geometry. |
| 8 | Low | **The cover's screw holes have no countersink**, so the screw heads stick out under the base and the plinth rocks. | — |
| 9 | Low | **The cover ribs don't locate anything.** They sit at \|x\| 30–34, but the cavity wall is at \|x\| = 37. | — |
| 10 | Question | **The cover notch doesn't line up with the front port.** The notch is at y −57 to −42 at z 0–3; the port starts at z=13. The notch opens only 2 mm into the cavity (which ends at y = −44). Is it meant to be a cable exit? | Left unchanged in v5. |
| 11 | Print note | **Crystals stand on only the 5.6 mm peg** (24 mm² on the bed), and the flat underside of the root is an overhang 8 mm off the bed. | They need supports (tree supports recommended). Not changed in v5. |
| 12 | Print note | **The body's cavity ceiling (z=36) is an unsupported span of about 74 × 88 mm.** | Needs supports inside the cavity; they come out through the open bottom. |

These were fine in v4 and are unchanged:
- 0.3 mm radial peg-to-socket clearance (5.6 mm peg in a 6.2 mm hole), good for a glued fit
- 2.5 mm M3 pilot holes with about 4.75 mm of wall to the cavity
- the wire slot from the cavity up into the reader pocket

## Changes made in v5

1. **Flat base.** `body()` is now wrapped in `intersection()` with a cube starting at z=0. **Fixes #1.**
2. **Socket bosses.** Every spot gets a 7-sided rock pillar: `cylinder(d1=16, d2=12, h=47, $fn=7)`, rotated `i*23°` so they don't look identical. The pillar is unioned into `shell()` before the holes are subtracted. Every socket is now 11 mm deep (top z=47, floor z=36) and completely surrounded by material, and every crystal sits at the same height. The crystal ring stays at the outer edge as in v4. **Fixes #2 and #3.**
3. **Upper tier widened from d=107 to d=112.** This leaves about 2.2 mm of wall around the reader pocket's corners. The lower ledge gets narrower to match. **Fixes #6.**
4. **Two spots moved:**
   - `spots[0]` moved from (−40, 40) to (−42, 42). The hole now clears the pocket corner by about 2.6 mm. **Fixes #5.**
   - `spots[16]` moved from (4, −55) to (22, −52), so its boss isn't cut through by the front port (x −11 to 11).
5. **Rounded cavity.** The cavity is now `linear_extrude(36.2) offset(r=6) square([62,76], center=true)`. It keeps the same 74 × 88 outer size with 6 mm corner radii. At z=20 the thinnest cavity wall is now **3.9 mm**. The only thinner spot is a 2.0 mm sliver beside the front port opening. *(The first version of this report gave 2.0 mm as the cavity-wall minimum; that figure was actually the port sliver.)* **Fixes #7.**
6. **Cover countersinks.** Each screw hole has a 90° countersink on the underside, `cylinder(d1=6.6, d2=3.4, h=1.6)`, for M3 flat-head screws. **Fixes #8.**
7. **Cover ribs moved** to \|x\| 34.6–36.6, which leaves 0.4 mm clearance to the cavity walls so they actually locate the cover. **Fixes #9.**
8. **All 17 clusters exported.** There's a new `part="cluster"` with `-D idx=N`, giving `crystal_00`–`crystal_16`. The old names still work and map as:
   - back = 04
   - left = 00
   - right = 08
   - front = 13
   - small = 03

   **Fixes #4.**

## Verification of v5

- **All 19 STLs are watertight**, with no bad edges.
- **Body:**
  - z range is 0.0 to 47.0
  - bed contact is **5,639 mm²** (v4 was 914)
  - downward-facing overhang area dropped from 10,004 to 6,722 mm²
- **All 17 sockets:**
  - surface is z=47.0 all the way around the rim
  - floor is at z=36.0
  - no rim point is open (0/12 for every socket)
- **Reader pocket corners:** solid to z=47 just inside all four corners.
- **Thinnest cavity wall at z=20:** 3.9 mm, or 2.0 mm counting the sliver beside the front port.

## Follow-up: assembly collision test on v5

**Method:** I placed each cluster at its spot in the installed position, with the root resting on the boss top at z=47 (cluster origin at z=39). I then ran an OpenSCAD intersection for every neighbouring pair (23 pairs with centres less than 32 mm apart). I also ran each cluster against the volume above the 76 × 76 reader pocket.

**Result: 8 pairs overlap. The design can't be assembled as laid out.**

| Pair | Centres apart | Root diameters summed | Cause |
|---|---|---|---|
| 0–1 | 14.2 mm | 19.2 mm | roots overlap |
| 0–2 | 10.6 mm | 17.6 mm | roots overlap |
| 1–3 | 14.1 mm | 15.2 mm | roots overlap |
| 4–5 | 15.0 mm | 21.6 mm | roots overlap |
| 4–6 | 15.0 mm | 20.8 mm | roots overlap |
| 6–7 | 13.9 mm | 15.2 mm | roots overlap |
| 8–9 | 14.9 mm | 18.4 mm | roots overlap |
| 15–16 | 13.6 mm | 13.6 mm | roots just touch; the shards collide |

- **Rotating doesn't help.** I re-tested with every cluster rotated so its lean points outward, and the same pairs still collide. The roots alone (bottom diameter w × 1.6) are wider than the spacing, so no amount of turning the pegs fixes it.
- **Reader pocket:** clusters 0, 8 and 12 reach over the edge of the pocket, above z=47. Their roots extend to 31.6, 33.6 and 36.4 mm from the centre, and the pocket edge is at 38 mm. That only matters if something has to sit flat over the reader.
- **v4 had the same overlaps**, since the spot layout is unchanged.

**Fix options (this is a design decision, not yet made):**
1. Reduce the number of spots and space them about 20 mm or more apart.
2. Merge each overlapping pair into one cluster on a single peg.
3. Shrink the roots, for example to `d1 = min(w*1.6, spacing)`.

The pegs are round, so orientation is still free. Keyed (D-shaped) pegs would lock each cluster's rotation once the layout is final.

## v6: collision fix (`crystal_plinth_v6.scad`, `crystal_plinth_v6_stl/`)

The owner chose to merge overlapping neighbours rather than shrink them.

### What changed

1. **Spots replaced by groups.** The 17 single `spots` are replaced by 11 `groups`, each written as `[angle, radius, [[dr, dt, h, w, lean, rot], ...]]`. A group's local +x points radially outward. Each overlapping pair from the v5 collision test now shares one peg:

   | v6 cluster | Was (v5) | Angle | Radius |
   |---|---|---|---|
   | 01 | 8 + 9 | 28° | 57.5 |
   | 02 | 6 + 7 | 60° | 56 |
   | 03 | 4 + 5 (rear, tallest) | 90° | 55 |
   | 04 | 0 + 2 | 120° | 57.5 |
   | 05 | 1 + 3 | 152° | 55 |
   | 09 | 15 + 16 | 295° | 55 |

   The singles are 00 (was 10), 06 (was 11), 07 (was 12), 08 (was 13) and 10 (was 14). The crystals keep their original sizes.

2. **How a merged cluster is built.** The taller crystal sits outboard. The two crystals are 0.35 × (w1 + w2) apart along the radius, and the second is rotated ±25° for variety. Their roots are joined with `hull()` into one base.
3. **Lean direction.** Every crystal now leans outward; the lean is the absolute value of the original.
4. **Spacing.** The groups were moved away from the reader pocket's diagonal corners, where the top tier is too narrow. Each group's radius was chosen so its root stays at least 1 mm outside the 76 mm pocket. Neither the front port (x ±11) nor the screw bosses is touched.
5. **Keyed pegs.** Pegs and sockets are D-shaped, with the flat 0.8 mm in and facing outward. The socket keeps 0.3 mm clearance, including on the flat. Each cluster therefore seats only at the orientation that was collision-tested.
6. **New parts.** `part="assembly"` renders the body with every cluster installed. The `installed(i)` module places a single cluster in its installed position.

### Verification

- **Collisions:** every one of the 55 cluster pairs was tested in the installed position with OpenSCAD intersection, and every one came back empty.
- **Margin:** the pairs were re-tested with every cluster scaled up **1.2×** in x and y about its peg. Still no overlaps, so there's real clearance between neighbours.
- **Reader pocket:** no cluster reaches over the 76 × 76 pocket (tested from z = 47 upward).
- **Meshes:** all 13 STLs (body, cover, 11 crystals) are watertight with no bad edges.
- **Sockets:** all 11 have surface z = 47.0 all the way around the rim, floor z = 36.0, and no open rim points.
- **Body:** z runs from 0 to 47. Bed contact is 4,883 mm². The pocket corners are solid to z = 47.
- **Cavity wall:** thinnest is 3.7 mm at z = 20, or 2.0 mm counting the sliver beside the front port.
- **Preview:** `crystal_plinth_v6_assembly_preview.png`.

## v7: fitted to the real electronics (`crystal_plinth_v7.scad`, `crystal_plinth_v7_stl/`)

### Inputs

- **Controller:** an ESP32 on a screw-terminal board, about 63 × 66 mm and about 25–30 mm tall (from the owner's photos). The USB-C port sticks out about 2 mm from one edge that has no terminal blocks. Jumper plugs in the screw terminals stick out about 12–15 mm.
- **Reader:** an Elechouse PN532 NFC V3, 42.7 × 40.4 mm (published size), with right-angle plugs on two edges.
- **Owner's decisions:**
  - The reader's wires run to the controller inside the body.
  - A cover must hide the reader.
  - Separate pins are OK for the crystals.
  - Nothing needs to leave through the bottom, so the cover notch is removed.

### Changes

1. **Controller cavity**
   - Turned to 92 wide × 74 deep × 36 tall, with 10 mm corner radii (was 74 × 88).
   - The board's USB edge faces the front port, and each terminal side gets about 13 mm for wires.
   - The front port is unchanged at x ±11, z 13–32, and now reaches the new front wall at y = −37.
2. **Board locating frame**
   - Four L-shaped corner pieces on the cover, 4 mm tall, framing 66.6 × 66.6 mm, so either board orientation fits.
   - The board's USB edge sits at y = −35, 2 mm from the front wall.
   - There are no standoffs yet, because the mounting-hole positions aren't known.
3. **Screws**
   - Moved to (±30, ±42), clear of the wider cavity and inside the d=112 cover with its countersinks.
   - The cover ribs moved to |x| 43.6–45.6, 0.4 mm clear of the new side walls.
4. **NFC pocket** (replaces the open 76 × 76 × 3 recess)
   - Size: 60.5 × 58.2 mm, floor at z = 40, which leaves a 4 mm deck over the cavity. That's the reader plus 17 mm on the +x and +y edges for its right-angle plugs.
   - Two locating bars on the open sides hold the reader against the −x and −y walls.
   - An 8 × 48 mm wire slot under the plug strip drops into the cavity.
5. **NFC lid** (new part `nfc_lid`)
   - 2 mm thick, sitting flush on a 2 mm ledge, 0.15 mm clearance per side (friction fit).
   - A finger notch in the rim lets you lift it out.
   - The reader reads through the 2 mm lid.
6. **Crystals and pins**
   - The pegs are gone. Each crystal has a flat root with a 4 mm-deep D-hole.
   - A separate D-pin (new part `pin`, 14.5 mm long, printed lying on its flat side) joins each crystal to its socket.
   - Each cluster is clipped at z = 0, because steeply leaning side shards on clusters 01 and 05 otherwise dipped 0.1–0.2 mm below the root.
   - Every crystal now prints root-down without supports.
7. **Cover notch removed.**
8. **Printing orientation:** print the body upside down (top face on the bed). The cavity then has no ceiling to support. Only the NFC pocket floor (60 mm span, 7 mm high) needs light supports.

### Verification

- **Meshes:** all 15 STLs (body, cover, nfc_lid, pin, 11 crystals) are watertight with no bad edges.
- **Crystal bed contact:** every crystal now sits flat, with 84–500 mm² on the bed (v6 had 22 mm², the peg tip only). Overhang area dropped from 101–538 mm² to 39–62 mm².
- **Sockets:** all 11 have surface z = 47.0 all the way around the rim, floor z = 36.0, and no open rim points.
- **Cavity wall:** thinnest is 3.9 mm at z = 20 (front port sector excluded).
- **Collision self-test** (`part="collisions"`):
  - empty at `grow=1`
  - empty at `grow=1.2`
  - overlaps found at `grow=1.6` (control: shows the test can detect collisions)
- **Part interference** (OpenSCAD intersection with the body):

  | Test | Result |
  |---|---|
  | Lid in its rebate | empty |
  | Cover against the base | touches face-to-face only; empty with a 0.02 mm gap |
  | Crystals on the bosses | touches face-to-face only; empty with a 0.02 mm gap |
  | Control: cover pushed 1 mm into the body | overlap found, so the test detects real interference |
- **Pin fit:** a pin in the D-hole doesn't intersect the hole wall.
- **Renders:** `v7_assembly.png`, `v7_body_top.png`, `v7_body_bottom.png`, `v7_cover.png`.

## Still open

- **Controller measurements:** the dimensions come from photos (±2 mm). The cavity and frame have margin for that, but confirm the board's outer size and its height before printing.
- **Board standoffs:** the terminal board's four mounting-hole positions are needed to add standoffs in place of the corner frame.
- **Reader seating:** the PN532's plugs set its thickness, estimated at about 4 mm; the lid clears 5 mm. Check it seats before closing the lid.
- **Test prints:** print one socket-and-pin pair, and a corner of the lid rebate, before committing to the full body.
