# Crystal Oracle — the odds

What a kid's chances actually are with the default `[odds]` in `config.ini`.
The numbers come from a 200,000-trial simulation per row that copies
`oracle::decide()` (firmware/crystal_oracle/oracle.cpp) step by step.

## How a visit is decided

Every visit is **one** random roll, laid over four bands, so each `[odds]`
percentage means "percent of visits":

| Band | Size (defaults) |
|---|---|
| LEGENDARY | `legend_pct` = 4 %. Loyalty does not change it. |
| RARE | `rare_pct` 18 % + 3 % per earlier visit (up to +9 %) + up to 5 % for the tap/press charge meter (scaled by how full it got) + the crystal's bench charge (charge 10 = +10 %). Capped at 95 %. |
| TRICK | `trick_pct` = 15 % of visits. Never on a crystal's first visit (`first_no_trick`). |
| TREAT | Everything else. |

* **Pity.** After `pity_after` = 2 visits in a row without a rare, the next visit is guaranteed at least rare. Legendary can still happen.
* **Tricks and treats both go to the common bowl.** Only rare and legendary send a kid to a special bowl.
* **The crystal's number.** It is drawn inside its tier's band (trick 1–39, treat 40–89, rare 90–99, legendary 100). A full charge meter moves it halfway towards the top of the band.

## Per visit (unlimited stock)

| Visit | Legendary | Rare | Trick | Treat | Average number |
|---|---:|---:|---:|---:|---:|
| 1st visit (awakening) | 4.0 % | 17.9 % | 0.0 % | 78.1 % | 71 |
| 2nd visit (last year a treat) | 4.0 % | 21.0 % | 15.0 % | 59.9 % | 66 |
| 4th visit (last year a rare) | 4.0 % | 27.1 % | 14.9 % | 54.0 % | 67 |
| Pity: no rare 2 years in a row | 4.0 % | **96.0 %** | 0 % | 0 % | 95 |
| 2nd visit, charge meter **full** | 4.0 % | 25.9 % | 14.9 % | 55.1 % | 76 |
| 2nd visit, charge meter half | 4.0 % | 23.6 % | 14.8 % | 57.6 % | 71 |
| 2nd visit, bench charge 10 | 4.0 % | 31.1 % | 15.0 % | 49.9 % | 69 |

Loyalty stops adding at the 4th visit (3 earlier visits × 3 % = the 9 % cap).

## Limited prizes (`[stock]`)

With `[stock] rare = N`, the firmware spreads the prizes over the night. The
chance for each visit becomes:

```
chance × clamp(prizes left ÷ (chance × visits still expected), 0.3, 2.0)
```

* "Visits still expected" is `expected_kids` minus tonight's visits so far, and is never less than 1.
* When the stock is used up, the chance drops to 0.
* `legend` is paced the same way.

The table below shows a 2nd-visit crystal (21 % rare unpaced) with `rare = 20`, `legend = 2` and `expected_kids = 200`:

| Moment | Rare left | Legendary left | Legendary | Rare | Trick | Treat |
|---|---:|---:|---:|---:|---:|---:|
| Start of the night | 20 | 2 | 1.2 % | 10.1 % | 14.8 % | 73.9 % |
| Visit 100, on schedule | 10 | 1 | 1.2 % | 10.0 % | 15.0 % | 73.8 % |
| Visit 100, behind (2 given) | 18 | 2 | 2.0 % | 18.1 % | 15.1 % | 64.8 % |
| Visit 100, ahead (18 given) | 2 | 0 | 0 % | 6.3 % | 14.9 % | 78.8 % |
| Visit 190, 5 left | 5 | 1 | 8.0 % | 42.1 % | 15.0 % | 34.9 % |
| Visit 250 (more kids than expected), 3 left | 3 | 1 | 8.0 % | 41.9 % | 15.0 % | 35.1 % |

**Whole nights.** Each case is 1,000 simulated nights.
* "First year" means 2026, when every crystal is new.
* "Later year" means a mix of 50 % first visits, 30 % second, 15 % third and 5 % fourth.

| Night | Rares given (median, 10–90 %) | Legendaries given | Stock ran out |
|---|---|---|---|
| First year, unlimited, 200 kids | 36 (29–43) | 8 (5–11) | — |
| Later year, unlimited, 200 kids | 56 (48–64) | 8 (5–11) | — |
| First year, 20 rare / 2 legendary, 200 kids | 20 | 2 | rare in 93 % of nights, around visit 193 |
| First year, 20 / 2, but **300** kids come | 20 | 2 | always, around visit 194 |
| First year, 20 / 2, only **120** kids come | 12 (9–15) | 1 (0–2) | rare never; legendary 45 % |
| Later year, 20 / 2, 200 kids | 20 | 2 | rare always, around visit **111** |

## What this means for the host

* **Unlimited stock.** Plan on about 1 rare in 5–6 visits in the first year, more in later years, and about 1 legendary in 25.
* **First year with limited stock.** Pacing works well: the prizes last almost exactly to the end of the night. If more kids come than `expected_kids`, the last few rares still go out at the very end, but nothing is left after that.
* **Later years: pity is the big spender.** With `pity_after = 2`, roughly 1 visit in 9 is a guaranteed rare, and pacing cannot slow pity down. A rare stock of 10 % of the kids would then run out around the middle of the night. Either budget about **25–30 % of `expected_kids`** as rare prizes, or raise `pity_after` to 3.
* **Set `expected_kids` honestly.** If it is set too low, prizes go out early. If it is set too high, they bunch up at the end, because the last visits get up to twice the normal chance.
