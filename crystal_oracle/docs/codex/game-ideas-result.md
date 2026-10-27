# Dragonglow: games that give the beacon a life

Design proposal, 27 October 2026. The return invitation should be “the cave has changed,” with a satisfying small job today. Seven designs below share one beacon, a few seasonal scenes, and no obligation to play every day.

## What the current five need

| Current experience | What's thin | Concrete next step |
|---|---|---|
| **Relight the Beacon** | The 45-second tapping round has urgency but little choice. Its 40-spark local blaze can be confused with the shared 2,000-spark goal. Snuffwick removes five earned sparks, although the story says travelers' light cannot be snuffed. | Let him cover the glass temporarily, hiding the next incoming spark until tapped; banked sparks stay safe. Offer two large lanes: catch a steady spark or catch a slower, wobbling bubble worth three. Label the round “Your spark basket,” with the shared beacon shown separately afterward. |
| **Crystal Sky** | A lovely record, primarily a viewer. Without a crystal or imported visits, there is little to do; rank-sized stars favour long ownership. | Add a 30-second “join these three lights” constellation task using anonymous decorative stars. Reveal a neighbourhood picture at beacon milestones. Keep every real crystal selectable through a large accessible list, without making high ranks better at playing. |
| **Goblin Hunt** | Five fixed positions are exhausted once; the 25 sparks arrive only after all five. Navigating the book can exceed a minute. | Make each session one illustrated clue and one page, with a “Show me” hint after eight seconds. Five discoveries assemble a tiny prank scene; rotate three authored weekly sets rather than moving everything randomly. Award five sparks per discovery, still 25 total. |
| **Butterscotch's Riddle** | Many questions are generic word jokes; three guesses guarantee success without much discovery. Browser-only candles are fragile, and catch-up can post repeated awards from other browsers. | Pair each riddle with a visible cave clue: “What woke me?” then tap crystal, pillow or coin. Five solved clues assemble a weekly picture; missed days stay playable. Replace reading-heavy wrong-answer text with “Look here!” and a highlighted clue. |
| **Shared meter** | It is an accumulator and one locked page, rather than an unfolding cooperative story. The current 600 sparks/IP/hour permits one source to supply 30% of the goal in an hour. Calendar-year resets also cut across winter continuity. | At 500/1,000/1,500/2,000 sparks reveal a path, warm cave, feast setting and sock teaser. Keep reached milestones permanent. Archive each October season, carrying its story through September; a fresh October is a new chapter, never a loss. |

## Shared rules and honest boundaries

These are proposed changes, not existing capabilities. Keep the 2,000-spark web goal. Each accepted new-game completion earns **five sparks**, once per game per Chicago day; repeats remain fun practice. Store season, game and puzzle receipts so retries cannot double-credit. No speed or accuracy multiplier; hints earn equally.

Use a random browser token solely for game progress, no profile or fingerprint. Browser progress stays local; optional crystal progress lives privately in `crystals.json`, capped at 3,000 entries. Clearing storage loses guest keepsakes; all story scenes remain available. Never treat that token or possession of a crystal link as proof of a physical visit.

**Cheat envelope:** replace the unrestricted amount-posting path for all games, including legacy ones, with server-selected awards. Across all games allow at most 20 credited sparks/browser/day, 20/IP/day and 100/IP/season. Persist rotating, keyed IP digests and counters only as needed for those limits; discard at season end. Shared networks may reach the limit: say “Your game is saved. The beacon has today's sparks.” Keep playing unrestricted. Limit all web additions together to 100 sparks/day, including after the beacon lights. This bounds one IP to 5% of the season goal and even distributed cheating to one visible daily step. There is no complete Sybil defence without identity; no shared deletions, penalties, votes controlling canon, or scarce prizes make that limitation acceptable.

With these conservative caps, sparse traffic may miss 2,000. Import genuine station points at ten web sparks each; if still short, the host releases a clearly labelled “Butterscotch's welcome glow” at season close so every family gets the ending. This is an authored finale, not invented participation. Test traffic uses a separate season.

