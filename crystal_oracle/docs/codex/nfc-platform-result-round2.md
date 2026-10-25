# Wakestone NFC platform - round 2 completed

## What changed

Edited only `nfc_platform_art.scad` among the source files. Regenerated the
requested STL and all three PNGs. Read the round 1 brief/result and viewed
all four supplied visual references.

- Added two engraved ritual rings centred on `target`, at radii 22.4 and
  30.4 mm, with ten chunky alternating radial/slanted rune notches between.
- Added five angular edge-crack paths and two branches. Their deliberately
  uneven placement breaks up the broad slab without marking the landing area.
- Added nine embedded seven-sided stone chunks around the sides, five
  full-height triangular chisel clefts, and a deeper bevelled course seam.
  The inherited broken-corner outline remains the outer silhouette; chunks
  are clipped to it and to the owner's clearance envelope.
- Kept the original plug, hollow, height, 2 mm base top-plate thickness,
  and the flat radius-20 mm landing circle at z=12. Top decoration is
  exclusively subtractive, through the new `art_engrave()` hook.

Grooves have 2.2 mm mouths and V sections, with nominal 1.2 mm depth.
The actual tip depth is **1.199 mm**, a deliberate 0.001 mm numerical
clearance from the frame's 1.2 mm clipping plane. Exactly coincident tips
produced tiny flat slivers in OpenSCAD 2021; this clearance removes them.
V floors satisfy the strict overhang scan even though groove floors are
above the print bed. No non-bed face was exempted from that scan.
Separate rune strokes and edge cracks that stop short of the rings avoid
coincident intersections. No libraries, imported meshes or Minkowski sums.

## OpenSCAD checks and times

Used OpenSCAD **2021.01**. Final geometry commands produced no warnings.
Times are wall-clock seconds; previews and fit check ran concurrently.

| Deliverable / check | Seconds | Result |
|---|---:|---|
| `nfc_platform.stl` | 16.816 | Exit 0; Simple: yes; **Volumes: 2** |
| Collision / straight-lift clearance check | 42.476 | Exit 1; **Current top level object is empty.** |
| `platform_assembly.png` | 31.353 | Exit 0; 1400 x 1000; visually inspected |
| `platform_top.png` | 17.898 | Exit 0; 1000 x 1000; visually inspected |
| `platform_bottom.png` | 17.953 | Exit 0; 1000 x 1000; hollow and plug inspected |

The STL is OpenSCAD's native **binary STL**, exported with:

```text
openscad.com --export-format binstl -D "part=\"platform\"" -o nfc_platform.stl nfc_platform.scad
```

Binary export retains more coordinate precision than the default ASCII
export near small Boolean intersection triangles. The scan below reads
the final binary file directly, without mesh repair or triangle removal.

PNG cameras retain the specified centres and rotations, with distances
420 (assembly), 275 (top), and 245 (bottom), to keep the objects in frame.
Tomorrow colour scheme retained. The assembly preview has OpenCSG
coplanar striping at the existing crystal-root/body contacts; the exact
CGAL fit check is empty. No `check.stl` remains.

## Final exported STL scan

Parsed all binary STL triangles with Python's standard library. Recomputed
normals from vertex cross products, then used the outward normal transform
for the print orientation z'=12-z. Original upward faces are downward in
the print orientation. For each such face, measured its angle from vertical
as asin(abs(normal_z)/normal_length).

Excluded **only** triangles with all three vertices on flipped z'=0
(1e-6 mm coordinate tolerance). Checked all other downward faces, including
all engraved surfaces, against **45.0 degrees**, without an angle allowance.
Also counted edge incidences, connected components, degeneracies and volume.

| Measurement | Result |
|---|---:|
| Triangles scanned | 2,666 |
| Unique vertices | 1,335 |
| Bed triangles excluded | 547 |
| Other downward-facing triangles | 1,180 |
| Maximum non-bed angle from vertical | **44.867366 degrees** |
| Faces exceeding 45 degrees | **0** |
| Edges with incidence other than 2 | **0** |
| Degenerate triangles | **0** |
| Connected components | **1** |
| Solid volume | 27,716.60 mm^3 |
| Installed bounding box | x [-43,43], y [-42,41], z [0,12] mm |

The closed, connected mesh and the OpenSCAD Volumes: 2 result agree.
The exported platform passes the requested upside-down overhang rule.

## Protected files and practical limits

SHA-256 hashes before and after this round match:

- `nfc_platform.scad`: `EF30C538EF4C6A0D7F52207698CACDA1900B6C70016D5E3AD388E51B2A078143`
- `crystal_plinth_v7.scad`: `10E5E3F8B0608DD5BE20FE5BDBF1BB7EE317F2890F743FD6249029F9854E5D1A`

The frame hash differs from round 1 because the owner supplied the new
engraving hook before this task. Neither protected file was edited here.
Project notes were left untouched to follow this task's source-edit limit.
No physical print, insertion test or NFC read test was performed.
