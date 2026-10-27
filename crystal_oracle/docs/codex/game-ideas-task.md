# Codex task: in-depth game designs for the Dragonglow website

## What this is
"Dragonglow: The Sleepy Candy Dragon" is a family Halloween prop. On Halloween night children
("travelers" from other worlds, in any costume) set an NFC crystal on a stone plinth (the Wakestone).
An iPad wakes Butterscotch, a sleepy candy dragon who guards a hoard of candy in a cave, and he sends
them to a prize bowl (Goblin Trick / Hoard Coin / Golden Star / Dragon Gem). Each crystal is a piece of
his crystal egg; it keeps one visit per year on the tag, so a crystal climbs ranks over the years
(Sparkfinder → … → Keeper of the Hoard). Snuffwick, a teacup goblin, is the pest who tries to snuff the
**Hoard Beacon** (the cave's iron lantern). Each crystal also links to its own web page
(`halloween.ciancio.cloud/c/<id>`) showing its history; families can nickname it.

A small web storybook lives at `halloween.ciancio.cloud`. Yesterday we added a test-build games hub at
`/play/`, all centred on **the Hoard Beacon**: a shared 2,000-spark meter for the whole neighbourhood
that every game feeds, which unlocks a bonus page when lit.

**Your job: design, don't code.** Come up with deeper game ideas than the five we have, and push the
existing five further. We want things a family would come back to across October, on Halloween night
itself, after Halloween, and next year, built around the beacon and the lore, not just "your crystal".

## Read these first (read-only; do not edit anything outside this folder)
- `../../STORY.md` — the creative spec: characters (§3), lore (§4), crystal colours (§5), ranks (§6),
  prizes/tricks (§7), the 2026 chapter and the "Hooks for later years" (§8), counters/milestones (§10),
  the style guide (§11). Respect it: kids are **travelers**, never "wizards"; the egg, not a Moonstone.
- `../../../site/index.html` — the storybook (8 pages).
- `../../../site/play/` — the current hub and games: `index.html`, `play.js` (shared helpers: beacon
  API, chimes, per-browser flags), `beacon.js`, `sky.js`, `riddle.js`; and `../../../site/hunt.js`.
- `../../../site/server.js` — the tiny Node back end (`/api/beacon`, `/api/beacon/sparks`, `/api/sky`,
  `/api/crystal/<id>`, nicknames).
- `../../../PROJECT-NOTES.md` — the "2026-10-27 (web games test build)" changelog entry (near the top).
- Art you can reuse (`../../../site/img/`): bg_cave, bg_cave_lit, bg_starburst, bg_wakestone,
  butterscotch_{delight,puff,sleep,surprise,wave,wink,yawn}, chapter_egg, hardcover, print_keyart,
  prop_{coin,crystal,gem,hat,star}, snuffwick_{base,shy,sneeze,sock}, fx_bubble. New stills can be made
  with ChatGPT (one still per prompt, about 2 minutes each); voice lines with ElevenLabs
  ("Butterscotch" voice). No video generation.

## Constraints
- Audience: children about 4–12 with a parent's phone; must be playable one-handed on a phone in under a
  minute a session, readable by a 6-year-old, with a parent able to help an under-5.
- No accounts, no email, no personal data. The crystal id is a secret key (it opens the crystal page and
  its nickname) — it must never be shown to other players. Playing must work without a crystal.
- Back end is one small Node file with JSON files on disk; per-IP rate limits; anything shared must be
  cheat-tolerant (a cheater should only be able to nudge things a little, never ruin it for others).
- The Halloween-night station is **offline** (iPad + plinth); it can only exchange data with the site
  through files copied after the night, plus anything readable by the crystal link itself.
- The crystal tag has pages 30–39 reserved for future data (40 bytes) if a design truly needs on-tag
  state; prefer web-side state.
- Tone: cosy, funny, never scary; Snuffwick is mischievous, not evil. No ads, no purchases, no
  leaderboards that shame kids; cooperative > competitive.
- One person maintains this in spare time: every idea needs an honest size (days of work) and the art /
  voice it would need.

## What to deliver
Write **`game-ideas-result.md` in this folder** with:
1. **A short critique of the five current games** (Relight the Beacon, Crystal Sky, Goblin Hunt,
   Butterscotch's Riddle, the shared meter) — what's thin, and one or two concrete ways to deepen each.
2. **6–8 new in-depth game designs.** For each:
   - name + one-line pitch;
   - the core loop (what the child does, second by second) and how a session ends;
   - progression: what changes on day 2, day 10, after Halloween, next year;
   - how it feeds or uses **the Hoard Beacon** and the neighbourhood-wide meter;
   - how a crystal (optional) changes it, without exposing ids;
   - lore fit (which characters, which STORY.md hooks it advances);
   - server / data needs (new endpoints, what's stored, how cheats are bounded);
   - art + voice list (reuse vs new stills, number of voice lines);
   - size estimate (days) and the riskiest part.
   Aim for range: something for Halloween night itself (families waiting in line, or right after
   visiting), something across October (a daily or weekly rhythm), something for the off-season, and
   something that makes **next year's** visit matter more.
3. **A season arc**: how 2–4 of these chain together into a story across October → Halloween night →
   November → next October (e.g. the beacon lit by the neighbourhood changes what Butterscotch says on
   the night, or next year's chapter).
4. **Your top 3 picks to build next**, with why.

Be specific and concrete (actual mechanics, numbers, example lines in Butterscotch's voice), not generic.
Keep the whole file under ~4,000 words. Do not write or change any code.