**Offline truth:** station sparks remain its own nightly goal (default 50), with its guaranteed finale. The present server reads a local station-state path; this is not a live connection to the offline Halloween rig. Copy the saved night snapshot and visit logs afterward. Import station points once per event using a stored high-water count; logs separately populate crystal histories, never another spark award. No phone game changes tonight's station dialogue, rank, odds or candy. No tag-page changes; pages 30–39 stay reserved.

**Private by design:** never publish ids, nicknames or links in shared scenes. Replace the sky's deterministic FNV-derived public seed with independently random stored coordinates; scrambling a secret is insufficient protection. Show “Your crystal” rather than a raw id in game labels. Public results are aggregate scenes only.

Every game uses three or fewer large choices at once, thumb-reachable controls of at least 48 CSS pixels, icons plus short words, optional tap-to-hear instructions, captions, and reduced-motion/static alternatives. No microphone, camera, location, free-text submissions or real-world scavenging. Play finishes locally without connectivity; a queued receipt adds sparks only after server acceptance. Sound never carries the only clue.

## Seven new designs

### 1. Beacon Post — deliver a little light

**Pitch:** Solve a tiny route puzzle to send the beacon's invitation toward another world. Best October rhythm.

**Session:** 0–5 seconds: see a closed castle, star or jungle door pictured on the far side of a three-tile path. “Help my light find the door!” At 5–25 seconds tap each tile to rotate its chunky channel, joining the beacon to the door. One bend is already correct; a clear preview shows light stopping at the first gap. At 25–35 seconds press “Send”; the glow travels, and a matching traveler silhouette peeks through a window. Two failed sends reveal a correct bend. End with an invitation stamp and five sparks, usually under 40 seconds. Invitations wait at closed doors; nobody crosses before Halloween.

**Progression:** Day 2 changes destination and bends. Day 10 reveals that all three routes meet at the cave. Four weekly postcards provide the authored variety; later days remix their layouts. After Halloween, play thank-you deliveries. Next October the saved neighbourhood postcard returns beside a new route; a future stuck-door chapter can reuse this system in 2028.

**Beacon/crystal:** At 500 sparks the public path gains its first stepping stones; 1,000 adds all three signposts. Optional crystal colour tints the traveling glow; guests choose Dragonberry, Starfrost or Rainbow Fizz equally. No public ownership markers.

**Lore:** Advances §4's crossroads and §8's 2028 door hook without prematurely opening that story. Butterscotch: “Beacon post! Mind the bend. I didn't.”

**Data/cheats:** Proposed `GET /api/post/today` returns a dated puzzle; `POST /api/post/finish` checks tile orientations and issues the shared capped award. Store only a bounded receipt and optional private stamp bits. Correct solutions can be copied; award limits make that harmless.

**Production:** Reuse cave, lit cave, crystal and wave; **one new still**, a three-door postcard, with channels drawn as interface shapes. **Six new Butterscotch lines**. **4–6 days**, including four postcard layouts and phone QA. Risk: making path connections obvious on a narrow screen.

### 2. Snuffwick's Shade Shop — brighten the cave, spare his nap

**Pitch:** Arrange a screen so the beacon lights the path while Snuffwick keeps one cosy dark corner.

**Session:** 0–6 seconds: see the beacon beam crossing a three-position folding screen. Snuffwick curls up on the right; the traveler-path icon sits below. At 6–25 seconds tap one of three screen positions and one of two screen heights; a preview shades different patches. At 25–35 seconds tap “Try it.” Success means the path icon glows and the sock-shaped nap patch stays shaded. Otherwise the blocked patch wiggles and a hint appears. By 45 seconds “Help me” sets one choice. End with a goblin sigh, five sparks and the finished room.

**Progression:** Day 2 changes the nap patch; day 10 adds a harmless gap in the screen, still only six combinations. October practice is framed as planning a welcome corner. After the finale, Snuffwick officially moves in as Prankster. November adds a sock rest; next year the same corner becomes his egg-sitter nook, still without exposing its occupant early.

**Beacon/crystal:** 1,000 sparks reveals the community's completed cosy corner; individual choices cannot remove it or dim the meter. Crystal users privately choose a familiar glow colour; guests get identical choices. Nobody votes on whether Snuffwick deserves a home.

**Lore:** Resolves the reason he snuffs lights, supporting §8's invitation and 2027 egg-sitter role. Butterscotch: “Bright path. Cosy goblin. Room for both!”

