"""gen_manifest_dragonglow.py -- builds assets/manifest.json for DRAGONGLOW (the story in STORY.md).

Self-contained: every ChatGPT prompt, still, clip recipe ("layers"), voice line, sound and on-screen word
is in this file. Written 2026-10-22 when the owner switched the station from Moonshards (archived in
docs/stories/) to Dragonglow.

    python -I tools/gen_manifest_dragonglow.py          # writes assets/manifest.json
    python -I tools/build_assets.py --check             # then validate it
    python -I tools/build_assets.py --gen-defaults      # firmware words (story_defaults.h)
    python -I tools/gen_asset_list.py                   # CHATGPT-ASSETS.md

assets/manifest.json stays the source of truth for the build. Small tweaks (an anchor, a clip position,
a re-worded line) can be made in the manifest directly, but re-running this script OVERWRITES them -
so either make lasting changes here and re-run, or stop using this script once you hand-edit.

Screen layout used by every clip (800x480, see STORY.md "Cue sheet"):
  * bottom band y >= 360: the device draws captions, the GLOW number (right), the 8-gem meter (centre)
    and the bowl icon (left) - nothing important of ours goes below y = 360;
  * the Hoard Lantern is PAINTED into bg_cave at about (150, 110) and the device draws its growing glow
    there (words meter.pos) - no sprite goes into the upper-left corner of a cave scene;
  * Butterscotch floats centred at x = 420, height 300, top y = 40; his chest/belly anchor [0.5, 0.62]
    is where the voice-pulsed glow sits and where the absorb beam lands;
  * Snuffwick is small (height ~115) and enters from the right, on the cave floor (top y ~225).

No big button in 2026 (config.ini [station] button = no, the firmware default): the words "nobutton.<key>"
replace <key> and "nobutton.voice" swaps the button lines for the tap-the-window lines (NOBUTTON_* below;
main() asserts that every line and word that says "button" / "press" has a no-button alternative).
"""
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "assets", "manifest.json")

TITLE = "DRAGONGLOW - The Sleepy Candy Dragon"

# --------------------------------------------------------------------------- prompt building blocks
STYLE = ("Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; "
         "glossy candy-like materials; smooth clean shading; gentle rim light; no grain, no fine texture, no mist, "
         "no bokeh, no tiny stars, no text, no letters, no watermark. Spooky-cute and cozy, never scary.")
BG = ("Format: wide landscape, 5:3 (1600x960 if you can choose; otherwise the widest landscape option). "
      "Dark midnight-indigo and plum colours (#140F2E, #2B1650). Keep the BOTTOM QUARTER of the image plain "
      "near-black and empty (the screen draws words there). Big simple shapes, clean smooth gradients.")
SPRITE = ("Full figure, centred, nothing cropped, on a TRANSPARENT background (PNG). If you can't make it "
          "transparent, use a flat pure green #00FF00 background with no shadow and no floor.")
BS_KEEP = ("Keep his body shape, pose, paws, tiny wings, swirl horns, curly tail, the faded purple wizard hat (same "
           "tilt, same two stars), size, colours, framing and the transparent background EXACTLY identical. Same style.")
BS_FACE = "Use the attached butterscotch_base image as Image 1. Edit Image 1: change ONLY the face: {what}. " + BS_KEEP
BS_POSE = ("Use the attached butterscotch_base image as Image 1. Edit Image 1: change ONLY {what}. Keep his body shape, "
           "his other paw, tiny wings, swirl horns, curly tail, the faded purple wizard hat (same tilt, same two stars), "
           "size, colours, framing and the transparent background EXACTLY identical. Same style.")
SN_EDIT = ("Use the attached snuffwick_base image as Image 1. Edit Image 1: change ONLY {what}. Keep his round "
           "sage-green body, floppy ears, mismatched striped socks, candle-stub cap, size, colours, framing and the "
           "transparent background EXACTLY identical. Same style.")


def P(*parts):
    return " ".join(p.strip() for p in parts if p)


# Butterscotch's look in one place (the base prompt, the key art and the hat prop all use it)
BUTTERSCOTCH_LOOK = (
    "Butterscotch, a chubby, round old dragon shaped like a soft gumdrop or a pear, floating like a balloon "
    "(two short stubby feet tucked under, not standing). Glossy, candy-smooth skin with NO scales, in warm CARAMEL / "
    "butterscotch-toffee orange-brown (#C77A35, deeper toffee #9A5424 in the shadows) with a smooth cream belly "
    "(#FBE3BF) - NOT gold, NOT yellow. Two small rounded horns shaped like caramel swirls; two tiny stubby wings, "
    "much too small to fly; big warm amber-brown eyes with bright highlights; small rounded paws; a short curly "
    "tail ending in a little round tuft. A crooked, faded purple wizard hat (#6E5A9E) with exactly TWO big simple "
    "pale-cream five-point stars (#F3E6C4) sits between his horns.")

