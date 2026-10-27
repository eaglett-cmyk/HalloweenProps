(function (g) {
  'use strict';

  // Metadata such as board.best is deliberately ignored when validating play.
  function inspect(board) {
    if (!board || !Array.isArray(board.rows) || board.rows.length !== 4 ||
        !Number.isSafeInteger(board.budget) || board.budget < 0) {
      throw new Error('Board needs four rows and a nonnegative integer budget.');
    }
    var cells = '', counts = { B: 0, L: 0, N: 0, F: 0 };
    board.rows.forEach(function (row, r) {
      if (typeof row !== 'string' || !/^[B.RUNFL]{4}$/.test(row)) {
        throw new Error('Each row must contain four legal cell characters.');
      }
      for (var c = 0; c < 4; c++) {
        var tile = row[c];
        if (Object.prototype.hasOwnProperty.call(counts, tile)) counts[tile]++;
        if (tile === 'B' && r !== 3) throw new Error('Beacon must be in row 3.');
        if ((tile === 'N' || tile === 'F') && r === 3) {
          throw new Error('Doors cannot be in row 3.');
        }
      }
      cells += row;
    });
    if (counts.B !== 1 || counts.L !== 3 || counts.N !== 1 || counts.F !== 1) {
      throw new Error('Board needs one beacon, three lanterns, and one of each door.');
    }
    return cells;
  }

  function validRoute(board, route) {
    var cost = 0, lanterns = 0, door = null, cells;
    function fail(error) {
      return { ok: false, cost: cost, score: null, lanterns: lanterns, door: door, error: error };
    }
    try { cells = inspect(board); } catch (e) { return fail(e.message); }
    if (!Array.isArray(route) || route.length < 2 || route.length > 16) {
      return fail('Route must contain 2 to 16 coordinates.');
    }
    var seen = 0, previous;
    for (var i = 0; i < route.length; i++) {
      var point = route[i];
      if (!Array.isArray(point) || point.length !== 2 ||
          !Number.isInteger(point[0]) || !Number.isInteger(point[1]) ||
          point[0] < 0 || point[0] > 3 || point[1] < 0 || point[1] > 3) {
        return fail('Coordinates must be integer [row, column] pairs inside the board.');
      }
      var index = point[0] * 4 + point[1], tile = cells[index];
      if (door) return fail('Route continues after a door.');
      if (i === 0 && tile !== 'B') return fail('Route must start on B.');
      if (seen & (1 << index)) return fail('Route revisits a cell.');
      if (previous && Math.abs(previous[0] - point[0]) + Math.abs(previous[1] - point[1]) !== 1) {
        return fail('Steps must be orthogonally adjacent.');
      }
      if (tile === 'R') return fail('Route enters rock.');
      seen |= 1 << index;
      if (i > 0) cost += tile === 'U' ? 2 : 1;
      if (cost > board.budget) return fail('Route exceeds the energy budget.');
      if (tile === 'L') lanterns++;
      if (tile === 'N' || tile === 'F') door = tile;
      previous = point;
    }
    if (!door) return fail('Route must end on a door.');
    return { ok: true, cost: cost, score: lanterns * 2 + (door === 'N' ? 2 : 4) +
      board.budget - cost, lanterns: lanterns, door: door, error: null };
  }

  function solve(board) {
    var cells = inspect(board), best = null, bestRoute = null, bestRoutes = 0;
    var routes = 0, scores = new Set(), path = [];
    function dfs(index, seen, cost, lanterns) {
      path.push([Math.floor(index / 4), index % 4]);
      var tile = cells[index];
      if (tile === 'N' || tile === 'F') {
        var score = lanterns * 2 + (tile === 'N' ? 2 : 4) + board.budget - cost;
        routes++;
        scores.add(score);
        if (best === null || score > best) {
          best = score;
          bestRoute = path.map(function (p) { return p.slice(); });
          bestRoutes = 1;
        } else if (score === best) bestRoutes++;
      } else {
        var r = Math.floor(index / 4), c = index % 4;
        // Fixed traversal order makes the representative optimal route deterministic.
        [[r - 1, c], [r, c + 1], [r + 1, c], [r, c - 1]].forEach(function (p) {
          if (p[0] < 0 || p[0] > 3 || p[1] < 0 || p[1] > 3) return;
          var next = p[0] * 4 + p[1], t = cells[next];
          var nextCost = cost + (t === 'U' ? 2 : 1);
          if (t !== 'R' && !(seen & (1 << next)) && nextCost <= board.budget) {
            dfs(next, seen | (1 << next), nextCost, lanterns + (t === 'L' ? 1 : 0));
          }
        });
      }
      path.pop();
    }
    var start = cells.indexOf('B');
    dfs(start, 1 << start, 0, 0);
    // bestRoutes is an extra field so callers can display ties without re-enumerating.
    return { best: best, bestRoute: bestRoute, bestRoutes: bestRoutes,
      routes: routes, scores: Array.from(scores).sort(function (a, b) { return a - b; }) };
  }

  g.validRoute = validRoute;
  g.solve = solve;
})(typeof module === 'object' && module.exports ? module.exports : (window.Lightpaths = {}));
