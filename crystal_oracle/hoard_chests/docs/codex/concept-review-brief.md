You are reviewing a mechanical + electronics concept for a Halloween prop before it is built.
Read `docs/DESIGN.md` in this folder (the design contract). Do not write code and do not change
any file; just answer.

Context: a home Halloween prop. A touch-screen "oracle" tells each kid which prize tier they won;
one of 15 small 3D-printed treasure chests (5 per tier) should then pop open with the prize inside.
A grown-up reloads and closes the chests between groups of up to 5 kids; 100 to 200 kids in one
evening, outdoors on a porch at dusk in Texas (about 10 to 25 C, maybe damp). Parts are printed in
PLA on a 256 mm bed and assembled by one hobbyist with a soldering iron. Small children's fingers
will be on the chests.

Answer in Markdown, at most 700 words, as specific as you can (numbers, part names):

1. The three most likely ways a chest fails on the night (latch, rubber band, lid switch, pixel,
   cable) and the cheapest change that prevents each.
2. Anything wrong in the latch idea: an SG90 arm swinging in a horizontal plane through a window in
   a tab that hangs from the lid and into a slot in the box, servo unpowered when idle.
3. Any electrical problem in the hub: one servo-pulse pin, one WS2812 data pin and one switch-sense
   pin fanned out by three CD74HCT4067 analog multiplexers at 5 V to 15 RJ45 ports over 1 to 2 m
   Ethernet patch cables; 74AHCT125 level shifting; 10 k pull-downs; ESP32 pin choices; 5 V 4 A
   supply; pixel data integrity through a mux (on-resistance, latching when the mux switches away).
4. Any safety problem for children and how to close it.
5. One idea that would make it more reliable or more fun that is not in the document.

Be blunt.
