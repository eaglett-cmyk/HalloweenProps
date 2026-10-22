<!-- Story bible for the Crystal Oracle station. Written 2026-10-22 from a 4-concept pitch + 3-judge panel (scores: Moonshards 23.5, Lumpkin 21.5, Madame Mira 20, Dragonglow 19.5). -->

> **ARCHIVED 2026-10-22.** The owner chose *Dragonglow* instead (see `../../STORY.md`). This Moonshards bible and its manifest (`manifest.moonshards.json`, made by `tools/gen_manifest_from_story.py`) are kept so the story can be swapped back in: copy the manifest to `assets/manifest.json`, then run `build_assets.py --gen-defaults` and `gen_asset_list.py`.

# MOONSHARDS — Oona's Halloween Window

*Station: the Crystal Oracle · Chapter 1, Halloween 2026: "The Nibbled Moon"*

> **Logline.** Every Halloween, a sleepy lantern ghost named Oona opens her window and lights the Moon so every trick-or-treater can find their way home. This year Smudge, a fluffy marshmallow moth, nibbled it down to a sliver. The only thing that can fill it again is the light kids carry in their crystals.

**What every kid gets:** your crystal does the magic, you are the hero, and you always get candy.

| At a glance | |
|---|---|
| The screen | **Oona's Window**. Dress the box as a round attic window with two little curtains. |
| The plinth | **the Glowstone**: a grey stone pedestal with a carved crescent. |
| Main voice | **Oona**, the lantern ghost. She is the only TTS voice. |
| Mischief-maker | **Smudge**, the marshmallow moth. He is almost entirely sound effects. |
| What the kids are | **Lightkeepers** |
| The crystals | **Moonshards** in the story; always just "crystal" out loud |
| Tonight's team goal | Fill the **Moon meter**. Every visitor counts, with or without a crystal. |
| Results | SMUDGED! · SILVER MOON · WITCH'S HAT · WISHING STAR |
| Bowls | silver circle · silver circle · red triangle · gold star (the host holds it) |

## 1. What changed from the pitch

Moonshards won the judging on clarity. Each result has the same name as its bowl, no crystal shares a colour with a bowl, it has a real rush mode, and the nightly goal includes every visitor. This version keeps all of that and fixes what the judges flagged:

- **No state bug.** At the chapter finale Smudge gets dazzled, says sorry, and becomes the Window's **Official Trickster** (taken from *Madame Mira*). He keeps playing silly tricks after the Moon is full and in every later year, which is also why tricks exist at all.
- **One voice per line.** Every trick line is spoken by Oona alone. Smudge's sneeze, munch and burp are sound effects.
- **Less "Moon".** The plinth is the Glowstone, the kids are Lightkeepers, and crystals are called "crystals" out loud. **Bowl lines say only colour and shape.**
- **Ranks worth bragging about** (from *Dragonglow*): Apprentice, Wizard, Moon Rider, Moon Guardian.
- **The bowl signs match the screen** (from *Dragonglow*). The silver moon, red witch's hat and gold star the kids see on screen are printed as the bowl signs.
- **Her lantern shows your colour** (from *Lumpkin*). Oona's lantern glows in the kid's crystal colour and pulses with her voice, so she seems to talk without needing mouth frames.
- **No public low score** (from *Mira*). On SMUDGED! a fuzzy smudge hides the GLOW number.
- **Gold is only for the Wishing Star.** Oona's lantern is dark iron and the star-wisp is silver.
- **Built for the real display.** There is no mist or bokeh, because both band badly on MJPEG/RGB565. The MVP set is 18 stills, the chapter reuses sprites, and lines that can't play in 2026 get recorded later.

## 2. How it plays

### Walking up (attract)
- **What kids see:** a round window at night. Oona is asleep and bobbing, with z's floating up. A thin crescent Moon in the top right fills up as the night goes on. Every so often Smudge flutters up to nibble it.
- **Counter:** "✦ 23 CRYSTALS AWAKENED".
- **Calls:** about every 30 s Oona half-wakes and calls out ("Crystal on the stone! Wake me up!").
- **Glowstone:** its LED ring breathes slowly through the six crystal colours.

**The story is told here, in the attract loop. A kid standing at the stone never has to sit through it.**

### First visit (2026: almost every kid)
The host hands over a crystal with the ritual line (§14), and the kid sets it on the Glowstone.

1. **absorb (0.0 s).** Within 0.1 s the ring snaps to the crystal's colour and a shimmer sound plays. A column of light rises into Oona's lantern, and the colour name appears on screen ("TWILIGHT").
2. **awaken (1.0 s).** The crystal rises on a moonbeam. Oona says:
   - "A brand-new crystal! It's waking up!"
   - "Twilight violet! The crystal of dreamers!"
   - "You're a Spark! Welcome, Lightkeeper!"

   Two of the eight facets light up, with a chime for each.
