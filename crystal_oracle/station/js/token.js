// token.js - the crystal's on-tag data (schema 0x03), a port of firmware token.h / token.cpp.
//   page  3      CC  E1 10 12 00   (written only if byte 0 != 0xE1 - the CC is OTP)
//   pages 4..15  NDEF URI record (phone link) + 0xFE: <ndefUrl>/c/<8-hex anonId> (crystalUrl)
//   page  16     'S' 'K' 0x03 visits         <- written LAST (= commit)
//   page  17     charge, colour, 0, 0
//   pages 18..29 one page per year (slot = 18 + year - baseYear, 12 years): number, tier, flags, chapter
//   pages 30..39 RESERVED for future use (never read or written; Format zeroes them)
// A TagImage is { uid: Uint8Array, uidLen, readOk, sak, pages: Uint8Array(37*4), readMs }.
// A PageWrite is { page, data: Uint8Array(4) }.
import { cfg, colorCount } from './settings.js';

export const TAG_FIRST_PAGE = 3, TAG_LAST_PAGE = 39, TAG_NUM_PAGES = 37, TAG_UID_MAX = 10, LINK_MAX_WRITES = 18;
export const PAGE_CC = 3, PAGE_NDEF = 4, NDEF_PAGES = 12, PAGE_MARKER = 16, PAGE_CHARGE = 17, PAGE_YEAR0 = 18, MAX_YEARS = 12, PAGE_RESERVED0 = 30;
export const MAGIC0 = 0x53, MAGIC1 = 0x4B, SCHEMA = 0x03;
export const SF = { FIRST: 0x01, CHARGED: 0x02, PITY: 0x04, MILESTONE: 0x08, CIRCLE: 0x10, BRAVE: 0x20, ORACLE: 0x80 };
export const BAND_LO = [1, 40, 90, 100], BAND_HI = [39, 89, 99, 100];
export const Kind = { Unreadable: 0, Foreign: 1, Fresh: 2, Ours: 3 };
export const PREP_MAX_WRITES = 40;

const NDEF_BYTES = NDEF_PAGES * 4, URL_MAX = NDEF_BYTES - 8;
const TLV_NULL = 0x00, TLV_LOCK = 0x01, TLV_MEM = 0x02, TLV_NDEF = 0x03, TLV_TERM = 0xFE, URI_PREFIX_HTTPS = 0x04;
const CC_NTAG203 = [0xE1, 0x10, 0x12, 0x00];
const ANON_SALT = 'crystal-oracle/anon/v1:', COLOR_SALT = 'crystal-oracle/colour/v1:';

export function newImage() {
  return { uid: new Uint8Array(TAG_UID_MAX), uidLen: 0, readOk: false, sak: 0, pages: new Uint8Array(TAG_NUM_PAGES * 4), readMs: 0 };
}
export function page(img, p) { return img.pages.subarray((p - TAG_FIRST_PAGE) * 4, (p - TAG_FIRST_PAGE) * 4 + 4); }
export function sameUid(a, al, b, bl) {
  if (al !== bl || al <= 0 || al > TAG_UID_MAX) return false;
  for (let i = 0; i < al; i++) if (a[i] !== b[i]) return false;
  return true;
}
export function uidHex(uid, len) { return Array.from(uid.subarray(0, len)).map(b => b.toString(16).padStart(2, '0').toUpperCase()).join(':'); }