# --------------------------------------------------------------------------- stills
# (id, kind, transparent, priority, desc, prompt, extra)
STILLS = [
    # ---- backgrounds (MVP) ----
    ("bg_cave", "background", False, "MVP",
     "The Hoard Cave at night (the main scene): a cozy rounded cave in deep indigo and plum framed by a big round "
     "smooth stone arch; soft piles of glossy candy (wrapped sweets, round lollipops, gumdrops in muted jewel colours, "
     "no gold coins) low on the far left and far right; the HOARD LANTERN hangs UNLIT in the upper-left (globe centre "
     "about x 150, y 110 of the 800x480 frame) with empty dark wall around it - the device paints its glow there; the "
     "centre is open for Butterscotch; bottom quarter plain near-black and empty.",
     P("The Hoard Cave at night: a cozy, rounded cave interior in deep indigo and plum, framed by a big round smooth "
       "stone arch. Soft piles of glossy candy - big wrapped sweets, round lollipops and gumdrops in muted jewel colours "
       "(NO gold coins, nothing gold) - rest low on the far left and the far right sides of the cave floor.",
       "THE HOARD LANTERN: an old, round, dark-iron lantern with a big ROUND, CLEAR, EMPTY glass globe hangs from a short "
       "chain in the UPPER-LEFT of the picture - its globe centre about one-fifth of the way in from the left edge and a "
       "little less than one-quarter of the way down from the top; the lantern is about one-sixth of the image height. "
       "It is clearly visible but UNLIT and dark (no flame, no glow), with plain, empty, dark cave wall all around it "
       "(a glow will be painted there later).",
       "Leave the CENTRE of the picture open and empty (a floating dragon goes there) and keep the upper-left area "
       "around the lantern uncluttered.", BG, STYLE),
     {}),
    ("bg_wakestone", "background", False, "MVP",
     "The Wakestone: a deeper chamber of the cave framed by big soft rounded rock shapes; in the exact centre a round, "
     "flat-topped stone pedestal with a carved curled-up sleeping-dragon swirl on its front, lit by one broad soft "
     "column of warm cream light from above; its top surface at about 60% of the height and EMPTY (the crystal rises "
     "from it); bottom quarter near-black and empty.",
     P("The Wakestone: a deeper chamber of a cozy cave, dark indigo and plum, framed by big soft rounded rock shapes.",
       "In the exact centre stands a round, flat-topped grey stone pedestal (the Wakestone) with a simple carved swirl of "
       "a curled-up sleeping dragon on its front. One broad, soft column of warm cream candle-light falls onto it from "
       "above. The pedestal's top surface is at about 60% of the image height and is EMPTY (nothing on it).",
       BG, STYLE),
     {}),
    ("bg_starburst", "background", False, "MVP",
     "Golden Star burst: a blue-black night sky with about 12 broad, soft-edged gold (#FFC83D) rays fanning from the "
     "exact centre, a soft gold glow at the centre and a few large simple stars; nothing else; bottom quarter fades to "
     "near-black. (The only gold background.)",
     P("A deep blue-black night sky with about 12 broad, soft-edged gold (#FFC83D) rays fanning out from the exact "
       "centre of the image, a soft gold glow at the centre and a few large simple stars. Nothing else.", BG, STYLE),
     {}),
    # ---- Butterscotch (MVP: master + 6 face-only edits) ----
    ("butterscotch_base", "character", True, "MVP",
     "MASTER IMAGE. " + BUTTERSCOTCH_LOOK + " Small rounded paws held together in front of his tummy; gentle "
     "closed-mouth smile; facing the viewer; no teeth, no claws, no fire, nothing gold.",
     P("MASTER CHARACTER IMAGE. " + BUTTERSCOTCH_LOOK,
       "His small rounded paws are held together in front of his tummy. Gentle closed-mouth smile, facing the viewer. "
       "No teeth, no claws, no fire, no smoke, nothing sharp, nothing gold. Keep the design SIMPLE: big clean shapes and "
       "only a few small details.", SPRITE, STYLE),
     {"anchor": [0.5, 0.62]}),
    ("butterscotch_sleep", "character", True, "MVP",
     "Edit of butterscotch_base: eyes closed in cozy sleep, a small round open snoozing mouth; face-only change.",
     BS_FACE.format(what="eyes closed in cozy, happy sleep, a small round open snoozing mouth, relaxed cheeks"),
     {"edit_of": "butterscotch_base", "anchor": [0.5, 0.62]}),
    ("butterscotch_peek", "character", True, "MVP",
     "Edit of butterscotch_base: one eye open and peeking, the other still closed, a sly little smile; face-only.",
     BS_FACE.format(what="one eye open and peeking, the other eye still closed, a sly little smile"),
     {"edit_of": "butterscotch_base", "anchor": [0.5, 0.62]}),
    ("butterscotch_delight", "character", True, "MVP",
     "Edit of butterscotch_base: a huge open happy smile (no teeth), sparkly happy eyes, rosy cheeks; face-only.",
     BS_FACE.format(what="a huge open happy smile (no teeth, just a soft pink mouth), sparkly happy eyes, rosy cheeks"),
     {"edit_of": "butterscotch_base", "anchor": [0.5, 0.62]}),
    ("butterscotch_surprise", "character", True, "MVP",
     "Edit of butterscotch_base: a round O-shaped mouth, eyebrows raised high, eyes wide and sparkly; face-only.",
     BS_FACE.format(what="a round O-shaped mouth, eyebrows raised high, eyes wide and sparkly with wonder"),
     {"edit_of": "butterscotch_base", "anchor": [0.5, 0.62]}),
    ("butterscotch_giggle", "character", True, "MVP",
     "Edit of butterscotch_base: a silly giggle - eyes squeezed shut into happy arcs, cheeks scrunched, a little pink "
     "tongue poking out of the corner of his mouth; face-only. (Trick gags, circle, finale.)",
     BS_FACE.format(what="a silly giggle - eyes squeezed shut into happy arcs, cheeks scrunched up, a little pink tongue "
                         "poking out of one corner of his mouth"),
     {"edit_of": "butterscotch_base", "anchor": [0.5, 0.62]}),
    ("butterscotch_puff", "character", True, "MVP",
     "Edit of butterscotch_base: cheeks puffed out big and round, eyes wide, nose scrunched as if about to sneeze or "
     "hiccup, lips pressed together; face-only. (Hiccups, the sparkle-sneeze, blowing a blessing.)",
     BS_FACE.format(what="cheeks puffed out big and round like he's holding his breath, eyes wide, nose scrunched up as "
                         "if he is about to sneeze or hiccup, lips pressed together"),
     {"edit_of": "butterscotch_base", "anchor": [0.5, 0.62]}),
    # ---- Snuffwick (MVP: master + rubber-chicken edit) ----
    ("snuffwick_base", "character", True, "MVP",
     "MASTER IMAGE. Snuffwick, a tiny teacup-sized goblin that looks like a plush toy: round soft sage-green (#8DBF6A) "
     "face and body, huge floppy ears, big shiny dark eyes with star highlights, a button nose, a wide TOOTHLESS cheeky "
     "grin, mismatched striped socks (one plum-and-cream, one teal-and-cream), a little cap shaped like a short unlit "
     "cream candle stub; carries a long pewter-grey candle-snuffer over his shoulder like an umbrella; tiptoeing, "
     "sneaky and giggly; soft pale rim light.",
     P("MASTER CHARACTER IMAGE. Snuffwick, a tiny, cute, teacup-sized goblin that looks like a PLUSH TOY, never scary:",
       "a round, soft sage-green (#8DBF6A) face and body, huge floppy ears, big shiny dark eyes with star highlights, a "
       "button nose and a wide TOOTHLESS cheeky grin; mismatched striped socks (one plum-and-cream, one teal-and-cream); "
       "a little cap shaped like a short, UNLIT cream candle stub.",
       "He carries a long pewter-grey candle-snuffer (a small cone on a stick) over his shoulder like an umbrella. "
       "Tiptoeing pose, sneaky and giggly. Soft pale rim light so he stands out on a dark background. No claws, no teeth, "
       "nothing gold. Keep the design SIMPLE.", SPRITE, STYLE),
     {}),
    ("snuffwick_chicken", "character", True, "MVP",
     "Edit of snuffwick_base: instead of the snuffer he hugs a big pale lemon-yellow squeaky rubber chicken with an "
     "orange beak, giggling with his eyes squeezed shut. (Trick gag 1.)",
     SN_EDIT.format(what="his arms and face: instead of the snuffer he hugs a big pale lemon-yellow (#F4EE8C) squeaky "
                         "rubber chicken with an orange beak (a classic cartoon rubber chicken) in both arms, and he "
                         "giggles with his eyes squeezed shut"),
     {"edit_of": "snuffwick_base"}),
    # ---- props (MVP; coin, gem and star are also printed as the bowl signs) ----
    ("prop_crystal", "prop", True, "MVP",
     "One pale clear faceted crystal with the shape of your real 3D-printed crystals (give ChatGPT a photo of one), "
     "nearly white with soft internal facets and a bright top highlight, no colour of its own; the builder tints it "
     "for each crystal colour (a purple-to-blue gradient for Moonspell, sky blue for Starfrost, a rainbow for "
     "Rainbow Fizz).",
     P("One pale, clear, faceted crystal shaped like the crystal in the attached photo (attach a photo of one of your "
       "3D-printed crystals), nearly white with soft internal facets and a bright top highlight. Make it colourless - "
       "pale white-grey glass, NOT the colour of the photo, and no glitter or sparkles inside. Front view, standing "
       "upright.", SPRITE, STYLE),
     {"refs": ["a photo of one of your 3D-printed crystals"]}),
    ("prop_coin", "prop", True, "MVP",
     "HOARD COIN: a big round shiny SILVER candy coin (#D9DEE8), like a chocolate coin in silver foil, a simple embossed "
     "dragon swirl in the middle, a smooth raised rim; front view, perfectly round; not gold. Also the silver-circle "
     "bowl sign.",
     P("A big, round, shiny SILVER candy coin (#D9DEE8), like a chocolate coin wrapped in silver foil, with a simple "
       "embossed little dragon swirl in the middle and a smooth raised rim. Front view, perfectly round, no text, no "
       "numbers, no face. Silver only - NOT gold.", SPRITE, STYLE),
     {}),
    ("prop_gem", "prop", True, "MVP",
     "DRAGON GEM: a big ruby-red (#FF3B47) gem cut as a clear TRIANGLE (point up), chunky glossy facets, a bright "
     "highlight; front view; no face. Also the red-triangle bowl sign.",
     P("A big RUBY-RED (#FF3B47) dragon gem cut as a clear TRIANGLE with the point up: chunky glossy facets and one "
       "bright highlight. Front view, a clear triangle silhouette, no face, no text.", SPRITE, STYLE),
     {}),
    ("prop_star", "prop", True, "MVP",
     "GOLDEN STAR: a chunky glossy gold (#FFC83D) five-point star with rounded points and a soft warm glow; no face; "
     "front view. Also the gold-star bowl sign (the host holds it). The only gold prop.",
     P("A chunky glossy GOLD (#FFC83D) five-point star with rounded points and a soft warm glow, no face, front view.",
       SPRITE, STYLE),
     {}),
    # ---- NICE ----
    ("bg_cave_lit", "background", False, "NICE",
     "Edit of bg_cave: the Hoard Lantern now BLAZES warm amber (#FFB347) with a soft warm pool of light on the nearby "
     "cave walls; the candy catches a few warm highlights; layout, lantern position and the empty centre identical. "
     "(After the finale: attract_full, moonfull, chapter.)",
     P("Use the attached bg_cave image as Image 1. Edit Image 1: make ONLY the Hoard Lantern blaze with warm amber light "
       "(#FFB347) - a bright glowing globe and a soft warm pool of light on the nearby cave walls; the candy piles catch a "
       "few warm highlights.",
       "Keep the cave, the arch, the lantern's exact position and size, the empty centre and the layout EXACTLY identical. "
       "Same style; the bottom quarter stays plain near-black."),
     {"edit_of": "bg_cave"}),
    ("butterscotch_yawn", "character", True, "NICE",
     "Edit of butterscotch_base: a big sleepy yawn - round open mouth (no teeth), eyes squeezed shut; face-only. (Sleepy "
     "crystal.)",
     BS_FACE.format(what="a big sleepy yawn - his mouth wide open in a round yawn (no teeth, just a soft pink mouth), eyes "
                         "squeezed shut, one tiny sleepy tear"),
     {"edit_of": "butterscotch_base", "anchor": [0.5, 0.62]}),
    ("butterscotch_wink", "character", True, "NICE",
     "Edit of butterscotch_base (pose edit 1 of 2): his right paw raised to his lips in a 'shh', one eye winking, a "
     "secret smile; the other paw unchanged. (Replay, keeper.)",
     BS_POSE.format(what="his RIGHT paw and his face: the right paw is raised to his lips in a 'shh' gesture, he winks one "
                         "eye, and he has a secret little smile"),
     {"edit_of": "butterscotch_base", "anchor": [0.5, 0.62]}),
    ("butterscotch_wave", "character", True, "NICE",
     "Edit of butterscotch_base (pose edit 2 of 2): his right paw raised high, waving goodbye, a big warm smile; the "
     "other paw unchanged. (Goodbye.)",
     BS_POSE.format(what="his RIGHT paw and his face: the right paw is raised high, waving goodbye, with a big warm smile "
                         "and happy eyes"),
     {"edit_of": "butterscotch_base", "anchor": [0.5, 0.62]}),
    ("snuffwick_sneeze", "character", True, "NICE",
     "Edit of snuffwick_base: dazzled by the glow, mid-sneeze - eyes squeezed shut, nose scrunched, a round puff of "
     "sparkles bursting from his nose, ears flung up. (Snuff-Out gag, circle, finale.)",
     SN_EDIT.format(what="his face and ears: dazzled by a bright light and caught mid-sneeze - eyes squeezed shut, nose "
                         "scrunched, a round puff of big simple sparkles bursting from his nose, ears flung up"),
     {"edit_of": "snuffwick_base"}),
    ("snuffwick_shy", "character", True, "NICE",
     "Edit of snuffwick_base: a shy happy smile with a tiny blush, big hopeful eyes, paws clasped in front of his tummy "
     "(the snuffer leans on his shoulder) - invited to the feast. (Finale.)",
     SN_EDIT.format(what="his face and paws: a shy, happy smile with a tiny blush, big hopeful eyes, both paws clasped "
                         "together in front of his tummy (the snuffer now leans against his shoulder)"),
     {"edit_of": "snuffwick_base"}),
    ("snuffwick_sock", "character", True, "NICE",
     "Edit of snuffwick_base: hugging a big lumpy plum-and-cream striped sock in both arms; something round inside it "
     "glows warm gold through the fabric; a sly delighted grin, one eye winking. (The 2027 stinger: chapter, finale, "
     "attract after the finale.)",
     SN_EDIT.format(what="his arms and face: instead of the snuffer he hugs a big, lumpy, plum-and-cream striped sock in "
                         "both arms; something round inside the sock glows warm gold through the fabric; a sly, "
                         "delighted grin with one eye winking"),
     {"edit_of": "snuffwick_base"}),
    ("prop_hat", "prop", True, "NICE",
     "A small crooked wizard hat in faded purple (#6E5A9E) with two big simple pale-cream stars and a soft floppy tip - "
     "the same design as Butterscotch's hat; front view; no face. (Rank 5 'Here's your hat!'.)",
     P("A small, crooked wizard hat in faded purple (#6E5A9E) with two big simple pale-cream (#F3E6C4) five-point stars and "
       "a soft floppy tip - the SAME design as the hat Butterscotch wears in the attached butterscotch_base image. Front "
       "view, no face, no character, just the hat.", SPRITE, STYLE),
     {"refs": ["butterscotch_base"]}),
    ("prop_feather", "prop", True, "NICE",
     "One long, curly tickle-feather in soft lavender with a cream tip, a big simple stylised shape (no fine strands), "
     "angled diagonally. (Trick gag 2: Snuffwick tickles Butterscotch's nose.)",
     P("One long, curly, cartoon tickle-feather in soft lavender with a cream tip: a big, simple, smooth stylised shape "
       "(NO fine strands or fluff), angled diagonally from lower right to upper left.", SPRITE, STYLE),
     {}),
    ("fx_bubble", "fx", True, "NICE",
     "A glossy round soap bubble with a soft rainbow rim and a mostly clear centre (screen-blended): hiccup bubbles and "
     "the no-crystal blessing bubble.",
     P("A glossy round soap bubble with a soft rainbow rim and a mostly clear centre. Nothing else.", SPRITE, STYLE),
     {}),
    ("fx_eyes", "fx", True, "NICE",
     "Two big round friendly goblin eyes peering out of the dark: sage-green glowing irises, big dark pupils, white star "
     "highlights, a little mischievous; ONLY the eyes. (Trick gag 3: Snuff-Out.)",
     P("Two big, round, friendly cartoon goblin eyes side by side, peering out of darkness: glowing sage-green irises, "
       "big dark pupils, bright white star highlights, slightly mischievous happy lids. ONLY the two eyes - no face, no "
       "body, nothing else. Cute, never scary.", SPRITE, STYLE),
     {}),
    ("chapter_moonstone", "background", False, "NICE",
     "Chapter still (lore): a cozy round wizard-tower room long ago; on a stone table a big round pale Moonstone has "
     "gently cracked open, and three smooth glowing crystals rest beside it - one purple fading into deep-sea blue "
     "(Moonspell), one clear sky blue (Starfrost), one in soft rainbow candy colours (Rainbow Fizz); three purple "
     "Candlewizard hats hang on pegs (no people); bottom quarter dark and empty.",
     P("Storybook wide shot, long ago: a cozy round wizard-tower room at night in deep indigo and plum, lit by a few "
       "thick cream candles. On a round stone table in the centre rests a big round pale Moonstone that has gently "
       "cracked open; beside it lie three smooth, chunky, faceted crystals, each glowing softly from inside: one purple "
       "fading into deep-sea blue, one clear sky blue, and one in soft rainbow candy colours (pink, yellow, mint and "
       "blue bands). Three friendly purple pointed wizard hats hang on pegs on the wall (no people).",
       BG, STYLE),
     {}),
    ("print_keyart", "print", False, "NICE",
     "Portrait 2:3 key art: Butterscotch floating happily with his paws around one glowing crystal, Snuffwick peeking "
     "out from behind his tail with a cheeky grin, the Hoard Lantern glowing warm amber above; generous plain dark space "
     "at the top and bottom for sign text. Parent sign + title card.",
     P("Portrait 2:3 poster art: Butterscotch (use the attached butterscotch_base image for his exact design) floating "
       "happily with his paws around one glowing crystal; Snuffwick (use the attached snuffwick_base image) peeking out "
       "from behind Butterscotch's tail with a cheeky grin; the Hoard Lantern glowing warm amber above them.",
       "Generous plain dark space at the top and the bottom for sign text. Dark midnight-indigo cave background.", STYLE),
     {"refs": ["butterscotch_base", "snuffwick_base"]}),
]

