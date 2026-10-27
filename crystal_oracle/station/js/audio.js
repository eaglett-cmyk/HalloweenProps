// audio.js - the show's sound on Web Audio (a port of audio.h): one looped MUSIC bed ducked
// -10 dB under the VOICE line, preloaded SFX, generated chimes for anything missing. Files are
// the card's WAVs (48 kHz mono s16), decoded once and kept. init() must run from a user gesture
// (Safari): the start overlay does that.
import { media } from './media.js';
import { vlog } from './vlog.js';

export const CHIME = { ABSORB: 0, REVEAL: 1, RARE: 2, LEGEND: 3, TRICK: 4, TAP: 5, ERROR: 6, TEST: 7 };

let ctx = null, master = null, musicGain = null, voiceGain = null, sfxGain = null, toneGain = null, analyser = null;
let s_volume = 75;
const s_decoded = new Map();     // path -> AudioBuffer | Promise | null (bad)
let s_music = null;              // { path, src, gainPct }
let s_voice = null;              // { src, endAt, path }
const s_sfx = [];                // handle -> path
let s_levelBuf = null, s_level = 0;
const DUCK_DB = -10;
let s_unmute = null;             // iOS: a silent <audio> loop keeps the audio session in "playback" mode
let s_stuck = false, s_builds = 0, s_tapHook = false, s_resuming = false;

