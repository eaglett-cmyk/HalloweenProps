// service.js - the host menu (a DOM overlay; the firmware's service.cpp): Tonight (counters,
// reset, fill / reset the meter), Crystals (read test + prep: colour, provision, clear tonight,
// format, simulated blank), Reader (the plinth's address + link status), Sound & mode (volume,
// rush, button mode, year...), Simulate (crystals of every kind, a button press), Log.
// While it is open the show is paused and every reader event comes here; a crystal write in
// flight is finished before the menu closes.
import * as S from './settings.js';
import { cfg, counters } from './settings.js';
import * as token from './token.js';
import { link, WS, statusName } from './link.js';
import { audio } from './audio.js';
import { vlog } from './vlog.js';
import { media } from './media.js';

const $ = (sel, root = document) => root.querySelector(sel);
const el = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) { if (k === 'class') e.className = v; else if (k === 'html') e.innerHTML = v; else if (k.startsWith('on')) e.addEventListener(k.slice(2), v); else e.setAttribute(k, v); }
  for (const k of kids) if (k != null) e.append(k);
  return e;
};
const hex = (u, n) => token.uidHex(u, n);
const rgbCss = c => `#${(c & 0xFFFFFF).toString(16).padStart(6, '0')}`;

let s_root = null, s_resolve = null, s_page = 'tonight', s_timer = 0, s_opts = {};
let s_img = null, s_crystal = null, s_stale = false, s_readTest = { good: 0, placed: 0, lastMs: 0 }, s_sim = false;
let s_write = null;       // { chunks, i, reqId, sentMs, resolve, label }
let s_armed = {};         // two-tap confirmations
let s_msg = '';
let s_simQueue = [];      // simulated crystals to place once the menu closes
// Batch provisioning (Crystals tab): every crystal placed gets this colour - blank ones are
// provisioned, already-ours ones recoloured if `recolour` is on (else relinked if their link is
// older than their own /c/<id> page). `clearYear` on: an already-ours crystal that holds a visit for
// event_year has it wiped (test visits before the night). Counts for the session.
let s_batch = { on: false, colour: 1, recolour: false, clearYear: false, done: 0, skipped: 0, failed: 0, last: '' };

// ---------------------------------------------------------------- simulated crystal images
function blankImage() {
  const img = token.newImage();
  img.uidLen = 7; img.uid[0] = link.SIM_UID0; for (let i = 1; i < 7; i++) img.uid[i] = Math.floor(Math.random() * 256);
  img.readOk = true; img.sak = 0;
  token.page(img, 3).set([0xE1, 0x10, 0x12, 0x00]);
  token.page(img, 4).set([0x03, 0x00, 0xFE, 0x00]);
  img.readMs = 0;
  return img;
}
function oursImage({ visits = 2, colour = 1, lastTier = 1, playedThisYear = false, charge = 0 } = {}) {
  const img = blankImage();
  const ndef = token.buildNdef(token.crystalUrl(cfg.ndefUrl, token.parse(img, cfg.eventYear, cfg.baseYear).anonId));
  if (ndef) for (let p = 0; p < 12; p++) token.page(img, 4 + p).set(ndef.subarray(p * 4, p * 4 + 4));
  token.page(img, 17).set([charge, colour, 0, 0]);
  let y = cfg.eventYear - 1;
  for (let i = 0; i < visits; i++, y--) {
    const slot = 18 + (y - cfg.baseYear);
    if (slot < 18 || slot >= token.PAGE_RESERVED0) break;
    const tier = i === 0 ? lastTier : 1;
    token.page(img, slot).set([tier === 0 ? 20 : tier === 1 ? 55 : tier === 2 ? 95 : 100, tier, 0x80, 1]);
  }
  let v = visits;
  const thisSlot = 18 + (cfg.eventYear - cfg.baseYear);
  if (playedThisYear && thisSlot >= 18 && thisSlot <= 39) { token.page(img, thisSlot).set([61, 1, 0x81, 1]); v++; }
  token.page(img, 16).set([0x53, 0x4B, 0x03, v]);
  return img;
}
function foreignImage() { const img = blankImage(); token.page(img, 4).set([0x03, 0x0B, 0xD1, 0x01]); token.page(img, 5).set([0x07, 0x55, 0x01, 0x65]); return img; }
function badImage() { const img = blankImage(); img.readOk = false; return img; }
export const sim = { blankImage, oursImage, foreignImage, badImage };

