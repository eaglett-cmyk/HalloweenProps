#pragma once
// ============================================================================
//  service.h — the host's hidden menu. Enter by holding the TOP-LEFT corner of
//  the screen (100x100 px) for 3 s while idle. Big touch buttons, no clips.
//
//  Pages:
//   * Tonight: visits, awakened, returning, replays, no-crystal, tiers, stock
//     left, write failures, station total, the communal meter (words
//     meter.name); Reset tonight, Fill the <name> (points = goal; the finale
//     plays when the menu closes), Reset <name>.
//   * Sound & light: volume -/+, brightness -/+, ring brightness -/+, test tone.
//   * Mode: rush auto/on/off, second helpings on/off, rare/legend stock -/+.
//   * Reader: backend, online, last packet age, packets, plinth button yes/no,
//     simulate crystal (fresh / returning / played / foreign / unreadable) and
//     button (labelled "Blessing" when config.ini button = no).
//   * Crystals: crystal prep through the reader - what's on it (read OK + kind,
//     UID, visits, bench colour with a live swatch, charge, this year's slot,
//     years on it, link + CC), a read test (good reads / placements since the
//     page opened, last read time), colour None / 1..N, Provision, Clear
//     tonight, Format (tap twice), Sim blank. Writes are built by token.h's
//     prep builders, sent with reader::write() and verified; while the page is
//     open every reader event is consumed by it (nothing leaks into the show).
//   * Media: SD size, asset count, missing-cue report, read benchmark, rescan;
//     play any cue (cue browser).
//   * System: I2C scan, free RAM/PSRAM, firmware version, panel refresh, reboot,
//     credits + "the voice is AI-generated" note.
//  Changes to settings are saved to /oracle/config.ini on exit.
// ============================================================================
#include <Arduino.h>

namespace service {
void run();       // blocks until the host taps EXIT (or 60 s idle)
}
