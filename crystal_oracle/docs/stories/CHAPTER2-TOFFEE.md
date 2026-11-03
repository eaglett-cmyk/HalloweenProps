<!-- Chapter 2 of Dragonglow, for Halloween 2027. Written 2026-11-02 at the owner's request: art for Toffee,
more story, Toffee in the voice lines and in every crystal placement. NOT on the live site yet (owner): the
website parts below are a plan only. The lines and stills live in tools/gen_manifest_dragonglow.py like the rest. -->

# Dragonglow, Chapter 2 (2027): "The Egg in the Sock"

*Follows Chapter 1, "The Night the Beacon Went Out" (STORY.md §8). Same cave, same Wakestone, same beacon, same
Butterscotch and Snuffwick. New: baby **Toffee**.*

> **Logline.** Last Halloween, while everyone cheered the blazing beacon, Snuffwick tiptoed off hugging his
> favourite sock. Something inside was warm and went *tap, tap, tap*. All year he kept it safe, and just before
> Halloween it hatched: a baby dragon, **Toffee**, glittery purple with little candy-corn horns. Now
> there are TWO sleepy dragons in the cave. Butterscotch wakes up when a crystal glows near him, like always. But
> Toffee is brand new and extra sleepy: she needs a whole night of crystal glow to wake up all the way, and when
> the beacon is full she is finally awake enough for her very first **flying lessons**.

**What every kid gets (unchanged):** your crystal wakes a dragon (now two), you are the light-bringer, and you
always get candy. New this year: you can *see* your glow waking Toffee, a little more with every crystal.

## 1. Toffee

