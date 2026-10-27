// sky.js — the crystal sky: one star per crystal the site knows (GET /api/sky), placed by a
// scrambled seed, sized by rank and coloured by the prize it found last. Your own crystal's
// star (?c=<id>, or the one this browser remembers) pulses and carries its nickname. The
// cave on the hill glows as much as the Hoard Beacon is lit. ?demo=1 fills the sky with
// made-up stars to preview it (test build).
(function () {
  'use strict';
  const W = 1280, H = 768, TAU = Math.PI * 2;
  const RANKS = ['', 'Sparkfinder', 'Apprentice', 'Glowcaster', 'Hoardguard', 'Worldwalker', 'Dragon Rider', 'Keeper of the Hoard'];
  const PRIZE = [
    { name: 'Goblin Trick', rgb: '141,191,106' },
    { name: 'Hoard Coin', rgb: '217,222,232' },
    { name: 'Golden Star', rgb: '255,200,61' },
    { name: 'Dragon Gem', rgb: '255,59,71' },
  ];
  const WAITING = '185,168,214';
  const $ = id => document.getElementById(id);
  const cv = $('cv'), g = cv.getContext('2d');
  const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
  DG.crystalLine($('pl-crystal'));

  let scale = 1;
  function fit() {
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    cv.width = Math.round(cv.clientWidth * dpr); cv.height = Math.round(cv.clientHeight * dpr);
    scale = cv.width / W;
    g.setTransform(scale, 0, 0, cv.height / H, 0, 0);
  }
  addEventListener('resize', fit);
  fit();

  // a fixed field of faint background stars
  let s = 12345;
  const rng = () => ((s = Math.imul(s ^ (s >>> 15), 0x2c1b3c6d) + 0x9e3779b9 >>> 0) / 4294967296);
  const dust = Array.from({ length: 260 }, () => ({ x: rng() * W, y: rng() * H * 0.8, r: 0.5 + rng() * 1.3, p: rng() * TAU }));

  let stars = [], you = null, beacon = 0, picked = null;
  const HORIZON = 640;

  function place(st) {
    const a = st.seed >>> 0;
    st.x = 50 + (a & 0xffff) / 0xffff * (W - 100);
    st.y = 40 + (a >>> 16) / 0xffff * (HORIZON - 150);
    st.size = st.visits ? 3.5 + Math.min(7, st.rank) * 1.25 : 2.6;
    st.rgb = st.visits ? (PRIZE[st.tier] || PRIZE[1]).rgb : WAITING;
    st.ph = (a % 1000) / 1000 * TAU;
  }

  async function load() {
    const q = new URLSearchParams(location.search);
    const v = await DG.getBeacon();
    beacon = Math.min(1, v.sparks / (v.goal || 1));
    $('sky-beacon').innerHTML = v.lit ? 'Beacon <b>LIT!</b>' : `Beacon <b>${Math.floor(beacon * 100)}%</b>`;
    let data = { stars: [], you: null };
    if (q.get('demo') === '1') {
      data.stars = Array.from({ length: 140 }, (_, i) => {
        const r = rng();
        return { seed: (rng() * 4294967296) >>> 0, visits: r < 0.1 ? 0 : 1 + Math.floor(rng() * rng() * 7), rank: 0,
                 tier: rng() < 0.12 ? 0 : rng() < 0.85 ? 1 : rng() < 0.85 ? 2 : 3 };
      }).map(st => ({ ...st, rank: st.visits }));
      data.you = 7; data.stars[7] = { ...data.stars[7], visits: 3, rank: 3, tier: 2, nickname: 'Sparkle' };
    } else {
      try {
        const r = await fetch(`/api/sky${DG.crystal ? `?c=${DG.crystal}` : ''}`, { cache: 'no-store' });
        if (r.ok) data = await r.json();
      } catch { /* empty sky */ }
    }
    stars = data.stars;
    stars.forEach(place);
    you = data.you != null ? stars[data.you] : null;
    const n = stars.filter(st => st.visits).length;
    $('sky-count').innerHTML = n ? `<b>${n}</b> crystal star${n === 1 ? '' : 's'}` : '';
    if (!stars.length) $('sky-lede').textContent = 'The sky is still dark. After Halloween night, every crystal that woke Butterscotch lights up here as a star.';
    else if (DG.crystal && !you) $('sky-lede').textContent = 'Your crystal\'s star appears here a day or two after it wakes the dragon.';
    if (you) picked = you;
  }

  // ---- input: tap a star
  cv.addEventListener('pointerdown', ev => {
    const r = cv.getBoundingClientRect();
    const x = (ev.clientX - r.left) / r.width * W, y = (ev.clientY - r.top) / r.height * H;
    let best = null, bd = 40 * 40;
    for (const st of stars) { const d = (st.x - x) ** 2 + (st.y - y) ** 2; if (d < bd) { bd = d; best = st; } }
    picked = best;
    if (best) DG.chime(best.visits ? Math.min(12, best.rank * 2) : -3, 0.06);
  });

  function glowDot(x, y, r, rgb, a) {
    const gr = g.createRadialGradient(x, y, 0, x, y, r);
    gr.addColorStop(0, `rgba(255,255,255,${a})`); gr.addColorStop(0.2, `rgba(${rgb},${a * 0.9})`); gr.addColorStop(1, `rgba(${rgb},0)`);
    g.fillStyle = gr; g.fillRect(x - r, y - r, r * 2, r * 2);
  }

  function draw(now) {
    const sky = g.createLinearGradient(0, 0, 0, H);
    sky.addColorStop(0, '#0b0820'); sky.addColorStop(0.6, '#1b1240'); sky.addColorStop(1, '#2b1a50');
    g.fillStyle = sky; g.fillRect(0, 0, W, H);
    // the moon
    glowDot(1120, 110, 120, '233,238,255', 0.25);
    g.fillStyle = '#eef0ff'; g.beginPath(); g.arc(1120, 110, 38, 0, TAU); g.fill();
    g.fillStyle = '#d6d9ee'; for (const [u, v, rr] of [[-12, -8, 8], [10, 12, 6], [14, -14, 4]]) { g.beginPath(); g.arc(1120 + u, 110 + v, rr, 0, TAU); g.fill(); }
    g.globalCompositeOperation = 'lighter';
    for (const d of dust) { const tw = reduced ? 0.6 : 0.45 + 0.35 * Math.sin(now / 900 + d.p); g.fillStyle = `rgba(220,225,255,${tw * 0.5})`; g.fillRect(d.x, d.y, d.r, d.r); }
    // crystal stars
    for (const st of stars) {
      const tw = reduced ? 1 : 0.8 + 0.2 * Math.sin(now / 700 + st.ph);
      glowDot(st.x, st.y, st.size * 2.6, st.rgb, 0.7 * tw);
      g.fillStyle = `rgba(255,255,255,${0.9 * tw})`;
      g.beginPath(); g.arc(st.x, st.y, Math.max(1.2, st.size * 0.32), 0, TAU); g.fill();
      if (st.visits >= 5) {   // high ranks get a cross of light
        g.strokeStyle = `rgba(${st.rgb},${0.45 * tw})`; g.lineWidth = 1.2;
        g.beginPath(); g.moveTo(st.x - st.size * 5, st.y); g.lineTo(st.x + st.size * 5, st.y); g.moveTo(st.x, st.y - st.size * 5); g.lineTo(st.x, st.y + st.size * 5); g.stroke();
      }
    }
    g.globalCompositeOperation = 'source-over';
    // the hill and the cave mouth, lit by the beacon
    g.fillStyle = '#120b26';
    g.beginPath(); g.moveTo(0, H); g.lineTo(0, 690);
    g.bezierCurveTo(200, 610, 380, 600, 560, 640); g.bezierCurveTo(760, 690, 980, 600, 1280, 660); g.lineTo(W, H); g.closePath(); g.fill();
    const cx = 420, cy = 668;
    const fl = reduced ? 1 : 1 + 0.06 * Math.sin(now / 170);
    g.globalCompositeOperation = 'lighter';
    glowDot(cx, cy, (90 + 260 * beacon) * fl, '255,179,71', 0.15 + 0.6 * beacon);
    g.globalCompositeOperation = 'source-over';
    g.fillStyle = beacon > 0.02 ? `rgba(255,${170 + 60 * beacon | 0},${80 + 80 * beacon | 0},${0.35 + 0.65 * beacon})` : '#05030c';
    g.beginPath(); g.ellipse(cx, cy + 8, 34, 26, 0, Math.PI, 0); g.lineTo(cx + 34, cy + 20); g.lineTo(cx - 34, cy + 20); g.closePath(); g.fill();
    g.font = '600 20px Fredoka, sans-serif'; g.textAlign = 'center'; g.fillStyle = 'rgba(246,234,210,.7)';
    g.fillText('Butterscotch\'s cave', cx, cy + 56);

    // your star
    if (you) {
      const p = reduced ? 0.5 : (now % 1600) / 1600;
      g.strokeStyle = `rgba(255,200,61,${1 - p})`; g.lineWidth = 2.5;
      g.beginPath(); g.arc(you.x, you.y, you.size * 2 + 30 * p, 0, TAU); g.stroke();
    }
    // the picked star's card
    if (picked) label(picked);
  }

  function label(st) {
    const mine = st === you;
    const lines = [];
    if (mine) lines.push(st.nickname ? `“${st.nickname}”` : 'Your crystal');
    if (st.visits) {
      lines.push(`${RANKS[Math.min(7, Math.max(1, st.rank))]} · ${st.visits} Halloween${st.visits === 1 ? '' : 's'}`);
      lines.push(`Last found: ${(PRIZE[st.tier] || PRIZE[1]).name}`);
    } else lines.push('Waiting to wake the dragon');
    if (st.sparks) lines.push(`${st.sparks} spark${st.sparks === 1 ? '' : 's'} for the beacon`);
    g.font = '600 22px Fredoka, sans-serif';
    const w = Math.max(...lines.map(l => g.measureText(l).width)) + 32, h = lines.length * 28 + 20;
    let x = st.x + 24, y = st.y - h / 2;
    if (x + w > W - 10) x = st.x - 24 - w;
    y = Math.max(10, Math.min(HORIZON - h, y));
    g.fillStyle = 'rgba(246,234,210,.95)'; roundRect(x, y, w, h, 14); g.fill();
    g.textAlign = 'left';
    lines.forEach((l, i) => { g.fillStyle = i === 0 && mine ? '#9a5424' : '#3a2a1a'; g.fillText(l, x + 16, y + 34 + i * 28); });
  }
  function roundRect(x, y, w, h, r) {
    g.beginPath(); g.moveTo(x + r, y); g.arcTo(x + w, y, x + w, y + h, r); g.arcTo(x + w, y + h, x, y + h, r); g.arcTo(x, y + h, x, y, r); g.arcTo(x, y, x + w, y, r); g.closePath();
  }

  function loop(t) { draw(t); requestAnimationFrame(loop); }
  load().then(() => requestAnimationFrame(loop));
})();