# Extra "used by" notes that are not clips (printables)
USED_EXTRA = {
    "prop_coin": ["bowl sign (silver circle)"],
    "prop_gem": ["bowl sign (red triangle)"],
    "prop_star": ["bowl sign (gold star)"],
    "print_keyart": ["parent sign", "title card"],
}

# --------------------------------------------------------------------------- clip helpers
BS = {"height": 300, "x": 420, "y": 40}          # Butterscotch: centred, top y 40, bottom ~340
TIPTOE = {"amp": 4, "period": 0.4}                # Snuffwick's scamper / tiptoe bounce


def bs(sprite, t=None, bob=(7, 3.0), fin=0.25, fout=0.25, **kw):
    """Butterscotch at his home spot (balloon bob)."""
    layer = {"sprite": sprite, **BS, "bob": {"amp": bob[0], "period": bob[1]}, "fade_in": fin, "fade_out": fout}
    if t:
        layer["t"] = t
    layer.update(kw)
    return layer


def bs_side(sprite, t=None, x=625, height=230, y=45, bob=(6, 3.0), **kw):
    """Butterscotch smaller, to the right (scenes where the centre belongs to the crystal / gem / counter)."""
    layer = {"sprite": sprite, "height": height, "x": x, "y": y, "bob": {"amp": bob[0], "period": bob[1]}}
    if t:
        layer["t"] = t
    layer.update(kw)
    return layer


def sn(sprite, x, y, t, height=115, hop=True, **kw):
    """Snuffwick on the cave floor (top y ~225 -> bottom ~340)."""
    layer = {"sprite": sprite, "height": height, "x": x, "y": y, "t": t, "fade_in": 0.1, "fade_out": 0.1}
    if hop:
        layer["bob"] = dict(TIPTOE)
    layer.update(kw)
    return layer


def bubble(h, x, y, t, **kw):
    layer = {"sprite": "fx_bubble", "height": h, "x": x, "y": y, "t": t, "fade_in": 0.1, "fade_out": 0.3,
             "blend": "screen", "optional": True}
    layer.update(kw)
    return layer


WOBBLE = [0, -6, 6, -6, 0]

# The crystal colours (words color.N / color.N_hex; N = the colour id the bench writes to the tag, page 17 byte 1).
# The crystals are 3D-printed in PLA; one filament per colour. A colour is a colour SPEC (manifest.schema.md):
# "#RRGGBB", 2-4 stops "#RRGGBB,#RRGGBB" (the device cycles them, ~4 s; the awaken tint paints them as a gradient)
# or "rainbow". Ids 4..6 are free for future filaments: add the name + spec here and an awaken_b_<id> line below.
CRYSTAL_COLORS = [
    # (id, name, spec, filament)
    (1, "Moonspell", "#A35CFF,#2F7BFF", "VOXELAB PLA Crystal Rainbow 'Deep Sea Realm' (alternating transparent purple "
                                        "and blue; every crystal differs)"),
    (2, "Starfrost", "#5CD6FF", "PLA HS Glitter, Clear Sky Blue"),
    (3, "Rainbow Fizz", "rainbow", "FLASHFORGE Translucent Glitter PLA, Crystal 'Rainbow Candy' (multi-colour gradient)"),
]
CRYSTAL_SPECS = [spec for _, _, spec, _ in CRYSTAL_COLORS]