// ---------------------------------------------------------------- crystal writes (prep)
function sendWrites(writes, label) {
  return new Promise(resolve => {
    const chunks = [];
    for (let i = 0; i < writes.length; i += token.LINK_MAX_WRITES) chunks.push(writes.slice(i, i + token.LINK_MAX_WRITES));
    s_write = { chunks, i: 0, reqId: 0, sentMs: 0, resolve, label, writes };
    sendNextChunk();
  });
}
function sendNextChunk() {
  const w = s_write;
  if (!w || !s_img) return;
  w.reqId = link.write(s_img.uid, s_img.uidLen, w.chunks[w.i]);
  w.sentMs = performance.now();
  s_msg = `${w.label}... (${w.i + 1}/${w.chunks.length})`;
  render();
}
function onWriteDone(ev) {
  const w = s_write;
  if (!w || ev.reqId !== w.reqId) return;
  if (ev.status !== WS.OK) {
    s_write = null; s_stale = true;
    s_msg = `${w.label} failed: ${statusName(ev.status)}${ev.failedPage ? ` (page ${ev.failedPage})` : ''} - lift it and place it again`;
    vlog.event(`crystals: ${s_msg}`);
    w.resolve(false); render(); return;
  }
  for (const pw of w.chunks[w.i]) { if (pw.page === 3 && token.page(s_img, 3)[0] === 0xE1) continue; token.page(s_img, pw.page).set(pw.data); }
  w.i++;
  if (w.i < w.chunks.length) { sendNextChunk(); return; }
  s_write = null;
  s_crystal = token.parse(s_img, cfg.eventYear, cfg.baseYear);
  s_msg = `${w.label}: saved`;
  vlog.event(`crystals: ${w.label} on ${hex(s_img.uid, s_img.uidLen)} - ${w.writes.length} page(s) saved`);
  w.resolve(true); render();
}
function prepReady() {
  if (s_write) { s_msg = 'a write is still running'; return false; }
  if (!s_img || !s_img.readOk) { s_msg = 'place a crystal on the plinth first'; return false; }
  if (s_stale) { s_msg = 'lift it and place it again first'; return false; }
  return true;
}
async function doPrep(label, build) {
  if (!prepReady()) { render(); return; }
  const { writes, why } = build();
  if (!writes.length) { s_msg = `${label}: ${why || 'nothing to write'}`; render(); return; }
  await sendWrites(writes, label);
}

