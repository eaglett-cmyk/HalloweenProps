// oracle.js - the rules: odds, prize pacing, ranks, rush mode, groups (a port of oracle.cpp).
// One visit = ONE uniform roll laid over four bands: LEGENDARY | RARE | TRICK | TREAT
// (docs/ODDS.md has the resulting probabilities).
import { cfg, counters, RUSH } from './settings.js';
import { Kind, SF, BAND_LO, BAND_HI, tierForNumber } from './token.js';

const RARE_CAP = 0.95, RUSH_BURST_N = 5, RUSH_BURST_MS = 60000, GROUP_IDS = 16;

export const rand01 = () => Math.random();
export const randBelow = n => (n > 0 ? Math.floor(Math.random() * n) : 0);

function paced(p, stock, given) {
  if (p <= 0) return 0;
  if (stock === 0) return p;
  if (given >= stock) return 0;
  const left = cfg.expectedKids - counters.visits;
  const expectedRemaining = left > 1 ? left : 1;
  let f = (stock - given) / (p * expectedRemaining);
  f = Math.min(2.0, Math.max(0.3, f));
  return p * f;
}

// ---- rush state ----
let s_rushOn = false, s_inSeq = false, s_haveEnd = false, s_lastEndMs = 0, s_lastActivityMs = 0;
const s_starts = []; let s_nStarts = 0, s_startHead = 0;
// ---- group state ----
let s_groupIds = [], s_groupCount = 0, s_groupLastMs = 0, s_groupAlive = false, s_circleFired = false, s_seqPending = false;

function groupExpired(now) {
  const gapMs = cfg.groupGapSec * 1000;
  return !s_groupAlive || gapMs === 0 || now - s_groupLastMs >= gapMs;
}
function noteCrystal(anonId, now) {
  s_seqPending = false;
  if (groupExpired(now)) { s_groupIds = []; s_groupCount = 0; s_circleFired = false; }
  s_groupAlive = true;
  s_groupLastMs = now;
  if (s_groupIds.includes(anonId)) return;
  if (s_groupIds.length < GROUP_IDS) s_groupIds.push(anonId);
  if (s_groupCount < 255) s_groupCount++;
}

export function rankFor(visitsAfter) {
  if (visitsAfter <= 1) return 1;
  if (visitsAfter <= 5) return visitsAfter;
  if (visitsAfter <= 7) return 6;
  return 7;
}
export function meterFor(visitsAfter) {
  if (visitsAfter < 1) visitsAfter = 1;
  return visitsAfter >= 7 ? 8 : visitsAfter + 1;
}

export function decide(c, chargeFill, now = performance.now()) {
  if (chargeFill > 100) chargeFill = 100;
  const first = c.firstVisit();
  const prior = c.kind === Kind.Ours ? c.visits : 0;
  noteCrystal(c.anonId, now);

  const pL = paced(cfg.legendPct / 100, cfg.legendStock, counters.legendGiven);
  let loyalty = cfg.loyaltyRarePct * prior;
  if (loyalty > cfg.loyaltyCapPct) loyalty = cfg.loyaltyCapPct;
  let pR = (cfg.rarePct + loyalty + cfg.chargeRarePct * chargeFill / 100 + c.charge) / 100;
  if (pR > RARE_CAP) pR = RARE_CAP;
  pR = paced(pR, cfg.rareStock, counters.rareGiven);
  if (pR > RARE_CAP) pR = RARE_CAP;
  if (pR > 1 - pL) pR = 1 - pL;
  const rareInStock = cfg.rareStock === 0 || counters.rareGiven < cfg.rareStock;
  const pity = cfg.pityAfter > 0 && c.noRareStreak >= cfg.pityAfter && rareInStock;
  let pT = (first && cfg.firstNoTrick) ? 0 : cfg.trickPct / 100;
  if (pT > 1 - pL - pR) pT = 1 - pL - pR;
  if (pT < 0) pT = 0;

  const o = { number: 0, tier: 1, flags: 0, rank: 1, visitsAfter: 1, meter: 2, rankUp: false, pity: false, milestone: false };
  const r = rand01();
  if (r < pL) o.tier = 3;
  else if (pity) { o.tier = 2; o.pity = true; }
  else if (r < pL + pR) o.tier = 2;
  else if (r < pL + pR + pT) o.tier = 0;
  else o.tier = 1;

  const lo = BAND_LO[o.tier], hi = BAND_HI[o.tier];
  let n = lo + randBelow(hi - lo + 1);
  if (chargeFill) n = n + Math.floor(((hi - n) * chargeFill + 100) / 200);
  o.number = n;

  o.flags = SF.ORACLE;
  if (first) o.flags |= SF.FIRST;
  if (chargeFill >= 100) o.flags |= SF.CHARGED;
  if (o.pity) o.flags |= SF.PITY;
  if (cfg.milestoneEvery && (counters.stationCrystals + 1) % cfg.milestoneEvery === 0) { o.flags |= SF.MILESTONE; o.milestone = true; }
  if (groupSize(now) >= 3) o.flags |= SF.CIRCLE;

  o.visitsAfter = prior < 255 ? prior + 1 : 255;
  o.rank = rankFor(o.visitsAfter);
  o.meter = meterFor(o.visitsAfter);
  o.rankUp = prior >= 1 && rankFor(prior) !== o.rank;
  console.log(`oracle: ${first ? 'first' : 'return'} prior=${prior} streak=${c.noRareStreak} fill=${chargeFill} charge=${c.charge}  L=${(pL * 100).toFixed(1)}% R=${(pR * 100).toFixed(1)}% T=${(pT * 100).toFixed(1)}%${pity ? ' PITY' : ''}  r=${r.toFixed(3)} -> t${o.tier} #${o.number} r${o.rank}`);
  return o;
}

