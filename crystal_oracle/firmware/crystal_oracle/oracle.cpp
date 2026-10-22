/*
 * ============================================================================
 *  oracle.cpp — the rules: odds, prize pacing, ranks, rush mode, groups
 * ----------------------------------------------------------------------------
 *  Implements oracle.h. One visit = ONE uniform roll r in [0,1) laid over four
 *  bands, so every percentage in config.ini [odds] is "percent of visits":
 *
 *      0 ......... pL ......... pL+pR ......... pL+pR+pT ................ 1
 *      | LEGENDARY |    RARE     |     TRICK      |         TREAT          |
 *
 *    pL = legendPct, paced by [stock] legend               (no loyalty bonus)
 *    pR = rarePct + min(loyaltyRarePct * priorVisits, loyaltyCapPct)
 *         + chargeRarePct * chargeFill/100 + crystal charge (bench, in %),
 *         capped at 95 %, then paced by [stock] rare (and capped at 95 % again)
 *         PITY: noRareStreak >= pityAfter (and rare stock left) => everything
 *         outside the legendary band is rare.
 *    pT = trickPct (0 on a crystal's first visit when firstNoTrick), squeezed
 *         if the prize bands leave less room than that.
 *    Treat = whatever is left.
 *  Stock pacing (stock 0 = unlimited):
 *    p_eff = p * clamp(remaining / (p * expectedRemaining), 0.3, 2.0),
 *    expectedRemaining = max(1, expectedKids - tonight's visits), 0 when used up.
 *  Number: uniform inside the tier's skull band, then a filled charge meter
 *  moves it up to halfway towards the top of the band (never out of it).
 *  docs/ODDS.md has the resulting probabilities (simulated, 200k trials each).
 *
 *  Rush (DESIGN.md §5.3): on when a sequence starts within rushGapSec of the
 *  previous one ending, or when 5 sequences started within 60 s; off after
 *  rushQuietSec without a sequence. [station] rush = on|off overrides.
 *  Groups: every crystal seen by decide()/replay() joins the current group
 *  (distinct crystals, by anonymous id); the group ends after groupGapSec
 *  without a new crystal. A sequence that has started but whose crystal isn't
 *  identified yet counts as one more member, so circleDue() is right when the
 *  director asks it just BEFORE decide() (show.cpp does). circleDue() fires
 *  once per group, when it reaches 3.
 *
 *  Everything here runs on the Arduino loop task (the director) - no locking.
 * ============================================================================
 */
#include "oracle.h"
#include "settings.h"
#include "esp_random.h"