// ---------------------------------------------------------------- batch provisioning
async function batchHandle(img) {
  if (s_write || link.isSimUid(img.uid, img.uidLen)) return;
  const id = hex(img.uid, img.uidLen);
  if (!img.readOk) { s_batch.last = `${id}: read failed - lift it and set it down again`; audio.chime(6, 50); render(); return; }
  const c = token.parse(img, cfg.eventYear, cfg.baseYear);
  let build, label;
  if (c.kind === token.Kind.Fresh) { label = 'Provision'; build = () => token.buildProvisionWrites(img, s_batch.colour, cfg.ndefUrl); }
  else if (c.kind === token.Kind.Ours && c.color !== s_batch.colour && s_batch.recolour) { label = 'Recolour'; build = () => token.buildColorWrite(img, s_batch.colour, cfg.ndefUrl); }
  else if (c.kind === token.Kind.Ours && s_batch.clearYear && (c.playedThisYear || c.visits > c.history.length)) { label = 'Clear'; build = () => token.buildClearYearWrites(img, cfg.eventYear, cfg.baseYear); }
  else if (c.kind === token.Kind.Ours && !c.hasOurLink) { label = 'Relink'; build = () => token.buildProvisionWrites(img, c.color, cfg.ndefUrl); }
  else {
    s_batch.skipped++;
    s_batch.last = c.kind === token.Kind.Ours ? `${id}: already provisioned (colour ${c.color || 'none'}${c.visits ? `, ${c.visits} visit(s)` : ''}) - skipped` : `${id}: ${token.kindName(c.kind)} - skipped`;
    audio.chime(5, 40); render(); return;
  }
  const { writes, why } = build();
  if (!writes.length) { s_batch.skipped++; s_batch.last = `${id}: ${why}`; audio.chime(5, 40); render(); return; }
  s_batch.last = `${id}: ${label.toLowerCase()}ing...`; render();
  const ok = await sendWrites(writes, label);
  if (ok) { s_batch.done++; s_batch.last = label === 'Clear' ? `${id}: this year's visit cleared - next crystal` : label === 'Relink' ? `${id}: link updated to its own page - next crystal` : `${id}: ${label.toLowerCase()}ed as ${S.text(`color.${s_batch.colour}`, s_batch.colour)} - next crystal`; audio.chime(1, 60); }
  else { s_batch.failed++; s_batch.last = `${id}: ${label} FAILED (${s_msg}) - set it down again`; audio.chime(6, 60); }
  render();
}

// ---------------------------------------------------------------- event pump (while the menu is open)
function pump() {
  for (let i = 0; i < 16; i++) {
    const ev = link.poll();
    if (!ev) break;
    switch (ev.type) {
      case 'Placed':
        s_img = ev.tag; s_stale = false; s_sim = link.isSimUid(ev.tag.uid, ev.tag.uidLen);
        s_crystal = token.parse(ev.tag, cfg.eventYear, cfg.baseYear);
        s_readTest.placed++; if (ev.tag.readOk) s_readTest.good++; s_readTest.lastMs = ev.tag.readMs;
        s_msg = '';
        if (s_page === 'crystals') render();
        if (s_batch.on) batchHandle(ev.tag);
        break;
      case 'Removed':
        if (s_img && token.sameUid(ev.tag.uid, ev.tag.uidLen, s_img.uid, s_img.uidLen)) { s_img = null; s_crystal = null; s_stale = false; s_sim = false; if (s_page === 'crystals') render(); }
        break;
      case 'WriteDone': onWriteDone(ev); break;
      default: break;   // Button / LinkUp / LinkDown are swallowed
    }
  }
  if (s_write && performance.now() - s_write.sentMs > 2500) onWriteDone({ reqId: s_write.reqId, status: WS.NO_TAG, failedPage: 0 });
  if (s_page === 'crystals' || s_page === 'reader' || s_page === 'log') renderLive();
}

// ---------------------------------------------------------------- rendering
function tabs() {
  const names = [['tonight', 'Tonight'], ['crystals', 'Crystals'], ['reader', 'Reader'], ['sound', 'Sound & mode'], ['sim', 'Simulate'], ['log', 'Log']];
  return el('nav', { class: 'tabs' }, ...names.map(([id, label]) => el('button', { class: id === s_page ? 'tab on' : 'tab', onclick: () => { s_page = id; s_armed = {}; render(); } }, label)),
    el('button', { class: 'tab close', onclick: close }, 'Close'));
}
function kv(rows) { return el('table', { class: 'kv' }, ...rows.map(([k, v]) => el('tr', {}, el('th', {}, k), el('td', {}, String(v))))); }
function armedButton(key, label, danger, action) {
  const armed = !!s_armed[key];
  return el('button', { class: armed ? 'btn danger' : 'btn' + (danger ? ' warn' : ''), onclick: () => {
    if (!armed) { s_armed = { [key]: true }; setTimeout(() => { if (s_armed[key]) { s_armed = {}; render(); } }, 3000); render(); return; }
    s_armed = {}; action(); render();
  } }, armed ? 'Tap again to confirm' : label);
}