3. **reveal (~7.5 s).** SILVER MOON, WITCH'S HAT or WISHING STAR appears, and the GLOW number counts up. The bowl line plays over the end of the reveal.
4. **goodbye (~10 s).** It plays while the kid walks to the bowl.

The result is written to the crystal before awaken starts, so lifting the crystal early loses nothing. The default setting `firstNoTrick` stays on, so a crystal's first visit is never SMUDGED!.

### Returning crystal (2027 onward)
The sequence is absorb (1 s), then **greet** (2 s), then the **reveal** with the bowl line over its end, then goodbye. That's about 5.5 s.
- **greet** shows the rank name, with last year's facets already lit and the new facet popping on.
- **recall** ("Last year, you found the Witch's Hat!") and the **charge** window (2.5 s) play only when nobody is waiting.

### No crystal (the big button)
A star-wisp drifts down from Oona's lantern and the Moon meter goes up by 1, because this kid helped too. Then Oona says "Silver circle! Take one treat!" The whole thing takes about 4 s.

### Crystal already used tonight
**replay** (2 s). Oona says "Shh! Your crystal already glows tonight!" Tonight's result shows again, dimmed, with a ✓. There is no bowl line unless the `secondHelpings` setting is on.

### Bad read, or a tag that isn't ours
- If the tag can't be read, **sleepy** plays ("Set it down again! Sleepy crystal."). It is never shown as an error.
- If the tag isn't ours (a bus pass, or an old token without our marker), the kid gets the **no-crystal** path. The tag is never written to.

### Rush mode (turns on automatically when a crystal arrives within 8 s of the last one)
- **First visit:** absorb (0.5 s, colour shown as a caption only), then awaken_c only ("You're a Spark!"), then a 1.5 s reveal with one **combined line**: "Silver circle! Silver Moon!" Total is about 3.5 s.
- **Returning crystal:** the rank appears as a caption only, then the combined line. Total is about 3 s.
- **Skipped in rush:** awaken_a and awaken_b, recall, charge and goodbye.
- **If rush lines aren't built yet,** the bowl line plays on its own.

| Path | Normal | Rush |
|---|---|---|
| First visit | ≈ 10 s (the awakening is the show) | ≈ 3.5–4 s |
| Returning | ≈ 5.5 s (+2.5 s charge, +2 s recall when nobody is waiting) | ≈ 3 s |
| No crystal | ≈ 4 s | ≈ 3 s |
| Replay / sleepy | 2 s | 2 s |

**A group of 8 brand-new kids takes under 45 s:** about 10 s for the first kid, then about 4 s each. Hand out the crystals while the first kid is at the stone.

## 3. Characters

### Oona, the lantern ghost (main voice)

**How she looks (for the illustrator)**
- A small, chubby grandmother ghost shaped like a soft teardrop that ends in one wispy curl. She has no legs.
- Moonlight-white body with a faint mint glow around the edge.
- Big kind dark eyes, rosy cheeks and tiny round **silver** spectacles.
- A floppy indigo nightcap with a small **silver** star on the tip.
- Her stubby arms hold an old **dark-iron** lantern in front of her tummy. The lantern's globe is **round, clear and empty**, because the device paints the kid's colour into it.
- Nothing gold and nothing sharp.

**Personality**
- Cozy and giggly, and a little forgetful (she overslept this year).
- Fiercely proud of "her Lightkeepers".
- Never scolds anyone and never sounds scared. When Smudge pranks her, she laughs.

**TTS voice direction**
- **Prompt to paste:** "Warm, playful old ghost grandmother telling a bedtime story. Smiling, slightly whispery, sing-song, bright on exclamations. Never cackles, never spooky. Quick delivery: each line fits in about 2 seconds."
- **ElevenLabs:** use a gentle, older female storyteller voice. Settings: Stability ~40%, Similarity ~75%, Style ~30%, Speed 1.05–1.1.
- **OpenAI gpt-4o-mini-tts:** use `fable` or `sage` with the prompt above.
- **Recording:** make two takes of each line and keep the warmer one. Trim the silence to 40 ms or less at each end.
- **File format:** any WAV/MP3/M4A is fine. Name each file after its line id from `CHATGPT-ASSETS.md` (for example `attract_call_1.mp3`), drop it in `assets/source/voice/`, and the asset builder converts it to the 48 kHz mono WAV the screen plays.

**How she moves**
- Her sprite bobs ±6 px on a 2.4 s sine wave, with a 1° sway and a 2% breathing scale. This motion is baked into the clip.
- Her expressions crossfade over 250 ms, and every expression is an edit of `oona_base`.
- The device draws a glow at a **lantern anchor (x, y) stored for each still**. The glow is tinted to the crystal colour and pulses with `voiceLevel()`.