namespace oracle {

namespace {

constexpr float RARE_CAP = 0.95f;            // rare never exceeds 95 % of visits
constexpr uint32_t RUSH_BURST_N = 5;         // this many sequences ...
constexpr uint32_t RUSH_BURST_MS = 60000;    // ... started within this window => rush
constexpr uint8_t GROUP_IDS = 16;            // distinct crystals remembered per group

// ---- randomness: the ESP32 hardware RNG ----
inline float rand01() { return (float)(esp_random() >> 8) * (1.0f / 16777216.0f); }   // [0,1), 24 bits
inline uint32_t randBelow(uint32_t n) { return n ? (uint32_t)(((uint64_t)esp_random() * n) >> 32) : 0; }

inline uint16_t sat16(uint16_t v) { return v < 0xFFFF ? (uint16_t)(v + 1) : v; }

// Stock pacing: spread `stock` prizes over the expected rest of the night.
float paced(float p, uint16_t stock, uint16_t given) {
  if (p <= 0.0f) return 0.0f;
  if (stock == 0) return p;               // unlimited
  if (given >= stock) return 0.0f;        // used up
  const int32_t left = (int32_t)settings::cfg.expectedKids - (int32_t)counters::c.visits;
  const float expectedRemaining = left > 1 ? (float)left : 1.0f;
  float f = (float)(stock - given) / (p * expectedRemaining);
  if (f < 0.3f) f = 0.3f;
  if (f > 2.0f) f = 2.0f;
  return p * f;
}

// ---- rush state ----
bool     s_rushOn = false;
bool     s_inSeq = false;
bool     s_haveEnd = false;
uint32_t s_lastEndMs = 0;
uint32_t s_lastActivityMs = 0;
uint32_t s_starts[RUSH_BURST_N] = {0};
uint8_t  s_nStarts = 0, s_startHead = 0;

// ---- group state ----
uint32_t s_groupIds[GROUP_IDS] = {0};
uint8_t  s_groupIdN = 0;
uint8_t  s_groupCount = 0;      // distinct crystals in the group (keeps counting past GROUP_IDS)
uint32_t s_groupLastMs = 0;
bool     s_groupAlive = false;
bool     s_circleFired = false;
bool     s_seqPending = false;  // a sequence started, its crystal not identified yet

bool groupExpired(uint32_t now) {
  const uint32_t gapMs = (uint32_t)settings::cfg.groupGapSec * 1000u;
  return !s_groupAlive || gapMs == 0 || (uint32_t)(now - s_groupLastMs) >= gapMs;
}

void noteCrystal(uint32_t anonId, uint32_t now) {
  s_seqPending = false;   // the pending member is now known
  if (groupExpired(now)) {
    s_groupIdN = 0;
    s_groupCount = 0;
    s_circleFired = false;
  }
  s_groupAlive = true;
  s_groupLastMs = now;
  for (uint8_t i = 0; i < s_groupIdN; i++) {
    if (s_groupIds[i] == anonId) return;   // same crystal again (replay, retry): not a new member
  }
  if (s_groupIdN < GROUP_IDS) s_groupIds[s_groupIdN++] = anonId;
  if (s_groupCount < 255) s_groupCount++;
}

}  // namespace

// ------------------------------------------------------------- ranks ----
uint8_t rankFor(uint8_t visitsAfter) {   // 1, 2, 3, 4, 5, 6-7, 8+  ->  1..7
  if (visitsAfter <= 1) return 1;
  if (visitsAfter <= 5) return visitsAfter;
  if (visitsAfter <= 7) return 6;
  return 7;
}

uint8_t meterFor(uint8_t visitsAfter) {   // visits + 1 of 8 (a first visit already shows 2)
  if (visitsAfter < 1) visitsAfter = 1;
  return visitsAfter >= 7 ? 8 : (uint8_t)(visitsAfter + 1);
}

// ------------------------------------------------------------- decide ----
Outcome decide(const token::Crystal &c, uint8_t chargeFill) {
  const Settings &s = settings::cfg;
  const Counters &k = counters::c;
  const uint32_t now = millis();
  if (chargeFill > 100) chargeFill = 100;
  const bool first = c.firstVisit();
  const uint8_t prior = (c.kind == token::Kind::Ours) ? c.visits : 0;
  noteCrystal(c.anonId, now);

  // 1. legendary: flat, paced by its own stock
  const float pL = paced(s.legendPct / 100.0f, s.legendStock, k.legendGiven);

  // 2. rare: base + loyalty (capped) + charge meter + bench charge, <= 95 %
  uint32_t loyalty = (uint32_t)s.loyaltyRarePct * prior;
  if (loyalty > s.loyaltyCapPct) loyalty = s.loyaltyCapPct;
  float rarePct = (float)s.rarePct + (float)loyalty + (float)s.chargeRarePct * chargeFill / 100.0f + (float)c.charge;
  float pR = rarePct / 100.0f;
  if (pR > RARE_CAP) pR = RARE_CAP;
  pR = paced(pR, s.rareStock, k.rareGiven);
  if (pR > RARE_CAP) pR = RARE_CAP;
  if (pR > 1.0f - pL) pR = 1.0f - pL;
  const bool rareInStock = s.rareStock == 0 || k.rareGiven < s.rareStock;
  const bool pity = s.pityAfter > 0 && c.noRareStreak >= s.pityAfter && rareInStock;

  // 3. trick share of all visits (never a first visit if firstNoTrick)
  float pT = (first && s.firstNoTrick) ? 0.0f : s.trickPct / 100.0f;
  if (pT > 1.0f - pL - pR) pT = 1.0f - pL - pR;
  if (pT < 0.0f) pT = 0.0f;

  Outcome o;
  const float r = rand01();
  if (r < pL) {
    o.tier = 3;
  } else if (pity) {
    o.tier = 2;
    o.pity = true;
  } else if (r < pL + pR) {
    o.tier = 2;
  } else if (r < pL + pR + pT) {
    o.tier = 0;
  } else {
    o.tier = 1;
  }

  // 4. number inside the skull band, nudged up by the charge meter
  const uint8_t lo = token::BAND_LO[o.tier], hi = token::BAND_HI[o.tier];
  uint8_t n = (uint8_t)(lo + randBelow((uint32_t)(hi - lo) + 1));
  if (chargeFill) n = (uint8_t)(n + ((uint32_t)(hi - n) * chargeFill + 100) / 200);   // full = halfway to the top
  o.number = n;

  // flags (SF_ORACLE included so the Outcome shows exactly what the slot gets)
  o.flags = token::SF_ORACLE;
  if (first) o.flags |= token::SF_FIRST;
  if (chargeFill >= 100) o.flags |= token::SF_CHARGED;
  if (o.pity) o.flags |= token::SF_PITY;
  if (s.milestoneEvery && (k.stationCrystals + 1) % s.milestoneEvery == 0) {   // record() will confirm
    o.flags |= token::SF_MILESTONE;
    o.milestone = true;
  }
  if (groupSize(now) >= 3) o.flags |= token::SF_CIRCLE;

  // rank + meter by total visits including this one
  o.visitsAfter = prior < 255 ? (uint8_t)(prior + 1) : 255;
  o.rank = rankFor(o.visitsAfter);
  o.meter = meterFor(o.visitsAfter);
  o.rankUp = prior >= 1 && rankFor(prior) != o.rank;

  Serial.printf("oracle: %s prior=%u streak=%u fill=%u charge=%u  L=%.1f%% R=%.1f%% T=%.1f%%%s  r=%.3f -> t%u #%u r%u\n",
                first ? "first" : "return", prior, c.noRareStreak, chargeFill, c.charge, pL * 100.0f,
                pR * 100.0f, pT * 100.0f, pity ? " PITY" : "", r, o.tier, o.number, o.rank);
  return o;
}

Outcome replay(const token::Crystal &c) {
  Outcome o;
  const token::YearEntry &e = c.thisYear;
  o.number = e.number;
  o.tier = e.tier <= 3 ? e.tier : token::tierForNumber(e.number);
  o.flags = e.flags;
  o.visitsAfter = c.visits ? c.visits : 1;   // the marker already counts tonight's visit
  o.rank = rankFor(o.visitsAfter);
  o.meter = meterFor(o.visitsAfter);
  o.rankUp = false;
  o.pity = (e.flags & token::SF_PITY) != 0;
  o.milestone = false;                       // celebrated when it happened
  noteCrystal(c.anonId, millis());
  return o;
}

bool record(const Outcome &o, bool firstVisit) {
  Counters &k = counters::c;
  k.visits = sat16(k.visits);
  k.stationCrystals++;
  if (firstVisit) {
    k.awakened = sat16(k.awakened);
    k.stationAwakened++;
  } else {
    k.returning = sat16(k.returning);
  }
  const uint8_t t = o.tier <= 3 ? o.tier : 1;
  k.tier[t] = sat16(k.tier[t]);
  if (t == 2) k.rareGiven = sat16(k.rareGiven);
  if (t == 3) k.legendGiven = sat16(k.legendGiven);
  // (no counters::save() here: it would hold the reveal on the SD card; the show
  //  saves the counters in endSequence(), a few seconds later)
  const uint16_t every = settings::cfg.milestoneEvery;
  return every && (k.stationCrystals % every) == 0;
}

// --------------------------------------------------------------- rush ----
namespace {
// Rush ends after rushQuietSec without a sequence (checked before anything
// refreshes the activity time, so a long unpolled idle still ends it).
void expireRush(uint32_t nowMs) {
  if (s_rushOn && !s_inSeq &&
      (uint32_t)(nowMs - s_lastActivityMs) >= (uint32_t)settings::cfg.rushQuietSec * 1000u) {
    s_rushOn = false;
  }
}
}  // namespace

void sequenceStarted(uint32_t nowMs) {
  expireRush(nowMs);
  const uint32_t gapMs = (uint32_t)settings::cfg.rushGapSec * 1000u;
  if (s_haveEnd && gapMs && (uint32_t)(nowMs - s_lastEndMs) <= gapMs) s_rushOn = true;

  s_starts[s_startHead] = nowMs;
  s_startHead = (uint8_t)((s_startHead + 1) % RUSH_BURST_N);
  if (s_nStarts < RUSH_BURST_N) s_nStarts++;
  // with the ring full, s_startHead now points at the oldest of the last 5 starts
  if (s_nStarts == RUSH_BURST_N && (uint32_t)(nowMs - s_starts[s_startHead]) <= RUSH_BURST_MS) s_rushOn = true;

  s_inSeq = true;
  s_lastActivityMs = nowMs;
  s_seqPending = true;     // its crystal (if any) joins the group in decide()/replay()
}

void sequenceEnded(uint32_t nowMs, bool armsGap) {
  s_inSeq = false;
  s_seqPending = false;    // no crystal identified (button / foreign / sleepy): not a member
  if (armsGap) {           // a sleepy retry / button-then-crystal must not start rush
    s_haveEnd = true;
    s_lastEndMs = nowMs;
  }
  s_lastActivityMs = nowMs;
}

bool rush(uint32_t nowMs) {
  expireRush(nowMs);
  if (settings::cfg.rush == RUSH_ON) return true;
  if (settings::cfg.rush == RUSH_OFF) return false;
  return s_rushOn;
}

// ------------------------------------------------------------- groups ----
uint8_t groupSize(uint32_t nowMs) {
  const uint8_t known = groupExpired(nowMs) ? 0 : s_groupCount;
  return (s_seqPending && known < 255) ? (uint8_t)(known + 1) : known;
}

bool circleDue(uint32_t nowMs) {
  if (s_circleFired || groupSize(nowMs) < 3) return false;
  s_circleFired = true;
  return true;
}

}  // namespace oracle
