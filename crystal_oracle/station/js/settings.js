// settings.js - station settings (config.ini over the firmware defaults, settings.h), the words
// (captions.ini over story_defaults, with the no-button overrides), the crystal colours
// (color.N_hex: steady / 2-4 stop gradient / rainbow) and the persistent counters.
//
// Persistence (there is no TF card here): the host menu's setting changes go to localStorage
// as overrides on top of config.ini; the counters go to localStorage AND the station server's
// /api/state (so a cleared browser, or a second iPad, keeps tonight's numbers).
import { parseIni, parseBool, parseColor } from './ini.js';

// ---------------------------------------------------------------- settings (settings.h defaults)
export const RUSH = { AUTO: 0, ON: 1, OFF: 2 };
export const READER = { PLINTH: 0, RFID2: 1, NONE: 2 };

export const cfg = {
  eventYear: 2026, baseYear: 2026, chapter: 1, reader: READER.PLINTH, hasButton: false,
  group: 1, channel: 1, volume: 75, brightness: 85, ringBrightness: 120, rush: RUSH.AUTO,
  secondHelpings: false, allowFresh: true, logVisits: true, showCounter: true, ndefUrl: 'halloween.ciancio.cloud',
  trickPct: 15, rarePct: 18, legendPct: 4, loyaltyRarePct: 3, loyaltyCapPct: 9, chargeRarePct: 5, pityAfter: 2, firstNoTrick: true,
  rareStock: 0, legendStock: 0, expectedKids: 200,
  attractCallSec: 30, chapterEvery: 4, chargeMs: 2500, writeTimeoutMs: 1500, milestoneEvery: 100,
  rushGapSec: 8, rushQuietSec: 45, groupGapSec: 45, tapCooldownSec: 6, tapSparkSec: 20,
  moonGoal: 50, keeperOneIn: 3,
  // station-app only (not in the firmware): where the plinth is
  plinth: 'dragonglow-plinth.local:81',
  fit: 'fill',          // 'fill' = scale the 800x480 frame to fill the screen, cropping the sides (iPad); 'fit' = letterbox
  attractText: false,   // the attract loop's "place your crystal" / "tap" caption (off: the art fills the frame)
  attractHints: false,  // the attract loop's two crystal-and-arrow hints (put-back / sleepy keep theirs)
};

// config.ini key -> [field, type]  (types: int, bool, str, rush, reader)
const FIELDS = {
  'station.event_year': ['eventYear', 'int'], 'station.base_year': ['baseYear', 'int'], 'station.chapter': ['chapter', 'int'],
  'station.reader': ['reader', 'reader'], 'station.button': ['hasButton', 'bool'], 'station.group': ['group', 'int'],
  'station.channel': ['channel', 'int'], 'station.volume': ['volume', 'int'], 'station.brightness': ['brightness', 'int'],
  'station.ring_brightness': ['ringBrightness', 'int'], 'station.rush': ['rush', 'rush'],
  'station.second_helpings': ['secondHelpings', 'bool'], 'station.allow_fresh': ['allowFresh', 'bool'],
  'station.log_visits': ['logVisits', 'bool'], 'station.show_counter': ['showCounter', 'bool'], 'station.ndef_url': ['ndefUrl', 'str'],
  'station.plinth': ['plinth', 'str'], 'station.fit': ['fit', 'str'], 'station.attract_text': ['attractText', 'bool'], 'station.attract_hints': ['attractHints', 'bool'],
  'odds.trick_pct': ['trickPct', 'int'], 'odds.rare_pct': ['rarePct', 'int'], 'odds.legend_pct': ['legendPct', 'int'],
  'odds.loyalty_rare_pct': ['loyaltyRarePct', 'int'], 'odds.loyalty_cap_pct': ['loyaltyCapPct', 'int'],
  'odds.charge_rare_pct': ['chargeRarePct', 'int'], 'odds.pity_after': ['pityAfter', 'int'], 'odds.first_no_trick': ['firstNoTrick', 'bool'],
  'stock.rare': ['rareStock', 'int'], 'stock.legend': ['legendStock', 'int'], 'stock.expected_kids': ['expectedKids', 'int'],
  'timing.attract_call_sec': ['attractCallSec', 'int'], 'timing.chapter_every': ['chapterEvery', 'int'],
  'timing.charge_ms': ['chargeMs', 'int'], 'timing.write_timeout_ms': ['writeTimeoutMs', 'int'],
  'timing.milestone_every': ['milestoneEvery', 'int'], 'timing.rush_gap_sec': ['rushGapSec', 'int'],
  'timing.rush_quiet_sec': ['rushQuietSec', 'int'], 'timing.group_gap_sec': ['groupGapSec', 'int'],
  'timing.tap_cooldown_sec': ['tapCooldownSec', 'int'], 'timing.tap_spark_sec': ['tapSparkSec', 'int'],
  'story.moon_goal': ['moonGoal', 'int'], 'story.keeper_one_in': ['keeperOneIn', 'int'],
};
const OVERRIDES_KEY = 'station.cfgOverrides';
let s_overrides = {};