CLIPS = [
    # ---------------------------------------------------------------- idle
    {"id": "attract", "priority": "MVP", "recipe": "layers", "seconds": 12, "loop": True, "bg": {"image": "bg_cave"},
     "layers": [bs("butterscotch_sleep", t=[0, 8.75], bob=(8, 4), fin=0),
                bs("butterscotch_peek", t=[8.5, 11.75], bob=(8, 4)),
                bs("butterscotch_sleep", t=[11.5, 12], bob=(8, 4), fout=0),
                sn("snuffwick_base", x=[880, 690], y=225, t=[4.0, 5.4]),
                sn("snuffwick_base", x=690, y=225, t=[5.3, 6.6], hop=False, rotate=WOBBLE),
                sn("snuffwick_base", x=[690, 900], y=225, t=[6.5, 7.6])],
     "anchor_layer": 0,
     "notes": "Butterscotch dozes (balloon bob) and peeks one eye open; Snuffwick tiptoes in from the right, giggles "
              "(sfx 'munch' at 5.4 s) and scampers off. The device draws the Hoard Lantern's glow (upper-left), the "
              "spark counter and the place-crystal hints."},
    {"id": "attract_full", "priority": "NICE", "recipe": "layers", "seconds": 12, "loop": True,
     "bg": {"image": "bg_cave_lit", "fallback": "bg_cave"},
     "layers": [bs("butterscotch_sleep", t=[0, 8.75], bob=(8, 4), fin=0),
                bs("butterscotch_delight", t=[8.5, 11.75], bob=(8, 4), fallback="butterscotch_peek"),
                bs("butterscotch_sleep", t=[11.5, 12], bob=(8, 4), fout=0),
                sn("snuffwick_sock", x=[880, 690], y=225, t=[3.5, 5.0], optional=True, fallback="snuffwick_base"),
                sn("snuffwick_sock", x=690, y=225, t=[4.9, 6.4], hop=False, rotate=WOBBLE, optional=True,
                   fallback="snuffwick_base"),
                sn("snuffwick_sock", x=[690, 900], y=225, t=[6.3, 7.4], optional=True, fallback="snuffwick_base")],
     "anchor_layer": 0,
     "notes": "After the lantern is lit: the cave glows; Butterscotch dozes happily; Snuffwick, now the Official "
              "Prankster, scampers by hugging his glowing sock (the 2027 tease)."},
    # ---------------------------------------------------------------- a crystal arrives
    {"id": "absorb", "priority": "MVP", "recipe": "layers", "seconds": 1, "loop": False, "bg": {"image": "bg_cave"},
     "layers": [bs("butterscotch_peek", t=[0, 0.3], fin=0, fout=0.12),
                bs("butterscotch_surprise", t=[0.18, 1], fin=0.12, fout=0)],
     "anchor_layer": 1,
     "notes": "One eye pops open, then surprise. The device draws the beam of crystal-coloured light into his chest "
              "(the anchor) and the colour name."},
    {"id": "awaken", "priority": "MVP", "recipe": "layers", "seconds": 8, "loop": False, "variants": "colors",
     "bg": {"image": "bg_wakestone", "zoom": [1.0, 1.05]},
     "layers": [{"sprite": "prop_crystal", "tint": "$color", "height": [80, 130, 130], "x": 400, "y": [300, 175, 165],
                 "t": [0, 8], "fade_in": 0.4, "bob": {"amp": 4, "period": 2}},
                bs_side("butterscotch_surprise", t=[0.4, 3.2], y=[-280, 45], fade_in=0.2, fade_out=0.25),
                bs_side("butterscotch_delight", t=[3.0, 8], fade_in=0.25)],
     "anchor_layer": 2,
     "notes": "One colour variant per crystal colour, awaken_1..awaken_%d (the crystal tinted with color.N_hex: "
              "%s), rises from the Wakestone; Butterscotch floats down (surprise), then beams (his chest glow starts "
              "at 3.0 s). The device adds sparkles + the meter."
              % (len(CRYSTAL_COLORS), ", ".join("%d %s" % (i, n) for i, n, _, _ in CRYSTAL_COLORS))},
    {"id": "greet", "priority": "MVP", "recipe": "layers", "seconds": 4, "loop": True, "bg": {"image": "bg_cave"},
     "layers": [bs("butterscotch_delight", bob=(7, 4), fin=0, fout=0)], "anchor_layer": 0,
     "notes": "One visual for every rank (except the Wizard hat below); the device draws the rank name and the meter."},
    {"id": "greet_r5", "priority": "NICE", "recipe": "layers", "seconds": 3.5, "loop": False, "bg": {"image": "bg_cave"},
     "layers": [bs("butterscotch_delight", bob=(7, 3.5), fin=0, fout=0),
                {"sprite": "prop_hat", "height": [30, 105, 95], "x": [440, 600, 625], "y": [210, 40, 55],
                 "t": [0.35, 3.5], "fade_in": 0.15, "spin": 1, "bob": {"amp": 4, "period": 1.75}}],
     "anchor_layer": 0,
     "notes": "Rank 5 Wizard: 'Here's your hat!' - a little wizard hat pops out of Butterscotch's paws and twirls up "
              "beside him (upper right; it never lands on the device-drawn crystal)."},
    {"id": "charge", "priority": "MVP", "recipe": "layers", "seconds": 4, "loop": True, "bg": {"image": "bg_cave"},
     "layers": [bs("butterscotch_base", bob=(7, 4), fin=0, fout=0)], "anchor_layer": 0,
     "notes": "The device draws the tap prompt (TAP! TAP!; the button prompt when a plinth button is fitted), the "
              "fill bar and sparkles; his chest glow swells with each tap on the window."},
    # ---------------------------------------------------------------- trick gags (GOBLIN TRICK!)
    {"id": "reveal_t0_1", "priority": "MVP", "recipe": "layers", "seconds": 2.5, "loop": False, "bg": {"image": "bg_cave"},
     "layers": [bs("butterscotch_surprise", t=[0, 0.55], fin=0.15, fout=0.12),
                bs("butterscotch_giggle", t=[0.45, 2.5], fin=0.12, fout=0),
                sn("snuffwick_chicken", x=[900, 645], y=[215, 220], t=[0, 0.45], fade_in=0),
                sn("snuffwick_chicken", x=645, y=220, t=[0.4, 1.2], hop=False, rotate=[0, -8, 8, -8, 0]),
                {"sprite": "prop_coin", "height": [20, 90, 75], "x": [615, 600, 560], "y": [255, 170, 185],
                 "t": [1.12, 2.5], "fade_in": 0.1, "spin": 2},
                sn("snuffwick_base", x=[645, 900], y=[220, 215], t=[1.15, 1.9])],
     "anchor_layer": 0,
     "notes": "Gag 1 Rubber Chicken: Snuffwick zips in hugging a squeaky rubber chicken, Butterscotch pulls a silly face, "
              "poof - it's a hoard coin! Snuffwick giggles off. (Device: a white poof at about (615, 230) at 1.1 s.)"},
    {"id": "reveal_t0_2", "priority": "MVP", "recipe": "layers", "seconds": 2.5, "loop": False, "bg": {"image": "bg_cave"},
     "layers": [sn("snuffwick_base", x=578, y=[235, 112, 112, 112, 235], t=[0, 1.3], hop=False, fade_in=0.08,
                   fade_out=0.15),
                bs("butterscotch_base", t=[0, 0.6], fin=0.15, fout=0.08),
                bs("butterscotch_puff", t=[0.55, 0.95], fin=0.06, fout=0.06, y=[40, 28, 40]),
                bs("butterscotch_giggle", t=[0.9, 1.4], fin=0.06, fout=0.06),
                bs("butterscotch_puff", t=[1.35, 1.75], fin=0.06, fout=0.06, y=[40, 28, 40]),
                bs("butterscotch_giggle", t=[1.7, 2.5], fin=0.06, fout=0),
                {"sprite": "prop_feather", "height": 90, "x": 500, "y": 100, "t": [0.3, 1.2], "fade_in": 0.1,
                 "fade_out": 0.1, "rotate": [-20, 15, -20, 15, -20, 15], "optional": True},
                bubble([20, 60], [450, 520], [170, 60], [0.65, 1.6]),
                bubble([15, 45], [430, 360], [175, 70], [0.75, 1.7]),
                bubble([20, 70], [445, 560], [170, 40], [1.45, 2.5]),
                bubble([15, 50], [425, 340], [175, 50], [1.55, 2.5])],
     "anchor_layer": 1,
     "notes": "Gag 2 Hiccup Bubbles: Snuffwick pops up behind Butterscotch's shoulder and tickles his nose with a "
              "feather; hic! hic! - bubbles float up. (Device: sparkles in the kid's crystal colour from his mouth.)"},
    {"id": "reveal_t0_3", "priority": "MVP", "recipe": "layers", "seconds": 2.5, "loop": False,
     "layers": [{"image": "bg_cave", "t": [0, 0.72], "fade_out": 0.07},
                {"image": "bg_cave", "t": [1.42, 2.5], "fade_in": 0.06},
                bs("butterscotch_surprise", t=[0, 0.72], fin=0.15, fout=0.07),
                bs("butterscotch_puff", t=[1.42, 1.85], fin=0.04, fout=0.08, y=[28, 44, 40]),
                bs("butterscotch_giggle", t=[1.8, 2.5], fin=0.08, fout=0),
                sn("snuffwick_base", x=[900, 650], y=225, t=[0, 0.5], fade_in=0),
                sn("snuffwick_base", x=650, y=[225, 212, 225], t=[0.48, 0.72], hop=False, fade_in=0.02, fade_out=0.07),
                {"sprite": "fx_eyes", "height": 44, "x": 655, "y": 248, "t": [0.8, 1.1], "fade_in": 0.02,
                 "fade_out": 0.02, "optional": True},
                {"sprite": "fx_eyes", "height": 44, "x": 655, "y": 248, "t": [1.17, 1.41], "fade_in": 0.02,
                 "fade_out": 0.02, "optional": True},
                sn("snuffwick_sneeze", x=[650, 900], y=[225, 205], t=[1.42, 2.15], hop=False, rotate=[0, -10, 10, 0],
                   fallback="snuffwick_base")],
     "anchor_layer": 2,
     "notes": "Gag 3 Snuff-Out: Snuffwick tiptoes in, 'pfft!' - the cave goes dark (no bg = black; only his eyes "
              "blink, and the device's lantern sparks + Butterscotch's tummy glow stay on: the kids' light can't be "
              "snuffed), Butterscotch sneezes sparkles at 1.4 s and the lights pop back on; Snuffwick sneezes and "
              "scampers off."},
    # ---------------------------------------------------------------- results
    {"id": "reveal_t1", "priority": "MVP", "recipe": "layers", "seconds": 2.5, "loop": False, "bg": {"image": "bg_cave"},
     "layers": [bs("butterscotch_delight", fin=0.15, fout=0),
                {"sprite": "prop_coin", "height": [40, 110, 100], "x": [420, 605, 615], "y": [215, 85, 95],
                 "t": [0.3, 2.5], "fade_in": 0.15, "spin": 2, "bob": {"amp": 3, "period": 1.25}}],
     "anchor_layer": 0,
     "notes": "HOARD COIN: the silver coin flips up out of his paws and spins (upper right, clear of the lantern)."},
    {"id": "reveal_t2", "priority": "MVP", "recipe": "layers", "seconds": 2.5, "loop": False,
     "bg": {"image": "bg_wakestone"},
     "layers": [bs_side("butterscotch_surprise", fade_in=0.15, bob=(6, 2.5)),
                {"sprite": "prop_gem", "height": [50, 150, 140], "x": 400, "y": [285, 90, 100], "t": [0.2, 2.5],
                 "fade_in": 0.15, "spin": 1}],
     "anchor_layer": 0,
     "notes": "DRAGON GEM: the ruby triangle rises out of the Wakestone and turns once; Butterscotch gasps."},
    {"id": "reveal_t2_2", "priority": "NICE", "recipe": "layers", "seconds": 2.5, "loop": False,
     "bg": {"image": "bg_wakestone", "zoom": [1.0, 1.06]},
     "layers": [bs_side("butterscotch_surprise", t=[0, 1.25], fade_in=0.15, fade_out=0.2, bob=(6, 2.5)),
                bs_side("butterscotch_delight", t=[1.1, 2.5], fade_in=0.2, bob=(6, 2.5)),
                {"sprite": "prop_gem", "height": [50, 150, 140], "x": 400, "y": [285, 90, 100], "t": [0.1, 2.5],
                 "fade_in": 0.15, "rotate": [-8, 8, -8, 0]}],
     "anchor_layer": 0,
     "notes": "Variant 2 (with reveal_t2_2's line): the gem wobbles up and Butterscotch's gasp turns into a beam."},
    {"id": "reveal_t3", "priority": "MVP", "recipe": "layers", "seconds": 3, "loop": False,
     "bg": {"image": "bg_starburst", "rotate": [0, 15]},
     "layers": [{"sprite": "prop_star", "height": [70, 240], "x": 380, "y": [300, 60], "t": [0, 3], "fade_in": 0.3,
                 "bob": {"amp": 4, "period": 1.5}},
                bs_side("butterscotch_delight", t=[0.5, 3], x=655, height=200, y=85, fade_in=0.3)],
     "anchor_layer": 1,
     "notes": "GOLDEN STAR: the gold rays turn slowly, the star rises and grows (no strobe); Butterscotch beams beside it."},
    # ---------------------------------------------------------------- other paths
    {"id": "replay", "priority": "MVP", "recipe": "layers", "seconds": 4, "loop": True, "bg": {"image": "bg_cave"},
     "layers": [bs("butterscotch_wink", bob=(7, 4), fin=0, fout=0, fallback="butterscotch_peek")], "anchor_layer": 0,
     "notes": "Already played tonight: a 'shh' wink (one-eye peek until the wink still exists); the device shows "
              "tonight's result dimmed."},
    {"id": "nocrystal", "priority": "MVP", "recipe": "layers", "seconds": 3.5, "loop": False, "bg": {"image": "bg_cave"},
     "layers": [bs("butterscotch_puff", t=[0, 0.9], fin=0.15, fout=0.15),
                bs("butterscotch_delight", t=[0.8, 3.5], fin=0.15, fout=0),
                bubble([30, 95, 85], [440, 330, 400], [150, 210, 255], [0.9, 3.4], fade_in=0.2, fade_out=0.35,
                       bob={"amp": 3, "period": 1.0})],
     "anchor_layer": 0,
     "notes": "The dragon blessing: Butterscotch puffs his cheeks and blows a sparkly blessing bubble down to the kid "
              "(+1 spark for the lantern)."},
    {"id": "sleepy", "priority": "MVP", "recipe": "layers", "seconds": 4, "loop": True, "bg": {"image": "bg_cave"},
     "layers": [bs("butterscotch_yawn", bob=(7, 4), fin=0, fout=0, fallback="butterscotch_sleep")], "anchor_layer": 0,
     "notes": "Unreadable crystal: a big yawn (the sleeping face until the yawn still exists)."},
    {"id": "goodbye", "priority": "MVP", "recipe": "layers", "seconds": 1.6, "loop": False, "bg": {"image": "bg_cave"},
     "layers": [{"sprite": "butterscotch_wave", "height": [300, 170], "x": [420, 470], "y": [40, -40], "t": [0, 1.6],
                 "fade_out": 0.5, "fallback": "butterscotch_delight"}],
     "anchor_layer": 0,
     "notes": "He waves and floats up like a balloon (delighted face until the wave still exists)."},
    {"id": "milestone", "priority": "MVP", "recipe": "layers", "seconds": 3, "loop": False,
     "bg": {"image": "bg_starburst", "rotate": [0, 15]},
     "layers": [bs_side("butterscotch_delight", x=640, fade_in=0.2)], "anchor_layer": 0,
     "notes": "Every 100th crystal: the device adds confetti in the crystal colours + the big station counter in "
              "the centre (so Butterscotch sits to the right)."},
    {"id": "circle", "priority": "MVP", "recipe": "layers", "seconds": 2.5, "loop": False,
     "bg": {"image": "bg_wakestone"},
     "layers": [bs("butterscotch_giggle", fin=0.15, fout=0),
                sn("snuffwick_sneeze", x=[880, 715], y=225, t=[0.8, 2.5], optional=True)],
     "anchor_layer": 0,
     "notes": "3+ crystals in one group: the device orbits small crystals in the group's colours around Butterscotch; "
              "Snuffwick peeks in and sneezes at all that glow."},
    {"id": "keeper", "priority": "NICE", "recipe": "layers", "seconds": 2.5, "loop": False,
     "bg": {"image": "bg_cave", "zoom": [1.0, 1.15]},
     "layers": [{"sprite": "butterscotch_wink", "height": [300, 350], "x": 420, "y": [40, 5], "t": [0, 2.5],
                 "fade_in": 0.2, "fallback": "butterscotch_peek"}],
     "anchor_layer": 0,
     "notes": "Keeper of the Hoard secret (about 1 visit in 3): a slow push-in on the wink - the only close-up."},
    # ---------------------------------------------------------------- the finale (once per night)
    {"id": "moonfull", "priority": "NICE", "recipe": "layers", "seconds": 7, "loop": False, "bg": {"image": "bg_cave"},
     "layers": [{"image": "bg_cave_lit", "t": [2.0, 7], "fade_in": 1.0, "optional": True},
                bs("butterscotch_delight", t=[0, 4.0], fin=0.25),
                bs("butterscotch_giggle", t=[3.8, 7], fin=0.25, fout=0),
                sn("snuffwick_sneeze", x=[880, 650, 640], y=[210, 225, 225], t=[0.6, 2.9], hop=False,
                   rotate=[0, -10, 10, -10, 0], fallback="snuffwick_base"),
                sn("snuffwick_shy", x=640, y=225, t=[2.8, 5.4], hop=False, fallback="snuffwick_base"),
                sn("snuffwick_sock", x=[640, 900], y=[225, 215], t=[5.3, 6.6], optional=True)],
     "anchor_layer": 1,
     "notes": "The Hoard Lantern blazes (the lit cave fades in); Snuffwick, dazzled, sneezes, then smiles shyly - "
              "invited to the feast as Official Prankster - and scampers off hugging his glowing sock (2027 stinger)."},
    # ---------------------------------------------------------------- chapter 1 (idle rotation)
    {"id": "chapter_a", "priority": "NICE", "recipe": "layers", "seconds": 4.5, "loop": False,
     "bg": {"image": "bg_cave_lit", "fallback": "bg_cave"},
     "layers": [bs("butterscotch_sleep", t=[0, 1.6], fin=0.3, fout=0.3),
                bs("butterscotch_delight", t=[1.4, 4.5], fin=0.3, fout=0.3)],
     "anchor_layer": 0,
     "notes": "Every Halloween he wakes under the lit lantern to share the hoard."},
    {"id": "chapter_b", "priority": "NICE", "recipe": "layers", "seconds": 5, "loop": False,
     "bg": {"image": "bg_cave_lit", "fallback": "bg_cave"},
     "layers": [{"image": "bg_cave", "t": [2.45, 5], "fade_in": 0.35},
                {"sprite": "butterscotch_sleep", "height": 300, "x": 440, "y": 40, "bob": {"amp": 8, "period": 4},
                 "fade_in": 0.3},
                sn("snuffwick_base", x=[-70, 100, 150], y=[225, 190, 160], t=[0.3, 2.3]),
                sn("snuffwick_base", x=150, y=[160, 148, 160], t=[2.25, 2.7], hop=False),
                sn("snuffwick_base", x=[150, -80], y=[160, 225], t=[2.65, 3.7])],
     "notes": "Snuffwick climbs the candy pile under the lantern, 'pfft!' - the lit cave goes dark (the device does not "
              "draw the lantern glow in this part)."},
    {"id": "chapter_c", "priority": "NICE", "recipe": "kenburns", "image": "chapter_moonstone", "seconds": 4.5,
     "zoom": [1.05, 1.12], "pan": "up",
     "notes": "Lore: the Candlewizards' Moonstone cracked into the crystals."},
    {"id": "chapter_d", "priority": "NICE", "recipe": "layers", "seconds": 4.5, "loop": False,
     "bg": {"image": "bg_wakestone"},
     # One crystal per colour, each fading in OVER the previous one (which stays opaque until it is covered, so the
     # crystal never turns see-through mid-change); the first fades in from the empty stone.
     "layers": [{"sprite": "prop_crystal", "tint": c, "height": 130, "x": 400, "y": 165,
                 "t": [round(i * 4.5 / len(CRYSTAL_SPECS), 3),
                       4.5 if i == len(CRYSTAL_SPECS) - 1 else round((i + 1) * 4.5 / len(CRYSTAL_SPECS) + 0.25, 3)],
                 "fade_in": 0.4 if i == 0 else 0.25, "bob": {"amp": 4, "period": 2}}
                for i, c in enumerate(CRYSTAL_SPECS)],
     "notes": "The crystal on the Wakestone changes through every crystal colour (%s)."
              % ", ".join(n for _, n, _, _ in CRYSTAL_COLORS)},
    {"id": "chapter_e", "priority": "NICE", "recipe": "layers", "seconds": 4.5, "loop": False, "bg": {"image": "bg_cave"},
     "layers": [{"image": "bg_cave_lit", "t": [1.2, 4.5], "fade_in": 2.4, "optional": True},
                bs("butterscotch_peek", t=[0, 1.9], fin=0.3),
                bs("butterscotch_delight", t=[1.7, 4.5], fout=0.3)],
     "anchor_layer": 1,
     "notes": "Sparks light the lantern (the lit cave fades in) and Butterscotch wakes up delighted. The device draws "
              "tonight's lantern glow over it (meter.on)."},
    {"id": "chapter_f", "priority": "NICE", "recipe": "layers", "seconds": 5, "loop": False, "bg": {"image": "bg_cave"},
     "layers": [bs("butterscotch_peek", fin=0.3, fout=0.3),
                sn("snuffwick_sock", x=[900, -100], y=222, t=[0.8, 4.4], fallback="snuffwick_base")],
     "anchor_layer": 0,
     "notes": "The cliffhanger: Snuffwick scampers past hugging a lumpy sock that glows gold inside."},
]

