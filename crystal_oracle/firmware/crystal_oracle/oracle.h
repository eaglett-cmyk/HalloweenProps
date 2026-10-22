#pragma once
// ============================================================================
//  oracle.h — the rules: ranks, odds, prize pacing, rush mode, groups.
//
//  Outcome for a crystal visit (decided BEFORE the reveal and written to the tag
//  before it is shown, so lifting the crystal early never changes anything):
//   1. Legendary roll: legendPct, NOT affected by loyalty; paced by legendStock.
//   2. Rare roll: rarePct + min(loyaltyRarePct * priorVisits, loyaltyCapPct)
//                 + chargeRarePct * chargeFill + crystal charge (bench "blessing", %);
//      pity: noRareStreak >= pityAfter => rare guaranteed (if stock allows).
//      Paced by rareStock.
//   3. Otherwise trick (trickPct of all visits, never on a first visit if
//      firstNoTrick) or treat.
//   4. number = random inside the tier's skull band (trick 1-39, treat 40-89,
//      rare 90-99, legendary 100), nudged up by the charge fill within the band.
//  Stock pacing: p_eff = p * clamp(remaining / (p * expectedRemaining), 0.3, 2.0),
//  and 0 when the stock is used up (stock 0 in config = unlimited).
//
//  Ranks by TOTAL visits including this one (missing a year never lowers rank):
//   1, 2, 3, 4, 5, 6-7, 8+  => rank 1..7. Meter segments (of 8): visits+1, max 8
//  (first visit already shows 2 = endowed progress).
// ============================================================================
#include <Arduino.h>
#include "token.h"

struct Outcome {
  uint8_t number = 0;       // 1..100 ("crystal power" shown on screen)
  uint8_t tier = 1;         // 0 trick, 1 treat, 2 rare, 3 legendary
  uint8_t flags = 0;        // token::SlotFlags
  uint8_t rank = 1;         // 1..7 after this visit
  uint8_t visitsAfter = 1;
  uint8_t meter = 2;        // 2..8 segments
  bool    rankUp = false;   // rank changed with this visit
  bool    pity = false;
  bool    milestone = false;
};

namespace oracle {

uint8_t rankFor(uint8_t visitsAfter);
uint8_t meterFor(uint8_t visitsAfter);

// chargeFill 0..100 = how full the optional charge meter got (0 if skipped).
Outcome decide(const token::Crystal &c, uint8_t chargeFill);
Outcome replay(const token::Crystal &c);   // tonight's stored result (already played)
// Record a completed visit in counters (stock used, tier counts, station counter).
// Returns true if this visit is a station milestone (counter % milestoneEvery == 0).
bool record(const Outcome &o, bool firstVisit);

// ---- rush mode (auto unless forced in settings) ----
void sequenceStarted(uint32_t nowMs);
// armsGap = false: this sequence's end doesn't count for the short-gap rule
// (SLEEPY "set it down again" / the no-crystal button: the next crystal is the same kid).
void sequenceEnded(uint32_t nowMs, bool armsGap = true);
bool rush(uint32_t nowMs);

// ---- groups ("the circle is complete") ----
uint8_t groupSize(uint32_t nowMs);   // crystals placed in the current group (gap < groupGapSec)
bool    circleDue(uint32_t nowMs);   // true once per group, when it reaches 3

}  // namespace oracle