### Smudge, the marshmallow moth (troublemaker, almost all sound effects)

**How he looks**
- He looks like a **plush toy, never a bug**: a round pom-pom of lavender-grey fluff about the size of a cat.
- A fluffy cream collar, tiny fern antennae, and two little soft wings with pale moon spots.
- Huge glossy black eyes with star highlights, and one small rounded buck tooth.
- No legs, no body segments, and nothing that looks like a real insect.
- A soft rim light so he stands out against the dark sky.

**Personality:** greedy and giggly, but never mean. He just thinks moonlight tastes like toasted marshmallow. Light is his weakness, because moths can't stop staring at it.

**Sound:** cartoon sound effects only: munching, a squeaky giggle, a tiny "ah-ah-CHOO!" and a small "hic-burp". He has one optional line, which is Oona's TTS pitched up 5 semitones and sped up 1.15×.

**How he moves:** he bobs fast (0.4 s per bob). A 3% horizontal squash at about 6 Hz makes him look like he's fluttering.

### The star-wisp (silent)
A tiny, smiling **silver** star with a wispy tail. Oona sends one to every kid who doesn't have a crystal.

## 4. Lore

**Where the crystals came from.** On the very first Halloween, a pumpkin told the Moon a joke so funny that the Moon laughed until sparkles fell off. The sparkles landed in a pumpkin patch as **Moonshards**.
- A Moonshard sleeps until it finds its **Lightkeeper**.
- All year it soaks up that child's own colour of light.
- It remembers only numbers: how many Halloweens it has visited, and each year's GLOW. It never remembers names.

**Why it works on Halloween.** The veil is thin on Halloween night. Light can travel from a crystal on the Glowstone, through Oona's Window and into her lantern, then up to the Moon.

**Oona's job.** She lights the Halloween Moon so every trick-or-treater can find the way home. She sleeps the other 364 nights.

**Smudge** wants to eat that moonlight. He isn't bad, just hungry and a little lonely: nobody ever shares with moths.

**How the kids win, together**
- Every visitor adds light, and the **Moon meter** fills up one phase at a time.
- When the Moon is full, Smudge is dizzy-dazzled by the brightest light he has ever seen. He tumbles, then says sorry.
- Oona makes him **Official Trickster** of the Window. He may play one silly trick now and then, but he never takes anything, and the kid always gets candy.

This deal holds in every future year.

## 5. Crystal colours

The ID is the colour code written to the tag at the bench (page 17). It matches the firmware's default order, so crystals that are already made don't need new codes. Silver, red and gold belong to the bowls and are **never** used as crystal colours.

| ID | Resin | Name | Screen hex | The crystal of… |
|---|---|---|---|---|
| 1 | violet | **Twilight** | #A35CFF | dreamers and storytellers |
| 2 | orange | **Ember** | #FF7A1A | brave hearts |
| 3 | ice blue | **Frost** | #5CD6FF | the cool and calm |
| 4 | green | **Potion** | #3DFF7A | clever tricksters |
| 5 | pink | **Pixie** | #FF6FC8 | best friends |
| 6 | clear / white glitter | **Ghostlight** | #EAFBFF | those who see hidden things |

On screen and on the LEDs, Ghostlight shows as a cool white with a slow mint shimmer.

## 6. Ranks

Ranks count **total** visits, not streaks, so missing a year never lowers a kid's rank.

The meter is an 8-facet gem drawn by the device. It lights `min(8, visits + 1)` facets:
- A first visit already shows 2 of 8: one for finding you, one for tonight.
- Visit 7 fills all 8 facets, and visit 8 crowns the kid as Moon Guardian.

| Visits | Rank | Meaning | Aura (ring on screen + plinth LEDs) |
|---|---|---|---|
| 1 | **Spark** | Your crystal just woke up | one soft ring; slow LED breathing |
| 2 | **Apprentice** | Oona is teaching you light magic | ring + one orbiting sparkle |
| 3 | **Lamplighter** | You light the way home | ring + a candle-flicker on the crystal's tip |
| 4 | **Wizard** | You can cast starlight | double ring + three orbiting sparkles |
| 5 | **Starcatcher** | Even the stars wave at you | five tiny silver stars in orbit |
| 6–7 | **Moon Rider** | You ride the crescent Moon | a silver crescent sweeping around the crystal |
| 8+ | **Moon Guardian** | Guardian of the Halloween Moon | full silver halo + a crown of sparkles |

## 7. Tiers, bowls and tricks

Each result has the same name as its bowl, so a three-year-old hears it twice and then sees the same picture on the bowl. The GLOW number ranges below are the firmware's tier bands, which stay compatible with the skull.