# --------------------------------------------------------------------------- voice
# (group, n, speaker, text, caption, delivery, priority)   speaker "Snuffwick" -> fx "smudge"
B, S = "Butterscotch", "Snuffwick"
VOICE = [
    ("attract_call", 1, B, "Crystal on the Wakestone! Wake me up!", "CRYSTAL HERE",
     "sleepy rumble that brightens on 'Wake me up'; cozy, not loud", "MVP"),
    ("attract_call", 2, B, "No crystal? Press the big button!", "OR PRESS",
     "friendly and clear, a touch slower on 'big button' (only plays when a plinth button is fitted, config.ini "
     "button = yes; without one attract_tap_1 plays instead)", "NICE"),
    ("attract_call", 3, B, "Zzz... a goblin snuffed my lantern... zzz.", "HELP BUTTERSCOTCH!",
     "half-asleep mumble with a soft snore; playful, never sad", "MVP"),
    ("attract_call", 4, B, "Every crystal adds a spark to my lantern!", "LIGHT THE LANTERN",
     "warm storyteller, hopeful lift at the end", "MVP"),
    ("attract_call", 5, B, "Shh... I hear wizards coming...", "SHHH...",
     "excited secret whisper, smiling", "MVP"),
    ("attract_call", 6, B, "Wake me, wizards! My candy hoard is waiting!", "WAKE ME!",
     "bright inviting call across the porch, a happy yawn after 'wizards'", "MVP"),
    ("attract_call", 7, B, "Set your crystal here! I'm awake... almost!", "SET IT DOWN",
     "yawny and cozy, a chuckle on 'almost'", "MVP"),
    ("attract_call", 8, S, "Pfft! Hee hee! I like the dark!", "PFFT!",
     "Snuffwick: record in Butterscotch's voice - the builder pitches it up 5 semitones at 1.15x; a tiny shy "
     "giggle, mischievous, never mean", "NICE"),
    # no big button (button = no, the 2026 default): plays instead of attract_call_2 (words nobutton.voice)
    ("attract_tap", 1, B, "No crystal? Tap my window!", "OR TAP ME",
     "friendly and clear, a touch slower on 'tap my window' (the no-crystal kids tap the screen for a dragon "
     "blessing; plays instead of attract_call_2)", "MVP"),
    ("awaken_a", 1, B, "Ooh! A brand-new crystal is waking!", "NEW CRYSTAL!",
     "sleepy gasp of wonder that wakes right up", "MVP"),
    ("awaken_a", 2, B, "A new crystal! Hello, little light!", "HELLO!",
     "tender delight, like greeting a baby bird", "MVP"),
    # awaken_b_<n> names crystal colour n (CRYSTAL_COLORS): the name first, then what it looks like, then its gift.
    ("awaken_b", 1, B, "Moonspell! Purple and blue, full of dreams!", "MOONSPELL",
     "proud announcement of the name, then dreamy and soft on 'full of dreams' (colour ID 1: the purple-and-blue "
     "Deep Sea Realm crystals - some look mostly purple, some mostly blue)", "MVP"),
    ("awaken_b", 2, B, "Starfrost! Sparkly sky blue, full of wonder!", "STARFROST",
     "proud, then hushed amazement on 'wonder' (colour ID 2: the glittery sky-blue crystals)", "MVP"),
    ("awaken_b", 3, B, "Rainbow Fizz! Every colour, full of giggles!", "RAINBOW FIZZ",
     "proud and bubbly, then a giggle on 'giggles' (colour ID 3: the rainbow-candy glitter crystals)", "MVP"),
    ("awaken_c", 1, B, "Sparkfinder! Welcome, little wizard!", "SPARKFINDER!",
     "joyful welcome, bright on 'Sparkfinder' (also the rush-mode line, so the rank comes first)", "MVP"),
    ("awaken_c", 2, B, "Sparkfinder! Look, two sparks already!", "SPARKFINDER!",
     "delighted, impressed, quick", "MVP"),
    ("greet_r2", 1, B, "Apprentice! You came back, wizard!", "APPRENTICE", "thrilled reunion, a warm chuckle", "MVP"),
    ("greet_r2", 2, B, "Apprentice! Your crystal remembers you!", "APPRENTICE", "tender and proud", "NICE"),
    ("greet_r3", 1, B, "Glowcaster! Your glow is getting big!", "GLOWCASTER", "proud announcement, rising", "MVP"),
    ("greet_r3", 2, B, "Glowcaster! Welcome back! Shine on!", "GLOWCASTER", "cozy and admiring", "NICE"),
    ("greet_r4", 1, B, "Hoardguard! My trusty candy protector!", "HOARDGUARD",
     "proud and a little grand, like knighting someone", "MVP"),
    ("greet_r4", 2, B, "Hoardguard! Keep those goblins away!", "HOARDGUARD",
     "playful-conspiratorial, a wink in the voice", "NICE"),
    ("greet_r5", 1, B, "Wizard! A true Wizard! Here's your hat!", "WIZARD",
     "awed and proud, a magic-trick flourish on 'Here's your hat!' (the hat pops out on screen)", "MVP"),
    ("greet_r5", 2, B, "Wizard now! Here's your very own hat!", "WIZARD",
     "warm and delighted, presenting a gift", "NICE"),
    ("greet_r6", 1, B, "Dragon Rider! Climb aboard, old friend!", "DRAGON RIDER", "excited, adventurous, warm", "MVP"),
    ("greet_r6", 2, B, "Dragon Rider! You're back! Wheee!", "DRAGON RIDER", "giddy, a floaty 'Wheee'", "NICE"),
    ("greet_r7", 1, B, "Keeper of the Hoard! I bow to you!", "KEEPER OF THE HOARD",
     "grand and reverent, but smiling", "MVP"),
    ("greet_r7", 2, B, "Keeper! The whole hoard rejoices!", "KEEPER OF THE HOARD",
     "deeply warm, like hugging an old friend", "NICE"),
    # charge: with a plinth button (button = yes) the charge lines play; without one (the 2026 default) the
    # charge_tap lines play instead (words nobutton.voice "charge=charge_tap")
    ("charge", 1, B, "Press the button! Pump up the glow!", "PRESS!",
     "urgent-fun cheer, quick (only with a plinth button, button = yes)", "NICE"),
    ("charge", 2, B, "Press fast! Fill your crystal!", "PRESS!",
     "bouncy encouragement (only with a plinth button, button = yes)", "NICE"),
    ("charge_tap", 1, B, "Tap the window! Pump up the glow!", "TAP! TAP!",
     "urgent-fun cheer, quick (kids tap the screen to fill the glow; plays instead of the charge lines)", "MVP"),
    ("charge_tap", 2, B, "Tap tap tap! Fill your crystal!", "TAP! TAP!",
     "bouncy encouragement, three quick light taps in the voice (plays instead of the charge lines)", "MVP"),
    ("reveal_t0", 1, B, "A rubber chicken?! Poof! Candy anyway!", "GOBLIN TRICK!",
     "gag 1 Rubber Chicken: silly disbelief, a big laugh, then reassuring and warm on 'Candy anyway!'", "MVP"),
    ("reveal_t0", 2, B, "Hic! Snuffwick tickled me! Hic! Candy anyway!", "GOBLIN TRICK!",
     "gag 2 Hiccup Bubbles: real little hiccups, giggling; reassuring on 'Candy anyway!'", "MVP"),
    ("reveal_t0", 3, B, "Lights out?! Ah... ah... CHOO! Candy anyway!", "GOBLIN TRICK!",
     "gag 3 Snuff-Out: startled-silly in the dark, a big sparkly sneeze about 1.4 s in (the lights pop back on), "
     "then a happy chuckle on 'Candy anyway!'", "MVP"),
    ("reveal_t1", 1, B, "Hoard coin! A big shiny one!", "HOARD COIN!", "happy, admiring, warm", "MVP"),
    ("reveal_t1", 2, B, "Hoard coin! Shiny silver treasure!", "HOARD COIN!", "sweet and pleased, a little chuckle", "MVP"),
    ("reveal_t2", 1, B, "Dragon Gem! Whoa, a rare one!", "DRAGON GEM!",
     "genuinely surprised, an excited whisper-shout", "MVP"),
    ("reveal_t2", 2, B, "Dragon Gem! Not many wizards find those!", "DRAGON GEM!", "thrilled, conspiratorial", "MVP"),
    ("reveal_t3", 1, B, "The Golden Star! You're legendary!", "GOLDEN STAR!", "awed hush rising to joy", "MVP"),
    ("reveal_t3", 2, B, "Golden Star! The rarest treasure of all!", "GOLDEN STAR!",
     "breathless wonder, slow and grand", "MVP"),
    ("bowl_common", 1, B, "Silver circle! Take one treat!", "SILVER CIRCLE",
     "clear and friendly, crisp on 'Silver circle'", "MVP"),
    ("bowl_rare", 1, B, "Red triangle! Take one treat!", "RED TRIANGLE",
     "clear, excited, crisp on 'Red triangle'", "MVP"),
    ("bowl_legend", 1, B, "Gold star! The grown-up has it!", "GOLD STAR", "celebratory, pointing-out tone", "MVP"),
    ("replay", 1, B, "Shh! Your crystal already glowed tonight!", "ALREADY GLOWED", "secretive and kind, smiling", "MVP"),
    ("replay", 2, B, "Still glowing from tonight! Clever crystal!", "ALREADY GLOWED",
     "gentle and proud, no scolding", "MVP"),
    ("nocrystal", 1, B, "A dragon blessing! Whoosh, you sparkle!", "DRAGON BLESSING!",
     "generous and cheery, a soft 'whoosh' like blowing a bubble", "MVP"),
    ("nocrystal", 2, B, "Dragon blessing! You added a spark too!", "DRAGON BLESSING!",
     "warm thank-you, inclusive", "MVP"),
    ("sleepy", 1, B, "Again, please! Sleepy crystal.", "AGAIN, PLEASE", "soft and patient, a yawn in it", "MVP"),
    ("sleepy", 2, B, "Place it again! Your crystal's yawning.", "AGAIN, PLEASE", "giggly and reassuring", "MVP"),
    ("goodbye", 1, B, "Bring your crystal back next Halloween!", "SEE YOU 2027", "warm send-off, bright", "MVP"),
    ("goodbye", 2, B, "Keep it safe, wizard! See you next year!", "SEE YOU 2027",
     "fond, a sleepy-happy sigh, smiling", "MVP"),
    # ---- nice to have ----
    ("recall_t0", 1, B, "Last year, Snuffwick tricked you! Silly goblin!", "LAST YEAR",
     "remembering fondly, a chuckle", "NICE"),
    ("recall_t1", 1, B, "Last year, a shiny hoard coin!", "LAST YEAR", "fond memory, warm", "NICE"),
    ("recall_t2", 1, B, "Last year, a Dragon Gem! Remember?", "LAST YEAR", "impressed memory", "NICE"),
    ("recall_t3", 1, B, "Last year, the Golden Star! Legendary!", "LAST YEAR", "awed remembering", "NICE"),
    ("rush_t0", 1, B, "Silver circle! Goblin trick!", "GOBLIN TRICK!", "fast, giggly, bowl words crisp (rush mode)", "NICE"),
    ("rush_t1", 1, B, "Silver circle! Hoard coin!", "HOARD COIN!", "fast and bright (rush mode)", "NICE"),
    ("rush_t2", 1, B, "Red triangle! Dragon Gem!", "DRAGON GEM!", "fast, excited (rush mode)", "NICE"),
    ("rush_t3", 1, B, "Gold star! Golden Star!", "GOLDEN STAR!", "fast but awed (rush mode)", "NICE"),
    ("milestone", 1, B, "Another hundred crystals! Hooray, wizards!", "HOORAY, WIZARDS!", "party cheer, bubbly", "NICE"),
    ("circle", 1, B, "A crystal circle! Wizards together!", "WIZARD CIRCLE!",
     "delighted, gathering-everyone warmth", "NICE"),
    ("circle", 2, S, "Eek! Too much glow! Hee hee!", "EEK!",
     "Snuffwick (pitched up by the builder): a dazzled squeak, then a giggle", "NICE"),
    ("keeper", 1, B, "Psst, Keeper... my real name's Bartholomew.", "SHH... SECRET",
     "close secret whisper, barely holding a chuckle", "NICE"),
    ("keeper", 2, B, "Shh... my wings are tiny. I just float!", "SHH... SECRET",
     "embarrassed confiding whisper, then a tiny giggle", "NICE"),
    ("moonfull_a", 1, B, "My lantern is lit! You did it, wizards!", "LANTERN LIT!",
     "overjoyed triumph, the biggest moment of the night - warm, never booming", "NICE"),
    ("moonfull_b", 1, B, "Silly Snuffwick... come to the feast! You're my Official Prankster!", "OFFICIAL PRANKSTER",
     "fond forgiving chuckle, then a proud announcement", "NICE"),
    ("chapter_a", 1, B, "Every Halloween, I wake up to share my candy hoard.", "BUTTERSCOTCH'S HOARD",
     "bedtime-story opening, unhurried, a little yawn at the start", "NICE"),
    ("chapter_b", 1, B, "But sneaky Snuffwick snuffed my Hoard Lantern! Pfft!", "UH-OH, SNUFFWICK!",
     "mock-outraged, giggling; a soft 'Pfft!'", "NICE"),
    ("chapter_c", 1, B, "Long ago, the Candlewizards' Moonstone broke into crystals. Like yours!", "THE MOONSTONE",
     "hushed once-upon-a-time wonder, leaning in on 'Like yours!'", "NICE"),
    ("chapter_d", 1, B, "Each crystal remembers its wizard's glow... and glow wakes dragons!", "YOUR GLOW",
     "tender, then delighted", "NICE"),
    ("chapter_e", 1, B, "Every crystal adds a spark. Light my lantern, wizards!", "LIGHT THE LANTERN",
     "clear instruction, rising excitement", "NICE"),
    ("chapter_f", 1, B, "Psst... what's Snuffwick hiding in that lumpy sock?", "WHAT'S IN THE SOCK?",
     "curious, teasing whisper (the 2027 cliffhanger)", "NICE"),
]

