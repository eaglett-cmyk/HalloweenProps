"""gen_asset_list.py -- writes CHATGPT-ASSETS.md (the owner's shopping list for ChatGPT + sound sources)
from the asset manifest, so the document and the manifest never drift apart.

    python -I tools/gen_asset_list.py            # reads assets/manifest.json

"Used by" and the clip table are computed from the manifest's clip recipes (backgrounds, layers,
fallbacks), so they always match what tools/build_assets.py really builds.

Story-neutral: the title, narrator, voice direction and characters come from the manifest (words
"title" / "character" / "color.N", "voice_style", the character master stills). Story-specific prose
(examples, guardrails, suggested order, printables) comes from the manifest's free-text "_asset_guide"
object and sound search words from each sfx/music entry's "_search"; generic text is used without them.
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
OUT = os.path.join(ROOT, "CHATGPT-ASSETS.md")


def load_manifest():
    p = os.path.join(ROOT, "assets", "manifest.json")
    if not os.path.exists(p):
        raise SystemExit("no assets/manifest.json")
    with open(p, encoding="utf-8") as f:
        return json.load(f), "manifest.json"


def clip_stills(c):
    """(required [still ids], optional-or-fallback [still ids]) of one clip recipe."""
    req, opt = [], []
    if c.get("recipe") == "layers":
        bg = c.get("bg")
        bg = {"image": bg} if isinstance(bg, str) else (bg or {})
        if bg.get("image"):
            req.append(bg["image"])
        if bg.get("fallback"):
            opt.append(bg["fallback"])
        for lay in c.get("layers", []):
            sid = lay.get("sprite") or lay.get("image")
            (opt if lay.get("optional") else req).append(sid)
            if lay.get("fallback"):
                opt.append(lay["fallback"])
    else:
        for k in ("image", "bg", "sprite"):
            if isinstance(c.get(k), str):
                req.append(c[k])
        for it in c.get("frames", []):
            req.append(it.get("image") or it.get("sprite"))
    req = list(dict.fromkeys(x for x in req if x))
    opt = [x for x in dict.fromkeys(x for x in opt if x) if x not in req]
    return req, opt


def color_count(words):
    """Crystal colours in use: the run of non-empty color.N_hex from N = 1 (max 6), as the firmware counts."""
    n = 0
    while n < 6 and (words.get("color.%d_hex" % (n + 1)) or "").strip():
        n += 1
    return n


def clip_ids(c, ncol):
    if c.get("variants") == "colors":
        return [c["id"]] + ["%s_%d" % (c["id"], n) for n in range(1, ncol + 1)]
    return [c["id"]]


# Generic search words, used when a sound has no "_search" of its own in the manifest.
SFX_SEARCH = {
    "absorb": "magic whoosh chime, glass chime rise",
    "awaken": "magic shimmer, harp glissando, fairy sparkle",
    "reveal_t0": "cartoon boing, slide whistle down, comedic",
    "reveal_t0_1": "cartoon gag sound, comedic",
    "reveal_t0_2": "cartoon gag sound, comedic",
    "reveal_t0_3": "cartoon gag sound, comedic",
    "reveal_t1": "twinkle chime, magic bell",
    "reveal_t2": "magic spell swirl, sparkle arpeggio",
    "reveal_t3": "harp glissando, soft gong, magical reveal",
    "tap": "ui blip, bubble pop short",
    "button": "button bloop, cartoon pop",
    "charge_full": "level up ding, power up chime",
    "sleepy": "soft bloop descending, gentle cartoon",
    "whoosh": "soft whoosh, air swish",
    "munch": "cute cartoon sound, small giggle",
    "milestone": "party horn kids, celebration cheer",
    "moonfull": "magical swell, triumphant fairy chord",
}
MUSIC_SEARCH = "music box lullaby halloween, celesta spooky cute, cozy halloween loop"


def masters(stills):
    """Character master images: 'character' stills that are not edits but have edits of their own."""
    edited = {s.get("edit_of") for s in stills if s.get("edit_of")}
    return [s for s in stills if s.get("kind") == "character" and not s.get("edit_of") and s["id"] in edited]


def who(still_id):
    """'butterscotch_base' -> 'Butterscotch'."""
    return still_id.split("_")[0].capitalize()


def main():
    m, src = load_manifest()
    stills, voice, sfx, music, clips = m["stills"], m["voice"], m["sfx"], m["music"], m["clips"]
    mvp_st = [s for s in stills if s["priority"] == "MVP"]
    nice_st = [s for s in stills if s["priority"] != "MVP"]
    mvp_v = [v for v in voice if v["priority"] == "MVP"]
    words = m.get("words", {})
    ncol = color_count(words)
    n_built = sum(len(clip_ids(c, ncol)) for c in clips)
    using = {}
    for c in clips:
        req, opt = clip_stills(c)
        for sid in req + opt:
            using.setdefault(sid, []).append(c["id"])
    vs = m.get("voice_style", {}) or {}
    guide = m.get("_asset_guide", {}) or {}
    title = words.get("title") or m.get("title") or "the Crystal Oracle"
    narrator = words.get("character") or "the narrator"
    direction = vs.get(narrator.lower()) or vs.get("direction") or ""
    sidekick = vs.get("sidekick") or vs.get("smudge") or ""
    mains = masters(stills)
    names = [who(s["id"]) for s in mains]
    L = []
    w = L.append
    w(f"# What to make: the ChatGPT images, voice lines, sounds and music for {title}")
    w("")
    w(f"*Generated from `assets/{src}` by `tools/gen_asset_list.py` — edit the manifest, then re-run that script.*")
    w("")
    w("Everything the screen needs, in priority order. **MVP** = needed for Halloween night 2026. **NICE** = makes it better; the screen works without it (missing art is replaced by the screen's own glowing-orb animation, missing voice lines are skipped, missing sounds become built-in chimes).")
    w("")
    w("| What | MVP | Nice-to-have | Made with |")
    w("|---|---|---|---|")
    w(f"| Images (stills) | {len(mvp_st)} | {len(nice_st)} | **ChatGPT** (Images) |")
    w(f"| Voice lines ({narrator}) | {len(mvp_v)} | {len(voice) - len(mvp_v)} | ElevenLabs or OpenAI text-to-speech (ChatGPT can't export audio) |")
    w(f"| Sound effects | {sum(s['priority'] == 'MVP' for s in sfx)} | {sum(s['priority'] != 'MVP' for s in sfx)} | Pixabay / Freesound / ElevenLabs Sound Effects |")
    w(f"| Music bed | {sum(s['priority'] == 'MVP' for s in music)} | {sum(s['priority'] != 'MVP' for s in music)} | Pixabay Music / ElevenLabs Music |")
    w(f"| Animated clips | built automatically from the images ({len(clips)} recipes, {n_built} clips with the colour variants) | | `tools/build_assets.py` |")
    w("")
    examples = guide.get("why_stills") or "characters bobbing and changing expressions, props spinning and rising"
    magic = guide.get("live_magic") or "glows in each kid's crystal colour, sparkles, the meter, the numbers and words"
    w(f"> **Why stills, not videos?** As of October 2026 ChatGPT no longer makes video or audio files (OpenAI shut Sora down in 2026). ChatGPT Images still makes excellent stills, transparent cut-outs and consistent edits of one character. The asset builder turns those stills into the screen's animations: {examples}. The screen adds the live magic on top ({magic}). If you do get real video clips from another tool, drop the MP4s in `assets/source/video/` — the builder can use them too.")
    w("")
    w("## 1. How to make the images in ChatGPT")
    w("")
    chats = ", ".join(["a chat for " + names[0]] + ["one for " + n for n in names[1:]]) if names else "a chat per character"
    w(f"1. **One chat per character.** Start {chats}, one for backgrounds, one for props. Edits stay consistent when the base image is in the same chat.")
    bases = ", ".join(f"`{s['id']}`" for s in mains) or "the character master images"
    w(f"2. **Make the base image first and re-roll until you love it** ({bases}). Every other {'/'.join(names) or 'character'} image is an *edit* of its base — always attach/edit the **base**, never an earlier edit (drift adds up).")
    w("3. **Paste the prompt exactly** (each image's prompt is below). Ask for a **transparent background** on characters and props. If ChatGPT gives you a solid background instead, ask it once more for transparency; if it still can't, a flat pure green (#00FF00) background is fine — the builder removes it.")
    w("4. **Backgrounds are wide (5:3).** Pick the widest landscape shape (1536×1024 is fine — the builder crops it). Keep the bottom quarter dark and empty — the screen writes words and numbers there.")
    ex_ids = [s["id"] for s in mains[:1]] + [s["id"] for s in stills if s.get("kind") == "background"][:1]
    ex = ", ".join(f"`{i}.png`" for i in ex_ids) or "`<id>.png`"
    w(f"5. **Download each image and name it exactly by its id** — {ex}, … — into `crystal_oracle/assets/source/images/`. PNG, JPG and WebP all work.")
    w(f"6. **Run the builder** (`python -I tools/build_assets.py --placeholders` from `crystal_oracle/`), look at `assets/previews/` (or the screen), and tweak. If {narrator}'s glow lands in the wrong spot, adjust that still's `\"anchor\": [x, y]` (fractions of the image) in the manifest." + (" " + guide["anchor_note"] if guide.get("anchor_note") else ""))
    w("7. **Style guardrails** (already inside every prompt): soft storybook-3D look, big round shapes, dark indigo night, one bright subject, **no grain / mist / bokeh / tiny stars / text** (they turn blocky on this screen)." + (" " + guide["guardrails"] if guide.get("guardrails") else ""))
    if guide.get("order"):
        w("")
        w("**Suggested order:** " + guide["order"])
    w("")
    w("## 2. Image list")
    w("")
    w("| # | File name | What it is | Priority | Edit of | Used by |")
    w("|---|---|---|---|---|---|")
    for i, s in enumerate(mvp_st + nice_st, 1):
        u = using.get(s["id"]) or s.get("used_by", [])
        used = ", ".join(u[:6]) + (" …" if len(u) > 6 else "")
        d = s["desc"]
        if d.startswith("Edit of") and ":" in d:
            d = d.split(":", 1)[1].strip()
        if d.startswith("MASTER IMAGE."):
            d = "Master image — " + d[len("MASTER IMAGE."):].strip()
        d = d.replace("|", "/")
        short = d if len(d) <= 90 else d[:88].rsplit(" ", 1)[0] + " …"
        w(f"| {i} | `{s['id']}.png` | {short} | {s['priority']} | {s.get('edit_of', '—')} | {used} |")
    w("")
    w("## 3. The prompts (copy-paste)")
    w("")
    for s in mvp_st + nice_st:
        kind = "transparent" if s["transparent"] else ("print" if s.get("kind") == "print" else "background")
        w(f"### `{s['id']}` — {s['priority']} · {kind}")
        w("")
        if s.get("edit_of"):
            w(f"*Attach / edit: `{s['edit_of']}`.*")
            w("")
        if s.get("refs"):
            w(f"*Attach: {', '.join(s['refs'])}.*")
            w("")
        w("```text")
        w(s["prompt"])
        w("```")
        w("")
    w("### Which pictures each animated clip needs")
    w("")
    w("A clip is built as soon as its **needed** pictures exist; *extras* are used when they exist (optional layers, or stand-ins named by `fallback`). Until then the screen draws its own animation for that moment. After a build, `assets/previews/index.html` shows every clip as four frames.")
    w("")
    w("| Clip | Priority | Needs | Extras / stand-ins | What happens |")
    w("|---|---|---|---|---|")
    for c in clips:
        req, opt = clip_stills(c)
        ids = clip_ids(c, ncol)
        name = f"`{ids[0]}`" + (f" (+ `{ids[1]}` … `{ids[-1]}`, one per crystal colour)" if len(ids) > 1 else "")
        note = (c.get("notes") or c.get("desc") or "").replace("|", "/")
        w(f"| {name} | {c.get('priority', 'NICE')} | {', '.join('`%s`' % x for x in req)} | "
          f"{', '.join('`%s`' % x for x in opt) or '—'} | {note} |")
    w("")
    w(f"## 4. Voice lines ({narrator})")
    w("")
    w("ChatGPT can't export audio, so make these with a text-to-speech tool:")
    w("")
    w("* **ElevenLabs** (easiest; free plan ≈ 10,000 characters/month is enough for every line): Voice Design or a library voice — " + vs.get("elevenlabs", ""))
    w("  * **Let the script record them:** `tools/make_voice_elevenlabs.py` sends every line to ElevenLabs and saves `assets/source/voice/<id>.mp3`, skipping lines that already have a file. Put your API key in `tools/.elevenlabs_key` (git-ignored) or the `ELEVENLABS_API_KEY` environment variable, then from `crystal_oracle/`: `python -I tools/make_voice_elevenlabs.py --list-voices` (pick the voice; add library voices to *My Voices* first) → `--voice \"<name>\" --only attract_call_1 --force` (try one line, listen) → `--priority MVP` (the night-one set) → no option (everything missing) → `python -I tools/build_assets.py --placeholders`. The voice, model and settings are remembered in `tools/.elevenlabs_voice.json`, so re-takes match. README.md → *Voice lines with ElevenLabs* has the details.")
    w("* **OpenAI text-to-speech** (needs an API key; the `gpt-4o-mini-tts` model is being retired on 2027-01-06, so make the lines before then): " + vs.get("openai_tts", ""))
    w("* **Until then** the builder speaks every missing line with Windows' built-in voice (`--placeholders`), so you can test the whole show today.")
    w("")
    if direction:
        w(f"**{narrator}'s voice direction (paste into the voice prompt / style box):**")
        w("")
        w("```text")
        w(direction)
        w("```")
        w("")
    w("Save each line as its own file named by its **id** (any of WAV / MP3 / M4A) into `assets/source/voice/` — e.g. `attract_call_1.mp3`. Make two takes of each and keep the warmer one; trim silence." + (" " + sidekick if sidekick else ""))
    w("")
    w("| id | Line | Delivery | Priority |")
    w("|---|---|---|---|")
    for v in sorted(voice, key=lambda v: (v["priority"] != "MVP",)):
        txt = v["text"].replace("|", "/")
        if v.get("fx"):
            txt = f"*{v.get('speaker') or 'Sidekick'} (record in {narrator}'s voice; the builder pitches it up):* " + txt
        w(f"| `{v['id']}` | {txt} | {v['delivery'].replace('|', '/')} | {v['priority']} |")
    w("")
    cols = ", ".join("%d %s" % (n, words.get("color.%d" % n) or "?") for n in range(1, ncol + 1))
    ab = "`awaken_b_1` … `awaken_b_%d`" % ncol if ncol > 1 else "`awaken_b_1`" if ncol else "`awaken_b_<n>`"
    w("Variant numbers matter: the screen picks a random `_1`, `_2`, … of each group and stops counting at the first gap. " + ab + " are special — the number is the crystal's colour id set on the bench" + (" (%s)." % cols if cols else ".") + " A new colour later = a new `color.N` / `color.N_hex` in the manifest words plus its `awaken_b_N` line.")
    w("")
    swaps = [x.split("=", 1) for x in (words.get("nobutton.voice") or "").split(";") if "=" in x]
    if swaps:
        w("**No big button** (`config.ini` `[station] button = no`, the default): the screen plays the stand-ins instead — "
          + ", ".join("`%s` → %s" % (a.strip(), "`%s`" % b.strip() if b.strip() else "nothing") for a, b in swaps)
          + " (words `nobutton.voice`). The button lines are only needed if a button is added later (`button = yes`).")
        w("")
    w("## 5. Sound effects")
    w("")
    w("Free sources: **Pixabay Sound Effects** (free, no attribution needed), **Freesound.org** (check each sound's licence; CC0 is easiest), or **ElevenLabs Sound Effects** (type the description). Keep them short and gentle. Save as `assets/source/sfx/<id>.wav` (or .mp3). Missing ones are replaced by the screen's built-in chimes.")
    w("")
    w("| id | What it should sound like | Search words | Priority |")
    w("|---|---|---|---|")
    for s in sfx:
        w(f"| `{s['id']}` | {s['desc'].replace('|', '/')} | {s.get('_search') or SFX_SEARCH.get(s['id'], '')} | {s['priority']} |")
    w("")
    w("## 6. Music")
    w("")
    w(f"Save as `assets/source/music/<id>.mp3` (or .wav). It loops under everything and ducks when {narrator} talks.")
    w("")
    w("| id | What | Search words | Priority |")
    w("|---|---|---|---|")
    for s in music:
        w(f"| `{s['id']}` | {s['desc'].replace('|', '/')} | {s.get('_search') or MUSIC_SEARCH} | {s['priority']} |")
    w("")
    w("## 7. Printables (nice to have)")
    w("")
    prints = guide.get("printables") or [
        "**Bowl signs** — print the prop stills that match your bowls, so the bowls match the screen picture-for-picture. Light each sign with a battery LED puck.",
        "**Parent sign** — " + (", ".join("`%s`" % s["id"] for s in stills if s.get("kind") == "print") or "your key art")
        + " with the sign text from STORY.md. Add one line: *\"%s's voice is AI-generated.\"* (OpenAI's and ElevenLabs' terms ask you to disclose AI voices.)" % narrator,
    ]
    for item in prints:
        w("* " + item)
    w("")
    w("## 8. After you have the files")
    w("")
    w("```bash")
    w("cd crystal_oracle")
    w("python -I tools/build_assets.py --check          # what's still missing (writes assets/ASSET-STATUS.md)")
    w("python -I tools/build_assets.py --placeholders   # build sd_card/ (fills gaps with the Windows voice + synth sounds)")
    w("```")
    w("")
    w("Then copy the `sd_card/oracle` folder to the root of the screen's TF card. See `README.md` for the full setup.")
    w("")
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(L))
    print("wrote", OUT, len("\n".join(L)), "chars")


if __name__ == "__main__":
    main()
