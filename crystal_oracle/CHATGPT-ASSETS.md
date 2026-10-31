# What to make: the ChatGPT images, voice lines, sounds and music for Dragonglow

*Generated from `assets/manifest.json` by `tools/gen_asset_list.py` — edit the manifest, then re-run that script.*

Everything the screen needs, in priority order. **MVP** = needed for Halloween night 2026. **NICE** = makes it better; the screen works without it (missing art is replaced by the screen's own glowing-orb animation, missing voice lines are skipped, missing sounds become built-in chimes).

| What | MVP | Nice-to-have | Made with |
|---|---|---|---|
| Images (stills) | 16 | 13 | **ChatGPT** (Images) |
| Voice lines (Butterscotch) | 43 | 31 | ElevenLabs or OpenAI text-to-speech (ChatGPT can't export audio) |
| Sound effects | 10 | 7 | Pixabay / Freesound / ElevenLabs Sound Effects |
| Music bed | 1 | 1 | Pixabay Music / ElevenLabs Music |
| Animated clips | built automatically from the images (28 recipes, 31 clips with the colour variants) | | `tools/build_assets.py` |

> **Why stills, not videos?** As of October 2026 ChatGPT no longer makes video or audio files (OpenAI shut Sora down in 2026). ChatGPT Images still makes excellent stills, transparent cut-outs and consistent edits of one character. The asset builder turns those stills into the screen's animations: Butterscotch floating like a balloon and changing expressions, Snuffwick tiptoeing in with his rubber chicken, the hoard coin spinning, the Golden Star rising out of the Wakestone, the Dragon Gem climbing. The screen adds the live magic on top (glows in each kid's crystal colour, sparkles, the Hoard Beacon's growing glow, the numbers and words). If you do get real video clips from another tool, drop the MP4s in `assets/source/video/` — the builder can use them too.

## 1. How to make the images in ChatGPT

1. **One chat per character.** Start a chat for Butterscotch, one for Snuffwick, one for backgrounds, one for props. Edits stay consistent when the base image is in the same chat.
2. **Make the base image first and re-roll until you love it** (`butterscotch_base`, `snuffwick_base`). Every other Butterscotch/Snuffwick image is an *edit* of its base — always attach/edit the **base**, never an earlier edit (drift adds up).
3. **Paste the prompt exactly** (each image's prompt is below). Ask for a **transparent background** on characters and props. If ChatGPT gives you a solid background instead, ask it once more for transparency; if it still can't, a flat pure green (#00FF00) background is fine — the builder removes it.
4. **Backgrounds are wide (5:3).** Pick the widest landscape shape (1536×1024 is fine — the builder crops it). Keep the bottom quarter dark and empty — the screen writes words and numbers there.
5. **Download each image and name it exactly by its id** — `butterscotch_base.png`, `bg_cave.png`, … — into `crystal_oracle/assets/source/images/`. PNG, JPG and WebP all work.
6. **Run the builder** (`python -I tools/build_assets.py --placeholders` from `crystal_oracle/`), look at `assets/previews/` (or the screen), and tweak. If Butterscotch's glow lands in the wrong spot, adjust that still's `"anchor": [x, y]` (fractions of the image) in the manifest. The Hoard Beacon's glow is placed by the words key `meter.pos` ("x,y,radius" in screen pixels, default 150,110,70): once `bg_cave` exists, open `assets/previews/` and, if the painted lantern isn't there, change `meter.pos` (or `/oracle/captions.ini` [meter] pos on the card).
7. **Style guardrails** (already inside every prompt): soft storybook-3D look, big round shapes, dark indigo night, one bright subject, **no grain / mist / bokeh / tiny stars / text** (they turn blocky on this screen). Butterscotch is CARAMEL / toffee brown, never gold: gold is only for the Golden Star (and its starburst); silver is the hoard coin; red is only the Dragon Gem.

**Suggested order:** Day 1 Butterscotch (base + sleep, peek, delight, surprise) · Day 2 Butterscotch giggle + puff, Snuffwick (base + rubber chicken) and the four props · Day 3 the three backgrounds (`bg_cave` first: check where the lantern landed) · Day 4+ the nice-to-haves.

## 2. Image list

| # | File name | What it is | Priority | Edit of | Used by |
|---|---|---|---|---|---|
| 1 | `bg_cave.png` | The Hoard Cave at night (the main scene): a cozy rounded cave in deep indigo and plum … | MVP | — | attract, attract_full, absorb, greet, greet_r5, charge … |
| 2 | `bg_wakestone.png` | The Wakestone: a deeper chamber of the cave framed by big soft rounded rock shapes; in … | MVP | — | awaken, reveal_t2, reveal_t2_2, circle, chapter_d |
| 3 | `bg_starburst.png` | Golden Star burst: a blue-black night sky with about 12 broad, soft-edged gold … | MVP | — | reveal_t3, milestone |
| 4 | `butterscotch_base.png` | Master image — Butterscotch, a chubby, round old dragon shaped like a soft gumdrop or a … | MVP | — | charge, reveal_t0_2 |
| 5 | `butterscotch_sleep.png` | eyes closed in cozy sleep, a small round open snoozing mouth; face-only change. | MVP | butterscotch_base | attract, attract_full, sleepy, chapter_a, chapter_b |
| 6 | `butterscotch_peek.png` | one eye open and peeking, the other still closed, a sly little smile; face-only. | MVP | butterscotch_base | attract, attract_full, absorb, replay, keeper, chapter_e … |
| 7 | `butterscotch_delight.png` | a huge open happy smile (no teeth), sparkly happy eyes, rosy cheeks; face-only. | MVP | butterscotch_base | attract_full, awaken, greet, greet_r5, reveal_t1, reveal_t2_2 … |
| 8 | `butterscotch_surprise.png` | a round O-shaped mouth, eyebrows raised high, eyes wide and sparkly; face-only. | MVP | butterscotch_base | absorb, awaken, reveal_t0_1, reveal_t0_3, reveal_t2, reveal_t2_2 |
| 9 | `butterscotch_giggle.png` | a silly giggle - eyes squeezed shut into happy arcs, cheeks scrunched, a little pink … | MVP | butterscotch_base | reveal_t0_1, reveal_t0_2, reveal_t0_3, circle, moonfull |
| 10 | `butterscotch_puff.png` | cheeks puffed out big and round, eyes wide, nose scrunched as if about to sneeze or … | MVP | butterscotch_base | reveal_t0_2, reveal_t0_3, nocrystal |
| 11 | `snuffwick_base.png` | Master image — Snuffwick, a tiny teacup-sized goblin that looks like a plush toy: round … | MVP | — | attract, attract_full, reveal_t0_1, reveal_t0_2, reveal_t0_3, moonfull … |
| 12 | `snuffwick_chicken.png` | instead of the snuffer he hugs a big pale lemon-yellow squeaky rubber chicken with an … | MVP | snuffwick_base | reveal_t0_1 |
| 13 | `prop_crystal.png` | One pale clear faceted crystal with the shape of your real 3D-printed crystals (give … | MVP | — | awaken, chapter_d |
| 14 | `prop_coin.png` | HOARD COIN: a big round shiny SILVER candy coin (#D9DEE8), like a chocolate coin in … | MVP | — | reveal_t0_1, reveal_t1 |
| 15 | `prop_gem.png` | DRAGON GEM: a big dark red ruby in a CUSHION CUT (rounded-corner square), chunky … | MVP | — | reveal_t3 |
| 16 | `prop_star.png` | GOLDEN STAR: a chunky glossy gold (#FFC83D) five-point star with rounded points and a … | MVP | — | reveal_t2, reveal_t2_2 |
| 17 | `bg_cave_lit.png` | the Hoard Beacon now BLAZES warm amber (#FFB347) with a soft warm pool of light on the … | NICE | bg_cave | attract_full, moonfull, chapter_a, chapter_b, chapter_e |
| 18 | `butterscotch_yawn.png` | a big sleepy yawn - round open mouth (no teeth), eyes squeezed shut; face-only. (Sleepy … | NICE | butterscotch_base | sleepy |
| 19 | `butterscotch_wink.png` | his right paw raised to his lips in a 'shh', one eye winking, a secret smile; the other … | NICE | butterscotch_base | replay, keeper |
| 20 | `butterscotch_wave.png` | his right paw raised high, waving goodbye, a big warm smile; the other paw unchanged. … | NICE | butterscotch_base | goodbye |
| 21 | `snuffwick_sneeze.png` | dazzled by the glow, mid-sneeze - eyes squeezed shut, nose scrunched, a round puff of … | NICE | snuffwick_base | reveal_t0_3, circle, moonfull |
| 22 | `snuffwick_shy.png` | a shy happy smile with a tiny blush, big hopeful eyes, paws clasped in front of his … | NICE | snuffwick_base | moonfull |
| 23 | `snuffwick_sock.png` | hugging a big lumpy plum-and-cream striped sock in both arms; something round inside it … | NICE | snuffwick_base | attract_full, moonfull, chapter_f |
| 24 | `prop_hat.png` | A small crooked wizard hat in faded purple (#6E5A9E) with two big simple pale-cream … | NICE | — | greet_r5 |
| 25 | `prop_feather.png` | One long, curly tickle-feather in soft lavender with a cream tip, a big simple stylised … | NICE | — | reveal_t0_2 |
| 26 | `fx_bubble.png` | A glossy round soap bubble with a soft rainbow rim and a mostly clear centre … | NICE | — | reveal_t0_2, nocrystal |
| 27 | `fx_eyes.png` | Two big round friendly goblin eyes peering out of the dark: sage-green glowing irises, … | NICE | — | reveal_t0_3 |
| 28 | `chapter_egg.png` | Chapter still (lore): long ago, in the Hoard Cave, a tiny BABY Butterscotch (the same … | NICE | — | chapter_c |
| 29 | `print_keyart.png` | Portrait 2:3 key art: Butterscotch floating happily with his paws around one glowing … | NICE | — | parent sign, title card |

## 3. The prompts (copy-paste)

### `bg_cave` — MVP · background

```text
The Hoard Cave at night: a cozy, rounded cave interior in deep indigo and plum, framed by a big round smooth stone arch. Soft piles of glossy candy - big wrapped sweets, round lollipops and gumdrops in muted jewel colours (NO gold coins, nothing gold) - rest low on the far left and the far right sides of the cave floor. THE HOARD BEACON: an old, round, dark-iron lantern with a big ROUND, CLEAR, EMPTY glass globe hangs from a short chain in the UPPER-LEFT of the picture - its globe centre about one-fifth of the way in from the left edge and a little less than one-quarter of the way down from the top; the lantern is about one-sixth of the image height. It is clearly visible but UNLIT and dark (no flame, no glow), with plain, empty, dark cave wall all around it (a glow will be painted there later). Leave the CENTRE of the picture open and empty (a floating dragon goes there) and keep the upper-left area around the lantern uncluttered. Format: wide landscape, 5:3 (1600x960 if you can choose; otherwise the widest landscape option). Dark midnight-indigo and plum colours (#140F2E, #2B1650). Keep the BOTTOM QUARTER of the image plain near-black and empty (the screen draws words there). Big simple shapes, clean smooth gradients. Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, no text, no letters, no watermark. Spooky-cute and cozy, never scary.
```

### `bg_wakestone` — MVP · background

```text
The Wakestone: a deeper chamber of a cozy cave, dark indigo and plum, framed by big soft rounded rock shapes. In the exact centre stands a round, flat-topped grey stone pedestal (the Wakestone) with a simple carved swirl of a curled-up sleeping dragon on its front. One broad, soft column of warm cream candle-light falls onto it from above. The pedestal's top surface is at about 60% of the image height and is EMPTY (nothing on it). Format: wide landscape, 5:3 (1600x960 if you can choose; otherwise the widest landscape option). Dark midnight-indigo and plum colours (#140F2E, #2B1650). Keep the BOTTOM QUARTER of the image plain near-black and empty (the screen draws words there). Big simple shapes, clean smooth gradients. Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, no text, no letters, no watermark. Spooky-cute and cozy, never scary.
```

### `bg_starburst` — MVP · background

```text
A deep blue-black night sky with about 12 broad, soft-edged gold (#FFC83D) rays fanning out from the exact centre of the image, a soft gold glow at the centre and a few large simple stars. Nothing else. Format: wide landscape, 5:3 (1600x960 if you can choose; otherwise the widest landscape option). Dark midnight-indigo and plum colours (#140F2E, #2B1650). Keep the BOTTOM QUARTER of the image plain near-black and empty (the screen draws words there). Big simple shapes, clean smooth gradients. Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, no text, no letters, no watermark. Spooky-cute and cozy, never scary.
```

### `butterscotch_base` — MVP · transparent

```text
MASTER CHARACTER IMAGE. Butterscotch, a chubby, round old dragon shaped like a soft gumdrop or a pear, floating like a balloon (two short stubby feet tucked under, not standing). Glossy, candy-smooth skin with NO scales, in warm CARAMEL / butterscotch-toffee orange-brown (#C77A35, deeper toffee #9A5424 in the shadows) with a smooth cream belly (#FBE3BF) - NOT gold, NOT yellow. Two small rounded horns shaped like caramel swirls; two tiny stubby wings, much too small to fly; big warm amber-brown eyes with bright highlights; small rounded paws; a short curly tail ending in a little round tuft. A crooked, faded purple wizard hat (#6E5A9E) with exactly TWO big simple pale-cream five-point stars (#F3E6C4) sits between his horns. His small rounded paws are held together in front of his tummy. Gentle closed-mouth smile, facing the viewer. No teeth, no claws, no fire, no smoke, nothing sharp, nothing gold. Keep the design SIMPLE: big clean shapes and only a few small details. Full figure, centred, nothing cropped, on a TRANSPARENT background (PNG). If you can't make it transparent, use a flat pure green #00FF00 background with no shadow and no floor. Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, no text, no letters, no watermark. Spooky-cute and cozy, never scary.
```

### `butterscotch_sleep` — MVP · transparent

*Attach / edit: `butterscotch_base`.*

```text
Use the attached butterscotch_base image as Image 1. Edit Image 1: change ONLY the face: eyes closed in cozy, happy sleep, a small round open snoozing mouth, relaxed cheeks. Keep his body shape, pose, paws, tiny wings, swirl horns, curly tail, the faded purple wizard hat (same tilt, same two stars), size, colours, framing and the transparent background EXACTLY identical. Same style.
```

### `butterscotch_peek` — MVP · transparent

*Attach / edit: `butterscotch_base`.*

```text
Use the attached butterscotch_base image as Image 1. Edit Image 1: change ONLY the face: one eye open and peeking, the other eye still closed, a sly little smile. Keep his body shape, pose, paws, tiny wings, swirl horns, curly tail, the faded purple wizard hat (same tilt, same two stars), size, colours, framing and the transparent background EXACTLY identical. Same style.
```

### `butterscotch_delight` — MVP · transparent

*Attach / edit: `butterscotch_base`.*

```text
Use the attached butterscotch_base image as Image 1. Edit Image 1: change ONLY the face: a huge open happy smile (no teeth, just a soft pink mouth), sparkly happy eyes, rosy cheeks. Keep his body shape, pose, paws, tiny wings, swirl horns, curly tail, the faded purple wizard hat (same tilt, same two stars), size, colours, framing and the transparent background EXACTLY identical. Same style.
```

### `butterscotch_surprise` — MVP · transparent

*Attach / edit: `butterscotch_base`.*

```text
Use the attached butterscotch_base image as Image 1. Edit Image 1: change ONLY the face: a round O-shaped mouth, eyebrows raised high, eyes wide and sparkly with wonder. Keep his body shape, pose, paws, tiny wings, swirl horns, curly tail, the faded purple wizard hat (same tilt, same two stars), size, colours, framing and the transparent background EXACTLY identical. Same style.
```

### `butterscotch_giggle` — MVP · transparent

*Attach / edit: `butterscotch_base`.*

```text
Use the attached butterscotch_base image as Image 1. Edit Image 1: change ONLY the face: a silly giggle - eyes squeezed shut into happy arcs, cheeks scrunched up, a little pink tongue poking out of one corner of his mouth. Keep his body shape, pose, paws, tiny wings, swirl horns, curly tail, the faded purple wizard hat (same tilt, same two stars), size, colours, framing and the transparent background EXACTLY identical. Same style.
```

### `butterscotch_puff` — MVP · transparent

*Attach / edit: `butterscotch_base`.*

```text
Use the attached butterscotch_base image as Image 1. Edit Image 1: change ONLY the face: cheeks puffed out big and round like he's holding his breath, eyes wide, nose scrunched up as if he is about to sneeze or hiccup, lips pressed together. Keep his body shape, pose, paws, tiny wings, swirl horns, curly tail, the faded purple wizard hat (same tilt, same two stars), size, colours, framing and the transparent background EXACTLY identical. Same style.
```

### `snuffwick_base` — MVP · transparent

```text
MASTER CHARACTER IMAGE. Snuffwick, a tiny, cute, teacup-sized goblin that looks like a PLUSH TOY, never scary: a round, soft sage-green (#8DBF6A) face and body, huge floppy ears, big shiny dark eyes with star highlights, a button nose and a wide TOOTHLESS cheeky grin; mismatched striped socks (one plum-and-cream, one teal-and-cream); a little cap shaped like a short, UNLIT cream candle stub. He carries a long pewter-grey candle-snuffer (a small cone on a stick) over his shoulder like an umbrella. Tiptoeing pose, sneaky and giggly. Soft pale rim light so he stands out on a dark background. No claws, no teeth, nothing gold. Keep the design SIMPLE. Full figure, centred, nothing cropped, on a TRANSPARENT background (PNG). If you can't make it transparent, use a flat pure green #00FF00 background with no shadow and no floor. Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, no text, no letters, no watermark. Spooky-cute and cozy, never scary.
```

### `snuffwick_chicken` — MVP · transparent

*Attach / edit: `snuffwick_base`.*

```text
Use the attached snuffwick_base image as Image 1. Edit Image 1: change ONLY his arms and face: instead of the snuffer he hugs a big pale lemon-yellow (#F4EE8C) squeaky rubber chicken with an orange beak (a classic cartoon rubber chicken) in both arms, and he giggles with his eyes squeezed shut. Keep his round sage-green body, floppy ears, mismatched striped socks, candle-stub cap, size, colours, framing and the transparent background EXACTLY identical. Same style.
```

### `prop_crystal` — MVP · transparent

*Attach: a photo of one of your 3D-printed crystals.*

```text
One pale, clear, faceted crystal shaped like the crystal in the attached photo (attach a photo of one of your 3D-printed crystals), nearly white with soft internal facets and a bright top highlight. Make it colourless - pale white-grey glass, NOT the colour of the photo, and no glitter or sparkles inside. Front view, standing upright. Full figure, centred, nothing cropped, on a TRANSPARENT background (PNG). If you can't make it transparent, use a flat pure green #00FF00 background with no shadow and no floor. Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, no text, no letters, no watermark. Spooky-cute and cozy, never scary.
```

### `prop_coin` — MVP · transparent

```text
A chunky glossy SILVER DRAGON COIN (#D9DEE8, polished smooth silver foil with soft lavender-pink candy reflections), tilted very slightly so its thick rounded rim shows, in the same candy-treasure style as the hoard in bg_cave. Deep raised relief in the centre: a CUTE CHUBBY round dragon like Butterscotch (round tummy, tiny wings, swirl horns, curly tail, sweet smile, tiny wizard hat with a star), all silver. Tiny embossed stars and candy dots inside the rim. No text, no numbers. Silver only - NOT gold. Full figure, centred, nothing cropped, on a TRANSPARENT background (PNG). If you can't make it transparent, use a flat pure green #00FF00 background with no shadow and no floor. Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, no text, no letters, no watermark. Spooky-cute and cozy, never scary.
```

### `prop_gem` — MVP · transparent

```text
A big DARK RED RUBY dragon gem (deep garnet-ruby #7A0A16 with brighter ruby #D21A2C glints) in a classic CUSHION CUT: a rounded-corner square with gently bowed sides, a flat table facet and chunky glossy crown facets, one bright highlight and a warm inner glow. Front view, tilted very slightly, no face, no text. Full figure, centred, nothing cropped, on a TRANSPARENT background (PNG). If you can't make it transparent, use a flat pure green #00FF00 background with no shadow and no floor. Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, no text, no letters, no watermark. Spooky-cute and cozy, never scary.
```

### `prop_star` — MVP · transparent

```text
A chunky glossy GOLDEN DRAGON STAR (#FFC83D, warm honey-gold polished foil with soft candy-pink reflections and a gentle glow): a five-point star with softly rounded points and a thick bevelled rim, in exactly the style of prop_coin. Deep raised relief in the centre: the same CUTE CHUBBY wizard-hat dragon, all gold. Tiny embossed candy dots inside the rim. No text, no numbers. Full figure, centred, nothing cropped, on a TRANSPARENT background (PNG). If you can't make it transparent, use a flat pure green #00FF00 background with no shadow and no floor. Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, no text, no letters, no watermark. Spooky-cute and cozy, never scary.
```

### `bg_cave_lit` — NICE · background

*Attach / edit: `bg_cave`.*

```text
Use the attached bg_cave image as Image 1. Edit Image 1: make ONLY the Hoard Beacon blaze with warm amber light (#FFB347) - a bright glowing globe and a soft warm pool of light on the nearby cave walls; the candy piles catch a few warm highlights. Keep the cave, the arch, the lantern's exact position and size, the empty centre and the layout EXACTLY identical. Same style; the bottom quarter stays plain near-black.
```

### `butterscotch_yawn` — NICE · transparent

*Attach / edit: `butterscotch_base`.*

```text
Use the attached butterscotch_base image as Image 1. Edit Image 1: change ONLY the face: a big sleepy yawn - his mouth wide open in a round yawn (no teeth, just a soft pink mouth), eyes squeezed shut, one tiny sleepy tear. Keep his body shape, pose, paws, tiny wings, swirl horns, curly tail, the faded purple wizard hat (same tilt, same two stars), size, colours, framing and the transparent background EXACTLY identical. Same style.
```

### `butterscotch_wink` — NICE · transparent

*Attach / edit: `butterscotch_base`.*

```text
Use the attached butterscotch_base image as Image 1. Edit Image 1: change ONLY his RIGHT paw and his face: the right paw is raised to his lips in a 'shh' gesture, he winks one eye, and he has a secret little smile. Keep his body shape, his other paw, tiny wings, swirl horns, curly tail, the faded purple wizard hat (same tilt, same two stars), size, colours, framing and the transparent background EXACTLY identical. Same style.
```

### `butterscotch_wave` — NICE · transparent

*Attach / edit: `butterscotch_base`.*

```text
Use the attached butterscotch_base image as Image 1. Edit Image 1: change ONLY his RIGHT paw and his face: the right paw is raised high, waving goodbye, with a big warm smile and happy eyes. Keep his body shape, his other paw, tiny wings, swirl horns, curly tail, the faded purple wizard hat (same tilt, same two stars), size, colours, framing and the transparent background EXACTLY identical. Same style.
```

### `snuffwick_sneeze` — NICE · transparent

*Attach / edit: `snuffwick_base`.*

```text
Use the attached snuffwick_base image as Image 1. Edit Image 1: change ONLY his face and ears: dazzled by a bright light and caught mid-sneeze - eyes squeezed shut, nose scrunched, a round puff of big simple sparkles bursting from his nose, ears flung up. Keep his round sage-green body, floppy ears, mismatched striped socks, candle-stub cap, size, colours, framing and the transparent background EXACTLY identical. Same style.
```

### `snuffwick_shy` — NICE · transparent

*Attach / edit: `snuffwick_base`.*

```text
Use the attached snuffwick_base image as Image 1. Edit Image 1: change ONLY his face and paws: a shy, happy smile with a tiny blush, big hopeful eyes, both paws clasped together in front of his tummy (the snuffer now leans against his shoulder). Keep his round sage-green body, floppy ears, mismatched striped socks, candle-stub cap, size, colours, framing and the transparent background EXACTLY identical. Same style.
```

### `snuffwick_sock` — NICE · transparent

*Attach / edit: `snuffwick_base`.*

```text
Use the attached snuffwick_base image as Image 1. Edit Image 1: change ONLY his arms and face: instead of the snuffer he hugs a big, lumpy, plum-and-cream striped sock in both arms; something round inside the sock glows warm gold through the fabric; a sly, delighted grin with one eye winking. Keep his round sage-green body, floppy ears, mismatched striped socks, candle-stub cap, size, colours, framing and the transparent background EXACTLY identical. Same style.
```

### `prop_hat` — NICE · transparent

*Attach: butterscotch_base.*

```text
A small, crooked wizard hat in faded purple (#6E5A9E) with two big simple pale-cream (#F3E6C4) five-point stars and a soft floppy tip - the SAME design as the hat Butterscotch wears in the attached butterscotch_base image. Front view, no face, no character, just the hat. Full figure, centred, nothing cropped, on a TRANSPARENT background (PNG). If you can't make it transparent, use a flat pure green #00FF00 background with no shadow and no floor. Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, no text, no letters, no watermark. Spooky-cute and cozy, never scary.
```

### `prop_feather` — NICE · transparent

```text
One long, curly, cartoon tickle-feather in soft lavender with a cream tip: a big, simple, smooth stylised shape (NO fine strands or fluff), angled diagonally from lower right to upper left. Full figure, centred, nothing cropped, on a TRANSPARENT background (PNG). If you can't make it transparent, use a flat pure green #00FF00 background with no shadow and no floor. Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, no text, no letters, no watermark. Spooky-cute and cozy, never scary.
```

### `fx_bubble` — NICE · transparent

```text
A glossy round soap bubble with a soft rainbow rim and a mostly clear centre. Nothing else. Full figure, centred, nothing cropped, on a TRANSPARENT background (PNG). If you can't make it transparent, use a flat pure green #00FF00 background with no shadow and no floor. Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, no text, no letters, no watermark. Spooky-cute and cozy, never scary.
```

### `fx_eyes` — NICE · transparent

```text
Two big, round, friendly cartoon goblin eyes side by side, peering out of darkness: glowing sage-green irises, big dark pupils, bright white star highlights, slightly mischievous happy lids. ONLY the two eyes - no face, no body, nothing else. Cute, never scary. Full figure, centred, nothing cropped, on a TRANSPARENT background (PNG). If you can't make it transparent, use a flat pure green #00FF00 background with no shadow and no floor. Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, no text, no letters, no watermark. Spooky-cute and cozy, never scary.
```

### `chapter_egg` — NICE · background

*Attach: butterscotch_base.*

```text
Storybook wide shot, long ago, inside a cozy rounded cave at night in deep indigo and plum, framed by a big round smooth stone arch, with low soft piles of glossy candy at the sides (no gold coins, nothing gold). In the centre, a tiny BABY dragon - a newborn version of the dragon in the attached butterscotch_base image: the same glossy caramel skin (#C77A35) with a cream belly, little caramel-swirl horn buds, tiny stubby wings and big warm amber-brown eyes, but much smaller and rounder, and with NO hat - sits happily in the bottom half of a big, chunky, cracked-open CRYSTAL eggshell resting on a soft pile of candy. The broken shell pieces are smooth, chunky, faceted crystal, each glowing softly from inside in one of three colours: purple fading to light lilac, clear sky blue, and soft rainbow candy colours (pink, yellow, mint and blue bands). A few of the pieces float gently upward and drift toward three or four small round glowing doorways in the cave walls (doors to other worlds - each one shows a hint of a different place: a starry sky, a green forest, a pink sunset), as if they are leaving to find someone. No people. Format: wide landscape, 5:3 (1600x960 if you can choose; otherwise the widest landscape option). Dark midnight-indigo and plum colours (#140F2E, #2B1650). Keep the BOTTOM QUARTER of the image plain near-black and empty (the screen draws words there). Big simple shapes, clean smooth gradients. Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, no text, no letters, no watermark. Spooky-cute and cozy, never scary.
```

### `print_keyart` — NICE · print

*Attach: butterscotch_base, snuffwick_base.*

```text
Portrait 2:3 poster art: Butterscotch (use the attached butterscotch_base image for his exact design) floating happily with his paws around one glowing crystal; Snuffwick (use the attached snuffwick_base image) peeking out from behind Butterscotch's tail with a cheeky grin; the Hoard Beacon glowing warm amber above them. Generous plain dark space at the top and the bottom for sign text. Dark midnight-indigo cave background. Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, no text, no letters, no watermark. Spooky-cute and cozy, never scary.
```

### Which pictures each animated clip needs

A clip is built as soon as its **needed** pictures exist; *extras* are used when they exist (optional layers, or stand-ins named by `fallback`). Until then the screen draws its own animation for that moment. After a build, `assets/previews/index.html` shows every clip as four frames.

| Clip | Priority | Needs | Extras / stand-ins | What happens |
|---|---|---|---|---|
| `attract` | MVP | `bg_cave`, `butterscotch_sleep`, `butterscotch_peek`, `snuffwick_base` | — | Butterscotch dozes (balloon bob) and peeks one eye open; Snuffwick tiptoes in from the right, giggles (sfx 'munch' at 5.4 s) and scampers off. The device draws the Hoard Beacon's glow (upper-left), the spark counter and the place-crystal hints. |
| `attract_full` | NICE | `bg_cave_lit`, `butterscotch_sleep`, `butterscotch_delight` | `bg_cave`, `butterscotch_peek`, `snuffwick_sock`, `snuffwick_base` | After the lantern is lit: the cave glows; Butterscotch dozes happily; Snuffwick, now the Official Prankster, scampers by hugging his glowing sock (the 2027 tease). |
| `absorb` | MVP | `bg_cave`, `butterscotch_peek`, `butterscotch_surprise` | — | One eye pops open, then surprise. The device draws the beam of crystal-coloured light into his chest (the anchor) and the colour name. |
| `awaken` (+ `awaken_1` … `awaken_3`, one per crystal colour) | MVP | `bg_wakestone`, `prop_crystal`, `butterscotch_surprise`, `butterscotch_delight` | — | One colour variant per crystal colour, awaken_1..awaken_3 (the crystal tinted with color.N_hex: 1 Dragonberry, 2 Starfrost, 3 Rainbow Fizz), rises from the Wakestone; Butterscotch floats down (surprise), then beams (his chest glow starts at 3.0 s). The device adds sparkles + the meter. |
| `greet` | MVP | `bg_cave`, `butterscotch_delight` | — | One visual for every rank (except the Worldwalker hat below); the device draws the rank name and the meter. |
| `greet_r5` | NICE | `bg_cave`, `butterscotch_delight`, `prop_hat` | — | Rank 5 Worldwalker: 'Here's your traveler's hat!' - a little hat pops out of Butterscotch's paws and twirls up beside him (upper right; it never lands on the device-drawn crystal). |
| `charge` | MVP | `bg_cave`, `butterscotch_base` | — | The device draws the tap prompt (TAP! TAP!; the button prompt when a plinth button is fitted), the fill bar and sparkles; his chest glow swells with each tap on the window. |
| `reveal_t0_1` | MVP | `bg_cave`, `butterscotch_surprise`, `butterscotch_giggle`, `snuffwick_chicken`, `prop_coin`, `snuffwick_base` | — | Gag 1 Rubber Chicken: Snuffwick zips in hugging a squeaky rubber chicken, Butterscotch pulls a silly face, poof - it's a hoard coin! Snuffwick giggles off. (Device: a white poof at about (615, 230) at 1.1 s.) |
| `reveal_t0_2` | MVP | `bg_cave`, `snuffwick_base`, `butterscotch_base`, `butterscotch_puff`, `butterscotch_giggle` | `prop_feather`, `fx_bubble` | Gag 2 Hiccup Bubbles: Snuffwick pops up behind Butterscotch's shoulder and tickles his nose with a feather; hic! hic! - bubbles float up. (Device: sparkles in the kid's crystal colour from his mouth.) |
| `reveal_t0_3` | MVP | `bg_cave`, `butterscotch_surprise`, `butterscotch_puff`, `butterscotch_giggle`, `snuffwick_base`, `snuffwick_sneeze` | `fx_eyes` | Gag 3 Snuff-Out: Snuffwick tiptoes in, 'pfft!' - the cave goes dark (no bg = black; only his eyes blink, and the device's lantern sparks + Butterscotch's tummy glow stay on: the kids' light can't be snuffed), Butterscotch sneezes sparkles at 1.4 s and the lights pop back on; Snuffwick sneezes and scampers off. |
| `reveal_t1` | MVP | `bg_cave`, `butterscotch_delight`, `prop_coin` | — | HOARD COIN: the silver coin flips up out of his paws and spins (upper right, clear of the lantern). |
| `reveal_t2` | MVP | `bg_wakestone`, `butterscotch_surprise`, `prop_star` | — | GOLDEN STAR (rare since 2026-10-26): the gold star rises out of the Wakestone and turns once; Butterscotch gasps. |
| `reveal_t2_2` | NICE | `bg_wakestone`, `butterscotch_surprise`, `butterscotch_delight`, `prop_star` | — | Variant 2 (with reveal_t2_2's line): the star wobbles up and Butterscotch's gasp turns into a beam. |
| `reveal_t3` | MVP | `bg_starburst`, `prop_gem`, `butterscotch_delight` | — | DRAGON GEM (the top prize since 2026-10-26): the rays turn slowly, the ruby gem rises and grows (no strobe); Butterscotch beams beside it. |
| `replay` | MVP | `bg_cave`, `butterscotch_wink` | `butterscotch_peek` | Already played tonight: a 'shh' wink (one-eye peek until the wink still exists); the device shows tonight's result dimmed. |
| `nocrystal` | MVP | `bg_cave`, `butterscotch_puff`, `butterscotch_delight` | `fx_bubble` | The dragon blessing: Butterscotch puffs his cheeks and blows a sparkly blessing bubble down to the kid (+1 spark for the lantern). |
| `sleepy` | MVP | `bg_cave`, `butterscotch_yawn` | `butterscotch_sleep` | Unreadable crystal: a big yawn (the sleeping face until the yawn still exists). |
| `goodbye` | MVP | `bg_cave`, `butterscotch_wave` | `butterscotch_delight` | He waves and floats up like a balloon (delighted face until the wave still exists). |
| `milestone` | MVP | `bg_starburst`, `butterscotch_delight` | — | Every 100th crystal: the device adds confetti in the crystal colours + the big station counter in the centre (so Butterscotch sits to the right). |
| `circle` | MVP | `bg_wakestone`, `butterscotch_giggle` | `snuffwick_sneeze` | 3+ crystals in one group: the device orbits small crystals in the group's colours around Butterscotch; Snuffwick peeks in and sneezes at all that glow. |
| `keeper` | NICE | `bg_cave`, `butterscotch_wink` | `butterscotch_peek` | Keeper of the Hoard secret (about 1 visit in 3): a slow push-in on the wink - the only close-up. |
| `moonfull` | NICE | `bg_cave`, `butterscotch_delight`, `butterscotch_giggle`, `snuffwick_sneeze`, `snuffwick_shy` | `bg_cave_lit`, `snuffwick_base`, `snuffwick_sock` | The Hoard Beacon blazes (the lit cave fades in); Snuffwick, dazzled, sneezes, then smiles shyly - invited to the feast as Official Prankster - and scampers off hugging his glowing sock (2027 stinger). |
| `chapter_a` | NICE | `bg_cave_lit`, `butterscotch_sleep`, `butterscotch_delight` | `bg_cave` | Every Halloween he wakes under the lit lantern to share the hoard. |
| `chapter_b` | NICE | `bg_cave_lit`, `bg_cave`, `butterscotch_sleep`, `snuffwick_base` | — | Snuffwick climbs the candy pile under the lantern, 'pfft!' - the lit cave goes dark (the device does not draw the lantern glow in this part). |
| `chapter_c` | NICE | `chapter_egg` | — | Lore: Butterscotch hatched from a crystal egg long ago; its shell pieces scattered through the worlds and became the crystals. |
| `chapter_d` | NICE | `bg_wakestone`, `prop_crystal` | — | The crystal on the Wakestone changes through every crystal colour (Dragonberry, Starfrost, Rainbow Fizz). |
| `chapter_e` | NICE | `bg_cave`, `butterscotch_peek`, `butterscotch_delight` | `bg_cave_lit` | Sparks light the lantern (the lit cave fades in) and Butterscotch wakes up delighted. The device draws tonight's lantern glow over it (meter.on). |
| `chapter_f` | NICE | `bg_cave`, `butterscotch_peek`, `snuffwick_sock` | `snuffwick_base` | The cliffhanger: Snuffwick scampers past hugging a lumpy sock that glows gold inside. |

## 4. Voice lines (Butterscotch)

ChatGPT can't export audio, so make these with a text-to-speech tool:

* **ElevenLabs** (easiest; free plan ≈ 10,000 characters/month is enough for every line): Voice Design or a library voice — A gentle, older male storyteller voice (warm, soft, slightly husky - a kindly grandpa, not a deep booming one). Stability ~40%, Similarity ~75%, Style ~30%, Speed 1.05-1.1.
  * **Let the script record them:** `tools/make_voice_elevenlabs.py` sends every line to ElevenLabs and saves `assets/source/voice/<id>.mp3`, skipping lines that already have a file. Put your API key in `tools/.elevenlabs_key` (git-ignored) or the `ELEVENLABS_API_KEY` environment variable, then from `crystal_oracle/`: `python -I tools/make_voice_elevenlabs.py --list-voices` (pick the voice; add library voices to *My Voices* first) → `--voice "<name>" --only attract_call_1 --force` (try one line, listen) → `--priority MVP` (the night-one set) → no option (everything missing) → `python -I tools/build_assets.py --placeholders`. The voice, model and settings are remembered in `tools/.elevenlabs_voice.json`, so re-takes match. README.md → *Voice lines with ElevenLabs* has the details.
* **OpenAI text-to-speech** (needs an API key; the `gpt-4o-mini-tts` model is being retired on 2027-01-06, so make the lines before then): gpt-4o-mini-tts with voice 'ballad' or 'cedar' (or 'ash'; avoid 'onyx', too deep), the Butterscotch prompt as instructions. Disclose the AI voice on the parent sign.
* **Until then** the builder speaks every missing line with Windows' built-in voice (`--placeholders`), so you can test the whole show today.

**Butterscotch's voice direction (paste into the voice prompt / style box):**

```text
Warm, slow, cozy old dragon grandpa telling a bedtime story: soft and round, a gentle low purr-rumble, smiling, a little whispery, a yawn hiding in some lines, delighted chuckles, bright on exclamations. Never booming, never growly, never a roar. Quick delivery: each line fits in about 2-2.5 seconds.
```

Save each line as its own file named by its **id** (any of WAV / MP3 / M4A) into `assets/source/voice/` — e.g. `attract_call_1.mp3`. Make two takes of each and keep the warmer one; trim silence. Snuffwick is not a separate actor and is mostly sound effects (giggle, squeak, 'pfft'). His only 2 lines (attract_call_8, circle_2) are recorded in Butterscotch's voice; the builder pitches them up 5 semitones at 1.15x speed (voice fx "smudge" - the effect kept its Moonshards name).

| id | Line | Delivery | Priority |
|---|---|---|---|
| `attract_call_1` | Crystal on the Wakestone! Wake me up! | sleepy rumble that brightens on 'Wake me up'; cozy, not loud | MVP |
| `attract_call_3` | Zzz... a goblin snuffed my beacon... zzz. | half-asleep mumble with a soft snore; playful, never sad | MVP |
| `attract_call_4` | Every crystal adds a spark to my beacon! | warm storyteller, hopeful lift at the end | MVP |
| `attract_call_5` | Shh... I hear travelers coming... | excited secret whisper, smiling | MVP |
| `attract_call_6` | Wake me, travelers! My candy hoard is waiting! | bright inviting call across the porch, a happy yawn after 'travelers' | MVP |
| `attract_call_7` | Set your crystal here! I'm awake... almost! | yawny and cozy, a chuckle on 'almost' | MVP |
| `attract_call_9` | Travelers from every world! Come in, come in! | warm, welcoming host calling across the porch on Halloween night, a happy chuckle on the second 'come in' | MVP |
| `attract_tap_1` | No crystal? Tap my window! | friendly and clear, a touch slower on 'tap my window' (the no-crystal kids tap the screen for a dragon blessing; plays instead of attract_call_2) | MVP |
| `awaken_a_1` | Ooh! A brand-new crystal is waking! | sleepy gasp of wonder that wakes right up | MVP |
| `awaken_a_2` | A new crystal! Hello, little light! | tender delight, like greeting a baby bird | MVP |
| `awaken_b_1` | Dragonberry! Purple and sweet, full of dreams! | proud announcement of the name, then dreamy and soft on 'full of dreams' (colour ID 1: the deep-to-light purple Deep Sea Realm crystals - some look darker, some lighter) | MVP |
| `awaken_b_2` | Starfrost! Sparkly sky blue, full of wonder! | proud, then hushed amazement on 'wonder' (colour ID 2: the glittery sky-blue crystals) | MVP |
| `awaken_b_3` | Rainbow Fizz! Every colour, full of giggles! | proud and bubbly, then a giggle on 'giggles' (colour ID 3: the rainbow-candy glitter crystals) | MVP |
| `awaken_c_1` | Sparkfinder! Welcome, little traveler! | joyful welcome, bright on 'Sparkfinder' (also the rush-mode line, so the rank comes first) | MVP |
| `awaken_c_2` | Sparkfinder! Look, two sparks already! | delighted, impressed, quick | MVP |
| `greet_r2_1` | Apprentice! You found your way back! | thrilled reunion, a warm chuckle | MVP |
| `greet_r3_1` | Glowcaster! Your glow is getting big! | proud announcement, rising | MVP |
| `greet_r4_1` | Hoardguard! My trusty candy protector! | proud and a little grand, like knighting someone | MVP |
| `greet_r5_1` | Worldwalker! Here's your traveler's hat! | awed and proud, a magic-trick flourish on 'Here's your hat!' (the hat pops out on screen) | MVP |
| `greet_r6_1` | Dragon Rider! Climb aboard, old friend! | excited, adventurous, warm | MVP |
| `greet_r7_1` | Keeper of the Hoard! I bow to you! | grand and reverent, but smiling | MVP |
| `charge_tap_1` | Tap the window! Pump up the glow! | urgent-fun cheer, quick (kids tap the screen to fill the glow; plays instead of the charge lines) | MVP |
| `charge_tap_2` | Tap tap tap! Fill your crystal! | bouncy encouragement, three quick light taps in the voice (plays instead of the charge lines) | MVP |
| `reveal_t0_1` | A rubber chicken?! Poof! Candy anyway! | gag 1 Rubber Chicken: silly disbelief, a big laugh, then reassuring and warm on 'Candy anyway!' | MVP |
| `reveal_t0_2` | Hic! Snuffwick tickled me! Hic! Candy anyway! | gag 2 Hiccup Bubbles: real little hiccups, giggling; reassuring on 'Candy anyway!' | MVP |
| `reveal_t0_3` | Lights out?! Ah... ah... CHOO! Candy anyway! | gag 3 Snuff-Out: startled-silly in the dark, a big sparkly sneeze about 1.4 s in (the lights pop back on), then a happy chuckle on 'Candy anyway!' | MVP |
| `reveal_t1_1` | Hoard coin! A big shiny one! | happy, admiring, warm | MVP |
| `reveal_t1_2` | Hoard coin! Shiny silver treasure! | sweet and pleased, a little chuckle | MVP |
| `reveal_t2_1` | Golden Star! Whoa, a rare one! | genuinely surprised, an excited whisper-shout | MVP |
| `reveal_t2_2` | Golden Star! Not many travelers find those! | thrilled, conspiratorial | MVP |
| `reveal_t3_1` | The Dragon Gem! You're legendary! | awed hush rising to joy | MVP |
| `reveal_t3_2` | Dragon Gem! The rarest treasure of all! | breathless wonder, slow and grand | MVP |
| `bowl_common_1` | Silver coin! Take one treat! | clear and friendly, crisp on 'Silver coin' | MVP |
| `bowl_rare_1` | Gold star! Take one treat! | clear, excited, crisp on 'Gold star' | MVP |
| `bowl_legend_1` | Dragon gem! The grown-up has it! | celebratory, pointing-out tone | MVP |
| `replay_1` | Shh! Your crystal already glowed tonight! | secretive and kind, smiling | MVP |
| `replay_2` | Still glowing from tonight! Clever crystal! | gentle and proud, no scolding | MVP |
| `nocrystal_1` | A dragon blessing! Whoosh, you sparkle! | generous and cheery, a soft 'whoosh' like blowing a bubble | MVP |
| `nocrystal_2` | Dragon blessing! You added a spark too! | warm thank-you, inclusive | MVP |
| `sleepy_1` | Again, please! Sleepy crystal. | soft and patient, a yawn in it | MVP |
| `sleepy_2` | Place it again! Your crystal's yawning. | giggly and reassuring | MVP |
| `goodbye_1` | Bring your crystal back next Halloween! | warm send-off, bright | MVP |
| `goodbye_2` | Keep it safe, traveler! See you next year! | fond, a sleepy-happy sigh, smiling | MVP |
| `attract_call_2` | No crystal? Press the big button! | friendly and clear, a touch slower on 'big button' (only plays when a plinth button is fitted, config.ini button = yes; without one attract_tap_1 plays instead) | NICE |
| `attract_call_8` | *Snuffwick (record in Butterscotch's voice; the builder pitches it up):* Pfft! Hee hee! I like the dark! | Snuffwick: record in Butterscotch's voice - the builder pitches it up 5 semitones at 1.15x; a tiny shy giggle, mischievous, never mean | NICE |
| `greet_r2_2` | Apprentice! Your crystal remembers you! | tender and proud | NICE |
| `greet_r3_2` | Glowcaster! Welcome back! Shine on! | cozy and admiring | NICE |
| `greet_r4_2` | Hoardguard! Keep those goblins away! | playful-conspiratorial, a wink in the voice | NICE |
| `greet_r5_2` | Worldwalker now! Here's your very own hat! | warm and delighted, presenting a gift | NICE |
| `greet_r6_2` | Dragon Rider! You're back! Wheee! | giddy, a floaty 'Wheee' | NICE |
| `greet_r7_2` | Keeper! The whole hoard rejoices! | deeply warm, like hugging an old friend | NICE |
| `charge_1` | Press the button! Pump up the glow! | urgent-fun cheer, quick (only with a plinth button, button = yes) | NICE |
| `charge_2` | Press fast! Fill your crystal! | bouncy encouragement (only with a plinth button, button = yes) | NICE |
| `recall_t0_1` | Last year, Snuffwick tricked you! Silly goblin! | remembering fondly, a chuckle | NICE |
| `recall_t1_1` | Last year, a shiny hoard coin! | fond memory, warm | NICE |
| `recall_t2_1` | Last year, a Golden Star! Remember? | impressed memory | NICE |
| `recall_t3_1` | Last year, the Dragon Gem! Legendary! | awed remembering | NICE |
| `rush_t0_1` | Goblin trick! Silver coin bowl! | fast, giggly, bowl words crisp (rush mode) | NICE |
| `rush_t1_1` | Hoard coin! Silver coin bowl! | fast and bright (rush mode) | NICE |
| `rush_t2_1` | Gold star! Golden Star! | fast, excited (rush mode) | NICE |
| `rush_t3_1` | Dragon Gem! The grown-up has it! | fast but awed (rush mode) | NICE |
| `milestone_1` | Another hundred crystals! Hooray, travelers! | party cheer, bubbly | NICE |
| `circle_1` | A crystal circle! All the worlds together! | delighted, gathering-everyone warmth | NICE |
| `circle_2` | *Snuffwick (record in Butterscotch's voice; the builder pitches it up):* Eek! Too much glow! Hee hee! | Snuffwick (pitched up by the builder): a dazzled squeak, then a giggle | NICE |
| `keeper_1` | Psst, Keeper... my real name's Bartholomew. | close secret whisper, barely holding a chuckle | NICE |
| `keeper_2` | Shh... my wings are tiny. I just float! | embarrassed confiding whisper, then a tiny giggle | NICE |
| `moonfull_a_1` | My beacon is lit! You did it, travelers! | overjoyed triumph, the biggest moment of the night - warm, never booming | NICE |
| `moonfull_b_1` | Silly Snuffwick... come to the feast! You're my Official Prankster! | fond forgiving chuckle, then a proud announcement | NICE |
| `chapter_a_1` | Every Halloween, the doors between worlds open... and I wake up! | bedtime-story opening, unhurried, a little yawn at the start, waking up bright on 'and I wake up!' | NICE |
| `chapter_b_1` | But sneaky Snuffwick snuffed my beacon! Pfft! How will travelers find me? | mock-outraged, giggling; a soft 'Pfft!', then a worried-silly little gasp on 'How will travelers find me?' | NICE |
| `chapter_c_1` | Long ago, I hatched from a crystal egg. Its pieces are yours! | hushed once-upon-a-time wonder, a proud little chuckle on 'hatched', leaning in on 'Its pieces are yours!' | NICE |
| `chapter_d_1` | Each crystal glows when it's near me... and glow wakes dragons! | tender, then delighted | NICE |
| `chapter_e_1` | Every crystal adds a spark. Light my beacon, travelers! | clear instruction, rising excitement | NICE |
| `chapter_f_1` | Psst... what's Snuffwick hiding in that lumpy sock? | curious, teasing whisper (the 2027 cliffhanger) | NICE |

Variant numbers matter: the screen picks a random `_1`, `_2`, … of each group and stops counting at the first gap. `awaken_b_1` … `awaken_b_3` are special — the number is the crystal's colour id set on the bench (1 Dragonberry, 2 Starfrost, 3 Rainbow Fizz). A new colour later = a new `color.N` / `color.N_hex` in the manifest words plus its `awaken_b_N` line.

**No big button** (`config.ini` `[station] button = no`, the default): the screen plays the stand-ins instead — `attract_call_2` → `attract_tap_1`, `charge` → `charge_tap` (words `nobutton.voice`). The button lines are only needed if a button is added later (`button = yes`).

## 5. Sound effects

Free sources: **Pixabay Sound Effects** (free, no attribution needed), **Freesound.org** (check each sound's licence; CC0 is easiest), or **ElevenLabs Sound Effects** (type the description). Keep them short and gentle. Save as `assets/source/sfx/<id>.wav` (or .mp3). Missing ones are replaced by the screen's built-in chimes.

| id | What it should sound like | Search words | Priority |
|---|---|---|---|
| `absorb` | Crystal placed: a soft whoosh up into a warm glass chime (light flying into Butterscotch's tummy). | magic whoosh chime, glass chime rise | MVP |
| `awaken` | A new crystal wakes: rising magical shimmer, twinkly harp glissando, warm and gentle. | magic shimmer, harp glissando, fairy sparkle | MVP |
| `reveal_t0` | Generic GOBLIN TRICK! sting: a playful cartoon boing / slide-whistle + a tiny goblin giggle (never a fail buzzer). | cartoon boing, slide whistle, small giggle | MVP |
| `reveal_t0_1` | Gag 1 Rubber Chicken (starts 0.65 s into the gag): two squeaky rubber-chicken squeaks, then a soft magic 'poof' + a coin 'ting' at about 0.5 s. | rubber chicken squeak, cartoon poof, coin ting | NICE |
| `reveal_t0_2` | Gag 2 Hiccup Bubbles: two cute 'hic!' hiccups, each with a soap-bubble pop. | cartoon hiccup, bubble pop | NICE |
| `reveal_t0_3` | Gag 3 Snuff-Out (starts 0.65 s into the gag): a tiny candle 'pfft' (the lights go out), a beat of quiet with a little goblin giggle, then a bright sparkly shimmer at about 0.75 s (the sneeze relights the cave; Butterscotch's voice says the 'CHOO'). | candle snuff out, small giggle, magic sparkle burst | NICE |
| `reveal_t1` | HOARD COIN: a bright coin 'ting' + a twinkly two-chord chime, happy. | coin ting, twinkle chime, magic bell | MVP |
| `reveal_t2` | GOLDEN STAR (rare): a magical swirl + sparkle arpeggio, wondrous. | magic spell swirl, sparkle arpeggio | MVP |
| `reveal_t3` | DRAGON GEM (top prize): harp glissando + a soft warm gong (the host's cue to come over). No harsh brass. | harp glissando, soft gong, magical reveal | MVP |
| `tap` | Each tap on the window (or button press) during charge: a tiny bright blip (it repeats fast). | ui blip, bubble pop short | MVP |
| `button` | Only with a plinth button (config.ini button = yes; none in 2026): a friendly bloop when it's pressed with no crystal on the stone. | button bloop, cartoon pop | NICE |
| `charge_full` | Charge meter full: a quick rising 'ding-ding-DING'. | level up ding, power up chime | MVP |
| `sleepy` | Sleepy crystal: a soft descending double-bloop with a tiny yawn feel, gentle (never an error buzz). | soft bloop descending, gentle cartoon | MVP |
| `whoosh` | Soft airy whoosh for transitions (goodbye float-up, the blessing bubble, rank-ups). | soft whoosh, air swish | MVP |
| `munch` | Snuffwick in the attract loop (played ~5.4 s into each loop as he peeks in; the id stays 'munch' for the firmware): a tiny shy goblin giggle + a soft candle 'pfft'. | cute small giggle, candle blow out | NICE |
| `milestone` | Milestone: a party-horn toot + a cheer + sparkles, kid-friendly. | party horn kids, celebration cheer | NICE |
| `moonfull` | The Hoard Beacon lights (finale): a warm 'whoomph' of a big lantern catching, swelling into a happy magical chord. Never a roar. | lantern whoosh ignite soft, magical swell, triumphant fairy chord | NICE |

## 6. Music

Save as `assets/source/music/<id>.mp3` (or .wav). It loops under everything and ducks when Butterscotch talks.

| id | What | Search words | Priority |
|---|---|---|---|
| `idle` | Seamless 45-90 s loop: a gentle spooky-cute music-box / celesta lullaby with soft pizzicato and a cozy low bassoon, slow, warm, never scary. Quiet enough to talk over. | music box lullaby halloween, celesta spooky cute, cozy halloween loop | MVP |
| `idle_full` | The same lullaby in a brighter, happier feast-time arrangement for after the Hoard Beacon is lit. | happy music box halloween, cozy feast celesta, playful pizzicato loop | NICE |

## 7. Printables (nice to have)

* **Bowl signs** — print `prop_coin` (silver coin bowl: HOARD COIN, also the GOBLIN TRICK bowl), `prop_gem` (dragon gem, held by the host: DRAGON GEM, the top prize) and `prop_star` (gold star bowl: GOLDEN STAR) so the bowls match the screen picture-for-picture. Light each sign with a battery LED puck.
* **Parent sign** — `print_keyart` with the sign text from STORY.md §13. Keep the line *"Butterscotch's voice is AI-generated."* (OpenAI's and ElevenLabs' terms ask you to disclose AI voices.)

## 8. After you have the files

```bash
cd crystal_oracle
python -I tools/build_assets.py --check          # what's still missing (writes assets/ASSET-STATUS.md)
python -I tools/build_assets.py --placeholders   # build sd_card/ (fills gaps with the Windows voice + synth sounds)
```

Then copy the `sd_card/oracle` folder to the root of the screen's TF card. See `README.md` for the full setup.
