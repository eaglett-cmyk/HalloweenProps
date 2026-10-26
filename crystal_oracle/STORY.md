<!-- Story bible for the Crystal Oracle station. Written 2026-10-22 when the owner chose DRAGONGLOW (the kids' judge's pick, 8.5/10) over the panel winner Moonshards (archived: docs/stories/MOONSHARDS.md), with every judge fix applied. Revised 2026-10-23 (owner): the kids are travelers from other worlds, not wizards, and the crystals are pieces of Butterscotch's crystal egg instead of the Moonstone (§1). Revised 2026-10-26 (owner): the DRAGON GEM is now the top (legendary, ~4%, host-held red triangle) prize and the GOLDEN STAR the rare one (~18%, gold star bowl); the tables and lines below that still name them the other way round are superseded by the manifest. The asset manifest is generated from tools/gen_manifest_dragonglow.py. -->

> **2026-10-26 prize swap:** the **Dragon Gem is the top prize** (legendary, the dragon gem bowl the host holds) and the **Golden Star is the rare prize** (gold star bowl). Where older text below says otherwise, this note and `tools/gen_manifest_dragonglow.py` win.

# DRAGONGLOW — The Sleepy Candy Dragon

*Station: the Crystal Oracle · Chapter 1, Halloween 2026: "The Night the Beacon Went Out"*

> **Logline.** Once a year, on Halloween, the doors between worlds open, and travelers of every kind (princesses, robots, dinosaurs, ghosts) find their way to Butterscotch's cave by the light of the Hoard Beacon. Butterscotch, the sleepy candy dragon, wakes up to share his hoard with every one of them. This year Snuffwick, a shy little goblin who loves the dark, tiptoed in and snuffed the beacon out. The only thing that can light it again is the glow the travelers carry in their crystals: pieces of the egg Butterscotch hatched from.

**What every kid gets:** your crystal wakes a dragon, you are the light-bringer, and you always get candy.

| At a glance | |
|---|---|
| The screen | **the Dragon's Window**. Dress the box as a round stone cave mouth with a little garland of paper sweets. |
| The plinth | **the Wakestone**. On the porch it is the owner's crystal-crowned stone altar (`plinth/crystal_plinth_v7.scad`), with the crystal reader hidden under a flush lid in its top. In the art (`bg_wakestone`) it is a round stone pedestal with a carved sleeping-dragon swirl. This year it has **no light ring and no big button**: the crystal and the screen carry the magic. |
| No crystal? | The kid **taps the Dragon's Window** (the screen) and gets a dragon blessing. |
| Main voice | **Butterscotch**, the sleepy candy dragon. He is the only TTS voice. |
| Mischief-maker | **Snuffwick**, a teacup-sized goblin. He is almost entirely sound effects (2 optional pitched-up lines). |
| What the kids are | **travelers** from other worlds. Whatever the costume, to Butterscotch it is real: that's the world they came from |
| The crystals | pieces of the **crystal egg** Butterscotch hatched from, scattered through all the worlds; always just "crystal" out loud |
| Tonight's team goal | Light **the Hoard Beacon**. Every visitor adds a spark, with or without a crystal. |
| Results | GOBLIN TRICK! · HOARD COIN! · GOLDEN STAR! · DRAGON GEM! (top prize) |
| Bowls | silver coin · silver coin · gold star · dragon gem (the host holds it). Bowls are named after their prize, never their shape. |

## 1. What changed from the pitch

**Revision, 2026-10-23 (owner).** Some ideas carried over from the first story didn't fit:
- **Travelers, not wizards.** The kids come dressed as anything, so Butterscotch calls them **travelers**: Halloween is the one night the doors between worlds open, and every costume is the world its kid came from. Every "wizard" line was re-recorded, and rank 5 is now **Worldwalker** (it still earns the little hat).
- **The crystal egg replaces the Moonstone.** The crystals are pieces of the egg Butterscotch hatched from long ago. The pieces scattered through all the worlds, and each one finds its way to one traveler. That's why a crystal wakes him, and it sets up 2027's egg in the sock. The Candlewizards are gone; the hoard was built by travelers over the ages, and Butterscotch's purple hat was a gift from one wizard traveler long ago.
- **The Hoard Lantern is now the Hoard Beacon.** Its light is how travelers find the cave from their own worlds, so Snuffwick snuffing it is real (but gentle) trouble. It still looks like an old iron lantern (the art is unchanged); everything spoken or written on screen says "beacon" (words `meter.name = Beacon`, so the host menu reads *Fill the Beacon*).
- **Why a crystal wakes him.** A piece of his eggshell glows when it comes near him, and glow wakes dragons (chapter part d).

**The original judge fixes:**

Dragonglow was the kids' judge's pick (8.5/10): it has the broadest pull from age 3 to 12, a rank ladder that older kids will actually brag about, and a reason to come back next year (the egg in the sock). It scored lower on production (5/10) and systems (6/10). The owner chose it; this version applies every fix the judges asked for:

- **Snuffwick never wants the candy.** He snuffs out lights because he likes the dark: he's shy, and dark corners are cozy for napping. "All this candy is MINE!" and the candy-in-the-sock framing are gone. Glow makes him sneeze, giggle and scamper off. At the finale Butterscotch invites him to the feast and makes him the **Official Prankster** (taken from *Madame Mira*), so his tricks still make sense after the beacon is lit and in every later year.
- **One voice.** Butterscotch is the only TTS voice. Snuffwick is giggles, squeaks and "pfft"s, plus at most 2 short lines recorded in Butterscotch's voice and pitched up by the builder.
- **No crystal shares a colour with a bowl.** The bowls are silver, red and gold. The crystals are 3D-printed in three filaments (since 2026-10-22): deep-to-light purple **Dragonberry**, sky-blue **Starfrost** and rainbow **Rainbow Fizz** (§5). None of them is silver, red or gold; Rainbow Fizz is every colour at once, which reads as its own thing. The pitch's amber "Sunhoney" is gone.
- **Each result is named after its bowl picture** (from *Moonshards*). HOARD COIN! is the silver coin on the silver coin bowl, GOLDEN STAR! is the gold star on the gold star bowl, and DRAGON GEM! is the ruby gem the host holds. The coin, star and gem are printed as the bowl signs. **Bowl lines name the prize, never a shape** ("Silver coin! Take one treat!").
- **Butterscotch is caramel, not gold.** Gold now belongs only to the Golden Star, so the legendary prize stays special.
- **The upside-down gag is gone.** It is replaced by the **Snuff-Out** gag, which uses the story's own darkness and needs no screen rotation.
- **The finale always happens.** The pitch tied it to 100 crystals with no fallback. Now the host sets the goal (`moon_goal`, default 50 sparks), the blessings of kids without a crystal count too, and the host menu can fill the beacon.
- **A real rush mode for all-new 2026 groups.** A first visit in rush is absorb, "Sparkfinder!" and one combined line, about 3.5–4 s per kid.
- **Toddler-safe.** No fire, no teeth, no claws, never a roar. "Who's brave enough to wake a dragon?" is cut. Every trick's joke is on Butterscotch, never the kid, and the GLOW number is hidden on tricks.
- **Built for ChatGPT and the real display.** Butterscotch's design is simplified (two hat stars, no scales, no stripes). All six of his MVP expressions are face-only edits of one master, with only 2 pose edits in total (wink, wave). The MVP set is 16 stills (29 in all), and the chapter reuses the cave and the sprites (1 dedicated chapter still).

## 2. How it plays