function pageTonight() {
  const name = s_opts.meterName || 'Meter';
  return el('div', { class: 'page' },
    el('h2', {}, `Tonight ${cfg.eventYear}`),
    kv([['Crystal visits that rolled', counters.visits], ['Awakened (first visits)', counters.awakened], ['Returning', counters.returning],
        ['Already played (replays)', counters.replays], ['Blessings (no crystal)', counters.noCrystal], ['Unreadable (sleepy)', counters.sleepy], ['Foreign tags', counters.foreign],
        ['Tricks / treats / rare / legendary', `${counters.tier[0]} / ${counters.tier[1]} / ${counters.tier[2]} / ${counters.tier[3]}`],
        ['Rare / legendary given', `${counters.rareGiven} / ${counters.legendGiven}`], ['Write failures', counters.writeFails],
        [`${name} points`, `${counters.moonPoints}${cfg.webBeacon ? ` + ${counters.webPoints} from the website` : ''} / ${cfg.moonGoal}${counters.moonFull ? ' (finale played)' : ''}`],
        ['All-time crystals / awakened here', `${counters.stationCrystals} / ${counters.stationAwakened}`],
        ['Counters saved on the server', S.stateServerOk() === null ? 'unknown' : S.stateServerOk() ? 'yes' : 'NO (browser only)']]),
    el('div', { class: 'row' },
      armedButton('reset', 'Reset tonight', true, () => { S.resetTonight(); vlog.event('tonight reset from the host menu'); }),
      el('button', { class: 'btn', onclick: () => { counters.moonPoints = Math.max(counters.moonPoints, cfg.moonGoal); S.saveCounters(); vlog.event(`${name} filled from the host menu`); render(); } }, `Fill the ${name}`),
      armedButton('meter', `Reset ${name}`, true, () => { counters.moonPoints = 0; counters.webPoints = 0; counters.moonFull = false; S.saveCounters(); vlog.event(`${name} reset from the host menu`); })),
    el('p', { class: 'hint' }, `"Fill the ${name}" plays the finale as soon as the menu closes. Counters also live in state/state.json on the station server.`));
}