// ---- the media-element engine: plain <audio> playback (iPadOS 26 Home Screen apps keep the Web
// Audio context "suspended" for good; HTML media still plays). No mixer: music = a looping
// element ducked by volume, voice = one element, sfx = a pool of 4, chimes = tiny WAVs synthesized
// here. Every element is unlocked (play + pause) inside the first tap.
let s_mediaMode = false, s_mediaForced = null;   // forced: null = auto, true/false
const s_el = { music: null, voice: null, sfx: [], chime: [] };
let s_elMusic = null;            // { path, gainPct }
let s_elVoice = null;            // { el, startMs, durMs }
let s_elDuck = 1;
const s_chimeUrl = new Map();
function makeEl() { const a = document.createElement('audio'); a.setAttribute('playsinline', ''); a.preload = 'auto'; document.body.append(a); return a; }
function unlockEl(a) { if (!a.src) a.src = s_unmute ? s_unmute.src : ''; const p = a.play(); if (p && p.then) p.then(() => a.pause()).catch(() => {}); }
function mediaInit() {
  if (s_el.music) return;
  s_el.music = makeEl(); s_el.music.loop = true;
  s_el.voice = makeEl();
  for (let i = 0; i < 4; i++) s_el.sfx.push(makeEl());
  for (let i = 0; i < 2; i++) s_el.chime.push(makeEl());
  [s_el.music, s_el.voice, ...s_el.sfx, ...s_el.chime].forEach(unlockEl);
  watchEl('music', s_el.music); watchEl('voice', s_el.voice);
  setInterval(mediaTick, 100);
}
function mediaVol() { return volGain(); }
function mediaTick() {
  if (!s_el.music) return;
  const want = (s_elVoice && mediaVoiceBusy()) ? db(DUCK_DB) : 1;
  s_elDuck += (want - s_elDuck) * 0.35;
  if (s_elMusic) s_el.music.volume = Math.max(0, Math.min(1, mediaVol() * (s_elMusic.gainPct / 100) * s_elDuck));
}
function mediaVoiceBusy() { const v = s_elVoice; return !!v && !v.el.ended && !v.el.paused; }
// WAV synth for the chimes: notes = [[hz, startMs, lenMs, type]], 22050 Hz mono 16-bit
function synthWav(notes, gain = 0.5) {
  const sr = 22050;
  const total = Math.max(...notes.map(n => n[1] + n[2])) + 80;
  const N = Math.ceil(total * sr / 1000), pcm = new Float32Array(N);
  for (const [hz, st, len, type] of notes) {
    const a = Math.floor(st * sr / 1000), b = Math.min(N, a + Math.floor(len * sr / 1000));
    for (let i = a; i < b; i++) {
      const t = (i - a) / sr, u = (i - a) / (b - a);
      const env = Math.min(1, u * 25) * Math.pow(1 - u, 1.6);
      const ph = 2 * Math.PI * hz * t;
      let v = Math.sin(ph);
      if (type === 'triangle') v = 2 / Math.PI * Math.asin(Math.sin(ph));
      else if (type === 'square') v = Math.sign(Math.sin(ph)) * 0.5;
      else if (type === 'sawtooth') v = ((t * hz) % 1) * 2 - 1;
      pcm[i] += v * env;
    }
  }
  const buf = new ArrayBuffer(44 + N * 2), dv = new DataView(buf);
  const w = (o, str) => { for (let i = 0; i < str.length; i++) dv.setUint8(o + i, str.charCodeAt(i)); };
  w(0, 'RIFF'); dv.setUint32(4, 36 + N * 2, true); w(8, 'WAVE'); w(12, 'fmt '); dv.setUint32(16, 16, true); dv.setUint16(20, 1, true); dv.setUint16(22, 1, true);
  dv.setUint32(24, sr, true); dv.setUint32(28, sr * 2, true); dv.setUint16(32, 2, true); dv.setUint16(34, 16, true); w(36, 'data'); dv.setUint32(40, N * 2, true);
  for (let i = 0; i < N; i++) dv.setInt16(44 + i * 2, Math.max(-1, Math.min(1, pcm[i] * gain * 0.6)) * 32767, true);
  return URL.createObjectURL(new Blob([buf], { type: 'audio/wav' }));
}
function chimeNotes(kind) {
  const seq = (fs, len, gap, type) => fs.map((f, i) => [f, i * gap, len, type]);
  switch (kind) {
    case CHIME.ABSORB: return [[523, 0, 140, 'triangle'], [784, 120, 220, 'triangle']];
    case CHIME.REVEAL: return seq([523, 659, 784], 180, 110, 'triangle');
    case CHIME.RARE: return seq([659, 784, 988, 1319], 220, 100, 'triangle');
    case CHIME.LEGEND: return [...seq([523, 659, 784, 1047, 1319], 320, 120, 'triangle'), [2093, 600, 500, 'sine']];
    case CHIME.TRICK: return seq([440, 415, 370, 311], 170, 130, 'square');
    case CHIME.TAP: return [[1175, 0, 70, 'triangle']];
    case CHIME.ERROR: return [[196, 0, 260, 'sawtooth'], [185, 20, 260, 'sawtooth']];
    default: return [[1000, 0, 200, 'sine']];
  }
}
// Media-element play results go to the station's events.log (the iPad has no console): every failure,
// and the first success of each kind, so "no sound" can be told apart from "blocked" from the PC.
const s_playOk = new Set();
function reportPlay(kind, a, p) {
  const tag = `${kind} ${String(a.src || '').split('/').pop().slice(0, 40)}`;
  if (p && p.then) {
    p.then(() => { if (!s_playOk.has(kind)) { s_playOk.add(kind); vlog.event(`audio: ${tag} playing (media element)`); } })
      .catch(e => vlog.event(`audio: ${tag} play() refused: ${e.name}: ${e.message}`));
  }
}
function watchEl(kind, a) {
  a.addEventListener('error', () => { const e = a.error; vlog.event(`audio: ${kind} element error ${e ? e.code : '?'} ${e && e.message ? e.message : ''} (${String(a.src || '').split('/').pop()})`); });
}
let s_poolIdx = 0;
function playPooled(pool, url, vol) {
  const a = pool[s_poolIdx++ % pool.length];
  try { a.pause(); a.src = url; a.volume = Math.max(0, Math.min(1, vol)); a.currentTime = 0; reportPlay(pool === s_el.chime ? 'chime' : 'sfx', a, a.play()); } catch (e) { vlog.event(`audio: sfx ${e.name}: ${e.message}`); }
}

