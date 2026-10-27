// halloween.ciancio.cloud — the Dragonglow storybook landing page the crystals link to.
// Plain static files from this folder, no auth, no dependencies.
//
// Each crystal links to its own page, /c/<id> (id = the 8-hex salted UID hash the
// station logs as `crystal`): crystal.html, which reads GET /api/crystal/<id>. That
// history comes from the station's visit logs copied into data/ (import_visits.js);
// data/ is never served as files. data/crystals.json (optional) holds anything the
// site adds per crystal later: { "<id>": { ... } }, returned as `extra` - so far the
// nickname a family gives it on its page (POST /api/crystal/<id>/nickname), and the
// sparks it gave the beacon this year.
//
// /play/ holds the games (test build, not linked from the book yet): the Hoard Beacon
// meter everyone fills (GET /api/beacon, POST /api/beacon/sparks), the crystal sky
// (GET /api/sky), Butterscotch's riddle and the goblin hunt (index.html?hunt=1).
//
// Normally this is NOT run on its own: ..\..\VMS-Checklist\server.js requires it and
// dispatches requests whose Host is halloween.ciancio.cloud to `handler` (same pattern
// as beacon). Run directly only for local testing:  node server.js  → http://127.0.0.1:8470/
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
const DATA_DIR = path.join(ROOT, 'data');
const ID_RE = /^[0-9a-f]{8}$/;

// ---- crystal history from the visit logs (data/visits_<year>[_<source>].csv)
// Rows: ms,crystal,path,kind,visits,number,tier,flags,rank,write. A row counts when the
// visit had an outcome and its tag write landed (write = ok); the first such row per
// crystal per year wins (later ones are replays). Re-read whenever a file changes.
let cache = { sig: '', crystals: new Map(), extra: {} };

function loadData() {
  let names = [];
  try { names = fs.readdirSync(DATA_DIR); } catch { return cache; }
  const files = names.filter(n => /^visits_\d{4}.*\.csv$/i.test(n) || n === 'crystals.json').sort();
  const sig = files.map(n => {
    try { const st = fs.statSync(path.join(DATA_DIR, n)); return `${n}:${st.size}:${st.mtimeMs}`; } catch { return n; }
  }).join('|');
  if (sig === cache.sig) return cache;
  const crystals = new Map();
  let extra = {};
  for (const n of files) {
    let text;
    try { text = fs.readFileSync(path.join(DATA_DIR, n), 'utf8'); } catch { continue; }
    if (n === 'crystals.json') {
      try { extra = JSON.parse(text) || {}; } catch { console.warn('[halloween] data/crystals.json is not valid JSON'); }
      continue;
    }
    const year = Number(n.match(/^visits_(\d{4})/i)[1]);
    for (const line of text.split(/\r?\n/)) {
      const f = line.split(',');
      if (f.length < 10 || f[0] === 'ms') continue;
      const [, id, pathName, , , number, tier, flags, rank, write] = f.map(s => s.trim());
      if (!ID_RE.test(id) || number === '' || write !== 'ok') continue;
      let c = crystals.get(id);
      if (!c) crystals.set(id, (c = new Map()));
      if (c.has(year)) continue;
      c.set(year, { year, number: Number(number), tier: Number(tier), flags: Number(flags) || 0, rank: Number(rank) || 0, path: pathName });
    }
  }
  cache = { sig, crystals, extra };
  return cache;
}

function crystalApi(id, res) {
  const { crystals, extra } = loadData();
  const years = [...(crystals.get(id) || new Map()).values()].sort((a, b) => a.year - b.year);
  const body = JSON.stringify({
    id,
    known: years.length > 0,
    visits: years.length,
    rank: years.length ? years[years.length - 1].rank : 0,
    years,
    nickname: (extra[id] && extra[id].nickname) || null,
    extra: extra[id] || null,
  });
  res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-cache' });
  res.end(body);
}
const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.webp': 'image/webp',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.txt': 'text/plain; charset=utf-8',
};