function fnv1a(salt, uid, n) {
  let h = 0x811C9DC5;
  for (let i = 0; i < salt.length; i++) { h ^= salt.charCodeAt(i); h = Math.imul(h, 0x01000193) >>> 0; }
  for (let i = 0; i < n; i++) { h ^= uid[i]; h = Math.imul(h, 0x01000193) >>> 0; }
  return h >>> 0;
}
function urlBody(url) { return String(url || '').replace(/^https:\/\//i, ''); }
// The crystal's own link as stored (no "https://"): "<ndefUrl>/c/<anonId as 8 hex>", or the
// bare ndefUrl if that would pass 40 characters; '' if ndefUrl is empty.
export function crystalUrl(ndefUrl, anonId) {
  const base = urlBody(ndefUrl).replace(/\/+$/, '');
  if (!base) return '';
  return base.length + 11 <= URL_MAX ? `${base}/c/${(anonId >>> 0).toString(16).padStart(8, '0')}` : base;
}

function firstContentTlv(a) {
  let i = 0;
  while (i < NDEF_BYTES) {
    const t = a[i];
    if (t === TLV_NULL) { i++; continue; }
    if (t !== TLV_LOCK && t !== TLV_MEM) return i;
    if (i + 1 >= NDEF_BYTES || a[i + 1] !== 3) return -1;
    i += 5;
    if (i > NDEF_BYTES) return -1;
  }
  return NDEF_BYTES;
}
function ndefEmpty(a) {
  if (a.every(b => b === 0)) return true;
  const i = firstContentTlv(a);
  if (i < 0) return false;
  if (i >= NDEF_BYTES) return true;
  if (a[i] === TLV_TERM) return true;
  if (a[i] === TLV_NDEF && i + 1 < NDEF_BYTES && a[i + 1] === 0) return true;
  return false;
}
function linkMatches(a, url) {
  const n = url.length;
  if (!n || n > URL_MAX) return false;
  const i = firstContentTlv(a);
  if (i < 0 || i + 2 >= NDEF_BYTES || a[i] !== TLV_NDEF) return false;
  const tlvLen = a[i + 1], r = i + 2;
  if (tlvLen === 0xFF || r + tlvLen > NDEF_BYTES || tlvLen < 5) return false;
  if (a[r] !== 0xD1 || a[r + 1] !== 0x01 || a[r + 3] !== 0x55 || a[r + 4] !== URI_PREFIX_HTTPS) return false;
  const payloadLen = a[r + 2];
  if (payloadLen !== n + 1 || tlvLen !== 4 + payloadLen) return false;
  for (let k = 0; k < n; k++) if (a[r + 5 + k] !== url.charCodeAt(k)) return false;
  return true;
}
export function buildNdef(url) {
  const n = url.length;
  if (!n || n > URL_MAX) return null;
  const b = new Uint8Array(NDEF_BYTES);
  let i = 0;
  b[i++] = TLV_NDEF; b[i++] = n + 1 + 4; b[i++] = 0xD1; b[i++] = 0x01; b[i++] = n + 1; b[i++] = 0x55; b[i++] = URI_PREFIX_HTTPS;
  for (let k = 0; k < n; k++) b[i++] = url.charCodeAt(k) & 0xFF;
  b[i++] = TLV_TERM;
  return b;
}
function partialOurLink(a, url) {
  const want = buildNdef(url);
  if (!want) return false;
  let k = 0;
  while (k < NDEF_PAGES && a.subarray(k * 4, k * 4 + 4).every((v, j) => v === want[k * 4 + j])) k++;
  if (k === 0 || k === NDEF_PAGES) return false;
  for (let j = k; j < NDEF_PAGES; j++) {
    const p = a.subarray(j * 4, j * 4 + 4);
    const zero = !(p[0] | p[1] | p[2] | p[3]);
    const factoryTail = j === 1 && p[1] === 0x03 && p[2] === 0x00 && p[3] === 0xFE;
    if (!zero && !factoryTail) return false;
  }
  return true;
}
const put = (out, pg, d) => out.push({ page: pg, data: Uint8Array.from(d) });
// [CC if missing] + pages 4..15 = the crystal's own link, unless it's already there.
function putLink(out, c, ndefUrl) {
  const ndef = c.hasOurLink ? null : buildNdef(crystalUrl(ndefUrl, c.anonId));
  if ((ndef || c.hasOurLink) && !c.hasCC) put(out, PAGE_CC, CC_NTAG203);
  if (ndef) for (let p = 0; p < NDEF_PAGES; p++) put(out, PAGE_NDEF + p, ndef.subarray(p * 4, p * 4 + 4));
}

export function tierForNumber(n) { return n >= 100 ? 3 : n >= 90 ? 2 : n >= 40 ? 1 : 0; }
export function kindName(k) { return ['unreadable', 'foreign', 'fresh', 'ours'][k] || '?'; }

export function parse(img, eventYear, baseYear) {
  const n = Math.min(img.uidLen, TAG_UID_MAX);
  const c = {
    kind: Kind.Unreadable, uid: Uint8Array.from(img.uid.subarray(0, TAG_UID_MAX)), uidLen: n, visits: 0, charge: 0, color: 0,
    hasCC: false, hasOurLink: false, playedThisYear: false, thisYear: null, history: [], lastTier: -1, lastYear: 0,
    noRareStreak: 0, anonId: n ? fnv1a(ANON_SALT, img.uid, n) : 0,
    firstVisit() { return this.kind === Kind.Fresh || (this.kind === Kind.Ours && this.visits === 0); },
  };
  if (!img.readOk || n === 0) return c;
  c.hasCC = page(img, PAGE_CC)[0] === 0xE1;
  const ndef = img.pages.subarray((PAGE_NDEF - TAG_FIRST_PAGE) * 4, (PAGE_NDEF - TAG_FIRST_PAGE) * 4 + NDEF_BYTES);
  const base = urlBody(cfg.ndefUrl), own = crystalUrl(base, c.anonId);
  c.hasOurLink = linkMatches(ndef, own);
  const m = page(img, PAGE_MARKER);
  if (!(m[0] === MAGIC0 && m[1] === MAGIC1)) {
    // blank, or our link (its own or the bare site link, whole or cut short) -> Fresh
    c.kind = (c.hasOurLink || ndefEmpty(ndef) || linkMatches(ndef, base) || partialOurLink(ndef, own) || partialOurLink(ndef, base)) ? Kind.Fresh : Kind.Foreign;
    return c;
  }
  c.kind = Kind.Ours;
  c.visits = m[3];
  const chg = page(img, PAGE_CHARGE);
  c.charge = chg[0]; c.color = chg[1];
  const yIdx = eventYear - baseYear;
  for (let y = 0; y < MAX_YEARS; y++) {
    const s = page(img, PAGE_YEAR0 + y);
    if (s[0] === 0) continue;
    const e = { year: baseYear + y, number: Math.min(s[0], 100), tier: s[1] <= 3 ? s[1] : tierForNumber(Math.min(s[0], 100)), flags: s[2], chapter: s[3] };
    c.history.push(e);
    if (y === yIdx) { c.playedThisYear = true; c.thisYear = e; }
  }
  if (c.history.length > c.visits) c.visits = c.history.length;
  let haveLast = false;
  for (let i = c.history.length - 1; i >= 0; i--) {
    const e = c.history[i];
    if (e.year >= eventYear) continue;
    if (!haveLast) { c.lastTier = e.tier; c.lastYear = e.year; haveLast = true; }
    if (e.tier <= 1) c.noRareStreak++; else break;
  }
  return c;
}

// This year's visit as an ORDERED write list (marker last). [] on error / already played.
export function buildVisitWrites(c, eventYear, baseYear, number, tier, flags, chapter, ndefUrl, max = LINK_MAX_WRITES) {
  if (c.kind !== Kind.Fresh && c.kind !== Kind.Ours) return [];
  if (c.playedThisYear) return [];
  const yIdx = eventYear - baseYear;
  if (yIdx < 0 || yIdx >= MAX_YEARS) return [];
  if (number === 0) return [];
  if (number > 100) number = 100;
  if (tier > 3) tier = tierForNumber(number);
  const out = [];
  putLink(out, c, ndefUrl);   // provisions a Fresh crystal's link, upgrades an older one of ours
  if (c.kind === Kind.Fresh) put(out, PAGE_CHARGE, [0, 0, 0, 0]);
  put(out, PAGE_YEAR0 + yIdx, [number, tier, (flags | SF.ORACLE) & 0xFF, chapter & 0xFF]);
  const prior = c.kind === Kind.Ours ? c.visits : 0;
  put(out, PAGE_MARKER, [MAGIC0, MAGIC1, SCHEMA, Math.min(prior + 1, 255)]);
  if (out.length > max) return [];
  for (const w of out) if (w.page < PAGE_CC || w.page >= PAGE_RESERVED0) return [];
  return out;
}

// ---------------------------------------------------------------- crystal prep (host menu -> Crystals)
// Each builder returns { writes: [...], why } ; writes empty = nothing to do (why says why).
function prepCrystal(img) {
  if (!img.readOk || img.uidLen === 0) return { c: null, why: 'read failed - lift it and place it again' };
  const c = parse(img, cfg.eventYear, cfg.baseYear);
  if (c.kind === Kind.Unreadable) return { c: null, why: 'read failed - lift it and place it again' };
  return { c, why: '' };
}
function finishPrep(img, tmp, max) {
  if (!tmp.length) return { writes: [], why: 'nothing to write' };
  if (tmp.length > max) return { writes: [], why: 'too many pages for one write' };
  for (let i = 0; i < tmp.length; i++) {
    const pg = tmp[i].page;
    const unsafe = pg < PAGE_CC || pg > TAG_LAST_PAGE || (pg === PAGE_CC && page(img, PAGE_CC)[0] === 0xE1) || (pg === PAGE_MARKER && i + 1 !== tmp.length);
    if (unsafe) return { writes: [], why: 'refused: unsafe page list' };
  }
  return { writes: tmp, why: '' };
}
function provisionList(c, colour, ndefUrl) {
  const out = [];
  putLink(out, c, ndefUrl);
  put(out, PAGE_CHARGE, [0, colour, 0, 0]);
  put(out, PAGE_MARKER, [MAGIC0, MAGIC1, SCHEMA, 0]);
  return out;
}
export function buildProvisionWrites(img, colour, ndefUrl, max = PREP_MAX_WRITES) {
  const { c, why } = prepCrystal(img);
  if (!c) return { writes: [], why };
  if (c.kind === Kind.Foreign) return { writes: [], why: 'not blank - Format it first' };
  if (c.kind === Kind.Ours) {   // its visits stay; only an older link is brought up to date
    const out = [];
    putLink(out, c, ndefUrl || '');
    return out.length ? finishPrep(img, out, max) : { writes: [], why: 'already provisioned (its visits stay)' };
  }
  if (colour > 6) return { writes: [], why: 'no such colour' };
  return finishPrep(img, provisionList(c, colour, ndefUrl || ''), max);
}
export function buildColorWrite(img, colour, ndefUrl, max = PREP_MAX_WRITES) {
  const { c, why } = prepCrystal(img);
  if (!c) return { writes: [], why };
  if (colour > 6) return { writes: [], why: 'no such colour' };
  if (c.kind === Kind.Foreign) return { writes: [], why: 'not one of ours - Format it first' };
  if (c.kind === Kind.Fresh) return buildProvisionWrites(img, colour, ndefUrl, max);
  const cur = page(img, PAGE_CHARGE);
  if (cur[1] === colour) return { writes: [], why: 'it already has that colour' };
  const out = [];
  putLink(out, c, ndefUrl || '');   // an older link is brought up to date on the way
  put(out, PAGE_CHARGE, [cur[0], colour, cur[2], cur[3]]);
  return finishPrep(img, out, max);
}
export function buildClearYearWrites(img, eventYear, baseYear, max = PREP_MAX_WRITES) {
  const { c, why } = prepCrystal(img);
  if (!c) return { writes: [], why };
  if (c.kind === Kind.Foreign) return { writes: [], why: 'not one of ours - nothing to clear' };
  const yIdx = eventYear - baseYear;
  if (yIdx < 0 || yIdx >= MAX_YEARS) return { writes: [], why: 'event_year has no slot on the crystal' };
  const slotPg = PAGE_YEAR0 + yIdx;
  const s = page(img, slotPg);
  const out = [];
  if (!(s[0] | s[1] | s[2] | s[3])) {
    const m = page(img, PAGE_MARKER);
    if (c.kind === Kind.Ours && m[3] > c.history.length) {
      put(out, PAGE_MARKER, [MAGIC0, MAGIC1, m[2], c.history.length]);
      return finishPrep(img, out, max);
    }
    return { writes: [], why: 'nothing to clear - not played this year' };
  }
  put(out, slotPg, [0, 0, 0, 0]);
  if (c.kind === Kind.Ours && s[0] !== 0) {
    const m = page(img, PAGE_MARKER);
    put(out, PAGE_MARKER, [MAGIC0, MAGIC1, m[2], c.visits ? c.visits - 1 : 0]);
  }
  return finishPrep(img, out, max);
}
export function buildFormatWrites(img, max = PREP_MAX_WRITES) {
  if (!img.readOk || img.uidLen === 0) return { writes: [], why: 'read failed - lift it and place it again' };
  const out = [];
  const nz = p => (p[0] | p[1] | p[2] | p[3]) !== 0;
  for (let p = PAGE_NDEF; p <= TAG_LAST_PAGE && out.length < PREP_MAX_WRITES - 1; p++)
    if (p !== PAGE_MARKER && nz(page(img, p))) put(out, p, [0, 0, 0, 0]);
  if (nz(page(img, PAGE_MARKER))) put(out, PAGE_MARKER, [0, 0, 0, 0]);
  if (!out.length) return { writes: [], why: 'it is already blank' };
  return finishPrep(img, out, max);
}

export function benchColor(c) { return (c.color >= 1 && c.color <= colorCount()) ? c.color : 0; }
export function displayColor(c) {
  const n = colorCount();
  if (!n) return 1;
  if (c.color >= 1 && c.color <= n) return c.color;
  if (!c.uidLen) return 1;
  return 1 + (fnv1a(COLOR_SALT, c.uid, c.uidLen) % n);
}