| Tier | Odds | GLOW | Reveal | Bowl | Sign art |
|---|---|---|---|---|---|
| 0 Trick | ~15% | 1–39 (hidden) | **SMUDGED!** | **silver circle** | silver moon |
| 1 Treat | ~63% | 40–89 | **SILVER MOON** | **silver circle** | silver moon |
| 2 Rare | ~18% | 90–99 | **WITCH'S HAT** | **red triangle** | red witch hat |
| 3 Legendary | ~4% | 100 | **WISHING STAR** | **gold star**, held by the host | gold star |

### Setting up the bowls
- **Silver circle:** a big round stainless or foil bowl.
- **Red triangle:** a red bowl.
- **Gold star:** stays with the host. A star-shaped basket works well.
- **Signs:** print each sign from its prop still, and light it with a battery LED puck so the colour reads on a dark porch.
- **Layout:** put the bowls beside the Glowstone so the line flows from stone to bowl to sidewalk.

### Rules for SMUDGED!
- Every gag ends with candy.
- The joke is always on Oona or Smudge, never on the kid.
- The GLOW number stays hidden.
- The same gag never plays twice in a row.

### The three trick gags
1. **The Sneeze.** Smudge zips into the beam and goes "ah-ah-CHOO!" Polka dots in the kid's crystal colour sprinkle across the screen. *"Bless you, Smudge! Candy's still yours!"*
2. **The Bubble Burp.** Smudge gobbles a sparkle and goes *hic-burp*. A rainbow bubble floats down **into the kid's crystal**, so the trick gives the kid something. *"He burped you a bubble! Candy anyway!"*
3. **Pumpkin Head.** Smudge zips past, *poof*, and Oona has a smiling jack-o'-lantern stuck on her head. *"Pumpkin on my head?! Candy's still yours!"*

## 8. Story

### Chapter 1 (2026): "The Nibbled Moon"
1. Every Halloween, Oona wakes in her window and raises her lantern to light the Moon.
2. This year she overslept, and Smudge nibbled the Moon down to a thin crescent, like a cookie.
3. Her lantern can't refill it alone. Then she remembers the Moonshards, and that kids carry them now.
4. All night, each crystal on the Glowstone sends its light up, and the Moon meter fills one phase at a time. Star-wisps from kids who press the button count too.
5. Smudge keeps sneaking snacks (the SMUDGED! gags), but every bit of light makes him dizzier.
6. **Finale (the goal is reached).** The Moon blazes full. Smudge spins, dazzled, and lands looking sheepish. Oona says, "Silly Smudge… you're my Official Trickster now!" The pumpkins on the hill light up and Halloween is saved by the Lightkeepers.
7. **Stinger for 2027:** Smudge hugs something twinkly behind his back.

**Every visit works on its own.** A kid who arrives at 8:30 pm still lights a crystal and still helps the Moon.

**The finale always happens.** The goal is set by the host (`moon_goal`, default 50 points). If the night is slow, the host taps **Fill the Moon** in the hidden host menu and the finale plays.

### Hooks for later years
The window, the Glowstone, Oona and the meter all stay the same. Each new year needs about 5 new stills and 8 new lines.
- **2027, "The Hidden Stars".** Smudge, now the Trickster, hid the stars for a game of hide-and-seek and forgot where he put them. Each crystal lights up a pumpkin to find one, and the meter becomes a sky filling with stars.
- **2028, "The Fog Party".** Smudge's fluffy cousins, the Fuzzles, throw a fog party that hides the way home. The crystals become a lighthouse, because Lightkeepers keep the light.
- **2029, "Smudge's Crystal".** Smudge wants to be a Lightkeeper. The Wizards help grow him a tiny lavender crystal. (The Wizards are the 2026 Sparks on their fourth visit.) This sets up a new troublemaker for 2030.

## 9. Cue sheet

**Applies to every cue unless the row says otherwise**
- **Bottom band (y ≥ 360):** the device draws the captions, the GLOW number, the 8-facet meter and the bowl icons. Each bowl icon is a coloured shape with a white outline that pulses at 1 Hz.
- **Moon meter:** drawn live by the device as a silver Moon in the top-right sky (centre about 650, 110) that grows from a thin crescent to full over 8 phases.
- **Pulses and flashes:** pulses run at 2 Hz or slower, with no more than 3 bursts per second.
- **LED ring:** shows the crystal's colour.

