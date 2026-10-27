# Game ideas: the shortlist (Claude + Codex, 2026-10-27)

Two rounds with Codex (`game-ideas-task*.md` → `game-ideas-result*.md`, full designs there), reviewed by
Claude. Round 1 gave 7 designs + a critique; Claude pushed back (the iPad station is on the same PC as the
site, so Halloween night can be live; six of seven designs were the same "3 choices, +5 sparks" shape;
the caps made 2,000 unreachable), and round 2 fixed those.

## Fix first (bugs in what is live now)
1. **Sky leak (real).** `/api/sky` seeds are `fnv1a('dragonglow/sky/v1:' + id)`: an unkeyed 32-bit hash of
   a 32-bit id, so anyone can brute-force all 4.3 billion ids offline and map stars back to crystal ids,
   which open crystal pages and let a stranger rename the nickname. Fix: HMAC with a random secret kept in
   `site/data/` (or stored random coordinates). Small; needs a VMS restart.
2. **Snuffwick takes −5 sparks** in Relight the Beacon. The lore says the kids' light can't be snuffed.
   Make him cover the glass (the next spark is hidden until you shoo him) instead.
3. **Caps are far too loose** (600 sparks/IP/hour = 30% of the goal). Server picks the award (5 per
   completed game), clients no longer send amounts.

## For Halloween night (4 days): "The Beacon Tonight" + Welcome Chime
- QR on the parent sign → **Welcome Chime**: copy a 3-picture-note phrase (Simon-style, no fail state),
  ~30 s, +5 sparks; after the visit, "send a goodbye" (4 notes) for +5 more.
- **One beacon for the night: 500 sparks = the station's 50 points.** A Wakestone point = 10 sparks; phones
  can add at most 100 sparks (10 points, 20%) for the whole event.
- **The iPad shows phone light live**, opt-in `[station] web_beacon = no` by default: polls every 5 s,
  applies credit only between turns (attract / after a turn), a quiet "Pocket welcomes +1" pulse, never
  interrupts a crystal. Phone credit kept separate from `moonPoints` (the site already reads that ×10).
- **Web can bring it to 49/50, never fire the finale**: the next real crystal does. "Fill the Beacon"
  stays. Phones see "The beacon is lit on the porch! Look up!" when the station flips `moonFull`.
- Site down → the station is exactly as today. Key off if rehearsal shows any doubt.
- Claude's caution: the iPad rig itself has not been run on a real iPad + plinth yet. Rehearse that first;
  the bridge is the first thing to drop if time runs out. Chime alone (site only) is safe to ship.

## November and later
- **Cave Lightpaths** (the deep one, 6–9 days): 4×4 cave grid, plan a glow route from the beacon to a
  world-door within an energy budget; lanterns +2, near door +2 / far door +4, +1 per unused energy, rubble
  costs 2. Gentle / Winding / Deep bands, 12 authored boards each, a shared daily cave. Better scores earn
  private ribbons only; a valid delivery earns the same 5 sparks. Replayable for a 9-year-old.
- **Sock-Side Signals** (3–4 days): match the sock's little tapping pattern, once a month through winter;
  keeps the 2027 "Egg in the Sock" surprise without showing the egg.
- **Returning Glow** (4–6 days, needs the post-night log import): a keepsake now, an "old glow / new glow"
  reveal after next year's visit.
- Also in round 1, lower priority: Beacon Post (route puzzle to a world-door), Snuffwick's Shade Shop
  (place a screen: light the path, keep his nap corner dark), The Feast Has Room (seating puzzle),
  Dreamlight Shelves (winter picture puzzles).

## Deepening the current five (round 1)
- Goblin Hunt: one clue + one page per session, "Show me" hint after 8 s, 5 sparks per find, three
  authored weekly sets.
- Riddle: pair each riddle with a cave clue to tap; fewer reading-heavy wrong answers.
- Crystal Sky: a 30 s "join three stars" constellation task; milestone pictures.
- Shared meter: milestones at 25/50/75% reveal a path, a warm cave, the feast; archive each season.