**Data/cheats:** Proposed `GET /api/shade/today`, `POST /api/shade/finish` with two enumerated choices. Server validates the six-state solution table. Store completion receipts and private room stickers; public room state derives solely from beacon milestones. Shared caps apply.

**Production:** Reuse cave, shy/base/sock Snuffwick, wink; **one new still**, a rounded plum folding screen. **Five voice lines**, Snuffwick uses existing squeaks. **3–5 days**. Risk: communicating light and shade without colour-only clues; use sun/path and sock icons plus solid outlines.

### 3. The Welcome Chime — a pocket game for the queue

**Pitch:** Put three picture-notes in order to play a welcome from your world. No music skill required.

**Session:** 0–5 seconds choose castle, star or jungle icon, explicitly a pretend world rather than costume data. At 5–12 seconds three large picture pads light in sequence: leaf, moon, bubble. At 12–30 seconds tap the same pads; “Again” replays the demonstration. Wrong taps simply repeat the next pad. The picture sequence remains available in an untimed helper mode. At 30–40 seconds hear the completed chime and see a glow reach the beacon. “Ready for the Wakestone!” ends the round with five sparks. A grown-up can close it immediately when the line moves.

**Progression:** Day 2 supplies another three-note phrase; day 10 offers an optional four-note puzzle with the same award. Halloween gets a welcome arrangement; after Halloween the chime becomes a goodbye. Next October it plays beside the new chapter's motif. No escalating reaction-speed requirement.

**Beacon/crystal:** At 1,500 sparks the hub adds a combined three-world welcome melody, assembled from fixed authored parts. Each player hears the full arrangement; no noisy live multiplayer. Optional crystal selects a private instrument sound; guests choose it too. Nobody must show a phone at the station.

**Lore:** Supports travelers from every world and the §10 crystal-circle theme. Butterscotch: “Welcome chime! Even my hat heard that.”

**Data/cheats:** Proposed dated chime descriptor at `GET /api/chime/today`; `POST /api/chime/finish` carries the sequence and receipt. Store receipt only, plus optional private favourite instrument. Exact taps are discarded. Shared caps bound scripted completions; no live queue tracking.

**Production:** Reuse cave, delight, bubble and crystal. **Zero new stills**, three large drawn icons, synthesized chimes. **Four voice lines**. **2–3 days**, after shared infrastructure. Risk: sound activation on phones; first tap starts audio, and picture-only play always works.

### 4. The Feast Has Room — solve the welcome table

**Pitch:** Fit a place for every traveler, including one teacup goblin, in a tiny picture puzzle.

**Session:** 0–6 seconds: three place cards show simple needs: dinosaur needs a wide gap, robot needs a round mat, goblin needs a shady seat. At 6–30 seconds tap a card then one of three seats; tapping an occupied seat swaps cards. Preview icons show which needs are met. At 30–40 seconds press “All welcome!” Correct placement produces a short silly feast tableau. After two tries the next correct seat pulses. End by 50 seconds with a table tile and five sparks; no child is excluded by a wrong arrangement.

**Progression:** Day 2 changes seating clues; day 10 adds one fixed neighbour relationship, still three movable cards. October prepares illustrated place settings; the feast itself appears after the Halloween finale. November leaves a thank-you table picture. Next October one new setting is reserved for a very small guest, revealed as Toffee only in the 2027 chapter.

**Beacon/crystal:** At 1,500 sparks the community table receives all its settings; 2,000 lights the feast tableau. Play can preview the completed puzzle regardless of meter progress. Optional crystal privately frames the family’s table tile; guests keep it on that browser. No digital prizes imply extra candy.

**Lore:** Develops §4's shared hoard, §8's welcome for Snuffwick and the 2029 All-Night Feast hook. Snuffwick sits beside his place setting; he never holds or reaches for candy. Butterscotch: “One more seat! My hat can scoot.”

**Data/cheats:** Proposed `GET /api/feast/today` supplies three constraints; `POST /api/feast/finish` submits the permutation. Store receipts and six private tile bits. Public decorations are predetermined, never player-authored. Shared caps apply.