| Cue | Visual | Voice (Oona unless noted) | Caption |
|---|---|---|---|
| **attract** | `bg_window` on a slow 40 s zoom. `oona_sleep` with z's, switching to `oona_peek` for 3 s about every 12 s. About every 45 s `smudge_base` flutters to the Moon, swaps to `smudge_munch` with sparkle crumbs, and sometimes `smudge_sneeze`. Moon phase, counter and the place-crystal hint are drawn on top. LEDs cycle through the six colours. After the finale it switches to `attract_full` (`bg_window_lit`). | `attract_call` every ~30 s (see the list below this table) | CRYSTAL HERE / OR PRESS |
| **absorb** (1 s) | A column of light in the crystal colour rises into the lantern globe, and Oona switches to `oona_surprise`. LEDs snap to the colour within 0.1 s. | Sound effect only: whoosh + glass chime | the colour name |
| **awaken** (6–8 s) | `bg_awaken`. A beam lands on the stone and `prop_crystal`, tinted to the crystal colour, rises with slow rays. `oona_surprise` floats in, then changes to `oona_delight`. Two facets pop on, each with a chime. | `awaken_a` → `awaken_b` (**variant = colour ID**) → `awaken_c`. Rush mode: `awaken_c` only. | NEW CRYSTAL! / colour / SPARK! |
| **greet_r2…r7** (2 s) | `bg_window` with `oona_delight` (or `oona_proud`). The tinted crystal shows its rank aura, and the new facet pops on. | one line per rank (see the list below this table) | the rank name |
| **recall_t0…t3** | A faded icon of last year's result floats up in a corner. | "Last year, you found the Witch's Hat!" | LAST YEAR |
| **charge** (2.5 s; returning crystals only; skipped in rush) | `oona_base` holds the lantern forward while a button icon pulses. Each press sends a sparkle up to the Moon. When the meter is full, a `charge_full` chime plays. The fill nudges the GLOW number and slightly raises the rare odds, and it counts before the result is written. | "Press the button! Fill the Moon!" | PRESS! |
| **reveal_t0** (2.5 s) | Visuals `reveal_t0_1…3`, one per gag in §7: sneeze with polka dots and `oona_giggle`; burp with the bubble going into the crystal and a rainbow shimmer on the LEDs; `oona_pumpkin`. LEDs wobble lavender. | the line for that gag | SMUDGED! |
| **reveal_t1** (2.5 s) | `oona_delight`. A small silver moon rises from the lantern and joins the big Moon, which pulses. | "Silver Moon! Your crystal shines so bright!" | SILVER MOON |
| **reveal_t2** (2.5 s) | `bg_awaken` with `oona_surprise`. `prop_witchhat` spins up out of the lantern. In variant 2, Oona winks wearing the hat (`oona_witchhat`). LEDs chase red and the crystal colour. | "Ooh! A Witch's Hat! So rare!" | WITCH'S HAT! |
| **reveal_t3** (3 s) | `bg_starburst` with slowly turning rays. `prop_wishstar` rises and grows. A 1 Hz glow, no strobe. A harp and gong play, which is the host's cue to come over. LEDs swirl slowly in gold. | "The Wishing Star! Make a wish!" | WISHING STAR! |
| **bowl_common / bowl_rare / bowl_legend** | big bowl icon during the last second of the reveal | "Silver circle! Take one treat!" · "Red triangle! Take one treat!" · "Gold star! The grown-up has it!" | SILVER CIRCLE · RED TRIANGLE · GOLD STAR |
| **rush_t0…t3** (nice-to-have) | 1.5 s reveal with the bowl icon showing from the start | "Silver circle! Silver Moon!" and so on | the tier name |
| **replay** (2 s) | `oona_peek` (or `oona_shh`). Tonight's result and GLOW number appear dimmed, with a ✓ and the word TONIGHT. | "Shh! Your crystal already glows tonight!" | ALREADY GLOWING |
| **nocrystal** (3–4 s, then `bowl_common`) | `oona_delight`. `prop_starwisp` drifts from the lantern to the bottom centre, and the Moon meter goes up by 1. LEDs glow a random crystal colour. **foreign** reuses this cue. | "A star-wisp for you! Happy Halloween!" | STAR-WISP! |
| **sleepy** (2 s) | `oona_sleep` and the place-crystal hint with a ↻ arrow. LEDs breathe a dim white. | "Set it down again! Sleepy crystal." | TRY AGAIN |
| **goodbye** (1.5 s; skipped in rush) | `oona_delight` floats up and shrinks, and the lantern dims to a twinkle. | "Bring it back next Halloween!" | SEE YOU 2027 |
| **milestone** (every 100th visit) | `bg_starburst` with a big counter and confetti in the six crystal colours. A milestone flag is saved on that crystal. | "Another hundred crystals! Hooray, Lightkeepers!" | HOORAY! |
| **moonfull** (nice-to-have; once per night) | The Moon flares full. `smudge_dazzled` changes to `smudge_sheepish`, then the scene crossfades to `bg_window_lit` with `oona_proud`. | "The Moon is full! You did it!" → "Silly Smudge… you're my Official Trickster now!" | MOON SAVED! |
| **circle** (3 or more crystals in one group) | `bg_awaken`. Small crystals in the group's colours orbit the centre. `oona_giggle`, and `smudge_dazzled` peeks in. | "A crystal circle! The Moon loves friends!" | CRYSTAL CIRCLE! |
| **keeper** (Moon Guardian; about 1 visit in 3) | `oona_shh` with a slow push-in. This is the only cue where her face fills the screen, and only the oldest kids see it. | "Psst, Guardian… Smudge wants your autograph!" | SHH… SECRET |

