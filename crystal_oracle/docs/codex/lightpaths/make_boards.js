'use strict';

const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { solve, validRoute } = require('./solver.js');
const SEED = 0xCA7E2026;
const SPECS = {
  gentle: { prefix: 'g', budget: 12, rocks: 1, rubble: 0 },
  winding: { prefix: 'w', budget: 10, rocks: 2, rubble: 2 },
  deep: { prefix: 'd', budget: 9, rocks: 3, rubble: 3 }
};

// Mulberry32: all randomness derives from this fixed unsigned 32-bit seed.
function randomSource(seed) {
  return function () {
    seed = (seed + 0x6D2B79F5) >>> 0;
    let t = seed;
    t = Math.imul(t ^ t >>> 15, t | 1);
    t ^= t + Math.imul(t ^ t >>> 7, t | 61);
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}

function candidate(spec, column, random) {
  const cells = Array(16).fill('.');
  cells[12 + column] = 'B';
  function place(tile, count, limit) {
    for (let n = 0; n < count; n++) {
      const available = [];
      for (let i = 0; i < limit; i++) if (cells[i] === '.') available.push(i);
      cells[available[Math.floor(random() * available.length)]] = tile;
    }
  }
  place('N', 1, 12);
  place('F', 1, 12);
  place('L', 3, 16);
  place('R', spec.rocks, 16);
  place('U', spec.rubble, 16);
  return { rows: [0, 4, 8, 12].map(i => cells.slice(i, i + 4).join('')), budget: spec.budget };
}

// Canonicalize the entire square symmetry group, also with N/F exchanged.
// This is stronger than horizontal mirror exclusion alone.
function canonical(rows) {
  const variants = [];
  for (let mirror = 0; mirror < 2; mirror++) {
    for (let turns = 0; turns < 4; turns++) {
      const transformed = Array(16);
      for (let r = 0; r < 4; r++) for (let c = 0; c < 4; c++) {
        let y = r, x = mirror ? 3 - c : c;
        for (let t = 0; t < turns; t++) [y, x] = [x, 3 - y];
        transformed[y * 4 + x] = rows[r][c];
      }
      const key = transformed.join('');
      variants.push(key, key.replace(/[NF]/g, t => t === 'N' ? 'F' : 'N'));
    }
  }
  return variants.sort()[0];
}

// Independent path enumeration used for quality checks and solver cross-checks.
function enumerate(board) {
  const cells = board.rows.join(''), found = [], route = [];
  function walk(index, visited, energy) {
    route.push([index >> 2, index % 4]);
    const tile = cells[index];
    if (tile === 'N' || tile === 'F') {
      const result = validRoute(board, route);
      assert.equal(result.ok, true, result.error);
      assert.equal(result.cost, energy);
      found.push({ ...result, steps: route.length - 1 });
    } else {
      for (let next = 0; next < 16; next++) {
        if (Math.abs((next >> 2) - (index >> 2)) + Math.abs(next % 4 - index % 4) !== 1 ||
            visited.has(next) || cells[next] === 'R') continue;
        const cost = energy + (cells[next] === 'U' ? 2 : 1);
        if (cost > board.budget) continue;
        visited.add(next);
        walk(next, visited, cost);
        visited.delete(next);
      }
    }
    route.pop();
  }
  const start = cells.indexOf('B');
  walk(start, new Set([start]), 0);
  return found;
}

function assess(board) {
  const result = solve(board);
  if (result.routes < 3 || result.scores.length < 2) return null;
  const routes = enumerate(board);
  assert.equal(result.routes, routes.length);
  assert.deepEqual(result.scores, [...new Set(routes.map(r => r.score))].sort((a, b) => a - b));
  assert.equal(result.best, Math.max(...routes.map(r => r.score)));
  const top = routes.filter(r => r.score === result.best);
  assert.equal(result.bestRoutes, top.length);
  const far = routes.filter(r => r.door === 'F');
  if (!far.length || !routes.some(r => r.door === 'N')) return null;
  const minCost = Math.min(...far.map(r => r.cost));
  const minSteps = Math.min(...far.map(r => r.steps));
  if (top.some(r => r.door === 'F' && (r.cost === minCost || r.steps === minSteps))) return null;
  if (top.some(r => r.lanterns === 3)) {
    const alternatives = routes.filter(r => r.lanterns < 3);
    if (!alternatives.length || result.best - Math.max(...alternatives.map(r => r.score)) !== 1) return null;
  }
  const checked = validRoute(board, result.bestRoute);
  assert.equal(checked.ok, true);
  assert.equal(checked.score, result.best);
  return { result, tradeoff: top.some(r => r.lanterns < 3 || r.door === 'N'),
    nearBest: top.some(r => r.door === 'N') };
}

function checkSolver() {
  const board = { rows: ['NF..', 'LUR.', 'L...', 'BL..'], budget: 12 };
  const near = [[3, 0], [2, 0], [1, 0], [0, 0]];
  const far = [[3, 0], [3, 1], [2, 1], [1, 1], [0, 1]];
  assert.deepEqual(validRoute(board, near),
    { ok: true, cost: 3, score: 15, lanterns: 2, door: 'N', error: null });
  assert.deepEqual(validRoute(board, far),
    { ok: true, cost: 5, score: 13, lanterns: 1, door: 'F', error: null });
  const invalid = [null, [], [[3, 0]], [[2, 0], [1, 0], [0, 0]],
    [[3, 0], [1, 0], [0, 0]], // jump
    [[3, 0], [2, 1], [1, 1], [0, 1]], // diagonal
    [[3, 0], [3, 1], [3, 0]], // revisit
    [[3, 0], [3, 1], [2, 1], [2, 2], [1, 2]], // rock
    near.concat([[0, 1]]), // continuing after N
    [[3, 0], [2, 0]], // unfinished
    [[3, 0], [-1, 0]], [[3, 0], [4, 0]], [[3, 0], [3, 4]],
    [[3, 0], [2.5, 0]], [[3, 0], ['2', 0]], [[3, 0], null],
    [[3, 0], [NaN, 0]], [[3, 0], [2, 0, 0]], Array(17).fill([3, 0])];
  for (const route of invalid) {
    const result = validRoute(board, route);
    assert.equal(result.ok, false, JSON.stringify(route));
    assert.equal(result.score, null);
    assert.equal(typeof result.error, 'string');
  }
  assert.equal(validRoute({ ...board, budget: 2 }, near).ok, false);
  assert.equal(validRoute({ ...board, budget: 3 }, near).ok, true);
  for (const bad of [null, {}, { ...board, budget: '12' },
    { ...board, rows: ['NF..', 'LUX.', 'L...', 'BL..'] }]) {
    assert.equal(validRoute(bad, near).ok, false);
    assert.throws(() => solve(bad));
  }
  const blocked = { rows: ['NFL.', 'LR..', 'RRRR', 'B.L.'], budget: 12 };
  assert.deepEqual(solve(blocked),
    { best: null, bestRoute: null, bestRoutes: 0, routes: 0, scores: [] });
  // Exercise the actual script-tag export without Node's module global.
  const context = { window: {} };
  require('node:vm').runInNewContext(fs.readFileSync(path.join(__dirname, 'solver.js'), 'utf8'), context);
  assert.deepEqual(JSON.parse(JSON.stringify(context.window.Lightpaths.solve(board))), solve(board));
  assert.deepEqual(JSON.parse(JSON.stringify(context.window.Lightpaths.validRoute(board, far))),
    validRoute(board, far));
}

function main() {
  checkSolver();
  console.log(`Seed: ${SEED} (0x${SEED.toString(16).toUpperCase()}); Mulberry32`);
  const random = randomSource(SEED), seen = new Set(), bands = {}, summary = [];
  for (const [name, spec] of Object.entries(SPECS)) {
    const boards = [], quality = [];
    let attempts = 0;
    while (boards.length < 12) {
      assert.ok(++attempts <= 1000000, `Candidate limit reached for ${name}; no rules relaxed.`);
      const board = candidate(spec, boards.length % 4, random);
      const key = canonical(board.rows);
      if (seen.has(key)) continue;
      const assessment = assess(board);
      // Guarantee at least eight trade-off boards without relying on chance.
      if (!assessment || (boards.length < 8 && !assessment.tradeoff)) continue;
      const result = assessment.result;
      boards.push({ id: spec.prefix + String(boards.length + 1).padStart(2, '0'),
        rows: board.rows, budget: board.budget, best: result.best,
        bestRoutes: result.bestRoutes, bestRoute: result.bestRoute,
        routes: result.routes, scores: result.scores });
      quality.push(assessment);
      seen.add(key);
    }
    assert.equal(quality.filter(q => q.tradeoff).length >= 8, true);
    assert.equal(new Set(boards.map(b => b.rows[3].indexOf('B'))).size, 4);
    for (const b of boards) {
      const cells = b.rows.join('');
      assert.equal([...cells].filter(t => t === 'R').length, spec.rocks);
      assert.equal([...cells].filter(t => t === 'U').length, spec.rubble);
      assert.equal(b.budget, spec.budget);
      assert.ok(assess(b));
    }
    bands[name] = boards;
    const range = values => `${Math.min(...values)}-${Math.max(...values)}`;
    summary.push({ band: name, boards: boards.length, budget: spec.budget,
      best: range(boards.map(b => b.best)), routes: range(boards.map(b => b.routes)),
      optimalRoutes: range(boards.map(b => b.bestRoutes)),
      tradeoffBoards: quality.filter(q => q.tradeoff).length,
      nearOptimalBoards: quality.filter(q => q.nearBest).length,
      beaconColumns: '0,1,2,3 (3 each)', attempts });
  }
  assert.equal(seen.size, 36);
  const output = { version: 1, rules: {
    columns: 4, rows: 4, rowZero: 'top', beaconRow: 3,
    cells: { B: { cost: 0, start: true }, '.': { cost: 1 }, U: { cost: 2 },
      R: { impassable: true }, L: { cost: 1, score: 2 },
      N: { cost: 1, score: 2, endsRoute: true }, F: { cost: 1, score: 4, endsRoute: true } },
    counts: { B: 1, L: 3, N: 1, F: 1 }, doorsAllowedRows: [0, 1, 2],
    movement: 'orthogonal', revisits: false, mustEndOnDoor: true,
    score: '2 * lanterns + (door === N ? 2 : 4) + budget - cost',
    bands: Object.fromEntries(Object.entries(SPECS).map(([name, s]) =>
      [name, { budget: s.budget, rocks: s.rocks, rubble: s.rubble }]))
  }, bands };
  fs.writeFileSync(path.join(__dirname, 'boards.json'), JSON.stringify(output, null, 2) + '\n');
  console.table(summary);
  console.log('All checks passed; wrote boards.json (36 boards).');
}

if (require.main === module) main();
