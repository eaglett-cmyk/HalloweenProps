# Round 2: one beacon, a live porch, and a game worth replaying

Design only · 27 October 2026

## A. The Beacon Tonight

**Correction confirmed.** I reread round 1, `station/README.md`, `station/server.js`, station counter/show logic, and the site's beacon reader. The iPad saves counters through `PUT /api/state`; the website already reads that PC's `station/state/state.json`. The README's production-path sentence is stale relative to this task: plan for the iPad in 2026. Live web-to-station input is proposed, not already implemented.

**Queue game: Welcome Chime.** The parent-sign QR opens “Help light tonight's beacon” without login, crystal entry or app installation. Tap to start three picture pads; watch a three-note phrase, then repeat it. A wrong tap highlights the next picture without resetting progress. Replay and a persistent picture guide are always available. First completion takes 20–35 seconds and banks five web sparks. After visiting, “Send a goodbye” offers a four-note phrase for another five. This is an honour-system invitation, not a claim to verify visits. Unlimited practice follows; closing the page never costs anything. Sound is optional, controls are at least 48 CSS pixels, and colour always has a picture equivalent.

**One meter, explicit units.** Set the shared target to **500 sparks**, equivalent to the station's **50 points**. Keep one genuine Wakestone point worth ten shared sparks. Ten accepted phone sparks supply one station-equivalent point; fractional progress waits for the next full ten. All games share a maximum **100 phone sparks per event**, so phones can contribute only ten points, or 20% of the beacon. Label the phone meter “Wakestone + pocket welcomes”; the iPad remains the Beacon.

Keep `moonPoints` strictly physical. Hold phone credit in a separate event-scoped field. The meter uses physical points plus acknowledged phone-equivalent points, capped at the goal. Never write converted web credit into `moonPoints`: the website already multiplies that field by ten, which would otherwise create feedback and double counting.

**Small, safe arrival.** Poll in the background every five seconds, with a one-second timeout and one request in flight. Stage the latest cumulative value; apply it only in attract mode or after a completed turn, with no crystal sequence, chapter, finale or host menu active. A quiet half-second pulse and “Pocket welcomes +1” appear beside the meter; coalesce a backlog into one pulse. No voice interruption, extra show step, plinth command or alteration to crystal odds, ranks or candy. Phones explain “Your light joins between turns,” never promise an immediate animation.

**Finale belongs to the porch.** Web credit can bring the meter to **49/50**, but a web arrival alone cannot start the finale. The next genuine point-awarding station interaction may cross 50 and use the existing after-turn finale scheduler. Replays that currently award no point do not qualify. When physical points alone reach 50, today's normal finale path still works. Preserve the existing host “Fill the Beacon” override. Once lit, keep it lit; later games produce a thank-you glow.

**Phone during the finale.** Poll a public aggregate view every five seconds while visible. On the station's persisted `moonFull` transition, display “The beacon is lit on the porch! Look up!” with one gentle glow and silent captions. That field is saved at finale entry, so do not claim the sequence has finished or promise synchronized audio/video. Away from the porch, “See the welcome” opens an optional recap. If the numerical target arrives before station confirmation, show “Ready for the next Wakestone spark,” not a prematurely unlocked ending. Station confirmation also unlocks the site's beacon ending when phones contributed nothing.

**Minimum station change.** One opt-in key, `station.web_beacon = no` by default; one same-origin, read-only station endpoint exposing only the site's bounded event credit; a background poller; and a small meter/finale eligibility hook. The station server reads the site's local credit file, with no dependency on the public site's process or an external network request. Persist acknowledged credit separately through the existing counter-save path. Extend the site's aggregate response with station confirmation. No new hardware protocol, socket service, write access from phones to station state, or log import is required for the beacon.

Use an explicit event ID such as `2026-10-31-live`, cumulative credit and a persisted maximum, rather than replaying increments. Duplicate polls and reloads cannot add twice. Ordinary nightly resets retain acknowledged event credit; a deliberate new event changes the ID. Disabled means the existing station path, without polls.

**Failure behaviour.** Missing, malformed, wrong-event or timed-out credit changes nothing; never block the director. Retain acknowledged credit, never retract earned light. A site outage leaves station play and its physical 50-point route intact; station counters continue saving locally as today. Loaded phone games work locally, showing “Saved here; waiting to send,” and retry the same receipt on reconnect. An unopened page during a site outage simply cannot load: the sign also says “No phone needed—your crystal helps.” Offline receipts do not claim shared credit until accepted.

Losing internet/site access is distinct from losing the iPad-to-plinth Wi-Fi: the latter already interrupts the existing reader link. This feature cannot fix that; it adds no new dependency or obstruction to today's reconnect behaviour. A stale phone view says “Last update…” and catches up later.

**Four optional phone recordings, always captioned:**

- “Three little notes. One big welcome!”
- “That's your light! It'll join between turns.”
- “Been to the Wakestone? Send a little goodbye!”
- “The beacon's shining! Hello, every world!”

Reuse existing station finale audio. New station recordings are unnecessary this week.

## B. Cave Lightpaths — replay for a better route

**Premise.** Guide a small planning glow from the beacon through the cave, collecting welcome lanterns and ending at a pictured world-door. October doors remain closed; this lays their welcome paths. In November, the paths carry dream postcards while Butterscotch sleeps.

**Board and controls.** A 4×4 grid fits above a bottom thumb-control strip. Start at the beacon in the bottom row. Tap an orthogonally adjacent cell to extend your luminous route; tap Undo to remove the last step, or Clear to restart. No diagonal travel or revisiting cells. Rock cells are impassable. Preview the entire path and its remaining energy continuously. Door cells terminate a route; press Send there to bank it. Until Send, everything is reversible.