### attract_call lines
1. "Crystal on the stone! Wake me up!"
2. "No crystal? Press the big button!"
3. "Smudge nibbled my Moon! Can you help?"
4. "Every crystal makes the Moon brighter!"
5. "Shh… I hear crystals coming…"
6. "Crystals on the stone! Fill the Moon!"
7. "Set your crystal here! I'm awake… almost!"
8. *Smudge:* "Nom nom! Marshmallow moonlight! Hee hee!"

### Greeting lines

| Cue | Line 1 | Line 2 |
|---|---|---|
| greet_r2 (Apprentice) | "You came back! Hello, Apprentice!" | "Your crystal remembers! Welcome, Apprentice!" |
| greet_r3 (Lamplighter) | "Lamplighter! Now you light the way!" | "Welcome back, Lamplighter! So bright!" |
| greet_r4 (Wizard) | "A real Wizard! Your starlight sparkles!" | "Welcome back, Wizard! Cast your light!" |
| greet_r5 (Starcatcher) | "Starcatcher! Even the stars wave hello!" | "Welcome back, Starcatcher! The stars remember!" |
| greet_r6 (Moon Rider) | "Moon Rider! Climb aboard my Moon!" | "My Moon Rider is back! Wheee!" |
| greet_r7 (Moon Guardian) | "Moon Guardian! The Moon bows to you!" | "Welcome home, Moon Guardian!" |

### chapter (about 20 s, played in the idle rotation)
Every part makes sense if a kid only catches that one.

| Part | Shot | Voice | Caption |
|---|---|---|---|
| a | `bg_window_lit` with `oona_base` raising her lantern | "Every Halloween, I light the Moon, so everyone finds their way home." | OONA'S MOON |
| b | `chapter_nibble` (or `smudge_munch` next to a crescent Moon), with munching sound | "But this year I overslept… and Smudge nibbled it like a cookie!" | UH-OH, SMUDGE! |
| c | `chapter_shardfall` | "Long ago, the Moon laughed so hard that sparkles fell as crystals." | MOONSHARDS |
| d | the crystal cycling through all six colours on the stone | "And now YOU carry them. Each crystal holds its very own light." | YOUR LIGHT |
| e | a beam rising into the Moon as the meter goes up | "Set your crystal on the stone, and its light flies up to the Moon!" | CRYSTAL HERE |
| f | `oona_peek` with Smudge sneaking past | "Fill the Moon tonight, Lightkeepers… and see what happens to Smudge!" | FILL THE MOON |

## 10. Counters, milestones and circles

- **Moon meter (tonight only).**
  - Every crystal visit and every button press adds 1. Replays don't count.
  - It fills over 8 phases, from a thin crescent to full.
  - When it reaches the goal, the `moonfull` cue plays once and the attract loop switches to the lit hillside.
  - After that, each reveal makes the full Moon pulse brighter.
- **Station counter (all time).** "✦ N CRYSTALS AWAKENED" sits under the Moon. It's what kids brag about.
- **Milestones.** Every 100th visit gets confetti and its own voice line, and that crystal remembers it was a milestone.
- **Circles.** The circle cue plays once per group, when three or more crystals arrive together.

## 11. Style guide

| Use | Hex |
|---|---|
| Night indigo / plum shadow / bottom band | #12102A / #2B1D4A / #0A0B1E |
| Moon silver | #E9EEFF |
| Oona's body / her mint rim glow | #F7FBFF / #CFFFF2 |
| Nightcap | #3B3A8C |
| Lantern iron / lantern edge highlight | #4A4E63 / #AEB7CC |
| Smudge's fluff / collar | #B9A8D6 / #FFF1D6 |
| **Reserved for bowls:** silver / red / gold | #D9DEE8 / #FF3B47 / #FFC83D |

**Look and mood:** round, soft shapes; one bright subject on a deep, dark field; a lullaby-spooky feel; soft storybook 3D. On-screen words are drawn by the device in its built-in bold font, white with a dark outline.

**Do**
- Keep the bottom 25% of every image plain near-black.
- Keep subjects in the top three-quarters of the frame.
- Use big, simple shapes.
- Rim-light dark characters so they stand out.
- Make every expression an edit of that character's base image.

**Don't**
- Use grain, fine texture, mist, bokeh, fur strands or tiny stars. They band and turn blocky on this screen.
- Put text in images.
- Use gold anywhere except the Wishing Star and the starburst background.
- Use red anywhere except the witch's hat.
- Draw teeth, claws or real-bug details.
- Show faces full-screen (keeper is the only exception).
- Flash more than 3 times a second.

