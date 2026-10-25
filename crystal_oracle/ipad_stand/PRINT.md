# Printing the Hoard Stand fast (Bambu Lab X1C, Bambu Studio)

Most of the time is the stone body. In Bambu Studio, start from **0.28mm Extra Draft @BBL X1C** and change:

| Setting | Body (stone) | Crystals |
|---|---|---|
| Layer height | 0.28 mm (faceted rock hides the lines) | 0.20 mm |
| Wall loops | 2 | 4 (or 100 % infill) so they glow evenly |
| Top / bottom shells | 3 / 3 | default |
| Sparse infill | **8 % lightning** (only the tops need holding up; the iPad is 460 g) | 100 % or many walls |
| Supports | off | off |
| Orientation | upright, flat bottom down | root down |

Plates: body alone; one plate per crystal colour (add the 3 pins to any of them). Don't put
the three crystals on one AMS plate: the colour swap every layer costs more time and purge than it saves.

Geometry already trimmed for speed (2026-10-24): backrest contact 100 mm up the iPad (was 118),
backrest 70 mm wide (was 90), decoration clipped at 75 mm (`art_top`) since the iPad hides the rear.
Body went from 174 to 142 mm tall and about 15 % less volume. Set `art_top=200`, `rest_h=118`, `rest_w=90`
in `ipad_stand.scad` to get the tall cave spires back.
