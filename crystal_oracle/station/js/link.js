// link.js - where crystals come from: the plinth over a WebSocket (firmware plinth_wifi.h), the
// same binary packets as the ESP-NOW link (crystal_link.h, LINK-PROTOCOL.md), plus simulated
// crystals for the host menu. A port of the screen's reader.cpp plinth backend:
//   * connects to ws://<addr>/ and reconnects by itself; sends HELLO_ACK on connect and a PING
//     every second (the plinth goes standalone after 3 s without one);
//   * online() = a packet heard in the last 3 s; LinkUp / LinkDown events on each change;
//   * write(): resent every 250 ms until the WRITE_RESULT with that reqId, WS_NO_TAG after 2 s;
//   * a lost REMOVED is repaired from HELLO.tagPresent (two heartbeats saying "no tag");
//   * ignoreUntilLifted(): the crystal on the reader when the host menu closed is not posted
//     again until it has been lifted.
// Events (poll()): { type: 'Placed'|'Removed'|'WriteDone'|'Button'|'LinkUp'|'LinkDown', tag, reqId,
//                    status, failedPage, button, pressed, heldMs }
import { TAG_UID_MAX, TAG_NUM_PAGES, LINK_MAX_WRITES, newImage, sameUid, uidHex } from './token.js';

export const LINK_VERSION = 1;
export const LT = { HELLO: 0x01, TAG_PLACED: 0x02, TAG_REMOVED: 0x03, WRITE_RESULT: 0x04, BUTTON: 0x05,
                    HELLO_ACK: 0x81, WRITE_REQ: 0x82, LED: 0x83, PING: 0x84 };
export const LED = { OFF: 0, IDLE: 1, FLARE: 2, CHARGING: 3, REVEAL: 4, SLEEPY: 5, RAINBOW: 6, SOLID: 7, METER: 8, DUAL: 9 };
export const WS = { OK: 0, NO_TAG: 1, WRONG_TAG: 2, WRITE_FAIL: 3, VERIFY_FAIL: 4, BAD_REQUEST: 5 };
export const statusName = s => ['ok', 'no_tag', 'wrong_tag', 'write_fail', 'verify_fail', 'bad_request'][s] || 'error';

const ONLINE_MS = 3000, PING_MS = 1000, WRITE_RESEND_MS = 250, WRITE_GIVEUP_MS = 2000, SCREEN_FW = 130;
const SIM_UID0 = 0xF5;

let s_group = 1;
let s_addr = '';
let s_sock = null, s_wantOpen = false, s_reconnectTimer = 0, s_backoffMs = 1000;
let s_seq = 0, s_reqId = 1;
let s_lastHeardMs = 0, s_wasOnline = false, s_pktIn = 0, s_pktOut = 0;
let s_tagPresent = false, s_curUid = null, s_curLen = 0;
let s_helloNoTag = 0, s_helloNoTagMs = 0;
let s_quietUid = null, s_quietLen = 0;
let s_idleMode = LED.IDLE, s_idleColor = 0x8B5CF6, s_idleBright = 120;
let s_plinth = { fw: 0, board: 0, readerOk: false, uptimeMs: 0 };
const s_events = [];
const s_writes = new Map();   // reqId -> { buf, firstMs, lastMs }
let s_lastLed = null;
// simulated crystal (host menu): a perfect reader
let s_sim = null;             // TagImage, present on the "reader"
let s_simPresent = false;
let s_state = 'idle';         // idle | connecting | open | closed
let s_sentAck = false;
let s_lastErr = '';

const now = () => performance.now();
export const nowMs = now;

function post(ev) { s_events.push(ev); }