**ChatGPT recipe**
- **Prompt prefix:** "Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; smooth clean shading; no grain, no fine texture, no text."
- **Backgrounds:** add "landscape, dark midnight-indigo night, bottom quarter plain near-black and empty, open centre." Crop the 1536×1024 output to 1536×922, then scale it to 800×480.
- **Sprites:** add "full figure, centred, isolated on flat pure black," then key out the black.
- **Expression edits:** "Edit this exact image: change ONLY the face; keep body, lantern position, size, colours and framing identical." Always edit from the base image, never from a previous edit, and keep all of one character's edits in a single chat.
- **Lantern position:** after each Oona edit, store the lantern's (x, y) position in the manifest.

## 12. Production plan (7 days)

### Images
- **Day 1: Oona.** Make `oona_base` and re-roll it until it's right, because every other Oona image is an edit of it. Then make her four most-used edits.
- **Day 2: Smudge and props.** Make Smudge's base and his edits, plus the props. For `prop_crystal`, give ChatGPT a photo of one of your real crystals.
- **Day 3: backgrounds and trick faces.** Make the three backgrounds, then `oona_giggle` and `oona_pumpkin`. The trick faces can wait until last, because tricks rarely play in 2026.
- **Days 4–5: nice-to-haves.** Work through the nice-to-have stills as budget allows. That's 30 images at most, leaving room for re-rolls.

### Voice: 73 lines, recorded in batches
1. **Needed on night one (40 lines):** `attract_call` 1–7, all of awaken, `greet_r2` line 1, charge, all reveals, the three bowl lines, replay, nocrystal, sleepy and goodbye.
2. **Night-one extras (21 lines):** chapter, rush, moonfull, milestone, circle, recall, Smudge's attract call, and `greet_r2` line 2.
3. **Before Halloween 2027 (12 lines):** `greet_r3` to `greet_r7` and keeper. These can't play in 2026 anyway.

Number variants with no gaps (`_1`, `_2`, `_3`…), because the firmware stops counting at the first missing number. Give nice-to-have lines the highest numbers.

