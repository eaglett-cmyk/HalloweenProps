**Build one chest and cycle it 200 times before printing fifteen. The concept is workable, but the latch and “power-off” assumptions need fixing.**

1. **Three likely failures**

   - **Latch jams under band tension or a child pulling.** Supporting the arm tip reduces servo bending load; it does not eliminate withdrawal friction. Cheapest prevention: chamfer the window/slot entrances, start with **0.5–0.8 mm clearance per side**, add lid alignment stops, and use the weakest band that reliably opens it. Test release while pulling upward.
   - **Band slips, breaks, or cannot overcome hinge friction.** Use captive, rounded attachment holes instead of open hooks; deburr the hinge and use a captive metal pin. Fit fresh bands and keep identical spares. Verify opening torque throughout travel: the band must not pass through a dead centre near closed.
   - **Lid switch falsely reports reload.** A misaligned actuator, bouncing contact, or children closing empty boxes causes trouble. Add an adjustable actuator and hard lid stop that prevents switch overtravel; require **30–50 ms stable closure**. Closure proves neither a prize nor latch engagement. A broken cable appears OPEN with this wiring.

2. **Latch geometry**

   A rotary arm follows an **arc**, not a straight bolt path. The receiving slot needs clearance for the entire withdrawal sweep; otherwise its “support” traps the arm. Make a sectioned prototype to check this.

   The closed latch needs a mechanical stop/detent so lid force cannot rotate the unpowered servo backward. SG90 gearbox friction is not a lock. Avoid holding torque against a servo endpoint; retain the horn screw. Provide an adult-accessible manual release.

   **The documented circuit never cuts servo power.** Disconnecting PWM through a mux only removes commands. Add per-port high-side power switches if actual power-off is required; never switch servo current through a 4067.

3. **Hub electrical review**

   - **Logic:** HCT mux controls accept 3.3 V at a 5 V supply; ordinary HC parts are not equivalent. The AHCT125 is appropriate for outgoing signals. Keep switch sensing pulled to **3.3 V**, never 5 V: an analog mux does not level-shift. Add **100 nF per IC**, defined enable states, and tie unused buffer inputs. [TI datasheet](https://www.ti.com/lit/ds/symlink/cd74hct4067.pdf)
   - **Pins:** These assignments suit the stated WROOM board. GPIO34 is input-only without an internal pull-up; the external 4.7 kΩ is necessary. GPIO4 is **not** a classic ESP32 boot-strapping pin. GPIO16/17 can conflict with PSRAM on other module variants. [Espressif datasheet](https://documentation.espressif.com/esp32_datasheet_en.html)
   - **Pixels:** Multiplexing can work. On-resistance is approximately **70 Ω typical**, higher across voltage/temperature; with 100 Ω series resistance and cable capacitance it slows edges. It is not automatically fatal at 1–2 m, but scope DIN at the farthest chest. Four **74AHCT125s after the mux**, one buffer per port, provide more margin. Keep DATA low during address changes and switch scans.
   - Send exactly **24 bits**, finish low, then disconnect. The chest’s local 10 kΩ pull-down supplies the reset interval and the powered pixel retains its colour. Allow **≥300 µs low** for variants requiring >280 µs. Switching away mid-bit corrupts data. [Worldsemi datasheet](https://www.tme.eu/Document/83abb3faee82fb745a9fc1c574c0a8a3/WS2812B-V5W%20Datasheet_V6.0_EN.pdf)
   - **Power/cables:** Regulated **5 V/4 A** is plausible with one moving servo, not guaranteed with fifteen powered SG90s. Measure idle/start/stall currents and far-end voltage. Use copper patch leads, strain relief, per-port fault protection, and **470 µF near each servo**. A shorted cable must not reset everything.

4. **Children and damp**

   Enclose the horn, tab, band and hinge pinch points; round edges, retain pins, screw down electronics covers, anchor boxes and secure cables. Limit opening to roughly **60–80°** with a soft stop and weak spring force. Keep mains indoors on GFCI protection; shelter low-voltage joints with drip loops.

5. **One addition**

   Require an **adult “reload confirmed” action** before closed chests become ARMED. Otherwise children can close empty boxes and win an empty chest.