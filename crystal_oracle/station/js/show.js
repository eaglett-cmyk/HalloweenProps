// show.js - the Crystal Oracle's director (a port of firmware show.cpp / show.h to the browser).
// Story-neutral: the story is the card's art, voice and the captions.ini words.
//
// Everything a kid sees and hears is a chain of STEPS: a visual (clip -> still -> procedural
// fallback), voice parts back to back (each with a caption and an earliest start), an sfx at
// the start, live overlays (narrator glow, the communal meter, gems, number, bowl icon, fx.*
// effects, sparkles, caption band, host badges), held for max(min, voice + 250 ms), capped at
// max (DESIGN.md 5.1; the rush column when rush mode is on).
//
// Flows: ATTRACT loop (+ chapter story, attract calls) -> crystal placed: ABSORB -> SLEEPY /
// NOCRYSTAL (foreign) / REPLAY / AWAKEN + reveal chain / GREET [RECALL] [CHARGE] + reveal chain
// (HOLD STILL until the write is in -> REVEAL + bowl line -> keeper / milestone / circle -> GOODBYE).
// Taps bless (no-button mode). MOONFULL once per night. Long-press top-left 3 s -> host menu.
//
// Differences from the firmware that follow from the platform: steps are async (voice decode,
// clip fetch) and frames run on requestAnimationFrame; the host menu is a DOM overlay; counters
// live in localStorage + the station server; a simulated crystal never touches the plinth.
import * as S from './settings.js';
import { cfg, counters } from './settings.js';
import { media, Clip } from './media.js';
import { audio, CHIME } from './audio.js';
import * as fx from './fx.js';
import * as token from './token.js';
import * as oracle from './oracle.js';
import { link, LED, WS, statusName } from './link.js';
import { input } from './input.js';
import { vlog } from './vlog.js';
import { service } from './service.js';

const cue = {
  ATTRACT: 'attract', ATTRACT_FULL: 'attract_full', ATTRACT_CALL: 'attract_call', CHAPTER: 'chapter', ABSORB: 'absorb',
  AWAKEN: 'awaken', GREET: 'greet', RECALL: 'recall', CHARGE: 'charge', REVEAL: 'reveal', BOWL: 'bowl', RUSH: 'rush',
  REPLAY: 'replay', NOCRYSTAL: 'nocrystal', SLEEPY: 'sleepy', FOREIGN: 'foreign', GOODBYE: 'goodbye', MILESTONE: 'milestone',
  CIRCLE: 'circle', KEEPER: 'keeper', RANKUP: 'rankup', MOONFULL: 'moonfull',
  SFX_REVEAL: ['reveal_t0', 'reveal_t1', 'reveal_t2', 'reveal_t3'], BOWL_KEY: ['common', 'common', 'rare', 'legend'],
};

// ---------------------------------------------------------------- layout + timing (show.cpp)
const W = 800, H = 480, CX = 400, CY = 222, Y_TITLE = 88, Y_POWER = 118, Y_NUMBER = 250, Y_TIER = 334, Y_METER = 374;
const NUMBER_MID = Y_NUMBER - 46, NUM_ART_X0 = 690, NUM_ART_BASE = 472, NUM_ART_LABEL = 366, NUM_ART_MID = NUM_ART_BASE - 46;
const BAND_BOWL_X0 = 104, BAND_BOWL_Y = 422, BAND_BOWL_SIZE = 88, MINI_BOWL_SIZE = 62, FB_BOWL_X = 150, FB_BOWL_Y = 206, FB_BOWL_SIZE = 130;
// Edge-anchored reveal art follows the visible part of the frame (main.js crops the sides on an iPad).
const numArtX = () => NUM_ART_X0 - (W - fx.SAFE_R), bandBowlX = () => BAND_BOWL_X0 + fx.SAFE_L;
const HINT_L = 132, HINT_R = 668, RECALL_EDGE_X = 116, RECALL_Y = 150, SERVICE_ZONE = 100, SLEEPY_WHITE = 0x707888;
const VOICE_TAIL_MS = 250, PART_MIN_ROOM_MS = 1500, VOICE_STUCK_MS = 1500, LONG_PRESS_MS = 3000, ATTRACT_LOOP_MS = 20000;
const MUSIC_RETRY_MS = 15000, SIM_WRITE_MS = 400, CAPTION_SWAP_MS = 2500, LED_CYCLE_MS = 6000, AURA_STEP_MS = 250, MUNCH_AT_MS = 5400;
const MOON_EASE_MS = 1500, MOON_PULSE_UP_MS = 300, MOON_PULSE_DOWN_MS = 1500, CHARGE_STEP_PCT = 12, MAX_EVENTS_PER_FRAME = 8;
const MAX_GAGS = 5, GROUP_MAX = 8, MAX_PARTS = 6, NO_PART = 0xFF;
const LINE_MS = 7500;
const T = {
  ABSORB: [1000, 1000, 600, 600], GREET: [2000, LINE_MS, 1200, 2000], RECALL: [1500, LINE_MS, 0, 0],
  AWAKEN: [6000, 3 * LINE_MS, 1500, LINE_MS], REVEAL: [2500, LINE_MS, 1500, LINE_MS], REVEAL3: [3000, LINE_MS + 500, 1500, LINE_MS],
  BOWL: [1500, LINE_MS, 0, 0], BOWL_NC: [1500, LINE_MS, 1200, LINE_MS], EXTRA: [1500, LINE_MS, 1200, LINE_MS],
  GOODBYE: [1500, LINE_MS, 0, 0], REPLAY: [2000, LINE_MS, 1500, LINE_MS], NOCRYSTAL: [2500, LINE_MS, 1800, LINE_MS], SLEEPY: [2000, LINE_MS, 2000, LINE_MS],
};
const CHAPTER_PART_MIN_MS = 4500, CHAPTER_PART_MAX_MS = 12000, MOONFULL_MIN_MS = 6000, MOONFULL_MAX_MS = 16000;
const Mode = { Attract: 0, Chapter: 1, Absorb: 2, Awaken: 3, Greet: 4, Recall: 5, Charge: 6, Hold: 7, Reveal: 8, Extra: 9, Goodbye: 10, Replay: 11, NoCrystal: 12, Sleepy: 13, MoonFull: 14 };
const METER_ON_DEFAULT = 'attract,absorb,greet,recall,charge,reveal_t0,reveal_t1,replay,nocrystal,foreign,sleepy,goodbye,chapter_a,chapter_f';

const now = () => performance.now();
const clampRank = r => (r < 1 ? 1 : r > 7 ? 7 : r);
const clampTier = t => (t > 3 ? 3 : t < 0 ? 0 : t);
const nColors = () => S.colorCount() || 1;
const clampColor = n => ((n < 1 || n > nColors()) ? 1 : n);
const clamp01 = v => (v < 0 ? 0 : v > 1 ? 1 : v);
const easeOut = f => { f = clamp01(f); return 1 - (1 - f) * (1 - f); };
const pulse01 = (t, p) => 0.5 + 0.5 * Math.sin((t % p) * (2 * Math.PI / p));
const randBelow = oracle.randBelow;
const lerpRgb = (a, b, f) => fx.mixRgb(a, b, f);

// ---------------------------------------------------------------- state
function newStep(m) {
  return { mode: m, vis: ['', '', ''], visVariant: 0, voice: [], voiceVar: [], partCap: [], partNotBefore: [], nVoice: 0, coherent: true,
           caption: '', fbTitle: null, titleDup: null, minMs: 0, maxMs: 0, minFromClip: false, loop: false, freeze: false, interruptible: false,
           rainbow: false, crystalTint: false, energy: 120, rgb: 0x8B5CF6, ambient: 0, sfx: -1, chime: -1,
           bowlTier: -1, bowlMini: false, bowlPart: NO_PART, bowlAt: Infinity, overlay: null, band: null, tick: null };
}
function newRun() { return { part: 0, speaking: false, allDone: false, anySpoke: false, lastStarted: -1, partStart: 0, partDur: 0, doneAt: 0, variant: 0, path: '', starting: false }; }
function newSeq() {
  return { active: false, simulated: false, rush: false, first: false, removed: false, c: null, colorIdx: 1, rgb: 0x8B5CF6, anim: false, path: null,
           decided: false, recorded: false, o: null, writes: [], nWrites: 0, noWrites: false, writeReq: 0, writePending: false, writeOk: false,
           writeStatus: WS.OK, failedPage: 0, writeTries: 0, writeSentMs: 0, simDoneAt: 0,
           rankBefore: 1, rankAfter: 1, meterFrom: 1, meterTo: 2, gemAt: 0, gemAt2: 0, gemsLit: 0, gemLit: false,
           chargeFill: 0, chargeFullT: -1, chargeLedSent: 0xFF, lockAt: 0, locked: false, gag: 0, bowlTier: -1, bowlShownAt: -1,
           milestone: false, circle: false, fxNext: 0, fxDone: false, capRank: '', recallLine: '' };
}
const s_w = {};               // words
const s_sfx = { absorb: -1, awaken: -1, tap: -1, full: -1, sleepy: -1, button: -1, whoosh: -1, munch: -1, milestone: -1, moonfull: -1, reveal: [-1, -1, -1, -1], gag: [-1, -1, -1, -1, -1, -1] };
let s_step = newStep(Mode.Attract), s_run = newRun(), s_seq = newSeq();
let s_mode = Mode.Attract, s_stepT0 = 0;
let s_vis = { kind: 'none', path: '', stem: '' }, s_visBase = '';
const s_clip = new Clip();
let s_clipFails = 0, s_stillFailed = false, s_endStep = false, s_art = false, s_shown = '';
let s_anchor = { ok: false, x: 0, y: 0, r: 0 }, s_anchorLv = 0, s_bowlFirstT = -1;
const s_meter = { style: 'moon', x: 650, y: 110, r: 46, rgb: 0xE9EEFF, name: 'Moon', counterBase: 194, recallX: 116, recallY: 150, on: [] };
let s_meterOn = false, s_meterZoom = false, s_meterZ0 = 1, s_meterZ1 = 1;
let s_fx = [];
let s_g = input.update();
let s_next = null, s_hasNext = false, s_cur = null;
let s_buttonReq = false, s_tapReq = false, s_serviceReq = false, s_chapterReq = false;
let s_seqEndMs = 0, s_seqEnded = false, s_tapSparkMs = 0, s_tapSparked = false;
let s_freeze = null, s_freezeOk = false;
let s_idleRgb = 0x8B5CF6, s_musicFile = false, s_musicFullFile = false, s_musicCheckMs = 0, s_lastCallMs = 0;
let s_attractLoopsSeen = 0, s_loopsSinceChapter = 0, s_munchLoop = -1, s_ledCycleMs = 0, s_ledIdx = 0, s_chapterAvail = false, s_begun = false;
let s_moonFrom = 0, s_moonTo = 0, s_moonAt = 0, s_moonPulseAt = 0, s_moonFinaleDue = false;
const s_group = []; let s_groupLastMs = 0;
const s_memo = new Map();
let s_canvas = null, s_ctx = null, s_running = false;
const MUSIC_PATH = '/oracle/music/idle.wav', MUSIC_FULL_PATH = '/oracle/music/idle_full.wav';

const timingMin = tm => (s_seq.rush ? tm[2] : tm[0]);
const timingMax = tm => (s_seq.rush ? tm[3] : tm[1]);