// ---------------------------------------------------------------- codec (packed little-endian structs)
function header(view, type) {
  view.setUint8(0, 0x43); view.setUint8(1, 0x4F); view.setUint8(2, LINK_VERSION); view.setUint8(3, type);
  view.setUint16(4, (s_seq++) & 0xFFFF, true); view.setUint16(6, s_group, true);
}
function pktHelloAck(type) {
  const b = new ArrayBuffer(16), v = new DataView(b);
  header(v, type);
  v.setUint16(8, SCREEN_FW, true); v.setUint8(10, s_idleMode); v.setUint8(11, s_idleBright); v.setUint32(12, s_idleColor >>> 0, true);
  return b;
}
function pktLed(mode, color, color2, ms, speed, level) {
  const b = new ArrayBuffer(22), v = new DataView(b);
  header(v, LT.LED);
  v.setUint8(8, mode); v.setUint8(9, speed & 255); v.setUint8(10, level & 255); v.setUint8(11, 0);
  v.setUint32(12, (color & 0xFFFFFF) >>> 0, true); v.setUint32(16, (color2 & 0xFFFFFF) >>> 0, true); v.setUint16(20, ms & 0xFFFF, true);
  return b;
}
function pktWriteReq(reqId, uid, uidLen, writes) {
  const b = new ArrayBuffer(8 + 11 + 2 + 1 + LINK_MAX_WRITES * 5), v = new DataView(b), u8 = new Uint8Array(b);
  header(v, LT.WRITE_REQ);
  u8.set(uid.subarray(0, uidLen), 8); v.setUint8(18, uidLen); v.setUint16(19, reqId, true); v.setUint8(21, writes.length);
  writes.forEach((w, i) => { u8[22 + i * 5] = w.page; u8.set(w.data.subarray(0, 4), 23 + i * 5); });
  return b;
}
function parseTagPlaced(u8) {
  const img = newImage();
  img.uid.set(u8.subarray(8, 18)); img.uidLen = Math.min(u8[18], TAG_UID_MAX); img.readOk = u8[19] === 1; img.sak = u8[20];
  img.pages.set(u8.subarray(22, 22 + TAG_NUM_PAGES * 4));
  img.readMs = u8.length >= 172 ? (u8[170] | (u8[171] << 8)) : 0;
  return img;
}

function send(buf) {
  if (!s_sock || s_sock.readyState !== 1) return false;
  try { s_sock.send(buf); s_pktOut++; return true; } catch { return false; }
}