// ---- nicknames: POST /api/crystal/<id>/nickname {"nickname": "..."} ("" clears it)
// Anyone holding the crystal's link can name it (the link is the key). Stored in
// data/crystals.json as extra[id].nickname. Kept small and plain: 1-24 letters, digits,
// spaces and ' - . !, a per-IP write limit, and a cap on how many crystals can be named
// (stops the file growing from made-up ids). The owner can edit or blank any name there.
const NICK_MAX = 24;
const NICK_RE = /^[\p{L}\p{N}][\p{L}\p{N} '\-.!]*$/u;
const NICK_CAP = 3000;                       // named crystals, total
const NICK_RATE = { max: 12, windowMs: 60 * 60 * 1000 };   // writes per IP per hour
const nickHits = new Map();                  // ip -> [timestamps]

function cleanNickname(v) {
  if (typeof v !== 'string') return null;
  const s = v.normalize('NFC').replace(/\s+/g, ' ').trim();
  if (s === '') return '';
  if ([...s].length > NICK_MAX || !NICK_RE.test(s)) return null;
  return s;
}

function rateOk(ip) {
  const now = Date.now();
  const hits = (nickHits.get(ip) || []).filter(t => now - t < NICK_RATE.windowMs);
  if (hits.length >= NICK_RATE.max) { nickHits.set(ip, hits); return false; }
  hits.push(now);
  nickHits.set(ip, hits);
  if (nickHits.size > 5000) nickHits.clear();   // never grows without bound
  return true;
}

function sendJson(res, code, obj) {
  res.writeHead(code, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
  res.end(JSON.stringify(obj));
}

function nicknameApi(id, req, res) {
  const ip = String(req.headers['cf-connecting-ip'] || req.socket.remoteAddress || '?');
  let body = '';
  req.setEncoding('utf8');
  req.on('data', c => { body += c; if (body.length > 1024) { sendJson(res, 413, { error: 'too long' }); req.destroy(); } });
  req.on('end', () => {
    if (res.headersSent) return;
    let nick;
    try { nick = cleanNickname(JSON.parse(body).nickname); } catch { nick = null; }
    if (nick === null) { sendJson(res, 400, { error: `Use up to ${NICK_MAX} letters, numbers and spaces.` }); return; }
    if (!rateOk(ip)) { sendJson(res, 429, { error: 'Too many names for now. Try again later.' }); return; }
    const file = path.join(DATA_DIR, 'crystals.json');
    let all = {};
    try { all = JSON.parse(fs.readFileSync(file, 'utf8')) || {}; } catch (e) {
      if (e.code !== 'ENOENT') { sendJson(res, 500, { error: 'The dragon lost his notes. Try again later.' }); return; }
    }
    const entry = { ...(all[id] || {}) };
    if (nick) {
      if (!all[id] && Object.keys(all).length >= NICK_CAP) { sendJson(res, 503, { error: 'Naming is full for now.' }); return; }
      entry.nickname = nick;
      entry.nicknamedAt = new Date().toISOString();
    } else {
      delete entry.nickname;
      delete entry.nicknamedAt;
    }
    if (Object.keys(entry).length) all[id] = entry; else delete all[id];
    try {
      fs.mkdirSync(DATA_DIR, { recursive: true });
      const tmp = `${file}.${process.pid}.tmp`;
      fs.writeFileSync(tmp, JSON.stringify(all, null, 1));
      fs.renameSync(tmp, file);
    } catch {
      sendJson(res, 500, { error: 'The dragon lost his notes. Try again later.' }); return;
    }
    console.log(`[halloween] crystal ${id} nickname ${nick ? `set to "${nick}"` : 'cleared'}`);
    sendJson(res, 200, { id, nickname: nick || null });
  });
}

// ---- the Hoard Beacon (the /play/ games): one shared meter for the season.
// Every game round on the site adds the sparks it earned (POST /api/beacon/sparks
// {"n", "game", "crystal"?}); travelers at the Wakestone count too: the station's
// meter points (its state.json, same PC) are worth STATION_SPARKS each, but only
// as saved on Halloween itself, so test nights never light it. The highest number
// seen that night is kept, so the station's "Reset tonight" never dims the beacon.
// data/beacon.json: { year, sparks, plays, station, litAt }. A new year starts dark.
// The owner can edit or delete the file; it is re-read on every request.
const BEACON_GOAL = 2000;                      // sparks to light it (web + station)
const STATION_SPARKS = 10;                     // one traveler at the Wakestone
const SPARK_MAX_POST = 100;                    // one round can't add more than this
const SPARK_RATE = { max: 600, windowMs: 60 * 60 * 1000 };   // sparks per IP per hour
const GAMES = new Set(['beacon', 'riddle', 'hunt']);
const STATION_STATE = path.join(ROOT, '..', 'crystal_oracle', 'station', 'state', 'state.json');
const sparkHits = new Map();                   // ip -> [[ms, n], ...]

function seasonYear() {
  return Number(new Intl.DateTimeFormat('en-US', { timeZone: 'America/Chicago', year: 'numeric' }).format(new Date()));
}
function chicagoDate(d) {   // "2026-10-31"
  return new Intl.DateTimeFormat('en-CA', { timeZone: 'America/Chicago', year: 'numeric', month: '2-digit', day: '2-digit' }).format(d);
}

function readJson(file, fallback) {
  try { return JSON.parse(fs.readFileSync(file, 'utf8')) || fallback; } catch (e) { return e.code === 'ENOENT' ? fallback : null; }
}
function writeJson(file, obj) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const tmp = `${file}.${process.pid}.tmp`;
  fs.writeFileSync(tmp, JSON.stringify(obj, null, 1));
  fs.renameSync(tmp, file);
}

function loadBeacon() {
  const year = seasonYear();
  let b = readJson(path.join(DATA_DIR, 'beacon.json'), {});
  if (!b || b.year !== year) b = { year, sparks: 0, plays: 0, station: 0, litAt: null };
  // the station's travelers, counted only from its Halloween-night state
  const st = readJson(STATION_STATE, null);
  const t = st && st.tonight;
  if (t && t.year === year && st.savedAt && chicagoDate(new Date(st.savedAt)) === `${year}-10-31`) {
    b.station = Math.max(b.station | 0, t.moonPoints | 0);
  }
  return b;
}
function beaconView(b) {
  const total = (b.sparks | 0) + (b.station | 0) * STATION_SPARKS;
  return { year: b.year, sparks: total, goal: BEACON_GOAL, lit: total >= BEACON_GOAL, litAt: b.litAt || null,
           web: b.sparks | 0, travelers: b.station | 0, plays: b.plays | 0 };
}
function saveBeacon(b) {
  const v = beaconView(b);
  if (v.lit && !b.litAt) b.litAt = new Date().toISOString();
  writeJson(path.join(DATA_DIR, 'beacon.json'), b);
  return beaconView(b);
}

function sparkRoom(ip) {
  const now = Date.now();
  const hits = (sparkHits.get(ip) || []).filter(([t]) => now - t < SPARK_RATE.windowMs);
  if (sparkHits.size > 5000) sparkHits.clear();   // never grows without bound
  sparkHits.set(ip, hits);
  return Math.max(0, SPARK_RATE.max - hits.reduce((s, [, n]) => s + n, 0));
}

function readBody(req, res, max, done) {
  let body = '';
  req.setEncoding('utf8');
  req.on('data', c => { body += c; if (body.length > max) { sendJson(res, 413, { error: 'too long' }); req.destroy(); } });
  req.on('end', () => { if (!res.headersSent) done(body); });
}

function sparksApi(req, res) {
  const ip = String(req.headers['cf-connecting-ip'] || req.socket.remoteAddress || '?');
  readBody(req, res, 512, body => {
    let j;
    try { j = JSON.parse(body); } catch { j = null; }
    const n = j && Number.isInteger(j.n) ? j.n : NaN;
    const game = j && GAMES.has(j.game) ? j.game : null;
    const crystal = j && typeof j.crystal === 'string' && ID_RE.test(j.crystal.toLowerCase()) ? j.crystal.toLowerCase() : null;
    if (!(n >= 1 && n <= SPARK_MAX_POST) || !game) { sendJson(res, 400, { error: 'bad sparks' }); return; }
    const room = sparkRoom(ip);
    const add = Math.min(n, room);
    const b = loadBeacon();
    let view;
    try {
      if (add > 0) {
        sparkHits.get(ip).push([Date.now(), add]);
        b.sparks = (b.sparks | 0) + add;
        b.plays = (b.plays | 0) + 1;
        view = saveBeacon(b);
      } else view = beaconView(b);
    } catch { sendJson(res, 500, { error: 'The beacon flickered. Try again later.' }); return; }
    // credit the crystal that played (its page shows how many sparks it gave)
    let crystalSparks = null;
    if (crystal && add > 0) {
      try {
        const file = path.join(DATA_DIR, 'crystals.json');
        const all = readJson(file, {});
        if (all && (all[crystal] || Object.keys(all).length < NICK_CAP)) {
          const e = all[crystal] = { ...(all[crystal] || {}) };
          if (e.sparksYear !== b.year) { e.sparksYear = b.year; e.sparks = 0; }
          e.sparks = (e.sparks | 0) + add;
          crystalSparks = e.sparks;
          writeJson(file, all);
        }
      } catch { /* the beacon still got them */ }
    }
    sendJson(res, 200, { ...view, added: add, crystalSparks });
  });
}

// ---- the crystal sky (/play/sky.html): one star per crystal the site knows (from the
// visit logs, or named / played on the site). Crystal ids are the key to a crystal's
// page and nickname, so they are never listed: each star gets a scrambled seed for its
// place in the sky, and only the asker's own star (?c=<id>) is marked and named.
function skyApi(urlObj, res) {
  const { crystals, extra } = loadData();
  const mine = (urlObj.searchParams.get('c') || '').toLowerCase();
  const ids = new Set([...crystals.keys(), ...Object.keys(extra).filter(id => ID_RE.test(id))]);
  const stars = [];
  let you = null;
  for (const id of [...ids].sort()) {
    const years = [...(crystals.get(id) || new Map()).values()].sort((a, b) => a.year - b.year);
    const last = years[years.length - 1];
    const x = extra[id] || {};
    const s = { seed: fnv1a(`dragonglow/sky/v1:${id}`), visits: years.length, rank: last ? last.rank : 0,
                tier: last ? last.tier : -1, sparks: x.sparksYear === seasonYear() ? x.sparks | 0 : 0 };
    if (id === mine) { you = stars.length; s.nickname = x.nickname || null; }
    stars.push(s);
    if (stars.length >= NICK_CAP) break;
  }
  sendJson(res, 200, { stars, you });
}
function fnv1a(s) {
  let h = 0x811c9dc5;
  for (const ch of Buffer.from(s, 'utf8')) { h ^= ch; h = Math.imul(h, 0x01000193) >>> 0; }
  return h >>> 0;
}

function handler(req, res) {
  if (req.method === 'POST') {
    let p;
    try { p = new URL(req.url, 'http://x').pathname; } catch { p = ''; }
    const pm = p.match(/^\/api\/crystal\/([0-9a-fA-F]{8})\/nickname$/);
    if (pm) { nicknameApi(pm[1].toLowerCase(), req, res); return; }
    if (p === '/api/beacon/sparks') { sparksApi(req, res); return; }
  }
  if (req.method !== 'GET' && req.method !== 'HEAD') {
    res.writeHead(405, { Allow: 'GET, HEAD' }); res.end(); return;
  }
  let urlPath;
  try { urlPath = decodeURIComponent(new URL(req.url, 'http://x').pathname); }
  catch { res.writeHead(400); res.end(); return; }
  if (urlPath === '/health') { res.writeHead(200, { 'Content-Type': 'text/plain' }); res.end('ok'); return; }
  if (urlPath === '/api/beacon') {
    let v;
    try { v = beaconView(loadBeacon()); } catch { sendJson(res, 500, { error: 'The beacon flickered.' }); return; }
    sendJson(res, 200, v); return;
  }
  if (urlPath === '/api/sky') { skyApi(new URL(req.url, 'http://x'), res); return; }
  // a crystal's own page (what its tag links to) and its history
  let m = urlPath.match(/^\/api\/crystal\/([0-9a-fA-F]{8})$/);
  if (m) { crystalApi(m[1].toLowerCase(), res); return; }
  m = urlPath.match(/^\/c\/([0-9a-fA-F]{8})\/?$/);
  if (m) urlPath = '/crystal.html';
  else if (urlPath.startsWith('/c/')) { res.writeHead(302, { Location: '/' }); res.end(); return; }   // a mangled link: the book
  if (urlPath.endsWith('/')) urlPath += 'index.html';

  // Resolve inside ROOT only; never serve this file or dotfiles.
  const file = path.normalize(path.join(ROOT, urlPath));
  const rel = path.relative(ROOT, file);
  if (!rel || rel.startsWith('..') || path.isAbsolute(rel) || rel.split(path.sep).some(s => s.startsWith('.')) ||
      rel === 'server.js' || rel === 'import_visits.js' || rel.split(path.sep)[0].toLowerCase() === 'data') {
    res.writeHead(404); res.end('not found'); return;
  }
  const ext = path.extname(file).toLowerCase();
  const type = TYPES[ext];
  if (!type) { res.writeHead(404); res.end('not found'); return; }

  fs.stat(file, (err, st) => {
    if (err || !st.isFile()) { res.writeHead(404); res.end('not found'); return; }
    const etag = `"${st.size.toString(16)}-${Math.floor(st.mtimeMs).toString(16)}"`;
    const headers = {
      'Content-Type': type,
      'Content-Length': st.size,
      'ETag': etag,
      // HTML is what we edit; images/css are fingerprint-free, so keep their cache modest.
      'Cache-Control': ext === '.html' ? 'no-cache' : 'public, max-age=86400',
    };
    if (req.headers['if-none-match'] === etag) { res.writeHead(304, headers); res.end(); return; }
    res.writeHead(200, headers);
    if (req.method === 'HEAD') { res.end(); return; }
    fs.createReadStream(file).pipe(res);
  });
}

module.exports = { handler };

if (require.main === module) {
  const port = Number(process.env.HALLOWEEN_PORT || 8470);
  http.createServer(handler).listen(port, '127.0.0.1', () =>
    console.log(`Dragonglow storybook: http://127.0.0.1:${port}/`));
}
