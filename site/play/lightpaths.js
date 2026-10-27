// lightpaths.js — "Cave Lightpaths": plan a glow's route on a 4x4 cave from the Hoard Beacon
// (B) to a door between worlds within an energy budget. Floor costs 1, candy rubble 2, rocks
// block; lanterns +2 each, near door +2, far door +4, +1 per energy left. The boards and the
// solver are Codex's (lightpaths/boards.json, lightpaths/solver.js; generator in
// crystal_oracle/docs/codex/lightpaths/); the server checks a sent route with the same solver.
// Today's cave per band comes from the date; personal bests are kept in this browser only.
(function () {
  'use strict';
  const $ = id => document.getElementById(id);
  const boardEl = $('lp-board'), pathSvg = $('lp-path'), say = $('lp-say');
  const COST = { '.': 1, L: 1, U: 2, N: 1, F: 1 };
  const WORLDS = ['castle', 'star', 'jungle'];
  DG.crystalLine($('pl-crystal'));

  const today = new Intl.DateTimeFormat('en-CA', { timeZone: 'America/Chicago' }).format(new Date());
  const dayNum = Math.floor(Date.parse(`${today}T12:00:00Z`) / 864e5);
  let set = null, band = DG.store('dg.lp.band') || 'gentle', idx = 0, board = null, route = [], sent = false;

  const at = (r, c) => board.rows[r][c];
  const key = ([r, c]) => `${r},${c}`;
  function bStart() { for (let r = 0; r < 4; r++) for (let c = 0; c < 4; c++) if (at(r, c) === 'B') return [r, c]; return [3, 0]; }

  function tally() {
    let cost = 0, lanterns = 0, door = null;
    for (const [r, c] of route.slice(1)) { const t = at(r, c); cost += COST[t]; if (t === 'L') lanterns++; if (t === 'N' || t === 'F') door = t; }
    const left = board.budget - cost;
    return { cost, left, lanterns, door, score: lanterns * 2 + (door === 'F' ? 4 : door === 'N' ? 2 : 0) + (door ? left : 0) };
  }

  function worldsFor(id) {   // which world sits behind each door, fixed per board
    let h = 0; for (const ch of id) h = (h * 31 + ch.charCodeAt(0)) >>> 0;
    const near = WORLDS[h % 3];
    return { N: near, F: WORLDS[(h + 1 + (h >> 3) % 2) % 3] };
  }

  // ---- drawing
  function build() {
    const w = worldsFor(board.id);
    boardEl.replaceChildren();
    for (let r = 0; r < 4; r++) for (let c = 0; c < 4; c++) {
      const t = at(r, c), b = document.createElement('button');
      b.type = 'button'; b.className = `lp-cell lp-${{ B: 'beacon', '.': 'floor', U: 'rubble', R: 'rock', L: 'lantern', N: 'door', F: 'door' }[t]}`;
      b.dataset.r = r; b.dataset.c = c; b.setAttribute('role', 'gridcell');
      const label = { B: 'the Hoard Beacon', '.': 'cave floor, 1 energy', U: 'candy rubble, 2 energy', R: 'a big rock', L: 'a lantern, plus 2', N: `the near door to the ${w.N} world, plus 2`, F: `the far door to the ${w.F} world, plus 4` }[t];
      b.setAttribute('aria-label', label);
      if (t === 'R') b.disabled = true;
      if (t === 'N' || t === 'F') {
        b.innerHTML = `<img src="/img/portal_${w[t]}.webp" alt="" width="512" height="512"><span class="lp-tag">${t === 'F' ? 'far +4' : 'near +2'}</span>`;
      } else if (t === 'L') b.innerHTML = '<span class="lp-flame"></span><span class="lp-tag">+2</span>';
      else if (t === 'U') b.innerHTML = '<span class="lp-candy"></span><span class="lp-tag">2</span>';
      else if (t === 'B') b.innerHTML = '<span class="lp-lamp"></span>';
      boardEl.append(b);
    }
    route = [bStart()]; sent = false;
    $('lp-result').hidden = true;
    render();
  }

  function render() {
    const s = tally(), on = new Set(route.map(key)), last = route[route.length - 1];
    for (const b of boardEl.children) {
      const p = [+b.dataset.r, +b.dataset.c];
      b.classList.toggle('is-on', on.has(key(p)));
      b.classList.toggle('is-end', key(p) === key(last));
      const adj = Math.abs(p[0] - last[0]) + Math.abs(p[1] - last[1]) === 1;
      const t = at(p[0], p[1]);
      b.classList.toggle('is-next', !sent && !s.door && adj && t !== 'R' && !on.has(key(p)) && COST[t] <= s.left);
      b.classList.remove('is-hint');
    }
    const pts = route.map(([r, c]) => `${c * 100 + 50},${r * 100 + 50}`).join(' ');
    pathSvg.innerHTML = route.length > 1 ? `<polyline points="${pts}" class="lp-line-glow"/><polyline points="${pts}" class="lp-line"/>` : '';
    $('lp-energy').textContent = s.left;
    $('lp-bar').style.setProperty('--f', Math.max(0, s.left / board.budget).toFixed(3));
    $('lp-score').textContent = s.door ? s.score : `${s.lanterns * 2}`;
    $('btn-send').disabled = !s.door || sent;
    $('btn-undo').disabled = route.length < 2 || sent;
  }

  // ---- input: tap the next square; tap a square on the path to go back to it
  boardEl.addEventListener('click', ev => {
    const b = ev.target.closest('.lp-cell');
    if (!b || sent) return;
    const p = [+b.dataset.r, +b.dataset.c], k = key(p);
    const i = route.findIndex(q => key(q) === k);
    if (i >= 0) { route = route.slice(0, Math.max(1, i + 1)); say.textContent = 'Back to there.'; render(); return; }
    const s = tally(), last = route[route.length - 1], t = at(p[0], p[1]);
    if (s.door) { say.textContent = 'The path reached a door. Send it, or tap Undo.'; return; }
    if (Math.abs(p[0] - last[0]) + Math.abs(p[1] - last[1]) !== 1) { say.textContent = 'Step to a square right next to the glow (no corners).'; return; }
    if (t === 'R') { say.textContent = 'A big rock blocks the way.'; return; }
    if (COST[t] > s.left) { say.textContent = 'Not enough energy for that step. Try another way, or Undo.'; return; }
    route.push(p);
    const n = tally();
    DG.chime(t === 'L' ? 9 : t === 'N' || t === 'F' ? 12 : Math.min(7, route.length - 1), 0.06);
    say.textContent = t === 'L' ? 'A lantern lit! +2' : t === 'U' ? 'Candy rubble: that cost 2.' :
      t === 'N' ? `The near door! Send it for ${n.score} points, or Undo and look for more.` :
      t === 'F' ? `The far door! Send it for ${n.score} points, or Undo and look for more.` :
      `${n.left} energy left.`;
    render();
  });

  $('btn-undo').addEventListener('click', () => { if (route.length > 1 && !sent) { route.pop(); say.textContent = 'One step back.'; render(); } });
  $('btn-clear').addEventListener('click', () => { if (!sent) { route = [bStart()]; say.textContent = 'A fresh start.'; render(); } });
  $('btn-hint').addEventListener('click', () => {
    if (sent) return;
    const best = board.bestRoute, ok = route.every((p, i) => best[i] && key(best[i]) === key(p));
    if (!ok || route.length >= best.length) { say.textContent = 'A best path goes another way. Try Undo or Clear.'; return; }
    const [r, c] = best[route.length];
    const b = boardEl.children[r * 4 + c];
    b.classList.remove('is-hint'); void b.offsetWidth; b.classList.add('is-hint');
    say.textContent = 'Try the glowing square.';
  });

  $('btn-send').addEventListener('click', async () => {
    const s = tally();
    if (!s.door || sent) return;
    sent = true; render();
    const prev = Number(DG.flag(`lp.${board.id}`) || 0);
    if (s.score > prev) DG.flag(`lp.${board.id}`, s.score);
    const gap = board.best - s.score;
    $('lp-ribbon').textContent = gap <= 0 ? 'Gold ribbon: a best path!' : gap <= 2 ? 'Silver ribbon: so close!' : 'Bronze ribbon: the light got through!';
    $('lp-ribbon').className = `lp-ribbon ${gap <= 0 ? 'is-gold' : gap <= 2 ? 'is-silver' : 'is-bronze'}`;
    $('lp-result-text').textContent = `Your path: ${s.score} points (${s.lanterns} lantern${s.lanterns === 1 ? '' : 's'}, the ${s.door === 'F' ? 'far' : 'near'} door, ${s.left} energy left). The best this cave allows: ${board.best}.` +
      (prev && s.score > prev ? ' A new best for you!' : '');
    $('btn-show').hidden = gap <= 0;
    $('lp-result').hidden = false;
    [0, 4, 7, 12].forEach((p, i) => setTimeout(() => DG.chime(p, 0.07), i * 120));
    $('lp-beacon').textContent = 'Sending the light to the Hoard Beacon…';
    const v = await DG.addSparks('lightpaths', '', { board: board.id, route });
    $('lp-beacon').innerHTML = DG.sparkNote(v);
  });
  $('btn-retry').addEventListener('click', build);
  $('btn-new').addEventListener('click', () => { idx = (idx + 1) % set.bands[band].length; load(); });
  $('btn-show').addEventListener('click', () => { route = board.bestRoute.map(p => p.slice()); render(); say.textContent = `One best path: ${board.best} points.`; $('btn-show').hidden = true; });

  document.querySelectorAll('.lp-bands [data-band]').forEach(b => b.addEventListener('click', () => {
    band = b.dataset.band; DG.store('dg.lp.band', band);
    idx = dayNum % set.bands[band].length; load();
  }));

  function load() {
    document.querySelectorAll('.lp-bands [data-band]').forEach(b => b.setAttribute('aria-selected', String(b.dataset.band === band)));
    board = set.bands[band][idx];
    const best = DG.flag(`lp.${board.id}`);
    say.textContent = (idx === dayNum % set.bands[band].length ? 'Today\'s cave. ' : 'Another cave. ') +
      (best ? `Your best here: ${best}.` : 'Tap a square next to the beacon to start the path.');
    build();
  }

  fetch('/play/lightpaths/boards.json?v=1').then(r => r.json()).then(j => {
    set = j;
    if (!set.bands[band]) band = 'gentle';
    idx = dayNum % set.bands[band].length;
    load();
  }).catch(() => { say.textContent = 'The cave map could not be loaded. Try again in a moment.'; });
})();