### Firmware features this story uses (built in)
1. **Awaken colour line:** the screen plays `awaken_b_<colourId>` (the crystal's bench colour, 1–6). Crystals without a bench colour skip that line.
2. **Tricks:** the GLOW number is hidden on SMUDGED! results.
3. **Moon meter:** fill = tonight's Moon points ÷ `moon_goal` (config.ini, default 50; crystal visits and button presses each add 1). The host menu has **Fill the Moon**. When it fills, `moonfull` plays once and the attract loop switches to `attract_full`. The finale is triggered by the meter or by the host, not by the clock.
4. **Rush lines:** in rush mode the screen plays `rush_t<tier>`; if that line doesn't exist it plays the bowl line.
5. **Lantern glow:** each clip can carry a lantern anchor (from the asset manifest); the screen draws a glow there in the crystal colour that pulses with Oona's voice.
6. **keeper:** plays for Moon Guardians about 1 visit in 3.

### Old Skull Oracle tokens
Test one on the bench first.
- **If it carries the SK marker,** the Window greets the kid as an **Apprentice** and recall plays their 2025 result.
- **If it doesn't,** the token reads as foreign and the kid gets a star-wisp. The host then offers them a crystal.

## 13. Parent sign

> **MOONSHARDS — Oona's Halloween Window**
> Every child gets candy, every time.
> Your crystal is yours to keep. Bring it back every Halloween!
> **The crystal remembers only numbers:** how many Halloweens it has visited and each year's glow number. It never stores names, photos or personal information. (Its tag also holds a plain web link about this project.)
> No crystal? Press the big button. Everyone helps fill the Moon.
> One crystal on the stone at a time, please. Spooky-cute, no jump scares, and gentle lights (fewer than 3 flashes per second).

## 14. Handing out a crystal (host ritual)

Kneel and hold the crystal out in cupped hands, then say: **"This crystal fell from the Moon, and it chose you. Wake it up on the stone!"**

When a kid gets the Wishing Star, raise the gold star bowl and say: **"The Wishing Star found you. Make a wish, and pick one!"**

## 15. captions.ini (ready to paste)

```ini
title = Moonshards
character = Oona
[rank]
1 = Spark
2 = Apprentice
3 = Lamplighter
4 = Wizard
5 = Starcatcher
6 = Moon Rider
7 = Moon Guardian
[tier]
0 = SMUDGED!
0_color = #B9A8D6
1 = SILVER MOON
1_color = #D9DEE8
2 = WITCH'S HAT!
2_color = #FF3B47
3 = WISHING STAR!
3_color = #FFC83D
[bowl]
common = Silver Circle
common_color = #D9DEE8
common_shape = circle
rare = Red Triangle
rare_color = #FF3B47
rare_shape = triangle
legend = Gold Star
legend_color = #FFC83D
legend_shape = star
[color]
1 = Twilight
1_hex = #A35CFF
2 = Ember
2_hex = #FF7A1A
3 = Frost
3_hex = #5CD6FF
4 = Potion
4_hex = #3DFF7A
5 = Pixie
5_hex = #FF6FC8
6 = Ghostlight
6_hex = #EAFBFF
[caption]
attract = Crystal here
awaken = NEW CRYSTAL!
greet = Welcome back!
charge = PRESS!
reveal_t0 = SMUDGED!
reveal_t1 = SILVER MOON
reveal_t2 = WITCH'S HAT!
reveal_t3 = WISHING STAR!
replay = ALREADY GLOWING
nocrystal = STAR-WISP!
foreign = STAR-WISP!
sleepy = TRY AGAIN
goodbye = SEE YOU 2027
milestone = HOORAY!
circle = CRYSTAL CIRCLE!
keeper = SHH... SECRET
[ui]
place = Crystal on the stone
hold = Hold still...
power = GLOW
counter = crystals awakened
press = Press the button!
```

## 16. The runner-up concepts (swap-in options)

If you'd rather go a different direction, these were the other three pitches. Each would reuse the same firmware, plinth and crystals; only the art, the lines and the words in `captions.ini` change.

### Madame Mira's Magic Mirror (mystic: second place on systems)
- **The idea:** a giggly ghost-grandma fortune-teller lives inside a magic mirror. Kids' crystal light wipes away the fog that Smudge, a cheeky fog-puff, keeps blowing on her glass. Each crystal reads a "fortune".
- **Swap to it if** you want a cozy parlour mood and one setting that never changes (the mirror). That makes it the cheapest base for adding a new chapter each year.
- **Already borrowed into Moonshards:** the Official Trickster finale, hiding the number on tricks, and never showing her face full-screen.
- **Fix before using:**
  - Drop the headscarf, coins and hoop earrings, because the fortune-teller look risks a stereotype.
  - Bake the fog as 4-5 fixed levels, because fog bands badly on MJPEG.
  - Recolour the bowls, because the green and orange crystals clash with them.
  - Simplify the rank names (Rune-Reader and Moon-Seer are hard for small kids to say).
- **Cost:** about 24 stills, about 50 lines, one voice.

### Dragonglow: The Sleepy Candy Dragon (quest: the kids' judge's pick)
- **The idea:** Butterscotch is a chubby candy-gold dragon who floats like a balloon and breathes sparkles. He guards a candy hoard. A teacup-sized goblin has snuffed out the Hoard Lantern, and the kids' crystals relight it.
- **Swap to it if** you want the strongest pull for 9-12 year olds: titles like Dragon Rider and Keeper of the Hoard, and a 2027 hook where an egg found in a sock hatches.
- **Already borrowed into Moonshards:** the aspirational rank ladder and printing the on-screen tier art as bowl signs.
- **Fix before using:**
  - The goblin must never claim the kids' candy.
  - Drop the gag that flips the screen 180 degrees.
  - The amber and violet crystals clash with the orange and purple bowls.
  - A gold dragon dilutes the legendary gold star.
- **Main risk:** it is the hardest to keep consistent in ChatGPT, because horns, wings and a hat drift across 10 edits. If it drifts, crop to head and shoulders.
- **Cost:** about 27 stills, about 67 lines, plus an optional second voice.

### Lumpkin's Hungry Cauldron (silly: the production judge's pick)
- **The idea:** a sassy floating jack-o'-lantern and Burp, his bottomless cauldron, relight Pumpkin Street after a grumpy cloud blows it out. Every crystal makes Burp burp up a fortune, all the way up to the Golden Burp.
- **Swap to it if** ChatGPT consistency becomes the bottleneck. A rigid pumpkin needs only face edits, so it is the easiest character to keep looking the same, and glowing carvings read best on a 7-inch screen. It also gets the most laughs from ages 3-7.
- **Already borrowed into Moonshards:** the character's glow in the kid's colour pulsing with the voice, the handling of old Skull tokens, and the meter formula min(8, visits+1).
- **Fix before using:**
  - Cut the toot gag.
  - Never show Burp slurping the crystal's light, because little kids will think it got eaten.
  - Rename the clashing pairs: the Slime Lime crystal vs the Slime Pool bowl, and the Witch Plum crystal vs the Witch Hat bowl.
  - Add a rim light to the dark cauldron and the dark cloud so they show up on screen.
- **Cost:** about 25 stills, about 64 lines, one voice plus a curated set of cartoony burp sound effects.