Each normal cell entered costs one energy; rubble costs two. The beacon costs zero. Three lantern cells are worth two local points each. Two doors offer different endings: the nearer door gives two points, the farther gives four. Finishing also earns one point per unused energy. A route must reach a door within budget; collecting lanterns is optional. Local score therefore rewards balancing collection, detours and efficiency rather than simply visiting every square. Display a running projected score and the best possible score after finishing.

**Example.** With ten energy, a five-cost route collecting one lantern and reaching the near door scores 2 + 2 + 5 = **9**. A nine-cost route collecting all three and reaching the far door scores 6 + 4 + 1 = **11**. A clever eight-cost version scores **12**. Author layouts where these alternatives actually exist; reject layouts whose shortest exit also trivially collects everything.

**Round structure.** Five seconds to inspect, 40 seconds to plan and revise, five seconds for delivery and comparison: **50 seconds maximum** in standard mode. At the bell, automatically send a currently valid door route; otherwise the round ends with a harmless practice glow and “Try a shorter path.” No shared spark is lost. Show personal best, this route and the board's ceiling—no public leaderboard. “Try this cave again” preserves the board for a deliberate second attempt; “New cave” changes it. Untimed helper mode keeps the same awards and can highlight one feasible route. Pause when the browser is hidden; calling a family forward takes priority.

**Difficulty through decisions.** Three freely selectable bands:

| Band | Budget | Board contents | Main challenge |
|---|---:|---|---|
| Gentle | 12 | 1 rock, no rubble | Reach either door; find lantern detours |
| Winding | 10 | 2 rocks, 2 rubble | Trade energy against collection |
| Deep | 9 | 3 rocks, 3 rubble | Sacrifice one reward to improve the total |

Lanterns, doors, rubble and rocks occupy distinct cells. Every board has a feasible exit and at least two scoring routes. After three successful sends, suggest the next band; never force it. Start with **12 authored boards per band**, rotated/reflected where valid. Keep one common daily cave for families to discuss, plus the practice library. No unseen hazards, reaction gates, purchased moves or random changes after planning begins.

**Cooperative, cheat-tolerant reward.** A valid delivery earns five shared sparks, at most two credited deliveries per browser per Chicago day across all games; Halloween uses the stricter event pool below. Better scores earn only private route ribbons. Families improve the same cave together; nobody occupies cells or steals another route. Server validation checks the board ID, adjacency, costs, endpoint and receipt. Copying an optimal route is acceptable. Do not pretend a browser token proves a person, or try to enforce honest timing for a noncompetitive score.

**Size.** One screen, 36 layouts, five simple cell symbols, existing cave/beacon art, two optional saved-message lines, no new character animation. Estimate **6–9 focused implementation days**, including layout validation, touch accessibility and device QA. Ship in November. The route search and scoring need playtesting with children, not a rushed Halloween promise.

## C. Goal, caps and arithmetic

For this Halloween event, replace 2,000 with **500 shared sparks / 50 station-equivalent points**. Milestones at 125, 250 and 375 reveal the path, warm cave and welcome lights. Only station finale confirmation declares the beacon lit.

| Limit | Recommendation |
|---|---|
| Server-selected completion award | 5 sparks; ignore client-supplied amounts |
| Browser, across every game | 10 sparks for the Halloween event |
| IP, across every game | 30 sparks/hour; 100/event |
| All web traffic together | 100/event, including pre-night play |
| Physical station contributions | Existing point-awarding rules; no web cap |

Persist receipts and caps across restarts. Route legacy games through the same credit gate; closing only Chime's loopholes leaves the old amount-posting path exploitable. Shared-IP families may still play after credit closes. Message: “The pocket welcome is full. Your Wakestone light still counts.”

**80-family case:** assume 40 genuine point-awarding interactions and 20 browsers each completing one Chime. Physical: 40 × 10 = 400. Web: 20 × 5 = 100. Total **500**, with a real interaction supplying the final crossing. With zero phone uptake, **50 physical points** suffice. At 120–200 families this goal is comfortably reachable, likely earlier in the evening; it is a welcome milestone, not a closing-time countdown. Attendance and point-awarding interactions are assumptions, not interchangeable counts. If fewer than 40 points occur, accept an unfinished meter or use the existing explicit host finale; no routine invented participation.

A distributed bot can fill the entire web allocation but only advance the beacon 20%; no web-only finale or scarce reward follows. November can reuse 10/browser/day and 100 global/day for decorative contributions to the already-lit beacon, without reopening Halloween credit or its finale.

## D. Revised top three and release order

1. **Welcome Chime + The Beacon Tonight:** Oct 31 priority. Budget the four remaining days for bounded awards/privacy fixes, stripped-down Chime, optional bridge, and rehearsal. Verify duplicate/reload handling, finale boundaries, outages and real iPad/plinth operation. If the bridge fails rehearsal, leave its key off and ship Chime with honest status text.
2. **Cave Lightpaths:** November's main game; the strongest answer to replayable skill. Prototype the route tradeoffs before producing all 36 boards.
3. **Sock-Side Signals:** November's smaller story companion, using the existing sock silhouette and gentle monthly patterns; preserve the 2027 reveal.

Returning Glow remains valuable but follows reliable private visit-history synchronization. Also retain the accepted immediate corrections: Snuffwick never removes earned sparks, and public sky coordinates use keyed HMAC or stored randomness. Avoid making archives, new stills or year-long progression prerequisites for this week's live experience.