function pageCrystals() {
  const c = s_crystal;
  const live = el('div', { id: 'crystal-live' });
  const colourBtns = [];
  const n = S.colorCount();
  for (let i = 0; i <= n; i++) {
    const cur = c && c.color === i;
    const b = el('button', { class: 'btn colour' + (cur ? ' on' : ''), onclick: () => doPrep(`Colour ${i}`, () => token.buildColorWrite(s_img, i, cfg.ndefUrl)) },
      i ? el('span', { class: 'swatch', 'data-colour': i }) : null, i ? S.text(`color.${i}`, `Colour ${i}`) : 'None');
    colourBtns.push(b);
  }
  return el('div', { class: 'page' },
    el('h2', {}, 'Crystals - read test and prep'),
    live,
    el('div', { class: 'row' }, el('span', { class: 'label' }, 'Colour:'), ...colourBtns),
    el('div', { class: 'row' },
      el('button', { class: 'btn accent', onclick: () => doPrep('Provision', () => token.buildProvisionWrites(s_img, s_crystal && s_crystal.color || 0, cfg.ndefUrl)) }, 'Provision'),
      el('button', { class: 'btn', onclick: () => doPrep('Clear tonight', () => token.buildClearYearWrites(s_img, cfg.eventYear, cfg.baseYear)) }, 'Clear tonight'),
      armedButton('format', 'Format', true, () => doPrep('Format', () => token.buildFormatWrites(s_img))),
      el('button', { class: 'btn', onclick: () => { if (link.simImage()) link.simulateLifted(true); else link.simulatePlaced(blankImage()); } }, link.simImage() ? 'Lift sim' : 'Sim blank')),
    el('div', { class: 'batch' + (s_batch.on ? ' on' : '') },
      el('h3', {}, s_batch.on ? `BATCH ON - ${S.text(`color.${s_batch.colour}`, s_batch.colour)}: set the next crystal on the plinth` : 'Batch provisioning (a box of new crystals)'),
      el('div', { class: 'row' },
        el('span', { class: 'label' }, 'Colour:'),
        ...Array.from({ length: n }, (_, k) => k + 1).map(i => el('button', { class: 'btn colour' + (s_batch.colour === i ? ' on' : ''), onclick: () => { s_batch.colour = i; render(); } },
          el('span', { class: 'swatch', 'data-colour': i }), S.text(`color.${i}`, `Colour ${i}`))),
        el('label', { class: 'check' }, el('input', { type: 'checkbox', ...(s_batch.recolour ? { checked: '' } : {}), onchange: ev => { s_batch.recolour = ev.target.checked; } }), ' also recolour crystals already provisioned'),
        el('label', { class: 'check' }, el('input', { type: 'checkbox', ...(s_batch.clearYear ? { checked: '' } : {}), onchange: ev => { s_batch.clearYear = ev.target.checked; } }), ` also clear ${cfg.eventYear} visits (test runs)`),
        el('button', { class: 'btn ' + (s_batch.on ? 'danger' : 'accent'), onclick: () => { s_batch.on = !s_batch.on; if (s_batch.on) { s_batch.done = s_batch.skipped = s_batch.failed = 0; s_batch.last = 'waiting for the first crystal'; if (s_img) batchHandle(s_img); } render(); } }, s_batch.on ? 'Stop batch' : 'Start batch')),
      el('p', { class: 'msg' }, s_batch.on || s_batch.done || s_batch.skipped ? `${s_batch.done} provisioned, ${s_batch.skipped} skipped, ${s_batch.failed} failed. ${s_batch.last}` : 'Pick the colour, start, then place crystals one after another. Blank ones are provisioned with the link and that colour (a chime per crystal); crystals that already carry a history are left alone unless the recolour box is ticked.')),
    el('p', { class: 'hint' }, 'Place a crystal on the plinth: every placement counts as a read test (good reads / placements, last read time). Provision writes the link + colour to a blank crystal; Format wipes any tag back to blank (the only write ever made to a foreign tag).'));
}
function renderCrystalsLive() {
  const box = $('#crystal-live', s_root);
  if (!box) return;
  const c = s_crystal, img = s_img;
  const rows = [];
  if (!img) rows.push(['Crystal', 'none on the reader' + (link.online() ? '' : ' (plinth offline)')]);
  else {
    const kind = !img.readOk ? 'unreadable' : c.kind === token.Kind.Fresh ? 'Fresh (blank)' : c.kind === token.Kind.Ours ? 'ours' : c.kind === token.Kind.Foreign ? 'FOREIGN' : 'unreadable';
    rows.push(['Crystal', `${kind}${s_sim ? ' (simulated)' : ''}${s_stale ? ' - STALE, lift it and place it again' : ''}`]);
    rows.push(['UID / SAK', `${hex(img.uid, img.uidLen)} / ${img.sak.toString(16).padStart(2, '0')}`]);
    if (c && c.kind === token.Kind.Ours) {
      rows.push(['Visits / charge', `${c.visits} / ${c.charge}`]);
      rows.push(['Colour', c.color ? `${c.color} ${S.text(`color.${c.color}`, '')}${token.benchColor(c) ? '' : ' (not defined in the words)'}` : 'none']);
      rows.push(['Years on it', c.history.map(h => `${h.year}: ${h.number} t${h.tier}`).join(', ') || 'none']);
      rows.push([`This year (${cfg.eventYear})`, c.playedThisYear ? `played: ${c.thisYear.number} ${S.text(`tier.${c.thisYear.tier}`, '')}` : 'not yet']);
    }
    if (c) rows.push(['Link / CC', `${c.hasOurLink ? 'its own' : (c.kind === token.Kind.Foreign ? 'not ours' : c.kind === token.Kind.Ours ? 'old - Provision updates it' : 'none')} / ${c.hasCC ? 'yes' : 'no'}`]);
  }
  rows.push(['Read test', `${s_readTest.good} good of ${s_readTest.placed} placement(s), last read ${s_readTest.lastMs ? s_readTest.lastMs + ' ms' : 'n/a'}`]);
  box.replaceChildren(kv(rows), el('p', { class: 'msg' }, s_msg));
  for (const sw of box.parentElement.querySelectorAll('.swatch')) sw.style.background = rgbCss(S.crystalRgb(Number(sw.dataset.colour), performance.now()));
}