function colourCycle(tMs, periodMs) {
  const n = nColors();
  if (n === 1) return S.crystalRgb(1, tMs);
  const seg = Math.max(1, Math.floor(periodMs / n)), pos = tMs % (seg * n), i = Math.floor(pos / seg), f = Math.floor(((pos % seg) * 255) / seg);
  return lerpRgb(S.crystalRgb(1 + i, tMs), S.crystalRgb(1 + (i + 1) % n, tMs), f);
}
function steadyRgb(id) { const st = S.crystalStops(id, 1); return st.length ? st[0] : S.crystalRgb(id, 0); }
function refreshCrystalRgb(t) { const q = s_seq; if (!q.active || !q.anim) return; q.rgb = S.crystalRgb(q.colorIdx, t); if (s_step.crystalTint) s_step.rgb = q.rgb; }
function setSeqColour(id, t) { const q = s_seq; q.colorIdx = clampColor(id); q.anim = S.crystalAnimated(q.colorIdx); q.rgb = S.crystalRgb(q.colorIdx, t); }
function tintCrystal(st) { st.rgb = s_seq.rgb; st.crystalTint = true; }
function crystalLed(mode, id) {
  const steady = mode === LED.IDLE || mode === LED.SOLID;
  if ((steady || mode === LED.CHARGING) && S.crystalRainbow(id)) { link.led(LED.RAINBOW, 0); return; }
  const stops = S.crystalStops(id, 2);
  if ((steady || mode === LED.CHARGING) && stops.length >= 2) { link.led(steady ? LED.DUAL : LED.CHARGING, stops[0], stops[1]); return; }
  link.led(mode, S.crystalRgb(id, now()));
}
function playSfx(handle, fallback, gain = 90) { if (handle >= 0) audio.sfx(handle, gain); else audio.chime(fallback); }
const colourName = n => s_w.colorName[clampColor(n)];

function expandTokens(src) {
  if (!src || !src.includes('{')) return src || '';
  return src.replace(/\{([a-z_]+)\}/gi, (m, k) => {
    k = k.toLowerCase();
    if (k === 'year') return String(cfg.eventYear);
    if (k === 'next_year') return String(cfg.eventYear + 1);
    if (k === 'rank') return s_w.rank[clampRank(s_seq.active ? s_seq.rankAfter : 1)];
    if (k === 'color' || k === 'colour') return s_seq.active ? colourName(s_seq.colorIdx) : '';
    return m;
  });
}