export function replay(c, now = performance.now()) {
  const e = c.thisYear || { number: 0, tier: 1, flags: 0 };
  const o = { number: e.number, tier: e.tier <= 3 ? e.tier : tierForNumber(e.number), flags: e.flags, rank: 1,
              visitsAfter: c.visits ? c.visits : 1, meter: 2, rankUp: false, pity: (e.flags & SF.PITY) !== 0, milestone: false };
  o.rank = rankFor(o.visitsAfter);
  o.meter = meterFor(o.visitsAfter);
  noteCrystal(c.anonId, now);
  return o;
}

const sat16 = v => (v < 0xFFFF ? v + 1 : v);
export function record(o, firstVisit) {
  counters.visits = sat16(counters.visits);
  counters.stationCrystals++;
  if (firstVisit) { counters.awakened = sat16(counters.awakened); counters.stationAwakened++; }
  else counters.returning = sat16(counters.returning);
  const t = o.tier <= 3 ? o.tier : 1;
  counters.tier[t] = sat16(counters.tier[t]);
  if (t === 2) counters.rareGiven = sat16(counters.rareGiven);
  if (t === 3) counters.legendGiven = sat16(counters.legendGiven);
  const every = cfg.milestoneEvery;
  return !!every && counters.stationCrystals % every === 0;
}

// ---- rush ----
function expireRush(now) {
  if (s_rushOn && !s_inSeq && now - s_lastActivityMs >= cfg.rushQuietSec * 1000) s_rushOn = false;
}
export function sequenceStarted(now) {
  expireRush(now);
  const gapMs = cfg.rushGapSec * 1000;
  if (s_haveEnd && gapMs && now - s_lastEndMs <= gapMs) s_rushOn = true;
  s_starts[s_startHead] = now;
  s_startHead = (s_startHead + 1) % RUSH_BURST_N;
  if (s_nStarts < RUSH_BURST_N) s_nStarts++;
  if (s_nStarts === RUSH_BURST_N && now - s_starts[s_startHead] <= RUSH_BURST_MS) s_rushOn = true;
  s_inSeq = true;
  s_lastActivityMs = now;
  s_seqPending = true;
}
export function sequenceEnded(now, armsGap = true) {
  s_inSeq = false;
  s_seqPending = false;
  if (armsGap) { s_haveEnd = true; s_lastEndMs = now; }
  s_lastActivityMs = now;
}
export function rush(now) {
  expireRush(now);
  if (cfg.rush === RUSH.ON) return true;
  if (cfg.rush === RUSH.OFF) return false;
  return s_rushOn;
}

// ---- groups ----
export function groupSize(now) {
  const known = groupExpired(now) ? 0 : s_groupCount;
  return (s_seqPending && known < 255) ? known + 1 : known;
}
export function circleDue(now) {
  if (s_circleFired || groupSize(now) < 3) return false;
  s_circleFired = true;
  return true;
}