# --------------------------------------------------------------------------- sounds
SFX = [
    {"id": "absorb", "priority": "MVP", "seconds": 1.0, "synth": "rise",
     "desc": "Crystal placed: a soft whoosh up into a warm glass chime (light flying into Butterscotch's tummy).",
     "_search": "magic whoosh chime, glass chime rise"},
    {"id": "awaken", "priority": "MVP", "seconds": 2.0, "synth": "sparkle",
     "desc": "A new crystal wakes: rising magical shimmer, twinkly harp glissando, warm and gentle.",
     "_search": "magic shimmer, harp glissando, fairy sparkle"},
    {"id": "reveal_t0", "priority": "MVP", "seconds": 1.0, "synth": "wahwah",
     "desc": "Generic GOBLIN TRICK! sting: a playful cartoon boing / slide-whistle + a tiny goblin giggle (never a fail "
             "buzzer).", "_search": "cartoon boing, slide whistle, small giggle"},
    {"id": "reveal_t0_1", "priority": "NICE", "seconds": 1.2, "synth": "bloop",
     "desc": "Gag 1 Rubber Chicken (starts 0.65 s into the gag): two squeaky rubber-chicken squeaks, then a soft magic "
             "'poof' + a coin 'ting' at about 0.5 s.", "_search": "rubber chicken squeak, cartoon poof, coin ting"},
    {"id": "reveal_t0_2", "priority": "NICE", "seconds": 1.2, "synth": "bloop",
     "desc": "Gag 2 Hiccup Bubbles: two cute 'hic!' hiccups, each with a soap-bubble pop.",
     "_search": "cartoon hiccup, bubble pop"},
    {"id": "reveal_t0_3", "priority": "NICE", "seconds": 1.2, "synth": "whoosh",
     "desc": "Gag 3 Snuff-Out (starts 0.65 s into the gag): a tiny candle 'pfft' (the lights go out), a beat of quiet "
             "with a little goblin giggle, then a bright sparkly shimmer at about 0.75 s (the sneeze relights the cave; "
             "Butterscotch's voice says the 'CHOO').", "_search": "candle snuff out, small giggle, magic sparkle burst"},
    {"id": "reveal_t1", "priority": "MVP", "seconds": 1.5, "synth": "chime",
     "desc": "HOARD COIN: a bright coin 'ting' + a twinkly two-chord chime, happy.",
     "_search": "coin ting, twinkle chime, magic bell"},
    {"id": "reveal_t2", "priority": "MVP", "seconds": 1.8, "synth": "sparkle",
     "desc": "DRAGON GEM: a magical swirl + sparkle arpeggio, wondrous.", "_search": "magic spell swirl, sparkle arpeggio"},
    {"id": "reveal_t3", "priority": "MVP", "seconds": 3.0, "synth": "fanfare",
     "desc": "GOLDEN STAR: harp glissando + a soft warm gong (the host's cue to come over). No harsh brass.",
     "_search": "harp glissando, soft gong, magical reveal"},
    {"id": "tap", "priority": "MVP", "seconds": 0.2, "synth": "blip",
     "desc": "Each tap on the window (or button press) during charge: a tiny bright blip (it repeats fast).",
     "_search": "ui blip, bubble pop short"},
    {"id": "button", "priority": "NICE", "seconds": 0.3, "synth": "blip",
     "desc": "Only with a plinth button (config.ini button = yes; none in 2026): a friendly bloop when it's pressed "
             "with no crystal on the stone.", "_search": "button bloop, cartoon pop"},
    {"id": "charge_full", "priority": "MVP", "seconds": 1.0, "synth": "chime",
     "desc": "Charge meter full: a quick rising 'ding-ding-DING'.", "_search": "level up ding, power up chime"},
    {"id": "sleepy", "priority": "MVP", "seconds": 1.0, "synth": "bloop",
     "desc": "Sleepy crystal: a soft descending double-bloop with a tiny yawn feel, gentle (never an error buzz).",
     "_search": "soft bloop descending, gentle cartoon"},
    {"id": "whoosh", "priority": "MVP", "seconds": 0.6, "synth": "whoosh",
     "desc": "Soft airy whoosh for transitions (goodbye float-up, the blessing bubble, rank-ups).",
     "_search": "soft whoosh, air swish"},
    {"id": "munch", "priority": "NICE", "seconds": 1.5, "synth": "bloop",
     "desc": "Snuffwick in the attract loop (played ~5.4 s into each loop as he peeks in; the id stays 'munch' for the "
             "firmware): a tiny shy goblin giggle + a soft candle 'pfft'.", "_search": "cute small giggle, candle blow out"},
    {"id": "milestone", "priority": "NICE", "seconds": 2.0, "synth": "fanfare",
     "desc": "Milestone: a party-horn toot + a cheer + sparkles, kid-friendly.",
     "_search": "party horn kids, celebration cheer"},
    {"id": "moonfull", "priority": "NICE", "seconds": 3.0, "synth": "fanfare",
     "desc": "The Hoard Lantern lights (finale): a warm 'whoomph' of a big lantern catching, swelling into a happy "
             "magical chord. Never a roar.", "_search": "lantern whoosh ignite soft, magical swell, triumphant fairy chord"},
]
MUSIC = [
    {"id": "idle", "priority": "MVP", "seconds": 60, "synth": "drone",
     "desc": "Seamless 45-90 s loop: a gentle spooky-cute music-box / celesta lullaby with soft pizzicato and a cozy low "
             "bassoon, slow, warm, never scary. Quiet enough to talk over.",
     "_search": "music box lullaby halloween, celesta spooky cute, cozy halloween loop"},
    {"id": "idle_full", "priority": "NICE", "seconds": 60, "synth": "drone",
     "desc": "The same lullaby in a brighter, happier feast-time arrangement for after the Hoard Lantern is lit.",
     "_search": "happy music box halloween, cozy feast celesta, playful pizzicato loop"},
]

