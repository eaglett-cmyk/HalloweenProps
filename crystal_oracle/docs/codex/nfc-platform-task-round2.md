# Codex task, round 2: more stone character on the NFC platform

Round 1 (read `CODEX-RESULT.md` and your current `nfc_platform_art.scad`) is valid and passes every check,
but it reads as a flat plate with a seam — not yet a carved altar stone. Keep everything that works
(outline, print rules, checks) and push the look further.

## New in the frame
`nfc_platform.scad` now has an engraving hook: define **`module art_engrave()`** in `nfc_platform_art.scad`
(local frame). It is subtracted from the finished platform, limited to the top 1.2 mm of the top face and
outside the `target_r` circle around `target` — so you can now engrave the top. The frame is still not yours
to edit; only `nfc_platform_art.scad`.

## What to add
- **Top face:** an engraved ring (or a double ring) just outside the landing circle around `target`, with
  simple chunky runes / notches / a small rising-sun or crystal-egg mark between the rings, plus a few
  engraved cracks running in from the slab edge. Grooves 1.2 mm deep, >= 1.2 mm wide. The landing circle
  itself stays flat (the frame enforces it).
- **Sides:** more carved-stone character within the 45° rule (print is upside down: no upward-facing surface
  flatter than 45° from vertical except the top plane): chipped corners, a few V-shaped chisel cuts and
  vertical cracks, faceted (7-sided rock style) chunks on the sides like the plinth's boulders, a deeper
  shadow line between courses. Look at `v7_assembly.png` again: match its chunky low-poly rocks.
- Same envelope, hard rules, checks and deliverables as round 1 (in `CODEX-TASK-ROUND1.md`, rules 1–5,
  "How to check", the overhang scan of the exported STL). Engraved grooves on the top face lie on the bed
  when printed, so they are fine; the scan's bed-face exclusion should only cover faces at flipped z=0.

## Deliverables
Overwrite: `nfc_platform_art.scad`, `nfc_platform.stl`, `platform_assembly.png`, `platform_top.png`,
`platform_bottom.png`, and `CODEX-RESULT.md` (round 2: what changed, times, check, overhang scan).
