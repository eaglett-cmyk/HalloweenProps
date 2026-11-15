# Hoard Chests — parts research (2026-11-12)

Companion to `DESIGN.md` (rev 2). Researched by web search and page fetches on 2026-11-12.

**How to read this:**
* A value with a source URL was read from that page or document this session.
* **unverified** means I could not confirm the value (or part number) on a maker or distributor page. Treat it
  as a starting point and **measure the part in hand before you finalise a printed part around it.**
* Mouser, DigiKey, Octopart and Amphenol's own site blocked automated fetches (403s, Cloudflare checks, timeouts)
  for most of this session. Many distributor part numbers below are therefore unverified, even where the maker part
  number is certain. Prices are rough single-unit ballparks from search snippets and memory, not quotes.

---

## A. Verified dimensions

### A1. SG90 micro servo (TowerPro and Amazon clones)

Source for the drawing values: the TowerPro SG90 datasheet sheet (drawing image) hosted by the University of Hamburg,
https://tams.informatik.uni-hamburg.de/lehre/2023ss/vorlesung/es/doc/sg90.pdf. I extracted the embedded drawing and read
the numbers off it.

| Dimension | Value | Status / source |
|---|---|---|
| Body L x W | **22.2 x 11.8 mm** | TowerPro drawing (above) |
| Span across the mounting tabs | **32.2 mm** | TowerPro drawing |
| Bottom of body to underside of tabs | **16 mm** | TowerPro drawing |
| Overall height, bottom to top of fitted horn | **31 mm** | TowerPro drawing. The datasheet text also gives "22.2 x 11.8 x 31 mm approx." |
| Body (case) height, without the gear dome and spline | ~22.5–22.7 mm | **unverified** (common clone figure) |
| Height to top of output spline (no horn) | ~26.5–27 mm | **unverified**. Other sources give 29 mm "product size" ([M5Stack](https://docs.m5stack.com/en/accessory/sg90_servo)) and 32 mm "w/ shaft" ([ProtoSupplies](https://protosupplies.com/product/servo-motor-micro-sg90/)), so measure. |
| Tab thickness | ~2.5 mm | **unverified**. Kpower notes that ear position and thickness vary by maker. |
| Mounting-hole spacing (centre to centre) | ~27.5–28 mm | **unverified**. Kpower notes that holes vary by maker. |
| Mounting-hole size | ~2 mm, tapered for the supplied self-tapping screws (M2-class) | **unverified** ([Kpower](https://www.kpower.com/insight_servo/2460.html) says M2 or M3) |
| Shaft axis offset from the nearer end of the body | ~5.9–6 mm (the shaft sits on the body's centreline across the 11.8 mm width) | **unverified**. The drawing shows the shaft near one end, but that distance is not dimensioned. |
| Output spline | **21 teeth**, OD ~4.8–4.9 mm | 21T: [M5Stack SG90](https://docs.m5stack.com/en/accessory/sg90_servo). OD: [Electrodragon servo notes](https://w2.electrodragon.com/gollum/overview/Tech-dat/acturator-dat/motor-dat/motor-servo-dat/) (4.80–4.90 mm, ~4.85 mm typical; 20T on some). TowerPro heads run slightly smaller than no-name ones ([RobotShop thread](https://community.robotshop.com/forum/t/modelling-a-servo-spline/31313)). Horn screw is M2 (~M2.5 on some). |
| Cable exit | Low on the end face at the **output-shaft end**, just under that tab | Read from the TowerPro drawing and photo. Clones match, but check yours. |
| Cable length | 19–25 cm (Adafruit 19 cm, ProtoSupplies 24 cm) | [Adafruit 169](https://www.adafruit.com/product/169), [ProtoSupplies](https://protosupplies.com/product/servo-motor-micro-sg90/) |
| Single-arm horn length | ~16 mm from the spline centre to the outermost hole, ~19–20 mm overall | **unverified** |
| Electrical | 4.8–6 V; ~10 mA idle, 100–250 mA moving, ~360 mA stall (measured) | [ProtoSupplies](https://protosupplies.com/product/servo-motor-micro-sg90/) |

**Advice:** before drawing, measure the hole spacing, tab thickness and spline-top height on two servos from the batch
you buy. Allow 0.3 mm of clearance around the body pocket, because clones differ by ~0.2–0.5 mm.

### A2. Lever microswitch — Omron SS-5GL and KW12-3 clones

Source: the Omron SS series datasheet, Cat. No. B032-E1-14,
https://omronfs.omron.com/en_US/ecb/products/pdf/en-ss.pdf (converted to text with pdftotext). General tolerance is
±0.4 mm unless the drawing states otherwise.

| Dimension (Omron SS-5GL, hinge lever) | Value |
|---|---|
| Body | **19.8 (L) x 6.4 (thick) x 10.2 (H) mm** |
| Mounting holes | **2 x 2.35 mm dia. (+0.075 / −0.05), 9.5 ± 0.1 mm apart**. Omron recommends 2.4 mm holes or M2.3 screws (0.23–0.26 N·m) |
| Hole positions | Hole centres are **2.9 mm above the body bottom** (the terminal edge) and 7.3 mm below the top. The first hole is **5.1 mm** from the end of the body, so the pair is centred along the 19.8 mm length. |
| Free position FP (hinge lever, from the mounting-hole centreline) | **13.6 mm max.**, which is about 6.3 mm above the top face |
| Operating position OP (from the hole centreline) | **8.8 ± 0.8 mm**, which is about 1.5 mm above the top face |
| Lever | Stainless steel, t = 0.3 mm, dimensioned at **14.5 mm** |
| OF / RF / OT / MD (SS-5GL) | OF 0.49 N max. (50 gf), RF 0.06 N min., OT ~1.2 mm min., MD 0.8 mm max. (read from the table columns; the pdftotext layout was ambiguous) |
| Lever options (same body) | GL hinge (above). **GL111 long hinge**: lever 22.6 mm, FP 16.8 mm max., OP 8.8 ± 1.5 mm. **GL13 simulated roller**: lever 15.8 mm, FP 15.5 mm max., OP 10.7 ± 0.8 mm. **GL2 hinge roller**: 4.8 x 3.2 mm roller, FP 19.3 mm max., OP 14.5 ± 0.8 mm |
| Terminals | **Solder** (no suffix): 3 lugs, 3.2 mm long, 0.5 mm thick, with 1.6 mm holes. **PCB** ("D"): ~3.3 mm long. **Quick-connect #110** ("T"): 2.8 mm tabs. Lug spacing 1.6 / 8.8 / (7.3) mm along the 19.8 length |
| Rating | SS-5 = 5 A 125 VAC. SS-01 (gold crossbar) = 0.1 A, the better choice for a 3.3 V / sub-mA signal (min. load 5 V 1 mA vs 5 V 160 mA for SS-5) |

**KW12-3 generic clones** (Amazon, often sold with 3 lever styles): sold as about **20 x 10 x 6 mm** ([Soldered 101411
datasheet listed on Mouser](https://www.mouser.com/catalog/specsheets/Soldered_101411%20microswitch%20kw12%203.pdf),
fetch timed out). Every other number is **unverified**: they appear to be Omron SS outline copies (9.5 mm hole
spacing, ~2.2 mm holes, 3 solder lugs ~4 mm long), with straight levers of ~16–17 mm or ~27–28 mm and roller levers.
FP and OP are not published. **Measure one before fixing the lid-finger geometry.** The cheap ones are rated 5 A
250 VAC with silver contacts. They work at 3.3 V / ~0.7 mA (4.7 k pull-up) in practice, but a gold-contact SS-01GL is
more reliable at that load.

### A3. Pre-wired WS2812B pixels and 12 mm bullet pixels

| Item | Value | Status / source |
|---|---|---|
| WS2812B on a round heatsink PCB, pre-wired | PCB **~10 mm diameter**; "10 mm x 3 mm" in listings (3 mm appears to be total thickness with the 5050 LED; bare PCB ~1–1.6 mm) | Listing mirrors: [Fruugo 10x3 mm](https://www.fruugo.co.nz/100-pcs-ws2812b-led-individually-addressable-ws2811-ic-rgb-whiteblack-2812b-led-heatsink-10x3mm-5050-smd-rgb-built-in-white/p-386128319-829780726), [BTF heatsink PCB (no wire)](https://everymarket.ca/products/btf-lighting-ws2812b-100pcs-5050smd-individually-addressable-smart-rgb-led-pixel-for-arduino-5v-dc-single-1-led-led-module-pixels-color-light-little-rgb-leds-lampon-black-heat-sink-pcb-board-only-dc5v). **unverified** on Amazon itself |
| Wire between pixels | **~10 cm** (eBay "10cm RGB wire" strings of 50, a TOPXCDZ 100-pack); **~12 cm** on PMD Way's 50-piece string | [eBay](https://www.ebay.de/itm/255965417889), [PMD Way](https://pmdway.com/collections/prewired-leds/products/pre-wired-ws2812b-led-pcb-strings-50-pieces) |
| Brand | I could **not** find a BTF-LIGHTING-branded pre-wired heatsink string. BTF sells the heatsink PCBs bare. Wired strings are sold by TOPXCDZ, ALITOVE and unbranded sellers | search results |
| Wire colours / order | Usually **red = +5 V**, with GND and DATA on the other two (white, green, black, yellow or blue depending on the seller). There are **separate DIN and DOUT pads**: an arrow on the PCB shows the data direction | **unverified**. Read the pad labels on the board, not the wire colours |
| Colour order | WS2812B = **GRB** | WS2812B standard; set it in firmware |
| Current | up to ~60 mA per pixel at full white (20 mA per colour) | [Adafruit 12 mm pixel guide](https://learn.adafruit.com/12mm-led-pixels?view=all) (same LED budget) |
| **12 mm WS2811 diffused bullet pixel string, 5 V** (ALITOVE etc.) | Fits a **12 mm mounting hole** (it has an 8 mm diffused LED inside). Body length ~**30–35 mm** with a flange (**unverified**). Spacing **~100 mm (4 in)** on ALITOVE-type strings, 75 mm on Adafruit's older WS2801 version. 3 wires, red = +V. Colour order is usually **RGB** for WS2811 bullets, but some "WS2811" strings are really WS2812 inside (GRB), so test | [Adafruit 12 mm pixels](https://learn.adafruit.com/12mm-led-pixels?view=all); ALITOVE listing mirrors ([desertcart](https://www.desertcart.co.th/products/80393935-alitove-ws2811-rgb-led-pixels-light-individually-addressable-12mm-diffused)). Length and colours **unverified** |

**Design note:** cutting a pre-wired string between pixels leaves each pixel with **one ~10–12 cm lead** on its DIN
side. That reaches the back compartment of the **small** chest (106 mm bay), but probably **not the long chest (160 mm
bay plus walls)**. Splice on 26 AWG extensions, or cut so that the wire length goes to the input side. Make sure the
**DIN** end goes to the node board.

### A4. RJ45 jack — Amphenol 54602-908LF (8P8C, right angle, unshielded, through-hole, no LEDs)

Source for the footprint geometry: KiCad library footprint `Connector_RJ:RJ45_Amphenol_54602-x08_Horizontal`,
https://gitlab.com/kicad/libraries/kicad-footprints/-/raw/master/Connector_RJ.pretty/RJ45_Amphenol_54602-x08_Horizontal.kicad_mod
(it cites Amphenol drawing c-bmj-0102). **KiCad covers this exact part.**

| Dimension | Value | Status |
|---|---|---|
| Width (body, across the pins) | **15.30 mm** (fab outline x −3.205 … 12.095) | KiCad footprint |
| Depth (back to front face) | **17.74 mm** (fab outline y −3.77 … 13.97) | KiCad footprint |
| Front face to pin row 1 (pins 1,3,5,7 at y = 0) | **13.97 mm** | KiCad footprint |
| Front face to pin row 2 (pins 2,4,6,8 at y = −2.54) | **16.51 mm** | KiCad footprint |
| Pin pitch | 1.27 mm stagger (2.54 mm within a row), 0.76 mm drills | KiCad footprint |
| Locating pegs | 2 x 3.2 mm NPTH at x = −1.27 and 10.16, **7.62 mm behind the front face** (y = 6.35) | KiCad footprint |
| Height above the PCB | ~13.3–13.5 mm (typical for a full-height RJ45) | **unverified**. The Amphenol and Mouser datasheet fetches were blocked. Measure, or open c-bmj-0102 by hand |
| Contact current rating | ~1.5 A per contact (typical for FCI modular jacks) | **unverified** |
| Stock | LCSC C2847314 was **out of stock** on the day of research; Future Electronics and Avnet list it | [LCSC](https://www.lcsc.com/product-image/C2847314.html) |

The front face sits flush with the PCB edge (the courtyard runs to y = 14.47). Hold the back-wall cut-out to ~16 mm
wide x ~14 mm tall above the board, then trim once you have measured the height.

### A5. AITRIP ESP32-WROOM-32 30-pin dev board (DOIT DevKit V1 style)

| Item | Value | Status / source |
|---|---|---|
| Board outline | **51.45 x 28.33 mm** (others quote 51.4 x 28.5; clones vary by ±1 mm) | [espboards.dev DOIT DevKit V1](https://www.espboards.dev/esp32/esp32doit-devkit-v1/), [mischianti (author reply 28.2 x 51.8)](https://mischianti.org/doit-esp32-dev-kit-v1-high-resolution-pinout-and-specs/) |
| Distance between the two pin rows | **25.4 mm (1.000 in)** centre to centre | **unverified**. No page stated it. **Measure the AITRIP you already have** (the plinth board); some 30-pin clones are 22.86 mm (0.9 in) |
| Pin pitch | 2.54 mm, 15 per side | standard |
| Pin order, **left** row (antenna at the top, components up, USB at the bottom), from the antenna end | EN, VP (36), VN (39), D34, D35, D32, D33, D25, D26, D27, D14, D12, D13, GND, VIN | **unverified** here (silkscreen pattern of the DOIT V1; same GPIO set as [espboards' list](https://www.espboards.dev/esp32/esp32doit-devkit-v1/)). Check against the silkscreen |
| Pin order, **right** row, from the antenna end | D23, D22, TX0 (1), RX0 (3), D21, D19, D18, D5, TX2 (17), RX2 (16), D4, D2, D15, GND, 3V3 | **unverified**, as above |
| Regulator | AMS1117-3.3; CP2102 or CH340 USB-UART | espboards.dev |

Every GPIO in DESIGN.md (4, 13, 14, 16–19, 21, 22, 25–27, 32–34) is on these headers, and none is a strapping pin.

### A6. US rubber bands (Alliance size chart: all five sizes are 1/16 in wide)

Source: https://www.rubberband.com/about-us/rubber-band-size-chart/ and
https://www.rubberband.com/wp-content/syndication-assets/flyers/RubberBandSizeChart.pdf

| Size | Flat length | Width | Flat length (mm) |
|---|---|---|---|
| #8 | 7/8 in | 1/16 in | 22 mm |
| #10 | 1-1/4 in | 1/16 in | 32 mm |
| #12 | 1-3/4 in | 1/16 in | 44 mm |
| #14 | 2 in | 1/16 in | 51 mm |
| #16 | 2-1/2 in | 1/16 in | 64 mm |

A 1/16 in band is light. For a 30 g lid on a 12 mm tail, start with **#12 or #14** looped between the tail and the hook
(about 1.5–2x stretch when the lid is shut). Use a 1/8 in wide band (#32/#33 class) or two bands if it will not pop.
Buy an assortment and tune on the first printed chest.

---

## B. Bill of materials

Prices in USD, rough, single-unit or small-pack. "Search phrase" means an Amazon search.

### Hub board (1 built, 5 bare PCBs from JLCPCB)

| Item | Qty needed | Suggested part (maker + PN) | Where | Approx price | Notes |
|---|---|---|---|---|---|
| ESP32 30-pin dev board | 1 (+1 spare) | AITRIP ESP32-WROOM-32 DevKit V1 type | Amazon: "AITRIP ESP32 WROOM-32 30 pin" (3-pack) | $17 / 3-pack | Owner already uses this board in the plinth. Measure its row spacing (A5) before laying out the sockets |
| 15-pin female header | 2 | generic 1x15 2.54 mm (or Sullins PPTC151LFBN-RC, **unverified**) | Amazon "2.54mm female header 1x15" (pack of 10–20) | $6 / pack | |
| 16-ch analog mux | 3 (+1 spare) | **CD74HCT4067E (PDIP-24) is not orderable new** — see "Lead time / stock-outs". Use **TI CD74HCT4067M96 (SOIC-24 DW)** (**unverified** distributor PN; TI lists HCT4067 as ACTIVE in SOIC/TSSOP only) | DigiKey/Mouser: search "CD74HCT4067M" | ~$1 each | Put SOIC-24W pads on the hub (hand-solderable at 1.27 mm) or use a SOIC-24 to DIP-24 adapter. Do **not** buy HC breakout boards (see the contradictions) |
| Wide DIP-24 socket (0.6 in) | 3, only if you use adapters or old stock | Mill-Max 110-43-624-41-001000 (**unverified**) or generic | DigiKey/Mouser | $1–3 each | Skip if the muxes are soldered as SOIC |
| Quad 5 V buffer | 1 (+1) | **TI SN74AHCT125N** (PDIP-14) | DigiKey **296-4655-5-ND** (from search snippet); Mouser 595-SN74AHCT125N (**unverified**) | ~$0.70 | VIH 2.0 V, so the ESP32's 3.3 V drives it correctly |
| DIP-14 socket | 1 | generic or Mill-Max 110-43-314-41-001000 (**unverified**) | DigiKey/Mouser or Amazon socket kit | $0.50 | |
| RJ45 jack | 15 (+2) | **Amphenol 54602-908LF** | Mouser **649-54602-908LF (unverified)**; Future/Avnet stock it | ~$0.60–0.75 | Same jack on the nodes, so buy ~34 at once for the price break |
| 10 k bussed SIP-9 (8 resistors) | 4 | **Bourns 4609X-101-103LF** | DigiKey (product page 2634616; active, ~15-week factory lead); Mouser 652-4609X-101-103LF (**unverified**) | ~$0.35 | 3 arrays cover 24 servo and pixel lines; the 4th covers the rest |
| Resistors 1/4 W | 10 k x ~4, 4.7 k x 1, 1 k x 1, 100 R x 1 | any metal-film (Yageo MFR-25 family, **unverified** PNs) | Amazon "1/4W metal film resistor kit" | $8 kit | |
| 100 nF ceramic, 5 mm pitch | 5 | Vishay **K104K15X7RF53H5** (bulk) or **K104K15X7RF5UH5** (cut tape) — both 5.0 mm lead spacing ("H5"; codes ending "L2" are 2.5 mm), or generic. NOT K104K15X7RF5TL2: that one is 2.5 mm lead spacing (owner checked on Mouser 2026-11-12) | DigiKey/Mouser or Amazon kit | $0.20 each | |
| 1000 uF 10 V radial | 1 | Panasonic EEU-FR1A102 (**unverified**), or any 1000 uF ≥ 10 V | DigiKey/Mouser | $0.60 | Use 16 V parts for margin; the same footprint usually fits |
| 100 uF 10 V radial | 1 | Panasonic EEU-FR1A101 (**unverified**) or any 100 uF ≥ 10 V | DigiKey/Mouser | $0.20 | |
| Barrel jack 5.5 x 2.1 | 1 | **CUI / Same Sky PJ-102AH** | DigiKey/Mouser (CP-102AH-ND **unverified**); LCSC C3096093 | ~$1 | Distributors list it at **5 A** (2.0 mm pin). The plain PJ-102A is only 2.5 A, so order the **AH** |
| 2-pin 5.08 mm screw terminal | 1 | **Phoenix Contact 1729128** (MKDSN 1,5/2-5,08) | DigiKey/Mouser (**unverified** PNs) | ~$0.80 | 26–16 AWG |
| 6 mm tactile switch | 1 | Omron B3F-1000 (**unverified**) or generic | DigiKey/Mouser or Amazon kit | $0.20 | TEST on GPIO 13 |
| Male pin headers 1x4 / 1x5 | 1 each | generic 2.54 mm breakaway | Amazon "2.54mm male header breakaway" | $5 / pack | I2C header plus the spare 16th port |
| Bare PCB | 5 (1 built) | JLCPCB, 124 x 112 mm, 2-layer | jlcpcb.com | ~$15–25 + shipping | Over 100 x 100 mm, so not the $2 tier |

### Node boards (15 built + 2–5 spares)

| Item | Qty needed | Suggested part | Where | Approx price | Notes |
|---|---|---|---|---|---|
| RJ45 jack | 17 | Amphenol 54602-908LF | as above | ~$0.65 | |
| 1x3 male header (servo) | 17 | generic 2.54 mm | same pack as the hub | — | Order GND / +5 V / SIG to match the SG90 plug (brown / red / orange) |
| 5-pos 3.5 mm screw terminal | 17 | **Phoenix Contact 1751277** (MKDS 1/5-3,5) — ~$2+ each; or generic 3.5 mm 2+3 interlocking blocks | DigiKey/Mouser (**unverified** PNs) or Amazon "3.5mm PCB screw terminal block" (50+ pack) | $2.50 (Phoenix) / ~$0.25 (generic) | The generic blocks are fine for a prop. **Pitch must be 3.5 mm**: 2.54 mm and 5.08 mm blocks do not fit the board |
| **470 uF 10 V radial, 8 mm diameter, 3.5 mm lead spacing** | 17 | Panasonic **EEU-FR1A471** (8 x 11.5 mm, 3.5 mm; Mouser 667-EEU-FR1A471 — confirm the spacing on the page) | Mouser/DigiKey | ~$0.60 | Corrected 2026-11-13: this row still said 100 uF after the node board's C1 was changed to 470 uF. The 100 uF part (5 mm can, 2 mm leads) is only for the hub's C2 |
| Bare PCB | 20+ | JLCPCB 42 x 20 mm (or panelise 2–3 per board) | jlcpcb.com | ~$5–10 | |

### Per chest x 15 (+ spares)

| Item | Qty needed | Suggested part | Where | Approx price | Notes |
|---|---|---|---|---|---|
| SG90 micro servo | 15 (+3–5) | TowerPro SG90 or clone | Amazon "SG90 micro servo 10 pack" | ~$20–25 / 10 | Clones vary ±0.5 mm (A1). Buy every servo from **one** listing |
| Lever microswitch | 15 (+5) | Omron **SS-01GL** (gold contacts, low-level signal) or SS-5GL; or KW12-3 clone | DigiKey/Mouser (Omron PNs **unverified**); Amazon "KW12-3 micro limit switch lever" (20-pack) | Omron ~$2–4 each; clones ~$7 / 20 | Same Omron SS outline (A2). Check the clone's FP/OP |
| WS2812B pre-wired pixel | 15 (+5) | pre-wired WS2812B on a 10 mm heatsink PCB (TOPXCDZ / ALITOVE / generic) | Amazon "WS2812B pre-wired heatsink 10cm wire" (strings of 50, packs of 50 or 100) | ~$15 / 50 | ~10 cm leads, which are short for the long chest (A3). Alternative: "ALITOVE 12mm WS2811 5V pixel string" (50 / ~$20) |
| Ethernet patch cable | 15 (+2) | slim Cat6, mix of 3 ft and 5 ft | Amazon "slim Cat6 patch cable 3 ft 10 pack" / "5 ft 10 pack" | ~$15–20 per 10-pack | Slim cables are 28–32 AWG, which is fine at this current. Label them "NOT ETHERNET" |
| Rubber bands | 15 + lots of spares | Alliance #12 and #14 (1/16 in), plus a few 1/8 in | Amazon "Alliance rubber bands assorted" or office store | ~$5 | Size table in A6 |
| **#4 x 1/2 in flat-head** wood / sheet-metal screw | 109 | Phillips, 82 degree head. **Same screw the owner bought for the plinth cover** (changed 2026-11-13 from 1/4 in and 3/8 in pan-heads) | Amazon / hardware store (100 pack + what is left of the plinth box) | ~$6 | 7 per chest (1 node board, 2 deck, 4 backplate) + 4 in the hub case |
| Hinge pin | 15 | 1.75 mm filament offcut or a large paper clip | on hand | — | |
| Stick-on rubber feet | 60 (4 per chest) | small clear bumpers ~8–10 mm | Amazon "clear rubber bumper feet" (100 pack) | ~$6 | |
| 26 AWG hook-up wire | ~5 m | stranded silicone 26 AWG, 2 colours | Amazon "26 AWG silicone wire kit" | ~$10 | For the switches, plus pixel-lead extensions |

### Power

| Item | Qty | Suggested part | Where | Approx price | Notes |
|---|---|---|---|---|---|
| 5 V 4 A UL-listed desktop supply, 5.5 x 2.1 mm centre-positive | 1 | **Mean Well GST25A05-P1J** (5 V 4 A, P1J = 2.1 mm plug) (**unverified** on a distributor page this session); or GST40A05-P1J (5 V 6 A) | DigiKey/Mouser | ~$13–18 | Real worst-case draw is ~1.5–2 A (one servo ≤ 0.4 A stall, 15 px x 60 mA = 0.9 A, ESP32 ~0.25 A), so 4 A has margin |

### Filament

See PRINT.md.

### Rough total

| Group | Approx |
|---|---|
| Hub parts + 5 PCBs (incl. shipping) | ~$75–95 (ESP32 3-pack $17, RJ45 x 34 ~$24, PCB ~$35, the rest ~$15) |
| Node parts + PCBs | ~$20–35 (generic terminals) / ~$60 (Phoenix terminals) |
| Per-chest items (20 servos, 20 switches, 50 px, 17 cables, hardware) | ~$130–170 |
| Power supply | ~$15 |
| **Total (excluding filament)** | **~$250–320**, plus distributor shipping (~$8 each for Mouser and DigiKey unless the order qualifies for free shipping) |

### Lead time / stock-out call-outs

* **CD74HCT4067E (DIP-24) is not available new.** TI's product page lists CD74HCT4067 and CD74HC4067 as ACTIVE only in
  SOIC / TSSOP / VQFN / VSSOP ([TI HCT4067](https://www.ti.com/product/CD74HCT4067),
  [TI HC4067](https://www.ti.com/product/CD74HC4067)). DigiKey marks CD74HC4067E **obsolete, 0 stock**, and the
  CD4067BE DIP is **obsolete** too. Change the hub footprint to **SOIC-24W** (or plan for SOIC-to-DIP adapters). SOIC
  HCT parts have shown ~12-week factory lead times, so **order these first**.
* **Amphenol 54602-908LF:** LCSC was out of stock. Check Mouser/DigiKey stock for 34 pieces before freezing the
  footprint. KiCad's `Connector_RJ` library has other unshielded horizontal jacks if you need to switch.
* **Bourns 4609X-101-103LF:** active, but DigiKey shows a ~15-week factory lead time, so buy from stock now. Plain
  10 k resistors are a drop-in fallback.
* The Amazon items (SG90s, pre-wired pixels, slim cables) are always in stock but **change specs between listings**.
  Buy all of a kind from one listing.
