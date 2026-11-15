#!/usr/bin/env node
// fake_plinth.js - a pretend crystal plinth for testing the station web app without hardware.
//
// Speaks the plinth's Wi-Fi / WebSocket carrier (firmware/crystal_plinth/plinth_wifi.h): a
// WebSocket server on port 81 (or FAKE_PLINTH_PORT) carrying the crystal_link.h binary packets.
// Dependency-free (node's http + crypto). Type commands on its stdin to place / lift crystals:
//
//   node fake_plinth.js                 then in the app: plinth address  127.0.0.1:81
//
//   fresh            place a blank NTAG (never provisioned) - the show awakens it and writes it
//   fresh 2          ... whose bench colour is already 2 (provisioned, no visits)
//   ours             place a returning crystal (2 earlier visits, last year a treat)
//   played           place a crystal that already played THIS year (replay)
//   foreign          place someone's NFC sticker (never written)
//   bad              place a crystal the reader could not fully read (sleepy)
//   again            place the last crystal again (same UID, its pages as written)
//   lift             take the crystal off
//   button           press + release the (optional) big button
//   dump             print the current crystal's pages 3..39
//   fail <status>    make the next write fail with WriteStatus 1..5 (once)
//   refill           the pretend Hoard Hub: all 15 prize chests armed again
//   (FAKE_HUB_EMPTY=1 node fake_plinth.js starts the pretend hub with every chest open: awards answer "tier empty")
//   quit
//
// Hoard Hub relay (plinth 1.4, LT_CHEST 0x85 -> LT_CHEST_INFO 0x06): every LT_CHEST is logged as
// "chest: <ACTION> tier N ..." and answered by a pretend hub with 15 chests (ports 1-5 tier 1,
// 6-10 tier 2, 11-15 tier 3, all armed at start): an AWARD opens the lowest armed port of the tier
// (or answers "tier empty") after 1.2 s, OPEN_ALL opens everything, QUERY answers at once.
//
// Writes from the app are applied to the fake tag's pages and verified like the real plinth
// (UID check, pages 3..39 only, CC page skipped when already E1, result cached by reqId + hash).
'use strict';
const http = require('http');
const crypto = require('crypto');
const readline = require('readline');

const PORT = Number(process.env.FAKE_PLINTH_PORT || 81);
const GROUP = Number(process.env.FAKE_PLINTH_GROUP || 1);
const LINK_VERSION = 1;
const LT = { HELLO: 0x01, TAG_PLACED: 0x02, TAG_REMOVED: 0x03, WRITE_RESULT: 0x04, BUTTON: 0x05, CHEST_INFO: 0x06,
             HELLO_ACK: 0x81, WRITE_REQ: 0x82, LED: 0x83, PING: 0x84, CHEST: 0x85 };
const CHEST_ACTIONS = ['QUERY', 'AWARD', 'TEASE', 'IDLE', 'IDENTIFY', 'OPEN_ALL', 'OPEN_PORT'];
const CHEST_ALL = 0x7FFF;   // ports 1..15
const WS = { OK: 0, NO_TAG: 1, WRONG_TAG: 2, WRITE_FAIL: 3, VERIFY_FAIL: 4, BAD_REQUEST: 5 };
const FIRST_PAGE = 3, LAST_PAGE = 39, NUM_PAGES = 37, UID_MAX = 10, MAX_WRITES = 18;
const EVENT_YEAR = Number(process.env.FAKE_PLINTH_YEAR || new Date().getFullYear());
const BASE_YEAR = 2026;

// ---------------------------------------------------------------- the fake tag
let tag = null;            // { uid: Buffer, sak, atqaLo, pages: Buffer(37*4), readOk }
let lastTag = null;
let seq = 0;
let failNext = 0;
let standalone = true, lastHeard = 0;
const clients = new Set();
const wcache = [];
let stat = { placed: 0, writes: 0 };
// the pretend Hoard Hub behind the plinth
// FAKE_HUB_EMPTY=1 starts it with every chest open, so each AWARD answers "tier empty" (tests chest_empty)
const hub = { cmdId: 0, result: 0, port: 0, armed: process.env.FAKE_HUB_EMPTY ? 0 : CHEST_ALL,
              open: process.env.FAKE_HUB_EMPTY ? CHEST_ALL : 0, stuck: 0, heardAt: Date.now() };