function pageReader() {
  const addr = el('input', { type: 'text', value: cfg.plinth, placeholder: 'dragonglow-plinth.local:81 or 192.168.1.50:81', autocapitalize: 'off', autocorrect: 'off', spellcheck: 'false' });
  return el('div', { class: 'page' },
    el('h2', {}, 'Reader - the plinth link'),
    el('div', { class: 'row' }, el('span', { class: 'label' }, 'Plinth address:'), addr,
      el('button', { class: 'btn accent', onclick: () => { S.setConfig('station.plinth', addr.value.trim()); link.connect(cfg.plinth); s_msg = `connecting to ${link.url()}`; render(); } }, 'Connect')),
    el('div', { id: 'reader-live' }),
    el('p', { class: 'hint' }, 'The plinth must be on the same Wi-Fi, flashed with PLINTH_LINK_WIFI 1. Its serial console prints the IP; http://<plinth>:81/ shows its status in any browser. For a test without hardware run station/fake_plinth.js and use 127.0.0.1:81 (or the PC\'s IP from the iPad).'));
}
function renderReaderLive() {
  const box = $('#reader-live', s_root);
  if (!box) return;
  const p = link.plinth();
  const heard = link.lastHeardMs() ? `${Math.round(performance.now() - link.lastHeardMs())} ms ago` : 'never';
  box.replaceChildren(kv([
    ['Socket', `${link.state()} ${link.url()}${link.lastError() ? ' - ' + link.lastError() : ''}`],
    ['Plinth', link.online() ? 'ONLINE' : 'offline'], ['Last packet', heard], ['Packets in / out', `${link.packetsIn()} / ${link.packetsOut()}`],
    ['Plinth firmware / board', `${p.fw ? `${Math.floor(p.fw / 100)}.${Math.floor(p.fw / 10) % 10}.${p.fw % 10}` : '-'} / ${['other', 'Feather ESP32-S3', 'ESP32 dev (AITRIP)'][p.board] || '-'}`],
    ['PN532 / crystal on it', `${p.readerOk ? 'ok' : (p.fw ? 'MISSING' : '-')} / ${link.tagPresent() ? 'yes' : 'no'}`],
    ['Assets', media.sdOk() ? `${media.assetCount()} files, ${(media.bytesLoaded() / 1048576).toFixed(0)} MB loaded` : 'NOT FOUND (run tools/build_assets.py; the server serves ../sd_card/oracle)'],
  ]), el('p', { class: 'msg' }, s_msg));
}

