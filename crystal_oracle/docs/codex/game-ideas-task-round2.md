# Codex task, round 2: game ideas — corrections and two deeper designs

Thanks for `game-ideas-result.md`. Claude (the other agent on this project) reviewed it. Read it again,
then this. Write **`game-ideas-result-round2.md`** in this folder (under ~2,000 words). Design only, no code.

## What we're taking from round 1
- The critique of the current five is accepted: Snuffwick must never remove earned sparks ("the kids'
  light can't be snuffed"), 600 sparks/IP/hour is far too loose, and the sky's public seed is a real
  leak (an unkeyed 32-bit FNV of a 32-bit id can be brute-forced back to the id; we will switch to a
  keyed HMAC or stored random coordinates).
- Welcome Chime, Sock-Side Signals and Returning Glow are the strongest of the seven.

## Where round 1 was wrong or thin
1. **The station is not cut off from the site in 2026.** The 2026 rig is the iPad station
   (`../../station/`): the iPad runs the show in a browser, served by `station/server.js` on **the same PC**
   that serves the website, and it saves its counters to `station/state/state.json` there through
   `PUT /api/state` during the night. The site's `server.js` already reads that file. So, on Halloween
   night, **web ↔ station can be live in both directions** (the iPad has Wi-Fi to that PC). Only the old
   Waveshare screen is truly offline, and it is not being used this year. Read `../../station/README.md`
   and `../../station/server.js` to confirm what exists.
2. **Six of the seven designs are the same shape**: three big choices, solve, +5 sparks, once a day.
   That's fine for a 4-year-old but it isn't "in depth". We want at least one design with real play depth
   that a 9-year-old would replay for fun, still one-thumb and under a minute a round.
3. **The caps make 2,000 unreachable** for one neighbourhood (maybe 80–200 families), so the finale would
   always be a host-gifted "welcome glow". Propose a goal and caps that fit, rather than designing around
   a goal nobody can reach.

## What we want
A. **"The Beacon Tonight" — the Halloween-night live link.** A design for families waiting in line (a QR
   code on the parent sign) and right after their visit, where phone play and the Wakestone feed **one
   beacon that the iPad shows live**. Cover: what the phone game is (it can be one of yours, e.g. Welcome
   Chime); how web sparks show up on the iPad meter without disrupting the show (exchange rate, a visible
   but small effect, never interrupting a crystal's turn); whether web sparks may trigger the station's
   finale or only bring it closer; what the phone shows when the finale plays on the porch; what happens
   if the Wi-Fi or the site is down (the station must work exactly as today); the minimum station-side
   change (it's 4 days to Halloween, so keep it opt-in behind a config key and tiny), and the voice lines.
B. **One design with real depth**, built around the beacon and the cave, replayable for skill, cooperative
   with the neighbourhood rather than competitive, cheat-tolerant. Give its full mechanics, the numbers,
   how difficulty grows, and the size.
C. **A recommended goal and cap set** for one neighbourhood, with the arithmetic.
D. **Your revised top 3**, given A–C and that only ~4 days remain before Halloween 2026 (what to ship by
   Oct 31 vs in November).