// iOS mutes Web Audio with the ring/silent switch (and in a Home Screen app, more eagerly) unless an
// HTML media element is playing: a looping, silent WAV does that. Must be started from a user gesture.
function startUnmute() {
  if (s_unmute) { s_unmute.play().catch(() => {}); return; }
  // 0.1 s of silence, 44.1 kHz mono 8-bit PCM WAV (a low rate can switch iOS's session rate)
  const n = 4410, h = new Uint8Array(44 + n);
  const w = (o, str) => { for (let i = 0; i < str.length; i++) h[o + i] = str.charCodeAt(i); };
  const u32 = (o, v) => { h[o] = v & 255; h[o + 1] = (v >> 8) & 255; h[o + 2] = (v >> 16) & 255; h[o + 3] = (v >> 24) & 255; };
  const u16 = (o, v) => { h[o] = v & 255; h[o + 1] = (v >> 8) & 255; };
  w(0, 'RIFF'); u32(4, 36 + n); w(8, 'WAVE'); w(12, 'fmt '); u32(16, 16); u16(20, 1); u16(22, 1); u32(24, 44100); u32(28, 44100); u16(32, 1); u16(34, 8); w(36, 'data'); u32(40, n);
  h.fill(128, 44);
  const a = document.createElement('audio');
  a.src = URL.createObjectURL(new Blob([h], { type: 'audio/wav' }));
  a.loop = true; a.volume = 0.01; a.setAttribute('playsinline', ''); a.preload = 'auto';
  document.body.append(a);
  a.play().catch(e => console.warn('unmute audio element', e.message));
  s_unmute = a;
}

function db(x) { return Math.pow(10, x / 20); }
function volGain() { return s_volume <= 0 ? 0 : Math.pow(s_volume / 100, 1.6); }

