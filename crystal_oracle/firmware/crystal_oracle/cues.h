#pragma once
// ============================================================================
//  cues.h — the names that tie firmware, SD card and asset manifest together.
//  tools/build_assets.py and assets/manifest.json use exactly these ids.
//
//  Visual  id  -> /oracle/clips/<id>.omj|.jpg     (variant: <id>_<n>.omj|.jpg)
//  Voice group -> /oracle/voice/<group>_<n>.wav   (n = 1..9, one picked at random,
//                                                  never the same twice in a row)
//  Sequenced voice parts use letter suffixes: awaken_a, awaken_b, awaken_c.
//  When a cue picks voice variant n, it shows visual <id>_<n> if that exists
//  (e.g. reveal_t2_2 = the 2nd Witch's Hat picture with the 2nd hat line).
//  Explicit variants (the director chooses n, not the voice):
//    awaken      visual awaken_<colour 1..N> (token::displayColor; N = settings::colorCount()), voice
//                awaken_a_<random> -> awaken_b_<bench colour> (skipped when the
//                crystal has no bench colour) -> awaken_c_<random>
//    reveal_t0   gag n shared by visual reveal_t0_<n>, voice reveal_t0_<n> and
//                sfx reveal_t0_<n>.wav (falls back to sfx reveal_t0, then a chime)
//  Lantern anchors: captions.ini [anchor] <clip id> = "x,y,r" (screen px at t = 0,
//  r = glow radius) for each built clip id incl. variants (anchor.awaken_3);
//  the director looks up the shown id, then the base id.
// ============================================================================

namespace cue {

// ---- visuals (and the voice group of the same name unless noted) ----
constexpr const char *ATTRACT     = "attract";      // idle loop; voice group ATTRACT_CALL
constexpr const char *ATTRACT_FULL= "attract_full"; // idle loop after the meter is full (falls back to ATTRACT)
constexpr const char *ATTRACT_CALL= "attract_call"; // voice only
constexpr const char *CHAPTER     = "chapter";      // idle-rotation story in parts: visual + voice chapter_a..chapter_f
                                                    // (a part without its own visual uses "chapter")
constexpr const char *ABSORB      = "absorb";       // 1 s crystal placed (sfx only)
constexpr const char *AWAKEN      = "awaken";       // first visit; visual awaken_<colour>; voice parts awaken_a, awaken_b_<colour>, awaken_c
constexpr const char *GREET       = "greet";        // returning: visual greet_r<n> or greet; voice greet_r<n>
constexpr const char *RECALL      = "recall";       // voice recall_t<lastTier> ("last year you found...")
constexpr const char *CHARGE      = "charge";       // optional tap/press window
constexpr const char *REVEAL      = "reveal";       // visual+voice reveal_t<tier> (t0 = trick gags reveal_t0_<n>)
constexpr const char *BOWL        = "bowl";         // voice bowl_common | bowl_rare | bowl_legend
constexpr const char *RUSH        = "rush";         // rush mode: voice rush_t<tier> = bowl + result in one line
constexpr const char *REPLAY      = "replay";       // already played tonight
constexpr const char *NOCRYSTAL   = "nocrystal";    // button without a crystal (a blessing; +1 meter point)
constexpr const char *SLEEPY      = "sleepy";       // unreadable crystal
constexpr const char *FOREIGN     = "foreign";      // not an oracle crystal (falls back to nocrystal visuals/voice)
constexpr const char *GOODBYE     = "goodbye";
constexpr const char *MILESTONE   = "milestone";
constexpr const char *CIRCLE      = "circle";
constexpr const char *KEEPER      = "keeper";       // extra line for top-rank crystals (about 1 visit in keeper_one_in)
constexpr const char *RANKUP      = "rankup";       // optional voice group rankup_r<n>
constexpr const char *MOONFULL    = "moonfull";     // once per night when the meter is full; voice parts moonfull_a, moonfull_b

// ---- sound effects: /oracle/sfx/<id>.wav (generated chimes are used if missing) ----
constexpr const char *SFX_ABSORB  = "absorb";
constexpr const char *SFX_AWAKEN  = "awaken";
constexpr const char *SFX_REVEAL[4] = {"reveal_t0", "reveal_t1", "reveal_t2", "reveal_t3"};   // + reveal_t0_<n> per gag
constexpr const char *SFX_TAP     = "tap";
constexpr const char *SFX_FULL    = "charge_full";
constexpr const char *SFX_SLEEPY  = "sleepy";
constexpr const char *SFX_BUTTON  = "button";
constexpr const char *SFX_WHOOSH  = "whoosh";
constexpr const char *SFX_MUNCH   = "munch";        // the sidekick's moment, ~5.4 s into each attract loop
constexpr const char *SFX_MILESTONE = "milestone";
constexpr const char *SFX_MOONFULL  = "moonfull";

// ---- music: /oracle/music/<id>.wav (streamed, looped) ----
constexpr const char *MUSIC_IDLE  = "idle";
constexpr const char *MUSIC_FULL  = "idle_full";    // after the meter is full (falls back to MUSIC_IDLE)

// Bowl keys used in captions.ini: [bowl] common/rare/legend (+ _color, _shape)
constexpr const char *BOWL_KEY[4] = {"common", "common", "rare", "legend"};

}  // namespace cue
