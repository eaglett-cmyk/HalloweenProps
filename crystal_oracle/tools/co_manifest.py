"""
============================================================================
 co_manifest.py  -  load + validate assets/manifest.json (Crystal Oracle)
----------------------------------------------------------------------------
 The manifest is the single source of truth for the TF-card assets
 (docs/DESIGN.md section 7, schema in assets/manifest.schema.md). This module:
   * loads it (JSON errors reported with line/column),
   * parses firmware/crystal_oracle/cues.h so visual / voice / sfx / music ids
     are checked against the names the firmware actually asks for,
   * normalises every entry (defaults filled in, recipe parameters checked),
   * finds the source files under assets/source/ and peeks at image headers
     (size, alpha) without any image library,
   * collects ERRORS (build refuses to run), WARNINGS and INFO lines.

 Used by tools/build_assets.py. Python 3 standard library only.
============================================================================
"""
import json
import math
import os
import re
import struct

import co_layers

SCHEMA = 1
PRIORITIES = ("MVP", "NICE")
STILL_KINDS = ("reference", "background", "character", "prop", "fx", "icon", "ui", "print")
RECIPES = ("still", "kenburns", "float", "crossfade", "video", "layers")
CROPS = ("center", "top", "bottom")
PANS = ("center", "up", "down", "left", "right")
SHAPES = ("circle", "triangle", "star")
SYNTHS = ("rise", "sparkle", "chime", "fanfare", "wahwah", "blip", "bloop", "whoosh", "drone")
VOICE_FX = ("smudge",)        # voice "fx": smudge = +5 semitones at a net 1.15x speed (co_media.VOICE_FX)

IMAGE_EXTS = (".png", ".jpg", ".jpeg", ".webp")
AUDIO_EXTS = (".wav", ".mp3", ".m4a", ".ogg", ".flac", ".aac")
VIDEO_EXTS = (".mp4", ".mov", ".m4v", ".webm", ".mkv", ".avi", ".gif")

ID_RE = re.compile(r"^[a-z0-9][a-z0-9_]*$")
# "section.key" or a top-level "key", optionally behind the no-button prefix ("nobutton.caption.charge")
WORD_KEY_RE = re.compile(r"^(?:nobutton\.)?[a-z0-9_]+(\.[a-z0-9_]+)?$")
# No big button (config.ini [station] button = no): the firmware reads "nobutton.<key>" before <key>
# (captions.ini section [nobutton], key "<section>.<key>"), and "nobutton.voice" = "from=to;from=to" swaps
# voice: each side a voice GROUP id or a full LINE id (<group>_<n>); group=group plays group <to> instead,
# line=line plays line <to> when the pick is line <from>; an empty <to> = never play <from>.
NOBUTTON = "nobutton."
NOBUTTON_VOICE = "nobutton.voice"
LINE_ID_RE = re.compile(r"^(.+)_([1-9])$")      # a voice LINE id (no cue group ends in _<digit>)
HEX_RE = re.compile(r"^#[0-9A-Fa-f]{6}$")
COLOR_HEX_RE = re.compile(r"^color\.\d+_hex$")   # crystal colours take a colour SPEC (co_layers.color_spec_problem)
MAX_ID_LEN = 40          # media.h Visual::path is char[80]: "/oracle/clips/" + id + ".omj" must fit easily
CAPTION_MAX = 24         # fx::caption() is laid out for about 24 characters

# Free-text fields allowed anywhere (never interpreted).
FREE_FIELDS = {"desc", "notes", "note", "comment", "prompt"}

# ---------------------------------------------------------------------------
# Firmware cue names. (cues.h constant, [suffixes]); "" = the bare name.
# Voice files are <group>_<n>.wav (n = 1..9); visuals are <id>[_<n>].omj|.jpg.
# ---------------------------------------------------------------------------
_T = ["_t0", "_t1", "_t2", "_t3"]
_R = ["_r2", "_r3", "_r4", "_r5", "_r6", "_r7"]
VOICE_GROUPS = [
    ("ATTRACT_CALL", [""]),
    ("CHAPTER", ["_a", "_b", "_c", "_d", "_e", "_f"]),
    ("AWAKEN", ["", "_a", "_b", "_c"]),
    ("GREET", _R),
    ("RECALL", _T),
    ("CHARGE", [""]),
    ("REVEAL", _T),
    ("BOWL", ["_common", "_rare", "_legend"]),
    ("REPLAY", [""]),
    ("NOCRYSTAL", [""]),
    ("FOREIGN", [""]),
    ("SLEEPY", [""]),
    ("GOODBYE", [""]),
    ("MILESTONE", [""]),
    ("CIRCLE", [""]),
    ("KEEPER", [""]),
    ("RANKUP", _R),
]
VISUALS = [
    ("ATTRACT", [""]),
    ("CHAPTER", ["", "_a", "_b", "_c", "_d", "_e", "_f"]),
    ("ABSORB", [""]),
    ("AWAKEN", ["", "_a", "_b", "_c"]),
    ("GREET", [""] + _R),
    ("RECALL", [""] + _T),
    ("CHARGE", [""]),
    ("REVEAL", [""] + _T),
    ("BOWL", ["", "_common", "_rare", "_legend"]),
    ("REPLAY", [""]),
    ("NOCRYSTAL", [""]),
    ("SLEEPY", [""]),
    ("FOREIGN", [""]),
    ("GOODBYE", [""]),
    ("MILESTONE", [""]),
    ("CIRCLE", [""]),
    ("KEEPER", [""]),
    ("RANKUP", [""] + _R),
]
# Names the story firmware plays that cues.h may not define yet (a firmware revision
# adds RUSH / MOONFULL / ATTRACT_FULL). They are accepted whether or not cues.h has
# them, so --check passes either way; if cues.h defines the constant, its value counts too.
OPTIONAL_VOICE_GROUPS = [("RUSH", "rush", _T), ("MOONFULL", "moonfull", ["_a", "_b"])]
OPTIONAL_VISUALS = [("ATTRACT_FULL", "attract_full", [""]), ("MOONFULL", "moonfull", [""])]
EXTRA_SFX = ["munch", "milestone", "moonfull"]     # + any known sfx id with a _1.._9 variant suffix
EXTRA_MUSIC = ["idle_full"]
# Cues listed in the coverage table of ASSET-STATUS.md (what the show plays).
COVERAGE_VISUALS = ["attract", "chapter", "absorb", "awaken", "greet", "recall", "charge",
                    "reveal_t0", "reveal_t1", "reveal_t2", "reveal_t3", "replay", "nocrystal",
                    "sleepy", "foreign", "goodbye", "milestone", "circle", "keeper", "moonfull"]