function pageSound() {
  const vol = el('input', { type: 'range', min: 0, max: 100, value: cfg.volume, oninput: ev => { S.setConfig('station.volume', ev.target.value); audio.setVolume(cfg.volume); $('#vol-val', s_root).textContent = cfg.volume; } });
  const sel = (key, value, options) => el('select', { onchange: ev => { if (key === '__audio') audio.forceMedia(ev.target.value === 'auto' ? null : ev.target.value === 'media'); else S.setConfig(key, ev.target.value); render(); } }, ...options.map(([v, l]) => el('option', { value: v, ...(String(v) === String(value) ? { selected: '' } : {}) }, l)));
  const num = (key, value, min, max) => el('input', { type: 'number', min, max, value, onchange: ev => S.setConfig(key, ev.target.value) });
  return el('div', { class: 'page' },
    el('h2', {}, 'Sound & mode'),
    el('div', { class: 'row' }, el('span', { class: 'label' }, 'Volume:'), vol, el('span', { id: 'vol-val' }, cfg.volume),
      el('button', { class: 'btn', onclick: () => { audio.resume().then(() => { audio.chime(7, 60); render(); }); } }, 'Test tone')),
    el('div', { class: 'row' }, el('span', { class: 'label' }, `Sound engine: ${audio.engine()} (Web Audio ${audio.state()}, build ${audio.builds()}; keeper ${audio.unmuteState()})`),
      el('button', { class: 'btn', onclick: () => audio.resume().then(render) }, 'Resume Web Audio'),
      el('span', { class: 'label' }, 'Engine on next start:'),
      sel('__audio', audio.mediaForced() === null ? 'auto' : audio.mediaForced() ? 'media' : 'webaudio', [['auto', 'automatic'], ['media', 'media elements (iPad fallback)'], ['webaudio', 'Web Audio']])),
    el('div', { class: 'row' }, el('span', { class: 'label' }, 'Rush:'), sel('station.rush', ['auto', 'on', 'off'][cfg.rush], [['auto', 'auto'], ['on', 'on'], ['off', 'off']]),
      el('span', { class: 'label' }, 'Plinth button:'), sel('station.button', cfg.hasButton ? 'yes' : 'no', [['no', 'no (taps bless)'], ['yes', 'yes']])),
    el('div', { class: 'row' }, el('span', { class: 'label' }, 'Second helpings:'), sel('station.second_helpings', cfg.secondHelpings ? 'yes' : 'no', [['no', 'no'], ['yes', 'yes']]),
      el('span', { class: 'label' }, 'Awaken blank tags:'), sel('station.allow_fresh', cfg.allowFresh ? 'yes' : 'no', [['yes', 'yes'], ['no', 'no']]),
      el('span', { class: 'label' }, 'Show counter:'), sel('station.show_counter', cfg.showCounter ? 'yes' : 'no', [['yes', 'yes'], ['no', 'no']])),
    el('div', { class: 'row' }, el('span', { class: 'label' }, 'Event year:'), num('station.event_year', cfg.eventYear, 2024, 2045),
      el('span', { class: 'label' }, 'Chapter every N loops:'), num('timing.chapter_every', cfg.chapterEvery, 0, 50),
      el('span', { class: 'label' }, `${s_opts.meterName || 'Meter'} goal:`), num('story.moon_goal', cfg.moonGoal, 1, 9999)),
    el('div', { class: 'row' }, el('span', { class: 'label' }, 'Odds % trick / rare / legendary:'),
      num('odds.trick_pct', cfg.trickPct, 0, 100), num('odds.rare_pct', cfg.rarePct, 0, 100), num('odds.legend_pct', cfg.legendPct, 0, 100),
      el('span', { class: 'label' }, 'Stock rare / legendary (0 = unlimited):'), num('stock.rare', cfg.rareStock, 0, 9999), num('stock.legend', cfg.legendStock, 0, 9999)),
    el('div', { class: 'row' }, el('button', { class: 'btn warn', onclick: () => { S.clearOverrides(); s_msg = 'overrides cleared - reload the page to go back to config.ini'; render(); } }, 'Forget my changes (back to config.ini)')),
    el('p', { class: 'msg' }, s_msg),
    el('p', { class: 'hint' }, 'Changes apply when the menu closes and are remembered in this browser (config.ini itself is never edited). Year changes need a reload.'));
}