**Production:** Reuse delight, hat, shy goblin and cave. **Two new stills:** empty rounded table and three traveler place-card portraits together on one sheet. **Six voice lines**. **4–6 days**. Risk: clues becoming reading tests; every constraint needs a clear picture demonstration.

### 5. Sock-Side Signals — listen without opening

**Pitch:** Match a tiny tapping pattern from the mysterious sock; help its proud keeper settle it comfortably.

**Session:** 0–6 seconds see Snuffwick hugging his warm sock. “What was that little tap?” At 6–12 seconds two to four soft pulses appear as expanding circles, with optional tapping sounds. At 12–30 seconds choose the matching strip from three large dot patterns. A miss replays the signal; the helper highlights one pulse. At 30–40 seconds choose folded cloth or a little cushion to support the sock, both valid. It settles with one answering tap. End with five sparks and a signal stamp. Never shake, squeeze or unwrap it.

**Progression:** Day 2 changes the pattern. Day 10 reveals a compiled strip of three signals, not an anatomical clue or a countdown. November introduces the sock scene to everyone after the seasonal ending. Winter brings one authored signal monthly, replayable anytime. Next October Snuffwick carries it into the 2027 chapter; only Halloween's chapter reveals the egg and Toffee.

**Beacon/crystal:** The lit beacon unlocks the existing sock teaser early. At the season-close welcome glow everyone can reach it. Signals reuse the beacon's warm pulses without spending sparks or implying the egg will fail without visits. Optional crystal collects private signal stamps; guests collect locally.

**Lore:** Directly advances §8's Egg in the Sock, preserving both the surprise and Snuffwick's caring role. Butterscotch's line comes from a saved Halloween message: “Soft taps! That's a very lumpy lullaby.”

**Data/cheats:** Proposed `GET /api/sock/signal` selects the published monthly pattern; `POST /api/sock/finish` validates the pattern choice. Store receipts and stamp bits. Schedule revelations by authored date, never player votes or volume; shared caps apply.

**Production:** Reuse sock, shy, bubble, cave and sleep. **One new still**, folded indigo cushion; **five recorded-message lines**. **3–4 days**. Risk: accidentally revealing the egg through new art; keep contents entirely inside the existing sock silhouette.

### 6. Dreamlight Shelves — keep a small kindness through winter

**Pitch:** Finish a three-piece dream picture while Butterscotch sleeps; leave it on the beacon's memory shelf.

**Session:** 0–5 seconds pick a picture frame: floating hat, winding path or cosy cave. At 5–30 seconds fill three holes by tapping matching pieces from a two-choice tray; pieces differ by silhouette, not just colour. Each placed piece releases a gentle sparkle. At 30–40 seconds tap the finished picture; it becomes a small shelf ornament. End with five sparks for the active season and a saved picture. Use chimes only during play so Butterscotch stays asleep 364 nights.

**Progression:** Day 2 varies the holes; day 10 offers a new frame after three completed pictures, with no consecutive-day requirement. November opens the shelf. December–September rotates four reusable scene sets, with missed sets always available. Next October the shelf appears in the “last season” album, and one ornament decorates the new chapter's opening web scene.

**Beacon/crystal:** After lighting, the meter stays lit; extra sparks make a brief welcome pulse, never a maintenance bill. At most one new public shelf ornament appears per month, using an authored schedule once the finale is available. Its appearance cannot be rushed by bots. Optional crystal retains up to twelve private ornament bits across devices; guests keep them locally, with no disadvantage in public story access.

**Lore:** Extends §4's sleeping dragon and accumulated gifts without opening off-season world doors or waking him. Opening text: “Butterscotch is dreaming. Let's leave a little light.”

**Data/cheats:** Proposed `GET /api/dream/picture`, `POST /api/dream/finish` validate three piece indexes. Store bounded picture bits and capped receipts; public shelf has no uploads. Shared limits apply even to off-season sparks.

**Production:** Reuse sleep, hat, cave, lit cave and bubble. **Zero new stills**; three-piece crops and four layouts from existing images. **Two saved-message voice lines**, optional. **3–5 days**. Risk: making an appealing puzzle from recognisable crops without fine detail.

### 7. The Returning Glow — a promise that survives a year

**Pitch:** Build a tiny beacon memory now, then discover a new piece beside it after next year's real visit.

