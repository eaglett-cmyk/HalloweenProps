# Codex task: art pass on the Hoard Stand (iPad stand, OpenSCAD)

## What this is
A 3D-printed landscape stand for the iPad that runs the "Dragonglow" Halloween show (Butterscotch, the
sleepy candy dragon, guards a hoard of candy in a cave; children bring crystals that are pieces of
his crystal egg). The functional design is done and is NOT yours to change. Your job is the art: make
it look like a piece of Butterscotch's hoard cave.

Style reference images (open them): `../assets/source/images/bg_cave_lit.png` (the hoard cave),
`../assets/source/images/chapter_egg.png` (the crystal egg), `../assets/source/images/prop_coin.png`,
`../assets/source/images/prop_gem.png`, `../plinth/v7_assembly.png` (the companion crystal plinth this
stand sits next to: faceted 7-sided rocks + hexagonal quartz shards; match that low-poly faceted look).

## Files
- `ipad_stand.scad` — functional frame. **Do not edit it.** Read it to learn the variables and parts.
- `ipad_stand_art.scad` — **the only file you edit.** It defines two modules:
  - `art_body()` — extra STONE geometry in world coordinates (z=0 is the table, -y is the front where
    the children stand). It is clipped to the footprint and then the iPad slot, the sightline cut and the
    crystal sockets are subtracted automatically, so you may overlap the iPad region freely; it will be
    cut away. Ideas: turn the slab, the seat wall and the backrest into a rocky cave outcrop (faceted
    boulders, a stalagmite-ish backrest or a cave-arch/portal ring around it, ledges); a hoard of candy
    and treasure sculpted in stone heaped on the base and spilling around the crystal sockets
    (wrapped sweets, round lollipops with spiral grooves, gumdrops, coins with a star, gem facets,
    a candy cane or two). Everything prints in ONE stone-grey filament, so readability must come from
    shape and relief, not colour; keep relief chunky (no detail under ~1.2 mm, grooves ≥ 1 mm wide/deep).
  - `art_cluster(i)` — one CRYSTAL cluster, printed separately in translucent glitter PLA:
    i=0 **Dragonberry** (purple, left), i=1 **Rainbow Fizz** (centre: make this one read as a piece of the
    hatched **crystal egg** — e.g. a cracked egg-shell half with jagged rim and shards sprouting from it),
    i=2 **Starfrost** (sky blue, right). Local frame: root at z=0, centred on the pin hole at x=y=0.
    Clipped to radius `crystal_r` (24) and height `crystal_cap - socket_top` (34). Give each cluster its
    own silhouette (different counts/leans/heights) in the plinth's hexagonal-quartz style.

## Hard rules (printability + fit)
1. Edit only `ipad_stand_art.scad`. Do not change any variable in `ipad_stand.scad`.
2. Body prints upright on its flat bottom with **no supports**: no overhang steeper than 45° from
   vertical anywhere in `art_body()` (things that bulge out over air must be supported from below — hull
   to the slab, chamfer undersides). The bottom must stay flat (the main file already cuts z<0).
3. Clusters print root-down with **no supports**: the root disc (d≥14 at z=0..3, already provided in the
   main file) must stay, no shard may overhang more than 45°, and nothing may cover the pin hole
   (D-hole, ~6.2 mm, 4.2 deep at the root centre; it is cut automatically, but don't leave a hollow).
4. Keep the backrest's front contact face and the seat/lip working: don't add anything that would
   reach into the iPad slot area from the front of the lip (it gets cut, but check the result still
   holds the iPad: a lip ≥ 6 mm tall must remain in front of the slot along most of its 184 mm length).
5. Leave the iPad's short ends (|x| > 92 above the seat) free: the USB-C cable and speakers are there.
6. Must render in OpenSCAD 2021 in reasonable time (body < ~3 min): prefer `hull()` of low-$fn
   spheres/cylinders and `polyhedron` over big `minkowski()`; no imported meshes, no external libraries.

## How to check your work
OpenSCAD CLI: `"C:\Program Files\OpenSCAD\openscad.com"` (run from this folder).
- `openscad.com -D "part=\"body\"" -o body.stl ipad_stand.scad`
- `openscad.com -D "part=\"cluster\"" -D idx=0 -o cluster_dragonberry.stl ipad_stand.scad` (idx 1 → `cluster_rainbowfizz_egg.stl`, idx 2 → `cluster_starfrost.stl`)
- `openscad.com -D "part=\"pin\"" -o pin.stl ipad_stand.scad`
- `openscad.com -D "part=\"check\"" -o check.stl ipad_stand.scad` → must say the top level object is **empty**.
- Previews: `openscad.com -D "part=\"assembly\"" --imgsize=1400,1000 --camera=0,20,90,70,0,-30,650 --colorscheme=Tomorrow -o art_front.png ipad_stand.scad`
  plus a side view (`--camera=0,20,80,80,0,90,560`) and a top view; look at them and iterate until it
  looks like a treasure-strewn piece of the cave, not a box with bumps.
- The STL must be a single closed manifold (OpenSCAD reports `Volumes: 2` for one solid; check that each
  export is not empty).

## Deliverables
1. The edited `ipad_stand_art.scad`.
2. STLs in this folder: `body.stl`, `cluster_dragonberry.stl`, `cluster_rainbowfizz_egg.stl`,
   `cluster_starfrost.stl`, `pin.stl`.
3. `art_front.png`, `art_side.png`, `art_top.png`.
4. `CODEX-RESULT.md`: what you made, render times, the check result, anything you were unsure about.
