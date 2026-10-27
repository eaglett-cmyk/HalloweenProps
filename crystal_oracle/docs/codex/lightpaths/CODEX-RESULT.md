# Cave Lightpaths board set and solver

Created all four requested deliverables in this folder:

- `make_boards.js`: seeded, deterministic generation, exhaustive quality checks, validator regression checks, and per-band summaries. Run `node make_boards.js` from any working directory; output goes beside the script.
- `boards.json`: version 1, complete rule metadata, and 36 boards (`g01`–`g12`, `w01`–`w12`, `d01`–`d12`). Each board includes every requested field, with coordinates in `[row, column]` order.
- `solver.js`: dependency-free browser/Node module exporting `validRoute` and `solve`.
- `CODEX-RESULT.md`: this report.

Seed: **3397263398 (`0xCA7E2026`)**, PRNG: Mulberry32. No network or dependencies used. Code targets Node 18+; executed here on Node v24.15.0.

| Band | Boards | Budget | Best score range | Valid routes per board | Optimal routes per board | Trade-off boards | Boards with optimal N route | Candidates tried |
|---|---:|---:|---|---|---|---:|---:|---:|
| gentle | 12 | 12 | 13–17 | 12–119 | 1–3 | 12 | 8 | 150 |
| winding | 12 | 10 | 10–13 | 6–28 | 1–3 | 12 | 10 | 98 |
| deep | 12 | 9 | 7–12 | 3–17 | 1–2 | 11 | 8 | 100 |

“Trade-off boards” counts boards having an optimal route that skips a lantern or ends at N; each band exceeds the minimum of eight. Every bottom-row B column appears exactly three times per band. Both doors are reachable within budget on every board, and every board has at least three valid routes and two distinct scores.

## Quality interpretation

No requirements were relaxed. The generator enforces the following conservative interpretations of the ambiguous quality wording:

- No optimal route may be a shortest far-door route, whether shortest means energy cost or number of steps. These minima are taken over valid door-ending routes, so a route cannot pass through N to reach F.
- If any optimal route collects all three lanterns, its score must exceed the best route collecting fewer than three by exactly one point. A tie with a fewer-lantern alternative is rejected.
- Optimal ties are allowed as specified: `bestRoutes` counts all distinct optimal coordinate sequences, and `bestRoute` is the first encountered in fixed DFS order (up, right, down, left).
- Duplicate exclusion covers all eight square symmetries, with and without swapping N/F, across the entire set.

## Integration

Node: `const { validRoute, solve } = require('./solver.js');`

Browser: load `solver.js` as a classic script, then use `window.Lightpaths.validRoute(board, route)` and `window.Lightpaths.solve(board)`.

Pass the authoritative board object containing `rows` and `budget`. Stored score metadata is ignored during validation. `validRoute` always returns `{ok, cost, score, lanterns, door, error}`; failures have `score: null` and a descriptive error, with cost/lantern/door fields describing progress before rejection. It validates board structure as well as posted coordinates and route rules.

`solve` returns `{best, bestRoute, routes, scores}` plus the additional `bestRoutes` tie count. Scores are sorted numerically ascending. For a well-formed board with no valid routes it returns null best/route, zero counts, and an empty scores array; malformed boards throw an Error.

## Verification

Ran `node make_boards.js` successfully, including all self-checks. The generator cross-checks solver totals, score sets, maxima, and tie counts against a separate exhaustive path enumerator using different adjacency and visited-state implementations. Every route in that second enumeration is also checked through `validRoute`.

Regression checks cover known near/far scoring, rubble cost, an exact-budget finish, over-budget rejection, wrong starts, jumps, diagonals, revisits, rocks, continuing after a door, unfinished routes, malformed coordinates, malformed boards, and an unreachable-board result. A Node VM with only `window` verifies the browser export and matching solve/validation results.

Repeated generation produced byte-identical `boards.json`. SHA-256:

`208ECEE4371A4C7EB3307748DB3CD797FCB706B543F2AEBC5745C8C283E9E794`

All file changes were confined to this folder. No deployment or external project changes were made.
