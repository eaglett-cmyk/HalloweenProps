# Wakestone NFC capstone — completed

## Design

Changed only `nfc_platform_art.scad` among the existing files. Created a broad,
asymmetric, low-poly altar slab with broken corners, two broad chisel clefts,
and a recessed, bevelled bedding seam around its sides. The seam reads as two
heavy stone courses in a single filament. Viewed all four supplied references
and inspected the assembly, top and underside previews, iterating the outline,
seam and camera framing.

The top remains a single flat, unengraved plane at local z=12. The entire
20 mm radius landing circle around [-8.5,-8.5] stays clear. The original
2 mm top plate, hollow, plug and platform height are preserved. Art starts at
z=2; there is no raised top decoration. Full-height perimeter clefts avoid
unsupported blind crack ceilings. No libraries, imported meshes or Minkowski
operations are used.

An engraved target ring was optional and is omitted: the functional frame
unions a full-height plain plug with the art, filling any art-only engraving
over that footprint. Side relief and the irregular silhouette provide the
stone character without changing the protected frame.

## Deliverables

- `nfc_platform_art.scad`
- `nfc_platform.stl`
- `platform_assembly.png` (1400 x 1000)
- `platform_top.png` (1000 x 1000)
- `platform_bottom.png` (1000 x 1000)
- This result file.

## OpenSCAD verification

Used the installed OpenSCAD **2021.01** CLI. Final geometry runs had no warnings.
Times below are wall-clock seconds; processes sometimes ran concurrently.

| Output/check | Time | Result |
|---|---:|---|
| Platform STL | 10.265 s | Exit 0; Simple: yes; **Volumes: 2** |
| Body/crystal collision check | 45.301 s | Exit 1; **Current top level object is empty.** |
| Assembly preview | 34.333 s | Exit 0; inspected |
| Top preview | 13.159 s | Exit 0; inspected |
| Bottom preview | 14.013 s | Exit 0; inspected |

No `check.stl` remains. Preview camera rotations and centres follow the brief;
distances were widened to 390 (assembly), 275 (top) and 245 (bottom) so the
objects fit in frame. Tomorrow colour scheme retained. Standalone previews
use OpenSCAD's default blue; the assembly shows the stone/crystal colours.
The assembly has OpenCSG coplanar striping at existing crystal-root/body
contacts; the exact CGAL collision check is empty.

## Exported STL scan

Parsed every ASCII STL triangle with Python's standard library. Computed
normals from vertex cross products, transformed outward normals for the
print orientation z'=12-z, and checked every downward-facing triangle.
Only triangles whose three vertices lie on z'=0 were excluded as bed faces.
Surface angle from vertical is asin(abs(normal_z)/normal_length); a
non-bed downward face exceeding 45.01 degrees would be reported as a failure
(0.01 degree numerical tolerance).

| Measurement | Result |
|---|---:|
| Triangles | 392 |
| Vertices | 198 |
| Bed triangles excluded | 35 |
| Other downward-facing triangles | 62 |
| Maximum angle from vertical | **42.88236 degrees** |
| Faces violating 45 degrees | **0** |
| Nonmanifold edges (incidence other than 2) | **0** |
| Degenerate triangles | **0** |
| Connected components | **1** |
| Installed bounding box | x [-43,43], y [-42,41], z [0,12] mm |
| Solid volume | 27,868.34 mm^3 |

The scan includes the seam, clefts, envelope cuts and plug intersections.
It agrees with the one-solid OpenSCAD result. Geometry supports printing
upside down without supports as requested.

## Protected files and remaining uncertainty

SHA-256 hashes match the values captured before editing:

- `nfc_platform.scad`: `90B9B0E908F2CA52328A7A2825F88AEFFF9AA78300E8C3B1413ED9DB914C7175`
- `crystal_plinth_v7.scad`: `10E5E3F8B0608DD5BE20FE5BDBF1BB7EE317F2890F743FD6249029F9854E5D1A`

No physical print, insertion test or NFC read test was performed. Those are
the remaining practical checks; CAD fit, height, plate thickness and print
surface angles pass. Project-wide notes were left untouched to follow the
task's restriction on editing existing files.