# --------------------------------------------------------------------------- words (captions.ini)
WORDS = {
    "title": "Dragonglow",
    "character": "Butterscotch",
    "rank.1": "Sparkfinder", "rank.2": "Apprentice", "rank.3": "Glowcaster", "rank.4": "Hoardguard",
    "rank.5": "Wizard", "rank.6": "Dragon Rider", "rank.7": "Keeper of the Hoard",
    "tier.0": "GOBLIN TRICK!", "tier.0_color": "#8DBF6A",
    "tier.1": "HOARD COIN!", "tier.1_color": "#D9DEE8",
    "tier.2": "DRAGON GEM!", "tier.2_color": "#FF3B47",
    "tier.3": "GOLDEN STAR!", "tier.3_color": "#FFC83D",
    "bowl.common": "Silver Circle", "bowl.common_color": "#D9DEE8", "bowl.common_shape": "circle",
    "bowl.rare": "Red Triangle", "bowl.rare_color": "#FF3B47", "bowl.rare_shape": "triangle",
    "bowl.legend": "Gold Star", "bowl.legend_color": "#FFC83D", "bowl.legend_shape": "star",
    # crystal colours color.N / color.N_hex: filled in from CRYSTAL_COLORS just below (ids 4..6 left out = unused)
    "caption.attract": "Crystal here",
    "caption.absorb": "",
    "caption.awaken": "NEW CRYSTAL!",
    "caption.greet": "Welcome back!",
    "caption.charge": "PRESS!",
    "caption.reveal_t0": "GOBLIN TRICK!",
    "caption.reveal_t1": "HOARD COIN!",
    "caption.reveal_t2": "DRAGON GEM!",
    "caption.reveal_t3": "GOLDEN STAR!",
    "caption.replay": "ALREADY GLOWED",
    "caption.nocrystal": "DRAGON BLESSING!",
    "caption.foreign": "DRAGON BLESSING!",
    "caption.sleepy": "AGAIN, PLEASE",
    "caption.goodbye": "SEE YOU {next_year}",
    "caption.milestone": "HOORAY, WIZARDS!",
    "caption.circle": "WIZARD CIRCLE!",
    "caption.keeper": "SHH... SECRET",
    "caption.chapter": "",
    "caption.chapter_a": "BUTTERSCOTCH'S HOARD",
    "caption.chapter_b": "UH-OH, SNUFFWICK!",
    "caption.chapter_c": "THE MOONSTONE",
    "caption.chapter_d": "YOUR GLOW",
    "caption.chapter_e": "LIGHT THE LANTERN",
    "caption.chapter_f": "WHAT'S IN THE SOCK?",
    "caption.moonfull": "LANTERN LIT!",
    "caption.moonfull_b": "OFFICIAL PRANKSTER",
    "caption.recall": "LAST YEAR",
    "ui.place": "Crystal on the Wakestone",
    "ui.hold": "Hold still...",
    "ui.power": "GLOW",
    "ui.counter": "sparks for the lantern",
    "ui.press": "Press the button!",
    "ui.or_press": "OR PRESS",
    "ui.tonight": "TONIGHT",
    "ui.reader_asleep": "READER ASLEEP",
    "ui.rush": "RUSH",
    "ui.touch": "TOUCH FOR A BLESSING",
    "ui.no_card": "NO TF CARD",
    # The communal meter: the Hoard Lantern painted into bg_cave; the device draws its glow (x,y,glow radius in
    # screen px). Fill = tonight's sparks / config.ini [story] moon_goal (the key keeps its old name).
    "meter.style": "lantern",
    "meter.pos": "150,110,70",
    "meter.name": "Lantern",          # host menu "Fill the Lantern" / "Reset Lantern", boot screen "Lantern 12/50"
    "meter.color": "#FFB347",
    # The visuals that show the lantern glow: every clip on the cave background (bg_cave / bg_cave_lit) except
    # chapter_b (Snuffwick climbs up to the lantern and snuffs it: the story beat is the lantern going OUT).
    # chapter_e is listed: it starts on bg_cave with the painted (unlit) lantern while bg_cave_lit fades in, and
    # its line is "Every crystal adds a spark. Light my lantern, wizards!" - tonight's glow belongs there.
    # An id also covers its variants (greet -> greet_r5, reveal_t0 -> reveal_t0_3, attract -> attract_full);
    # recall shows the greet scene. Not listed: awaken, reveal_t2 (bg_wakestone), reveal_t3 / milestone
    # (bg_starburst), circle (bg_wakestone); moonfull flares the lantern itself.
    # main() adds meter.zoom_<id> for the listed clips whose background zooms (keeper's slow push-in).
    "meter.on": "attract,absorb,greet,recall,charge,reveal_t0,reveal_t1,replay,nocrystal,foreign,sleepy,goodbye,"
                "keeper,chapter_a,chapter_e,chapter_f",
    # Butterscotch's tummy glow when no crystal is around (attract, chapter): warm caramel-amber.
    "glow.idle_color": "#FFC27A",
    # GOBLIN TRICK! device effects per gag ("kind,x,y,startMs,endMs"; STORY.md section 7):
    # 1 Rubber Chicken: a white poof where the chicken turns into the coin (prop_coin appears at 1.12 s, x 615);
    # 2 Hiccup Bubbles: sparkles in the crystal colour from his mouth through both hiccups;
    # 3 Snuff-Out: a crystal-colour sparkle burst from his nose (just above the mouth) as the lights pop back on
    #   (bg_cave returns at 1.42 s).
    "fx.reveal_t0_1": "poof,615,230,1100,1100",
    "fx.reveal_t0_2": "sparkles,440,170,600,1800",
    "fx.reveal_t0_3": "burst,420,150,1420,1420",
    # nobutton.* (voice swaps + word overrides): appended from NOBUTTON_VOICE / NOBUTTON_WORDS below
}

# --------------------------------------------------------------------------- no big button (2026)
# The plinth has no big button this year (config.ini [station] button = no, the default). Then the firmware
#  * reads the word "nobutton.<key>" before <key> (captions.ini section [nobutton], e.g. "caption.charge = TAP! TAP!"),
#  * swaps voice per "nobutton.voice" = "from=to;from=to": a voice GROUP id or a full LINE id on each side.
#    group=group plays group <to> instead of group <from>; line=line plays line <to> when the random pick is
#    line <from>; an empty <to> = never play <from> (another variant of the group, or nothing).
# Kids without a crystal TAP THE WINDOW (the screen) for their dragon blessing, and returning kids tap it to
# charge the glow. With a button (button = yes) the original lines and words play.
NOBUTTON_VOICE = [
    ("attract_call_2", "attract_tap_1"),   # "No crystal? Press the big button!" -> "No crystal? Tap my window!"
    ("charge", "charge_tap"),              # "Press the button! ..." / "Press fast! ..." -> "Tap the window! ..." / "Tap tap tap! ..."
]
NOBUTTON_WORDS = {
    "caption.charge": "TAP! TAP!",          # was PRESS!
    "ui.press": "Tap the window!",         # was "Press the button!" (charge prompt)
    "ui.or_press": "OR TAP ME",            # was OR PRESS (alternates with "Crystal here" in the attract loop)
}
# Wording that needs a no-button alternative (checked in main() over every voice line and word).
BUTTON_WORDING = re.compile(r"\b(button|press)", re.I)


def _with_colors(words):
    """WORDS with color.N / color.N_hex from CRYSTAL_COLORS inserted after the bowls (captions.ini [color])."""
    out = {}
    for k, v in words.items():
        if k == "caption.attract":
            for cid, name, spec, _ in CRYSTAL_COLORS:
                out["color.%d" % cid] = name
                out["color.%d_hex" % cid] = spec
        out[k] = v
    return out


