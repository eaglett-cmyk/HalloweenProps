# Crystal Oracle — station web app (the show on an iPad)

The Crystal Oracle's show as a web page: the same story, art, voice, words, odds and tag
layout as the Waveshare screen firmware (`../firmware/crystal_oracle/`), running in a browser —
meant for an iPad in Guided Access on the porch. The plinth stays exactly as built (AITRIP
ESP32 + PN532 in the v7 print); only its firmware link changes from ESP-NOW to **Wi-Fi +
WebSocket** (`../firmware/crystal_plinth/plinth_wifi.h`, `PLINTH_LINK_WIFI 1`). The packets
are unchanged (`crystal_link.h`, `../docs/LINK-PROTOCOL.md`); they ride a WebSocket instead of
the radio.

```
  iPad (Safari, this app) ──http──> station server (this PC, node server.js, :8460)
          │                             serves index.html + js/ and /oracle/* = ../sd_card/oracle
          └──ws://<plinth>:81/──> plinth (AITRIP, PLINTH_LINK_WIFI 1) ── PN532 ── crystal
```

Status (2026-10-25): **working on the iPad (Safari, Home Screen app) with the real plinth** on the
house Wi-Fi: crystals read, written, provisioned and formatted from the host menu; batch
provisioning for a box of new crystals. The Waveshare rig stays the Halloween 2026 production
path; this is the iPad alternative. Plinth IP 192.168.1.107 (no reservation yet); the mDNS name
did not resolve from the wired PC, use the IP.

## Files

| File | What |
|---|---|
| `server.js` | dependency-free node server: the app, `/oracle/*` from `../sd_card/oracle` (or `STATION_ASSETS`), `/api/index` (asset list), `/api/state` (counters, `state/state.json`), `/api/log` (`logs/visits_<year>.csv`, `logs/events.log`) |
| `index.html`, `style.css` | the page: an 800×480 canvas scaled to the screen, the start overlay, the host menu styles |
| `js/main.js` | boot: card index, config.ini, captions.ini, counters → "Tap to begin" (audio needs a gesture) → fullscreen, wake lock, plinth link, show; keyboard shortcuts for PC testing |
| `js/show.js` | **the director**, a port of `show.cpp`: steps, voice sequencing, flows (attract, chapter, crystal, no-crystal, finale), overlays, host badges |
| `js/fx.js` | the drawing primitives of `fx.h` on Canvas 2D (glows, sparkles, gems, meter, bowl icons, text, caption band…) |
| `js/media.js` | the asset index, OMJ clips decoded frame by frame (`createImageBitmap`), stills, background prefetch |
| `js/audio.js` | Web Audio: music bed (ducked under voice), voice lines, preloaded sfx, generated chimes |
| `js/link.js` | the plinth over WebSocket: binary codec of `crystal_link.h`, HELLO/PING, write resends, presence resync, simulated crystals |
| `js/token.js`, `js/oracle.js` | ports of `token.cpp` (page map, classification, write lists, prep) and `oracle.cpp` (odds, pacing, rush, groups) |
| `js/settings.js`, `js/ini.js`, `js/story_defaults.js` | config.ini + overrides, the words (+ no-button overrides), crystal colours, counters (localStorage + server) |
| `js/service.js` | the host menu (DOM): Tonight, Crystals (read test + prep), Reader, Sound & mode, Simulate, Log |
| `js/input.js`, `js/vlog.js` | taps / long-press as `touch::Gestures`; the visit CSV + event log |
| `fake_plinth.js` | a pretend plinth (node, WebSocket on :81) with console commands `fresh / ours / played / foreign / bad / lift / button`, for testing without hardware |

## Run it

1. Build the card once if `../sd_card/oracle` is missing: `python ../tools/build_assets.py`
   (the server can also point at a real card: `STATION_ASSETS=E:\oracle`).
2. On the hosting PC: `node server.js` → `http://<PC IP>:8460/`. Windows Firewall must allow
   node on 8460 for the iPad to reach it (first run prompts; or add an inbound rule).
3. Plinth: copy `../firmware/crystal_plinth/wifi_secrets.example.h` to `wifi_secrets.h`, fill in
   the Wi-Fi name / password, set `PLINTH_LINK_WIFI 1` at the top of `crystal_plinth.ino` (or
   `-DPLINTH_LINK_WIFI=1`), flash the AITRIP (hold BOOT). The serial console prints the IP;
   `http://<plinth IP>:81/` answers a JSON status from any browser on the same Wi-Fi. The blue
   LED: slow blink = no browser connected, steady = linked, flicker = linked + a crystal on it.
4. iPad: Safari → `http://<PC IP>:8460/` (plain http — Safari refuses a `ws://` plinth from an
   https page). Enter the plinth address (`dragonglow-plinth.local:81` or its IP), **Tap to
   begin**. For the night: Share → *Add to Home Screen*, open it from there, then triple-click
   the side button for **Guided Access**, and set Auto-Lock to Never.
5. Host menu: hold the **top-left corner** of the show for 3 s (same as the screen).

No hardware: `node fake_plinth.js` (port 81 may need admin on Windows: `FAKE_PLINTH_PORT=8181`),
then use `127.0.0.1:81` (or `:8181`) as the plinth address and type `fresh`, `ours`, `lift`… in
its console. Keyboard in the app: `m` menu, `f` fresh crystal, `r` returning, `p` already
played, `x` foreign, `u` unreadable, `b` button, `l` lift.

## What is the same, what differs

Same as the firmware: every cue, timing (DESIGN.md §5.1), the words and their no-button
overrides and voice swaps, the odds and pacing, ranks, rush mode, groups / circle, the meter and
finale, the token page map and all four prep operations, the visit CSV, simulated crystals.

Differs because it is a browser:
* **Counters and settings** live in the browser (localStorage) and on the station server
  (`state/state.json`), not on a TF card. Host-menu setting changes are overrides on top of
  `config.ini` ("Forget my changes" drops them).
* **Audio needs the first tap** (the start overlay). Volume = Web Audio gain × the iPad's own.
* **Clips** are decoded per frame from the OMJ file in memory (~100 MB for the whole card,
  fetched in the background after start; the attract loop first). Stills and voice likewise.
* **The plinth link** is a WebSocket: no channel / group discovery, a connected socket is the
  link; the app reconnects by itself. Both devices need the porch Wi-Fi.
* **No hardware badges** for the TF card: "NO TF CARD" means the server found no assets.
* The LED ring commands are still sent (the v7 plinth has no ring and ignores them).

## Gotchas

* Port **81** for the plinth is fixed in `plinth_wifi.h` (`WS_PORT`); the app adds `:81` when
  the address has no port.
* Safari blocks `ws://` from an `https://` page: serve the app over **http** on the LAN. Do not
  route it through Cloudflare.
* mDNS (`dragonglow-plinth.local`) works from an iPad on the same network; if it does not
  resolve, use the IP (the plinth prints it; give it a DHCP reservation on the UDM).
* The server binds all interfaces on 8460. It is not part of the merged VMS server and not
  watched by the site watchdog; start it by hand (or add a scheduled task) when the iPad rig is
  in use.