export const audio = {
  // Creates (or re-creates) the engine. Must run inside a user gesture on iOS. The device's own
  // sample rate is used (a forced 48 kHz can leave an iOS context suspended for good); decoded
  // buffers are resampled by the source nodes, so a rebuild keeps every loaded sound.
  _build() {
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return false;
    const old = ctx;
    const musicWas = s_music ? { path: s_music.path, gainPct: s_music.gainPct } : null;
    if (old) { try { s_music && s_music.src.stop(); } catch {} s_music = null; s_voice = null; try { old.close(); } catch {} }
    try { ctx = new AC(); } catch { ctx = null; return false; }
    s_builds++;
    master = ctx.createGain(); master.gain.value = volGain(); master.connect(ctx.destination);
    musicGain = ctx.createGain(); musicGain.connect(master);
    voiceGain = ctx.createGain(); voiceGain.connect(master);
    analyser = ctx.createAnalyser(); analyser.fftSize = 256; voiceGain.connect(analyser);
    sfxGain = ctx.createGain(); sfxGain.connect(master);
    toneGain = ctx.createGain(); toneGain.connect(master);
    s_levelBuf = new Uint8Array(analyser.fftSize);
    if (musicWas) setTimeout(() => this.music(musicWas.path, musicWas.gainPct), 0);
    return true;
  },
  async init(primeMusic = null) {
    startUnmute();
    try { s_mediaForced = JSON.parse(localStorage.getItem('station.audioMedia') || 'null'); } catch { s_mediaForced = null; }
    if (s_mediaForced !== false) mediaInit();                 // unlock the elements inside this first tap (cheap)
    // iPadOS 26 can leave a programmatic play() pending for good (seen 2026-10-26: music element ready,
    // paused, promise never settles). A play() from inside this tap with the REAL source does start, so
    // the music bed is started here, before any await; if Web Audio turns out to run, it is stopped again.
    if (primeMusic && s_mediaForced !== false && s_el.music && media.exists(primeMusic)) {
      const a = s_el.music; a.src = primeMusic; a.loop = true; a.volume = Math.max(0, Math.min(1, mediaVol() * 0.55));
      s_elMusic = { path: primeMusic, gainPct: 55 };
      reportPlay('music (tap)', a, a.play());
    }
    if (s_mediaForced === true) { s_mediaMode = true; console.log('audio: media-element engine (forced)'); return true; }
    if (!ctx && !this._build()) { s_mediaMode = true; mediaInit(); return true; }
    await this.resume();
    if (ctx.state !== 'running') { s_mediaMode = true; mediaInit(); console.warn(`audio: Web Audio stays ${ctx.state} - using the media-element engine`); vlog.event(`audio: Web Audio stays ${ctx.state} - media-element engine`); }
    else if (s_elMusic) { try { s_el.music.pause(); } catch {} s_elMusic = null; }   // Web Audio runs: it plays the bed
    if (!s_tapHook) {
      s_tapHook = true;
      document.addEventListener('pointerdown', () => {
        if (!s_mediaMode && ctx && ctx.state !== 'running') this.resume();
        // media engine: any tap re-starts a music bed iOS left paused (a tap is a gesture it honours)
        if (s_mediaMode && s_elMusic && s_el.music && s_el.music.paused) reportPlay('music (retap)', s_el.music, s_el.music.play());
      }, { passive: true });
    }
    return true;
  },
  ok() { return s_mediaMode || !!ctx; },
  engine() { return s_mediaMode ? 'media elements' : 'Web Audio'; },
  mediaForced() { return s_mediaForced; },
  // Host menu: force the engine (null = automatic). Takes effect on the next start.
  forceMedia(v) { s_mediaForced = v; try { if (v === null) localStorage.removeItem('station.audioMedia'); else localStorage.setItem('station.audioMedia', JSON.stringify(v)); } catch {} },
  // Wakes the engine (call from a user gesture when possible). A context that would not wake last
  // time is thrown away and rebuilt inside this gesture. Never waits more than 1.5 s.
  async resume() {
    startUnmute();
    if (!ctx) return;
    if (ctx.state === 'running') { s_stuck = false; return; }
    if (s_resuming) return;                        // one attempt at a time (pointerdown + click of the same tap)
    s_resuming = true;
    try {
      if (s_stuck && s_builds < 3) this._build();  // a context that would not wake: try a fresh one (twice at most)
      // the classic iOS unlock: resume() AND start a (silent) sound inside the gesture
      const p = ctx.resume().catch(() => {});
      try {
        const b = ctx.createBuffer(1, 1, ctx.sampleRate);
        const src = ctx.createBufferSource(); src.buffer = b; src.connect(ctx.destination); src.start(0);
      } catch {}
      await Promise.race([p, new Promise(r => setTimeout(r, 1500))]);
      s_stuck = ctx.state !== 'running';
      if (s_stuck) console.warn(`audio: context still ${ctx.state} (build ${s_builds}) after resume + unlock`);
    } finally { s_resuming = false; }
  },
  builds() { return s_builds; },
  mediaStatus() { const m = s_el.music; return m ? `music el paused=${m.paused} t=${m.currentTime.toFixed(1)} ready=${m.readyState} net=${m.networkState} vol=${m.volume.toFixed(2)} muted=${m.muted}` : 'no media elements'; },
  unmuteState() { return s_unmute ? (s_unmute.paused ? 'paused' : 'playing') : 'none'; },
  state() { return ctx ? ctx.state : 'none'; },
  setVolume(pct) { s_volume = Math.max(0, Math.min(100, pct | 0)); if (master && ctx) master.gain.setTargetAtTime(volGain(), ctx.currentTime, 0.05); },
  volume() { return s_volume; },

  // Decodes (once) a card WAV. null if missing / bad.
  async buffer(path) {
    if (s_mediaMode || !ctx) return null;
    if (s_decoded.has(path)) { const b = s_decoded.get(path); return b instanceof Promise ? b : b; }
    if (!media.exists(path)) { s_decoded.set(path, null); return null; }
    const p = media.getBuffer(path).then(ab => ctx.decodeAudioData(ab.slice(0))).then(ab => { s_decoded.set(path, ab); return ab; })
      .catch(e => { console.warn('audio', path, e.message); vlog.event(`audio: ${path} failed (${e.name || 'Error'}: ${e.message})`); s_decoded.set(path, null); return null; });
    s_decoded.set(path, p);
    return p;
  },
  isDecoded(path) { const b = s_decoded.get(path); return !!b && !(b instanceof Promise); },
  async preload(paths) { for (const p of paths) { await this.buffer(p); } },

  // ---- music bed (looped) ----
  async music(path, gainPct = 55) {
    if (s_mediaMode) {
      if (!media.exists(path)) return false;
      if (s_elMusic && s_elMusic.path === path && !s_el.music.paused) return true;
      if (s_elMusic && s_elMusic.path === path) { reportPlay('music', s_el.music, s_el.music.play()); return true; }
      s_elMusic = { path, gainPct };
      const a = s_el.music; a.src = path; a.loop = true; a.volume = Math.max(0, Math.min(1, mediaVol() * gainPct / 100 * s_elDuck));
      reportPlay('music', a, a.play());
      return true;
    }
    if (!ctx) return false;
    if (s_music && s_music.path === path) return true;        // same bed already playing
    const buf = await this.buffer(path);
    if (!buf) return false;
    if (s_music && s_music.path === path) return true;
    this.musicStop();
    const src = ctx.createBufferSource(); src.buffer = buf; src.loop = true;
    const g = ctx.createGain(); g.gain.value = gainPct / 100;
    src.connect(g).connect(musicGain);
    src.start();
    s_music = { path, src, g, gainPct };
    this._duck(!!(s_voice && this.voiceBusy()));
    return true;
  },
  musicStop() { if (s_mediaMode) { if (s_el.music) s_el.music.pause(); s_elMusic = null; return; } if (s_music) { try { s_music.src.stop(); } catch {} s_music = null; } },
  musicPlaying() { return s_mediaMode ? !!(s_elMusic && !s_el.music.paused) : !!s_music; },
  _duck(on) { if (musicGain) musicGain.gain.setTargetAtTime(on ? db(DUCK_DB) : 1, ctx.currentTime, 0.08); },

  // ---- voice line ----  resolves to the duration in ms (0 = nothing plays)
  async voice(path, gainPct = 100) {
    if (s_mediaMode) {
      if (!media.exists(path)) return 0;
      const a = s_el.voice;
      try { a.pause(); } catch {}
      a.src = path; a.volume = Math.max(0, Math.min(1, mediaVol() * gainPct / 100));
      const dur = await new Promise(res => {
        let done = false;
        const fin = () => { if (!done) { done = true; res(isFinite(a.duration) ? Math.round(a.duration * 1000) : 0); } };
        a.addEventListener('loadedmetadata', fin, { once: true });
        a.addEventListener('error', () => { if (!done) { done = true; res(0); } }, { once: true });
        setTimeout(fin, 2500);
        a.load();
      });
      if (!dur) { vlog.event(`audio: voice ${path.split('/').pop()} gave no duration (readyState ${a.readyState}, network ${a.networkState})`); return 0; }
      s_elVoice = { el: a, startMs: performance.now(), durMs: dur };
      const p = a.play(); reportPlay('voice', a, p); if (p && p.catch) p.catch(() => { s_elVoice = null; });
      return dur;
    }
    if (!ctx) return 0;
    const buf = await this.buffer(path);
    if (!buf) return 0;
    this.voiceStop();
    const src = ctx.createBufferSource(); src.buffer = buf;
    const g = ctx.createGain(); g.gain.value = gainPct / 100;
    src.connect(g).connect(voiceGain);
    const dur = Math.round(buf.duration * 1000);
    const me = { src, path, endAt: ctx.currentTime + buf.duration, done: false };
    src.onended = () => { me.done = true; if (s_voice === me) { s_voice = null; this._duck(false); } };
    src.start();
    s_voice = me;
    this._duck(true);
    return dur;
  },
  voiceStop() { if (s_mediaMode) { if (s_el.voice) { try { s_el.voice.pause(); } catch {} } s_elVoice = null; return; } if (s_voice) { const v = s_voice; s_voice = null; try { v.src.onended = null; v.src.stop(); } catch {} this._duck(false); } },
  voiceBusy() { if (s_mediaMode) return mediaVoiceBusy(); return !!s_voice && !s_voice.done && ctx.currentTime < s_voice.endAt + 0.05; },
  voiceRemainingMs() { if (s_mediaMode) return s_elVoice ? Math.max(0, Math.round((s_elVoice.durMs - (performance.now() - s_elVoice.startMs)))) : 0; return s_voice ? Math.max(0, Math.round((s_voice.endAt - ctx.currentTime) * 1000)) : 0; },
  voiceLevel() {                       // 0..255 short-term envelope of the voice (for the narrator glow)
    if (s_mediaMode) {                 // no analyser on a media element: a gentle syllable-like pulse while it talks
      if (!mediaVoiceBusy()) { s_level *= 0.8; return Math.round(s_level); }
      const t = performance.now();
      s_level = 120 + 90 * (0.5 + 0.5 * Math.sin(t / 95)) * (0.6 + 0.4 * Math.sin(t / 310));
      return Math.round(s_level);
    }
    if (!analyser || !this.voiceBusy()) { s_level *= 0.8; return Math.round(s_level); }
    analyser.getByteTimeDomainData(s_levelBuf);
    let sum = 0;
    for (let i = 0; i < s_levelBuf.length; i++) { const d = (s_levelBuf[i] - 128) / 128; sum += d * d; }
    const rms = Math.sqrt(sum / s_levelBuf.length);
    const lv = Math.min(255, rms * 900);
    s_level = lv > s_level ? lv : s_level * 0.85 + lv * 0.15;
    return Math.round(s_level);
  },

  // ---- sound effects (preloaded) ----
  sfxLoad(path) {
    if (!media.exists(path)) return -1;
    let h = s_sfx.indexOf(path);
    if (h < 0) { h = s_sfx.length; s_sfx.push(path); }
    if (!s_mediaMode) this.buffer(path); else media.getBuffer(path).catch(() => {});   // warm the browser cache
    return h;
  },
  sfx(handle, gainPct = 100) {
    if (handle < 0 || handle >= s_sfx.length) return;
    if (s_mediaMode) { playPooled(s_el.sfx, s_sfx[handle], mediaVol() * gainPct / 100); return; }
    if (!ctx) return;
    const b = s_decoded.get(s_sfx[handle]);
    if (!b || b instanceof Promise) return;
    const src = ctx.createBufferSource(); src.buffer = b;
    const g = ctx.createGain(); g.gain.value = gainPct / 100;
    src.connect(g).connect(sfxGain);
    src.start();
  },
  sfxStopAll() {},

  // ---- generated tones (always available) ----
  tone(hz, ms, gainPct = 50, type = 'sine', when = 0) {
    if (s_mediaMode) { playPooled(s_el.chime, synthWav([[hz, when * 1000, ms, type]]), mediaVol() * gainPct / 100); return; }
    if (!ctx) return;
    const t0 = ctx.currentTime + when;
    const o = ctx.createOscillator(); o.type = type; o.frequency.value = hz;
    const g = ctx.createGain();
    const a = gainPct / 100 * 0.5;
    g.gain.setValueAtTime(0.0001, t0); g.gain.exponentialRampToValueAtTime(a, t0 + 0.012);
    g.gain.setValueAtTime(a, t0 + Math.max(0.02, ms / 1000 - 0.06)); g.gain.exponentialRampToValueAtTime(0.0001, t0 + ms / 1000);
    o.connect(g).connect(toneGain);
    o.start(t0); o.stop(t0 + ms / 1000 + 0.02);
  },
  chime(kind, gainPct = 60) {
    if (s_mediaMode) {
      if (!s_chimeUrl.has(kind)) s_chimeUrl.set(kind, synthWav(chimeNotes(kind)));
      playPooled(s_el.chime, s_chimeUrl.get(kind), mediaVol() * gainPct / 100);
      return;
    }
    const g = gainPct;
    switch (kind) {
      case CHIME.ABSORB: this.tone(523, 140, g, 'triangle'); this.tone(784, 220, g, 'triangle', 0.12); break;
      case CHIME.REVEAL: [523, 659, 784].forEach((f, i) => this.tone(f, 180, g, 'triangle', i * 0.11)); break;
      case CHIME.RARE: [659, 784, 988, 1319].forEach((f, i) => this.tone(f, 220, g, 'triangle', i * 0.1)); break;
      case CHIME.LEGEND: [523, 659, 784, 1047, 1319].forEach((f, i) => this.tone(f, 320, g, 'triangle', i * 0.12)); this.tone(2093, 500, g * 0.6, 'sine', 0.6); break;
      case CHIME.TRICK: [440, 415, 370, 311].forEach((f, i) => this.tone(f, 170, g, 'square', i * 0.13)); break;
      case CHIME.TAP: this.tone(1175, 70, g, 'triangle'); break;
      case CHIME.ERROR: this.tone(196, 260, g, 'sawtooth'); this.tone(185, 260, g * 0.7, 'sawtooth', 0.02); break;
      default: this.tone(1000, 200, g); break;
    }
  },
};
