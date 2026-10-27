# Codex task: board set + solver for "Cave Lightpaths"

Cave Lightpaths is a one-thumb puzzle in the Dragonglow web games (design in
`../game-ideas-result-round2.md`, section B). Claude is writing the game UI; **your job is the boards and
the solver**, in this folder only. Plain Node 18+, no dependencies, no network.

## Rules (exactly these)
- Board: 4 columns × 4 rows. Row 0 is the TOP, row 3 the BOTTOM. Cells are one character each:
  - `B` the beacon (start). Always in row 3. Cost 0 (you start on it).
  - `.` floor: entering costs 1 energy.
  - `U` rubble: entering costs 2 energy.
  - `R` rock: impassable.
  - `L` lantern: floor (cost 1) that scores **+2** the first time the route enters it. Exactly 3 per board.
  - `N` near door, `F` far door: entering costs 1 and **ends the route**. Exactly one of each. Doors never
    sit in row 3. Reaching `N` scores **+2**, `F` scores **+4**.
- A route starts on `B` and steps to orthogonally adjacent cells (no diagonals), never revisits a cell,
  never enters `R`, and must end on a door. Total cost ≤ budget.
- Score = lanterns×2 + door (2 or 4) + (budget − cost).
- Bands: **gentle** budget 12, exactly 1 `R`, 0 `U`; **winding** budget 10, 2 `R`, 2 `U`;
  **deep** budget 9, 3 `R`, 3 `U`.

## Board quality (reject boards that fail any)
1. At least one valid route to each door (both doors reachable within budget).
2. At least **3 distinct valid routes** with **at least 2 distinct scores**.
3. The best-scoring route is NOT simply the shortest route to the far door, and NOT a route that collects
   all 3 lanterns unless doing so is strictly better than every alternative by ≤ 1 point (we want trade-offs:
   in most boards the best route should skip at least one lantern or take the near door).
   Concretely: across each band's 12 boards, at least 8 must have a best route that collects < 3 lanterns
   or ends at `N`.
4. The best score is unique to within the board's top route set (ties are fine, but report how many).
5. No two boards in the whole set are identical, mirror images, or the same after swapping N/F.
6. `B` position varies (use all 4 bottom-row columns across each band).

## Deliver (in this folder)
- `make_boards.js` — deterministic generator (seeded PRNG, seed printed) + exhaustive solver (DFS over all
  simple paths is fine on 4×4) that writes `boards.json` and prints a per-band summary.
- `boards.json` — `{ "version": 1, "rules": {...}, "bands": { "gentle": [board…12], "winding": […12],
  "deep": […12] } }` where each board is
  `{ "id": "g01", "rows": ["....", "..", ...4 strings of 4 chars], "budget": 12, "best": <score>,
     "bestRoutes": <count>, "bestRoute": [[r,c],...], "routes": <total valid routes>,
     "scores": [sorted distinct scores] }`.
- `solver.js` — the solver alone as a small module usable in a browser AND Node
  (`(function (g) { ... })(typeof module === 'object' ? module.exports : (window.Lightpaths = {}))` style),
  exporting `validRoute(board, route) -> {ok, cost, score, lanterns, door, error}` and
  `solve(board) -> {best, bestRoute, routes, scores}`. The server will use `validRoute` to check a posted
  route, so it must reject anything off-rules (non-adjacent, revisit, rock, over budget, not ending on a
  door, continuing after a door, not starting on B).
- `CODEX-RESULT.md` — what you made, the summary table, and anything you had to relax.

Run `node make_boards.js` yourself and make sure it passes its own checks. Don't edit anything outside
this folder.
