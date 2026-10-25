# Hoard Stand art pass ? completed

## Design

Only `ipad_stand_art.scad` was edited among the existing source files. `ipad_stand.scad` is byte-for-byte identical to `.functional_baseline.scad` (SHA-256 `622a52b3a92f69227d3b4c3ca06843ce851cb0d8314e2e424b03f443c22758b8`).

Opened all five requested reference images as local files: the cave, crystal egg, coin, gem and v7 plinth assembly.

The stone body now has seven-sided bedrock terraces, an irregular faceted escarpment across the front seat wall, and three tall tapering cave spires around the backrest. The hoard includes four raised-star coins, four wrapped sweets, two spiral-grooved boiled sweets, four gumdrops and two loose quartz gems. Broad stone shelves support the treasure relief. Coin borders and spiral grooves are 1.5 mm wide; grooves are 1.5 mm deep, and raised motifs are chunky enough to read in a single stone-grey filament.

The separate crystals have distinct silhouettes:

- **Dragonberry:** broad asymmetric five-point quartz crown.
- **Rainbow Fizz:** fourteen-sided open egg cup with alternating jagged rim heights and three growing quartz shards. Its solid floor remains above the keyed pin hole.
- **Starfrost:** slender central spear with a lower outward-sweeping fan.

The stone has clearance pockets of radius 24.6 mm above the socket seating plane. These preserve removal clearance around the 24 mm crystal envelopes. All cluster roots are solid around their original automatically cut D-holes.

## Rendering and visual iteration

Rendered with the installed OpenSCAD 2021 CLI. Opened and inspected front, side and top previews, iterating on the seat-wall silhouette, rock heights, crystal roots, egg mesh triangulation and supported relief edges. The final front/side cameras use distance 740 to keep the complete ghost iPad in frame. All three PNGs are 1400 ? 1000.

Final elapsed times below include process startup. Independent jobs ran concurrently, so these are not isolated benchmarks. OpenSCAD's reported body geometry render time was **30.223 s**, comfortably below three minutes.

| Output/check | Elapsed |
| --- | --- |
| body.stl | 30.54 s |
| cluster_dragonberry.stl | 0.97 s |
| cluster_rainbowfizz_egg.stl | 1.17 s |
| cluster_starfrost.stl | 0.87 s |
| pin.stl | 0.39 s |
| art_front.png | 30.87 s |
| art_side.png | 30.79 s |
| art_top.png | 31.72 s |
| iPad/sightline check | 2.50 s |
| Body/cluster seating check | 37.49 s |

## Validation

- All five final STL exports reported **Simple: yes; Volumes: 2** in OpenSCAD, representing one closed solid each. No warnings appeared in those final exports.
- Independent STL edge/connectivity checks found one connected component per export, every edge shared by exactly two triangles, and positive signed volume.
- The required `part="check"` run reported **`Current top level object is empty.`** OpenSCAD returns exit code 1 for this successful empty export. No `check.stl` is retained, avoiding a stale file from an earlier iteration.
- An additional exact body/installed-clusters intersection contained only zero-thickness seating contact: every resulting vertex was at **z=22.0 mm**, with no volumetric overlap. This optional contact test naturally reports a non-manifold surface, because it consists of the three intended contact patches rather than a printable solid.
- A downward-face mesh scan found no unsupported surfaces exceeding 45 degrees above the bed in the body or decorative crystal geometry. The only downward ceilings in the clusters are the unchanged, approximately 6.2 mm keyed pin-hole roofs at z=4.19 mm; these require the short bridge inherent in the original functional design.
- All exported parts have a flat z=0 bottom. Body bounds are **204 ? 153 ? 174 mm**. Cluster heights are 33, 33 and 33.5 mm, below their 34 mm cap; all stay within radius 24 mm.
- No body vertices enter the short-end keepout (|x| > 92 mm above z=50 mm). The original seat, 7 mm lip, slot, backrest contact face, sockets and all automatic functional cuts are preserved; the art only adds supported geometry outside those cuts.

| STL | Triangles | Connected components | Height |
| --- | ---: | ---: | ---: |
| body.stl | 4558 | 1 | 174 mm |
| cluster_dragonberry.stl | 322 | 1 | 33 mm |
| cluster_rainbowfizz_egg.stl | 468 | 1 | 33 mm |
| cluster_starfrost.stl | 278 | 1 | 33.5 mm |
| pin.stl | 80 | 1 | 4.8 mm |

## Delivered files

`ipad_stand_art.scad`, `body.stl`, `cluster_dragonberry.stl`, `cluster_rainbowfizz_egg.stl`, `cluster_starfrost.stl`, `pin.stl`, `art_front.png`, `art_side.png`, `art_top.png`, and this `CODEX-RESULT.md`.

Print the body upright and each cluster root-down. Print three copies of the unchanged pin, in its exported flat orientation.

## Remaining uncertainty

No physical print, slicer simulation or hardware fit test was performed. Dimensional fit and the original short pin-hole bridging depend on printer calibration and filament. Geometry and visual checks passed; no unresolved modeling issue was found.
