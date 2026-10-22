# ARCHIVED: generates the archived Moonshards manifest (docs/stories/manifest.moonshards.json); it writes assets/manifest.json, so running it REPLACES the current Dragonglow manifest (tools/gen_manifest_dragonglow.py makes that one).
"""gen_manifest_from_story.py -- one-off generator that built assets/manifest.json (2026-10-22) from the
story synthesis (STORY.md's script, stills and words) plus the hand-designed clip recipes below.

Kept in the repo as documentation of HOW the manifest was designed. After the first run, edit
assets/manifest.json directly (it is the source of truth); re-running this script overwrites it.

    python -I tools/gen_manifest_from_story.py <path-to-synth.json>
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# assets/manifest.json is now HAND-EDITED (it is the source of truth; build_assets.py builds the "layers"
# recipe since 2026-10-22). Re-running this generator OVERWRITES it and loses those edits.
OUT = os.path.join(HERE, "..", "assets", "manifest.json")

STYLE = ("Cute storybook 3D-painted style like a modern family animated film; soft rounded chunky shapes; "
         "smooth clean shading; gentle rim light; no grain, no fine texture, no mist, no bokeh, no tiny stars, "
         "no text, no letters, no watermark. Spooky-cute and friendly, never scary.")
BG = ("Format: wide landscape, 5:3 (1600x960 if you can choose; otherwise the widest landscape option). "
      "Dark midnight-indigo night (#12102A). Keep the BOTTOM QUARTER of the image plain near-black and empty "
      "(the screen draws words there). Big simple shapes, clean smooth gradients.")
SPRITE = ("Full figure, centred, nothing cropped, on a TRANSPARENT background (PNG). If you can't make it "
          "transparent, use a flat pure green #00FF00 background with no shadow and no floor.")
OONA_EDIT = ("Use the attached oona_base image as Image 1. Edit Image 1: change ONLY {what}. Keep her body shape, "
             "the lantern (same position, same empty round clear glass globe), size, colours, framing and the "
             "transparent background EXACTLY identical. Same style.")
SMUDGE_EDIT = ("Use the attached smudge_base image as Image 1. Edit Image 1: change ONLY {what}. Keep his body, "
               "fluff colour, wings, size, colours, framing and the transparent background EXACTLY identical. Same style.")


def P(*parts):
    return " ".join(p.strip() for p in parts if p)


PROMPTS = {
    "bg_window": P("Night view through a round attic window (the window frame is just outside the picture; soft plum curtain edges at the far left and right).",
                   "A deep midnight-indigo sky with a few big soft glowing stars; a gentle rounded hill of glossy UNLIT pumpkins and a few tiny far-off town windows across the lower middle.",
                   "Leave the upper-right third as empty dark sky (a Moon will be drawn there later) and leave the centre open (a character floats there).", BG, STYLE),
    "bg_awaken": P("The Glowstone Hollow: a dark indigo-and-plum hilltop clearing framed by big soft rounded hill silhouettes and two curling vine shapes.",
                   "One broad, soft column of pale moonlight falls onto a round flat grey stone pedestal with a carved crescent on its front, in the exact centre; the stone's top surface is at about 60% of the image height and is EMPTY.",
                   BG, STYLE),
    "bg_starburst": P("A blue-black night sky with about 12 broad, soft-edged gold (#FFC83D) rays fanning out from the exact centre of the image, a soft gold glow at the centre and a few large simple stars. Nothing else.",
                      BG, STYLE),
    "oona_base": P("MASTER CHARACTER IMAGE. Oona, a small chubby grandmother ghost shaped like a soft teardrop that ends in one wispy curl at the bottom (no legs).",
                   "Moonlight-white body (#F7FBFF) with a faint mint rim glow (#CFFFF2); big kind dark eyes, rosy cheeks, tiny round SILVER spectacles; a floppy indigo nightcap (#3B3A8C) with a small SILVER star on the tip.",
                   "Two stubby arms hold an old dark-iron lantern in front of her tummy; the lantern's glass globe is ROUND, CLEAR and EMPTY (no flame). Gentle closed-mouth smile, facing the viewer. Nothing gold, nothing sharp.",
                   SPRITE, STYLE),
    "oona_sleep": OONA_EDIT.format(what="the face: eyes closed in happy sleep, a small round snoring mouth, and the nightcap drooping over one eye"),
    "oona_peek": OONA_EDIT.format(what="the face: one eye open, the other still closed, a sly little smile"),
    "oona_delight": OONA_EDIT.format(what="the face: a huge open smile, sparkly happy eyes, extra-rosy cheeks"),
    "oona_surprise": OONA_EDIT.format(what="the face: a round O-shaped mouth, eyebrows high, spectacles lifted a little"),
    "oona_giggle": OONA_EDIT.format(what="the face: eyes squeezed shut laughing, cheeks puffed, a tiny tongue-out giggle"),
    "oona_pumpkin": OONA_EDIT.format(what="her head: a friendly smiling jack-o'-lantern is stuck over her head down to her eyebrows, her amused eyes peeking out under its rim; the nightcap is hidden under the pumpkin"),
    "oona_shh": OONA_EDIT.format(what="her free hand and face: one finger to her lips, half-lidded twinkly eyes, a secret smile (the lantern stays exactly in place in the other hand)"),
    "oona_worried": OONA_EDIT.format(what="the face: a cute 'oh dear' look, worried raised eyebrows, a small wobbly mouth"),
    "oona_proud": OONA_EDIT.format(what="the face: eyes closed in a proud, happy smile, cheeks glowing"),
    "oona_witchhat": OONA_EDIT.format(what="her hat: the chunky bright red (#FF3B47) witch's hat (same design as prop_witchhat) replaces her nightcap, and she winks"),
    "smudge_base": P("MASTER CHARACTER IMAGE. Smudge, a marshmallow moth that looks like a PLUSH TOY, not a real bug:",
                     "a round pom-pom body of lavender-grey fluff (#B9A8D6), a fluffy cream collar (#FFF1D6), two tiny fern antennae, huge glossy black eyes with star highlights, one small rounded buck tooth in a cheeky grin,",
                     "two small soft wings with pale moon-shaped spots, two tiny stubby fluff paws. No legs, no body segments, nothing insect-like. Soft pale rim light. Hovering, facing the viewer.",
                     SPRITE, STYLE),
    "smudge_munch": SMUDGE_EDIT.format(what="his pose and face: cheeks stuffed, happily munching a glowing silver moon-crumb held in both paws, a few big sparkle crumbs falling"),
    "smudge_sneeze": SMUDGE_EDIT.format(what="his face: scrunched mid-sneeze, eyes squeezed shut, a round puff of glitter dust bursting from his nose"),
    "smudge_sheepish": SMUDGE_EDIT.format(what="his face and paws: guilty big shiny eyes, paws together, a tiny blush, a small apologetic smile"),
    "smudge_dazzled": SMUDGE_EDIT.format(what="his face: swirly spiral eyes, a dizzy wobbly smile, three little silver stars circling his head"),
    "prop_moon": P("A large full Moon disc, pale silver-white (#E9EEFF), a few big soft round craters, a gentle glowing edge, no face. Front view.", SPRITE, STYLE),
    "prop_crystal": P("One pale, clear, faceted crystal shaped like the crystal in the attached photo (attach a photo of one of your real resin crystals), nearly white with soft internal facets and a bright top highlight. Front view, standing upright.",
                      SPRITE, STYLE),
    "prop_starwisp": P("A tiny chubby five-point star with a sweet sleepy smile and a long wispy comet tail curling up and to the left, pale silver-white with a soft mint glow (NOT gold).", SPRITE, STYLE),
    "prop_witchhat": P("A chunky bright red (#FF3B47) pointed witch's hat with a slightly bent tip and a small silver buckle, front view, a clear triangle silhouette, no face.", SPRITE, STYLE),
    "prop_wishstar": P("A chunky glossy gold (#FFC83D) five-point star with rounded points and a soft warm glow, no face, front view.", SPRITE, STYLE),
    "bg_window_lit": P("Use the attached bg_window image as Image 1. Edit Image 1: make every pumpkin on the hill glow warm amber-orange from inside and light up the far-off town windows.",
                       "Keep the sky, layout, curtains and the empty upper-right Moon area EXACTLY identical. Same style; the bottom quarter stays plain near-black."),
    "chapter_nibble": P("Storybook wide shot: Smudge (use the attached smudge_base image for his exact design) sitting on a thin silver crescent Moon high in an indigo night sky, munching it like a cookie, a few glowing crumbs falling.", BG, STYLE),
    "chapter_shardfall": P("Storybook wide shot: a big round silver Moon with a simple friendly laughing face high in the sky, sparkles falling from it and landing as small glowing crystals in six colours (violet, orange, ice blue, green, pink, white) among pumpkins on a dark hill.", BG, STYLE),
    "fx_bubble": P("A glossy round soap bubble with a soft rainbow rim and a mostly clear centre. Nothing else.", SPRITE, STYLE),
    "icon_button": P("Top-down view of one big round glossy arcade button with a white ring and a soft glow, simple icon style.", SPRITE, STYLE),
    "print_keyart": P("Portrait 2:3 poster art: Oona (use the attached oona_base for her exact design) holding her glowing lantern, Smudge (attached smudge_base) peeking over her shoulder with a grin, one glowing crystal on a round grey stone below.",
                      "Generous plain dark space at the top and bottom for sign text. Dark midnight-indigo background.", STYLE),
}

ORDER = ["bg_window", "bg_awaken", "bg_starburst", "oona_base", "oona_sleep", "oona_peek", "oona_delight", "oona_surprise",
         "oona_giggle", "oona_pumpkin", "smudge_base", "smudge_munch", "smudge_sneeze", "prop_moon", "prop_crystal",
         "prop_starwisp", "prop_witchhat", "prop_wishstar", "oona_shh", "oona_worried", "oona_proud", "oona_witchhat",
         "smudge_sheepish", "smudge_dazzled", "bg_window_lit", "chapter_nibble", "chapter_shardfall", "fx_bubble",
         "icon_button", "print_keyart"]

# ---- clip helpers (screen 800x480; keep subjects above y=360; the device draws the Moon at (650,110)) ----
OONA = {"height": 300, "x": 400, "y": 50}


def oona(sprite, t=None, bob=(6, 2.4), fin=0.25, fout=0.25, **kw):
    layer = {"sprite": sprite, **OONA, "bob": {"amp": bob[0], "period": bob[1]}, "fade_in": fin, "fade_out": fout}
    if t:
        layer["t"] = t
    layer.update(kw)
    return layer


def smudge(sprite, x, y, t, height=110, **kw):
    layer = {"sprite": sprite, "height": height, "x": x, "y": y, "t": t, "flutter": True, "fade_in": 0.1, "fade_out": 0.1}
    layer.update(kw)
    return layer


CRYSTAL_HEX = ["#A35CFF", "#FF7A1A", "#5CD6FF", "#3DFF7A", "#FF6FC8", "#EAFBFF"]

CLIPS = [
    {"id": "attract", "priority": "MVP", "recipe": "layers", "seconds": 12, "loop": True, "bg": {"image": "bg_window"},
     "layers": [oona("oona_sleep", t=[0, 8.75], bob=(6, 3), fin=0, fout=0.25),
                oona("oona_peek", t=[8.5, 11.75], bob=(6, 3)),
                oona("oona_sleep", t=[11.5, 12], bob=(6, 3), fout=0),
                smudge("smudge_base", x=[880, 700], y=[30, 80], t=[3, 5.5]),
                smudge("smudge_munch", x=700, y=80, t=[5.4, 8.1], flutter=False, bob={"amp": 3, "period": 0.4}),
                smudge("smudge_base", x=[700, 900], y=[80, 20], t=[8, 10])],
     "anchor_layer": 0, "notes": "Oona sleeps/peeks; Smudge flutters to the (device-drawn) Moon, munches, flies off."},
    {"id": "attract_full", "priority": "NICE", "recipe": "layers", "seconds": 12, "loop": True, "bg": {"image": "bg_window_lit"},
     "layers": [oona("oona_sleep", t=[0, 8.75], bob=(6, 3), fin=0),
                oona("oona_proud", t=[8.5, 11.75], bob=(6, 3), fallback="oona_peek"),
                oona("oona_sleep", t=[11.5, 12], bob=(6, 3), fout=0),
                smudge("smudge_sheepish", x=[900, 690], y=[40, 70], t=[4, 9], optional=True)],
     "anchor_layer": 0, "notes": "After the Moon is full: lit pumpkins; Smudge the Official Trickster visits."},
    {"id": "absorb", "priority": "MVP", "recipe": "layers", "seconds": 1, "loop": False, "bg": {"image": "bg_window"},
     "layers": [oona("oona_base", t=[0, 0.25], fin=0, fout=0.12), oona("oona_surprise", t=[0.12, 1], fin=0.12, fout=0)],
     "anchor_layer": 1, "notes": "The device draws the beam of crystal-coloured light into the lantern."},
    {"id": "awaken", "priority": "MVP", "recipe": "layers", "seconds": 8, "loop": False, "variants": "colors",
     "bg": {"image": "bg_awaken", "zoom": [1.0, 1.05]},
     "layers": [{"sprite": "prop_crystal", "tint": "$color", "height": [80, 130, 130], "x": 400, "y": [300, 175, 165],
                 "t": [0, 8], "fade_in": 0.4, "bob": {"amp": 4, "period": 2}},
                {"sprite": "oona_surprise", "height": 220, "x": 620, "y": [-260, 40], "t": [0.4, 3.2], "fade_in": 0.2,
                 "fade_out": 0.25, "bob": {"amp": 5, "period": 2.4}},
                {"sprite": "oona_delight", "height": 220, "x": 620, "y": 40, "t": [3.0, 8], "fade_in": 0.25,
                 "bob": {"amp": 5, "period": 2.4}}],
     "anchor_layer": 2, "notes": "6 colour variants awaken_1..awaken_6 (crystal tinted with color.N_hex). The device adds rays/glow + the meter."},
    {"id": "greet", "priority": "MVP", "recipe": "layers", "seconds": 2.4, "loop": True, "bg": {"image": "bg_window"},
     "layers": [oona("oona_delight", bob=(6, 2.4), fin=0, fout=0)], "anchor_layer": 0,
     "notes": "One visual for every rank; the device draws the crystal, rank aura, meter and rank name."},
    {"id": "charge", "priority": "MVP", "recipe": "layers", "seconds": 2.4, "loop": True, "bg": {"image": "bg_window"},
     "layers": [oona("oona_base", bob=(6, 2.4), fin=0, fout=0, y=40)], "anchor_layer": 0,
     "notes": "The device draws the button prompt, meter fill and sparkles."},
    {"id": "reveal_t0_1", "priority": "MVP", "recipe": "layers", "seconds": 2.5, "loop": False, "bg": {"image": "bg_window"},
     "layers": [oona("oona_giggle", fin=0.15, fout=0),
                smudge("smudge_base", x=[900, 580], y=[140, 120], t=[0, 0.6]),
                smudge("smudge_sneeze", x=[580, 595], y=120, t=[0.55, 1.25], flutter=False),
                smudge("smudge_base", x=[595, 900], y=[120, 30], t=[1.2, 2.0])],
     "anchor_layer": 0, "notes": "Gag 1 The Sneeze. The device sprinkles polka dots in the crystal colour."},
    {"id": "reveal_t0_2", "priority": "MVP", "recipe": "layers", "seconds": 2.5, "loop": False, "bg": {"image": "bg_window"},
     "layers": [oona("oona_giggle", fin=0.15, fout=0),
                smudge("smudge_munch", x=600, y=120, t=[0, 1.0], flutter=False, bob={"amp": 3, "period": 0.4}),
                smudge("smudge_sheepish", x=600, y=120, t=[0.95, 2.5], fallback="smudge_base"),
                {"sprite": "fx_bubble", "height": [40, 90], "x": [600, 400], "y": [150, 330], "t": [1.0, 2.4],
                 "fade_in": 0.1, "fade_out": 0.2, "blend": "screen", "optional": True}],
     "anchor_layer": 0, "notes": "Gag 2 The Bubble Burp (the bubble floats down into the crystal)."},
    {"id": "reveal_t0_3", "priority": "MVP", "recipe": "layers", "seconds": 2.5, "loop": False, "bg": {"image": "bg_window"},
     "layers": [oona("oona_base", t=[0, 0.7], fin=0.1, fout=0.1),
                smudge("smudge_base", x=[-100, 900], y=[200, 60], t=[0.2, 0.9]),
                oona("oona_pumpkin", t=[0.6, 2.5], fin=0.1, fout=0, rotate=[0, -3, 3, -2, 0])],
     "anchor_layer": 2, "notes": "Gag 3 Pumpkin Head (the device adds a sparkle poof at 0.6 s)."},
    {"id": "reveal_t1", "priority": "MVP", "recipe": "layers", "seconds": 2.5, "loop": False, "bg": {"image": "bg_window"},
     "layers": [oona("oona_delight", fin=0.15, fout=0),
                {"sprite": "prop_moon", "height": [40, 70, 50], "x": [400, 420, 640], "y": [250, 140, 85], "t": [0.3, 2.2],
                 "fade_in": 0.2, "fade_out": 0.35}],
     "anchor_layer": 0, "notes": "A little silver moon rises from the lantern and joins the big Moon."},
    {"id": "reveal_t2", "priority": "MVP", "recipe": "layers", "seconds": 2.5, "loop": False, "bg": {"image": "bg_awaken"},
     "layers": [oona("oona_surprise", fin=0.15, fout=0),
                {"sprite": "prop_witchhat", "height": [60, 150, 150], "x": 400, "y": [260, 95, 105], "t": [0.2, 2.5],
                 "fade_in": 0.15, "spin": 1.5}],
     "anchor_layer": 0, "notes": "The Witch's Hat spins up out of the lantern."},
    {"id": "reveal_t2_2", "priority": "NICE", "recipe": "layers", "seconds": 2.5, "loop": False, "bg": {"image": "bg_awaken"},
     "layers": [oona("oona_surprise", t=[0, 1.3], fin=0.15, fout=0.2),
                {"sprite": "prop_witchhat", "height": [60, 110], "x": 400, "y": [260, 20], "t": [0.1, 1.3], "spin": 1, "fade_out": 0.2},
                oona("oona_witchhat", t=[1.1, 2.5], fin=0.2, fout=0)],
     "anchor_layer": 0, "notes": "Variant 2: the hat lands on Oona, who winks."},
    {"id": "reveal_t3", "priority": "MVP", "recipe": "layers", "seconds": 3, "loop": False,
     "bg": {"image": "bg_starburst", "rotate": [0, 15]},
     "layers": [{"sprite": "prop_wishstar", "height": [70, 240], "x": 400, "y": [300, 70], "t": [0, 3], "fade_in": 0.3,
                 "bob": {"amp": 4, "period": 1.5}}],
     "notes": "The rays turn slowly; the Wishing Star rises and grows (no strobe)."},
    {"id": "replay", "priority": "MVP", "recipe": "layers", "seconds": 2.4, "loop": True, "bg": {"image": "bg_window"},
     "layers": [oona("oona_shh", fin=0, fout=0, fallback="oona_peek")], "anchor_layer": 0},
    {"id": "nocrystal", "priority": "MVP", "recipe": "layers", "seconds": 3.5, "loop": False, "bg": {"image": "bg_window"},
     "layers": [oona("oona_delight", fin=0.15, fout=0),
                {"sprite": "prop_starwisp", "height": [40, 70, 60], "x": [400, 300, 400], "y": [240, 280, 330],
                 "t": [0.3, 3.2], "fade_in": 0.2, "fade_out": 0.3, "bob": {"amp": 3, "period": 1}}],
     "anchor_layer": 0, "notes": "A star-wisp drifts from the lantern to the kid."},
    {"id": "sleepy", "priority": "MVP", "recipe": "layers", "seconds": 3.2, "loop": True, "bg": {"image": "bg_window"},
     "layers": [oona("oona_sleep", bob=(6, 3.2), fin=0, fout=0)], "anchor_layer": 0},
    {"id": "goodbye", "priority": "MVP", "recipe": "layers", "seconds": 1.6, "loop": False, "bg": {"image": "bg_window"},
     "layers": [{"sprite": "oona_delight", "height": [300, 170], "x": 400, "y": [50, -40], "t": [0, 1.6], "fade_out": 0.5}],
     "anchor_layer": 0},
    {"id": "milestone", "priority": "MVP", "recipe": "layers", "seconds": 3, "loop": False,
     "bg": {"image": "bg_starburst", "rotate": [0, 15]}, "layers": [oona("oona_delight", fin=0.2, fout=0)], "anchor_layer": 0,
     "notes": "The device adds confetti in the six crystal colours + the big counter."},
    {"id": "circle", "priority": "MVP", "recipe": "layers", "seconds": 2.5, "loop": False, "bg": {"image": "bg_awaken"},
     "layers": [oona("oona_giggle", fin=0.15, fout=0),
                smudge("smudge_dazzled", x=[880, 700], y=60, t=[0.8, 2.5], optional=True)],
     "anchor_layer": 0, "notes": "The device orbits small crystals in the group's colours."},
    {"id": "keeper", "priority": "NICE", "recipe": "layers", "seconds": 2.5, "loop": False,
     "bg": {"image": "bg_window", "zoom": [1.0, 1.15]},
     "layers": [{"sprite": "oona_shh", "height": [300, 380], "x": 400, "y": [50, 10], "t": [0, 2.5], "fade_in": 0.2,
                 "fallback": "oona_peek"}],
     "anchor_layer": 0},
    {"id": "moonfull", "priority": "NICE", "recipe": "layers", "seconds": 6, "loop": False, "bg": {"image": "bg_window"},
     "layers": [{"image": "bg_window_lit", "t": [2.5, 6], "fade_in": 1.0, "optional": True},
                oona("oona_delight", t=[0, 3.5], fout=0.25),
                oona("oona_proud", t=[3.3, 6], fin=0.25, fout=0, fallback="oona_delight"),
                smudge("smudge_dazzled", x=[880, 560, 520], y=[40, 120, 200], t=[0.5, 3.5], rotate=[0, 360, 720], fallback="smudge_base"),
                smudge("smudge_sheepish", x=520, y=200, t=[3.4, 6], flutter=False, fallback="smudge_base")],
     "anchor_layer": 1, "notes": "The finale: Smudge dazzled, then sorry; the pumpkins light up."},
    {"id": "chapter_a", "priority": "NICE", "recipe": "layers", "seconds": 4.5, "loop": False,
     "bg": {"image": "bg_window_lit", "fallback": "bg_window"},
     "layers": [oona("oona_base", y=[70, 40], fin=0.3, fout=0.3)], "anchor_layer": 0},
    {"id": "chapter_b", "priority": "NICE", "recipe": "kenburns", "image": "chapter_nibble", "seconds": 4.5,
     "zoom": [1.05, 1.12], "pan": "up"},
    {"id": "chapter_c", "priority": "NICE", "recipe": "kenburns", "image": "chapter_shardfall", "seconds": 4.5,
     "zoom": [1.1, 1.1], "pan": "down"},
    {"id": "chapter_d", "priority": "NICE", "recipe": "layers", "seconds": 4.5, "loop": False, "bg": {"image": "bg_awaken"},
     "layers": [{"sprite": "prop_crystal", "tint": c, "height": 130, "x": 400, "y": 165, "t": [i * 0.75, i * 0.75 + 0.95],
                 "fade_in": 0.2, "fade_out": 0.2, "bob": {"amp": 4, "period": 2}} for i, c in enumerate(CRYSTAL_HEX)]},
    {"id": "chapter_e", "priority": "NICE", "recipe": "layers", "seconds": 4.5, "loop": False, "bg": {"image": "bg_awaken"},
     "layers": [{"sprite": "prop_crystal", "tint": "#A35CFF", "height": [90, 130], "x": 400, "y": [290, 160], "t": [0, 4.5], "fade_in": 0.3}]},
    {"id": "chapter_f", "priority": "NICE", "recipe": "layers", "seconds": 5, "loop": False, "bg": {"image": "bg_window"},
     "layers": [oona("oona_peek"), smudge("smudge_base", x=[-100, 900], y=[300, 60], t=[0.8, 4.2])], "anchor_layer": 0},
]

SFX = [
    {"id": "absorb", "priority": "MVP", "seconds": 1.0, "synth": "rise", "desc": "Crystal placed: a soft whoosh up into a bright glass chime (light flying into the lantern)."},
    {"id": "awaken", "priority": "MVP", "seconds": 2.0, "synth": "sparkle", "desc": "A new crystal wakes: rising magical shimmer, twinkly harp glissando, warm and gentle."},
    {"id": "reveal_t0", "priority": "MVP", "seconds": 1.0, "synth": "wahwah", "desc": "Generic SMUDGED! sting: playful cartoon boing / slide-whistle down (never a fail buzzer)."},
    {"id": "reveal_t0_1", "priority": "NICE", "seconds": 1.2, "synth": "wahwah", "desc": "Gag 1: a tiny cute 'ah-ah-CHOO!' sneeze + a sprinkle twinkle."},
    {"id": "reveal_t0_2", "priority": "NICE", "seconds": 1.2, "synth": "bloop", "desc": "Gag 2: a small cute 'hic-burp' + a soap-bubble pop."},
    {"id": "reveal_t0_3", "priority": "NICE", "seconds": 1.0, "synth": "bloop", "desc": "Gag 3: a soft 'poof' + a hollow pumpkin 'bonk'."},
    {"id": "reveal_t1", "priority": "MVP", "seconds": 1.5, "synth": "chime", "desc": "SILVER MOON: a twinkly two-chord chime, bright and happy."},
    {"id": "reveal_t2", "priority": "MVP", "seconds": 1.8, "synth": "sparkle", "desc": "WITCH'S HAT: a magical swirl + sparkle arpeggio, a little mischievous."},
    {"id": "reveal_t3", "priority": "MVP", "seconds": 3.0, "synth": "fanfare", "desc": "WISHING STAR: harp glissando + a soft warm gong (the host's cue to come over). No harsh brass."},
    {"id": "tap", "priority": "MVP", "seconds": 0.2, "synth": "blip", "desc": "Each press/tap during charge: a tiny bright blip (it repeats fast)."},
    {"id": "button", "priority": "MVP", "seconds": 0.3, "synth": "blip", "desc": "The big plinth button when idle: a friendly bloop."},
    {"id": "charge_full", "priority": "MVP", "seconds": 1.0, "synth": "chime", "desc": "Charge meter full: a quick rising 'ding-ding-DING'."},
    {"id": "sleepy", "priority": "MVP", "seconds": 1.0, "synth": "bloop", "desc": "Sleepy crystal: a soft descending double-bloop, gentle (never an error buzz)."},
    {"id": "whoosh", "priority": "MVP", "seconds": 0.6, "synth": "whoosh", "desc": "Soft airy whoosh for transitions (goodbye float-up, star-wisp)."},
    {"id": "munch", "priority": "NICE", "seconds": 1.5, "synth": "bloop", "desc": "Smudge munching moon-crumbs: cute nom-nom crunches (attract loop)."},
    {"id": "milestone", "priority": "NICE", "seconds": 2.0, "synth": "fanfare", "desc": "Milestone: a party-horn toot + a cheer + sparkles, kid-friendly."},
    {"id": "moonfull", "priority": "NICE", "seconds": 3.0, "synth": "fanfare", "desc": "The Moon fills: a big warm magical swell into a happy chord."},
]
MUSIC = [
    {"id": "idle", "priority": "MVP", "seconds": 60, "synth": "drone", "desc": "Seamless 45-90 s loop: gentle spooky-cute music-box / celesta lullaby with soft pizzicato, slow, cozy, never scary. Quiet enough to talk over."},
    {"id": "idle_full", "priority": "NICE", "seconds": 60, "synth": "drone", "desc": "The same lullaby in a brighter, happier arrangement for after the Moon is full."},
]


def main():
    syn = json.load(open(sys.argv[1], encoding="utf-8"))
    syn_stills = {s["id"]: s for s in syn["stills"]}
    assert set(ORDER) == set(syn_stills) == set(PROMPTS), set(ORDER) ^ set(syn_stills)
    stills = []
    for k in ORDER:
        s = syn_stills[k]
        e = {"id": k, "kind": s["kind"], "transparent": bool(s["transparent"]), "priority": s["priority"],
             "desc": s["description"], "used_by": s["used_by"], "prompt": PROMPTS[k]}
        if k.startswith("oona_") and k != "oona_base":
            e["edit_of"] = "oona_base"
        elif k.startswith("smudge_") and k != "smudge_base":
            e["edit_of"] = "smudge_base"
        elif k == "bg_window_lit":
            e["edit_of"] = "bg_window"
        if k.startswith("oona_"):
            e["anchor"] = [0.5, 0.70]   # lantern-globe centre as a fraction of the sprite box; adjust after generating
        if k == "chapter_nibble":
            e["refs"] = ["smudge_base"]
        if k == "print_keyart":
            e["refs"] = ["oona_base", "smudge_base"]
        if k == "prop_crystal":
            e["refs"] = ["a photo of one of your real crystals"]
        stills.append(e)

    voice = []
    for ln in syn["lines"]:
        g, n = ln["cue"], int(ln["variant"])
        e = {"id": f"{g}_{n}", "group": g, "variant": n, "speaker": ln["speaker"], "text": ln["text"],
             "caption": ln["caption"], "delivery": ln["delivery"], "priority": ln["priority"]}
        if ln["speaker"].lower().startswith("smudge"):
            e["fx"] = "smudge"
        voice.append(e)
    ids = [v["id"] for v in voice]
    assert len(ids) == len(set(ids)), "duplicate voice ids"

    words = {"title": "Moonshards", "character": "Oona"}
    for i, r in enumerate(syn["ranks"]):
        words[f"rank.{i + 1}"] = r["name"]
    words.update({
        "tier.0": "SMUDGED!", "tier.0_color": "#B9A8D6", "tier.1": "SILVER MOON", "tier.1_color": "#D9DEE8",
        "tier.2": "WITCH'S HAT!", "tier.2_color": "#FF3B47", "tier.3": "WISHING STAR!", "tier.3_color": "#FFC83D",
        "bowl.common": "Silver Circle", "bowl.common_color": "#D9DEE8", "bowl.common_shape": "circle",
        "bowl.rare": "Red Triangle", "bowl.rare_color": "#FF3B47", "bowl.rare_shape": "triangle",
        "bowl.legend": "Gold Star", "bowl.legend_color": "#FFC83D", "bowl.legend_shape": "star",
    })
    for c in syn["crystal_colours"]:
        words[f"color.{c['id']}"] = c["name"].split(" (")[0]
        words[f"color.{c['id']}_hex"] = c["hex"]
    words.update({
        "caption.attract": "Crystal here", "caption.absorb": "", "caption.awaken": "NEW CRYSTAL!",
        "caption.greet": "Welcome back!", "caption.charge": "PRESS!", "caption.reveal_t0": "SMUDGED!",
        "caption.reveal_t1": "SILVER MOON", "caption.reveal_t2": "WITCH'S HAT!", "caption.reveal_t3": "WISHING STAR!",
        "caption.replay": "ALREADY GLOWING", "caption.nocrystal": "STAR-WISP!", "caption.foreign": "STAR-WISP!",
        "caption.sleepy": "TRY AGAIN", "caption.goodbye": "SEE YOU {next_year}", "caption.milestone": "HOORAY!",
        "caption.circle": "CRYSTAL CIRCLE!", "caption.keeper": "SHH... SECRET", "caption.chapter": "",
        "caption.chapter_a": "OONA'S MOON", "caption.chapter_b": "UH-OH, SMUDGE!", "caption.chapter_c": "MOONSHARDS",
        "caption.chapter_d": "YOUR LIGHT", "caption.chapter_e": "CRYSTAL HERE", "caption.chapter_f": "FILL THE MOON",
        "caption.moonfull": "MOON SAVED!", "caption.moonfull_b": "OFFICIAL TRICKSTER", "caption.recall": "LAST YEAR",
        "ui.place": "Crystal on the stone", "ui.hold": "Hold still...", "ui.power": "GLOW", "ui.counter": "crystals awakened",
        "ui.press": "Press the button!", "ui.or_press": "OR PRESS", "ui.tonight": "TONIGHT",
        "ui.reader_asleep": "READER ASLEEP", "ui.rush": "RUSH",
        "ui.touch": "TOUCH FOR A STAR-WISP", "ui.no_card": "NO TF CARD",
    })

    manifest = {
        "schema": 1, "title": "MOONSHARDS - Oona's Halloween Window", "fps": 13,
        "screen": {"width": 800, "height": 480, "text_band_y": 360, "moon_center": [650, 110]},
        "voice_style": {
            "oona": "Warm, playful old ghost grandmother telling a bedtime story. Smiling, slightly whispery, sing-song, bright on exclamations. Never cackles, never spooky. Quick delivery: each line fits in about 2 seconds.",
            "elevenlabs": "A gentle, older female storyteller voice. Stability ~40%, Similarity ~75%, Style ~30%, Speed 1.05-1.1.",
            "openai_tts": "gpt-4o-mini-tts with voice 'fable' or 'sage' (or 'marin'/'cedar'), the Oona prompt as instructions. Disclose the AI voice on the parent sign.",
            "smudge": "Not a separate actor: record Smudge's line with Oona's voice; the builder pitches it up 5 semitones at 1.15x speed (fx: smudge).",
        },
        "words": words, "stills": stills, "clips": CLIPS, "voice": voice, "sfx": SFX, "music": MUSIC,
    }
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("stills", len(stills), "clips", len(CLIPS), "voice", len(voice), "sfx", len(SFX), "music", len(MUSIC))
    print("MVP stills", sum(s["priority"] == "MVP" for s in stills), "MVP lines", sum(v["priority"] == "MVP" for v in voice))


if __name__ == "__main__":
    main()