class Report:
    """Collected findings. Errors stop a build; warnings and info are printed / reported."""

    def __init__(self):
        self.errors, self.warnings, self.infos = [], [], []

    def error(self, where, msg):
        self.errors.append("%s: %s" % (where, msg) if where else msg)

    def warn(self, where, msg):
        self.warnings.append("%s: %s" % (where, msg) if where else msg)

    def info(self, where, msg):
        self.infos.append("%s: %s" % (where, msg) if where else msg)


# ---------------------------------------------------------------------------
# cues.h
# ---------------------------------------------------------------------------
_CONST_RE = re.compile(r'constexpr\s+const\s+char\s*\*\s*(\w+)\s*=\s*"([^"]*)"\s*;')
_ARRAY_RE = re.compile(r'constexpr\s+const\s+char\s*\*\s*(\w+)\s*\[\s*\d*\s*\]\s*=\s*\{([^}]*)\}\s*;')


def parse_cues_h(path):
    """Returns {CONST_NAME: "value" or ["v1", ...]} from cues.h (empty dict if unreadable)."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
    except OSError:
        return {}
    text = re.sub(r"//[^\n]*", "", text)          # drop line comments (they contain quotes)
    out = {}
    for name, val in _CONST_RE.findall(text):
        out[name] = val
    for name, body in _ARRAY_RE.findall(text):
        out[name] = re.findall(r'"([^"]*)"', body)
    return out


class Cues:
    """Firmware names derived from cues.h (falls back to lowercase constant names)."""

    def __init__(self, consts, report):
        self.consts = consts
        if not consts:
            report.warn("cues.h", "not found or unreadable - using built-in cue names")

        def base(name):
            v = consts.get(name)
            if isinstance(v, str):
                return v
            if consts:
                report.warn("cues.h", "no constant %s any more - the tool's cue table needs updating" % name)
            return name.lower()

        self.voice_groups = []                 # concrete group names, in show order
        for const, suffixes in VOICE_GROUPS:
            b = base(const)
            self.voice_groups += [b + s for s in suffixes]
        self.visuals = []
        for const, suffixes in VISUALS:
            b = base(const)
            self.visuals += [b + s for s in suffixes]

        def optional(name, literal):
            v = consts.get(name)
            return list(dict.fromkeys([literal] + ([v] if isinstance(v, str) else [])))
        for const, literal, suffixes in OPTIONAL_VOICE_GROUPS:
            for b in optional(const, literal):
                self.voice_groups += [b + s for s in suffixes if b + s not in self.voice_groups]
        for const, literal, suffixes in OPTIONAL_VISUALS:
            for b in optional(const, literal):
                self.visuals += [b + s for s in suffixes if b + s not in self.visuals]
        sfx = []
        music = []
        for name, v in consts.items():
            vals = v if isinstance(v, list) else [v]
            if name.startswith("SFX_"):
                sfx += vals
            elif name.startswith("MUSIC_"):
                music += vals
        self.sfx = sfx or ["absorb", "awaken", "reveal_t0", "reveal_t1", "reveal_t2", "reveal_t3",
                           "tap", "charge_full", "sleepy", "button", "whoosh"]
        self.sfx = list(dict.fromkeys(self.sfx + EXTRA_SFX))
        self.music = list(dict.fromkeys((music or ["idle"]) + EXTRA_MUSIC))
        self._visual_re = re.compile(r"^(?:%s)(?:_[1-9])?$" % "|".join(re.escape(v) for v in self.visuals))
        self._sfx_re = re.compile(r"^(?:%s)(?:_[1-9])?$" % "|".join(re.escape(v) for v in self.sfx))

    def is_visual(self, clip_id):
        return bool(self._visual_re.match(clip_id))

    def is_sound(self, key, sid):
        """sfx: a known id, optionally with a _1.._9 variant suffix (reveal_t0_2 = gag 2's sting)."""
        if key == "sfx":
            return bool(self._sfx_re.match(sid))
        return sid in self.music


# ---------------------------------------------------------------------------
# Image header peeking (no PIL): (width, height, has_alpha, format) or None
# ---------------------------------------------------------------------------
def probe_image(path):
    try:
        with open(path, "rb") as f:
            head = f.read(64)
            if head[:8] == b"\x89PNG\r\n\x1a\n":
                w, h = struct.unpack(">II", head[16:24])
                ctype = head[25]
                alpha = ctype in (4, 6)
                if not alpha and ctype in (0, 2, 3):          # tRNS gives palette/grey/rgb images alpha
                    f.seek(8)
                    while True:
                        hdr = f.read(8)
                        if len(hdr) < 8:
                            break
                        ln, typ = struct.unpack(">I4s", hdr)
                        if typ == b"tRNS":
                            alpha = True
                            break
                        if typ in (b"IDAT", b"IEND"):
                            break
                        f.seek(ln + 4, 1)
                return (w, h, alpha, "png")
            if head[:4] == b"RIFF" and head[8:12] == b"WEBP":
                chunk = head[12:16]
                if chunk == b"VP8X":
                    flags = head[20]
                    w = 1 + int.from_bytes(head[24:27], "little")
                    h = 1 + int.from_bytes(head[27:30], "little")
                    return (w, h, bool(flags & 0x10), "webp")
                if chunk == b"VP8L":
                    b = int.from_bytes(head[21:25], "little")
                    w = (b & 0x3FFF) + 1
                    h = ((b >> 14) & 0x3FFF) + 1
                    return (w, h, bool((b >> 28) & 1), "webp")
                if chunk == b"VP8 ":
                    w, h = struct.unpack("<HH", head[26:30])
                    return (w & 0x3FFF, h & 0x3FFF, False, "webp")
                return None
            if head[:2] == b"\xff\xd8":
                f.seek(2)
                while True:
                    b = f.read(1)
                    while b and b != b"\xff":
                        b = f.read(1)
                    while b == b"\xff":
                        b = f.read(1)
                    if not b:
                        return None
                    m = b[0]
                    if m in (0xD8, 0x01) or 0xD0 <= m <= 0xD7:
                        continue
                    ln_b = f.read(2)
                    if len(ln_b) < 2:
                        return None
                    ln = struct.unpack(">H", ln_b)[0]
                    if 0xC0 <= m <= 0xCF and m not in (0xC4, 0xC8, 0xCC):
                        seg = f.read(5)
                        _, h, w = struct.unpack(">BHH", seg)
                        return (w, h, False, "jpeg")
                    f.seek(ln - 2, 1)
    except (OSError, struct.error, IndexError):
        return None
    return None


# ---------------------------------------------------------------------------
# Source discovery
# ---------------------------------------------------------------------------
class SourceDir:
    """Case-insensitive stem -> [files] index of one assets/source/<kind> folder."""

    def __init__(self, path, exts):
        self.path = path
        self.exts = exts
        self.by_stem = {}
        self.by_name = {}
        if os.path.isdir(path):
            for name in sorted(os.listdir(path)):
                full = os.path.join(path, name)
                if not os.path.isfile(full):
                    continue
                stem, ext = os.path.splitext(name)
                self.by_name[name.lower()] = full
                if ext.lower() in exts:
                    self.by_stem.setdefault(stem.lower(), []).append(full)

    def find(self, stem):
        """Newest matching file for a stem, plus the other candidates (for a warning)."""
        c = self.by_stem.get(stem.lower(), [])
        if not c:
            return None, []
        c = sorted(c, key=lambda p: os.path.getmtime(p), reverse=True)
        return c[0], c[1:]

    def find_name(self, name):
        """A file named exactly (or, without extension, by stem)."""
        full = self.by_name.get(name.lower())
        if full:
            return full, []
        stem, ext = os.path.splitext(name)
        if not ext:
            return self.find(stem)
        return None, []


# ---------------------------------------------------------------------------
# Validation helpers
# ---------------------------------------------------------------------------
def _num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def _int(v):
    return isinstance(v, int) and not isinstance(v, bool)


def _check_fields(where, d, allowed, report):
    for k in d:
        if k not in allowed and k not in FREE_FIELDS and not k.startswith("_"):
            report.warn(where, "unknown field '%s' (typo?) - ignored" % k)


def _priority(where, d, report):
    p = d.get("priority")
    if p is None:
        report.warn(where, "no priority - treated as NICE")
        return "NICE"
    if p not in PRIORITIES:
        report.error(where, "priority must be MVP or NICE, not %r" % (p,))
        return "NICE"
    return p


def _check_id(where, i, report):
    if not isinstance(i, str) or not ID_RE.match(i):
        report.error(where, "id %r must be lowercase letters, digits and _ (it becomes a file name on the TF card)" % (i,))
        return False
    if len(i) > MAX_ID_LEN:
        report.error(where, "id is %d characters (max %d)" % (len(i), MAX_ID_LEN))
        return False
    return True


def _seconds(where, d, key, default, report, lo=0.05, hi=60.0):
    v = d.get(key, default)
    if v is None:
        return None
    if not _num(v) or not lo <= v <= hi:
        report.error(where, "'%s' must be a number of seconds between %g and %g (got %r)" % (key, lo, hi, v))
        return default
    return float(v)


class Model:
    """The validated manifest."""

    def __init__(self):
        self.path = ""
        self.title = ""
        self.fps = 13.0
        self.words = {}
        self.voice_style = {}
        self.extra_groups = []
        self.voice_subst = []     # [(from, to)] from words nobutton.voice (to "" = never play from)
        self.stills = {}          # id -> dict (insertion order kept)
        self.clips = []
        self.voice = []
        self.sfx = []
        self.music = []
        self.cues = None


def load_json(path, report):
    try:
        with open(path, "r", encoding="utf-8-sig") as f:
            return json.load(f)
    except FileNotFoundError:
        report.error(path, "manifest not found")
    except json.JSONDecodeError as e:
        report.error(path, "invalid JSON at line %d column %d: %s" % (e.lineno, e.colno, e.msg))
    except (OSError, UnicodeDecodeError) as e:
        report.error(path, "cannot read: %s" % e)
    return None


# ---------------------------------------------------------------------------
# Recipe normalisation
# ---------------------------------------------------------------------------
def _still_ref(where, d, key, model, report, required=True):
    v = d.get(key)
    if v is None:
        if required:
            report.error(where, "needs '%s' (a still id)" % key)
        return None
    if not isinstance(v, str) or v not in model.stills:
        report.error(where, "'%s' = %r is not a still id in the manifest" % (key, v))
        return None
    return v


def _xpos(where, v, report):
    if v == "center":
        return "center"
    if _int(v) and -800 <= v <= 1600:
        return v
    report.error(where, "x must be \"center\" or a pixel number (got %r)" % (v,))
    return "center"


def _ypos(where, v, report):
    if _int(v) and -480 <= v <= 960:
        return v
    report.error(where, "y must be a pixel number (top of the sprite; got %r)" % (v,))
    return 40


def _sprite_h(where, v, report):
    if _int(v) and 16 <= v <= 960:
        return v
    report.error(where, "sprite_height must be a whole number of pixels 16..960 (got %r)" % (v,))
    return 360


def _float_spec(where, v, report):
    if not isinstance(v, dict):
        report.error(where, "float must be an object {amp, period}")
        return None
    _check_fields(where + ".float", v, {"amp", "period"}, report)
    amp = v.get("amp", 8)
    per = v.get("period", 3.0)
    if not _num(amp) or not 0 <= amp <= 200:
        report.error(where, "float.amp must be 0..200 px")
        amp = 8
    if not _num(per) or not 0.2 <= per <= 60:
        report.error(where, "float.period must be 0.2..60 s")
        per = 3.0
    return {"amp": float(amp), "period": float(per)}


def _loop_exact(where, frames, fps, period, report):
    cyc = frames / (period * fps)
    if abs(cyc - round(cyc)) > 1e-6:
        report.warn(where, "not loop-exact: %d frames at %g fps = %.3f bob periods "
                           "(make seconds a multiple of period, and period*fps a whole number)" % (frames, fps, cyc))


def normalise_clip(c, model, report):
    where = "clips[%s]" % c.get("id", "?")
    recipe = c.get("recipe")
    out = {"id": c.get("id"), "recipe": recipe, "priority": _priority(where, c, report),
           "desc": c.get("desc", ""), "refs": [], "video": None}
    common = {"id", "recipe", "priority", "fps", "q"}
    fps = c.get("fps", model.fps)
    if not _num(fps) or not 1 <= fps <= 60:
        report.error(where, "fps must be 1..60")
        fps = model.fps
    out["fps"] = float(fps)
    qdef = 3 if recipe == "still" else 5
    q = c.get("q", qdef)
    if not _int(q) or not 2 <= q <= 31:
        report.error(where, "q must be a whole number 2..31 (lower = better quality, bigger frames)")
        q = qdef
    out["q"] = q
    p = {}
    if recipe not in RECIPES:
        report.error(where, "recipe must be one of %s (got %r)" % ("/".join(RECIPES), recipe))
        return None
    if recipe == "still":
        _check_fields(where, c, common | {"image"}, report)
        p["image"] = _still_ref(where, c, "image", model, report)
        out["refs"] = [p["image"]]
        out["ext"] = ".jpg"
    elif recipe == "kenburns":
        _check_fields(where, c, common | {"image", "seconds", "zoom", "pan"}, report)
        p["image"] = _still_ref(where, c, "image", model, report)
        p["seconds"] = _seconds(where, c, "seconds", 4.0, report)
        z = c.get("zoom", [1.0, 1.15])
        if (not isinstance(z, list) or len(z) != 2 or not all(_num(v) and 1.0 <= v <= 4.0 for v in z)):
            report.error(where, "zoom must be [from, to] with values 1.0..4.0 (1 = whole picture)")
            z = [1.0, 1.15]
        p["zoom"] = [float(z[0]), float(z[1])]
        p["pan"] = c.get("pan", "center")
        if p["pan"] not in PANS:
            report.error(where, "pan must be one of %s" % "/".join(PANS))
            p["pan"] = "center"
        if p["pan"] != "center" and min(p["zoom"]) < 1.05:
            report.warn(where, "pan '%s' needs zoom > 1 at both ends to have room to move (zoom %s)" % (p["pan"], p["zoom"]))
        out["refs"] = [p["image"]]
        out["ext"] = ".omj"
        out["frames"] = max(2, int(round(p["seconds"] * out["fps"])))
    elif recipe == "float":
        _check_fields(where, c, common | {"bg", "sprite", "seconds", "sprite_height", "x", "y", "amp", "period"}, report)
        p["bg"] = _still_ref(where, c, "bg", model, report)
        p["sprite"] = _still_ref(where, c, "sprite", model, report)
        p["sprite_height"] = _sprite_h(where, c.get("sprite_height", 360), report)
        p["x"] = _xpos(where, c.get("x", "center"), report)
        p["y"] = _ypos(where, c.get("y", 40), report)
        fs = _float_spec(where, {"amp": c.get("amp", 8), "period": c.get("period", 3.0)}, report)
        p["amp"], p["period"] = fs["amp"], fs["period"]
        default_s = p["period"] * max(1, round(4.0 / p["period"]))
        p["seconds"] = _seconds(where, c, "seconds", default_s, report)
        out["frames"] = max(1, int(round(p["seconds"] * out["fps"])))
        _loop_exact(where, out["frames"], out["fps"], p["period"], report)
        out["refs"] = [p["bg"], p["sprite"]]
        out["ext"] = ".omj"
    elif recipe == "crossfade":
        _check_fields(where, c, common | {"bg", "frames", "fade", "loop", "float", "sprite_height", "x", "y"}, report)
        p["bg"] = _still_ref(where, c, "bg", model, report, required=False)
        p["fade"] = _seconds(where, c, "fade", 0.5, report, lo=0.0, hi=10.0)
        p["loop"] = c.get("loop", False)
        if not isinstance(p["loop"], bool):
            report.error(where, "loop must be true or false")
            p["loop"] = False
        p["float"] = _float_spec(where, c["float"], report) if c.get("float") is not None else None
        sh = _sprite_h(where, c.get("sprite_height", 360), report)
        x0 = _xpos(where, c.get("x", "center"), report)
        y0 = _ypos(where, c.get("y", 40), report)
        items = []
        fr = c.get("frames")
        if not isinstance(fr, list) or not fr:
            report.error(where, "frames must be a non-empty list of {image|sprite, hold}")
            fr = []
        for k, it in enumerate(fr):
            w2 = "%s.frames[%d]" % (where, k)
            if not isinstance(it, dict):
                report.error(w2, "must be an object")
                continue
            _check_fields(w2, it, {"image", "sprite", "hold", "sprite_height", "x", "y"}, report)
            if ("image" in it) == ("sprite" in it):
                report.error(w2, "needs exactly one of 'image' (full frame) or 'sprite' (over bg)")
                continue
            kind = "image" if "image" in it else "sprite"
            ref = _still_ref(w2, it, kind, model, report)
            hold = _seconds(w2, it, "hold", 1.0, report, lo=0.0, hi=60.0)
            item = {"kind": kind, "still": ref, "hold": hold,
                    "sprite_height": _sprite_h(w2, it.get("sprite_height", sh), report),
                    "x": _xpos(w2, it.get("x", x0), report), "y": _ypos(w2, it.get("y", y0), report)}
            if kind == "sprite" and not p["bg"]:
                report.warn(w2, "sprite without a crossfade 'bg' - composited over black")
            items.append(item)
            if ref:
                out["refs"].append(ref)
        p["items"] = items
        if p["bg"]:
            out["refs"].insert(0, p["bg"])
        fps_ = out["fps"]
        H = [max(1, int(round(i["hold"] * fps_))) for i in items]
        F = int(round(p["fade"] * fps_))
        if p["fade"] > 0 and F == 0:
            F = 1
        if items:
            n = sum(H) + F * (len(items) if p["loop"] else len(items) - 1)
            if len(items) == 1 and p["loop"]:
                n = H[0]                   # one picture: nothing to fade to
        else:
            n = 0
        out["frames"] = n
        p["hold_frames"], p["fade_frames"] = H, F
        if p["float"] and items:
            if not any(i["kind"] == "sprite" for i in items):
                report.warn(where, "float only moves sprites, and this crossfade has none")
            elif p["loop"]:
                _loop_exact(where, n, fps_, p["float"]["period"], report)
        if p["loop"] and len(items) == 1:
            report.info(where, "loop with a single frame is just a still")
        out["ext"] = ".omj"
    elif recipe == "layers":
        out["variant_clips"] = co_layers.normalise(c, model, report, where, out)
        return out
    elif recipe == "video":
        _check_fields(where, c, common | {"source", "start", "seconds", "loop_fade_frames", "crop", "denoise"}, report)
        src = c.get("source")
        if not isinstance(src, str) or not src or "/" in src or "\\" in src:
            report.error(where, "source must be a file name in assets/source/video")
            src = None
        p["source"] = src
        out["video"] = src
        p["start"] = _seconds(where, c, "start", 0.0, report, lo=0.0, hi=3600.0)
        p["seconds"] = _seconds(where, c, "seconds", None, report, lo=0.1, hi=60.0)
        lf = c.get("loop_fade_frames", 0)
        if not _int(lf) or not 0 <= lf <= 120:
            report.error(where, "loop_fade_frames must be 0..120")
            lf = 0
        p["loop_fade_frames"] = lf
        p["crop"] = c.get("crop", "center")
        if p["crop"] not in CROPS:
            report.error(where, "crop must be one of %s" % "/".join(CROPS))
            p["crop"] = "center"
        dn = c.get("denoise", "4:3:8:6")
        if dn is not False and not (isinstance(dn, str) and re.match(r"^[0-9.]+(:[0-9.]+){0,3}$", dn)):
            report.error(where, "denoise must be hqdn3d parameters like \"4:3:8:6\" or false")
            dn = "4:3:8:6"
        p["denoise"] = dn
        if p["seconds"] is None:
            report.warn(where, "no 'seconds' - the whole source after 'start' is used")
        out["ext"] = ".omj"
    out["params"] = p
    out["refs"] = [r for r in out["refs"] if r]
    # transparency sanity: full-frame uses of sprites / sprite uses of opaque art
    for key in ("image",):
        sid = p.get(key)
        if sid and model.stills.get(sid, {}).get("transparent"):
            report.warn(where, "full-frame image '%s' is a transparent still - it will be flattened onto black" % sid)
    if p.get("bg") and model.stills.get(p["bg"], {}).get("transparent"):
        report.warn(where, "bg '%s' is a transparent still - flattened onto black" % p["bg"])
    if p.get("sprite") and not model.stills.get(p["sprite"], {}).get("transparent", True):
        report.warn(where, "sprite '%s' is not marked transparent - it will cover the background as a rectangle" % p["sprite"])
    return out


# ---------------------------------------------------------------------------
# Whole-manifest validation
# ---------------------------------------------------------------------------
def validate(data, path, cues_consts, report):
    m = Model()
    m.path = path
    m.cues = Cues(cues_consts, report)
    if not isinstance(data, dict):
        report.error(path, "the manifest must be a JSON object")
        return m
    _check_fields("manifest", data, {"schema", "title", "fps", "words", "voice_style", "extra_voice_groups",
                                     "screen", "stills", "clips", "voice", "sfx", "music"}, report)
    if data.get("schema") != SCHEMA:
        report.error("manifest", "schema must be %d (got %r)" % (SCHEMA, data.get("schema")))
    m.title = data.get("title", "") if isinstance(data.get("title", ""), str) else ""
    fps = data.get("fps", 13)
    if not _num(fps) or not 1 <= fps <= 60:
        report.error("manifest", "fps must be 1..60")
        fps = 13
    m.fps = float(fps)
    if abs(m.fps - 13) > 1e-9:
        report.warn("manifest", "fps %g: the panel refreshes at 39.02 Hz, so 13 fps (every 3rd refresh) is the "
                                "tested rate; 19.5 or 39 only work for tiny clips" % m.fps)
    m.voice_style = data.get("voice_style", {}) if isinstance(data.get("voice_style", {}), dict) else {}
    ex = data.get("extra_voice_groups", [])
    if not isinstance(ex, list) or not all(isinstance(x, str) for x in ex):
        report.error("manifest", "extra_voice_groups must be a list of regular expressions")
        ex = []
    for x in ex:
        try:
            m.extra_groups.append(re.compile(r"^(?:%s)$" % x))
        except re.error as e:
            report.error("extra_voice_groups", "bad pattern %r: %s" % (x, e))

    _validate_words(data.get("words", {}), m, report)

    def lst(key):
        v = data.get(key, [])
        if not isinstance(v, list):
            report.error("manifest", "'%s' must be a list" % key)
            return []
        good = []
        for k, e in enumerate(v):
            if not isinstance(e, dict):
                report.error("%s[%d]" % (key, k), "must be an object")
            else:
                good.append(e)
        return good

    # ---- stills ----
    for s in lst("stills"):
        where = "stills[%s]" % s.get("id", "?")
        if not _check_id(where, s.get("id"), report):
            continue
        if s["id"] in m.stills:
            report.error(where, "duplicate still id")
            continue
        _check_fields(where, s, {"id", "kind", "transparent", "priority", "used_by", "crop", "key",
                                 "edit_of", "refs", "anchor"}, report)
        kind = s.get("kind")
        if kind not in STILL_KINDS:
            report.error(where, "kind must be one of %s (got %r)" % ("/".join(STILL_KINDS), kind))
        tr = s.get("transparent", False)
        if not isinstance(tr, bool):
            report.error(where, "transparent must be true or false")
            tr = False
        crop = s.get("crop", "center")
        if crop not in CROPS:
            report.error(where, "crop must be one of %s" % "/".join(CROPS))
            crop = "center"
        key = s.get("key", "auto")
        if key not in ("auto", False) and not (isinstance(key, str) and HEX_RE.match(key)):
            report.error(where, "key must be \"auto\", false or a colour like \"#000000\"")
            key = "auto"
        ub = s.get("used_by", [])
        if not isinstance(ub, list):
            report.error(where, "used_by must be a list")
            ub = []
        anc = s.get("anchor")
        if anc is not None and (not isinstance(anc, list) or len(anc) != 2
                                or not all(_num(v) and -0.5 <= v <= 1.5 for v in anc)):
            report.error(where, "anchor must be [x, y] as fractions of the picture (0..1), e.g. [0.5, 0.7]")
            anc = None
        m.stills[s["id"]] = {"id": s["id"], "kind": kind, "transparent": tr, "priority": _priority(where, s, report),
                             "desc": s.get("desc", ""), "prompt": s.get("prompt", ""), "used_by": ub,
                             "crop": crop, "key": key, "src": None, "probe": None,
                             "edit_of": s.get("edit_of"), "refs": s.get("refs", []), "anchor": anc}
    # informational: ChatGPT edit chains ("edit_of": the still this one is an edit of)
    for st in m.stills.values():
        if st["edit_of"] is not None and st["edit_of"] not in m.stills:
            report.warn("stills[%s]" % st["id"], "edit_of %r is not a still id" % (st["edit_of"],))

    # ---- clips ----
    seen = set()
    for c in lst("clips"):
        where = "clips[%s]" % c.get("id", "?")
        if not _check_id(where, c.get("id"), report):
            continue
        if c["id"] in seen:
            report.error(where, "duplicate clip id")
            continue
        seen.add(c["id"])
        n = normalise_clip(c, m, report)
        if n:
            variants = n.pop("variant_clips", None) or []
            for x in [n] + variants:
                if x is not n:
                    if not _check_id("clips[%s]" % x["id"], x["id"], report):
                        continue
                    if x["id"] in seen:
                        report.error("clips[%s]" % x["id"], "duplicate clip id (also made by %s's variants)" % n["id"])
                        continue
                    seen.add(x["id"])
                m.clips.append(x)
                if not m.cues.is_visual(x["id"]):
                    report.warn("clips[%s]" % x["id"], "the firmware never asks for a visual called '%s' (cues.h) - "
                                                        "check the spelling" % x["id"])
    for st in m.stills.values():
        st["clips"] = [c["id"] for c in m.clips if st["id"] in c["refs"]]
        if not st["clips"] and st["kind"] not in ("reference", "print", "icon", "ui") and not st["used_by"]:
            report.info("stills[%s]" % st["id"], "not used by any clip")

    # ---- voice ----
    known_groups = set(m.cues.voice_groups)
    # groups played only as no-button stand-ins (words nobutton.voice "charge=charge_tap", "x_2=attract_tap_1")
    subst_groups = {voice_group(b) for _, b in m.voice_subst if b}
    seen = set()
    groups = {}
    for v in lst("voice"):
        where = "voice[%s]" % v.get("id", "?")
        if not _check_id(where, v.get("id"), report):
            continue
        if v["id"] in seen:
            report.error(where, "duplicate voice id")
            continue
        seen.add(v["id"])
        _check_fields(where, v, {"id", "group", "variant", "speaker", "text", "caption", "delivery", "priority", "fx"}, report)
        fx = v.get("fx")
        if fx is not None and fx not in VOICE_FX:
            report.warn(where, "fx %r is unknown (known: %s) - ignored" % (fx, ", ".join(VOICE_FX)))
            fx = None
        mt = re.match(r"^(.+)_([1-9])$", v["id"])
        if not mt:
            report.error(where, "voice id must be <group>_<n> with n = 1..9 (e.g. attract_call_1)")
            continue
        group, variant = mt.group(1), int(mt.group(2))
        if v.get("group", group) != group:
            report.error(where, "group %r does not match the id (expected %r)" % (v.get("group"), group))
        if v.get("variant", variant) != variant:
            report.error(where, "variant %r does not match the id (expected %d)" % (v.get("variant"), variant))
        if group not in known_groups and group not in subst_groups:
            if any(rx.match(group) for rx in m.extra_groups):
                report.info(where, "group '%s' is an extra group the firmware does not use yet" % group)
            else:
                report.error(where, "unknown voice group '%s' (not a cue group in cues.h; add it to "
                                    "extra_voice_groups if it is for a future firmware, or name it as a "
                                    "stand-in in the words nobutton.voice)" % group)
        text = v.get("text", "")
        if not isinstance(text, str) or not text.strip():
            report.warn(where, "no text - a TTS placeholder cannot be made")
            text = ""
        cap = v.get("caption", "")
        if isinstance(cap, str) and len(cap) > CAPTION_MAX:
            report.warn(where, "caption is %d characters (the caption band fits about %d)" % (len(cap), CAPTION_MAX))
        m.voice.append({"id": v["id"], "group": group, "variant": variant, "speaker": v.get("speaker", ""),
                        "text": text, "caption": cap if isinstance(cap, str) else "", "fx": fx,
                        "delivery": v.get("delivery", ""), "priority": _priority(where, v, report), "src": None})
        groups.setdefault(group, set()).add(variant)
    for g, vs in sorted(groups.items()):
        top = max(vs)
        gaps = [n for n in range(1, top + 1) if n not in vs]
        if gaps:
            dead = sorted(n for n in vs if n > gaps[0])
            report.warn("voice[%s]" % g, "variant(s) %s missing - the firmware stops counting at the first gap, "
                                         "so %s will never play" % (gaps, ["%s_%d" % (g, n) for n in dead]))
    _check_voice_subst(m, groups, seen, report)
    # awaken_b is colour-indexed: awaken_b_<n> names crystal colour n (words color.n / color.n_hex)
    if "awaken_b" in groups and any(k.startswith("color.") for k in m.words):
        ncol = co_layers.color_count(m.words)
        extra = sorted(n for n in groups["awaken_b"] if n > ncol)
        if extra:
            report.warn("voice[awaken_b]", "%s: only %d crystal colour(s) are defined (words color.N_hex), so these "
                                           "lines never play" % (", ".join("awaken_b_%d" % n for n in extra), ncol))
        missing = [n for n in range(1, ncol + 1) if n not in groups["awaken_b"]]
        if missing:
            report.warn("voice[awaken_b]", "no %s - %s's crystals are awakened without their colour line"
                        % (", ".join("awaken_b_%d" % n for n in missing),
                           " / ".join(m.words.get("color.%d" % n, "colour %d" % n) for n in missing)))
    # clip variants that can never show (visual <id>_<n> is used with voice variant n)
    for c in m.clips:
        mt = re.match(r"^(.+)_([1-9])$", c["id"])
        if mt and mt.group(1) in groups and int(mt.group(2)) not in groups[mt.group(1)]:
            report.warn("clips[%s]" % c["id"], "visual variant %s exists but voice %s_%s does not - it only shows "
                                               "when that voice variant plays" % (mt.group(2), mt.group(1), mt.group(2)))

    # ---- sfx / music ----
    for key, known, dest in (("sfx", m.cues.sfx, m.sfx), ("music", m.cues.music, m.music)):
        seen = set()
        for e in lst(key):
            where = "%s[%s]" % (key, e.get("id", "?"))
            if not _check_id(where, e.get("id"), report):
                continue
            if e["id"] in seen:
                report.error(where, "duplicate %s id" % key)
                continue
            seen.add(e["id"])
            _check_fields(where, e, {"id", "seconds", "priority", "synth", "gain"}, report)
            if not m.cues.is_sound(key, e["id"]):
                report.warn(where, "the firmware never loads %s '%s' (cues.h knows: %s%s)"
                            % (key, e["id"], ", ".join(known), ", each optionally + _1.._9" if key == "sfx" else ""))
            syn = e.get("synth")
            if syn is not None and syn not in SYNTHS:
                report.warn(where, "unknown synth '%s' - placeholder uses the default for this id" % syn)
                syn = None
            dest.append({"id": e["id"], "desc": e.get("desc", ""), "priority": _priority(where, e, report),
                         "seconds": _seconds(where, e, "seconds", None, report, lo=0.02, hi=600.0),
                         "synth": syn, "src": None})
    return m


# Story-feature words the firmware parses (show.cpp; manifest.schema.md "Story features").
METER_STYLES = ("moon", "lantern")
FX_KINDS = ("poof", "sparkles", "burst", "none")
_INT_RE = re.compile(r"^-?\d+$")


def _ints(text):
    parts = [p for p in re.split(r"[\s,]+", text.strip()) if p]
    return [int(p) for p in parts] if parts and all(_INT_RE.match(p) for p in parts) else None


def story_word_problem(k, v):
    """None if a story-feature word (meter.*, fx.*) is well-formed, else what's wrong with it."""
    if k == "meter.style":
        if v.strip().lower() not in METER_STYLES:
            return "must be one of %s" % "/".join(METER_STYLES)
    elif k == "meter.pos":
        n = _ints(v)
        if not n or len(n) not in (2, 3):
            return "must be \"x,y\" or \"x,y,r\" in screen pixels (got %r)" % v
        if not (0 <= n[0] < 800 and 0 <= n[1] < 480) or (len(n) == 3 and not 8 <= n[2] <= 200):
            return "x must be 0..799, y 0..479 and r 8..200 (got %r)" % v
    elif k == "meter.color":
        if not HEX_RE.match(v):
            return "colour must be #RRGGBB (got %r)" % v
    elif k == "meter.on":
        bad = [e for e in re.split(r"[\s,;]+", v.strip()) if e and not ID_RE.match(e)]
        if bad:
            return "must be a comma list of visual ids (lowercase); not ids: %s" % ", ".join(bad)
    elif k.startswith("meter.zoom_"):
        parts = [p for p in re.split(r"[\s,]+", v.strip()) if p]
        try:
            z = [float(p) for p in parts]
        except ValueError:
            z = []
        if len(z) != 2 or not all(0.5 <= x <= 4.0 for x in z):
            return "must be \"from,to\" zoom factors 0.5..4 (got %r)" % v
    elif k.startswith("fx."):
        for eff in [e.strip() for e in v.split(";") if e.strip()]:
            kind = re.split(r"[\s,]+", eff, 1)[0].lower()
            if kind not in FX_KINDS:
                return "unknown effect %r (use %s)" % (kind, "/".join(FX_KINDS))
            if kind == "none":
                continue
            rest = eff[len(kind):].strip(" ,\t")
            colour = None
            if "#" in rest:
                rest, colour = rest.split("#", 1)
                if not HEX_RE.match("#" + colour.strip()):
                    return "effect %r: the colour must be #RRGGBB" % eff
            n = _ints(rest) if rest.strip(" ,\t") else []
            if n is None or not 3 <= len(n) <= 6:
                return "effect %r must be kind,x,y,startMs[,endMs[,jx[,jy]]][,#RRGGBB]" % eff
            if len(n) >= 4 and n[3] < n[2]:
                return "effect %r ends before it starts" % eff
    return None


def _validate_words(words, m, report):
    if not isinstance(words, dict):
        report.error("words", "must be an object of \"section.key\": \"text\"")
        return
    for k, v in words.items():
        where = "words[%s]" % k
        if not isinstance(k, str) or not WORD_KEY_RE.match(k):
            report.error(where, "key must be lowercase 'section.key' or a top-level 'key' (letters, digits, _)")
            continue
        if not isinstance(v, str):
            report.error(where, "value must be a string")
            continue
        if "\n" in v or "\r" in v:
            report.error(where, "value must be one line")
            continue
        needs_quotes = v != v.strip() or re.search(r"[ \t];", v) or v[:1] in ("\"", "'")
        if needs_quotes and '"' in v and "'" in v:
            report.error(where, "can't be written to captions.ini: it needs quoting (edge blanks, ' ;' or a leading "
                                "quote) but contains both quote characters")
            continue
        if v != v.strip():
            report.warn(where, "leading/trailing blanks (kept: the value is quoted in captions.ini)")
        if any(ord(ch) > 126 for ch in v):
            report.warn(where, "non-ASCII characters (%s) may not exist in the screen fonts"
                        % " ".join("U+%04X" % ord(ch) for ch in sorted(set(ch for ch in v if ord(ch) > 126))))
        if k == NOBUTTON_VOICE:
            pairs, problem = parse_voice_subst(v)
            if problem:
                report.error(where, problem)
                continue
            if v != v.strip() or re.search(r"\s", v.strip()):
                report.warn(where, "write the swaps without blanks (\"from=to;from=to\")")
            m.voice_subst = pairs
            m.words[k] = v
            continue
        # a no-button override is checked like the key it replaces (nobutton.caption.charge -> caption.charge)
        base = k[len(NOBUTTON):] if k.startswith(NOBUTTON) else k
        if base != k and base not in words:
            report.info(where, "overrides '%s', which the manifest doesn't set (the firmware's built-in word is "
                               "used when the station has a button)" % base)
        last = base.split(".")[-1]
        if COLOR_HEX_RE.match(base):
            # a crystal colour: "#RRGGBB" | 2-4 stops "#RRGGBB,#RRGGBB" | "rainbow" ("" = not defined)
            if v and co_layers.color_spec_problem(v):
                report.error(where, co_layers.color_spec_problem(v))
        elif last.endswith("_color") or last.endswith("_hex"):
            if not HEX_RE.match(v):
                report.error(where, "colour must be #RRGGBB (got %r)" % v)
        elif last.endswith("_shape"):
            if v not in SHAPES:
                report.error(where, "shape must be one of %s" % "/".join(SHAPES))
        elif base.startswith("caption.") and len(v) > CAPTION_MAX:
            report.warn(where, "caption is %d characters (the caption band fits about %d)" % (len(v), CAPTION_MAX))
        problem = story_word_problem(base, v)
        if problem:
            report.error(where, problem)
            continue
        m.words[k] = v
    _check_crystal_colors(m.words, report)
    # (keys missing compared with firmware/story_defaults.h are listed by build_assets.py)


def voice_group(vid):
    """'charge_tap_2' -> 'charge_tap' (a line id); a group id is returned as it is."""
    mt = LINE_ID_RE.match(vid)
    return mt.group(1) if mt else vid


def parse_voice_subst(v):
    """words nobutton.voice "from=to;from=to" -> ([(from, to)], None) or (None, problem). Blanks are tolerated."""
    pairs = []
    for item in v.split(";"):
        item = item.strip()
        if not item:
            continue
        if "=" not in item:
            return None, "swap %r must be from=to (a voice group or a line id on each side; to may be empty)" % item
        a, b = (x.strip() for x in item.split("=", 1))
        if not ID_RE.match(a) or (b and not ID_RE.match(b)):
            return None, "swap %r: both sides must be voice ids (lowercase letters, digits, _)" % item
        pairs.append((a, b))
    return pairs, None


def subst_targets(m, group):
    """Voice groups that stand in for `group` (or one of its lines) when the station has no button."""
    return [voice_group(b) for a, b in m.voice_subst if b and voice_group(a) == group]


def _check_voice_subst(m, groups, line_ids, report):
    """words nobutton.voice against the manifest's voice lines (groups: {group: {variants}})."""
    if not m.voice_subst:
        return
    where = "words[%s]" % NOBUTTON_VOICE
    known = set(m.cues.voice_groups) | set(groups)
    froms = set()
    for a, b in m.voice_subst:
        a_line = bool(LINE_ID_RE.match(a))
        if a in froms:
            report.warn(where, "'%s' is swapped twice - only one swap can apply" % a)
        froms.add(a)
        if a_line and a not in line_ids:
            report.warn(where, "line '%s' is not in the manifest - nothing to swap" % a)
        elif not a_line and a not in known:
            report.warn(where, "'%s' is neither a voice group nor a line id the firmware plays" % a)
        if not b:
            continue
        if a == b:
            report.warn(where, "'%s=%s' swaps a line for itself" % (a, b))
        b_line = bool(LINE_ID_RE.match(b))
        if a_line != b_line:
            report.warn(where, "'%s=%s' swaps a %s for a %s: only group=group and line=line swaps are defined"
                        % (a, b, "line" if a_line else "group", "line" if b_line else "group"))
        if b_line and b not in line_ids:
            report.warn(where, "line '%s' is not in the manifest - nothing plays for '%s' without a button" % (b, a))
        elif not b_line and b not in groups:
            report.warn(where, "group '%s' has no lines in the manifest - nothing plays for '%s' without a button"
                        % (b, a))
    report.info(where, "without a big button (config.ini [station] button = no): %s"
                % ", ".join("%s -> %s" % (a, b or "(never)") for a, b in m.voice_subst))


def _check_crystal_colors(words, report):
    """The crystal colour set: color.1 .. color.N (+ _hex), N = the run of non-empty color.N_hex from 1."""
    slots = co_layers.COLOR_SLOTS
    n = co_layers.color_count(words)
    for k in words:
        mt = re.match(r"^color\.(\d+)(_hex)?$", k)
        if mt and not 1 <= int(mt.group(1)) <= slots:
            report.warn("words[%s]" % k, "crystal colours are numbered 1..%d (the bench writes 1..%d to the tag) - "
                                         "never used" % (slots, slots))
    late = [i for i in range(n + 2, slots + 1) if (words.get("color.%d_hex" % i) or "").strip()]
    if late:
        report.warn("words", "color.%d_hex is missing or empty, so the firmware stops counting crystal colours at %d - "
                             "color.%s never used" % (n + 1, n, ", color.".join("%d_hex" % i for i in late)))
    for i in range(1, n + 1):
        if not (words.get("color.%d" % i) or "").strip():
            report.warn("words[color.%d]" % i, "colour %d has a colour (color.%d_hex) but no name - the screen shows "
                                               "it without a name" % (i, i))
    for i in range(n + 1, slots + 1):
        if (words.get("color.%d" % i) or "").strip() and not (words.get("color.%d_hex" % i) or "").strip():
            report.warn("words[color.%d]" % i, "named but has no color.%d_hex - colour %d is not defined (crystals set "
                                               "to it show no colour)" % (i, i, i))
    if words and any(k.startswith("color.") for k in words):
        report.info("words", "%d crystal colour(s) defined: %s" % (n, ", ".join(
            "%d %s (%s)" % (i, words.get("color.%d" % i, "?"), words.get("color.%d_hex" % i)) for i in range(1, n + 1))))


def resolve_sources(m, src_root, report):
    """Fill in src paths (None = missing) and image probes. src_root = assets/source."""
    dirs = {
        "images": SourceDir(os.path.join(src_root, "images"), IMAGE_EXTS),
        "video": SourceDir(os.path.join(src_root, "video"), VIDEO_EXTS),
        "voice": SourceDir(os.path.join(src_root, "voice"), AUDIO_EXTS),
        "sfx": SourceDir(os.path.join(src_root, "sfx"), AUDIO_EXTS),
        "music": SourceDir(os.path.join(src_root, "music"), AUDIO_EXTS),
    }

    def pick(where, d, stem):
        path, others = d.find(stem)
        if others:
            report.warn(where, "several source files (%s) - using the newest, %s"
                        % (", ".join(os.path.basename(o) for o in [path] + others), os.path.basename(path)))
        return path

    for st in m.stills.values():
        where = "stills[%s]" % st["id"]
        st["src"] = pick(where, dirs["images"], st["id"])
        if st["src"]:
            pr = probe_image(st["src"])
            st["probe"] = pr
            if pr is None:
                report.warn(where, "cannot read the image header of %s (ffmpeg may still manage)" % os.path.basename(st["src"]))
                continue
            w, h, alpha, fmt = pr
            if st["transparent"] and not alpha:
                if st["key"] is False:
                    report.warn(where, "marked transparent but %s has no alpha channel (key is off)" % os.path.basename(st["src"]))
                else:
                    kc = ("its backdrop (green screen if the corners are green, else black)" if st["key"] == "auto"
                          else st["key"])
                    report.info(where, "no alpha channel - %s will be keyed out" % kc)
            if not st["transparent"] and st["kind"] == "background" and (w < 800 or h < 480):
                report.warn(where, "%dx%d is smaller than the 800x480 screen (it will be upscaled)" % (w, h))
    for c in m.clips:
        if c["video"]:
            path, others = dirs["video"].find_name(c["video"])
            c["video_src"] = path
            if others:
                report.warn("clips[%s]" % c["id"], "several video files match '%s' - using %s" % (c["video"], os.path.basename(path)))
        else:
            c["video_src"] = None
    for key in ("voice", "sfx", "music"):
        for e in getattr(m, key):
            e["src"] = pick("%s[%s]" % (key, e["id"]), dirs[key], e["id"])
    return dirs


def clip_sources(c, m):
    """[(label, path or None)] for every input a clip needs (layers: the REQUIRED inputs only -
    a fallback still satisfies its layer, and optional layers never block a build)."""
    if c["recipe"] == "layers":
        return co_layers.sources(c, m)
    out = [(r, m.stills[r]["src"]) for r in dict.fromkeys(c["refs"]) if r in m.stills]
    if c["recipe"] == "video":
        out.append(("video:" + (c["video"] or "?"), c.get("video_src")))
    return out


def clip_buildable(c, m):
    return all(p for _, p in clip_sources(c, m))


def clip_used_stills(c, m):
    """The still ids a build of this clip really reads (fallbacks resolved, missing optional layers dropped)."""
    if c["recipe"] == "layers":
        return co_layers.used_stills(c, m.stills)
    return list(dict.fromkeys(c["refs"]))


def clip_anchor(c, m):
    """(x, y, r): the clip's lantern anchor in screen pixels, or None."""
    return co_layers.anchor(c, m)


def loop_cycles(frames, fps, period):
    return frames / (period * fps) if period else math.nan