### Walking up (attract)
- **What kids see:** the Hoard Cave. Butterscotch dozes in the middle, floating like a balloon, and every 12 s peeks one eye open. The **Hoard Beacon** hangs in the upper-left. It is painted unlit, and the screen draws its warm glow, which grows with every spark tonight. Every loop, Snuffwick tiptoes in from the right, giggles "pfft!" and scampers off.
- **Counter:** under the beacon, "✦ 147 sparks for the beacon" (the all-time station count).
- **Calls:** about every 30 s Butterscotch mumbles or calls out ("Crystal on the Wakestone! Wake me up!").
- **No crystal?** The caption under the cave alternates CRYSTAL HERE and OR TAP ME, and one of the calls is "No crystal? Tap my window!"
- **Wakestone:** it has no lights of its own this year. The glow lives on the screen: the beacon, Butterscotch's tummy glow and, once a kid arrives, the beam in their crystal's colour.

**The story is told here, in the attract loop (the chapter plays every few loops). A kid standing at the stone never has to sit through it.**

### First visit (2026: almost every kid)
The host hands over a crystal with the ritual line (§14), and the kid sets it on the Wakestone.

1. **absorb (0.0 s).** A chime plays and a beam of the crystal's colour rises into Butterscotch's chest. One eye pops open, and the colour name appears ("DRAGONBERRY").
2. **awaken (1.0 s).** The scene cuts to the Wakestone. The crystal rises out of the stone, glowing in its colour, and Butterscotch floats down, surprised, then beams. He says:
   - "Ooh! A brand-new crystal is waking!"
   - "Dragonberry! Purple and sweet, full of dreams!"
   - "Sparkfinder! Welcome, little traveler!"

   Two of the eight gems light up, with a chime for each.
3. **reveal (~7.5 s).** HOARD COIN!, DRAGON GEM! or GOLDEN STAR! appears, and the GLOW number counts up. The bowl line plays over the end of the reveal.
4. **goodbye (~10 s).** Butterscotch waves and floats up like a balloon while the kid walks to the bowl.

The result is written to the crystal before awaken starts, so lifting the crystal early loses nothing. The default setting `first_no_trick` stays on, so a crystal's first visit is never GOBLIN TRICK!.

### Returning crystal (2027 onward)
The sequence is absorb (1 s), then **greet** (2 s), then the **reveal** with the bowl line over its end, then goodbye. That's about 5.5 s.
- **greet** shows the rank name, with last year's gems already lit and the new gem popping on. A kid reaching **Worldwalker** (visit 5) sees a little traveler's hat pop out of Butterscotch's paws: "Here's your hat!"
- **recall** ("Last year, a Dragon Gem! Remember?") and the **charge** window (2.5 s) play only when nobody is waiting. In the charge window the kid **taps the window** as fast as they can ("Tap the window! Pump up the glow!") to fill a glow bar, which nudges their GLOW number up a little.

### No crystal (tap the window)
There is no big button this year. A kid without a crystal **taps the Dragon's Window** (the screen). Butterscotch puffs his cheeks and blows a sparkly **dragon blessing** bubble down to the kid. The beacon gains a spark, because this kid helped too. Then he says "Silver coin! Take one treat!" The whole thing takes about 4 s.

### Crystal already used tonight
**replay** (2 s). Butterscotch winks with a paw to his lips: "Shh! Your crystal already glowed tonight!" Tonight's result shows again, dimmed, with a ✓. There is no bowl line unless the `second_helpings` setting is on.

### Bad read, or a tag that isn't ours
- If the tag can't be read, **sleepy** plays: a big yawn and "Again, please! Sleepy crystal." It is never shown as an error.
- If the tag isn't ours (a bus pass, or an old token without our marker), the kid gets the **dragon blessing** path. The tag is never written to.

### Rush mode (turns on automatically when a crystal arrives within 8 s of the last one)
- **First visit:** absorb (0.6 s, the colour shown as a caption only), then awaken_c only ("Sparkfinder!"), then a 1.5 s reveal with one **combined line** that leads with the bowl: "Hoard coin! Silver coin bowl!" Total is about 3.5–4 s.
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

### Later: a big button or a light ring
Both are optional extras for a later year; the plinth firmware already supports them (`firmware/crystal_plinth/README.md`).
- **A big button.** Wire it, then set `[station] button = yes` in `config.ini`. The button lines then play again: "No crystal? Press the big button!" (`attract_call_2`) instead of "No crystal? Tap my window!", and "Press the button! Pump up the glow!" · "Press fast! Fill your crystal!" (`charge`) instead of the tap lines. The captions go back to OR PRESS and PRESS!, and a press with no crystal on the stone gives the dragon blessing (with the `button` sound). Record those three lines (they are NICE in the manifest) and change the parent sign's no-crystal line before switching it on.
- **A light ring** under the crystal (the Skylanders glow): it would breathe through the crystal colours in the attract loop, snap to the crystal's colour within 0.1 s of placement, wobble sage green on a GOBLIN TRICK!, chase red and the crystal colour on a Dragon Gem, swirl gold on the Golden Star, glow a random crystal colour for a blessing and breathe a dim white for a sleepy crystal. Dragonberry would crossfade deep and light purple (`LED_DUAL`) and Rainbow Fizz would drift through the rainbow.

## 3. Characters

### Butterscotch, the sleepy candy dragon (main voice)

