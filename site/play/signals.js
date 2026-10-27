// signals.js — "Sock-Side Signals": the lumpy sock Snuffwick hugs (the 2027 "Egg in the
// Sock" hook) taps a little pattern from inside. Watch the rings / hear the taps, pick the
// matching dot strip from three, then settle the sock (blanket or cushion: both are right)
// and it taps back. One signal a day from the date; each solved day is a stamp. The sock is
// never opened or shaken, and nothing says what's inside. Opens after Halloween (the beacon's
// finale shows the sock first); ?preview=1 opens it early for the test build.
(function () {
  'use strict';
  const $ = id => document.getElementById(id);
  const sock = $('sg-sock'), say = $('sg-say'), choicesEl = $('sg-choices');
  const SHORT = 320, LONG = 760;   // the two gaps between taps
  DG.crystalLine($('pl-crystal'));

  const q = new URLSearchParams(location.search);
  const now = new Date();
  const today = new Intl.DateTimeFormat('en-CA', { timeZone: 'America/Chicago' }).format(now);
  const afterHalloween = today.slice(5) >= '11-01' || today.slice(5) < '10-01';   // November through September

  // ---- today's signal: 2-4 taps with short / long gaps, seeded by the date
  let h = 2166136261;
  for (const ch of `${today}/sock`) { h ^= ch.charCodeAt(0); h = Math.imul(h, 16777619) >>> 0; }
  const rnd = () => ((h = Math.imul(h ^ (h >>> 13), 0x5bd1e995) >>> 0) / 4294967296);
  const taps = 2 + Math.floor(rnd() * 3);
  let gaps = Array.from({ length: taps - 1 }, () => rnd() < 0.5 ? 'S' : 'L');
  if (gaps.length > 1 && gaps.every(x => x === gaps[0])) gaps[Math.floor(rnd() * gaps.length)] = gaps[0] === 'S' ? 'L' : 'S';
  const answer = gaps.join('');
  // two look-alikes: one tap more or less, and the gaps changed
  function decoys() {
    const set = new Set([answer]), out = [];
    const tries = [
      answer.length > 1 ? answer.slice(0, -1) : answer + (answer === 'S' ? 'L' : 'S'),
      answer + (rnd() < 0.5 ? 'S' : 'L'),
      [...answer].reverse().join(''),
      answer.replace(/./, c => c === 'S' ? 'L' : 'S'),
      [...answer].map(c => c === 'S' ? 'L' : 'S').join(''),
    ];
    for (const t of tries) if (t.length >= 1 && t.length <= 3 && !set.has(t)) { set.add(t); out.push(t); if (out.length === 2) break; }
    return out;
  }

  // ---- sound: a soft knock through knitted wool
  let ac = null;
  function knock(vol = 0.22) {
    try {
      ac = ac || new (window.AudioContext || window.webkitAudioContext)();
      if (ac.state === 'suspended') ac.resume();
      const t = ac.currentTime, o = ac.createOscillator(), gn = ac.createGain(), lp = ac.createBiquadFilter();
      o.type = 'sine'; o.frequency.setValueAtTime(330, t); o.frequency.exponentialRampToValueAtTime(140, t + 0.09);
      lp.type = 'lowpass'; lp.frequency.value = 700;
      gn.gain.setValueAtTime(0, t); gn.gain.linearRampToValueAtTime(vol, t + 0.004); gn.gain.exponentialRampToValueAtTime(0.0001, t + 0.18);
      o.connect(lp).connect(gn).connect(ac.destination); o.start(t); o.stop(t + 0.2);
    } catch { /* the rings still show it */ }
  }
  function ring() {
    const r = document.createElement('span');
    r.className = 'sg-ring';
    sock.append(r);
    setTimeout(() => r.remove(), 1200);
    sock.classList.remove('is-tap'); void sock.offsetWidth; sock.classList.add('is-tap');
  }
  const wait = ms => new Promise(r => setTimeout(r, ms));
  let playing = 0;
  async function play(pattern, vol) {
    const id = ++playing;
    knock(vol); ring();
    for (const g of pattern) {
      await wait(g === 'S' ? SHORT : LONG);
      if (id !== playing) return;
      knock(vol); ring();
    }
  }

  function strip(p) {   // the pattern drawn as dots with short / long spaces
    const b = document.createElement('button');
    b.type = 'button'; b.className = 'sg-strip'; b.dataset.p = p;
    b.setAttribute('aria-label', `${p.length + 1} taps: ${[...p].map(c => c === 'S' ? 'quick' : 'slow').join(', then ')}`);
    b.append(dot());
    for (const c of p) { const s = document.createElement('i'); s.className = c === 'S' ? 'sg-gap-s' : 'sg-gap-l'; b.append(s, dot()); }
    return b;
  }
  function dot() { const d = document.createElement('span'); d.className = 'sg-dot'; return d; }

  let stage = 'idle';
  async function listen() {
    stage = 'listen';
    $('ov-start').hidden = true; $('ov-end').hidden = true; $('sg-comfort').hidden = true;
    choicesEl.hidden = true;
    say.textContent = 'Shh… listen to the sock.';
    await wait(900);
    await play(answer);
    await wait(700);
    say.textContent = 'Which pattern was it?';
    const opts = [answer, ...decoys()].sort(() => rnd() - 0.5);
    choicesEl.replaceChildren(...opts.map(strip));
    const again = document.createElement('button');
    again.type = 'button'; again.className = 'sg-again'; again.textContent = '↻ Hear it again';
    again.addEventListener('click', () => play(answer));
    choicesEl.append(again);
    choicesEl.hidden = false;
    stage = 'choose';
  }

  choicesEl.addEventListener('click', async ev => {
    const b = ev.target.closest('.sg-strip');
    if (!b || stage !== 'choose') return;
    if (b.dataset.p !== answer) {
      b.classList.add('is-no'); b.disabled = true;
      say.textContent = 'Not quite. Listen again: count the rings!';
      await wait(500); play(answer);
      return;
    }
    stage = 'comfort';
    b.classList.add('is-yes'); DG.chime(4, 0.07);
    say.textContent = 'That\'s it! Now help Snuffwick make the sock comfy.';
    await wait(600);
    choicesEl.hidden = true;
    $('sg-comfort').hidden = false;
  });

  $('sg-comfort').addEventListener('click', async ev => {
    const b = ev.target.closest('[data-comfort]');
    if (!b || stage !== 'comfort') return;
    stage = 'done';
    $('sg-comfort').hidden = true;
    sock.classList.add('is-settled', b.dataset.comfort === 'blanket' ? 'has-blanket' : 'has-cushion');
    say.textContent = 'Snug as a sock can be…';
    await wait(1100);
    await play('S', 0.12);   // ...and it answers: tap-tap
    sock.classList.add('is-glow');
    [0, 7, 12].forEach((p, i) => setTimeout(() => DG.chime(p, 0.06), 500 + i * 140));
    DG.flag(`signals.${today}`, 1);
    const stamps = countStamps();
    $('end-text').textContent = `“Soft taps! That's a very lumpy lullaby,” Butterscotch yawns. You've collected ${stamps} signal stamp${stamps === 1 ? '' : 's'}.`;
    $('end-beacon').textContent = 'Sending a little light to the Hoard Beacon…';
    await wait(900);
    $('ov-end').hidden = false;
    const v = await DG.addSparks('signals');
    $('end-beacon').innerHTML = DG.sparkNote(v);
  });

  function countStamps() {
    let n = 0;
    try { for (let i = 0; i < localStorage.length; i++) if (/^dg\.\d{4}\.signals\.\d{4}-\d\d-\d\d$/.test(localStorage.key(i))) n++; } catch { /* private mode */ }
    return n;
  }

  $('btn-start').addEventListener('click', () => { knock(0.001); listen(); });
  $('btn-again').addEventListener('click', () => { sock.className = 'sg-sock'; listen(); });
  const stamps = countStamps();
  if (stamps) $('sg-stamps').textContent = `Signal stamps so far: ${stamps}${DG.flag(`signals.${today}`) ? ' (today\'s is done: play again just for fun)' : ''}`;

  (async () => {
    const v = await DG.getBeacon();
    if (!afterHalloween && !v.lit && q.get('preview') !== '1') { $('ov-start').hidden = true; $('ov-locked').hidden = false; }
  })();
})();