**How she looks** (the owner's design, `assets/source/images/ref_toffee_owner.png`, redrawn in Butterscotch's
style as the master still `toffee_base`; every pose is an edit of it)
- A small, chubby baby **girl** dragon, kitten-sized next to Butterscotch.
- Glittery **lilac-violet purple** with shiny rounded scales and a row of soft rounded spikes down her head and back.
- Two little **candy-corn horns** (white tip, orange middle, yellow base): the Halloween candy she is named for.
- Pale-lavender belly plates, rosy cheeks, huge sparkly amber-brown eyes, a sweet little smile.
- Translucent, shimmering pink-lilac **wings** (hers will fly one day, unlike Butterscotch's) and a long curled tail.
- Her bed is **Snuffwick's striped sock** (plum and cream, slouchy, a little holey). She sleeps in it, sits up in
  it and, when she flies, hovers just above it.
- No fire, no teeth, no claws, never scary. Same rules as Butterscotch.

**Personality.** A sweet, giggly, VERY sleepy baby. She talks in tiny 2–5 word bursts, mumbles in her sleep, yawns
mid-word and gets over-excited the moment she is properly awake. She calls the big dragon "Butterscotch" and adores
Snuffwick, her egg-sitter. She wants more than anything to fly.

**Voice.** Her **own** ElevenLabs voice (owner, 2026-11-02): a tiny, squeaky, giggly baby girl dragon, soft and
high, drowsy mumbles and big yawns, bright giggles once awake. Designed with the claude.ai ElevenLabs connector
("Toffee - Sleepy Baby Dragon"). Butterscotch is still the main voice; Toffee adds one short line per visit.
Snuffwick stays sound effects.

**Lore.** Toffee hatched from a crystal egg, just like Butterscotch did long ago, so she is family. The egg found
its way to the shyest creature in the cave, who hid it in the warmest place he knew: his sock. That is why
Snuffwick, the goblin who loved the dark, now keeps a nightlight on.

## 2. The story (the 2027 attract chapter, `chapter2_a…f`)

Told in the idle rotation like Chapter 1, about 25–30 s, every part makes sense on its own. Butterscotch narrates.

| Part | Still | Line (Butterscotch) | Caption |
|---|---|---|---|
| a | `chapter2_sock`: Snuffwick in his dark nook hugging the glowing sock | "Last Halloween, Snuffwick found something in his sock..." | SNUFFWICK'S SOCK |
| b | `chapter2_sock` (push-in, sparkles from the holes) | "An egg! Warm and glowing, going tap, tap, tap!" | TAP... TAP... TAP... |
| c | `chapter2_hatch`: the egg cracks in the sock, Toffee peeks out | "Crack! Out popped a baby dragon. Toffee!" | MEET TOFFEE! |
| d | `chapter2_nap`: both dragons asleep, the beacon a faint ember | "But Toffee is SO sleepy. Babies need lots of glow." | SO SLEEPY... |
| e | `chapter2_nap` (slow push-in to Toffee) | "Every crystal wakes her up a little more!" | BRING YOUR CRYSTAL |
| f | `chapter2_lesson`: the beacon blazes, Toffee's first wobbly loop | "Fill the beacon, and Toffee gets flying lessons!" | FLYING LESSONS! |

**The night itself.**
1. The cave is quiet: Butterscotch floats asleep, and Toffee sleeps curled in the sock on the candy pile at his side.
   The beacon is dim. Every so often Toffee mumbles in her sleep.
2. Each crystal on the Wakestone sends its glow to **both** dragons. Butterscotch wakes right up, as always. Toffee
   only stirs: how far she wakes depends on how full the beacon is tonight (§3).
3. All night the beacon fills, and every crystal finds Toffee a little more awake: asleep, then one eye open, then
   yawning and stretching, then wide awake and flapping.
4. **Finale (the beacon is full).** The beacon blazes, Toffee pops wide awake, flaps, and hovers for the very first
   time. Butterscotch: "My beacon is lit! Toffee's wide awake!" Toffee: "Teach me to fly! Please, please!"
   Butterscotch: "Flying lessons, little one! Starting tonight!" From then on she is awake and practising.
5. **After the night** (website, §5): the flying lessons happen online, one lesson at a time.
6. **Stinger for 2028:** during a lesson Toffee zooms too high and bumps a round door in the cave wall. It wobbles
   but won't open... (sets up 2028, "The Door That Wouldn't Open").

## 3. How Toffee plays at the station

**Toffee is in every scene where a crystal is placed** (owner, 2026-11-02). She is drawn live by the station over
the scene art (sprites `toffee_*.png`, `sprites/` on the iPad card), always in the same spot: on the cave floor,
lower left, sitting in her sock (clear of the beacon above her, Snuffwick on the right and the crystal in the
middle). Because she is drawn live, one set of clips serves every stage and every scene.

**How awake she is = how full the beacon is tonight** (the same meter the kids are filling):

| Stage | Beacon | In the attract loop | When a crystal glows | Her line (one per visit) |
|---|---|---|---|---|
| 0 Fast asleep | under 1/3 | `toffee_sleep`, breathing | the glow washes over her, she wriggles, stays asleep | sleep mumbles (`toffee_s0`) |
| 1 Drowsy | 1/3 to 2/3 | asleep | `toffee_drowsy`: one eye opens | groggy hellos (`toffee_s1`) |
| 2 Nearly awake | 2/3 to full | asleep | `toffee_yawn` big stretch, then `toffee_awake` | waking-up giggles (`toffee_s2`) |
| 3 Wide awake | full (after the finale) | `toffee_awake` / `toffee_flap`, practising | `toffee_awake`, flaps on the prize | awake and flying (`toffee_s3`) |

- Her glow: while a crystal is on the Wakestone, a soft glow in the crystal's colour sits behind her too.
- Her line plays after Butterscotch's greeting (first visit: after "Sparkfinder!"; returning: after the rank line),
  about 1.5–2 s. **Rush mode** skips her line (she still shows and still reacts).
- Five takes per stage, so a group of up to five never hears the same Toffee line twice.
- In the dark Snuff-Out gag she disappears with the lights, like everything else.
- Turned on by `[station] chapter = 2` in the card's `config.ini` (the chapter number is also written to every
  crystal's visit). `chapter = 1` is 2026 exactly as it was.

**Butterscotch's new 2027 lines** (his own voice): five attract calls about Toffee, the 2027 chapter narration and
the 2027 finale. Everything else he says stays as recorded.

## 4. Lines

All lines short (Toffee 2–5 words, Butterscotch ≤ 10). Delivery notes in `gen_manifest_dragonglow.py`.

**Toffee, stage 0 — asleep (`toffee_s0_1…5`)**
1. "Mmm... five more minutes..."
2. "Zzz... sparkly... zzz..."
3. "Mmf... is it morning?"
4. "Mmm... warm crystal... zzz..."
5. "Zzz... flying... wheee... zzz..."

**Toffee, stage 1 — drowsy (`toffee_s1_1…5`)**
1. "Hmm? A crystal? Ooh..."
2. "Yawwn... hello, traveler..."
3. "One eye open! Glowy..."
4. "Is that for me? So warm..."
5. "More glow, please... yawn..."

**Toffee, stage 2 — nearly awake (`toffee_s2_1…5`)**
1. "I'm waking up! More glow!"
2. "Look! My wings are wiggling!"
3. "Almost awake! Hee hee!"
4. "Glow tickles! Wheee!"
5. "Hi, traveler! I'm Toffee!"

**Toffee, stage 3 — wide awake (`toffee_s3_1…5`)**
1. "Wheee! I'm all awake!"
2. "Watch me flap! Flap, flap!"
3. "Look, Butterscotch! I'm hovering!"
4. "Thank you, traveler! Hee hee!"
5. "Your glow helps me fly!"

**Toffee, finale (`moonfull2_toffee_1`)**: "Teach me to fly! Please, please!"

**Toffee, idle loop** (2026-11-02; her own calls between kids, mixed with Butterscotch's. She sleep-talks until the
beacon is full, then practises flying. These are separate from her per-crystal takes, so the five kids in a group
still hear five different ones.)
- Asleep (`toffee_idle_sleep_1…5`): "Zzz... Butterscotch... you're snoring... zzz..." · "Mmm... candy dreams... zzz..." ·
  "Mmf... my sock... my cozy sock... zzz..." · "Mmm... bring a crystal... wake me up... zzz..." ·
  "Zzz... one day... I'll fly... zzz..."
- After the finale (`toffee_idle_awake_1…5`): "Flap, flap, flap! Are you watching, Butterscotch?" ·
  "Wheee! A loop! Almost a loop!" · "Travelers! Come see me fly!" · "Oops! Bumpy landing! Hee hee!" ·
  "Higher! Higher! Wheee!"

**Butterscotch, attract calls 2027 (`attract2_call_1…5`)**
1. "Shh... Toffee's napping. Wake us with a crystal!"
2. "Toffee needs lots of glow to wake up!"
3. "Zzz... two sleepy dragons... zzz..."
4. "Crystal on the Wakestone! Wake us both!"
5. "Snuffwick found an egg! Now we have Toffee!"

**Butterscotch, chapter 2 (`chapter2_a…f`)**: the six lines in §2.

**Butterscotch, finale 2027**: `moonfull2_a` "My beacon is lit! Toffee's wide awake!" ·
`moonfull2_b` "Flying lessons, little one! Starting tonight!"

## 5. The website (plan only — NOT on the live site)

The owner wants the flying lessons on the website, so kids can watch Toffee wake up over time and see her lessons
once the counter is full. None of this is built or live; it is the plan for 2027.

- **Toffee's nest** (new page in `site/play/` or on the storybook cover): Toffee in her sock, drawn in the stage
  that matches the shared Hoard Beacon (the site already keeps it in `data/beacon.json`: station sparks + web
  sparks). Same four stages and the same art as the station (`toffee_sleep` → `toffee_drowsy` → `toffee_yawn` →
  `toffee_awake`), a "how awake is Toffee" bar, and one of her lines as a caption.
- **Before the night**: the games' sparks already feed the beacon, so kids playing at home can watch her stir.
- **When the beacon is full: Flying Lessons.** A series of short lesson pages, unlocked in order (one per day or one
  per N extra sparks): 1 Flap Practice (`toffee_flap`), 2 First Hover, 3 The Wobbly Loop (`chapter2_lesson`),
  4 Landing (in the sock, mostly), 5 Over the Candy Hoard, 6 Through the Beacon's Light. Each lesson is a still,
  2–3 sentences and a tiny game or tap-along (e.g. tap in rhythm to flap her wings). The last lesson ends on the 2028
  stinger (the door that won't open).
- **Crystal pages** (`/c/<id>`): a 2027 visit could show "You helped wake Toffee" with the stage she was at when
  that crystal glowed (the station knows the stage; the visit CSV would need one more column).
- Art still needed for the site: lesson stills 2, 4, 5, 6 (the rest are made).

## 6. Art made (2026-11-02, Codex / ChatGPT plan)

| Still | What |
|---|---|
| `toffee_base` | master: Toffee sitting, sleepy smile (from the owner's design) |
| `toffee_sleep` | curled asleep in the striped sock |
| `toffee_drowsy` | sitting in the sock, one eye half open, rubbing the other |
| `toffee_yawn` | big yawn and stretch, wings out |
| `toffee_awake` | wide awake and delighted, wings spread |
| `toffee_flap` | hovering above the sock, flapping hard |
| `chapter2_sock` | Snuffwick in his nook hugging the glowing sock |
| `chapter2_hatch` | the egg hatching in the sock; Butterscotch and Snuffwick amazed |
| `chapter2_nap` | both dragons asleep, the beacon a faint ember |
| `chapter2_lesson` | the blazing beacon and Toffee's first wobbly loop |