**Session:** 0–8 seconds choose a remembered scene: beacon, goblin or crossroads. At 8–30 seconds complete its three-piece border using tap-to-place shapes. At 30–40 seconds choose a picture seal, with no typed promise. The memory closes as a keepsake; five sparks join the beacon. End: “Safe till next Halloween. No hurry, traveler.” A guest gets the complete keepsake too.

**Progression:** Day 2 offers another scene; day 10 combines three memories into a short flipbook. After Halloween an imported successful visit adds that year's genuine prize icon privately. Next October the old memory sits beside a new empty frame. After the next Halloween log import, a returning crystal gets an “old glow / new glow” reveal and its imported rank. Missed years leave memories intact; browser games never advance ranks. Guests can choose a pretend souvenir border, clearly separate from recorded station history.

**Beacon/crystal:** Each season's keepsake includes its archived neighbourhood beacon scene. New visitors see that same community history. Returning crystals supply continuity, not stronger sparks, exclusive puzzles or better candy. The reveal happens on the website after import; the offline station's existing returning greeting already makes the visit special.

**Lore:** Develops §4's remembering eggshell and §6's total-visits ladder: visit two is Apprentice, visit three Glowcaster, visit five Worldwalker, visit eight Keeper. Keep Keeper secrets out of public games. Butterscotch: “Old glow! New adventure! I saved your place.”

**Data/cheats:** Proposed `POST /api/memory/finish` saves three scene bits and a seal; private crystal responses join these to imported yearly history. Guest bits are local. A claimed visit never creates history: only host-copied successful-write logs do. Same-year imports deduplicate. Shared award caps apply; no new tag bytes.

**Production:** Reuse hardcover, cave, crystal, coin, star, gem and wave. **Zero new stills**, **four voice lines**. **4–6 days**, including import and year-boundary verification. Risk: explaining delayed history gracefully; show “Your visit picture arrives after the night,” never “You didn't visit.”

## A four-game season arc

**October — Beacon Post:** travelers send invitations along closed-world paths. At 500 sparks a path appears; at 1,000 the cave warms. Shade Shop is an optional parallel game explaining how a bright welcome and a dark goblin nap can coexist. Nobody crosses worlds early.

**Halloween — Welcome Chime:** families play 40-second welcomes in line or afterward. At 1,500 web sparks the combined melody appears. The offline station tells its complete 2026 story and reaches its own finale independently. Its lines need no knowledge of phone progress. Phones say “Tonight's Wakestone sparks arrive after the night,” not “live.”

**November — Sock-Side Signals:** copy logs and the night snapshot; deduplicate before adding ten web sparks per station point. At 2,000 the existing sock bonus opens; any shortfall gets the labelled season-close welcome glow. Butterscotch sleeps again; his recorded messages and Snuffwick carry the web activity. The sock answers without opening.

**Next October — Returning Glow:** display last season's beacon and private keepsake beside a new chapter frame. The 2027 Halloween chapter reveals Toffee and uses crystal glow for flying lessons, as specified in STORY.md. After log import, a second visit adds Apprentice and a second memory panel. A first-time traveler gets a full welcome. The previous beacon remains lit in its album while the new chapter starts its own goal.

## Build these three next

1. **Welcome Chime:** the smallest distinctive addition, immediately useful in the Halloween queue, with no new illustrations or station integration. Its picture mode makes the audience range practical.
2. **Snuffwick's Shade Shop:** the strongest mechanical expression of the lore. Six combinations create a real small puzzle, and success means welcoming Snuffwick rather than defeating him.
3. **Sock-Side Signals:** gives the already-built bonus page something to do after Halloween and protects the 2027 reveal. A monthly pattern is manageable for one maintainer.

Allow **3–5 shared foundation days** for bounded awards, receipts, private sky coordinates, season archives and manual-import handling, beyond the per-game estimates. These three plus foundation total roughly **11–17 focused days**; still generation is minutes per prompt, but consistency, audio editing and phone QA are included in the ranges. Recurring upkeep is about one hour a month plus the post-night import. With four days left until Halloween 2026, target foundation plus a stripped-down Chime, then build Shade Shop and Signals in November. Do not promise the whole arc this week.