function randUid() {
  const b = crypto.randomBytes(7); b[0] = 0x04; return b;   // NXP NTAG UIDs start with 04
}
function blankPages() {
  const p = Buffer.alloc(NUM_PAGES * 4, 0);
  // NTAG213 factory image: CC E1 10 12 00 + "03 00 FE 00" in page 4
  p.set([0xE1, 0x10, 0x12, 0x00], 0);
  p.set([0x03, 0x00, 0xFE, 0x00], (4 - FIRST_PAGE) * 4);
  return p;
}
function pageOf(p, n) { return p.subarray((n - FIRST_PAGE) * 4, (n - FIRST_PAGE) * 4 + 4); }
function ndefOurs(url) {
  const u = Buffer.from(url, 'latin1');
  const b = Buffer.alloc(48, 0);
  let i = 0;
  b[i++] = 0x03; b[i++] = u.length + 1 + 4; b[i++] = 0xD1; b[i++] = 0x01; b[i++] = u.length + 1; b[i++] = 0x55; b[i++] = 0x04;
  u.copy(b, i); i += u.length; b[i++] = 0xFE;
  return b;
}
function makeOurs({ visits = 2, colour = 1, lastTier = 1, playedThisYear = false, charge = 0 } = {}) {
  const p = blankPages();
  ndefOurs('halloween.ciancio.cloud').copy(p, (4 - FIRST_PAGE) * 4);
  pageOf(p, 17).set([charge, colour, 0, 0]);
  // history: `visits` prior years ending last year (tier of the most recent = lastTier)
  let y = EVENT_YEAR - 1;
  for (let i = 0; i < visits; i++, y--) {
    const slot = 18 + (y - BASE_YEAR);
    if (slot < 18 || slot > 29) break;   // 12 year slots (30..39 reserved)
    const tier = i === 0 ? lastTier : 1;
    const num = tier === 0 ? 20 : tier === 1 ? 55 : tier === 2 ? 95 : 100;
    pageOf(p, slot).set([num, tier, 0x80, 1]);
  }
  let v = visits;
  if (playedThisYear) { pageOf(p, 18 + (EVENT_YEAR - BASE_YEAR)).set([61, 1, 0x81, 1]); v++; }
  pageOf(p, 16).set([0x53, 0x4B, 0x03, v]);
  return p;
}

// ---------------------------------------------------------------- packets (little-endian packed structs)
function header(type) {
  const b = Buffer.alloc(8);
  b[0] = 0x43; b[1] = 0x4F; b[2] = LINK_VERSION; b[3] = type;
  b.writeUInt16LE(seq++ & 0xFFFF, 4); b.writeUInt16LE(GROUP, 6);
  return b;
}
function pktHello() {
  const b = Buffer.alloc(18);
  header(LT.HELLO).copy(b);
  b[8] = 1; b[9] = 0; b[10] = tag ? 1 : 0; b[11] = 2;          // readerOk, buttonDown, tagPresent, board (AITRIP)
  b.writeUInt16LE(130, 12); b.writeUInt32LE(Date.now() % 0xFFFFFFFF, 14);
  return b;
}
function pktPlaced(t) {
  const b = Buffer.alloc(172);
  header(LT.TAG_PLACED).copy(b);
  t.uid.copy(b, 8); b[18] = t.uid.length; b[19] = t.readOk ? 1 : 0; b[20] = t.sak; b[21] = t.atqaLo;
  (t.readOk ? t.pages : Buffer.alloc(NUM_PAGES * 4)).copy(b, 22);
  b.writeUInt16LE(60 + Math.floor(Math.random() * 40), 170);
  return b;
}
function pktRemoved(t) {
  const b = Buffer.alloc(19);
  header(LT.TAG_REMOVED).copy(b); t.uid.copy(b, 8); b[18] = t.uid.length;
  return b;
}
function pktWriteResult(reqId, status, failedPage, written) {
  const b = Buffer.alloc(13);
  header(LT.WRITE_RESULT).copy(b); b.writeUInt16LE(reqId, 8); b[10] = status; b[11] = failedPage; b[12] = written;
  return b;
}
function pktButton(pressed, heldMs) {
  const b = Buffer.alloc(12);
  header(LT.BUTTON).copy(b); b[8] = 0; b[9] = pressed ? 1 : 0; b.writeUInt16LE(heldMs, 10);
  return b;
}

