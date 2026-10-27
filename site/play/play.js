// play.js — what the /play/ games share: which crystal is playing (if any), the Hoard Beacon
// API, the beacon meter, a little chime, and per-year "done" flags kept in the browser.
// A crystal is optional: every traveler's sparks count. When one is known (its page links
// here with ?c=<id>, or crystal.js remembered it), its sparks are credited to it too. The
// server decides every award (a finished game = 5 sparks, each game once a day); a random
// browser token (dg.browser, nothing personal) is how it knows a game already paid today.
(function () {
  'use strict';
  const ID_RE = /^[0-9a-f]{8}$/;
  const YEAR = Number(new Intl.DateTimeFormat('en-US', { timeZone: 'America/Chicago', year: 'numeric' }).format(new Date()));

  function store(k, v) {
    try { if (v === undefined) return localStorage.getItem(k); if (v === null) localStorage.removeItem(k); else localStorage.setItem(k, v); } catch { /* private mode */ }
    return null;
  }

  // ---- the crystal (optional)
  const q = new URLSearchParams(location.search);
  let crystal = (q.get('c') || '').toLowerCase();
  if (ID_RE.test(crystal)) store('dg.crystal', crystal);
  else crystal = ID_RE.test(store('dg.crystal') || '') ? store('dg.crystal') : null;

  // ---- this browser's token (random; only so a game pays once a day)
  let browser = store('dg.browser');
  if (!/^[0-9a-f]{16}$/.test(browser || '')) {
    const a = new Uint8Array(8);
    (window.crypto || {}).getRandomValues ? crypto.getRandomValues(a) : a.forEach((_, i) => { a[i] = Math.random() * 256; });
    browser = [...a].map(x => x.toString(16).padStart(2, '0')).join('');
    store('dg.browser', browser);
  }

  // ---- the beacon
  const OFFLINE = { year: YEAR, sparks: 0, goal: 500, lit: false, web: 0, webMax: 100, travelers: 0, plays: 0, milestones: [125, 250, 375], offline: true };
  function preview(v) {   // ?lit=1 previews the lit beacon (test build)
    return q.get('lit') === '1' ? { ...v, sparks: Math.max(v.sparks, v.goal), lit: true } : v;
  }
  async function getBeacon() {
    try {
      const r = await fetch('/api/beacon', { cache: 'no-store' });
      if (!r.ok) throw new Error(r.status);
      return preview(await r.json());
    } catch { return preview(OFFLINE); }
  }
  // A finished game (part: the hunt's find, or the chime's hello / goodbye; extra: e.g. a
  // Lightpaths route) -> the view + {added, why}; see sparkNote for what to tell the player.
  async function addSparks(game, part = '', extra = {}) {
    try {
      const r = await fetch('/api/beacon/sparks', { method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ...extra, game, part, browser, ...(crystal ? { crystal } : {}) }) });
      if (!r.ok) throw new Error(r.status);
      return preview(await r.json());
    } catch { return { ...preview(OFFLINE), added: 0, why: 'failed', failed: true }; }
  }
  // One kind sentence for a game's end screen.
  function sparkNote(v) {
    const pct = v.lit ? 'The beacon is <b>LIT!</b>' : `The beacon is <b>${Math.floor(Math.min(1, v.sparks / (v.goal || 1)) * 100)}%</b> lit.`;
    switch (v.why) {
      case 'ok': return `<b>+${v.added} sparks</b> flew to the Hoard Beacon! ${pct}`;
      case 'again': return `You already sent today's sparks from this game. Play again just for fun! ${pct}`;
      case 'browser': case 'house': return `Your sparks for today are all sent. Come back tomorrow! ${pct}`;
      case 'full': return `The pocket welcome is full: the rest of the light comes from crystals at the Wakestone on Halloween night! ${pct}`;
      default: return 'The beacon could not be reached just now, but you played beautifully.';
    }
  }

  // The meter: <div class="meter" data-meter></div> filled from a beacon view.
  function renderMeter(el, v) {
    const f = Math.max(0, Math.min(1, v.sparks / (v.goal || 1)));
    el.classList.toggle('is-lit', !!v.lit);
    el.style.setProperty('--fill', f.toFixed(3));
    el.innerHTML = '';
    const lamp = document.createElement('div');
    lamp.className = 'meter-lamp';
    lamp.setAttribute('aria-hidden', 'true');
    lamp.innerHTML = '<span class="meter-glow"></span>';
    const body = document.createElement('div');
    body.className = 'meter-body';
    const pct = Math.floor(f * 100);
    body.innerHTML =
      `<p class="meter-kicker">The Hoard Beacon</p>` +
      `<p class="meter-big">${v.lit ? 'BEACON LIT!' : `${pct}% lit`}</p>` +
      `<div class="meter-bar" role="progressbar" aria-valuemin="0" aria-valuemax="${v.goal}" aria-valuenow="${Math.min(v.sparks, v.goal)}"><span></span></div>` +
      `<p class="meter-num">${v.sparks.toLocaleString()} of ${v.goal.toLocaleString()} sparks</p>` +
      (v.offline ? `<p class="meter-note">The beacon can't be reached right now. You can still play.</p>` :
       `<p class="meter-note">Games add up to ${v.webMax} sparks (${v.web} so far). Every traveler at the Wakestone on Halloween night adds ten` +
       (v.travelers ? `: ${v.travelers} so far!` : '.') + (v.porchLit ? ' The porch beacon blazed on Halloween night!' : '') + '</p>');
    el.append(lamp, body);
  }

  // ---- once-a-year flags and small saved numbers (this browser only)
  const key = k => `dg.${YEAR}.${k}`;
  const flag = (k, v) => v === undefined ? store(key(k)) : store(key(k), v === null ? null : String(v));

  // ---- a soft chime (Web Audio, made on the fly; needs a tap first)
  let ac = null;
  function chime(pitch = 0, vol = 0.12) {
    try {
      ac = ac || new (window.AudioContext || window.webkitAudioContext)();
      if (ac.state === 'suspended') ac.resume();
      const t = ac.currentTime, f = 880 * Math.pow(2, pitch / 12);
      for (const [mul, v] of [[1, 1], [2.01, 0.35], [3.02, 0.12]]) {
        const o = ac.createOscillator(), gn = ac.createGain();
        o.type = 'sine'; o.frequency.value = f * mul;
        gn.gain.setValueAtTime(0, t); gn.gain.linearRampToValueAtTime(vol * v, t + 0.01); gn.gain.exponentialRampToValueAtTime(0.0001, t + 0.7);
        o.connect(gn).connect(ac.destination); o.start(t); o.stop(t + 0.75);
      }
    } catch { /* no sound */ }
  }
  function pfft(vol = 0.15) {   // Snuffwick's snuffer: a short breath of noise
    try {
      ac = ac || new (window.AudioContext || window.webkitAudioContext)();
      const t = ac.currentTime, len = Math.floor(ac.sampleRate * 0.35);
      const buf = ac.createBuffer(1, len, ac.sampleRate), d = buf.getChannelData(0);
      for (let i = 0; i < len; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / len, 2);
      const src = ac.createBufferSource(), lp = ac.createBiquadFilter(), gn = ac.createGain();
      src.buffer = buf; lp.type = 'lowpass'; lp.frequency.value = 900; gn.gain.value = vol;
      src.connect(lp).connect(gn).connect(ac.destination); src.start(t);
    } catch { /* no sound */ }
  }

  // The crystal line on each page: who is playing.
  async function crystalLine(el) {
    if (!el) return;
    if (!crystal) { el.textContent = 'No crystal? You can still play. Every traveler\'s sparks count.'; return; }
    el.textContent = 'Playing with your crystal.';   // never its id: the id opens its page
    try {
      const d = await (await fetch(`/api/crystal/${crystal}`, { cache: 'no-cache' })).json();
      const name = d.nickname ? `“${d.nickname}”` : '';
      const given = d.extra && d.extra.sparksYear === YEAR ? d.extra.sparks | 0 : 0;
      el.textContent = `Playing with your crystal${name ? ` ${name}` : ''}.` + (given ? ` It has given the beacon ${given.toLocaleString()} spark${given === 1 ? '' : 's'} this year.` : '');
    } catch { /* keep the short line */ }
  }

  window.DG = { YEAR, crystal, getBeacon, addSparks, sparkNote, renderMeter, flag, chime, pfft, crystalLine, store };
})();
