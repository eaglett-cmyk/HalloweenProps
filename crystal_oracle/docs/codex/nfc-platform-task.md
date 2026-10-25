# Codex task: stone look for the raised NFC platform (OpenSCAD)

## What this is
The "Wakestone" plinth (`crystal_plinth_v7.scad`) is a crystal-crowned altar for the "Dragonglow" Halloween
show: children set their 3D-printed crystal on top and an NFC reader inside the plinth reads it. The reader
used to sit under a flush 2 mm lid. It is being replaced by a **raised, hollow platform** that plugs into the
same rebate and stands 10 mm above the plinth top. The functional part is done and is NOT yours to change.
Your job is the art: make it look like a **carved stone platform / altar stone** the crystals are offered on,
sitting in the middle of the ring of crystal clusters.

Style references (open them): `v7_assembly.png` and `v7_body_top.png` (the plinth: faceted 7-sided rocks,
13-sided stepped stone base, hexagonal quartz shards — match that low-poly faceted look),
`../assets/source/images/bg_wakestone.png` (the Wakestone in the story art), `../assets/source/images/bg_cave_lit.png`.
Ideas: a thick weathered capstone slab with chipped, faceted edges; a few cracks/chips in the sides; a
slightly irregular (not perfectly rectangular) outline that grows out over the plinth top where there is
room; low stepped or bevelled stone courses; an engraved ring or runes on the top face marking where the
crystal goes (around `target`). It prints in ONE stone-grey filament: readability from shape and relief only;
keep detail chunky (nothing under ~1.2 mm, grooves >= 1 mm wide and deep).

## Files
- `nfc_platform.scad` — functional frame. **Do not edit it.** Read it for the variables. Local frame:
  z=0 is the ledge the plug sits on, z=`lid_t`=2 is the plinth's top surface, z=`z_top`=12 is the platform top.
  Centre x=y=0 is the plinth axis; -y is the front (USB port side).
- `crystal_plinth_v7.scad` — the plinth (do not edit). Used by the frame for the fit checks.
- `nfc_platform_art.scad` — **the only file you edit.** It defines `art_platform()`: extra STONE geometry in
  the local frame. It is intersected with `envelope()` (above z=2, below z=z_top, within r=54, clear of the
  crystal clusters by 1.5 mm swept vertically), unioned with the plain plug block, and then the hollow is cut
  out. So you can overlap the plug/hollow freely; it gets cut.

## Hard rules (printability + fit)
1. Edit only `nfc_platform_art.scad`. Do not change any variable or module in the other two files.
2. **It prints UPSIDE DOWN** (top face on the bed). So in the installed orientation:
   - the top face must be one flat plane at z=`z_top` (anything above is clipped anyway); details on the top
     face may only be **engraved down into it** (max 1.2 mm deep), never raised;
   - **no upward-facing surface flatter than 45° from vertical** anywhere else (those become unsupported
     overhangs when flipped). Shapes should be widest at the top and may flare outward going down only at
     <= 45°. Bevel/chamfer is fine as long as it is on the underside-facing direction (faces looking down).
3. Keep the circle of radius `target_r` (20) around `target` on the top face **flat and free of engraving**
   (that is where the crystal sits over the reader antenna). An engraved ring just outside it is welcome.
4. Do not reduce the platform height or the top plate thickness; don't put anything under z=2 (the
   envelope already prevents it). The platform must still lift straight up out of the plinth.
5. Must render in OpenSCAD 2021 in reasonable time (platform < ~1 min): prefer `hull()` of low-$fn shapes and
   `polyhedron`; no `minkowski()` on big shapes, no imported meshes, no external libraries.

## How to check your work (run from this folder)
OpenSCAD CLI: `"C:\Program Files\OpenSCAD\openscad.com"`
- `openscad.com -D "part=\"platform\"" -o nfc_platform.stl nfc_platform.scad` — must report `Volumes: 2`
  (one closed solid), not empty.
- `openscad.com -D "part=\"check\"" -o check.stl nfc_platform.scad` → must say **`Current top level object is empty.`**
  (exit code 1 is normal for an empty export; delete any check.stl).
- Previews (look at them and iterate until it reads as a carved altar stone, not a box with bumps):
  `openscad.com -D "part=\"assembly\"" --imgsize=1400,1000 --camera=0,0,40,55,0,25,330 --colorscheme=Tomorrow -o platform_assembly.png nfc_platform.scad`
  `openscad.com -D "part=\"platform\"" --imgsize=1000,1000 --camera=0,0,6,55,0,25,180 --colorscheme=Tomorrow -o platform_top.png nfc_platform.scad`
  and one from below (`--camera=0,0,6,235,0,25,180`) to confirm the hollow and the plug are intact.
- Overhang check: scan the exported STL's triangles; in the FLIPPED orientation (z -> z_top - z) report any
  downward-facing face more than 45° from vertical, other than the faces lying on the bed (flipped z=0).
  (Flipped, the hollow becomes an open cup whose floor is the top plate, so it needs no support.)

## Deliverables
1. The edited `nfc_platform_art.scad`.
2. `nfc_platform.stl`.
3. `platform_assembly.png`, `platform_top.png`, `platform_bottom.png`.
4. `CODEX-RESULT.md`: what you made, render times, the check result, the overhang scan, anything unsure.