function pktChestInfo() {
  const b = Buffer.alloc(20);
  header(LT.CHEST_INFO).copy(b);
  b.writeUInt16LE(hub.cmdId, 8); b[10] = hub.result; b[11] = hub.port;
  b.writeUInt16LE(hub.armed, 12); b.writeUInt16LE(hub.open, 14); b.writeUInt16LE(hub.stuck, 16);
  b.writeUInt16LE(Math.min(0xFFFE, Math.floor((Date.now() - hub.heardAt) / 1000)), 18);
  return b;
}
function chestRequest(buf) {
  if (buf.length < 17) { log(`chest: request too short (${buf.length} bytes)`); return; }
  const action = buf[8], tier = buf[9], port = buf[10], color = buf.readUInt32LE(11) & 0xFFFFFF, ms = buf.readUInt16LE(15);
  log(`chest: ${CHEST_ACTIONS[action] || `action ${action}`} tier ${tier} port ${port} colour #${color.toString(16).padStart(6, '0')} ${ms} ms`);
  if (action === 0) { broadcast(pktChestInfo()); return; }
  const id = hub.cmdId = (hub.cmdId + 1) & 0xFFFF || 1;
  const done = (result, p) => { hub.cmdId = id; hub.result = result; hub.port = p; hub.heardAt = Date.now(); broadcast(pktChestInfo()); };
  if (action === 1) {
    const lo = tier >= 1 && tier <= 3 ? (tier - 1) * 5 : 0, hi = tier >= 1 && tier <= 3 ? lo + 5 : 15;
    let p = 0;
    for (let i = lo; i < hi; i++) if (hub.armed & (1 << i)) { p = i + 1; break; }
    setTimeout(() => {
      if (!p) { log(`chest: hub says tier ${tier} EMPTY`); done(1, 0); return; }
      hub.armed &= ~(1 << (p - 1)); hub.open |= 1 << (p - 1);
      log(`chest: hub opened chest ${p} (tier ${tier})`); done(0, p);
    }, 1200);
  } else if (action === 5) {
    hub.armed = 0; hub.open = CHEST_ALL; hub.stuck = 0;
    setTimeout(() => done(0, 0), 500);
  } else {
    setTimeout(() => done(0, 0), 100);
  }
}

function broadcast(buf) { for (const c of clients) wsSend(c, 0x2, buf); }

// ---------------------------------------------------------------- the reader
function place(t) {
  if (tag) lift();
  tag = t; lastTag = t; stat.placed++;
  log(`tag: placed ${hex(t.uid)} (${t.readOk ? 'read OK' : 'READ FAILED'}) p16 ${hex(pageOf(t.pages, 16))}`);
  broadcast(pktPlaced(t));
}
function lift() {
  if (!tag) { log('tag: nothing on the reader'); return; }
  log(`tag: removed ${hex(tag.uid)}`);
  broadcast(pktRemoved(tag));
  tag = null;
}

function executeWrites(req) {
  const { uid, uidLen, count, writes } = req;
  if (count === 0 || count > MAX_WRITES || uidLen === 0 || uidLen > UID_MAX) return [WS.BAD_REQUEST, 0, 0];
  for (const w of writes) if (w.page < FIRST_PAGE || w.page > LAST_PAGE) return [WS.BAD_REQUEST, w.page, 0];
  if (!tag) return [WS.NO_TAG, 0, 0];
  if (!uid.subarray(0, uidLen).equals(tag.uid)) return [WS.WRONG_TAG, 0, 0];
  if (failNext) { const s = failNext; failNext = 0; return [s, writes[0].page, 0]; }
  let written = 0;
  for (const w of writes) {
    if (w.page === FIRST_PAGE && pageOf(tag.pages, 3)[0] === 0xE1) continue;   // CC is OTP: skipped
    w.data.copy(pageOf(tag.pages, w.page));
    written++;
  }
  return [WS.OK, 0, written];
}