function applyKV(key, value) {
  const f = FIELDS[key];
  if (!f) return false;
  const [field, type] = f;
  const s = String(value).trim();
  switch (type) {
    case 'int': { const n = parseInt(s, 10); if (Number.isFinite(n)) cfg[field] = n; break; }
    case 'bool': { const b = parseBool(s); if (b !== undefined) cfg[field] = b; break; }
    case 'str': cfg[field] = s.replace(/^https?:\/\//i, ''); break;
    case 'rush': cfg[field] = /^on$/i.test(s) ? RUSH.ON : /^off$/i.test(s) ? RUSH.OFF : RUSH.AUTO; break;
    case 'reader': cfg[field] = /^rfid2$/i.test(s) ? READER.RFID2 : /^none$/i.test(s) ? READER.NONE : READER.PLINTH; break;
  }
  return true;
}

// Defaults + config.ini text (may be '') + the host menu's saved overrides.
export function loadConfig(iniText) {
  for (const [k, v] of parseIni(iniText)) applyKV(k, v);
  try { s_overrides = JSON.parse(localStorage.getItem(OVERRIDES_KEY) || '{}') || {}; } catch { s_overrides = {}; }
  for (const k of Object.keys(s_overrides)) applyKV(k, s_overrides[k]);
}

// The host menu changes a setting: applied now and remembered (config.ini stays untouched).
export function setConfig(key, value) {
  if (!applyKV(key, value)) return false;
  s_overrides[key] = String(value);
  try { localStorage.setItem(OVERRIDES_KEY, JSON.stringify(s_overrides)); } catch {}
  return true;
}
export function clearOverrides() { s_overrides = {}; try { localStorage.removeItem(OVERRIDES_KEY); } catch {} }
export function configOverrides() { return { ...s_overrides }; }

// ---------------------------------------------------------------- words (captions.ini + story defaults)
import { STORY_DEFAULTS } from './story_defaults.js';
let s_captions = new Map();

function lookupWord(key) {
  const k = key.toLowerCase();
  if (s_captions.has(k)) return s_captions.get(k);
  if (Object.prototype.hasOwnProperty.call(STORY_DEFAULTS, k)) return STORY_DEFAULTS[k];
  return null;
}
// No-button mode: "nobutton.<key>" wins when it exists, even when empty.
function noButtonWord(key) {
  if (cfg.hasButton || key.toLowerCase().startsWith('nobutton.')) return null;
  return lookupWord('nobutton.' + key);
}

export function loadWords(iniText) {
  s_captions = parseIni(iniText);
  parseCrystalColors();
}
export function text(key, fallback = '') {
  let v = noButtonWord(key);
  if (v === null) v = lookupWord(key);
  return v === null ? fallback : v;
}
export function color(key, fallbackRgb) {
  const nb = noButtonWord(key);
  let c = nb !== null ? parseColor(nb) : undefined;
  if (c !== undefined) return c;
  const k = key.toLowerCase();
  if (s_captions.has(k)) { c = parseColor(s_captions.get(k)); if (c !== undefined) return c; }
  if (Object.prototype.hasOwnProperty.call(STORY_DEFAULTS, k)) { c = parseColor(STORY_DEFAULTS[k]); if (c !== undefined) return c; }
  return fallbackRgb;
}
export function meterName() {
  const n = text('meter.name', '');
  if (n) return n;
  return /^\s*lantern/i.test(text('meter.style', '')) ? 'Lantern' : 'Moon';
}

// ---------------------------------------------------------------- crystal colours (color.N_hex)
export const CRYSTAL_COLORS_MAX = 6, CRYSTAL_STOPS_MAX = 4, CRYSTAL_CYCLE_MS = 4000;
const CRYSTAL_FALLBACK = [0x8B5CF6, 0x8B5CF6, 0xF97316, 0x38BDF8, 0x22C55E, 0xF472B6, 0xE9EEFF];
const RAINBOW_SAT = 0.70, RAINBOW_VAL = 1.00;
let s_colors = [];      // index 1..6: { n, stops[], rainbow }
let s_colorCount = 0;

function parseCrystalColors() {
  s_colors = [];
  s_colorCount = 0;
  let contiguous = true;
  for (let id = 1; id <= CRYSTAL_COLORS_MAX; id++) {
    const spec = text(`color.${id}_hex`, '').trim();
    const c = { n: 0, stops: [], rainbow: false };
    if (spec) {
      if (/^rainbow$/i.test(spec)) c.rainbow = true;
      else {
        for (const part of spec.split(',').slice(0, CRYSTAL_STOPS_MAX)) {
          const rgb = parseColor(part);
          if (rgb === undefined) { console.warn(`color.${id}_hex: "${part}" is not a colour`); c.stops.push(CRYSTAL_FALLBACK[id]); }
          else c.stops.push(rgb);
        }
        c.n = c.stops.length;
      }
      if (contiguous) s_colorCount = id;
    } else {
      contiguous = false;
      c.stops = [CRYSTAL_FALLBACK[id]]; c.n = 1;
    }
    s_colors[id] = c;
  }
}
function crystalAt(id) { return s_colors[id >= 1 && id <= CRYSTAL_COLORS_MAX ? id : 1] || { n: 1, stops: [CRYSTAL_FALLBACK[1]], rainbow: false }; }
export function colorCount() { return s_colorCount; }
export function hsvRgb(h, s, v) {
  h = ((h % 1) + 1) % 1 * 6;
  const i = Math.floor(h), f = h - i;
  const p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  const [r, g, b] = [[v, t, p], [q, v, p], [p, v, t], [p, q, v], [t, p, v], [v, p, q]][i % 6];
  return (Math.round(r * 255) << 16) | (Math.round(g * 255) << 8) | Math.round(b * 255);
}
export function blendRgb(a, b, f) {
  f = f < 0 ? 0 : f > 1 ? 1 : f;
  const r = Math.round(((a >> 16) & 255) + ((((b >> 16) & 255) - ((a >> 16) & 255)) * f));
  const g = Math.round(((a >> 8) & 255) + ((((b >> 8) & 255) - ((a >> 8) & 255)) * f));
  const bl = Math.round((a & 255) + (((b & 255) - (a & 255)) * f));
  return (r << 16) | (g << 8) | bl;
}
export function crystalRgb(id, tMs) {
  const c = crystalAt(id);
  const ph = ((tMs % CRYSTAL_CYCLE_MS) + CRYSTAL_CYCLE_MS) % CRYSTAL_CYCLE_MS;
  if (c.rainbow) return hsvRgb(ph / CRYSTAL_CYCLE_MS, RAINBOW_SAT, RAINBOW_VAL);
  if (c.n < 2) return c.n ? c.stops[0] : CRYSTAL_FALLBACK[1];
  const pos = ph * c.n;
  const i = Math.floor(pos / CRYSTAL_CYCLE_MS);
  const f = (pos % CRYSTAL_CYCLE_MS) / CRYSTAL_CYCLE_MS;
  const e = 0.5 - 0.5 * Math.cos(f * Math.PI);
  return blendRgb(c.stops[i % c.n], c.stops[(i + 1) % c.n], e);
}
export function crystalAnimated(id) { const c = crystalAt(id); return c.rainbow || c.n >= 2; }
export function crystalRainbow(id) { return crystalAt(id).rainbow; }
export function crystalStops(id, max = CRYSTAL_STOPS_MAX) { const c = crystalAt(id); return c.rainbow ? [] : c.stops.slice(0, max); }

// ---------------------------------------------------------------- counters (tonight + all-time)
export const counters = {
  stationCrystals: 0, stationAwakened: 0,
  visits: 0, awakened: 0, returning: 0, replays: 0, noCrystal: 0, sleepy: 0, foreign: 0,
  tier: [0, 0, 0, 0], rareGiven: 0, legendGiven: 0, writeFails: 0, moonPoints: 0, moonFull: false,
};
const STATE_KEY = 'station.state';
let s_cntYear = 0;
let s_saveTimer = 0;
let s_lastServerOk = null;

function freshTonight(year) {
  return { year, visits: 0, awakened: 0, returning: 0, replays: 0, noCrystal: 0, sleepy: 0, foreign: 0,
           tier: [0, 0, 0, 0], rareGiven: 0, legendGiven: 0, writeFails: 0, moonPoints: 0, moonFull: false };
}
function applyState(st, year) {
  const station = st && st.station || {};
  counters.stationCrystals = station.crystals | 0;
  counters.stationAwakened = station.awakened | 0;
  const t = (st && st.tonight && st.tonight.year === year) ? st.tonight : freshTonight(year);
  for (const k of ['visits', 'awakened', 'returning', 'replays', 'noCrystal', 'sleepy', 'foreign', 'rareGiven', 'legendGiven', 'writeFails', 'moonPoints'])
    counters[k] = t[k] | 0;
  counters.tier = [0, 1, 2, 3].map(i => (t.tier && t.tier[i]) | 0);
  counters.moonFull = !!t.moonFull;
}
function stateObj() {
  return {
    station: { crystals: counters.stationCrystals, awakened: counters.stationAwakened },
    tonight: { year: s_cntYear, visits: counters.visits, awakened: counters.awakened, returning: counters.returning,
               replays: counters.replays, noCrystal: counters.noCrystal, sleepy: counters.sleepy, foreign: counters.foreign,
               tier: counters.tier.slice(), rareGiven: counters.rareGiven, legendGiven: counters.legendGiven,
               writeFails: counters.writeFails, moonPoints: counters.moonPoints, moonFull: counters.moonFull },
    savedAt: new Date().toISOString(),
  };
}
// Loads the counters: the server's copy if it answers, else the browser's. The one with more
// all-time crystals wins when both exist (the server is the shared truth, the browser the backup).
export async function loadCounters(year) {
  s_cntYear = year;
  let local = null, remote = null;
  try { local = JSON.parse(localStorage.getItem(STATE_KEY) || 'null'); } catch {}
  try {
    const r = await fetch('/api/state', { cache: 'no-store' });
    if (r.ok) { remote = await r.json(); s_lastServerOk = true; }
  } catch { s_lastServerOk = false; }
  const pick = (remote && remote.station) ? ((local && local.station && (local.station.crystals | 0) > (remote.station.crystals | 0)) ? local : remote) : local;
  applyState(pick, year);
  return { local: !!local, remote: !!(remote && remote.station) };
}
export function saveCounters() {
  const st = stateObj();
  try { localStorage.setItem(STATE_KEY, JSON.stringify(st)); } catch {}
  clearTimeout(s_saveTimer);
  s_saveTimer = setTimeout(() => {
    fetch('/api/state', { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(st) })
      .then(r => { s_lastServerOk = r.ok; }).catch(() => { s_lastServerOk = false; });
  }, 300);
}
export function resetTonight() {
  applyState({ station: { crystals: counters.stationCrystals, awakened: counters.stationAwakened } }, s_cntYear);
  saveCounters();
}
export function stateServerOk() { return s_lastServerOk; }
