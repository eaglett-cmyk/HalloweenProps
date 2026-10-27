// beacon.js — "Relight the Beacon": tap the sparks rising from the hoard to send them up
// into the Hoard Beacon (the iron lantern hanging in the cave painting). Snuffwick sneaks in
// to cap the lantern with his snuffer: while he holds it there the sparks you send wait
// under the cap (the travelers' light can't be snuffed, so nothing is ever lost) until you
// tap him away, or he yawns and wanders off. Drawn in the painting's own 1280x768 space.
// The round's sparks are your score; the beacon gets the server's award for a finished game.
(function () {
  'use strict';
  const W = 1280, H = 768;
  const LAMP = { x: 265, y: 192, r: 52 };   // the beacon's glass in bg_cave
  const ROUND_MS = 45000;
  const FULL = 40;                           // sparks that make the beacon blaze this round
  const CAP_MS = 4500;                       // how long he holds the cap on if nobody shoos him
  const TAU = Math.PI * 2;

  const $ = id => document.getElementById(id);
  const cv = $('cv'), g = cv.getContext('2d');
  const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
  DG.crystalLine($('pl-crystal'));

  const img = {};
  for (const n of ['bg_cave', 'snuffwick_base', 'snuffwick_sneeze']) {
    const i = new Image(); i.src = `/img/${n}.webp${n === 'bg_cave' ? '?v=g2' : ''}`; img[n] = i;
  }

  // ---- sizing: the canvas fills the 5:3 stage, drawn in painting coordinates
  let scale = 1;
  function fit() {
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    const w = cv.clientWidth, h = cv.clientHeight;
    cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr);
    scale = cv.width / W;
    g.setTransform(scale, 0, 0, cv.height / H, 0, 0);
  }
  addEventListener('resize', fit);
  fit();

  // ---- state
  let state = 'ready', t0 = 0, now = 0, last = 0;
  let sparks = 0, caught = 0, shoos = 0, caps = 0, held = 0;
  let motes = [], flyers = [], puffs = [], texts = [];
  let nextMote = 0, gob = null, nextGob = 0;
  let glow = 0, pulse = 0;

  const rnd = (a, b) => a + Math.random() * (b - a);

  function reset() {
    sparks = caught = shoos = caps = held = 0;
    motes = []; flyers = []; puffs = []; texts = [];
    gob = null; glow = 0; pulse = 0;
    nextMote = 0; nextGob = 6000;
  }

  function spawnMote(el) {
    const gold = Math.random() < 0.12;
    motes.push({ x: rnd(420, 1220), y: rnd(600, 740), vy: rnd(55, 95) * (1 + el / ROUND_MS * 0.5),
                 ph: rnd(0, TAU), born: now, gold, r: gold ? 17 : 13 });
  }
  function spawnGoblin(el) {
    const dur = 3600 - 1100 * (el / ROUND_MS);   // he gets quicker
    gob = { born: now, dur, x: W + 120, y: 300, state: 'sneak', left: 0, flip: 0 };
  }

  function addText(x, y, s, col) { texts.push({ x, y, s, col, born: now }); }

  // ---- input
  function toGame(ev) {
    const r = cv.getBoundingClientRect();
    return { x: (ev.clientX - r.left) / r.width * W, y: (ev.clientY - r.top) / r.height * H };
  }
  cv.addEventListener('pointerdown', ev => {
    if (state !== 'play') return;
    ev.preventDefault();
    const p = toGame(ev);
    // Snuffwick first: he is the bigger danger
    if (gob && (gob.state === 'sneak' || gob.state === 'cap')) {
      const gx = gob.x, gy = gob.y;
      if (Math.abs(p.x - gx) < 95 && Math.abs(p.y - gy) < 110) {
        const wasCap = gob.state === 'cap';
        gob.state = 'shooed'; gob.left = now; shoos++;
        sparks += 2; addText(gx, gy - 120, 'Shoo! +2', '#8dbf6a');
        if (wasCap) release('Free!');
        DG.chime(-5, 0.1); DG.chime(3, 0.08);
        return;
      }
    }
    let best = null, bd = 70 * 70;   // generous for small fingers
    for (const m of motes) {
      const d = (m.x - p.x) ** 2 + (m.y - p.y) ** 2;
      if (d < bd) { bd = d; best = m; }
    }
    if (!best) return;
    motes.splice(motes.indexOf(best), 1);
    flyers.push({ x0: best.x, y0: best.y, born: now, gold: best.gold, cx: (best.x + LAMP.x) / 2 + rnd(-80, 80), cy: Math.min(best.y, LAMP.y) - rnd(80, 200) });
    DG.chime(Math.min(12, caught % 13), 0.07);
  });

  // ---- the loop
  function tick(ms) {
    now = ms;
    const dt = Math.min(50, ms - (last || ms)); last = ms;
    if (state === 'play') {
      const el = now - t0;
      if (el >= ROUND_MS) { finish(); }
      else {
        if (now >= nextMote) { spawnMote(el); nextMote = now + rnd(420, 800) * (1 - 0.35 * el / ROUND_MS); }
        if (!gob && el >= nextGob && el < ROUND_MS - 3000) spawnGoblin(el);
      }
      $('hud-sparks').textContent = sparks;
      const left = Math.max(0, Math.ceil((ROUND_MS - (now - t0)) / 1000));
      $('hud-time').textContent = `0:${String(left).padStart(2, '0')}`;
    }
    update(dt);
    draw();
    requestAnimationFrame(tick);
  }

  function update(dt) {
    // rising sparks
    for (const m of motes) { m.y -= m.vy * dt / 1000; m.x += Math.sin(now / 500 + m.ph) * 0.4; }
    motes = motes.filter(m => m.y > 90 && now - m.born < 9000);
    // sparks flying to the beacon
    flyers = flyers.filter(f => {
      if (now - f.born < 480) return true;
      const v = f.gold ? 3 : 1;
      caught++;
      if (gob && gob.state === 'cap') { held += v; return false; }   // waits under the cap
      sparks += v; pulse = 1;
      addText(LAMP.x + 70, LAMP.y - 30, `+${v}`, f.gold ? '#ffc83d' : '#fff1d6');
      return false;
    });
    // Snuffwick
    if (gob) {
      if (gob.state === 'sneak') {
        const u = Math.min(1, (now - gob.born) / gob.dur);
        const e = u < 0.85 ? u / 0.85 * 0.85 : 0.85 + (u - 0.85);   // steady tiptoe
        gob.x = W + 120 + (LAMP.x + 140 - (W + 120)) * e;
        gob.y = 300 - 60 * e + Math.abs(Math.sin(u * 14)) * -14;
        if (u >= 1) {   // pfft: the cap goes on, and the light waits underneath
          gob.state = 'cap'; gob.capAt = now; caps++;
          DG.pfft(0.25);
          puffs.push({ x: LAMP.x, y: LAMP.y, born: now });
          addText(LAMP.x + 60, LAMP.y + 120, 'pfft! Tap him!', '#b9a8d6');
        }
      } else if (gob.state === 'cap') {
        if (now - gob.capAt > CAP_MS) { gob.state = 'yawn'; gob.left = now; release('Yawn…'); }
      } else if (now - gob.left > 900) {
        gob = null;
        nextGob = (now - t0) + rnd(6500, 10500);
      }
    }
    texts = texts.filter(t => now - t.born < 1100);
    puffs = puffs.filter(p => now - p.born < 900);
    const capped = gob && gob.state === 'cap';
    const target = Math.min(1, sparks / FULL) * (capped ? 0.35 : 1);
    glow += (target - glow) * Math.min(1, dt / 300);
    pulse = Math.max(0, pulse - dt / 400);
  }

  // the cap comes off: every spark that waited under it lands at once
  function release(word) {
    if (held) { sparks += held; pulse = 1; addText(LAMP.x + 70, LAMP.y - 30, `${word} +${held}`, '#ffc83d'); DG.chime(7, 0.08); }
    held = 0;
  }

  function radial(x, y, r, stops, op = 'lighter') {
    const gr = g.createRadialGradient(x, y, 0, x, y, r);
    for (const [o, c] of stops) gr.addColorStop(o, c);
    g.globalCompositeOperation = op; g.fillStyle = gr;
    g.fillRect(x - r, y - r, r * 2, r * 2);
    g.globalCompositeOperation = 'source-over';
  }

  function draw() {
    g.clearRect(0, 0, W, H);
    if (img.bg_cave.complete) g.drawImage(img.bg_cave, 0, 0, W, H);
    // the cave is dark while the beacon is out, and warms as it fills
    g.fillStyle = `rgba(10, 6, 24, ${0.5 - 0.42 * glow})`;
    g.fillRect(0, 0, W, H);
    const flick = reduced ? 1 : 1 + 0.05 * Math.sin(now / 180) + 0.03 * Math.sin(now / 77);
    // light spilling into the cave
    radial(LAMP.x, LAMP.y, 700 * (0.3 + glow), [[0, `rgba(255,179,71,${0.32 * glow * flick})`], [0.5, `rgba(255,140,40,${0.1 * glow})`], [1, 'rgba(255,140,40,0)']]);
    // the flame in the glass
    const fr = LAMP.r * (0.5 + 0.9 * glow + 0.25 * pulse) * flick;
    radial(LAMP.x, LAMP.y, fr * 2.2, [[0, `rgba(255,250,230,${0.15 + 0.85 * glow})`], [0.25, `rgba(255,200,90,${0.1 + 0.8 * glow})`], [0.6, `rgba(255,140,40,${0.35 * glow})`], [1, 'rgba(255,120,30,0)']]);
    if (glow > 0.98) radial(LAMP.x, LAMP.y, 260, [[0, `rgba(255,255,230,${0.35 + 0.15 * Math.sin(now / 240)})`], [1, 'rgba(255,200,61,0)']]);

    // rising sparks
    for (const m of motes) {
      const tw = 0.75 + 0.25 * Math.sin(now / 120 + m.ph * 3);
      const c = m.gold ? '255,200,61' : '255,236,190';
      radial(m.x, m.y, m.r * 3.2, [[0, `rgba(255,255,240,${0.95 * tw})`], [0.25, `rgba(${c},${0.8 * tw})`], [1, `rgba(${c},0)`]]);
      if (m.gold) { g.save(); g.translate(m.x, m.y); g.rotate(now / 600); g.fillStyle = 'rgba(255,255,230,.9)'; star(0, 0, 9, 4); g.restore(); }
    }
    // flying sparks
    for (const f of flyers) {
      const u = Math.min(1, (now - f.born) / 480), a = 1 - u;
      const x = a * a * f.x0 + 2 * a * u * f.cx + u * u * LAMP.x, y = a * a * f.y0 + 2 * a * u * f.cy + u * u * LAMP.y;
      radial(x, y, 34, [[0, 'rgba(255,255,240,1)'], [0.3, f.gold ? 'rgba(255,200,61,.8)' : 'rgba(255,220,160,.7)'], [1, 'rgba(255,200,61,0)']]);
    }
    // snuffer smoke
    for (const p of puffs) {
      const u = (now - p.born) / 900;
      for (let i = 0; i < 5; i++) {
        const a = i / 5 * TAU + u;
        g.fillStyle = `rgba(185,168,214,${0.55 * (1 - u)})`;
        g.beginPath(); g.arc(p.x + Math.cos(a) * 40 * u, p.y - 60 * u + Math.sin(a) * 25 * u, 26 + 30 * u, 0, TAU); g.fill();
      }
    }
    // Snuffwick
    if (gob) {
      const sneaking = gob.state === 'sneak' || gob.state === 'cap';
      const sprite = sneaking ? img.snuffwick_base : img.snuffwick_sneeze;
      let x = gob.x, y = gob.y, rot = 0, alpha = 1;
      if (gob.state === 'shooed') { const u = (now - gob.left) / 900; x = gob.x + 700 * u * u; y = gob.y - 260 * u + 300 * u * u; rot = u * 5; alpha = 1 - u; }
      if (gob.state === 'yawn') { const u = (now - gob.left) / 900; x = gob.x + 500 * u; alpha = 1 - u; }
      if (gob.state === 'cap') {   // the snuffer's cap over the glass, the waiting light under it
        g.save(); g.fillStyle = '#8f96a3'; g.strokeStyle = '#5b6170'; g.lineWidth = 4;
        g.beginPath(); g.moveTo(LAMP.x - 46, LAMP.y + 6); g.quadraticCurveTo(LAMP.x, LAMP.y - 92, LAMP.x + 46, LAMP.y + 6); g.closePath(); g.fill(); g.stroke();
        g.beginPath(); g.moveTo(LAMP.x + 30, LAMP.y - 30); g.lineTo(x - 30, y - 40); g.stroke();
        g.restore();
        if (held) { g.font = '700 30px Fredoka, sans-serif'; g.textAlign = 'center'; g.fillStyle = '#ffc83d'; g.fillText(`${held} waiting`, LAMP.x, LAMP.y + 80); }
      }
      if (sprite.complete) {
        const h = 220, w = h * 640 / 678;
        g.save(); g.globalAlpha = Math.max(0, alpha); g.translate(x, y); g.rotate(rot);
        g.shadowColor = 'rgba(0,0,0,.5)'; g.shadowBlur = 18; g.shadowOffsetY = 8;
        g.drawImage(sprite, -w / 2, -h / 2, w, h);
        g.restore();
      }
    }
    // floating "+1"s
    g.textAlign = 'center'; g.font = '700 40px Fredoka, sans-serif';
    for (const t of texts) {
      const u = (now - t.born) / 1100;
      g.globalAlpha = 1 - u; g.fillStyle = 'rgba(0,0,0,.6)'; g.fillText(t.s, t.x + 2, t.y - 50 * u + 3);
      g.fillStyle = t.col; g.fillText(t.s, t.x, t.y - 50 * u);
    }
    g.globalAlpha = 1;
  }

  function star(x, y, R, r) {
    g.beginPath();
    for (let i = 0; i < 10; i++) { const a = i * Math.PI / 5 - Math.PI / 2, rr = i & 1 ? r : R; g.lineTo(x + Math.cos(a) * rr, y + Math.sin(a) * rr); }
    g.closePath(); g.fill();
  }

  // ---- start / finish
  function start() {
    reset();
    $('ov-start').hidden = true; $('ov-end').hidden = true;
    DG.chime(0, 0.06);   // wakes the audio on this tap
    state = 'play'; t0 = performance.now(); nextMote = t0;
  }
  async function finish() {
    state = 'done';
    motes = [];
    if (gob && gob.state === 'cap') release('Free!');
    gob = null;
    const best = Number(DG.flag('beacon.best') || 0);
    if (sparks > best) DG.flag('beacon.best', sparks);
    $('end-title').textContent = sparks >= FULL ? 'The beacon blazes!' : sparks >= FULL / 2 ? 'The beacon is glowing!' : 'A little glow is still a glow!';
    $('end-text').textContent = `You sent ${sparks} spark${sparks === 1 ? '' : 's'} up to the beacon` +
      (shoos ? ` and shooed Snuffwick away ${shoos} time${shoos === 1 ? '' : 's'}` : '') + '.' +
      (caps > shoos ? ' Snuffwick capped the lantern, but the light only waited: none of it was lost.' : '') +
      (sparks > best && best ? ' A new best!' : '');
    $('end-beacon').textContent = 'Sending your light to the Hoard Beacon…';
    $('ov-end').hidden = false;
    const v = await DG.addSparks('beacon');
    $('end-beacon').innerHTML = DG.sparkNote(v);
  }
  $('btn-start').addEventListener('click', start);
  $('btn-again').addEventListener('click', start);

  requestAnimationFrame(tick);
})();