// ---------------------------------------------------------------- socket
function wsUrl(addr) {
  let a = String(addr || '').trim();
  if (!a) return '';
  if (/^wss?:\/\//i.test(a)) return a;
  a = a.replace(/^https?:\/\//i, '').replace(/\/.*$/, '');
  if (!/:\d+$/.test(a)) a += ':81';
  return `ws://${a}/`;
}
function scheduleReconnect() {
  clearTimeout(s_reconnectTimer);
  if (!s_wantOpen) return;
  s_reconnectTimer = setTimeout(openSocket, s_backoffMs);
  s_backoffMs = Math.min(5000, s_backoffMs + 1000);
}
function openSocket() {
  const url = wsUrl(s_addr);
  if (!url || !s_wantOpen) return;
  try { if (s_sock) { s_sock.onclose = null; s_sock.close(); } } catch {}
  s_state = 'connecting';
  s_sentAck = false;
  let sock;
  try { sock = new WebSocket(url); } catch (e) { s_lastErr = String(e.message || e); s_state = 'closed'; scheduleReconnect(); return; }
  sock.binaryType = 'arraybuffer';
  s_sock = sock;
  sock.onopen = () => {
    if (sock !== s_sock) return;
    s_state = 'open'; s_backoffMs = 1000; s_lastErr = '';
    send(pktHelloAck(LT.HELLO_ACK));
    s_sentAck = true;
    console.log(`[link] connected to ${url}`);
  };
  sock.onmessage = ev => { if (sock === s_sock && ev.data instanceof ArrayBuffer) onPacket(new Uint8Array(ev.data)); };
  sock.onerror = () => { if (sock === s_sock) s_lastErr = 'socket error'; };
  sock.onclose = () => {
    if (sock !== s_sock) return;
    s_state = 'closed';
    s_sock = null;
    scheduleReconnect();
  };
}

function onPacket(u8) {
  if (u8.length < 8 || u8[0] !== 0x43 || u8[1] !== 0x4F || u8[2] !== LINK_VERSION) return;
  const v = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
  if (v.getUint16(6, true) !== s_group) return;
  const type = u8[3];
  if (type & 0x80) return;                      // another screen's traffic
  s_pktIn++;
  const t = now();
  s_lastHeardMs = t;
  switch (type) {
    case LT.HELLO: {
      if (u8.length < 18) break;
      s_plinth = { readerOk: u8[8] === 1, buttonDown: u8[9] === 1, board: u8[11], fw: v.getUint16(12, true), uptimeMs: v.getUint32(14, true) };
      if (!s_sentAck) { send(pktHelloAck(LT.HELLO_ACK)); s_sentAck = true; }
      const tagPresent = u8[10] === 1;
      // presence resync: two consecutive heartbeats (>= 1 s apart) saying "no tag" while we think one is there
      if (!tagPresent && s_tagPresent) {
        if (s_helloNoTag === 0 || t - s_helloNoTagMs >= 900) { s_helloNoTag++; s_helloNoTagMs = t; }
        if (s_helloNoTag >= 2) { postRemoved(s_curUid, s_curLen, 'heartbeat'); }
      } else {
        s_helloNoTag = 0;
      }
      break;
    }
    case LT.TAG_PLACED: {
      if (u8.length < 170) break;
      const img = parseTagPlaced(u8);
      s_tagPresent = true; s_helloNoTag = 0;
      s_curUid = Uint8Array.from(img.uid); s_curLen = img.uidLen;
      if (s_quietUid && sameUid(img.uid, img.uidLen, s_quietUid, s_quietLen)) {
        console.log(`[link] re-announced ${uidHex(img.uid, img.uidLen)} kept quiet until lifted`);
        break;
      }
      post({ type: 'Placed', tag: img });
      break;
    }
    case LT.TAG_REMOVED: {
      if (u8.length < 19) break;
      const uid = Uint8Array.from(u8.subarray(8, 18)), len = Math.min(u8[18], TAG_UID_MAX);
      postRemoved(uid, len, 'removed');
      break;
    }
    case LT.WRITE_RESULT: {
      if (u8.length < 13) break;
      const reqId = v.getUint16(8, true), status = u8[10], failedPage = u8[11];
      if (!s_writes.has(reqId)) break;          // a duplicate answer to a resend already resolved
      s_writes.delete(reqId);
      post({ type: 'WriteDone', reqId, status, failedPage });
      break;
    }
    case LT.BUTTON: {
      if (u8.length < 12) break;
      post({ type: 'Button', button: u8[8], pressed: u8[9] === 1, heldMs: v.getUint16(10, true) });
      break;
    }
  }
}
function postRemoved(uid, len, why) {
  const img = newImage();
  if (uid) { img.uid.set(uid.subarray(0, TAG_UID_MAX)); img.uidLen = len; }
  s_tagPresent = false; s_helloNoTag = 0;
  if (s_quietUid && uid && sameUid(uid, len, s_quietUid, s_quietLen)) s_quietUid = null;
  post({ type: 'Removed', tag: img, why });
}

// ---------------------------------------------------------------- housekeeping (called every frame)
let s_lastPingMs = 0;
function service() {
  const t = now();
  const online = s_lastHeardMs > 0 && t - s_lastHeardMs < ONLINE_MS;
  if (online !== s_wasOnline) {
    s_wasOnline = online;
    post({ type: online ? 'LinkUp' : 'LinkDown' });
    if (online && s_lastLed) send(pktLed(...s_lastLed));
  }
  if (s_sock && s_sock.readyState === 1 && t - s_lastPingMs >= PING_MS) { s_lastPingMs = t; send(pktHelloAck(LT.PING)); }
  for (const [reqId, w] of s_writes) {
    if (t - w.firstMs >= WRITE_GIVEUP_MS) { s_writes.delete(reqId); post({ type: 'WriteDone', reqId, status: WS.NO_TAG, failedPage: 0 }); }
    else if (t - w.lastMs >= WRITE_RESEND_MS) { w.lastMs = t; send(w.buf); }
  }
}

// ---------------------------------------------------------------- public API
export const link = {
  begin(addr, group = 1) { s_group = group; this.connect(addr); },
  connect(addr) {
    s_addr = addr;
    s_wantOpen = !!wsUrl(addr);
    s_backoffMs = 1000;
    clearTimeout(s_reconnectTimer);
    openSocket();
  },
  disconnect() { s_wantOpen = false; clearTimeout(s_reconnectTimer); try { s_sock && s_sock.close(); } catch {} s_sock = null; s_state = 'idle'; },
  address() { return s_addr; },
  url() { return wsUrl(s_addr); },
  state() { return s_state; },
  lastError() { return s_lastErr; },
  plinth() { return s_plinth; },
  poll() { service(); return s_events.length ? s_events.shift() : null; },
  online() { return s_lastHeardMs > 0 && now() - s_lastHeardMs < ONLINE_MS; },
  tagPresent() { return s_tagPresent || s_simPresent; },
  lastHeardMs() { return s_lastHeardMs; },
  packetsIn() { return s_pktIn; },
  packetsOut() { return s_pktOut; },
  backendName() { return 'plinth'; },

  // Ordered page writes for the tag with this UID. Returns the request id; the result comes back
  // as a WriteDone event (resent every 250 ms, WS_NO_TAG after 2 s without an answer).
  write(uid, uidLen, writes) {
    const id = s_reqId++; if (s_reqId > 0xFFFF) s_reqId = 1;
    let ok = uid && uidLen > 0 && uidLen <= TAG_UID_MAX && writes && writes.length > 0 && writes.length <= LINK_MAX_WRITES;
    let badPage = 0;
    for (const w of writes || []) if (w.page < 3 || w.page > 39) { ok = false; badPage = w.page; }
    if (!ok) { post({ type: 'WriteDone', reqId: id, status: WS.BAD_REQUEST, failedPage: badPage }); return id; }
    if (s_sim && s_simPresent && sameUid(uid, uidLen, s_sim.uid, s_sim.uidLen)) {   // the simulated crystal: a perfect reader
      for (const w of writes) {
        if (w.page === 3 && s_sim.pages[0] === 0xE1) continue;
        s_sim.pages.set(w.data.subarray(0, 4), (w.page - 3) * 4);
      }
      setTimeout(() => post({ type: 'WriteDone', reqId: id, status: WS.OK, failedPage: 0 }), 150);
      return id;
    }
    const buf = pktWriteReq(id, uid, uidLen, writes);
    const t = now();
    s_writes.set(id, { buf, firstMs: t, lastMs: t });
    send(buf);
    return id;
  },
  led(mode, color = 0, color2 = 0, ms = 0, speed = 0, level = 0) {
    const args = [mode, color, color2, ms, speed, level];
    const steady = mode !== LED.FLARE && mode !== LED.REVEAL && ms === 0;
    if (steady) s_lastLed = args;
    send(pktLed(...args));
  },
  setIdleLight(color, brightness) {
    if (color === s_idleColor && brightness === s_idleBright) return;
    s_idleColor = color & 0xFFFFFF; s_idleBright = brightness & 255;
    send(pktHelloAck(LT.HELLO_ACK));
  },
  // The crystal on the reader has been dealt with (the host menu closed): its re-announcement is
  // not posted as Placed until it has been lifted.
  ignoreUntilLifted() {
    if (s_tagPresent && s_curUid) { s_quietUid = Uint8Array.from(s_curUid); s_quietLen = s_curLen; }
    else s_quietUid = null;
  },

  // ---- simulated crystals (host menu / testing) ----
  simulatePlaced(img) {
    img.uid[0] = SIM_UID0;
    s_sim = img; s_simPresent = true;
    post({ type: 'Placed', tag: img });
  },
  simulateLifted(postRemoved = false) {
    if (postRemoved && s_sim) { const img = newImage(); img.uid.set(s_sim.uid); img.uidLen = s_sim.uidLen; post({ type: 'Removed', tag: img, why: 'sim' }); }
    s_simPresent = false;
  },
  simImage() { return s_simPresent ? s_sim : null; },
  simulateButton() { post({ type: 'Button', button: 0, pressed: true, heldMs: 0 }); post({ type: 'Button', button: 0, pressed: false, heldMs: 120 }); },
  isSimUid(uid, len) { return len > 0 && uid[0] === SIM_UID0; },
  SIM_UID0,
};