WORDS = _with_colors(WORDS)
WORDS["nobutton.voice"] = ";".join("%s=%s" % pair for pair in NOBUTTON_VOICE)
WORDS.update(("nobutton." + k, v) for k, v in NOBUTTON_WORDS.items())

VOICE_STYLE = {
    "butterscotch": "Warm, slow, cozy old dragon grandpa telling a bedtime story: soft and round, a gentle low "
                    "purr-rumble, smiling, a little whispery, a yawn hiding in some lines, delighted chuckles, bright on "
                    "exclamations. Never booming, never growly, never a roar. Quick delivery: each line fits in about "
                    "2-2.5 seconds.",
    "elevenlabs": "A gentle, older male storyteller voice (warm, soft, slightly husky - a kindly grandpa, not a deep "
                  "booming one). Stability ~40%, Similarity ~75%, Style ~30%, Speed 1.05-1.1.",
    "openai_tts": "gpt-4o-mini-tts with voice 'ballad' or 'cedar' (or 'ash'; avoid 'onyx', too deep), the Butterscotch "
                  "prompt as instructions. Disclose the AI voice on the parent sign.",
    "sidekick": "Snuffwick is not a separate actor and is mostly sound effects (giggle, squeak, 'pfft'). His only 2 "
                "lines (attract_call_8, circle_2) are recorded in Butterscotch's voice; the builder pitches them up 5 "
                "semitones at 1.15x speed (voice fx \"smudge\" - the effect kept its Moonshards name).",
    "tts_voice": "David",
}

# Story-specific text for tools/gen_asset_list.py (CHATGPT-ASSETS.md); free text, never read by the build.
ASSET_GUIDE = {
    "why_stills": "Butterscotch floating like a balloon and changing expressions, Snuffwick tiptoeing in with his "
                  "rubber chicken, the hoard coin spinning, the Dragon Gem rising out of the Wakestone, the Golden "
                  "Star climbing",
    "live_magic": "glows in each kid's crystal colour, sparkles, the Hoard Lantern's growing glow, the numbers and words",
    "anchor_note": "The Hoard Lantern's glow is placed by the words key `meter.pos` (\"x,y,radius\" in screen pixels, "
                   "default 150,110,70): once `bg_cave` exists, open `assets/previews/` and, if the painted lantern "
                   "isn't there, change `meter.pos` (or `/oracle/captions.ini` [meter] pos on the card).",
    "guardrails": "Butterscotch is CARAMEL / toffee brown, never gold: gold is only for the Golden Star (and its "
                  "starburst); silver is the hoard coin; red is only the Dragon Gem.",
    "order": "Day 1 Butterscotch (base + sleep, peek, delight, surprise) · Day 2 Butterscotch giggle + puff, Snuffwick "
             "(base + rubber chicken) and the four props · Day 3 the three backgrounds (`bg_cave` first: check where "
             "the lantern landed) · Day 4+ the nice-to-haves.",
    "printables": [
        "**Bowl signs** — print `prop_coin` (silver circle bowl: HOARD COIN, also the GOBLIN TRICK bowl), `prop_gem` "
        "(red triangle bowl: DRAGON GEM) and `prop_star` (gold star, held by the host: GOLDEN STAR) so the bowls match "
        "the screen picture-for-picture. Light each sign with a battery LED puck.",
        "**Parent sign** — `print_keyart` with the sign text from STORY.md §13. Keep the line *\"Butterscotch's voice is "
        "AI-generated.\"* (OpenAI's and ElevenLabs' terms ask you to disclose AI voices.)",
    ],
}

SCREEN = {"width": 800, "height": 480, "text_band_y": 360, "lantern_center": [150, 110], "lantern_glow_r": 70,
          "dragon": {"x": 420, "y": 40, "height": 300, "chest_anchor": [0.5, 0.62]},
          "snuffwick": {"height": 115, "floor_top_y": 225}}

INTERACTION_MAX_WORDS = 8     # lines during a kid's visit
ATTRACT_MAX_WORDS = 10
CAPTION_MAX = 24


def clip_stills(c):
    ids = []
    bg = c.get("bg")
    if isinstance(bg, dict):
        ids += [bg.get("image"), bg.get("fallback")]
    ids += [c.get("image")]
    for lay in c.get("layers", []):
        ids += [lay.get("sprite") or lay.get("image"), lay.get("fallback")]
    return [x for x in dict.fromkeys(ids) if x]


def clip_required(c):
    """Still ids a clip can't be built without (bg + non-optional layers; a fallback can stand in)."""
    req = []
    bg = c.get("bg")
    if isinstance(bg, dict) and bg.get("image"):
        req.append([bg["image"]] + ([bg["fallback"]] if bg.get("fallback") else []))
    if c.get("image"):
        req.append([c["image"]])
    for lay in c.get("layers", []):
        if not lay.get("optional"):
            req.append([lay.get("sprite") or lay.get("image")] + ([lay["fallback"]] if lay.get("fallback") else []))
    return req


def meter_listed(cid, on):
    """The firmware's meter.on rule: an id matches itself and its '_' variants (greet -> greet_r5)."""
    return any(cid == e or cid.startswith(e + "_") for e in on)


def meter_zoom_words(clips, words):
    """meter.zoom_<clip> = "from,to" for every meter.on clip whose background zooms (the layers recipe's centred,
    linear bg zoom), so the device's lantern glow follows the painted lantern (keeper's push-in)."""
    on = [e.strip() for e in words.get("meter.on", "").replace(";", ",").split(",") if e.strip()]
    out = {}
    for c in clips:
        bg = c.get("bg")
        zoom = bg.get("zoom") if isinstance(bg, dict) else None
        if zoom and meter_listed(c["id"], on) and list(zoom) != [1.0, 1.0]:
            out["meter.zoom_%s" % c["id"]] = "%g,%g" % (zoom[0], zoom[1])
    return out


def main():
    all_words = dict(WORDS)
    all_words.update(meter_zoom_words(CLIPS, WORDS))
    still_ids = [s[0] for s in STILLS]
    assert len(still_ids) == len(set(still_ids)), "duplicate still id"
    by_id = {s[0]: s for s in STILLS}
    pri = {s[0]: s[3] for s in STILLS}

    # sanity: every clip still exists; MVP clips build from MVP stills alone
    for c in CLIPS:
        for sid in clip_stills(c):
            assert sid in by_id, "clip %s uses unknown still %s" % (c["id"], sid)
        if c["priority"] == "MVP":
            for alts in clip_required(c):
                assert any(pri[a] == "MVP" for a in alts), "MVP clip %s needs NICE still(s) %s" % (c["id"], alts)

    stills = []
    for sid, kind, tr, prio, desc, prompt, extra in STILLS:
        used = [c["id"] for c in CLIPS if sid in clip_stills(c)] + USED_EXTRA.get(sid, [])
        e = {"id": sid, "kind": kind, "transparent": tr, "priority": prio, "desc": desc, "used_by": used,
             "prompt": prompt}
        e.update(extra)
        if e.get("edit_of"):
            assert e["edit_of"] in by_id
        stills.append(e)

    voice = []
    for g, n, spk, text, cap, delivery, prio in VOICE:
        e = {"id": "%s_%d" % (g, n), "group": g, "variant": n, "speaker": spk, "text": text, "caption": cap,
             "delivery": delivery, "priority": prio}
        if spk == S:
            e["fx"] = "smudge"
        words = len(text.split())
        if g in ("attract_call", "attract_tap"):
            assert words <= ATTRACT_MAX_WORDS, (e["id"], words)
        elif not g.startswith(("chapter_", "moonfull_")):
            assert words <= INTERACTION_MAX_WORDS, (e["id"], words)
        assert len(cap) <= CAPTION_MAX, (e["id"], cap)
        voice.append(e)
    ids = [v["id"] for v in voice]
    assert len(ids) == len(set(ids)), "duplicate voice id"
    # the crystal colours: ids 1..N contiguous, one awaken_b line each (key word = the colour's name first)
    assert [c[0] for c in CRYSTAL_COLORS] == list(range(1, len(CRYSTAL_COLORS) + 1)) and len(CRYSTAL_COLORS) <= 6
    ab = {v["variant"]: v for v in voice if v["group"] == "awaken_b"}
    assert sorted(ab) == [c[0] for c in CRYSTAL_COLORS], "one awaken_b line per crystal colour"
    for cid, name, _, _ in CRYSTAL_COLORS:
        assert ab[cid]["text"].startswith(name + "!"), (cid, ab[cid]["text"])
        assert ab[cid]["caption"] == name.upper(), (cid, ab[cid]["caption"])
    assert sum(v.get("fx") == "smudge" for v in voice) <= 2, "Snuffwick speaks at most 2 lines"
    for k, v in all_words.items():
        if k.startswith(("caption.", "nobutton.caption.")):
            assert len(v) <= CAPTION_MAX, (k, v)
    # no big button: every swap names lines/groups that exist, and every line / word that says "button" or
    # "press" has a no-button alternative (a voice swap from its line or group; a nobutton.<key> word)
    groups = {v["group"] for v in voice}
    swapped = {a for a, _ in NOBUTTON_VOICE}
    for a, b in NOBUTTON_VOICE:
        assert a in ids or a in groups, ("nobutton.voice from", a)
        assert not b or b in ids or b in groups, ("nobutton.voice to", b)
    for v in voice:
        if BUTTON_WORDING.search(v["text"]):
            assert v["id"] in swapped or v["group"] in swapped, ("button line without a no-button swap", v["id"])
        if v["id"] in swapped or v["group"] in swapped:
            assert v["priority"] == "NICE", ("button-only lines are NICE (button = no is the default)", v["id"])
    for k, v in all_words.items():
        if not k.startswith(("nobutton.", "fx.", "meter.")) and BUTTON_WORDING.search(v):
            assert "nobutton." + k in all_words, ("button word without a nobutton override", k)

    manifest = {
        "schema": 1, "title": TITLE, "fps": 13, "screen": SCREEN, "voice_style": VOICE_STYLE,
        "_asset_guide": ASSET_GUIDE,
        "words": all_words, "stills": stills, "clips": CLIPS, "voice": voice, "sfx": SFX, "music": MUSIC,
    }
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1, ensure_ascii=False)
        f.write("\n")
    n_mvp_st = sum(s["priority"] == "MVP" for s in stills)
    n_mvp_v = sum(v["priority"] == "MVP" for v in voice)
    n_built = sum(1 + len(CRYSTAL_COLORS) if c.get("variants") == "colors" else 1 for c in CLIPS)
    print("wrote", os.path.normpath(OUT))
    print("stills %d (MVP %d, NICE %d) | clips %d recipes -> %d clips | voice %d (MVP %d, NICE %d) | sfx %d | music %d"
          " | words %d" % (len(stills), n_mvp_st, len(stills) - n_mvp_st, len(CLIPS), n_built, len(voice), n_mvp_v,
                           len(voice) - n_mvp_v, len(SFX), len(MUSIC), len(all_words)))


if __name__ == "__main__":
    main()