function pageSim() {
  const q = (label, make) => el('button', { class: 'btn', onclick: () => { s_simQueue.push(make()); s_msg = `${label} queued - it plays when the menu closes`; render(); } }, label);
  return el('div', { class: 'page' },
    el('h2', {}, 'Simulate (no tag write, nothing counted; a blessing counts)'),
    el('div', { class: 'row' }, q('Fresh crystal', () => blankImage()), q('Fresh, colour 1', () => oursImage({ visits: 0, colour: 1 })), q('Fresh, colour 2', () => oursImage({ visits: 0, colour: 2 })), q('Fresh, colour 3', () => oursImage({ visits: 0, colour: 3 }))),
    el('div', { class: 'row' }, q('Returning (2 visits, last a treat)', () => oursImage({ visits: 2, colour: 1 + Math.floor(Math.random() * Math.max(1, S.colorCount())), lastTier: 1 })),
      q('Returning (4 visits, last rare)', () => oursImage({ visits: 4, colour: 2, lastTier: 2 })), q('Returning (8 visits: Keeper)', () => oursImage({ visits: 8, colour: 3, lastTier: 1 })),
      q('Pity (2 treats in a row)', () => oursImage({ visits: 2, colour: 1, lastTier: 1 }))),
    el('div', { class: 'row' }, q('Already played tonight', () => oursImage({ visits: 1, playedThisYear: true })), q('Foreign sticker', () => foreignImage()), q('Unreadable', () => badImage()),
      el('button', { class: 'btn', onclick: () => { s_simQueue.push('button'); s_msg = 'button press queued'; render(); } }, 'Plinth button press')),
    el('p', { class: 'msg' }, s_msg),
    el('p', { class: 'hint' }, 'Queued crystals are placed one after another when you close the menu. On a keyboard: m = menu, f = fresh, r = returning, p = played, b = button.'));
}

function pageLog() {
  return el('div', { class: 'page' }, el('h2', {}, `Log (${vlog.pending()} line(s) waiting for the server)`), el('pre', { id: 'log-live', class: 'log' }, vlog.recent().slice(-40).join('\n')));
}
function renderLive() {
  if (s_page === 'crystals') renderCrystalsLive();
  else if (s_page === 'reader') renderReaderLive();
  else if (s_page === 'log') { const p = $('#log-live', s_root); if (p) p.textContent = vlog.recent().slice(-40).join('\n'); }
}
function render() {
  if (!s_root) return;
  const pages = { tonight: pageTonight, crystals: pageCrystals, reader: pageReader, sound: pageSound, sim: pageSim, log: pageLog };
  s_root.replaceChildren(tabs(), pages[s_page]());
  renderLive();
}

async function close() {
  if (s_write) { s_msg = 'finishing the crystal write first...'; render(); await new Promise(r => { const t = setInterval(() => { if (!s_write) { clearInterval(t); r(); } }, 100); }); }
  clearInterval(s_timer); s_timer = 0;
  s_root.classList.remove('open');
  const q = s_simQueue; s_simQueue = [];
  const resolve = s_resolve; s_resolve = null;
  if (resolve) resolve();
  // simulated crystals go to the show one at a time, after it is running again
  let delay = 300;
  for (const item of q) {
    setTimeout(() => { if (item === 'button') link.simulateButton(); else link.simulatePlaced(item); }, delay);
    delay += 400;
  }
}

export const service = {
  mount() {
    if (s_root) return;
    s_root = el('div', { id: 'service' });
    document.body.append(s_root);
  },
  isOpen() { return !!s_resolve; },
  // Opens the menu; resolves when the host closes it.
  run(opts = {}) {
    this.mount();
    s_opts = opts; s_msg = ''; s_armed = {}; s_page = 'tonight';
    s_readTest = { good: 0, placed: 0, lastMs: 0 };
    s_img = link.simImage(); s_crystal = s_img ? token.parse(s_img, cfg.eventYear, cfg.baseYear) : null; s_sim = !!s_img;
    s_root.classList.add('open');
    render();
    clearInterval(s_timer);
    s_timer = setInterval(pump, 100);
    return new Promise(resolve => { s_resolve = resolve; });
  },
  queueSim(img) { s_simQueue.push(img); },
};