function handlePacket(buf) {
  if (buf.length < 8 || buf[0] !== 0x43 || buf[1] !== 0x4F || buf[2] !== LINK_VERSION) return;
  if (buf.readUInt16LE(6) !== GROUP) return;
  const type = buf[3];
  if (!(type & 0x80)) return;
  const wasStandalone = standalone;
  standalone = false; lastHeard = Date.now();
  if (wasStandalone && tag) {            // back in touch: announce the crystal still sitting here
    log('link: browser back - re-announcing the crystal on the reader');
    setTimeout(() => { if (tag) broadcast(pktPlaced(tag)); }, 120);
  }
  switch (type) {
    case LT.HELLO_ACK: case LT.PING: break;
    case LT.LED: {
      if (buf.length < 22) break;
      const mode = buf[8], color = buf.readUInt32LE(12) & 0xFFFFFF;
      if (process.env.FAKE_PLINTH_VERBOSE) log(`led: mode ${mode} #${color.toString(16).padStart(6, '0')}`);
      break;
    }
    case LT.WRITE_REQ: {
      if (buf.length < 22) break;
      const req = { uid: buf.subarray(8, 18), uidLen: buf[18], reqId: buf.readUInt16LE(19), count: buf[21], writes: [] };
      for (let i = 0; i < Math.min(req.count, MAX_WRITES); i++) {
        const o = 22 + i * 5;
        if (o + 5 > buf.length) break;
        req.writes.push({ page: buf[o], data: Buffer.from(buf.subarray(o + 1, o + 5)) });
      }
      const hash = crypto.createHash('sha1').update(buf.subarray(8)).digest('hex');
      const cached = wcache.find(c => c.reqId === req.reqId && c.hash === hash);
      if (cached) { broadcast(pktWriteResult(req.reqId, ...cached.res)); log(`write #${req.reqId}: resend answered from cache`); break; }
      const res = executeWrites(req);
      stat.writes++;
      wcache.push({ reqId: req.reqId, hash, res }); if (wcache.length > 4) wcache.shift();
      broadcast(pktWriteResult(req.reqId, ...res));
      log(`write #${req.reqId}: ${req.count} page(s) -> ${['OK', 'NO_TAG', 'WRONG_TAG', 'WRITE_FAIL', 'VERIFY_FAIL', 'BAD_REQUEST'][res[0]]} (written ${res[2]})`);
      break;
    }
    case LT.CHEST: chestRequest(buf); break;
    default: log(`link: unknown packet type 0x${type.toString(16)}`);
  }
}

// ---------------------------------------------------------------- minimal WebSocket server
function wsSend(sock, opcode, payload) {
  if (sock.destroyed) return;
  const len = payload.length;
  let hdr;
  if (len < 126) hdr = Buffer.from([0x80 | opcode, len]);
  else { hdr = Buffer.alloc(4); hdr[0] = 0x80 | opcode; hdr[1] = 126; hdr.writeUInt16BE(len, 2); }
  sock.write(Buffer.concat([hdr, payload]));
}