// ---------------------------------------------------------------- voice (groups, variants, no-button swaps)
const memoFor = group => s_memo.get(group) || 0;
const setMemo = (group, v) => s_memo.set(group, v);
const voicePath = (group, v) => `/oracle/voice/${group}_${v}.wav`;
const voiceFileExists = (group, v) => media.exists(voicePath(group, v));
function pickAvoiding(n, key) {
  if (!n) return 0;
  const last = memoFor(key);
  let v;
  if (n === 1) v = 1;
  else if (last >= 1 && last <= n) { v = 1 + randBelow(n - 1); if (v >= last) v++; }
  else v = 1 + randBelow(n);
  setMemo(key, v);
  return v;
}
let s_vsub = [];
function lineId(id) { const m = /^(.+)_([1-9])$/.exec(id); return m ? { group: m[1], v: Number(m[2]) } : null; }
const findSub = (id, line) => s_vsub.find(r => r.fromLine === line && r.from === id.toLowerCase()) || null;
const lineSub = (group, v) => (s_vsub.length ? findSub(`${group}_${v}`, true) : null);
function subPlayable(r) { if (!r.to) return false; const l = lineId(r.to); return l ? voiceFileExists(l.group, l.v) : media.countVariants('voice', r.to, 'wav') > 0; }
function lineBlocked(group, v) { const r = lineSub(group, v); return !!r && !subPlayable(r); }
function variantCount(group) {
  let n = media.countVariants('voice', group, 'wav');
  if (!s_vsub.length) return n;
  while (n < 9) { const v = n + 1; if (!voiceFileExists(group, v) && !lineSub(group, v)) break; n = v; }
  return n;
}
function loadVoiceSubs() {
  s_vsub = [];
  if (cfg.hasButton) return;
  let bad = 0;
  for (let part of S.text('nobutton.voice', '').split(';')) {
    part = part.trim().replace(/"/g, '');
    if (!part) continue;
    const eq = part.indexOf('=');
    if (eq < 0) { bad++; continue; }
    const from = part.slice(0, eq).trim().toLowerCase(), to = part.slice(eq + 1).trim().toLowerCase();
    if (!from || !/^[a-z0-9_]+$/.test(from) || (to && !/^[a-z0-9_]+$/.test(to)) || s_vsub.length >= 24) { bad++; continue; }
    s_vsub.push({ from, to, fromLine: !!lineId(from) });
  }
  if (s_vsub.length || bad) vlog.event(`no-button voice swaps: ${s_vsub.length} in use${bad ? ', some not understood' : ''} (words nobutton.voice)`);
}
function voiceExists(group) {
  if (!group) return false;
  let g = group;
  if (s_vsub.length) {
    const r = findSub(group, false);
    if (r) { if (!r.to) return false; const l = lineId(r.to); if (l) return voiceFileExists(l.group, l.v); g = r.to; }
  }
  const n = variantCount(g);
  for (let v = 1; v <= n; v++) if (!lineBlocked(g, v)) return true;
  return false;
}
function pickVariant(group, prefer) {
  const n = variantCount(group);
  if (!n) return 0;
  const ok = [];
  for (let v = 1; v <= n; v++) if (!lineBlocked(group, v)) ok.push(v);
  if (!ok.length) return 0;
  const last = memoFor(group);
  if (prefer >= 1 && prefer <= n && ok.includes(prefer)) { setMemo(group, prefer); return prefer; }
  const pool = ok.length > 1 && ok.includes(last) ? ok.filter(v => v !== last) : ok;
  const v = pool[randBelow(pool.length)];
  setMemo(group, v);
  return v;
}
// One line to play for `group`: returns { group, v } (v 0 = nothing plays)
function resolveVoice(group, explicitVar, prefer) {
  let out = group;
  if (s_vsub.length) {
    const r = findSub(group, false);
    if (r) {
      if (!r.to) return { group: out, v: 0 };
      const l = lineId(r.to);
      if (l) { if (!voiceFileExists(l.group, l.v)) return { group: l.group, v: 0 }; setMemo(l.group, l.v); return { group: l.group, v: l.v }; }
      out = r.to;
    }
  }
  let v;
  if (explicitVar) { v = (voiceFileExists(out, explicitVar) || lineSub(out, explicitVar)) ? explicitVar : 0; if (v) setMemo(out, v); }
  else v = pickVariant(out, prefer);
  if (!v) return { group: out, v: 0 };
  const ls = lineSub(out, v);
  if (!ls) return { group: out, v };
  if (!subPlayable(ls)) return { group: out, v: 0 };
  const l = lineId(ls.to);
  if (l) { setMemo(l.group, l.v); return { group: l.group, v: l.v }; }
  return { group: ls.to, v: pickVariant(ls.to, 0) };
}
async function sayGroup(group) {
  const r = resolveVoice(group, 0, 0);
  if (!r.v) return 0;
  return audio.voice(voicePath(r.group, r.v));
}

// ---------------------------------------------------------------- music
function musicPath() { if (counters.moonFull && s_musicFullFile) return MUSIC_FULL_PATH; if (s_musicFile) return MUSIC_PATH; return null; }
function startMusic() { const p = musicPath(); if (p) audio.music(p); }

// ---------------------------------------------------------------- the meter
function moonTargetPhase() { if (counters.moonFull) return 8; const goal = cfg.moonGoal || 1; const ph = Math.floor(counters.moonPoints * 8 / goal); return ph >= 8 ? 8 : ph; }
function moonPhaseNow(t) { const dt = t - s_moonAt; const f = easeOut(dt <= 0 ? 0 : dt / MOON_EASE_MS); return s_moonFrom + (s_moonTo - s_moonFrom) * f; }
function updateMoonTarget(t) { const target = moonTargetPhase(); if (target === s_moonTo) return; s_moonFrom = moonPhaseNow(t); s_moonTo = target; s_moonAt = t; }
function moonSnap() { s_moonFrom = s_moonTo = moonTargetPhase(); s_moonAt = now(); }
function moonFinalePending() { return !counters.moonFull && counters.moonPoints >= (cfg.moonGoal || 1); }
function moonAddPoint() {
  if (counters.moonPoints < 0xFFFF) counters.moonPoints++;
  s_moonPulseAt = now() || 1;
  updateMoonTarget(s_moonPulseAt);
  if (moonFinalePending()) s_moonFinaleDue = true;
}
function moonGlow(t) {
  const base = counters.moonFull ? 150 : 100;
  let p = 0;
  const dt = t - s_moonPulseAt;
  if (s_moonPulseAt && dt >= 0) { if (dt < MOON_PULSE_UP_MS) p = dt / MOON_PULSE_UP_MS; else if (dt < MOON_PULSE_UP_MS + MOON_PULSE_DOWN_MS) p = 1 - (dt - MOON_PULSE_UP_MS) / MOON_PULSE_DOWN_MS; }
  return Math.min(255, base + (255 - base) * p);
}
function readInts(s, maxN) { const out = []; for (const m of String(s).split(/[;#]/)[0].matchAll(/-?\d+/g)) { if (out.length >= maxN) break; out.push(Number(m[0])); } return out; }
function idListed(id, list) {
  if (!id) return false;
  const lid = id.toLowerCase();
  return list.some(e => e && lid.startsWith(e.toLowerCase()) && (lid.length === e.length || lid[e.length] === '_'));
}
function loadMeter() {
  const m = s_meter;
  m.style = /^\s*lantern/i.test(S.text('meter.style', 'moon')) ? 'lantern' : 'moon';
  const lantern = m.style === 'lantern';
  m.x = lantern ? 150 : 650; m.y = 110; m.r = lantern ? 70 : 46;
  const v = readInts(S.text('meter.pos', ''), 3);
  if (v.length >= 2 && v[0] >= 0 && v[0] < W && v[1] >= 0 && v[1] < H) { m.x = v[0]; m.y = v[1]; if (v.length >= 3) m.r = Math.min(200, Math.max(8, v[2])); }
  else vlog.event(`meter.pos "${S.text('meter.pos', '')}" not understood - using ${m.x},${m.y},${m.r}`);
  m.rgb = S.color('meter.color', lantern ? 0xFFB347 : 0xE9EEFF);
  m.name = S.meterName();
  m.counterBase = Math.min(350, Math.max(30, m.y + (lantern ? Math.floor(m.r * 6 / 10) : m.r) + 38));
  m.recallX = m.x > W / 2 ? RECALL_EDGE_X : W - RECALL_EDGE_X; m.recallY = RECALL_Y;
  m.on = S.text('meter.on', METER_ON_DEFAULT).split(/[\s,;"]+/).filter(Boolean).slice(0, 32);
  vlog.event(`meter: ${m.style} at ${m.x},${m.y} r ${m.r}, ${m.on.length} visual id(s)`);
}
function meterAt(t) {
  let x = s_meter.x, y = s_meter.y, r = s_meter.r;
  if (!s_meterZoom || !s_clip.isOpen()) return [x, y, r];
  const frames = s_clip.frames(), dur = s_clip.durationMs();
  if (s_step.loop && dur) t %= dur;
  const span = frames > 1 && dur ? dur * (frames - 1) / frames : 0;
  const f = span > 0 ? clamp01(t / span) : 1;
  const z = s_meterZ0 + (s_meterZ1 - s_meterZ0) * f;
  return [Math.round(CX + (x - CX) * z), Math.round(H / 2 + (y - H / 2) * z), Math.round(r * z)];
}
function drawMeter(t, nowMs) {
  updateMoonTarget(nowMs);
  const [x, y, r] = meterAt(t);
  if (s_meter.style === 'lantern') fx.lanternMeter(x, y, r, moonPhaseNow(nowMs), moonGlow(nowMs), s_meter.rgb, nowMs);
  else fx.moonMeter(x, y, r, moonPhaseNow(nowMs), moonGlow(nowMs), s_meter.rgb, nowMs);
}

// ---------------------------------------------------------------- group colours (circle)
function noteGroupColour(anonId, colorIdx, t) {
  const gapMs = cfg.groupGapSec * 1000;
  if (gapMs === 0 || !s_group.length || t - s_groupLastMs >= gapMs) s_group.length = 0;
  s_groupLastMs = t;
  if (s_group.some(m => m.anonId === anonId)) return;
  if (s_group.length >= GROUP_MAX) s_group.shift();
  s_group.push({ anonId, colorIdx: clampColor(colorIdx) });
}

// ---------------------------------------------------------------- write handling
function issueWrite(t) {
  const q = s_seq;
  if (!q.nWrites) return;
  q.writeTries++; q.writePending = true; q.writeOk = false; q.writeSentMs = t;
  if (q.simulated) { q.writeReq = 0; q.simDoneAt = t + SIM_WRITE_MS; return; }
  q.writeReq = link.write(q.c.uid, q.c.uidLen, q.writes);
}
function imageHasWrites(img) {
  const q = s_seq;
  if (!img.readOk || !q.nWrites) return false;
  for (const w of q.writes) {
    if (w.page < 3 || w.page > 39) return false;
    if (w.page === 3 && token.page(img, 3)[0] === 0xE1) continue;
    const p = token.page(img, w.page);
    for (let i = 0; i < 4; i++) if (p[i] !== w.data[i]) return false;
  }
  return true;
}
function onWriteDone(ev, t) {
  const q = s_seq;
  if (!q.active || q.simulated || !q.writePending || ev.reqId !== q.writeReq) { console.log(`[show] late/unknown WriteDone req=${ev.reqId} status=${statusName(ev.status)}`); return; }
  q.writePending = false; q.writeStatus = ev.status; q.failedPage = ev.failedPage;
  if (ev.status === WS.OK) { q.writeOk = true; return; }
  vlog.event(`write failed: crystal ${q.c.anonId.toString(16).padStart(8, '0')} status=${statusName(ev.status)} page=${ev.failedPage} try=${q.writeTries}${q.removed ? ' (removed)' : ''}`);
  if (!q.removed && ev.status !== WS.BAD_REQUEST && q.writeTries < 2) issueWrite(t);
}
function onRepresented(img, t) {
  const q = s_seq;
  q.removed = false;
  if (!q.nWrites || q.writeOk || q.writePending) return;
  if (imageHasWrites(img)) { q.writeOk = true; q.writeStatus = WS.OK; vlog.event(`write confirmed on re-presentation: crystal ${q.c.anonId.toString(16)}`); return; }
  if (q.writeTries < 3) issueWrite(t);
}
function pumpSequence(t) { const q = s_seq; if (q.active && q.simulated && q.writePending && t >= q.simDoneAt) { q.writePending = false; q.writeOk = true; q.writeStatus = WS.OK; } }

// ---------------------------------------------------------------- charge
function addCharge(x, y, t) {
  const q = s_seq;
  if (q.chargeFill >= 100) { fx.sparklesBurst(x, y, 6, q.rgb); return; }
  q.chargeFill = Math.min(100, q.chargeFill + CHARGE_STEP_PCT);
  fx.sparklesBurst(x, y, 12, q.rgb);
  if (s_meterOn) { const [mx, my, mr] = meterAt(t - s_stepT0); fx.sparklesBurst(mx, my + mr / 2, 5, q.rgb); }
  playSfx(s_sfx.tap, CHIME.TAP, 70);
  const level = Math.floor(q.chargeFill * 255 / 100);
  if (level !== q.chargeLedSent) { link.led(LED.METER, q.rgb, 0, 0, 0, level); q.chargeLedSent = level; }
  if (q.chargeFill >= 100) { q.chargeFullT = t - s_stepT0; playSfx(s_sfx.full, CHIME.RARE); fx.sparklesBurst(CX, CY, 40, q.rgb); }
}

// ---------------------------------------------------------------- input routing
const idleMode = () => s_mode === Mode.Attract || s_mode === Mode.Chapter;
function handleReader(ev, t) {
  const q = s_seq;
  switch (ev.type) {
    case 'Placed': {
      const same = q.active && q.c && token.sameUid(ev.tag.uid, ev.tag.uidLen, q.c.uid, q.c.uidLen);
      if (same && q.c.kind !== token.Kind.Unreadable) { onRepresented(ev.tag, t); break; }
      s_next = ev.tag; s_hasNext = true;
      break;
    }
    case 'Removed':
      if (q.active && q.c && token.sameUid(ev.tag.uid, ev.tag.uidLen, q.c.uid, q.c.uidLen)) q.removed = true;
      if (s_hasNext && token.sameUid(ev.tag.uid, ev.tag.uidLen, s_next.uid, s_next.uidLen)) s_hasNext = false;
      break;
    case 'WriteDone': onWriteDone(ev, t); break;
    case 'Button':
      if (!ev.pressed) break;
      if (s_mode === Mode.Charge) addCharge(CX, 333, t);
      else if (idleMode()) { if (!link.tagPresent()) s_buttonReq = true; else fx.sparklesBurst(CX, 420, 10, s_idleRgb); }
      break;
    case 'LinkUp':
      vlog.event(`plinth online (${link.backendName()})`);
      link.setIdleLight(s_idleRgb, cfg.ringBrightness);
      if (!q.active) link.led(LED.IDLE, s_idleRgb);
      break;
    case 'LinkDown': vlog.event('plinth offline'); break;
  }
}
function tapBlesses(t) {
  if (cfg.hasButton) return !link.online();
  if (link.tagPresent()) return false;
  return !s_seqEnded || t - s_seqEndMs >= cfg.tapCooldownSec * 1000;
}
function handleTouch(t) {
  const idle = idleMode();
  if (s_g.tap) {
    const inCorner = input.hit(s_g.tapAt, 0, 0, fx.SAFE_L + SERVICE_ZONE, SERVICE_ZONE);
    if (s_mode === Mode.Charge) addCharge(s_g.tapAt.x, s_g.tapAt.y, t);
    else if (idle && !inCorner && tapBlesses(t)) s_tapReq = true;
    else fx.sparklesBurst(s_g.tapAt.x, s_g.tapAt.y, 8, idle ? colourCycle(t, 18000) : s_step.rgb);
  }
  if (idle && s_g.longPress && input.hit(s_g.longAt, 0, 0, fx.SAFE_L + SERVICE_ZONE, SERVICE_ZONE)) s_serviceReq = true;
}
function pollInputs(t) {
  s_g = input.update(LONG_PRESS_MS);
  handleTouch(t);
  for (let i = 0; i < MAX_EVENTS_PER_FRAME; i++) { const ev = link.poll(); if (!ev) break; handleReader(ev, t); }
  if (s_step.interruptible && (s_hasNext || (idleMode() && (s_buttonReq || s_tapReq || s_serviceReq)))) s_endStep = true;
}

// ---------------------------------------------------------------- voice sequencing
async function startNextPart(t) {
  const st = s_step;
  while (s_run.part < st.nVoice) {
    const i = s_run.part;
    if (st.partNotBefore[i] > t) return;
    const prefer = (!st.voiceVar[i] && st.coherent && i > 0) ? s_run.variant : 0;
    const r = resolveVoice(st.voice[i], st.voiceVar[i], prefer);
    if (r.v) {
      const path = voicePath(r.group, r.v);
      const d = await audio.voice(path);
      if (st !== s_step) return;                    // the step ended while decoding
      if (d) {
        if (!s_run.variant) s_run.variant = r.v;
        s_run.speaking = true; s_run.anySpoke = true; s_run.lastStarted = i;
        s_run.partStart = now() - s_stepT0; s_run.partDur = d; s_run.path = path;
        if (st.partCap[i] != null) st.caption = st.partCap[i];
        return;
      }
    }
    s_run.part++;
  }
  s_run.allDone = true;
}
function advanceVoice(t) {
  if (s_run.allDone || s_run.starting) return;
  if (s_run.speaking) {
    const el = t - s_run.partStart;
    const finished = (el >= s_run.partDur && !audio.voiceBusy()) || el >= s_run.partDur + VOICE_STUCK_MS;
    if (!finished) return;
    s_run.speaking = false; s_run.doneAt = t; s_run.part++;
  }
  if (s_run.part >= s_step.nVoice) { s_run.allDone = true; return; }
  if (s_step.maxMs !== Infinity && t + PART_MIN_ROOM_MS > s_step.maxMs) { s_run.allDone = true; return; }
  if (s_step.partNotBefore[s_run.part] > t) return;
  s_run.starting = true;
  const run = s_run;
  startNextPart(t).finally(() => { run.starting = false; });
}
function timedPartCaptions(t) {
  const st = s_step;
  if (s_run.anySpoke || !s_run.allDone || st.nVoice < 2 || !st.minMs) return;
  const caps = st.partCap.filter(c => c != null);
  if (caps.length < 2) return;
  const want = Math.min(caps.length - 1, Math.floor(t * caps.length / st.minMs));
  st.caption = caps[want];
}
function stepFinished(t) {
  const st = s_step;
  if (t >= st.maxMs) return true;
  if (!s_run.allDone || s_run.starting) return false;
  const voiceEnd = s_run.doneAt ? s_run.doneAt + VOICE_TAIL_MS : 0;
  return t >= Math.max(st.minMs, voiceEnd);
}

// ---------------------------------------------------------------- anchor / meter / fx per step
function parseAnchor(v) {
  const n = readInts(v || '', 3);
  if (n.length < 2) return { ok: false };
  if (n[0] < -50 || n[0] > W + 50 || n[1] < -50 || n[1] > H + 50) return { ok: false };
  return { ok: true, x: n[0], y: n[1], r: Math.min(300, Math.max(6, n.length >= 3 ? n[2] : 40)) };
}
function loadAnchor() {
  s_anchor = { ok: false };
  if (!s_shown) return;
  s_anchor = parseAnchor(S.text(`anchor.${s_shown}`, ''));
  if (!s_anchor.ok && s_visBase && s_visBase.toLowerCase() !== s_shown.toLowerCase()) s_anchor = parseAnchor(S.text(`anchor.${s_visBase}`, ''));
}
function loadStepMeter(st) {
  s_meterOn = false; s_meterZoom = false; s_meterZ0 = s_meterZ1 = 1;
  if (st.freeze || st.mode === Mode.MoonFull) return;
  s_meterOn = idListed(s_shown || st.vis[0], s_meter.on);
  if (!s_meterOn || s_vis.kind !== 'clip' || !s_shown) return;
  let z = S.text(`meter.zoom_${s_shown}`, '');
  if (!z && s_visBase && s_visBase.toLowerCase() !== s_shown.toLowerCase()) z = S.text(`meter.zoom_${s_visBase}`, '');
  if (!z) return;
  const m = /^\s*([\d.]+)\s*[, ;\t]\s*([\d.]+)/.exec(z);
  const z0 = m ? parseFloat(m[1]) : NaN, z1 = m ? parseFloat(m[2]) : NaN;
  if (!(z0 >= 0.5 && z0 <= 4 && z1 >= 0.5 && z1 <= 4)) { console.log(`[show] meter.zoom = "${z}" not understood`); return; }
  s_meterZoom = true; s_meterZ0 = z0; s_meterZ1 = z1;
}
function parseStepFx(v, key) {
  s_fx = [];
  for (let part of v.split(';')) {
    part = part.trim().replace(/"/g, '');
    if (!part || s_fx.length >= 4) continue;
    const toks = part.split(/[,\s]+/).filter(Boolean);
    const kindName = (toks.shift() || '').toLowerCase();
    const kind = { poof: 'poof', sparkles: 'sparkles', burst: 'burst', none: 'none' }[kindName];
    if (!kind) { console.log(`[show] ${key}: unknown effect "${kindName}" skipped`); continue; }
    if (kind === 'none') continue;
    let rgb = null;
    const hex = toks.find(x => x[0] === '#');
    if (hex) { const c = /^#([0-9a-f]{6})$/i.exec(hex); if (c) rgb = parseInt(c[1], 16); }
    const n = toks.filter(x => /^-?\d+$/.test(x)).map(Number);
    if (n.length < 3) { console.log(`[show] ${key}: "${kindName}" needs at least kind,x,y,startMs - skipped`); continue; }
    const e = { kind, x: Math.min(W + 100, Math.max(-100, n[0])), y: Math.min(H + 100, Math.max(-100, n[1])), startMs: Math.min(600000, Math.max(0, n[2])), fired: false, next: 0 };
    e.endMs = n.length >= 4 ? Math.min(600000, Math.max(e.startMs, n[3])) : e.startMs;
    e.jx = n.length >= 5 ? Math.min(400, Math.max(0, n[4])) : 14;
    e.jy = n.length >= 6 ? Math.min(300, Math.max(0, n[5])) : (n.length >= 5 ? e.jx : 10);
    e.rgb = rgb;
    s_fx.push(e);
  }
}
function loadStepFx(st) {
  s_fx = [];
  if (st.freeze) return;
  let key = '', v = '';
  if (s_shown) { key = `fx.${s_shown}`; v = S.text(key, ''); }
  if (!v && st.vis[0]) {
    const vv = st.visVariant || s_run.variant;
    if (vv) { key = `fx.${st.vis[0]}_${vv}`; v = S.text(key, ''); }
    if (!v) { key = `fx.${st.vis[0]}`; v = S.text(key, ''); }
  }
  if (v) parseStepFx(v, key);
}
function runStepFx(t) {
  const crystal = s_seq.active ? s_seq.rgb : s_step.rgb;
  for (const e of s_fx) {
    if (t < e.startMs) continue;
    const rgb = e.rgb != null ? e.rgb : (e.kind === 'poof' ? 0xFFFFFF : crystal);
    if (e.kind === 'poof') {
      const span = e.endMs - e.startMs, dur = span ? Math.min(1500, Math.max(200, span)) : 500;
      if (!e.fired) { e.fired = true; fx.sparklesBurst(e.x, e.y, 16, rgb); }
      fx.poof(e.x, e.y, t - e.startMs, rgb, dur);
    } else if (e.kind === 'burst') {
      if (!e.fired) { e.fired = true; fx.sparklesBurst(e.x, e.y, 28, rgb); }
    } else if (e.kind === 'sparkles') {
      if (!e.fired) { e.fired = true; e.next = e.startMs; }
      let guard = 0;
      while (e.next <= t && (e.next < e.endMs || e.next === e.startMs) && guard++ < 40) {
        fx.sparklesBurst(e.x + randBelow(2 * e.jx + 1) - e.jx, e.y + randBelow(2 * e.jy + 1) - e.jy, 3, rgb);
        e.next += 120;
      }
    }
  }
}
function lanternVisibility(t) {
  if (s_vis.kind !== 'clip' || !s_clip.isOpen()) return 1;
  const base = s_visBase.toLowerCase();
  if (base === cue.AWAKEN) return clamp01((t - 3000) / 250);
  if (base === cue.GOODBYE) { const d = s_clip.durationMs(); return d ? 1 - clamp01(t / d) : 0; }
  return 1;
}
function drawAnchorGlow(t) {
  if (!cfg.anchorGlow || !s_anchor.ok) return;   // [station] anchor_glow: the breathing / voice-pulsing glow on his tummy
  const target = audio.voiceLevel() / 255;
  s_anchorLv += (target - s_anchorLv) * (target > s_anchorLv ? 0.45 : 0.12);
  const vis = lanternVisibility(t);
  if (vis <= 0) return;
  const k = clamp01(s_anchorLv), rgb = s_seq.active ? s_seq.rgb : s_w.glowIdle, breath = pulse01(t, 3000), r = s_anchor.r;
  fx.addGlow(s_anchor.x, s_anchor.y, r * (1 + 0.3 * k + 0.05 * breath), rgb, vis * (85 + 20 * breath + 130 * k));
  fx.addGlow(s_anchor.x, s_anchor.y, r * 0.45, lerpRgb(rgb, 0xFFFFFF, 140), vis * (60 + 120 * k));
}
function anchorPoint() { return s_anchor.ok ? [s_anchor.x, s_anchor.y] : [CX, CY]; }

// ---------------------------------------------------------------- freeze (HOLD STILL background)
function snapshotFreeze() {
  if (!s_canvas) return false;
  if (!s_freeze) { s_freeze = document.createElement('canvas'); s_freeze.width = W; s_freeze.height = H; }
  const x = s_freeze.getContext('2d');
  x.globalCompositeOperation = 'source-over';
  x.drawImage(s_canvas, 0, 0);
  x.fillStyle = 'rgba(0,0,0,0.56)'; x.fillRect(0, 0, W, 392);
  x.fillStyle = '#000'; x.fillRect(0, 392, W, H - 392);
  return true;
}

// ---------------------------------------------------------------- drawing
function drawBadges(nowMs) {
  const parts = [];
  let colour = 0xF59E0B;
  const rushNow = s_seq.active ? s_seq.rush : oracle.rush(nowMs);
  if (rushNow) parts.push(s_w.uiRush);
  if (cfg.reader !== S.READER.NONE && !link.online()) { parts.push(s_w.uiAsleep); colour = 0xEF4444; }
  if (!media.sdOk()) { parts.push(s_w.uiNoCard); colour = 0xEF4444; }
  if (parts.length) fx.hostBadge(parts.join(' | '), colour);
}
function bowlVisible(t) {
  const st = s_step;
  if (st.bowlTier < 0) return false;
  if (t >= st.bowlAt) return true;
  return st.bowlPart !== NO_PART && s_run.lastStarted >= st.bowlPart;
}
function drawStepBowl(t) {
  const st = s_step;
  if (!bowlVisible(t)) return;
  if (s_bowlFirstT < 0) { s_bowlFirstT = t; if (!st.bowlMini) fx.sparklesBurst(s_art ? bandBowlX() : FB_BOWL_X, s_art ? BAND_BOWL_Y : FB_BOWL_Y, 14, s_w.bowlRgb[st.bowlTier]); }
  const b = clampTier(st.bowlTier);
  const pop = st.bowlMini ? 1 : 0.6 + 0.4 * easeOut((t - s_bowlFirstT) / 250);
  if (st.bowlMini) fx.bowlIcon(bandBowlX() - 20, BAND_BOWL_Y + 2, MINI_BOWL_SIZE, s_w.bowlShape[b], s_w.bowlRgb[b], t);
  else if (s_art) fx.bowlIcon(bandBowlX(), BAND_BOWL_Y, BAND_BOWL_SIZE * pop, s_w.bowlShape[b], s_w.bowlRgb[b], t);
  else fx.bowlIcon(FB_BOWL_X, FB_BOWL_Y, FB_BOWL_SIZE * pop, s_w.bowlShape[b], s_w.bowlRgb[b], t);
}
function drawFrame(t, dt, nowMs) {
  const st = s_step, g = s_ctx;
  const rgb = st.rainbow ? colourCycle(t, 6000) : st.rgb;
  let auraRgb = st.rgb;
  if (st.rainbow) auraRgb = colourCycle(t - t % AURA_STEP_MS, 6000);
  else if (st.crystalTint && s_seq.active && s_seq.anim) auraRgb = S.crystalRgb(s_seq.colorIdx, nowMs - nowMs % AURA_STEP_MS);

  let drew = false;
  g.globalCompositeOperation = 'source-over';
  if (st.freeze && s_freezeOk) { g.drawImage(s_freeze, 0, 0); drew = true; }
  else if (s_vis.kind === 'clip' && s_clip.isOpen() && s_clipFails < 3) {
    if (s_clip.render(t, st.loop, g)) drew = true;
    else if (++s_clipFails >= 3) vlog.event(`clip ${s_vis.path} failed, using the fallback`);
  } else if (s_vis.kind === 'still' && !s_stillFailed) {
    if (media.drawStill(s_vis.path, g)) drew = true;
    else if (media.stillFailed(s_vis.path)) { s_stillFailed = true; vlog.event(`still ${s_vis.path} failed, using the fallback`); }
    else drew = true;                           // decoding: keep the previous frame a moment
  }
  s_art = drew && !st.freeze;

  const cap = expandTokens(st.caption), ttl = expandTokens(st.fbTitle);
  let captionInTitle = false;
  if (!drew) {
    if (ttl) { fx.fallbackCue(ttl, t, rgb, st.energy, auraRgb); captionInTitle = cap === ttl; }
    else fx.orbBackground(t, rgb, st.energy, auraRgb);
  }
  if (s_art) drawAnchorGlow(t);
  if (s_meterOn) drawMeter(t, nowMs);
  fx.sparklesAmbient(rgb, st.ambient, nowMs);
  if (st.overlay) st.overlay(t);
  if (s_fx.length) runStepFx(t);
  fx.sparklesStep(dt);
  let showCap = !!cap && !captionInTitle;
  if (showCap && !s_art && st.titleDup && cap.toLowerCase() === st.titleDup.toLowerCase()) showCap = false;
  if (showCap) fx.caption(cap);
  if (st.band) st.band(t);
  drawStepBowl(t);
  drawBadges(nowMs);
}

// ---------------------------------------------------------------- the step runner
function addVoice(st, group, variant = 0, cap = null, notBefore = 0) {
  if (st.nVoice >= MAX_PARTS || !group) return NO_PART;
  const i = st.nVoice++;
  st.voice[i] = group; st.voiceVar[i] = variant; st.partCap[i] = cap; st.partNotBefore[i] = notBefore;
  return i;
}
function addBowlPart(st, tier, notBefore, fallbackAt) {
  tier = clampTier(tier);
  s_seq.bowlTier = tier; st.bowlTier = tier;
  const g = `bowl_${cue.BOWL_KEY[tier]}`;
  if (voiceExists(g)) { st.bowlPart = addVoice(st, g, 0, s_w.bowlName[tier], notBefore); st.bowlAt = st.maxMs > 1200 ? st.maxMs - 1200 : 0; }
  else st.bowlAt = fallbackAt;
}
function setTiming(st, tm) { st.minMs = timingMin(tm); st.maxMs = Math.max(st.minMs, timingMax(tm)); }
const nextFrame = () => new Promise(r => requestAnimationFrame(r));

// Runs s_step until it finishes. Resolves false if an event interrupted it.
async function runStep() {
  const st = s_step;
  s_mode = st.mode; s_endStep = false; s_run = newRun(); s_clipFails = 0; s_stillFailed = false; s_bowlFirstT = -1;
  s_seq.fxNext = 0; s_seq.fxDone = false;
  s_stepT0 = now();
  await startNextPart(0);                         // the voice's variant picks the visual variant
  s_vis = { kind: 'none', path: '', stem: '' }; s_visBase = '';
  const vv = st.visVariant || s_run.variant;
  for (const id of st.vis) {
    if (!id) continue;
    const v = media.findVisual(id, vv);
    if (v.kind !== 'none') { s_vis = v; s_visBase = id; break; }
  }
  if (s_vis.kind === 'clip') {
    const ok = await s_clip.open(s_vis.path);
    if (st !== s_step) return false;
    if (!ok) { vlog.event(`clip ${s_vis.path} won't open, using the fallback`); s_vis = { kind: 'none', path: '', stem: '' }; }
    else if (st.minFromClip) { st.minMs = Math.max(st.minMs, s_clip.durationMs()); if (st.maxMs < st.minMs) st.maxMs = st.minMs; }
  }
  if (s_vis.kind === 'none') s_visBase = '';
  s_shown = s_vis.stem || '';
  loadAnchor(); loadStepMeter(st); loadStepFx(st);
  if (st.sfx >= 0) audio.sfx(st.sfx, 90); else if (st.chime >= 0) audio.chime(st.chime);
  if (st.freeze) s_freezeOk = snapshotFreeze();
  s_stepT0 = now();                               // the step's clock starts once its assets are in
  if (s_run.speaking) s_run.partStart = 0;

  let completed = true, lastFrame = s_stepT0;
  for (;;) {
    const frameStart = now(), t = frameStart - s_stepT0;
    refreshCrystalRgb(frameStart);
    pollInputs(frameStart);
    pumpSequence(frameStart);
    if (s_endStep) { completed = false; break; }
    advanceVoice(t);
    timedPartCaptions(t);
    if (st.tick && st.tick(t)) break;
    if (stepFinished(t)) break;
    drawFrame(t, frameStart - lastFrame, frameStart);
    lastFrame = frameStart;
    await nextFrame();
    if (!s_running) { completed = false; break; }
  }
  if (s_clip.isOpen()) s_clip.close();
  if (!completed) audio.voiceStop();
  else if (s_run.speaking && audio.voiceBusy()) console.log(`[show] voice ${s_run.path} cut by the ${st.maxMs} ms cap (${audio.voiceRemainingMs()} ms left)`);
  return completed;
}

// ================================================================== overlays + ticks
function drawGlowNumber(shown, tier, rgb, t, smudgeLevel, label) {
  const nx = s_art ? numArtX() : CX, base = s_art ? NUM_ART_BASE : Y_NUMBER, mid = s_art ? NUM_ART_MID : NUMBER_MID;
  if (label) fx.subtitle(label, nx, s_art ? NUM_ART_LABEL : Y_POWER, 0xFFFFFF);
  if (tier === 0) { if (smudgeLevel) fx.smudgeCloud(nx, mid, s_art ? 66 : 100, t, smudgeLevel, s_w.tierRgb[0]); }
  else fx.bigNumber(shown, nx, base, rgb);
}
function attractOverlay(t) {
  if (cfg.showCounter) { if (s_meterOn) fx.counterBadgeAt(s_meter.x, s_meter.counterBase, counters.stationCrystals, s_w.uiCounter); else fx.counterBadge(counters.stationCrystals, s_w.uiCounter); }
  if (s_g.down && input.hit(s_g.at, 0, 0, fx.SAFE_L + SERVICE_ZONE, SERVICE_ZONE) && s_g.heldMs > 500) fx.ring(fx.SAFE_L + 50, 50, 34, 6, 0xFFFFFF, Math.min(255, s_g.heldMs * 255 / LONG_PRESS_MS));
}
function hintsBand(t) {
  const tapMode = !link.online() && input.ok();
  if (tapMode && s_mode === Mode.Attract) return;
  const rgb = colourCycle(t, 18000);
  fx.placeHintAt(HINT_L, t, rgb); fx.placeHintAt(HINT_R, t + 800, rgb);
}
function attractTick(t) {
  const nowMs = s_stepT0 + t;
  const orPress = cfg.hasButton ? cfg.reader === S.READER.PLINTH : input.ok();
  if (!cfg.attractText) s_step.caption = '';   // station [station] attract_text = no: the art alone
  else if (!link.online() && input.ok()) s_step.caption = s_w.uiTouch;
  else if (orPress && s_w.uiOrPress) s_step.caption = (Math.floor(t / CAPTION_SWAP_MS) & 1) ? s_w.uiOrPress : s_w.capAttract;
  else s_step.caption = s_w.capAttract;
  if (nowMs - s_ledCycleMs >= LED_CYCLE_MS) { s_ledCycleMs = nowMs; s_ledIdx = (s_ledIdx + 1) % nColors(); crystalLed(LED.IDLE, 1 + s_ledIdx); }
  if (nowMs - s_musicCheckMs >= MUSIC_RETRY_MS) { s_musicCheckMs = nowMs; if (!audio.musicPlaying()) startMusic(); }
  if (cfg.attractCallSec && nowMs - s_lastCallMs >= cfg.attractCallSec * 1000) { s_lastCallMs = nowMs; if (!audio.voiceBusy()) sayGroup(cue.ATTRACT_CALL); }
  let loopLen = ATTRACT_LOOP_MS;
  const clip = s_vis.kind === 'clip' && s_clip.isOpen() && s_clipFails < 3;
  if (clip) loopLen = Math.max(s_clip.durationMs(), 4000);
  const loops = Math.floor(t / loopLen);
  if (loops > s_attractLoopsSeen) { s_loopsSinceChapter += loops - s_attractLoopsSeen; s_attractLoopsSeen = loops; }
  if (clip && s_sfx.munch >= 0 && s_visBase.toLowerCase() === cue.ATTRACT && loopLen > MUNCH_AT_MS + 300 && t % loopLen >= MUNCH_AT_MS && loops !== s_munchLoop) { s_munchLoop = loops; audio.sfx(s_sfx.munch, 80); }
  if (s_moonFinaleDue) return true;
  if (cfg.chapterEvery && s_chapterAvail && s_loopsSinceChapter >= cfg.chapterEvery && !audio.voiceBusy()) { s_chapterReq = true; return true; }
  return false;
}
function absorbOverlay(t) {
  const q = s_seq, [tx, ty] = anchorPoint(), rise = q.rush ? 360 : 600, f = easeOut(t / rise);
  const tip = H + 10 + (ty - H - 10) * f, lvl = t < rise ? 230 : Math.max(120, 230 - (t - rise) / 4);
  fx.beam(CX, H + 10, tx, tip, 44, q.rgb, lvl, t);
  if (t >= rise) { fx.burstRing(tx, ty, q.rgb, t - rise, 700); fx.addGlow(tx, ty, 70, q.rgb, 150); }
}
function awakenOverlay(t) {
  const q = s_seq, want = t >= q.gemAt2 ? 2 : t >= q.gemAt ? 1 : 0;
  while (q.gemsLit < want) { q.gemsLit++; fx.sparklesBurst(CX + (q.gemsLit === 1 ? -182 : -130), Y_METER, 22, q.rgb); audio.chime(CHIME.TAP, 75); }
  fx.crystalMeter(CX, Y_METER, q.gemsLit, 8, q.rgb, t, q.gemsLit > 0);
  if (!s_art && q.gemsLit >= 2) { fx.burstRing(CX, Y_TITLE - 14, q.rgb, t - q.gemAt2, 700); fx.title(s_w.rank[clampRank(q.rankAfter)], CX, Y_TITLE, q.rgb); }
}
function greetOverlay(t) {
  const q = s_seq, rankUp = q.rankAfter !== q.rankBefore;
  if (!q.gemLit && t >= q.gemAt) { q.gemLit = true; fx.sparklesBurst(CX, Y_METER, rankUp ? 36 : 16, q.rgb); if (rankUp) playSfx(s_sfx.whoosh, CHIME.RARE, 70); else audio.chime(CHIME.TAP, 70); }
  if (!s_art) { const rank = clampRank(q.gemLit ? q.rankAfter : q.rankBefore); fx.title(s_w.rank[rank], CX, Y_TITLE, q.rgb); if (q.gemLit && rankUp) fx.burstRing(CX, Y_TITLE - 14, q.rgb, t - q.gemAt, 800); }
  fx.crystalMeter(CX, Y_METER, q.gemLit ? q.meterTo : q.meterFrom, 8, q.rgb, t, q.gemLit);
}
function recallOverlay(t) {
  const q = s_seq, lt = clampTier(q.c.lastTier), f = easeOut(t / 900), rx = s_meter.recallX, y = s_meter.recallY + 40 - 40 * f;
  fx.bowlIcon(rx, y, 96, s_w.bowlShape[lt], s_w.bowlRgb[lt], t, 40 + 110 * f);
  fx.subtitle(s_w.tier[lt], rx + 4, y + 88, fx.scaleRgb(s_w.tierRgb[lt], 60 + 140 * f));
  if (!s_art) { fx.subtitle(q.recallLine, CX, Y_TITLE - 30, 0xFFFFFF); fx.title(s_w.tier[lt], CX, Y_TITLE + 30, s_w.tierRgb[lt]); }
}
function chargeOverlay(t) {
  const q = s_seq, f = q.chargeFill;
  if (!s_art) {
    fx.addGlow(CX, CY, 70 + f * 1.3, q.rgb, 60 + f * 1.5);
    fx.ring(CX, CY, 56 + f / 2, 8, q.rgb, 80 + f * 1.75);
    if (q.chargeFullT >= 0) fx.burstRing(CX, CY, 0xFFFFFF, t - q.chargeFullT, 700);
  } else if (q.chargeFullT >= 0 && s_meterOn) { const [mx, my] = meterAt(t); fx.burstRing(mx, my, q.rgb, t - q.chargeFullT, 700); }
}
function chargeBand() {
  const q = s_seq, f = q.chargeFill, g = s_ctx;
  const bx = 200, bw = 400, by = s_art ? 372 : 318, bh = s_art ? 22 : 30;
  g.globalCompositeOperation = 'source-over';
  g.strokeStyle = '#fff'; g.lineWidth = 2;
  g.beginPath(); g.roundRect(bx - 4, by - 4, bw + 8, bh + 8, bh / 2 + 4); g.stroke();
  const w = Math.floor(bw * f / 100);
  if (w > 0) { g.fillStyle = fx.css(q.rgb); g.beginPath(); g.roundRect(bx, by, Math.max(w, bh), bh, bh / 2); g.fill(); }
}
function chargeTick(t) {
  const q = s_seq, canTap = input.ok();
  const canPress = cfg.hasButton ? (cfg.reader === S.READER.PLINTH && link.online()) : canTap;
  if (canTap && canPress) s_step.caption = (Math.floor(t / 1300) & 1) ? s_w.uiPress : s_w.capCharge;
  else s_step.caption = canTap ? s_w.capCharge : s_w.uiPress;
  return q.chargeFullT >= 0 && t >= q.chargeFullT + 700;
}
function holdOverlay(t) { const p = pulse01(t, 1100); fx.glowPulse(CX, CY, 120, s_seq.rgb, t, 1100); fx.ring(CX, CY, 96 + 10 * p, 10, s_seq.rgb, 140 + 110 * p); }
function holdTick() { const q = s_seq; if (!q.writePending || q.removed) return true; return now() - q.writeSentMs >= cfg.writeTimeoutMs; }
function revealCommon(t) {
  const q = s_seq, tier = clampTier(q.o.tier), trgb = s_w.tierRgb[tier];
  if (!q.locked && t >= q.lockAt) {
    q.locked = true;
    const CH = [CHIME.TRICK, CHIME.REVEAL, CHIME.RARE, CHIME.LEGEND];
    let h = s_sfx.reveal[tier];
    if (tier === 0 && q.gag >= 1 && q.gag <= MAX_GAGS && s_sfx.gag[q.gag] >= 0) h = s_sfx.gag[q.gag];
    playSfx(h, CH[tier]);
    link.led(LED.REVEAL, trgb, q.rgb);
    fx.sparklesBurst(s_art ? numArtX() : CX, s_art ? NUM_ART_MID : NUMBER_MID, 18 + tier * 14, trgb);
  }
}
function revealShown(q, t) { if (!q.locked && q.lockAt) return Math.max(1, Math.floor(q.o.number * easeOut(t / q.lockAt))); return q.o.number; }
function revealOverlay(t) {
  revealCommon(t);
  if (s_art) return;
  const q = s_seq, tier = clampTier(q.o.tier), trgb = s_w.tierRgb[tier];
  drawGlowNumber(revealShown(q, t), tier, q.locked ? trgb : 0xFFFFFF, t, 255 * easeOut(t / 300), s_w.uiPower);
  if (q.locked) { fx.burstRing(CX, NUMBER_MID, trgb, t - q.lockAt, 800); fx.title(s_w.tier[tier], CX, Y_TIER, trgb); }
}
function revealBand(t) {
  if (!s_art) return;
  const q = s_seq, tier = clampTier(q.o.tier), trgb = s_w.tierRgb[tier];
  drawGlowNumber(revealShown(q, t), tier, q.locked ? trgb : 0xFFFFFF, t, 255 * easeOut(t / 300), s_w.uiPower);
  if (q.locked) fx.burstRing(numArtX(), NUM_ART_MID, trgb, t - q.lockAt, 700);
}
function replayDraw(t) {
  const q = s_seq, tier = clampTier(q.o.tier), dim = fx.scaleRgb(s_w.tierRgb[tier], 150);
  drawGlowNumber(q.o.number, tier, dim, t, 150, s_w.uiTonight);
  if (s_art) { fx.checkMark(numArtX() - 105, NUM_ART_LABEL - 6, 34, 0x7CF29C); fx.subtitle(s_w.tier[tier], 150 + fx.SAFE_L, 372, dim); }
  else { fx.checkMark(CX + 170, NUMBER_MID, 56, 0x7CF29C); fx.title(s_w.tier[tier], CX, Y_TIER, dim); }
}
const replayOverlay = t => { if (!s_art) replayDraw(t); };
const replayBand = t => { if (s_art) replayDraw(t); };
function nocrystalOverlay(t) {
  if (s_art) return;
  const q = s_seq, [x0, y0] = anchorPoint(), f = easeOut(t / 2200);
  const x = x0 + (CX - x0) * f + 60 * Math.sin(t / 380) * (1 - f), y = y0 + (360 - y0) * f;
  fx.addGlow(x, y, 46, s_meter.rgb, 200); fx.addGlow(x, y, 16, 0xFFFFFF, 220);
  if (t < 2400) { if (!q.fxNext) q.fxNext = 1; let guard = 0; while (q.fxNext <= t && guard++ < 30) { fx.sparklesBurst(x, y, 2, s_meter.rgb); q.fxNext += 90; } }
}
function keeperOverlay(t) { if (!s_art) fx.title(s_w.rank[7], CX, Y_TITLE, s_w.tierRgb[3]); fx.crystalMeter(CX, Y_METER, 8, 8, s_seq.rgb, t, false); }
function milestoneOverlay(t) {
  const q = s_seq, nowMs = s_stepT0 + t;
  if (!q.fxNext) q.fxNext = 1;
  let guard = 0;
  while (q.fxNext <= t && guard++ < 20) { fx.sparklesBurst(80 + randBelow(640), 50 + randBelow(260), 9, S.crystalRgb(1 + randBelow(nColors()), nowMs)); q.fxNext += 260; }
  fx.bigNumber(counters.stationCrystals, CX, Y_NUMBER, s_w.tierRgb[3]);
  fx.subtitle(s_w.uiCounter, CX, Y_TIER, 0xFFFFFF);
}
function circleOverlay(t) {
  const nowMs = s_stepT0 + t, n = Math.max(3, s_group.length), a0 = t * (2 * Math.PI / 4200);
  for (let i = 0; i < n; i++) {
    const a = a0 + i * (2 * Math.PI / n), x = CX + 190 * Math.cos(a), y = CY + 84 * Math.sin(a);
    const id = i < s_group.length ? clampColor(s_group[i].colorIdx) : 1 + (i * 2) % nColors();
    const rgb = S.crystalRgb(id, nowMs), depth = 0.75 + 0.25 * Math.sin(a);
    fx.addGlow(x, y, 40 * depth, rgb, 140); fx.gem(x, y, 26 * depth, 40 * depth, rgb, true);
  }
}
function moonfullOverlay(t) {
  const f = easeOut(t / 1500), nowMs = s_stepT0 + t, mx = s_meter.x, my = s_meter.y, rgb = s_meter.rgb, glow = t < 2500 ? 255 : 200;
  if (s_meter.style === 'lantern') {
    const flare = t < 400 ? t / 400 : Math.max(0.35, 1 - (t - 400) / 2600);
    fx.addGlow(mx, my, s_meter.r * (1.4 + 1.6 * f), rgb, 60 + 150 * flare);
    fx.lanternMeter(mx, my, s_meter.r * (1 + 0.3 * f), moonPhaseNow(nowMs), glow, rgb, nowMs);
  } else fx.moonMeter(mx, my, s_meter.r * (1 + 0.22 * f), moonPhaseNow(nowMs), glow, rgb, nowMs);
  fx.burstRing(mx, my, rgb, t, 900);
  if (t >= 1500) fx.burstRing(mx, my, 0xFFFFFF, t - 1500, 900);
  const q = s_seq;
  if (!q.fxDone && t >= 200) { q.fxDone = true; fx.sparklesBurst(mx, my, 40, rgb); }
}

// ================================================================== flows
function beginSequence(t) { s_seq = newSeq(); s_seq.active = true; oracle.sequenceStarted(t); s_seq.rush = oracle.rush(t); s_buttonReq = s_tapReq = false; }
function endSequence() {
  const q = s_seq, t = now();
  let wr = '-';
  if (q.noWrites) wr = 'nowrite';
  else if (q.nWrites) { if (q.writeOk) wr = q.simulated ? 'sim' : 'ok'; else if (q.writePending) wr = q.removed ? 'removed' : 'timeout'; else wr = statusName(q.writeStatus); }
  const failed = (q.nWrites || q.noWrites) && !q.writeOk;
  if (q.simulated) {
    vlog.event(`simulated ${q.path || '?'}: kind=${token.kindName(q.c.kind)} number=${q.o ? q.o.number : 0} tier=${q.o ? q.o.tier : 0} rank=${q.o ? q.o.rank : 0} write=${wr}`);
    link.simulateLifted();
  } else {
    if (failed) counters.writeFails++;
    if (cfg.logVisits && q.path) vlog.visit(q.path, q.c, q.o, wr);
    S.saveCounters();
  }
  link.led(LED.IDLE, s_idleRgb);
  const armsGap = !(q.path === 'sleepy' || q.path === 'nocrystal');
  oracle.sequenceEnded(t, armsGap);
  q.active = false;
  s_lastCallMs = t; s_ledCycleMs = t; s_seqEndMs = t; s_seqEnded = true;
}
function decideAndWrite(fill) {
  const q = s_seq, t = now();
  q.circle = oracle.circleDue(t);
  q.o = oracle.decide(q.c, fill, t);
  q.o.flags |= token.SF.ORACLE;
  if (q.first) q.o.flags |= token.SF.FIRST;
  if (q.circle) q.o.flags |= token.SF.CIRCLE;
  if (fill >= 100) q.o.flags |= token.SF.CHARGED;
  q.decided = true;
  q.rankAfter = clampRank(q.o.rank);
  noteGroupColour(q.c.anonId, q.colorIdx, t);
  q.writes = token.buildVisitWrites(q.c, cfg.eventYear, cfg.baseYear, q.o.number, q.o.tier, q.o.flags, cfg.chapter, cfg.ndefUrl);
  q.nWrites = q.writes.length;
  if (!q.nWrites) { q.noWrites = true; vlog.event(`no tag write built (year ${cfg.eventYear}, base ${cfg.baseYear}, crystal ${q.c.anonId.toString(16)}) - shown anyway`); return; }
  issueWrite(t);
}
function recordVisit() {
  const q = s_seq;
  if (q.recorded || !q.decided) return;
  q.recorded = true;
  if (q.simulated) { q.milestone = false; return; }
  moonAddPoint();
  q.milestone = oracle.record(q.o, q.first) || q.o.milestone;
}
function pickGag() {
  let n = media.countVariants('voice', 'reveal_t0', 'wav');
  if (!n) n = Math.max(media.countVariants('clips', 'reveal_t0', 'omj'), media.countVariants('clips', 'reveal_t0', 'jpg'));
  if (!n) n = media.countVariants('sfx', 'reveal_t0', 'wav');
  if (!n) n = 3;
  return pickAvoiding(n, 'gag:t0');
}

// ---- single steps
async function stepAbsorb(showColour) {
  const q = s_seq;
  if (showColour) crystalLed(LED.SOLID, q.colorIdx);
  const st = s_step = newStep(Mode.Absorb);
  st.vis[0] = cue.ABSORB;
  st.caption = (showColour && token.benchColor(q.c)) ? colourName(q.colorIdx) : s_w.capAbsorb;
  setTiming(st, T.ABSORB); tintCrystal(st);
  st.energy = 200; st.sfx = s_sfx.absorb; st.chime = CHIME.ABSORB; st.overlay = absorbOverlay;
  await runStep();
}
async function stepAwaken() {
  const q = s_seq;
  crystalLed(LED.CHARGING, q.colorIdx);
  q.rankBefore = 1; q.rankAfter = clampRank(q.o.rank || 1); q.gemsLit = 0;
  q.capRank = `${s_w.rank[q.rankAfter]}!`;
  const st = s_step = newStep(Mode.Awaken);
  st.vis[0] = cue.AWAKEN; st.visVariant = clampColor(q.colorIdx); st.coherent = false;
  if (!q.rush) {
    addVoice(st, 'awaken_a', 0, s_w.capAwaken);
    const bench = token.benchColor(q.c);
    if (bench) addVoice(st, 'awaken_b', bench, s_w.colorName[bench]);
    addVoice(st, 'awaken_c', 0, q.capRank);
    st.caption = s_w.capAwaken;
  } else { addVoice(st, 'awaken_c', 0, q.capRank); st.caption = q.capRank; }
  setTiming(st, T.AWAKEN);
  q.gemAt = st.minMs * 3 / 10; q.gemAt2 = st.minMs * 5 / 10;
  tintCrystal(st); st.energy = 190; st.ambient = 10; st.sfx = s_sfx.awaken; st.chime = CHIME.REVEAL; st.overlay = awakenOverlay;
  await runStep();
}
async function stepGreet() {
  const q = s_seq;
  crystalLed(LED.SOLID, q.colorIdx);
  const v = q.c.visits, va = Math.min(v + 1, 255);
  q.rankBefore = clampRank(oracle.rankFor(v)); q.rankAfter = clampRank(oracle.rankFor(va));
  q.meterFrom = oracle.meterFor(v); q.meterTo = oracle.meterFor(va); q.gemLit = false;
  const st = s_step = newStep(Mode.Greet);
  st.vis[0] = `greet_r${q.rankAfter}`; st.vis[1] = cue.GREET;
  if (!q.rush) {
    let g = `rankup_r${q.rankAfter}`;
    if (q.rankAfter === q.rankBefore || !voiceExists(g)) { g = `greet_r${q.rankAfter}`; if (!voiceExists(g)) g = cue.GREET; }
    addVoice(st, g);
  }
  st.caption = s_w.rank[q.rankAfter]; st.titleDup = st.caption;
  setTiming(st, T.GREET);
  q.gemAt = st.minMs * 2 / 5;
  tintCrystal(st); st.energy = 150; st.ambient = 6; st.sfx = s_sfx.whoosh; st.chime = CHIME.TAP; st.overlay = greetOverlay;
  await runStep();
}
async function stepRecall() {
  const q = s_seq, lt = clampTier(q.c.lastTier);
  const st = s_step = newStep(Mode.Recall);
  st.vis[0] = `recall_t${lt}`; st.vis[1] = cue.RECALL; st.vis[2] = cue.GREET;
  addVoice(st, `recall_t${lt}`);
  q.recallLine = String(q.c.lastYear);
  st.caption = s_w.capRecall;
  setTiming(st, T.RECALL); tintCrystal(st); st.energy = 110; st.overlay = recallOverlay;
  await runStep();
}
async function stepCharge() {
  const q = s_seq;
  q.chargeFill = 0; q.chargeFullT = -1; q.chargeLedSent = 0;
  link.led(LED.METER, q.rgb, 0, 0, 0, 0);
  const st = s_step = newStep(Mode.Charge);
  st.vis[0] = cue.CHARGE; addVoice(st, cue.CHARGE); st.caption = s_w.capCharge;
  st.minMs = cfg.chargeMs; st.maxMs = Math.max(cfg.chargeMs, LINE_MS); st.loop = true;
  tintCrystal(st); st.energy = 140; st.ambient = 4; st.overlay = chargeOverlay; st.band = chargeBand; st.tick = chargeTick;
  await runStep();
}
const putBackTick = () => !s_seq.removed || s_seq.writeOk;
async function stepPutBack() {
  const q = s_seq;
  vlog.event(`crystal ${q.c.anonId.toString(16)} lifted before its write: asking for it back`);
  link.led(LED.SLEEPY, q.rgb);
  const st = s_step = newStep(Mode.Sleepy);
  st.vis[0] = cue.SLEEPY; addVoice(st, cue.SLEEPY); st.caption = s_w.uiPlace; st.fbTitle = s_w.uiPlace;
  st.minMs = st.maxMs = LINE_MS; tintCrystal(st); st.energy = 60; st.chime = CHIME.ERROR; st.band = hintsBand; st.tick = putBackTick; st.interruptible = true;
  await runStep();
  if (!q.removed || q.writeOk) { audio.voiceStop(); crystalLed(LED.CHARGING, q.colorIdx); }
}
async function stepHold() {
  const q = s_seq;
  if (q.nWrites && !q.writeOk && q.removed && !q.simulated && !s_hasNext && q.writeTries < 3) await stepPutBack();
  if (!q.writePending || q.removed) return;
  if (now() - q.writeSentMs >= cfg.writeTimeoutMs) return;
  crystalLed(LED.CHARGING, q.colorIdx);
  const st = s_step = newStep(Mode.Hold);
  st.freeze = true; st.caption = s_w.uiHold;
  st.minMs = st.maxMs = 2 * cfg.writeTimeoutMs + 500;
  tintCrystal(st); st.energy = 170; st.overlay = holdOverlay; st.tick = holdTick;
  await runStep();
}
async function stepReveal() {
  const q = s_seq, tier = clampTier(q.o.tier);
  const st = s_step = newStep(Mode.Reveal);
  q.gag = 0;
  if (tier === 0) { q.gag = pickGag(); st.visVariant = q.gag; }
  st.vis[0] = `reveal_t${tier}`; st.vis[1] = cue.REVEAL; st.coherent = false;
  const rt = tier === 3 ? T.REVEAL3 : T.REVEAL;
  if (q.rush) {
    setTiming(st, rt);
    q.bowlTier = tier; st.bowlTier = tier; st.bowlAt = 0;
    let g = `${cue.RUSH}_t${tier}`;
    if (!voiceExists(g)) g = `bowl_${cue.BOWL_KEY[tier]}`;
    addVoice(st, g);
  } else {
    st.minMs = rt[0] + T.BOWL[0]; st.maxMs = rt[1] + T.BOWL[1];
    addVoice(st, `reveal_t${tier}`, q.gag);
    addBowlPart(st, tier, rt[0] > 800 ? rt[0] - 800 : 0, rt[0]);
  }
  st.caption = s_w.capReveal[tier] || s_w.tier[tier]; st.titleDup = s_w.tier[tier];
  st.rgb = s_w.tierRgb[tier]; st.energy = 150 + tier * 35; st.ambient = tier >= 2 ? 18 : 4;
  q.locked = false; q.lockAt = q.rush ? 350 : 650;
  st.overlay = revealOverlay; st.band = revealBand;
  await runStep();
}
async function stepExtra(m, id, captionText, ov, led, sfx, chime) {
  if (led === LED.RAINBOW) link.led(LED.RAINBOW, 0); else if (led !== LED.OFF) crystalLed(led, s_seq.colorIdx);
  const st = s_step = newStep(m);
  st.vis[0] = id; addVoice(st, id); st.caption = captionText;
  setTiming(st, T.EXTRA); tintCrystal(st); st.energy = 210; st.ambient = 16; st.sfx = sfx; st.chime = chime; st.overlay = ov;
  if (s_seq.bowlTier >= 0) { st.bowlTier = s_seq.bowlTier; st.bowlMini = true; st.bowlAt = 0; }
  await runStep();
}
async function stepGoodbye() {
  const st = s_step = newStep(Mode.Goodbye);
  st.vis[0] = cue.GOODBYE; addVoice(st, cue.GOODBYE); st.caption = s_w.capGoodbye; st.fbTitle = s_w.capGoodbye;
  setTiming(st, T.GOODBYE); tintCrystal(st); st.energy = 90; st.ambient = 4;
  if (s_seq.bowlTier >= 0) { st.bowlTier = s_seq.bowlTier; st.bowlMini = true; st.bowlAt = 0; }
  await runStep();
}
async function stepReplay() {
  const q = s_seq, tier = clampTier(q.o.tier);
  link.led(LED.SOLID, fx.scaleRgb(s_w.tierRgb[tier], 150));
  const st = s_step = newStep(Mode.Replay);
  st.vis[0] = cue.REPLAY; addVoice(st, cue.REPLAY); st.caption = s_w.capReplay;
  setTiming(st, T.REPLAY);
  if (cfg.secondHelpings) { const partMin = st.minMs; st.minMs += timingMin(T.BOWL_NC); st.maxMs += timingMax(T.BOWL_NC); addBowlPart(st, 0, partMin > 600 ? partMin - 600 : 0, partMin); }
  tintCrystal(st); st.energy = 130; st.ambient = 6;
  st.sfx = tier >= 2 ? -1 : s_sfx.reveal[tier]; st.chime = CHIME.REVEAL; st.overlay = replayOverlay; st.band = replayBand;
  await runStep();
}
async function stepNoCrystal(foreign, viaButton) {
  const q = s_seq;
  crystalLed(LED.SOLID, q.colorIdx);
  const st = s_step = newStep(Mode.NoCrystal);
  if (foreign) { st.vis[0] = cue.FOREIGN; st.vis[1] = cue.NOCRYSTAL; addVoice(st, voiceExists(cue.FOREIGN) ? cue.FOREIGN : cue.NOCRYSTAL); st.caption = s_w.capForeign; }
  else { st.vis[0] = cue.NOCRYSTAL; addVoice(st, cue.NOCRYSTAL); st.caption = s_w.capNoCrystal; }
  st.fbTitle = st.caption;
  const partMin = timingMin(T.NOCRYSTAL);
  st.minMs = partMin + timingMin(T.BOWL_NC); st.maxMs = timingMax(T.NOCRYSTAL) + timingMax(T.BOWL_NC);
  addBowlPart(st, 0, partMin > 500 ? partMin - 500 : 0, partMin);
  tintCrystal(st); st.energy = 170; st.ambient = 14; st.sfx = viaButton ? s_sfx.button : s_sfx.whoosh; st.chime = CHIME.REVEAL; st.overlay = nocrystalOverlay;
  await runStep();
}
async function stepSleepy() {
  link.led(LED.SLEEPY, SLEEPY_WHITE);
  const st = s_step = newStep(Mode.Sleepy);
  st.vis[0] = cue.SLEEPY; addVoice(st, cue.SLEEPY); st.caption = s_w.capSleepy; st.fbTitle = s_w.capSleepy;
  setTiming(st, T.SLEEPY); tintCrystal(st); st.energy = 40; st.sfx = s_sfx.sleepy; st.chime = CHIME.ERROR; st.band = hintsBand; st.interruptible = true;
  await runStep();
}
async function revealChain() {
  const q = s_seq;
  await stepHold();
  recordVisit();
  await stepReveal();
  if (clampRank(q.o.rank) >= 7 && !q.rush && cfg.keeperOneIn && randBelow(cfg.keeperOneIn) === 0)
    await stepExtra(Mode.Extra, cue.KEEPER, s_w.capKeeper, keeperOverlay, LED.SOLID, -1, CHIME.TAP);
  if (q.milestone) await stepExtra(Mode.Extra, cue.MILESTONE, s_w.capMilestone, milestoneOverlay, LED.RAINBOW, s_sfx.milestone, CHIME.LEGEND);
  if (q.circle) await stepExtra(Mode.Extra, cue.CIRCLE, s_w.capCircle, circleOverlay, LED.RAINBOW, s_sfx.whoosh, CHIME.RARE);
  if (!q.rush && !s_hasNext) await stepGoodbye();
}

// ---- whole sequences
async function runCrystal() {
  const t = now();
  beginSequence(t);
  const q = s_seq;
  q.c = token.parse(s_cur, cfg.eventYear, cfg.baseYear);
  q.simulated = link.isSimUid(s_cur.uid, s_cur.uidLen);
  setSeqColour(token.displayColor(q.c), t);
  fx.sparklesClear();
  let kind = q.c.kind;
  if (kind === token.Kind.Fresh && !cfg.allowFresh) kind = token.Kind.Foreign;
  if (kind === token.Kind.Unreadable && s_cur.uidLen && s_cur.sak !== 0x00) kind = token.Kind.Foreign;
  console.log(`[show] crystal ${q.c.anonId.toString(16).padStart(8, '0')} kind=${token.kindName(q.c.kind)} visits=${q.c.visits} played=${q.c.playedThisYear} colour=${q.c.color} rush=${q.rush}${q.simulated ? ' (simulated)' : ''}`);
  if (kind === token.Kind.Unreadable) {
    q.path = 'sleepy'; if (!q.simulated) counters.sleepy++;
    await stepAbsorb(false); await stepSleepy();
  } else if (kind === token.Kind.Foreign) {
    q.path = 'foreign'; if (!q.simulated) { counters.foreign++; moonAddPoint(); }
    await stepAbsorb(false); await stepNoCrystal(true, false);
  } else if (q.c.playedThisYear) {
    q.path = 'replay'; if (!q.simulated) counters.replays++;
    q.o = oracle.replay(q.c, t); q.rankAfter = clampRank(q.o.rank);
    noteGroupColour(q.c.anonId, q.colorIdx, t);
    await stepAbsorb(true); await stepReplay();
  } else if (q.c.firstVisit()) {
    q.path = 'awaken'; q.first = true;
    decideAndWrite(0);
    await stepAbsorb(true); await stepAwaken(); await revealChain();
  } else {
    q.path = 'return';
    q.rankAfter = clampRank(oracle.rankFor(Math.min(q.c.visits + 1, 255)));
    const canCharge = !q.rush && cfg.chargeMs > 0 && (input.ok() || (cfg.hasButton && cfg.reader === S.READER.PLINTH && link.online()));
    if (!canCharge) decideAndWrite(0);
    await stepAbsorb(true); await stepGreet();
    if (!q.rush && !s_hasNext && q.c.lastTier >= 0 && voiceExists(`recall_t${clampTier(q.c.lastTier)}`)) await stepRecall();
    if (canCharge) { if (!s_hasNext && !q.removed) await stepCharge(); decideAndWrite(q.chargeFill); }
    await revealChain();
  }
  endSequence();
}
async function runNoCrystal(viaButton) {
  const t = now();
  beginSequence(t);
  const q = s_seq;
  q.path = 'nocrystal';
  q.c = token.parse(token.newImage(), cfg.eventYear, cfg.baseYear);
  setSeqColour(1 + randBelow(nColors()), t);
  counters.noCrystal++;
  if (viaButton || cfg.hasButton || !s_tapSparked || t - s_tapSparkMs >= cfg.tapSparkSec * 1000) {
    moonAddPoint();
    if (!viaButton && !cfg.hasButton) { s_tapSparked = true; s_tapSparkMs = t; }
  }
  fx.sparklesClear();
  await stepNoCrystal(false, viaButton);
  endSequence();
}
async function runChapter() {
  let played = 0;
  for (let i = 0; i < 6; i++) {
    const id = `${cue.CHAPTER}_${String.fromCharCode(97 + i)}`;
    const hasVoice = voiceExists(id), hasVis = media.findVisual(id).kind !== 'none';
    if (!hasVoice && !hasVis) continue;
    const st = s_step = newStep(Mode.Chapter);
    st.vis[0] = id; if (hasVoice) { st.vis[1] = cue.CHAPTER; addVoice(st, id); }
    st.caption = s_w.capChapterPart[i]; st.fbTitle = st.caption || null;
    st.minMs = (hasVoice && audio.ok()) ? 0 : CHAPTER_PART_MIN_MS; st.maxMs = CHAPTER_PART_MAX_MS; st.minFromClip = true;
    st.rgb = s_idleRgb; st.rainbow = true; st.energy = 90; st.ambient = 6; st.interruptible = true;
    played++;
    if (!(await runStep())) break;
  }
  if (!played && media.findVisual(cue.CHAPTER).kind !== 'none') {
    const st = s_step = newStep(Mode.Chapter);
    st.vis[0] = cue.CHAPTER; st.caption = s_w.capChapter; st.fbTitle = s_w.capChapter || null;
    st.minMs = 8000; st.maxMs = 120000; st.minFromClip = true; st.rgb = s_idleRgb; st.rainbow = true; st.energy = 90; st.ambient = 6; st.interruptible = true;
    await runStep();
  }
  s_loopsSinceChapter = 0; s_lastCallMs = now();
}
async function runMoonFull() {
  s_moonFinaleDue = false;
  counters.moonFull = true; S.saveCounters();
  vlog.event(`${s_meter.name} full: finale (points ${counters.moonPoints}, goal ${cfg.moonGoal})`);
  updateMoonTarget(now());
  link.led(LED.RAINBOW, 0);
  s_seq = newSeq();
  fx.sparklesClear();
  const st = s_step = newStep(Mode.MoonFull);
  st.vis[0] = cue.MOONFULL; st.coherent = false;
  addVoice(st, 'moonfull_a', 0, s_w.capMoonFull); addVoice(st, 'moonfull_b', 0, s_w.capMoonFull2);
  st.caption = s_w.capMoonFull; st.fbTitle = s_w.capMoonFull;
  st.minMs = (audio.ok() && voiceExists('moonfull_a')) ? 0 : MOONFULL_MIN_MS; st.maxMs = MOONFULL_MAX_MS; st.minFromClip = true;
  st.rgb = s_meter.rgb; st.rainbow = true; st.energy = 230; st.ambient = 20; st.sfx = s_sfx.moonfull; st.chime = CHIME.LEGEND; st.overlay = moonfullOverlay;
  await runStep();
  startMusic();
  link.led(LED.IDLE, s_idleRgb);
  s_lastCallMs = s_ledCycleMs = s_seqEndMs = now(); s_seqEnded = true;
}
async function maybeMoonFinale() { if (!s_moonFinaleDue && !moonFinalePending()) return false; await runMoonFull(); return true; }
async function runAttract() {
  const st = s_step = newStep(Mode.Attract);
  if (counters.moonFull) { st.vis[0] = cue.ATTRACT_FULL; st.vis[1] = cue.ATTRACT; } else st.vis[0] = cue.ATTRACT;
  st.caption = s_w.capAttract; st.fbTitle = s_w.title; st.minMs = st.maxMs = Infinity; st.loop = true;
  st.rgb = s_idleRgb; st.rainbow = true; st.energy = 70; st.ambient = 4; st.interruptible = true;
  st.overlay = attractOverlay; st.band = cfg.attractHints ? hintsBand : null; st.tick = attractTick;   // [station] attract_hints
  s_attractLoopsSeen = 0; s_munchLoop = -1;
  startMusic();
  await runStep();
}

// ---- setup helpers
function loadWords() {
  const w = s_w;
  w.title = S.text('title', 'The Crystal Oracle');
  const RANK_FALLBACK = ['', 'Spark', 'Glimmer', 'Glow', 'Radiant', 'Luminous', 'Starborn', 'Keeper'];
  w.rank = []; for (let r = 1; r <= 7; r++) w.rank[r] = S.text(`rank.${r}`, RANK_FALLBACK[r]); w.rank[0] = w.rank[1];
  const TIER_FALLBACK = ['TRICK!', 'TREAT!', 'RARE!', 'LEGENDARY!'];
  w.tier = []; w.tierRgb = [0x84CC16, 0x22C55E, 0xA855F7, 0xF59E0B]; w.bowlName = []; w.bowlRgb = [0x22C55E, 0x22C55E, 0xA855F7, 0xF59E0B]; w.bowlShape = []; w.capReveal = [];
  for (let t = 0; t < 4; t++) {
    w.tier[t] = S.text(`tier.${t}`, TIER_FALLBACK[t]); w.tierRgb[t] = S.color(`tier.${t}_color`, w.tierRgb[t]);
    const bk = cue.BOWL_KEY[t];
    w.bowlName[t] = S.text(`bowl.${bk}`, bk); w.bowlRgb[t] = S.color(`bowl.${bk}_color`, w.bowlRgb[t]);
    w.bowlShape[t] = fx.shapeFromName(S.text(`bowl.${bk}_shape`, t === 2 ? 'triangle' : t === 3 ? 'star' : 'circle'));
    w.capReveal[t] = S.text(`caption.reveal_t${t}`, '');
  }
  const COLOR_FALLBACK = ['', 'Violet', 'Ember', 'Frost', 'Moss', 'Rose', 'Moon'];
  w.colorName = []; for (let n = 1; n <= 6; n++) w.colorName[n] = S.text(`color.${n}`, COLOR_FALLBACK[n]); w.colorName[0] = w.colorName[1];
  w.capAttract = S.text('caption.attract', 'Place your crystal'); w.capAbsorb = S.text('caption.absorb', ''); w.capAwaken = S.text('caption.awaken', 'Awake!');
  w.capGreet = S.text('caption.greet', 'Welcome back!'); w.capRecall = S.text('caption.recall', 'LAST YEAR'); w.capCharge = S.text('caption.charge', 'Tap! Tap! Tap!');
  w.capReplay = S.text('caption.replay', 'Already glowing!'); w.capNoCrystal = S.text('caption.nocrystal', 'A blessing!'); w.capForeign = S.text('caption.foreign', w.capNoCrystal);
  w.capSleepy = S.text('caption.sleepy', 'Try again'); w.capGoodbye = S.text('caption.goodbye', 'See you next year!'); w.capMilestone = S.text('caption.milestone', 'Milestone!');
  w.capCircle = S.text('caption.circle', 'Circle complete!'); w.capKeeper = S.text('caption.keeper', 'Keeper!'); w.capChapter = S.text('caption.chapter', '');
  w.capMoonFull = S.text('caption.moonfull', 'WE DID IT!'); w.capMoonFull2 = S.text('caption.moonfull_b', w.capMoonFull);
  w.capChapterPart = []; for (let i = 0; i < 6; i++) w.capChapterPart[i] = S.text(`caption.chapter_${String.fromCharCode(97 + i)}`, w.capChapter);
  w.uiHold = S.text('ui.hold', 'Hold still...'); w.uiPower = S.text('ui.power', 'POWER'); w.uiCounter = S.text('ui.counter', 'crystals awakened');
  w.uiPress = S.text('ui.press', 'Press the button!'); w.uiAsleep = S.text('ui.reader_asleep', 'READER ASLEEP'); w.uiRush = S.text('ui.rush', 'RUSH');
  w.uiTouch = S.text('ui.touch', 'Touch me for a blessing!'); w.uiNoCard = S.text('ui.no_card', 'NO TF CARD'); w.uiOrPress = S.text('ui.or_press', 'OR PRESS');
  w.uiTonight = S.text('ui.tonight', 'TONIGHT'); w.uiPlace = S.text('ui.place', 'Crystal on the stone');
  w.glowIdle = S.color('glow.idle_color', 0xCFFFF2);
  s_idleRgb = steadyRgb(1);
  loadMeter(); loadVoiceSubs();
  if (!cfg.hasButton) vlog.event('no plinth button (config.ini button = no): taps bless, words nobutton.* in use');
}
const loadSfx = id => audio.sfxLoad(`/oracle/sfx/${id}.wav`);
function loadAllSfx() {
  s_sfx.absorb = loadSfx('absorb'); s_sfx.tap = loadSfx('tap');
  for (let t = 0; t < 4; t++) s_sfx.reveal[t] = loadSfx(cue.SFX_REVEAL[t]);
  s_sfx.awaken = loadSfx('awaken'); s_sfx.full = loadSfx('charge_full'); s_sfx.whoosh = loadSfx('whoosh'); s_sfx.button = loadSfx('button'); s_sfx.sleepy = loadSfx('sleepy');
  for (let n = 1; n <= MAX_GAGS; n++) { s_sfx.gag[n] = loadSfx(`reveal_t0_${n}`); if (s_sfx.gag[n] < 0) s_sfx.gag[n] = s_sfx.reveal[0]; }
  s_sfx.munch = loadSfx('munch'); s_sfx.milestone = loadSfx('milestone'); s_sfx.moonfull = loadSfx('moonfull');
}
function refreshMediaFacts() {
  let chapter = media.findVisual(cue.CHAPTER).kind !== 'none';
  for (let p = 0; p < 6 && !chapter; p++) { const id = `${cue.CHAPTER}_${String.fromCharCode(97 + p)}`; chapter = voiceExists(id) || media.findVisual(id).kind !== 'none'; }
  s_chapterAvail = chapter;
  s_musicFile = media.exists(MUSIC_PATH); s_musicFullFile = media.exists(MUSIC_FULL_PATH);
}
async function enterService() {
  vlog.event('service menu opened');
  link.led(LED.SOLID, 0xFFFFFF);
  audio.voiceStop();
  await service.run({ meterName: s_meter.name, words: s_w });
  link.ignoreUntilLifted();
  loadWords();                                  // settings may have changed (button mode, words)
  refreshMediaFacts(); loadAllSfx(); startMusic();
  audio.setVolume(cfg.volume);
  link.setIdleLight(s_idleRgb, cfg.ringBrightness); link.led(LED.IDLE, s_idleRgb);
  const t = now();
  s_lastCallMs = s_musicCheckMs = s_ledCycleMs = t;
  s_buttonReq = s_tapReq = s_serviceReq = false;
  updateMoonTarget(t);
  if (moonFinalePending()) s_moonFinaleDue = true;
  vlog.event(`service menu closed (${s_meter.name} ${counters.moonPoints}/${cfg.moonGoal}${counters.moonFull ? ' full' : ''})`);
}
async function dispatchPending() {
  if (s_hasNext) { s_cur = s_next; s_hasNext = false; await runCrystal(); await maybeMoonFinale(); return true; }
  if (s_buttonReq || s_tapReq) { const viaButton = s_buttonReq; s_buttonReq = s_tapReq = false; await runNoCrystal(viaButton); await maybeMoonFinale(); return true; }
  if (s_serviceReq) { s_serviceReq = false; await enterService(); await maybeMoonFinale(); return true; }
  if (s_chapterReq) { s_chapterReq = false; await runChapter(); return true; }
  return false;
}

// ================================================================== public API
export const show = {
  begin(canvas) {
    s_canvas = canvas; s_ctx = canvas.getContext('2d', { alpha: false });
    fx.bind(s_ctx);
    loadWords(); loadAllSfx(); refreshMediaFacts();
    moonSnap();
    if (moonFinalePending()) s_moonFinaleDue = true;
    startMusic();
    link.setIdleLight(s_idleRgb, cfg.ringBrightness); link.led(LED.IDLE, s_idleRgb);
    fx.sparklesClear();
    const t = now();
    s_lastCallMs = s_musicCheckMs = s_ledCycleMs = t;
    s_begun = true;
    vlog.event(`show ready: chapter=${s_chapterAvail} music=${s_musicFile}/${s_musicFullFile} sfx absorb=${s_sfx.absorb} reveal=${s_sfx.reveal.join('/')} meter=${counters.moonPoints}/${cfg.moonGoal}${counters.moonFull ? ' full' : ''}`);
  },
  // Runs the show until stop(): the attract loop, then whatever is waiting, forever.
  async start() {
    if (s_running) return;
    s_running = true;
    while (s_running) {
      try {
        if (await dispatchPending()) continue;
        if (await maybeMoonFinale()) continue;
        await runAttract();
        await dispatchPending();
      } catch (e) {
        console.error('[show] step failed', e);
        vlog.event(`show error: ${e && e.message}`);
        if (s_seq.active) { try { endSequence(); } catch {} }
        await new Promise(r => setTimeout(r, 500));
      }
    }
  },
  stop() { s_running = false; },
  requestService() { s_serviceReq = true; },
  // A quick sequence from outside (host menu / tests)
  simulate(img) { link.simulatePlaced(img); },
  state() { return { mode: s_mode, step: s_shown || s_step.vis[0], seq: s_seq.active ? s_seq.path : null, rush: s_seq.active ? s_seq.rush : oracle.rush(now()), hasNext: s_hasNext }; },
  // The boot screen: title + status lines (drawn before start()).
  bootScreen(lines) {
    if (!s_ctx) return;
    fx.fill(0);
    fx.addGlow(CX, 120, 260, 0x5B21B6, 110);
    fx.textCentered(S.text('title', 'The Crystal Oracle'), CX, 120, 42, 0xE9D5FF, 2);
    (lines || []).slice(0, 8).forEach((l, i) => fx.textCentered(l, CX, 196 + i * 34, 20, 0xFFFFFF, 1));
  },
};