**How he looks (for the illustrator)**
- An old, chubby, round dragon shaped like a soft gumdrop or a pear. He **floats like a balloon**: two short stubby feet are tucked under him, never standing.
- Glossy, candy-smooth skin with no scales and no stripes, in warm **caramel / butterscotch toffee** (#C77A35, deeper toffee #9A5424 in the shadows), with a smooth **cream belly** (#FBE3BF). Caramel, **never gold or yellow**.
- Two small rounded horns shaped like caramel swirls, and two tiny stubby wings that are much too small to fly.
- Big warm amber-brown eyes, small rounded paws, and a short curly tail ending in a little round tuft.
- A crooked, faded purple **wizard hat** (#6E5A9E) with exactly **two** big simple pale-cream stars (#F3E6C4). It was a gift from a wizard traveler, long ago (one costume among many).
- **No fire, ever:** he breathes sparkles in the colour of the crystal he's looking at. No teeth, no claws, nothing sharp, and he never roars.
- **Keep him simple.** Every small detail drifts across ChatGPT edits.

**Personality**
- A cozy grandpa who yawns in the middle of sentences, chuckles a lot and is easily amazed.
- Fiercely proud of "my travelers", and delighted by every world they come from.
- Never scolds anyone and never sounds scary. When Snuffwick pranks him, he laughs hardest.
- Two secrets for the Keepers: his real name is Bartholomew, and his wings are just for show (he floats).

**TTS voice direction**
- **Prompt to paste:** "Warm, slow, cozy old dragon grandpa telling a bedtime story: soft and round, a gentle low purr-rumble, smiling, a little whispery, a yawn hiding in some lines, delighted chuckles, bright on exclamations. Never booming, never growly, never a roar. Quick delivery: each line fits in about 2-2.5 seconds."
- **ElevenLabs:** use a gentle, **older male storyteller** voice: warm, soft, slightly husky, a kindly grandpa rather than a deep booming one. Settings: Stability ~40%, Similarity ~75%, Style ~30%, Speed 1.05–1.1.
- **OpenAI gpt-4o-mini-tts:** use `ballad` or `cedar` (or `ash`) with the prompt above. Avoid `onyx`, which is too deep. The model retires on 2027-01-06, so record before then.
- **Recording:** make two takes of each line and keep the warmer one. Trim the silence to 40 ms or less at each end.
- **File format:** any WAV/MP3/M4A is fine. Name each file after its line id from `CHATGPT-ASSETS.md` (for example `attract_call_1.mp3`), drop it in `assets/source/voice/`, and the asset builder converts it to the 48 kHz mono WAV the screen plays.

**How he moves**
- His sprite bobs like a balloon (±7–8 px; a 4 s sine wave in the loops, 3 s elsewhere). This motion is baked into the clips.
- His expressions crossfade over 0.1–0.25 s, and every expression is an edit of `butterscotch_base`.
- The device draws his **tummy glow** at a chest/belly **anchor**, `[0.5, 0.62]` of each Butterscotch still. It is tinted to the crystal colour and pulses with `voiceLevel()`, so he seems to talk without mouth frames. The absorb beam lands there too.

### Snuffwick, the teacup goblin (pest, almost all sound effects)

**How he looks**
- He looks like a **plush toy**, never a monster: a teacup-sized goblin with a round, soft sage-green (#8DBF6A) face and body.
- Huge floppy ears, big shiny dark eyes with star highlights, a button nose and a wide **toothless** grin.
- Mismatched striped socks (one plum-and-cream, one teal-and-cream) and a little cap shaped like an **unlit** cream candle stub.
- He carries a long **pewter-grey** candle-snuffer over his shoulder like an umbrella. It is never brass, because gold is reserved.
- A soft pale rim light so he stands out against the dark cave.

**Personality:** a shy, giggly little pest. He likes the dark because it's cozy for naps, so he snuffs out every light he finds. He **never wants or takes the candy.** Glow makes him sneeze, giggle and scamper off. After the finale he's the Official Prankster and plays one silly trick now and then.

**Sound:** mostly cartoon sound effects: a tiny giggle, a rubber-chicken squeak, a soft "pfft" and a sparkly sneeze. He has two optional lines (`attract_call_8`, `circle_2`), recorded with Butterscotch's voice; the builder pitches them up 5 semitones at 1.15× (voice fx `"smudge"`, which kept its Moonshards name).

**How he moves:** he is small (about 115 px tall) and stays on the cave floor with a tiptoe bounce (4 px every 0.4 s). He always enters from the **right**, because the upper-left belongs to the beacon.

### The travelers (the kids)
On Halloween the doors between worlds open and travelers come through: a princess from a castle world, a robot from a star world, a dinosaur from a jungle world. Butterscotch never questions a costume; it's simply where that traveler is from. Each crystal is a piece of his eggshell that found its way to one of them.

## 4. Lore

**The doors between worlds.** Butterscotch's cave sits at a crossroads where many worlds touch. On one night a year, Halloween, the doors open and travelers come through, following the light of the Hoard Beacon.

**The hoard.** Over the ages, travelers from every world left a sweet to share, until the cave was full of candy. Butterscotch guards it and sleeps on it 364 nights a year, waking on Halloween to share it.

**The crystals.** Long ago Butterscotch hatched from a **crystal egg**. When it cracked, the pieces scattered through all the worlds, each one a different colour. A piece finds its way to one traveler, and when it comes near Butterscotch it remembers him and glows, and that glow wakes him. Three kinds have turned up so far: Dragonberry, Starfrost and Rainbow Fizz. More can turn up in later years (a new filament = a new colour, §5).
- A crystal wakes for exactly one child.
- It remembers only numbers: how many Halloweens it has visited, and how brightly it glowed each time. It never remembers names. This is the same promise the parent sign makes.

**The Hoard Beacon.** It is the beacon travelers follow from their worlds to the cave, and it lights the Halloween feast. It runs on **glow**, and glow comes from the crystals travelers carry all year, and from a traveler's good wish (a tap on the Dragon's Window). Every visitor adds **one spark**.

**Snuffwick** loves the dark and snuffs out lights to make cozy nap spots. This year, pfft, he snuffed the Hoard Beacon, so travelers might not find their way. He isn't after the candy; he just wanted it dark.

**How the kids win, together**
- Every spark makes the beacon glow a little warmer, all night.
- When the beacon is full it blazes. Snuffwick is dazzled: he sneezes, giggles, and shyly asks if he can stay.
- Butterscotch invites him to the feast and makes him **Official Prankster**. He may play one silly trick now and then, but he never takes anything, and every kid always gets candy.

This deal holds in every future year.

## 5. Crystal colours

The crystals are **3D-printed in PLA**, one filament per colour; there are **three colours** right now. The ID is the colour code written to the tag when the crystal is prepared (page 17, byte 1: the host menu's **Crystals** page, or the `nfc_tag_bench` tool); the screen and the bench use the same IDs. Silver, red and gold belong to the bowls and are **never** used as crystal colours.

| ID | Filament | Name | Screen colour (`color.N_hex`) | Full of… | Awaken line |
|---|---|---|---|---|---|
| 1 | VOXELAB PLA Crystal Rainbow, **Deep Sea Realm**: prints deep purple to light purple (a gradient filament, so every crystal comes out a little different) | **Dragonberry** | `#5A1E9E,#C8A0FF`: purple and blue, cycling slowly | dreams | "Dragonberry! Purple and sweet, full of dreams!" |
| 2 | PLA HS Glitter, **Clear Sky Blue** | **Starfrost** | `#5CD6FF`: sky blue | wonder | "Starfrost! Sparkly sky blue, full of wonder!" |
| 3 | FLASHFORGE Translucent Glitter PLA, Crystal **Rainbow Candy** (a multi-colour gradient) | **Rainbow Fizz** | `rainbow`: the full rainbow, cycling | giggles | "Rainbow Fizz! Every colour, full of giggles!" |

**How they look.**
- **Dragonberry:** the screen's glows (the absorb beam, Butterscotch's tummy glow, the sparkles) blend slowly back and forth between deep and light purple, about 4 s for a full cycle. In the awaken clip (`awaken_1`) the crystal is tinted as a purple-to-blue gradient, top to bottom, like the colour bands of the print.
- **Starfrost:** a steady sky blue.
- **Rainbow Fizz:** the glows run through the whole rainbow, about 4 s per turn. In `awaken_3` the crystal is a candy rainbow gradient (pink, orange, yellow, green, sky, violet).

Butterscotch's sparkle-breath and tummy glow always take the kid's colour.

**IDs 4–6 are free** for future filaments. To add one, give it a name, a colour (`"#RRGGBB"`, 2–4 stops such as `"#RRGGBB,#RRGGBB"`, or `rainbow`) and an `awaken_b_<id>` line in `tools/gen_manifest_dragonglow.py` (`CRYSTAL_COLORS` and `VOICE`), re-run it, then `build_assets.py --gen-defaults`, reflash the screen, and add the colour to the `nfc_tag_bench` tool if you use it. The firmware uses the colours 1…N up to the first empty `color.N_hex`. A crystal whose tag has no colour (or an unused ID) still glows in one of the defined colours, picked from its tag's serial number, but it isn't named (no `awaken_b` line, and no colour name under the absorb beam).

## 6. Ranks

Ranks count **total** visits, not streaks, so missing a year never lowers a kid's rank.

The meter is 8 gems drawn by the device (think of them as dragon scales). It lights `min(8, visits + 1)` gems:
- A first visit already shows 2 of 8: one for finding you, one for tonight.
- Visit 7 fills all 8 gems, and visit 8 crowns the kid as Keeper of the Hoard.

| Visits | Rank | Meaning | Aura (screen) |
|---|---|---|---|
| 1 | **Sparkfinder** | You woke your crystal; the dragon knows your glow | one soft ring of light, breathing slowly |
| 2 | **Apprentice** | Butterscotch starts teaching you glow magic | ring + one orbiting spark |
| 3 | **Glowcaster** | You can cast light on your own | ring + a candle-flicker on the crystal's tip |
| 4 | **Hoardguard** | Trusted to guard the candy hoard | double ring + three orbiting sparks |
| 5 | **Worldwalker** | You've crossed between worlds five times: you earn the traveler's hat | the little hat pops out (`greet_r5` clip) |
| 6–7 | **Dragon Rider** | Butterscotch lets you ride | a caramel swirl sweeping around the crystal |
| 8+ | **Keeper of the Hoard** | Highest honour: you know the secret | full warm halo + a crown of sparks; the keeper secret about 1 visit in 3 |

*The auras are the look to aim for. Today's firmware shows the rank name, the 8-gem meter and, at rank 5, the hat clip; drawing a per-rank aura is a firmware nice-to-have.*

## 7. Tiers, bowls and tricks

Each result has the same name as the picture on its bowl, so a three-year-old hears it, sees it on screen, and then sees it again on the bowl. The GLOW number ranges below are the firmware's tier bands, which stay compatible with the skull.

| Tier | Odds | GLOW | Reveal | Bowl | Sign art |
|---|---|---|---|---|---|
| 0 Trick | ~15% | 1–39 (hidden) | **GOBLIN TRICK!** (a silly gag, then candy) | **silver coin** | silver hoard coin |
| 1 Treat | ~63% | 40–89 | **HOARD COIN!** | **silver coin** | silver hoard coin (`prop_coin`) |
| 2 Rare | ~18% | 90–99 | **GOLDEN STAR!** | **gold star** | gold star (`prop_star`) |
| 3 Legendary | ~4% | 100 | **DRAGON GEM!** | **dragon gem**, held by the host | ruby triangle gem (`prop_gem`) |

### Setting up the bowls
- **Silver coin:** a big round stainless or foil bowl, with the silver coin sign.
- **Gold star:** a gold or star-shaped bowl, with the gold star sign (the rare prize).
- **Dragon gem:** stays with the host (the top prize). A red basket works well, with the ruby gem sign, and the host can wear a wizard hat.
- **Signs:** print each sign from its prop still, and light it with a battery LED puck so the colour reads on a dark porch.
- **Layout:** put the bowls beside the Wakestone so the line flows from stone to bowl to sidewalk.

### Rules for GOBLIN TRICK!
- Every gag ends with candy ("Candy anyway!"), and the bowl line follows as usual.
- The joke is always on Butterscotch or Snuffwick, never on the kid. Snuffwick never touches the candy.
- The GLOW number stays hidden behind a puff.
- The same gag never plays twice in a row, and a crystal's first visit is never a trick.

### The three trick gags
The firmware picks gag *n* and plays clip `reveal_t0_n`, line `reveal_t0_n` and sting `reveal_t0_n` together. The sting starts 0.65 s into the gag.
1. **Rubber Chicken.** Snuffwick zips in hugging a squeaky rubber chicken. Butterscotch's "huh?" turns into a silly tongue-out face, and the chicken squeaks twice. At 1.1 s, *poof*: the chicken is gone and a silver hoard coin is spinning in its place. Snuffwick giggles off. *"A rubber chicken?! Poof! Candy anyway!"*
2. **Hiccup Bubbles.** Snuffwick pops up behind Butterscotch's shoulder and tickles his nose with a feather. *Hic!* (0.55 s) *Hic!* (1.35 s): he jolts with each hiccup, bubbles float up, and the screen adds sparkles in the kid's crystal colour. *"Hic! Snuffwick tickled me! Hic! Candy anyway!"*
3. **Snuff-Out.** Snuffwick tiptoes in, and at 0.65 s, *pfft!*, the whole cave goes dark for a beat. Two goblin eyes blink in the dark. The beacon's sparks and Butterscotch's tummy glow stay on, because **the kids' light can't be snuffed**. At 1.4 s Butterscotch sneezes sparkles, the lights pop back on, and he giggles; Snuffwick sneezes too and scampers off. *"Lights out?! Ah... ah... CHOO! Candy anyway!"*

## 8. Story

### Chapter 1 (2026): "The Night the Beacon Went Out"
1. Every Halloween the doors between worlds open, and Butterscotch wakes beneath the Hoard Beacon to share the candy hoard with the travelers who follow its light.
2. This year Snuffwick, who loves the dark, tiptoed up the candy pile and snuffed the beacon out. *Pfft!*
3. In the dark, Butterscotch keeps nodding off ("Zzz... a goblin snuffed my beacon... zzz").
4. Long ago, Butterscotch hatched from a crystal egg. Its pieces scattered through the worlds, and the travelers carry them now. Each crystal glows when it comes near him, and glow wakes dragons.
5. All night, each crystal on the Wakestone, and each dragon blessing for a kid who taps the window, adds a spark, and the beacon grows warmer (the live meter). Snuffwick keeps playing silly tricks (GOBLIN TRICK!), but all that glow makes him sneeze.
6. **Finale (the goal is reached).** The beacon blazes. Snuffwick, dazzled, sneezes, giggles and shyly asks to stay. Butterscotch says, "Silly Snuffwick... come to the feast! You're my Official Prankster!" The travelers relit the beacon, and the Halloween feast is saved.
7. **Stinger for 2027:** Snuffwick scampers off hugging a lumpy sock with something golden glowing inside. (It is also teased in chapter part f and in the attract loop after the finale.)

**Every visit works on its own.** A kid who arrives at 8:30 pm still wakes the dragon and still adds a spark.

**The finale always happens.** The goal is set by the host (`[story] moon_goal`, default 50 sparks; the key kept its Moonshards name). If the night is slow, the host taps **Fill the Beacon** in the hidden host menu (the label comes from the words key `meter.name`) and the finale plays when the menu closes.

### Hooks for later years
The cave, the Wakestone, Butterscotch, Snuffwick and the beacon all stay the same. Each new year needs about 5 new stills and 8 new lines.
- **2027, "The Egg in the Sock".** The sock held a **dragon egg**. It hatches into baby **Toffee**, who needs crystal glow to learn to fly, and the meter becomes Toffee's flying lessons. Snuffwick, very proud, is the egg-sitter.
- **2028, "The Door That Wouldn't Open".** One world's door gets stuck, and the crystals learn a new spell to open it. The 2026 Sparkfinders are Glowcasters by now, and the Worldwalkers' hat (rank 5) starts to matter.
- **2029, "The All-Night Feast".** Butterscotch tries to stay awake all night for the very first time, and Snuffwick, who used to want the dark, joins the feast as a full member of the hoard. This sets up a new mischief-maker for 2030.

## 9. Cue sheet

**Applies to every cue unless the row says otherwise**
- **Bottom band (y ≥ 360):** the device draws the captions, the GLOW number (right), the 8-gem meter (centre) and the bowl icons (left). Each bowl icon is a coloured shape with a white outline that pulses at 1 Hz.
- **The Hoard Beacon:** painted, unlit, into `bg_cave` at about (150, 110). On the cave scenes the device draws its glow there (`meter.pos` = `150,110,70`, warm amber #FFB347), growing with tonight's sparks, with the spark counter under it. No sprite enters the upper-left of a cave scene.
- **Butterscotch at home:** centred at x 420, top y 40, height 300. His tummy glow (the anchor) sits at about (420, 226).
- **Pulses and flashes:** pulses run at 2 Hz or slower, with no more than 3 bursts per second.
- **No light ring this year:** the crystal's colour lives on the screen (the beam, the tummy glow, the sparkles). What an optional ring would do is in §2, *Later: a big button or a light ring*.
- **No big button this year** (`[station] button = no`): the tap lines and words below replace the button ones (words `nobutton.*`, §12).

| Cue | Visual | Voice (Butterscotch unless noted) | Caption |
|---|---|---|---|
| **attract** (12 s loop) | `bg_cave`. `butterscotch_sleep` floats with a slow balloon bob and switches to `butterscotch_peek` from 8.5 to 11.75 s. From 4 to 7.6 s `snuffwick_base` tiptoes in from the right, wobbles and scampers off; sfx `munch` is his giggle + "pfft" at 5.4 s. The device draws the beacon glow, the counter and the place-crystal hints on top. | `attract_call` every ~30 s (see the list below this table) | CRYSTAL HERE / OR TAP ME |
| **attract_full** (after the finale) | `bg_cave_lit` (or `bg_cave`). Sleep, then `butterscotch_delight`. Snuffwick, now the Official Prankster, scampers by hugging `snuffwick_sock`. Music switches to `idle_full`. | `attract_call` | CRYSTAL HERE / OR TAP ME |
| **absorb** (1 s) | `butterscotch_peek` → `butterscotch_surprise`. A beam in the crystal colour rises from the bottom into his chest. | Sound effect only: whoosh + warm glass chime | the colour name |
| **awaken** (6–8 s) | `bg_wakestone` on a slow push-in. `prop_crystal`, tinted to the crystal colour (clips `awaken_1…3`, one per colour: Dragonberry's deep-to-light purple gradient, Starfrost's sky blue, Rainbow Fizz's rainbow), rises out of the stone. `butterscotch_surprise` floats down on the right, then changes to `butterscotch_delight`; his tummy glow comes on at 3 s. Two gems pop on, each with a chime. | `awaken_a` → `awaken_b` (**variant = colour ID**) → `awaken_c`. Rush mode: `awaken_c` only. | NEW CRYSTAL! / colour / SPARKFINDER! |
| **greet_r2…r7** (2 s) | `bg_cave` with `butterscotch_delight` (clip `greet`). Rank 5 uses `greet_r5`, where a little traveler's hat (`prop_hat`) pops out of his paws and twirls up to his upper right; it never lands on the crystal. The new gem pops on. | one line per rank (see the list below this table) | the rank name |
| **recall_t0…t3** (nice-to-have) | The greet scene; a faded icon of last year's bowl floats up in a corner. | "Last year, a Dragon Gem! Remember?" | LAST YEAR |
| **charge** (2.5 s; returning crystals only; skipped in rush) | `butterscotch_base` with his paws out while the tap prompt and fill bar pulse. Each tap on the window swells his tummy glow. When the bar is full, a `charge_full` chime plays. The fill nudges the GLOW number and slightly raises the rare odds, and it counts before the result is written. | `charge_tap`: "Tap the window! Pump up the glow!" (with a button: `charge`, "Press the button! Pump up the glow!") | TAP! TAP! (with a button: PRESS!) |
| **reveal_t0** (2.5 s) | Visuals `reveal_t0_1…3`, one per gag in §7: Rubber Chicken; Hiccup Bubbles; Snuff-Out (the cave goes dark, only goblin eyes blink). The GLOW number stays hidden. | the line for that gag | GOBLIN TRICK! |
| **reveal_t1** (2.5 s) | `butterscotch_delight`. `prop_coin` flips up out of his paws and spins up to the upper right. | "Hoard coin! A big shiny one!" | HOARD COIN! |
| **reveal_t2** (2.5 s) | `bg_wakestone` with `butterscotch_surprise` on the right. `prop_gem` rises out of the Wakestone and turns once. In variant 2 (`reveal_t2_2`) the gem wobbles up and his gasp turns into a beam. | "Dragon Gem! Whoa, a rare one!" | DRAGON GEM! |
| **reveal_t3** (3 s) | `bg_starburst` with slowly turning gold rays. `prop_star` rises and grows, and `butterscotch_delight` beams beside it. A 1 Hz glow, no strobe. A harp and gong play, which is the host's cue to come over. | "The Golden Star! You're legendary!" | GOLDEN STAR! |
| **bowl_common / bowl_rare / bowl_legend** | big bowl icon during the last second of the reveal | "Silver coin! Take one treat!" · "Gold star! Take one treat!" · "Dragon gem! The grown-up has it!" | SILVER COIN · GOLD STAR · DRAGON GEM |
| **rush_t0…t3** (nice-to-have) | 1.5 s reveal with the bowl icon showing from the start | "Goblin trick! Silver coin bowl!" · "Hoard coin! Silver coin bowl!" · "Gold star! Golden Star!" · "Dragon Gem! The grown-up has it!" | the tier name |
| **replay** (2 s) | `butterscotch_wink` (`butterscotch_peek` until it exists). Tonight's result and GLOW number appear dimmed, with a ✓ and the word TONIGHT. | "Shh! Your crystal already glowed tonight!" | ALREADY GLOWED |
| **nocrystal** (3–4 s, then `bowl_common`; a tap on the window with no crystal on the stone) | `butterscotch_puff` → `butterscotch_delight`. A sparkly blessing bubble (`fx_bubble`) drifts down to the kid, and the beacon gains a spark. **foreign** reuses this cue. | "A dragon blessing! Whoosh, you sparkle!" | DRAGON BLESSING! |
| **sleepy** (2 s) | `butterscotch_yawn` (`butterscotch_sleep` until it exists) and the place-crystal hints. | "Again, please! Sleepy crystal." | AGAIN, PLEASE |
| **goodbye** (1.5 s; skipped in rush) | `butterscotch_wave` (`butterscotch_delight` until it exists) floats up and away like a balloon, and his tummy glow fades with him. | "Bring your crystal back next Halloween!" | SEE YOU 2027 |
| **milestone** (every 100th visit) | `bg_starburst` with `butterscotch_delight` on the right; the device puts the big counter in the centre with confetti in the crystal colours. A milestone flag is saved on that crystal. | "Another hundred crystals! Hooray, travelers!" | HOORAY, TRAVELERS! |
| **moonfull** (nice-to-have; once per night) | `bg_cave`, then `bg_cave_lit` fades in as the beacon blazes (the device flares the meter). `butterscotch_delight` → `butterscotch_giggle`. `snuffwick_sneeze` spins in dazzled, turns into `snuffwick_shy`, then scampers off hugging `snuffwick_sock`. | `moonfull_a` "My beacon is lit! You did it, travelers!" → `moonfull_b` "Silly Snuffwick... come to the feast! You're my Official Prankster!" | BEACON LIT! → OFFICIAL PRANKSTER |
| **circle** (3 or more crystals in one group) | `bg_wakestone` with `butterscotch_giggle`. Small crystals in the group's colours orbit him, and `snuffwick_sneeze` peeks in. | "A crystal circle! All the worlds together!" or *Snuffwick:* "Eek! Too much glow! Hee hee!" | CRYSTAL CIRCLE! |
| **keeper** (Keeper of the Hoard; about 1 visit in 3) | `butterscotch_wink` with a slow push-in. This is the only close-up, and only the oldest kids see it. | "Psst, Keeper... my real name's Bartholomew." | SHH... SECRET |

### attract_call lines
1. "Crystal on the Wakestone! Wake me up!"
2. "No crystal? Tap my window!" (`attract_tap_1`, played in this slot because there is no big button. With a button, `button = yes`, it is `attract_call_2`: "No crystal? Press the big button!")
3. "Zzz... a goblin snuffed my beacon... zzz."
4. "Every crystal adds a spark to my beacon!"
5. "Shh... I hear travelers coming..."
6. "Wake me, travelers! My candy hoard is waiting!"
7. "Set your crystal here! I'm awake... almost!"
8. *Snuffwick (pitched up):* "Pfft! Hee hee! I like the dark!"
9. "Travelers from every world! Come in, come in!"

### Greeting lines
Line 1 is the main line. Line 2 plays at random, including on a repeat visit at the same rank (visit 7 as Dragon Rider, visit 9+ as Keeper).

| Cue | Line 1 | Line 2 |
|---|---|---|
| greet_r2 (Apprentice) | "Apprentice! You found your way back!" | "Apprentice! Your crystal remembers you!" |
| greet_r3 (Glowcaster) | "Glowcaster! Your glow is getting big!" | "Glowcaster! Welcome back! Shine on!" |
| greet_r4 (Hoardguard) | "Hoardguard! My trusty candy protector!" | "Hoardguard! Keep those goblins away!" |
| greet_r5 (Worldwalker) | "Worldwalker! Here's your traveler's hat!" | "Worldwalker now! Here's your very own hat!" |
| greet_r6 (Dragon Rider) | "Dragon Rider! Climb aboard, old friend!" | "Dragon Rider! You're back! Wheee!" |
| greet_r7 (Keeper of the Hoard) | "Keeper of the Hoard! I bow to you!" | "Keeper! The whole hoard rejoices!" |

### Other lines
- **awaken_a:** "Ooh! A brand-new crystal is waking!" · "A new crystal! Hello, little light!"
- **awaken_c:** "Sparkfinder! Welcome, little traveler!" · "Sparkfinder! Look, two sparks already!"
- **charge** (no button, `charge_tap`): "Tap the window! Pump up the glow!" · "Tap tap tap! Fill your crystal!" With a button (`charge`): "Press the button! Pump up the glow!" · "Press fast! Fill your crystal!"
- **reveal_t1:** "Hoard coin! A big shiny one!" · "Hoard coin! Shiny silver treasure!"
- **reveal_t2:** "Dragon Gem! Whoa, a rare one!" · "Dragon Gem! Not many travelers find those!"
- **reveal_t3:** "The Golden Star! You're legendary!" · "Golden Star! The rarest treasure of all!"
- **replay:** "Shh! Your crystal already glowed tonight!" · "Still glowing from tonight! Clever crystal!"
- **nocrystal:** "A dragon blessing! Whoosh, you sparkle!" · "Dragon blessing! You added a spark too!"
- **sleepy:** "Again, please! Sleepy crystal." · "Place it again! Your crystal's yawning."
- **goodbye:** "Bring your crystal back next Halloween!" · "Keep it safe, traveler! See you next year!"
- **recall_t0…t3:** "Last year, Snuffwick tricked you! Silly goblin!" · "Last year, a shiny hoard coin!" · "Last year, a Dragon Gem! Remember?" · "Last year, the Golden Star! Legendary!"
- **keeper:** "Psst, Keeper... my real name's Bartholomew." · "Shh... my wings are tiny. I just float!"

Every interaction line is 8 words or fewer and starts with its key word; attract calls are 10 words or fewer. Delivery notes for every line are in `CHATGPT-ASSETS.md` §4.

### chapter (about 25–30 s, played in the idle rotation)
Every part makes sense if a kid only catches that one. The device draws the beacon meter over parts a, e and f (the cave scenes, except b, where the beacon goes out).

| Part | Shot | Voice | Caption |
|---|---|---|---|
| a | `bg_cave_lit` (or `bg_cave`): `butterscotch_sleep` wakes into `butterscotch_delight` | "Every Halloween, the doors between worlds open... and I wake up!" | BUTTERSCOTCH'S HOARD |
| b | The lit cave. `snuffwick_base` climbs the candy pile under the beacon, *pfft!*, and the cave goes dark (`bg_cave` fades in) while Butterscotch sleeps through it. Snuffwick scampers off. | "But sneaky Snuffwick snuffed my beacon! Pfft! How will travelers find me?" | UH-OH, SNUFFWICK! |
| c | `chapter_egg` on a slow Ken Burns: baby Butterscotch in his cracked crystal eggshell, the glowing pieces drifting off through little doors to other worlds | "Long ago, I hatched from a crystal egg. Its pieces are yours!" | THE CRYSTAL EGG |
| d | the crystal on the Wakestone changing through the three crystal colours (Dragonberry, Starfrost, Rainbow Fizz) | "Each crystal glows when it's near me... and glow wakes dragons!" | YOUR GLOW |
| e | `bg_cave` with `bg_cave_lit` slowly fading in; `butterscotch_peek` → `butterscotch_delight` | "Every crystal adds a spark. Light my beacon, travelers!" | LIGHT THE BEACON |
| f | `butterscotch_peek` while Snuffwick scampers past hugging the glowing sock (`snuffwick_sock`) | "Psst... what's Snuffwick hiding in that lumpy sock?" | WHAT'S IN THE SOCK? |

## 10. Counters, milestones and circles

- **The Hoard Beacon (tonight only).**
  - Every crystal visit and every no-crystal blessing adds 1 spark. Replays don't count. A blessing from a tap on the window adds a spark at most once every 20 s (`config.ini` `[timing] tap_spark_sec`), so a kid drumming on the window can't light the beacon alone.
  - Its glow grows from a faint ember to a full blaze as sparks ÷ `moon_goal` goes from 0 to 1.
  - When it reaches the goal, the `moonfull` cue plays once, the attract loop switches to the lit cave (`attract_full`), and the music to `idle_full`.
  - After that, each reveal makes the beacon swell a little brighter.
- **Station counter (all time).** "✦ N sparks for the beacon" sits under the beacon. It's what kids brag about.
- **Milestones.** Every 100th visit gets confetti and its own voice line, and that crystal remembers it was a milestone.
- **Circles.** The circle cue plays once per group, when three or more crystals arrive together.

## 11. Style guide

| Use | Hex |
|---|---|
| Night indigo / plum shadow / bottom band | #140F2E / #2B1650 / #0A0B1E |
| Butterscotch caramel / toffee shadow / cream belly | #C77A35 / #9A5424 / #FBE3BF |
| Wizard hat / hat stars | #6E5A9E / #F3E6C4 |
| Snuffwick sage / his snuffer (pewter) | #8DBF6A / #8E95A3 |
| Hoard Beacon glow (device) | #FFB347 |
| GOBLIN TRICK! text and its puff | #8DBF6A |
| **Reserved for bowls:** silver / red / gold | #D9DEE8 / #FF3B47 / #FFC83D |

**Look and mood:** round, soft, glossy candy shapes; one bright subject on a deep, dark field; a cozy-spooky cave lit warmly by the hoard; soft storybook 3D. On-screen words are drawn by the device in its built-in bold font, white with a dark outline.

**Do**
- Keep the bottom 25% of every image plain near-black.
- Keep subjects in the top three-quarters of the frame.
- Keep the **upper-left of `bg_cave`** (the beacon) uncluttered, and keep sprites out of it in every cave scene.
- Use big, simple shapes, and rim-light dark characters so they stand out.
- Make every expression an edit of that character's base image.

**Don't**
- Use grain, fine texture, mist, bokeh, scales, fur strands or tiny stars. They band and turn blocky on this screen.
- Put text in images.
- Make Butterscotch gold or yellow. Gold appears only on the Golden Star and its starburst (plus the glow inside the stinger sock, a deliberate tease).
- Use red anywhere except the Dragon Gem.
- Draw fire, smoke, teeth or claws, or use a roar sound.
- Show Snuffwick holding, eating or reaching for candy.
- Show faces full-screen (keeper is the only exception).
- Flash more than 3 times a second.

**ChatGPT recipe**
- **Prompt prefix:** "Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, no text..." The exact prompts are in `CHATGPT-ASSETS.md`.
- **Backgrounds:** add "wide landscape 5:3, dark indigo and plum, bottom quarter plain near-black and empty". A 1536×1024 output is cropped to 1536×922, then scaled to 800×480.
- **The beacon spot:** `bg_cave` asks for the beacon's globe about one-fifth of the way in from the left and a little under a quarter of the way down, which is (150, 110) on the screen. ChatGPT won't land it exactly. After the first build, look at `assets/previews/` and set `meter.pos` to where the painted beacon really is (the words key in the generator, or `[meter] pos` in the card's `captions.ini`). Then make `bg_cave_lit` as an edit of `bg_cave`, so the beacon stays put.
- **Sprites:** add "full figure, centred, on a TRANSPARENT background; if you can't, a flat pure green #00FF00". The builder keys out green (or black).
- **Face edits (6 MVP + yawn):** "Use the attached butterscotch_base image as Image 1. Edit Image 1: change ONLY the face: ...; keep his body shape, pose, paws, wings, horns, tail, the hat (same tilt, same two stars), size, colours, framing and the transparent background EXACTLY identical." Always edit from the base, never from a previous edit, and keep all of Butterscotch's edits in a single chat.
- **Pose edits (only 2):** `butterscotch_wink` and `butterscotch_wave` change ONLY his right paw and his face.
- **If the body drifts:** re-roll that edit from the base. Small drift between face edits hides under the bob and the crossfade.
- **Anchor:** after the edits, check that the cyan ring in `assets/previews/` sits on his chest/belly. If not, change the `anchor` (`[0.5, 0.62]`) of the Butterscotch stills.
- **Sprite sizes:** a clip's sprite height is the whole picture, including any transparent margin, so ask for the character to fill the frame. If he looks too small or too big in the previews, change the `BS` height in `tools/gen_manifest_dragonglow.py` and re-run it.

**Clip layout (800×480), as built by `tools/gen_manifest_dragonglow.py`**

| Element | Where |
|---|---|
| Butterscotch, home spot | centre x 420, top y 40, height 300 (bottom ≈ 340); tummy anchor ≈ (420, 226) |
| Butterscotch, side spot | centre x 625–655, height 200–230 (awaken, Dragon Gem, Golden Star, milestone) |
| Snuffwick | height 115, top y ≈ 225 on the cave floor, enters from the right |
| Hoard Beacon | painted at (150, 110); the device's glow radius is 70 (`meter.pos`); no sprites at x < 230, y < 200 in cave scenes |
| Props | coin and hat to the upper right of Butterscotch; gem and crystal on the Wakestone (x 400) |
| Bottom band | y ≥ 360: caption, GLOW number (right), 8-gem meter (centre), bowl icon (left) |

## 12. Production plan (7 days)

### Images: 29 stills (16 MVP), one slot spare for re-rolls
- **Day 1: Butterscotch.** Make `butterscotch_base` and re-roll it until it's right, because every other Butterscotch image is an edit of it. Then make `sleep`, `peek`, `delight` and `surprise`.
- **Day 2: Butterscotch `giggle` + `puff`, Snuffwick and props.** Make `snuffwick_base` and `snuffwick_chicken`, then the four props. For `prop_crystal`, give ChatGPT a photo of one of your 3D-printed crystals and ask for a colourless version (the builder adds each colour).
- **Day 3: backgrounds.** Make `bg_cave` first and check where the beacon landed (set `meter.pos`), then `bg_wakestone` and `bg_starburst`. Run `python -I tools/build_assets.py --placeholders` and look at `assets/previews/`.
- **Days 4–5: nice-to-haves.** Make `bg_cave_lit`, the yawn, wink and wave, Snuffwick's sneeze, shy and sock, the hat, feather, bubble and eyes, `chapter_egg` and `print_keyart`, as budget allows.

### Voice: 74 lines, recorded in batches
`tools/make_voice_elevenlabs.py` records them with ElevenLabs straight into `assets/source/voice/` (README → *Voice lines with ElevenLabs*); `--priority MVP` does batch 1 plus the rank lines.
1. **Needed on night one (39 lines):** `attract_call` 1, 3–7 and 9, `attract_tap_1`, all of awaken (7: two `awaken_a`, one `awaken_b` per crystal colour, two `awaken_c`), `greet_r2` and `greet_r3` line 1 (old Skull tokens can reach rank 3), `charge_tap` (2), all reveals (9), the three bowl lines, replay, nocrystal, sleepy and goodbye.
2. **Night-one extras (21 lines):** chapter (6), rush (4), moonfull (2), milestone, circle (2), recall (4), Snuffwick's attract call, and `greet_r2` line 2.
3. **Only with a big button (3 lines):** `attract_call_2` and `charge` 1–2. They play only when `config.ini` says `button = yes`.
4. **Before Halloween 2027 (11 lines):** the rest of `greet_r3` to `greet_r7` and keeper. These can't play in 2026 anyway.

The manifest marks 43 lines MVP (every rank's line 1, so the station is complete for any crystal) and 31 NICE. Number variants with no gaps (`_1`, `_2`, `_3`…), because the firmware stops counting at the first missing number.

### Sound and music
17 sound effects (10 MVP; `button` is NICE, since it only plays with a big button) and 2 music beds (1 MVP), with the same ids as before and descriptions rewritten for the dragon world: a sparkle-sneeze, rubber-chicken squeaks, a goblin giggle, a candle "pfft", and a warm beacon "whoomph" for the finale. Never a roar or a fire crackle. The full list with search words is in `CHATGPT-ASSETS.md` §5–6.

### Firmware features this story uses
1. **The Hoard Beacon meter**: the words `meter.style = lantern`, `meter.pos = 150,110,70` (x, y, glow radius in screen px), `meter.name = Beacon` and `meter.color = #FFB347` tell the screen to draw a warm glow on the beacon painted into `bg_cave`, instead of the old Moon in the top-right sky. Fill = tonight's sparks ÷ `moon_goal` (config.ini, default 50; crystal visits and no-crystal blessings each add 1). It is drawn on the visuals listed in `meter.on` (every cave scene except chapter part b, where Snuffwick snuffs it; in `keeper` it follows the push-in via `meter.zoom_keeper`), with the station counter under it. The host menu has **Fill the Beacon** and **Reset Beacon**. When it fills, `moonfull` plays once and the attract loop switches to `attract_full`. The finale is triggered by the meter or by the host, not by the clock.
2. **Awaken colour line:** the screen plays `awaken_b_<colourId>` (the crystal's bench colour: 1 Dragonberry, 2 Starfrost, 3 Rainbow Fizz). Crystals without a bench colour skip that line.
3. **Crystal colours that move:** a colour in the words (`color.N_hex`) can be one colour (`#5CD6FF`), 2–4 stops (`#5A1E9E,#C8A0FF`) or `rainbow`. The screen blends the stops (or runs the rainbow) round and round, about 4 s per cycle, in every glow, beam and sparkle. The plinth ring crossfades a two-colour crystal (`LED_DUAL`) and drifts through the rainbow for a rainbow one. The number of colours is the highest N with a `color.N_hex`, counting from 1 without gaps (3 now). The builder tints the awaken clip of each colour as a gradient (`awaken_1…3`). (An optional plinth ring would crossfade a two-colour crystal, `LED_DUAL`, and drift through the rainbow for a rainbow one.)
4. **Tricks:** the GLOW number is hidden on GOBLIN TRICK! results, the same gag never plays twice in a row, and never on a first visit.
5. **Rush lines:** in rush mode the screen plays `rush_t<tier>` (bowl first); if that line doesn't exist it plays the bowl line.
6. **Tummy glow:** each clip carries an anchor (generated from the stills' `anchor` into `captions.ini [anchor]`); the screen draws a glow there in the crystal colour that pulses with Butterscotch's voice. The absorb beam aims at it.
7. **keeper:** plays for Keepers of the Hoard about 1 visit in 3 (`keeper_one_in`).
8. **Gag effects** (words `fx.reveal_t0_1…3`, drawn by the screen): gag 1 = a white poof where the chicken turns into a coin (≈ (615, 230) at 1.1 s); gag 2 = sparkles in the crystal colour from Butterscotch's mouth (≈ (440, 170), 0.6–1.8 s); gag 3 = a crystal-colour sparkle burst from his nose (≈ (420, 150)) at 1.42 s. During the dark beat (0.72–1.42 s) the beacon and tummy glows stay lit: the kids' light can't be snuffed. The puff that hides the GLOW number on a trick is sage green (`tier.0_color`).
9. **No big button** (`config.ini [station] button = no`, the default): the screen reads the word `nobutton.<key>` before `<key>` and swaps voice per `nobutton.voice`. Dragonglow's: `nobutton.voice = attract_call_2=attract_tap_1;charge=charge_tap` (line → line, group → group), `nobutton.caption.charge = TAP! TAP!`, `nobutton.ui.press = Tap the window!`, `nobutton.ui.or_press = OR TAP ME` (captions.ini section `[nobutton]`, §15). A tap on the window with no crystal on the stone gives the dragon blessing. Set `button = yes` once a button is wired, and the original lines and words come back (§2).

### Old Skull Oracle tokens
Test one first (host menu → **Crystals** → test read, or the bench).
- **If it carries the SK marker,** the Dragon's Window greets the kid by their visit count (an Apprentice or a Glowcaster), and recall plays their old result.
- **If it doesn't,** the token reads as foreign and the kid gets a dragon blessing. The host then offers them a crystal.

## 13. Parent sign

> **DRAGONGLOW — The Sleepy Candy Dragon**
> Every child gets candy, every time.
> Your crystal is yours to keep. Bring it back every Halloween to climb the ranks!
> **The crystal remembers only numbers:** how many Halloweens it has visited and each year's glow number. It never stores names, photos or personal information. (Its tag also holds a plain web link about this project.)
> No crystal? Tap the dragon's window for a dragon blessing. Every visitor adds a spark to the Hoard Beacon.
> Butterscotch's voice is AI-generated.
> One crystal on the Wakestone at a time, please. Spooky-cute, no jump scares, no fire, and gentle lights (fewer than 3 flashes per second).

## 14. Host ritual

**Handing out a crystal.** Kneel and hold the crystal out in cupped hands, then say: **"Welcome, traveler! This crystal is a piece of a dragon's egg, and it found its way to you. Wake the dragon on the Wakestone!"**

**The Dragon Gem.** When a kid gets the Dragon Gem, raise the dragon gem bowl and say: **"The Dragon Gem found you, traveler! You're legendary. Pick one!"**

**A goblin trick** (if a kid looks unsure): "Snuffwick got Butterscotch again! Silver coin bowl, candy anyway!"

**The finale** (when the beacon lights): lead a cheer for the travelers: "You lit the beacon!"

## 15. captions.ini (ready to paste)

Generated from the manifest words (`tools/gen_manifest_dragonglow.py`). The build writes this file to the card itself, adding the `[anchor]` section; paste it by hand only for a quick fix on the card. The `[nobutton]` section is what the screen uses instead of the words it names while `config.ini` says `button = no` (the default: no big button this year, §12 item 9).

```ini
title = Dragonglow
character = Butterscotch

[rank]
1 = Sparkfinder
2 = Apprentice
3 = Glowcaster
4 = Hoardguard
5 = Worldwalker
6 = Dragon Rider
7 = Keeper of the Hoard

[tier]
0 = GOBLIN TRICK!
0_color = #8DBF6A
1 = HOARD COIN!
1_color = #D9DEE8
2 = GOLDEN STAR!
2_color = #FFC83D
3 = DRAGON GEM!
3_color = #FF3B47

[bowl]
common = Silver Coin
common_color = #D9DEE8
common_shape = circle
rare = Gold Star
rare_color = #FFC83D
rare_shape = star
legend = Dragon Gem
legend_color = #FF3B47
legend_shape = triangle

[color]
1 = Dragonberry
1_hex = #5A1E9E,#C8A0FF
2 = Starfrost
2_hex = #5CD6FF
3 = Rainbow Fizz
3_hex = rainbow

[caption]
attract = Crystal here
absorb =
awaken = NEW CRYSTAL!
greet = Welcome back!
charge = PRESS!
reveal_t0 = GOBLIN TRICK!
reveal_t1 = HOARD COIN!
reveal_t2 = GOLDEN STAR!
reveal_t3 = DRAGON GEM!
replay = ALREADY GLOWED
nocrystal = DRAGON BLESSING!
foreign = DRAGON BLESSING!
sleepy = AGAIN, PLEASE
goodbye = SEE YOU {next_year}
milestone = HOORAY, TRAVELERS!
circle = CRYSTAL CIRCLE!
keeper = SHH... SECRET
chapter =
chapter_a = BUTTERSCOTCH'S HOARD
chapter_b = UH-OH, SNUFFWICK!
chapter_c = THE CRYSTAL EGG
chapter_d = YOUR GLOW
chapter_e = LIGHT THE BEACON
chapter_f = WHAT'S IN THE SOCK?
moonfull = BEACON LIT!
moonfull_b = OFFICIAL PRANKSTER
recall = LAST YEAR

[ui]
place = Crystal on the Wakestone
hold = Hold still...
power = GLOW
counter = sparks for the beacon
press = Press the button!
or_press = OR PRESS
tonight = TONIGHT
reader_asleep = READER ASLEEP
rush = RUSH
touch = TOUCH FOR A BLESSING
no_card = NO TF CARD

[meter]
style = lantern
pos = 150,110,70
name = Beacon
color = #FFB347
on = attract,absorb,greet,recall,charge,reveal_t0,reveal_t1,replay,nocrystal,foreign,sleepy,goodbye,keeper,chapter_a,chapter_e,chapter_f
zoom_keeper = 1,1.15

[glow]
idle_color = #FFC27A

[fx]
reveal_t0_1 = poof,615,230,1100,1100
reveal_t0_2 = sparkles,440,170,600,1800
reveal_t0_3 = burst,420,150,1420,1420

[nobutton]
voice = attract_call_2=attract_tap_1;charge=charge_tap
caption.charge = TAP! TAP!
ui.press = Tap the window!
ui.or_press = OR TAP ME
```

## 16. The other concepts (swap-in options)

If you'd rather go a different direction, these were the other pitches. Each reuses the same firmware, plinth and crystals; only the art, the lines and the words in `captions.ini` change.

### Moonshards: Oona's Halloween Window (the panel winner, now archived)
- **The idea:** a sleepy grandmother beacon-ghost lights the Halloween Moon so every trick-or-treater finds their way home. Smudge, a fluffy marshmallow moth, nibbled it to a sliver, and the kids' crystals fill it again. Results: SMUDGED! · SILVER MOON · WITCH'S HAT · WISHING STAR, on the same silver/red/gold bowls.
- **Swap to it if** you want the calmest bedtime-story mood with the highest parent comfort. It won the judging on clarity and systems.
- **It's ready to go:** the full bible is `docs/stories/MOONSHARDS.md` and its manifest is `docs/stories/manifest.moonshards.json`. Copy that file to `assets/manifest.json`, then run `build_assets.py --gen-defaults` and `gen_asset_list.py`, reflash the screen (its built-in words change) and rebuild the card. The firmware draws its Moon meter in the top-right sky (its `meter.*` / `fx.*` words). It has no no-button words or tap lines yet (its `attract_call_2` and `charge` lines say "press the button"): before using it on the button-less plinth, add `nobutton.*` words and tap lines the way Dragonglow does (§12 item 9).

### Lumpkin's Hungry Cauldron (silly: the production judge's pick)
- **The idea:** a sassy floating jack-o'-beacon and Burp, his bottomless cauldron, relight Pumpkin Street after a grumpy cloud blows it out. Every crystal makes Burp burp up a fortune, all the way up to the Golden Burp.
- **Swap to it if** ChatGPT consistency becomes the bottleneck. A rigid pumpkin needs only face edits, so it is the easiest character to keep looking the same, and glowing carvings read best on a 7-inch screen. It also gets the most laughs from ages 3–7.
- **Fix before using:** cut the toot gag; never show Burp slurping a crystal's light (little kids will think it got eaten); rename the colour/bowl clashes (the Slime Lime crystal vs the Slime Pool bowl, the Witch Plum crystal vs the Witch Hat bowl); rim-light the dark cauldron and the dark cloud.
- **Cost:** about 25 stills, about 64 lines, one voice plus a curated set of cartoony burp sound effects.

### Madame Mira's Magic Mirror (mystic: second place on systems)
- **The idea:** a giggly ghost-grandma fortune-teller lives inside a magic mirror. Kids' crystal light wipes away the fog that a cheeky fog-puff keeps blowing on her glass, and each crystal reads a "fortune".
- **Swap to it if** you want a cozy parlour mood and one setting that never changes (the mirror), which makes it the cheapest base for a new chapter each year.
- **Already borrowed into Dragonglow:** the Official Prankster finale (her "Official Trickster"), hiding the number on tricks, and never showing a face full-screen.
- **Fix before using:** drop the headscarf, coins and hoop earrings (stereotype risk); bake the fog as 4–5 fixed levels (fog bands badly on MJPEG); recolour the bowls away from the crystal colours; simplify the rank names.
- **Cost:** about 24 stills, about 50 lines, one voice.