const server = http.createServer((req, res) => {
  res.writeHead(200, { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*', 'Cache-Control': 'no-store' });
  res.end(JSON.stringify({ plinth: 'fake-plinth', fw: 130, board: 'node', readerOk: true, tagPresent: !!tag,
                           uid: tag ? hex(tag.uid) : '', clients: clients.size, standalone, uptimeMs: Math.round(process.uptime() * 1000),
                           placed: stat.placed, writes: stat.writes }));
});
server.on('upgrade', (req, sock) => {
  const key = req.headers['sec-websocket-key'];
  if ((req.headers.upgrade || '').toLowerCase() !== 'websocket' || !key) { sock.destroy(); return; }
  const accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  sock.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' +
             `Sec-WebSocket-Accept: ${accept}\r\n\r\n`);
  sock.setNoDelay(true);
  clients.add(sock);
  log(`ws: browser connected from ${sock.remoteAddress} (${clients.size} open)`);
  let buf = Buffer.alloc(0);
  sock.on('data', d => {
    buf = Buffer.concat([buf, d]);
    for (;;) {
      if (buf.length < 2) return;
      const fin = buf[0] & 0x80, op = buf[0] & 0x0F, masked = buf[1] & 0x80;
      let len = buf[1] & 0x7F, hdr = 2;
      if (len === 126) { if (buf.length < 4) return; len = buf.readUInt16BE(2); hdr = 4; }
      else if (len === 127) { sock.destroy(); return; }
      if (masked) hdr += 4;
      if (buf.length < hdr + len) return;
      let payload = Buffer.from(buf.subarray(hdr, hdr + len));
      if (masked) { const m = buf.subarray(hdr - 4, hdr); for (let i = 0; i < len; i++) payload[i] ^= m[i & 3]; }
      buf = buf.subarray(hdr + len);
      if (!fin) { sock.destroy(); return; }
      if (op === 0x2) handlePacket(payload);
      else if (op === 0x8) { sock.end(); return; }
      else if (op === 0x9) wsSend(sock, 0xA, payload);
    }
  });
  const gone = () => { if (clients.delete(sock)) log(`ws: browser gone (${clients.size} open)`); };
  sock.on('close', gone); sock.on('error', gone); sock.on('end', gone);
});
server.listen(PORT, '0.0.0.0', () => log(`fake plinth: ws://127.0.0.1:${PORT}/  (status: http://127.0.0.1:${PORT}/)  type "fresh", "ours", "lift", ...`));
server.on('error', e => { console.error(`cannot listen on ${PORT}: ${e.message} (set FAKE_PLINTH_PORT, or run as admin for 81)`); process.exit(1); });

setInterval(() => {
  if (clients.size) broadcast(pktHello());
  if (!standalone && Date.now() - lastHeard > 3000) { standalone = true; log('link: nothing from the browser for 3 s - standalone'); }
}, 1000);

// ---------------------------------------------------------------- console
function hex(b) { return [...b].map(x => x.toString(16).padStart(2, '0').toUpperCase()).join(':'); }
function log(s) { console.log(`${(process.uptime()).toFixed(3).padStart(9)}  ${s}`); }

const rl = readline.createInterface({ input: process.stdin });
rl.on('line', line => {
  const [cmd, arg] = line.trim().split(/\s+/);
  switch ((cmd || '').toLowerCase()) {
    case 'fresh': {
      const p = blankPages();
      if (arg) { ndefOurs('halloween.ciancio.cloud').copy(p, (4 - FIRST_PAGE) * 4); pageOf(p, 17).set([0, Number(arg), 0, 0]); pageOf(p, 16).set([0x53, 0x4B, 0x03, 0]); }
      place({ uid: randUid(), sak: 0, atqaLo: 0x44, pages: p, readOk: true }); break;
    }
    case 'ours':   place({ uid: randUid(), sak: 0, atqaLo: 0x44, pages: makeOurs({ visits: Number(arg) || 2, colour: 1 + Math.floor(Math.random() * 3) }), readOk: true }); break;
    case 'played': place({ uid: randUid(), sak: 0, atqaLo: 0x44, pages: makeOurs({ visits: 1, playedThisYear: true }), readOk: true }); break;
    case 'foreign': { const p = blankPages(); pageOf(p, 4).set([0x03, 0x0B, 0xD1, 0x01]); pageOf(p, 5).set([0x07, 0x55, 0x01, 0x65]); place({ uid: randUid(), sak: 0, atqaLo: 0x44, pages: p, readOk: true }); break; }
    case 'bad':    place({ uid: randUid(), sak: 0, atqaLo: 0x44, pages: blankPages(), readOk: false }); break;
    case 'again':  if (lastTag) place(lastTag); else log('no previous crystal'); break;
    case 'lift':   lift(); break;
    case 'button': broadcast(pktButton(true, 0)); setTimeout(() => broadcast(pktButton(false, 150)), 150); log('button: pressed'); break;
    case 'refill': hub.armed = CHEST_ALL; hub.open = 0; hub.stuck = 0; hub.heardAt = Date.now(); log('chest: all 15 chests armed again'); broadcast(pktChestInfo()); break;
    case 'fail':   failNext = Number(arg) || WS.WRITE_FAIL; log(`the next write fails with status ${failNext}`); break;
    case 'dump':   if (!tag) log('no tag'); else for (let p = FIRST_PAGE; p <= LAST_PAGE; p++) console.log(`   page ${String(p).padStart(2)}: ${hex(pageOf(tag.pages, p))}`); break;
    case 'quit': case 'exit': process.exit(0);
    case '': break;
    default: log('commands: fresh [colour] | ours [visits] | played | foreign | bad | again | lift | button | fail [1-5] | dump | refill | quit');
  }
});
