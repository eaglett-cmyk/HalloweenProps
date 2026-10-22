#pragma once
// ============================================================================
//  show.h — the director. Runs on the Arduino loop task (core 1) and owns the
//  screen. One "step" = one visual (clip / still / procedural fallback) + an
//  optional chain of voice parts (each with its caption) + sfx + live overlays,
//  held for max(min time, voice length) and capped by a max time, while reader
//  + touch events keep being polled every frame.
//
//  Story-neutral: the story (STORY.md) is the TF card's art, voice and
//  captions.ini words (DESIGN.md §5.6). Flows (docs/DESIGN.md §5 has the
//  timing table, STORY.md §2/§9 the cue sheet):
//   ATTRACT   loop "attract" ("attract_full" after the finale) + music bed
//             ("idle_full" after the finale) + attract_call every ~30 s + the
//             "chapter" story (parts a..f) every N loops; captions alternate
//             "Crystal here" / "OR PRESS"; the plinth ring cycles the defined crystal
//             colours (1..settings::colorCount()); the sidekick's "munch" sfx ~5.4 s into each loop; the
//             communal meter (words meter.*) with the station counter under it.
//   PLACED -> ABSORB (1 s, a beam of the crystal's colour into the narrator's glow anchor,
//             the colour name) while the tag image is classified:
//     Unreadable          -> SLEEPY (2 s) -> ATTRACT
//     Foreign             -> NOCRYSTAL flow (still candy), tag never written
//     Ours, played today  -> REPLAY (tonight's number/tier dimmed + check mark
//                            [+ bowl line if secondHelpings])
//     First visit         -> decide + WRITE now -> AWAKEN (awaken_<colour>;
//                            awaken_a -> awaken_b_<bench colour> -> awaken_c)
//                            -> REVEAL (+ bowl line) -> GOODBYE
//     Returning           -> GREET r<n> (2 s) [-> RECALL] -> CHARGE window
//                            (skipped in rush) -> decide + WRITE -> REVEAL
//                            (+ bowl line) -> GOODBYE (skipped in rush)
//   BUTTON (no crystal on the reader) -> NOCRYSTAL (a blessing, +1 meter point) + bowl line
//   TAP on the screen, idle: the same blessing when the reader is asleep, or - with
//             config.ini button = no (no plinth button, DESIGN.md §5.7) - whenever no crystal
//             is on the reader (not for tap_cooldown_sec after a sequence; +1 meter point at
//             most every tap_spark_sec). No-button mode also reads the words nobutton.<key>
//             first and swaps voice lines per the word nobutton.voice.
//   keeper (top rank, ~1 in keeper_one_in) / milestone / circle extras
//   after the reveal when due. Rush: one combined rush_t<tier> line per reveal.
//   MOONFULL once per night, right after the sequence whose meter point reaches
//   [story] moon_goal (or when the host menu's "Fill the <meter.name>" closes).
//   Long-press (3 s) the top-left corner -> service menu (service.h).
//  Meter points (+1 per crystal visit that rolled, +1 per no-crystal blessing)
//  and the finale flag live in counters (tonight_<year>.ini on the TF card).
//  The tag write must finish (WriteDone OK) before the reveal starts; it is
//  retried while the crystal stays put, up to writeTimeoutMs. If it still fails
//  the kid sees the result anyway (never punished) and the failure is logged.
// ============================================================================
#include <Arduino.h>

namespace show {

void begin();     // load sfx, start music, set the idle light, enter ATTRACT
void loop();      // run the attract loop until something happens, then the sequence (returns between sequences)

// Boot screen (drawn before the show starts): title + up to 8 status lines.
void bootScreen(const char *const *lines, uint8_t n);

}  // namespace show
