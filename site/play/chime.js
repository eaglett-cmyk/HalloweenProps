// chime.js — "Welcome Chime": a pocket game for the line at the Wakestone. Pick your world
// (it only changes the sound), listen to today's three picture-notes (four for a goodbye),
// then play them back. A wrong tap never resets: the right pad glows to show you. Finishing
// sends a little light up to the Hoard Beacon (the server pays each of hello / goodbye once
// a day). Today's tune comes from the date, so a family in line all hear the same one.
(function () {
  'use strict';
  const $ = id => document.getElementById(id);
  const stage = $('stage'), pads = [...document.querySelectorAll('.ch-pad')];
  const say = $('ch-say'), dotsEl = $('ch-dots'), guide = $('ch-guide');
  const NAMES = ['Star', 'Crystal', 'Bubble'];
  const IMG = ['prop_star', 'prop_crystal', 'fx_bubble'];
  const NOTE = [0, 4, 7];            // semitones over the world's root: a happy major chord
  const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
  DG.crystalLine($('pl-crystal'));

  const today = new Intl.DateTimeFormat('en-CA', { timeZone: 'America/Chicago' }).format(new Date());
  if (today.endsWith('-10-31')) $('ch-tonight').textContent = 'Halloween night! Your light joins the beacon on the porch, between travelers\' turns.';

  // ---- today's tune: a small seeded shuffle of the three pads, never one pad three times running
  function tune(part) {
    let h = 2166136261;
    for (const ch of `${today}/${part}`) { h ^= ch.charCodeAt(0); h = Math.imul(h, 16777619) >>> 0; }
    const rnd = () => ((h = Math.imul(h ^ (h >>> 13), 0x5bd1e995) >>> 0) / 4294967296);
    const len = part === 'goodbye' ? 4 : 3;
    for (;;) {
      const t = Array.from({ length: len }, () => Math.floor(rnd() * 3));
      const kinds = new Set(t).size;
      const triple = t.some((p, i) => i >= 2 && p === t[i - 1] && p === t[i - 2]);
      if (kinds >= 2 && !triple) return t;
    }
  }

  // ---- the world's voice (Web Audio, made on the fly). The worlds follow what kids dress up
  // as (most popular costumes 2023–2026: superheroes far ahead, then princesses, witches and
  // ghosts, pop-star singers, game heroes, astronauts, animals, mermaids and pirates, toys).
  // Each voice: root note, partials [ratio, level, seconds, wave], and optional chord (semitones),
  // vol (loud waves are turned down), cut (a lowpass), attack and vib ([rate Hz, depth]).
  const VOICES = {
    castle:  { root: 523.25, parts: [[1, 1, 1.6], [2.76, 0.4, 0.9], [5.4, 0.15, 0.5]] },                     // a bell
    star:    { root: 659.25, parts: [[1, 1, 1.1, 'triangle'], [2, 0.5, 0.8], [4.01, 0.3, 0.5], [6.02, 0.12, 0.3]] }, // sparkly glass
    jungle:  { root: 392.0, parts: [[1, 1, 0.45], [3.93, 0.35, 0.12], [9.8, 0.08, 0.05]] },                  // wooden marimba
    hero:    { root: 440.0, vol: 0.5, cut: 2600, attack: 0.02,                                                // a bright brass fanfare
               parts: [[1, 1, 0.55, 'sawtooth'], [1.006, 0.7, 0.55, 'sawtooth'], [2, 0.3, 0.35, 'square']] },
    stage:   { root: 587.33, vol: 0.55, cut: 3400,                                                            // a pop synth pluck
               parts: [[1, 1, 0.5, 'square'], [2.004, 0.5, 0.4, 'triangle'], [3, 0.2, 0.25]] },
    haunted: { root: 440.0, chord: [0, 3, 7], attack: 0.14, vib: [6, 0.012], parts: [[1, 1, 1.3], [2, 0.12, 0.9]] }, // a spooky theremin
    pixel:   { root: 523.25, vol: 0.4, parts: [[1, 1, 0.24, 'square'], [2, 0.25, 0.12, 'square']] },        // a chiptune beep
    ocean:   { root: 466.16, parts: [[1, 1, 0.8], [2, 0.45, 0.5], [3, 0.25, 0.35], [4.2, 0.1, 0.2]] },          // a steel drum
    toy:     { root: 783.99, parts: [[1, 1, 1.2], [3, 0.3, 0.35], [5.05, 0.08, 0.15]] },                       // a music box
  };
  let ac = null, world = 'castle';
  function note(i, vol = 0.16) {
    try {
      ac = ac || new (window.AudioContext || window.webkitAudioContext)();
      if (ac.state === 'suspended') ac.resume();
      const v = VOICES[world], t = ac.currentTime, att = v.attack || 0.008;
      const f = v.root * Math.pow(2, (v.chord || NOTE)[i] / 12), level = vol * (v.vol || 1);
      let out = ac.destination;
      if (v.cut) { const lp = ac.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = v.cut; lp.connect(out); out = lp; }
      for (const [mul, lv, dur, type = 'sine'] of v.parts) {
        const o = ac.createOscillator(), gn = ac.createGain();
        o.type = type;
        if (v.vib) {   // the theremin swoops up into its note, then wobbles
          o.frequency.setValueAtTime(f * mul * 0.94, t); o.frequency.exponentialRampToValueAtTime(f * mul, t + att);
          const lfo = ac.createOscillator(), depth = ac.createGain();
          lfo.frequency.value = v.vib[0]; depth.gain.value = f * mul * v.vib[1];
          lfo.connect(depth).connect(o.frequency); lfo.start(t); lfo.stop(t + dur + 0.05);
        } else o.frequency.value = f * mul;
        gn.gain.setValueAtTime(0, t); gn.gain.linearRampToValueAtTime(level * lv, t + att);
        gn.gain.exponentialRampToValueAtTime(0.0001, t + dur);
        o.connect(gn).connect(out); o.start(t); o.stop(t + dur + 0.05);
      }
    } catch { /* no sound: the pads still light */ }
  }
  function thud() {
    try {
      const t = ac.currentTime, o = ac.createOscillator(), gn = ac.createGain();
      o.type = 'sine'; o.frequency.setValueAtTime(180, t); o.frequency.exponentialRampToValueAtTime(90, t + 0.15);
      gn.gain.setValueAtTime(0.12, t); gn.gain.exponentialRampToValueAtTime(0.0001, t + 0.2);
      o.connect(gn).connect(ac.destination); o.start(t); o.stop(t + 0.25);
    } catch { /* fine */ }
  }

  // ---- state
  let part = 'hello', seq = [], pos = 0, state = 'idle', misses = 0, runId = 0;
  const wait = ms => new Promise(r => setTimeout(r, ms));

  function light(i, cls = 'is-on', ms = 420) {
    const p = pads[i];
    p.classList.remove(cls); void p.offsetWidth; p.classList.add(cls);
    setTimeout(() => p.classList.remove(cls), ms);
  }
  function renderDots() {
    dotsEl.replaceChildren(...seq.map((p, i) => {
      const li = document.createElement('li');
      li.className = i < pos ? 'is-done' : i === pos && state === 'turn' ? 'is-next' : '';
      li.textContent = i < pos ? '✓' : '';
      return li;
    }));
  }
  function renderGuide() {
    guide.replaceChildren(...seq.map((p, i) => {
      const li = document.createElement('li');
      li.className = i < pos ? 'is-done' : '';
      li.innerHTML = `<img src="/img/${IMG[p]}.webp" alt="${NAMES[p]}" width="48" height="48">`;
      return li;
    }));
  }

  async function demo() {
    const id = ++runId;
    state = 'listen'; stage.classList.add('is-listening');
    say.textContent = 'Listen…'; renderDots();
    await wait(500);
    for (const p of seq) {
      if (id !== runId) return;
      light(p, 'is-on', 520); note(p);
      await wait(reduced ? 900 : 700);
    }
    if (id !== runId) return;
    stage.classList.remove('is-listening');
    state = 'turn'; say.textContent = pos ? 'Keep going!' : 'Your turn! Tap the same pictures.';
    renderDots();
  }

  function begin(which) {
    part = which; seq = tune(part); pos = 0; misses = 0;
    $('ov-start').hidden = true; $('ov-end').hidden = true;
    renderGuide();
    note(0, 0.001);   // wakes the audio on this tap
    demo();
  }

  pads.forEach((b, i) => b.addEventListener('click', () => {
    if (state !== 'turn') return;
    if (i !== seq[pos]) {   // never a reset: the right pad glows
      misses++; thud();
      b.classList.add('is-wrong'); setTimeout(() => b.classList.remove('is-wrong'), 350);
      light(seq[pos], 'is-hint', 1400);
      say.textContent = `Try the glowing one: ${NAMES[seq[pos]]}!`;
      return;
    }
    note(i); light(i);
    pos++; renderDots(); renderGuide();
    if (pos < seq.length) { say.textContent = ['Yes!', 'Lovely!', 'Just right!'][pos % 3]; return; }
    finish();
  }));

  async function finish() {
    state = 'done'; say.textContent = 'Listen to your welcome!';
    await wait(350);
    for (let k = 0; k < 2; k++) for (const p of seq) { light(p, 'is-on', 300); note(p, 0.12); await wait(170); }
    seq.forEach((p, i) => setTimeout(() => note(p, 0.09), i * 30));   // and all together
    // a glow flies up to the beacon
    const orb = $('ch-orb');
    orb.classList.remove('is-fly'); void orb.offsetWidth; orb.classList.add('is-fly');
    await wait(reduced ? 200 : 1100);
    $('ch-beacon').classList.add('is-bright');
    DG.flag(`chime.${today}.${part}`, 1);
    $('end-title').textContent = part === 'hello' ? 'What a welcome!' : 'What a lovely goodbye!';
    $('end-text').textContent = (part === 'hello' ? 'Butterscotch heard your hello all the way from the cave.' : 'Butterscotch hums it back: “Safe travels, traveler!”') +
      (misses ? '' : ' Every note right the first time!');
    $('btn-next').hidden = part !== 'hello';
    $('end-beacon').textContent = 'Sending your light to the Hoard Beacon…';
    $('ov-end').hidden = false;
    const v = await DG.addSparks('chime', part);
    $('end-beacon').innerHTML = DG.sparkNote(v);
  }

  // ---- buttons
  const worldBtns = [...document.querySelectorAll('.ch-world')];
  function pickWorld(b) {
    world = b.dataset.world; stage.dataset.world = world;
    worldBtns.forEach(o => o.setAttribute('aria-checked', String(o === b)));
  }
  worldBtns.forEach(b => b.addEventListener('click', () => { pickWorld(b); DG.store('dg.chimeWorld', world); note(1, 0.1); }));
  const saved = worldBtns.find(b => b.dataset.world === DG.store('dg.chimeWorld'));   // the world you picked last time
  if (saved) pickWorld(saved);
  $('btn-hello').addEventListener('click', () => begin('hello'));
  $('btn-goodbye').addEventListener('click', () => begin('goodbye'));
  $('btn-next').addEventListener('click', () => begin('goodbye'));
  $('btn-again').addEventListener('click', () => { $('ch-beacon').classList.remove('is-bright'); $('ov-end').hidden = true; $('ov-start').hidden = false; state = 'idle'; });
  $('btn-listen').addEventListener('click', () => { if (state === 'turn' || state === 'listen') demo(); });
  $('btn-guide').addEventListener('click', e => {
    guide.hidden = !guide.hidden;
    e.currentTarget.setAttribute('aria-pressed', String(!guide.hidden));
    e.currentTarget.textContent = guide.hidden ? 'Show me the notes' : 'Hide the notes';
  });
  if (DG.flag(`chime.${today}.hello`)) $('btn-goodbye').classList.replace('btn-ghost', 'btn-gold');
})();
