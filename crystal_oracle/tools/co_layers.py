"""
============================================================================
 co_layers.py  -  the "layers" clip recipe (Crystal Oracle)
----------------------------------------------------------------------------
 A timeline of stills over a background, built in ONE ffmpeg filter graph:

   "bg":     {"image", "zoom": [from, to] (centred Ken Burns), "rotate":
              [fromDeg, toDeg] (a larger-scaled copy turns, so no corner ever
              shows), "fallback": <still id>}
   "layers": draw order; each one is a "sprite" (alpha / colour-keyed still)
             or an "image" (a full-frame still, e.g. a background crossfade),
             with keyframed height / x (centre) / y (top) / rotate, a visible
             window t:[start, end], alpha fades, bob, flutter, spin, tint,
             screen blending, optional / fallback stills.

 Keyframe arrays with N values are spread evenly across the layer's window and
 interpolated linearly (piecewise); a single number is a constant. Times are
 clip seconds; frame n is at t = n / fps (every stream is re-timed to exactly
 that, so the overlay never pairs frame n with a neighbour).

 This module: validation/normalisation (called by co_manifest), source
 resolution (fallbacks, optional layers), the ffmpeg graph (called by
 co_media), the lantern ANCHOR of a clip in screen pixels (captions.ini
 [anchor]) and a loop-seam check. It imports nothing from the other co_*
 modules except co_media at graph time. Python 3 standard library only.
============================================================================
"""
import copy
import math
from fractions import Fraction

W, H = 800, 480
KB_SCALE = 3                  # Ken Burns bg zoom works on a 3x canvas (same as the kenburns recipe)
FLUTTER_HZ = 6.0              # "flutter": horizontal squash 1 +- FLUTTER_AMP at this rate
FLUTTER_AMP = 0.03
SPIN_MIN = 0.08               # "spin": narrowest width (fraction) when edge-on
DEFAULT_HEIGHT = 360
ANCHOR_R_FRAC = 0.11          # glow radius = 11 % of the layer height ...
ANCHOR_R_MIN = 18             # ... but at least this many px
TINT_MIX = 0.75               # tint: 75 % multiplied + 25 % original (keeps highlights)
COLOR_SLOTS = 6               # crystal colour ids the token/firmware know: words color.1 .. color.6 (+ _hex)
STOPS_MIN, STOPS_MAX = 2, 4   # a colour spec "#RRGGBB,#RRGGBB[,..]" has 2..4 stops
# Gradient tint direction across the sprite box: t = GRAD_Y * y/h + GRAD_X * x/w (0 = first stop, 1 = last).
# Mostly top -> bottom like the colour bands of a gradient-filament print, with a slight diagonal sheen.
GRAD_Y, GRAD_X = 0.8, 0.2
# "rainbow" as a tint: a candy hue sweep pink -> orange -> yellow -> green -> sky -> violet (no wrap, so the
# ends differ; it starts at pink, not pure red, because red belongs to the Dragon Gem bowl), slightly
# softened so the multiply still reads as candy colours on a pale sprite.
RAINBOW_STOPS = ["#FF6FC8", "#FFA23D", "#FFE94D", "#5CE65C", "#3DC8FF", "#8F7BFF"]

LAYER_FIELDS = {"sprite", "image", "height", "x", "y", "t", "fade_in", "fade_out", "bob", "flutter", "rotate",
                "spin", "tint", "blend", "optional", "fallback"}
BG_FIELDS = {"image", "zoom", "rotate", "fallback"}
CLIP_FIELDS = {"id", "recipe", "priority", "fps", "q", "seconds", "loop", "bg", "layers", "anchor_layer", "variants"}
FREE_FIELDS = {"desc", "notes", "note", "comment", "prompt"}


def _num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def _clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def _check_fields(where, d, allowed, report):
    for k in d:
        if k not in allowed and k not in FREE_FIELDS and not k.startswith("_"):
            report.warn(where, "unknown field '%s' (typo?) - ignored" % k)


# ---------------------------------------------------------------------------
# Keyframes: [v] or [v0, v1, ...] spread evenly over the window [t0, t1]
# ---------------------------------------------------------------------------
def kf_parse(where, key, v, lo, hi, report, default):
    """number | [numbers] -> list of floats (one value = constant)."""
    if v is None:
        return None if default is None else [float(default)]
    vals = v if isinstance(v, list) else [v]
    if not vals or not all(_num(x) and lo <= x <= hi for x in vals):
        report.error(where, "'%s' must be a number %g..%g or a list of them (keyframes), got %r" % (key, lo, hi, v))
        return None if default is None else [float(default)]
    return [float(x) for x in vals]


def kf_value(vals, t0, t1, t):
    """Piecewise-linear value of keyframes vals at time t (constant outside [t0, t1])."""
    if len(vals) == 1:
        return vals[0]
    span = t1 - t0
    p = _clamp((t - t0) / span, 0.0, 1.0) * (len(vals) - 1) if span > 0 else 0.0
    k = min(int(p), len(vals) - 2)
    return vals[k] + (vals[k + 1] - vals[k]) * (p - k)


def num(x):
    s = ("%.6f" % float(x)).rstrip("0").rstrip(".")
    return s if s not in ("-0", "") else "0"


def _p(x):
    """A number for an ffmpeg expression, negative ones in parentheses."""
    s = num(x)
    return "(%s)" % s if s.startswith("-") else s


def kf_expr(vals, t0, t1, tv="t"):
    """ffmpeg expression for kf_value(): v0 + sum_k (v[k+1]-v[k]) * clip(P - k, 0, 1),
    P = clip((t - t0) / (t1 - t0), 0, 1) * (N - 1)."""
    if len(vals) == 1 or all(abs(v - vals[0]) < 1e-9 for v in vals):
        return _p(vals[0])
    span = max(1e-6, t1 - t0)
    P = "clip((%s-%s)/%s,0,1)*%d" % (tv, _p(t0), num(span), len(vals) - 1)
    terms = [_p(vals[0])]
    for k in range(len(vals) - 1):
        d = vals[k + 1] - vals[k]
        if abs(d) > 1e-9:
            terms.append("%s*clip(%s-%d,0,1)" % (_p(d), P, k))
    return "(" + "+".join(terms) + ")"


def fps_timebase(fps):
    """'1/13' for 13 fps: every stream is re-timed to pts = n in this time base."""
    f = Fraction(fps).limit_denominator(1000)
    return "%d/%d" % (f.denominator, f.numerator)


# ---------------------------------------------------------------------------
# Validation / normalisation
# ---------------------------------------------------------------------------
def _still(where, v, key, model, report, required=True):
    if v is None:
        if required:
            report.error(where, "needs '%s' (a still id)" % key)
        return None
    if not isinstance(v, str) or v not in model.stills:
        report.error(where, "'%s' = %r is not a still id in the manifest" % (key, v))
        return None
    return v


def _bool(where, d, key, default, report):
    v = d.get(key, default)
    if not isinstance(v, bool):
        report.error(where, "'%s' must be true or false" % key)
        return default
    return v


def _secs(where, d, key, default, report, lo=0.0, hi=600.0):
    v = d.get(key, default)
    if not _num(v) or not lo <= v <= hi:
        report.error(where, "'%s' must be a number of seconds %g..%g (got %r)" % (key, lo, hi, v))
        return float(default)
    return float(v)


def _hex_ok(v):
    return isinstance(v, str) and len(v) == 7 and v[0] == "#" and all(ch in "0123456789abcdefABCDEF" for ch in v[1:])


# ---------------------------------------------------------------------------
# Colour specs (words color.N_hex, layer "tint"; the firmware reads the same grammar):
#   "#RRGGBB"                          one colour
#   "#RRGGBB,#RRGGBB[,#RRGGBB[,..]]"   2..4 stops (the device cycles them, ~4 s per cycle;
#                                      a tint paints them as a gradient across the sprite)
#   "rainbow"                          the full hue cycle (a tint: the RAINBOW_STOPS pink -> violet gradient)
# No blanks, exactly one "#" per stop. The number of defined crystal colours = the highest
# N (1..6) with a non-empty color.N_hex, contiguous from 1 (color_count()).
# ---------------------------------------------------------------------------
def color_spec_problem(v):
    """None if v is a valid colour spec, else what's wrong with it (one line)."""
    if not isinstance(v, str) or not v:
        return "must be \"#RRGGBB\", 2-4 stops \"#RRGGBB,#RRGGBB\" or \"rainbow\" (got %r)" % (v,)
    if v == "rainbow":
        return None
    if v.strip().lower() == "rainbow":
        return "write \"rainbow\" in lowercase with no blanks (got %r)" % v
    if v != v.strip() or " " in v or "\t" in v:
        return "no blanks in a colour spec: write the stops as \"#RRGGBB,#RRGGBB\" (got %r)" % v
    stops = v.split(",")
    bad = [s for s in stops if not _hex_ok(s)]
    if bad:
        return "%s is not #RRGGBB - a colour spec is \"#RRGGBB\", 2-4 stops \"#RRGGBB,#RRGGBB\" or \"rainbow\"" \
               % ", ".join(repr(s) for s in bad)
    if len(stops) > STOPS_MAX:
        return "%d stops - a colour spec has at most %d (got %r)" % (len(stops), STOPS_MAX, v)
    return None


def color_stops(v):
    """A valid colour spec -> its stops as [(r, g, b) 0..1]: 1 for a single colour, 2..4 for a
    stop list, RAINBOW_STOPS for "rainbow". None if v is not valid."""
    if color_spec_problem(v):
        return None
    hexes = RAINBOW_STOPS if v == "rainbow" else v.split(",")
    return [tuple(int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5)) for h in hexes]


def color_count(words):
    """How many crystal colours a words dict defines: the run of non-empty color.N_hex from N = 1
    (a colour after a gap is never used - co_manifest warns about it)."""
    n = 0
    while n < COLOR_SLOTS and (words.get("color.%d_hex" % (n + 1)) or "").strip():
        n += 1
    return n


def normalise(c, model, report, where, out):
    """Fill out (the normalised clip dict) for recipe 'layers'. Returns the list of variant
    clips to add after it (colour variants) - empty for an ordinary clip."""
    _check_fields(where, c, CLIP_FIELDS, report)
    fps = out["fps"]
    p = {}
    sec = c.get("seconds", 4.0)
    if not _num(sec) or not 0.1 <= sec <= 60:
        report.error(where, "'seconds' must be 0.1..60 (got %r)" % (sec,))
        sec = 4.0
    p["seconds"] = float(sec)
    p["loop"] = _bool(where, c, "loop", False, report)
    refs = []
    needs = []                       # [(label, [candidate still ids])] - all required
    # ---- background ----
    bg = c.get("bg")
    if isinstance(bg, str):
        bg = {"image": bg}
    if bg is None:
        p["bg"] = None
        report.info(where, "no 'bg' - the layers go over black")
    elif not isinstance(bg, dict):
        report.error(where, "'bg' must be an object {image, zoom, rotate, fallback} or a still id")
        p["bg"] = None
    else:
        w2 = where + ".bg"
        _check_fields(w2, bg, BG_FIELDS, report)
        img = _still(w2, bg.get("image"), "image", model, report)
        fb = _still(w2, bg.get("fallback"), "fallback", model, report, required=False)
        b = {"image": img, "fallback": fb, "zoom": None, "rotate": None}
        z = bg.get("zoom")
        if z is not None:
            if not isinstance(z, list) or len(z) != 2 or not all(_num(v) and 1.0 <= v <= 4.0 for v in z):
                report.error(w2, "zoom must be [from, to] with values 1.0..4.0")
            elif abs(z[0] - z[1]) > 1e-9 or z[0] != 1.0:
                b["zoom"] = [float(z[0]), float(z[1])]
        r = bg.get("rotate")
        if r is not None:
            if not isinstance(r, list) or len(r) != 2 or not all(_num(v) and -45 <= v <= 45 for v in r):
                report.error(w2, "rotate must be [fromDeg, toDeg] with values -45..45")
            elif any(abs(v) > 1e-9 for v in r):
                b["rotate"] = [float(r[0]), float(r[1])]
        for sid in (img, fb):
            if sid and model.stills[sid]["transparent"]:
                report.warn(w2, "'%s' is a transparent still - flattened onto black" % sid)
        p["bg"] = b
        if img:
            refs.append(img)
            needs.append(("bg " + img + (" (or %s)" % fb if fb else ""), [img] + ([fb] if fb else [])))
        if fb:
            refs.append(fb)
    # ---- layers ----
    variants = c.get("variants")
    if variants not in (None, "colors"):
        report.error(where, "variants must be \"colors\" (got %r)" % (variants,))
        variants = None
    p["variants"] = variants
    layers = []
    raw = c.get("layers")
    if not isinstance(raw, list) or not raw:
        report.error(where, "'layers' must be a non-empty list")
        raw = []
    uses_color = False
    for k, L in enumerate(raw):
        w2 = "%s.layers[%d]" % (where, k)
        if not isinstance(L, dict):
            report.error(w2, "must be an object")
            layers.append(None)
            continue
        _check_fields(w2, L, LAYER_FIELDS, report)
        if ("sprite" in L) == ("image" in L):
            report.error(w2, "needs exactly one of 'sprite' (a cut-out) or 'image' (a full-frame still)")
            layers.append(None)
            continue
        kind = "sprite" if "sprite" in L else "image"
        sid = _still(w2, L.get(kind), kind, model, report)
        fb = _still(w2, L.get("fallback"), "fallback", model, report, required=False)
        opt = _bool(w2, L, "optional", False, report)
        t = L.get("t", [0, p["seconds"]])
        if (not isinstance(t, list) or len(t) != 2 or not all(_num(v) for v in t) or not 0 <= t[0] < t[1] <= 600):
            report.error(w2, "t must be [start, end] seconds with 0 <= start < end (got %r)" % (t,))
            t = [0, p["seconds"]]
        t = [float(t[0]), float(t[1])]
        if t[0] >= p["seconds"]:
            report.warn(w2, "starts at %gs, after the clip ends (%gs) - never visible" % (t[0], p["seconds"]))
        lay = {"kind": kind, "still": sid, "fallback": fb, "optional": opt, "t": t,
               "fade_in": _secs(w2, L, "fade_in", 0.0, report, hi=60), "fade_out": _secs(w2, L, "fade_out", 0.0, report, hi=60),
               "blend": L.get("blend", "normal")}
        if lay["fade_in"] + lay["fade_out"] > t[1] - t[0] + 1e-9:
            report.warn(w2, "fade_in + fade_out (%gs) is longer than the window (%gs)" % (lay["fade_in"] + lay["fade_out"], t[1] - t[0]))
        if lay["blend"] not in ("normal", "screen"):
            report.error(w2, "blend must be \"normal\" or \"screen\"")
            lay["blend"] = "normal"
        if kind == "sprite":
            lay["height"] = kf_parse(w2, "height", L.get("height"), 8, 1440, report, DEFAULT_HEIGHT)
            xv = L.get("x", "center")
            lay["x"] = "center" if xv == "center" else kf_parse(w2, "x", xv, -1600, 2400, report, W / 2)
            lay["y"] = kf_parse(w2, "y", L.get("y", 40), -1440, 1440, report, 40)
            lay["rotate"] = kf_parse(w2, "rotate", L.get("rotate"), -7200, 7200, report, None)
            if lay["rotate"] and all(abs(v) < 1e-9 for v in lay["rotate"]):
                lay["rotate"] = None
            sp = L.get("spin")
            if sp is not None and (not _num(sp) or not 0 <= sp <= 50):
                report.error(w2, "spin must be a number of turns 0..50")
                sp = None
            lay["spin"] = float(sp) if sp else None
            lay["flutter"] = _bool(w2, L, "flutter", False, report)
            bob = L.get("bob")
            if bob is not None:
                if not isinstance(bob, dict):
                    report.error(w2, "bob must be an object {amp, period}")
                    bob = None
                else:
                    _check_fields(w2 + ".bob", bob, {"amp", "period"}, report)
                    amp, per = bob.get("amp", 6), bob.get("period", 3)
                    if not _num(amp) or not 0 <= amp <= 200 or not _num(per) or not 0.1 <= per <= 60:
                        report.error(w2, "bob needs amp 0..200 px and period 0.1..60 s")
                        bob = None
                    else:
                        bob = {"amp": float(amp), "period": float(per)} if amp else None
            lay["bob"] = bob
            tint = L.get("tint")
            if tint is not None and tint != "$color" and color_spec_problem(tint):
                report.error(w2, "tint: %s, or \"$color\"" % color_spec_problem(tint))
                tint = None
            if tint == "$color":
                uses_color = True
                if variants != "colors":
                    report.warn(w2, "tint \"$color\" only works with \"variants\": \"colors\" - left untinted")
            lay["tint"] = tint
            for s in (sid, fb):
                if s and not model.stills[s]["transparent"]:
                    report.warn(w2, "sprite '%s' is not marked transparent - it will cover the background as a rectangle" % s)
        else:
            for f in ("height", "x", "y", "rotate", "spin", "flutter", "bob", "tint"):
                if f in L:
                    report.warn(w2, "'%s' does nothing on a full-frame 'image' layer" % f)
            for s in (sid, fb):
                if s and model.stills[s]["transparent"]:
                    report.warn(w2, "full-frame image '%s' is a transparent still - flattened onto black" % s)
        layers.append(lay)
        if sid:
            refs.append(sid)
        if fb:
            refs.append(fb)
        if sid and not opt:
            needs.append(("%s%s" % (sid, " (or %s)" % fb if fb else ""), [sid] + ([fb] if fb else [])))
    p["layers"] = layers
    if variants == "colors" and not uses_color:
        report.warn(where, "variants \"colors\" but no layer has tint \"$color\" - the variants are identical")
    al = c.get("anchor_layer")
    if al is not None:
        if not isinstance(al, int) or isinstance(al, bool) or not 0 <= al < len(layers):
            report.error(where, "anchor_layer must be a layer index 0..%d" % (len(layers) - 1))
            al = None
        elif layers[al] is not None and layers[al]["kind"] != "sprite":
            report.warn(where, "anchor_layer %d is a full-frame image, not a sprite - no anchor" % al)
            al = None
        elif layers[al] is not None:
            for s in (layers[al]["still"], layers[al]["fallback"]):
                if s and model.stills[s].get("anchor") is None:
                    report.info(where, "anchor_layer still '%s' has no \"anchor\" - its centre is used" % s)
    p["anchor_layer"] = al
    out["params"] = p
    out["refs"] = list(dict.fromkeys(r for r in refs if r))
    out["needs"] = needs
    out["ext"] = ".omj"
    out["frames"] = max(1, int(round(p["seconds"] * fps)))
    if p["loop"]:
        for msg in seam_problems(out, model.stills):
            report.warn(where, "not loop-exact: " + msg)
    # ---- colour variants ----
    extra = []
    if variants == "colors":
        ncol = color_count(model.words)
        if not ncol:
            report.warn(where, "variants \"colors\" but words defines no crystal colour (color.1_hex) - only the "
                               "untinted %s is built" % out["id"])
        for n in range(1, ncol + 1):           # one variant per DEFINED colour: <id>_1 .. <id>_<count>
            hexv = model.words.get("color.%d_hex" % n)
            if color_spec_problem(hexv):        # (the words check reports it as an error)
                hexv = None
            v = copy.deepcopy(out)
            v["id"] = "%s_%d" % (out["id"], n)
            v["variant_of"] = out["id"]
            v["params"]["variants"] = None
            v["params"]["color"] = hexv
            for lay in v["params"]["layers"]:
                if lay and lay.get("tint") == "$color":
                    lay["tint"] = hexv
            extra.append(v)
        for lay in p["layers"]:                 # the plain <id>: untinted (crystal without a colour)
            if lay and lay.get("tint") == "$color":
                lay["tint"] = None
    return extra


# ---------------------------------------------------------------------------
# Sources: which stills a build really uses (fallbacks, optional layers)
# ---------------------------------------------------------------------------
def _pick(stills, *ids):
    for s in ids:
        if s and stills.get(s, {}).get("src"):
            return s
    return None


def resolve(c, stills):
    """{'bg': still id | None, 'layers': [(index, layer, still id)]} using only stills whose
    source exists; None if a REQUIRED still (and its fallback) is missing."""
    p = c["params"]
    bg = None
    if p.get("bg") and p["bg"].get("image"):
        bg = _pick(stills, p["bg"]["image"], p["bg"]["fallback"])
        if not bg:
            return None
    out = []
    for k, lay in enumerate(p["layers"]):
        if not lay or not lay["still"]:
            continue
        sid = _pick(stills, lay["still"], lay["fallback"])
        if not sid:
            if lay["optional"]:
                continue
            return None
        out.append((k, lay, sid))
    return {"bg": bg, "layers": out}


def sources(c, m):
    """[(label, path or None)] for every REQUIRED input (a fallback satisfies its group)."""
    res = []
    for label, cands in c.get("needs", []):
        sid = _pick(m.stills, *cands)
        res.append((label, m.stills[sid]["src"] if sid else None))
    return res


def used_stills(c, stills):
    r = resolve(c, stills)
    if r is None:
        return []
    return list(dict.fromkeys(([r["bg"]] if r["bg"] else []) + [sid for _, _, sid in r["layers"]]))


# ---------------------------------------------------------------------------
# Geometry at a time t (python mirror of the ffmpeg expressions)
# ---------------------------------------------------------------------------
def _aspect(still):
    pr = still.get("probe") if still else None
    return (pr[0] / float(pr[1])) if pr and pr[1] else None


def layer_state(lay, still, t, clip_end=None):
    """Visible state of one layer at clip time t (None if invisible):
    dict(cx, cy (box centre incl. bob), h, w (None if the aspect is unknown), rot, alpha).
    clip_end: a window that runs to the clip's end also covers the virtual frame after
    the last one (loop-seam check)."""
    t0, t1 = lay["t"]
    runs_on = clip_end is not None and t1 >= clip_end - 1e-6 and t >= t0
    if not (t0 - 1e-9 <= t <= t1 + 1e-9 or runs_on):
        return None
    a = 1.0
    if lay["fade_in"] > 0:
        a *= _clamp((t - t0) / lay["fade_in"], 0.0, 1.0)
    if lay["fade_out"] > 0:
        a *= _clamp((t1 - t) / lay["fade_out"], 0.0, 1.0)
    if a <= 0.004:
        return None
    if lay["kind"] == "image":
        return {"cx": W / 2.0, "cy": H / 2.0, "h": H, "w": W, "rot": 0.0, "alpha": a}
    h = kf_value(lay["height"], t0, t1, t)
    cx = W / 2.0 if lay["x"] == "center" else kf_value(lay["x"], t0, t1, t)
    cy = kf_value(lay["y"], t0, t1, t) + h / 2.0
    if lay["bob"]:
        cy += lay["bob"]["amp"] * math.sin(2 * math.pi * t / lay["bob"]["period"])
    sq = 1.0
    if lay["flutter"]:
        sq *= 1 + FLUTTER_AMP * math.sin(2 * math.pi * FLUTTER_HZ * t)
    if lay["spin"]:
        prog = _clamp((t - t0) / (t1 - t0), 0.0, 1.0)
        sq *= max(SPIN_MIN, abs(math.cos(2 * math.pi * lay["spin"] * prog)))
    asp = _aspect(still)
    rot = kf_value(lay["rotate"], t0, t1, t) if lay["rotate"] else 0.0
    return {"cx": cx, "cy": cy, "h": h, "w": h * asp * sq if asp else None, "rot": rot, "alpha": a}


def anchor(c, model):
    """(x, y, r) screen pixels of the clip's lantern anchor at the anchor layer's first
    visible moment (bob ignored), or None (no anchor_layer / layer not built)."""
    if c.get("recipe") != "layers":
        return None
    p = c["params"]
    ai = p.get("anchor_layer")
    if ai is None:
        return None
    res = resolve(c, model.stills)
    if res is None:
        return None
    ent = next(((lay, sid) for k, lay, sid in res["layers"] if k == ai), None)
    if not ent:
        return None
    lay, sid = ent
    st = model.stills[sid]
    t = max(0.0, lay["t"][0])
    if t >= p["seconds"]:
        return None
    nob = dict(lay, bob=None)
    s = layer_state(dict(nob, fade_in=0.0, fade_out=0.0), st, t)
    if s is None:
        return None
    fx, fy = 0.5, 0.5
    a = st.get("anchor")
    if isinstance(a, list) and len(a) == 2 and all(_num(v) for v in a):
        fx, fy = float(a[0]), float(a[1])
    dx = (fx - 0.5) * (s["w"] if s["w"] else 0.0)
    dy = (fy - 0.5) * s["h"]
    if s["rot"]:
        th = math.radians(s["rot"])                  # ffmpeg rotate: positive = clockwise on screen
        dx, dy = dx * math.cos(th) - dy * math.sin(th), dx * math.sin(th) + dy * math.cos(th)
    r = max(ANCHOR_R_MIN, int(round(ANCHOR_R_FRAC * s["h"])))
    return int(round(s["cx"] + dx)), int(round(s["cy"] + dy)), r


def _scene(c, stills, t, use_sources):
    """Comparable description of everything visible at time t."""
    p = c["params"]
    items = []
    bg = p.get("bg")
    if bg:
        z = 1.0
        if bg["zoom"]:                           # zoompan: z0 + (z1 - z0) * on / (frames - 1), on = t * fps
            z = bg["zoom"][0] + (bg["zoom"][1] - bg["zoom"][0]) * t * c["fps"] / max(1, c["frames"] - 1)
        rot = kf_value(bg["rotate"], 0, p["seconds"], t) if bg["rotate"] else 0.0
        items.append(("bg", round(z, 3), round(rot, 1)))
    for k, lay in enumerate(p["layers"]):
        if not lay or not lay["still"]:
            continue
        sid = (_pick(stills, lay["still"], lay["fallback"]) or (None if lay["optional"] else lay["still"])) \
            if use_sources else lay["still"]
        if not sid:
            continue
        s = layer_state(lay, stills.get(sid), t, clip_end=p["seconds"])
        if s is None:
            continue
        items.append((sid, lay["kind"], lay.get("tint"), lay["blend"], s))
    return items


def seam_problems(c, stills, use_sources=False):
    """For a loop clip: differences between frame N (= the frame after the last) and frame 0."""
    n, fps = c["frames"], c["fps"]
    tN = n / fps
    a, b = _scene(c, stills, 0.0, use_sources), _scene(c, stills, tN, use_sources)
    probs = []
    if a and b and a[0][0] == "bg" and b[0][0] == "bg":
        if a[0] != b[0]:
            probs.append("the background zoom/rotation at the end (%s) differs from the start (%s)" % (b[0][1:], a[0][1:]))
        a, b = a[1:], b[1:]
    rest = list(b)
    for it in a:
        m = None
        for j, o in enumerate(rest):
            if o[:4] == it[:4]:
                s0, s1 = it[4], o[4]
                if (abs(s0["cx"] - s1["cx"]) <= 1.0 and abs(s0["cy"] - s1["cy"]) <= 1.0 and abs(s0["h"] - s1["h"]) <= 1.0
                        and abs(s0["rot"] - s1["rot"]) <= 0.5 and abs(s0["alpha"] - s1["alpha"]) <= 0.03):
                    m = j
                    break
        if m is None:
            near = next((o for o in rest if o[:4] == it[:4]), None)
            if near:
                rest.remove(near)
                s0, s1 = it[4], near[4]
                probs.append("'%s' is at (%.0f, %.0f) h%.0f alpha %.2f in frame 0 but (%.0f, %.0f) h%.0f alpha %.2f "
                             "after the last frame (%d frames at %g fps = %.3f s; make bob periods divide the length)"
                             % (it[0], s0["cx"], s0["cy"], s0["h"], s0["alpha"], s1["cx"], s1["cy"], s1["h"], s1["alpha"], n, fps, tN))
            else:
                probs.append("'%s' is visible in frame 0 but not after the last frame" % it[0])
        else:
            rest.pop(m)
    for o in rest:
        probs.append("'%s' is visible after the last frame (t=%.3f s) but not in frame 0" % (o[0], tN))
    return probs


# ---------------------------------------------------------------------------
# ffmpeg graph
# ---------------------------------------------------------------------------
def _rotate_cover(deg):
    """Scale factor so an 800x480 view rotated by up to deg never shows a corner."""
    th = math.radians(abs(deg))
    bw = W * math.cos(th) + H * math.sin(th)
    bh = W * math.sin(th) + H * math.cos(th)
    return max(bw / W, bh / H) * 1.01


def _even(v):
    return int(math.ceil(v / 2.0)) * 2


def _static_full(media, still, inp, out, w, h):
    """[inp] (one decoded still) -> one opaque w x h rgb24 frame [out] (cover-crop, or fit+flatten)."""
    if not still["transparent"]:
        return ("[%s]scale=%d:%d:force_original_aspect_ratio=increase:flags=lanczos,crop=%d:%d:(iw-%d)/2:%s,"
                "setsar=1,format=rgb24[%s]" % (inp, w, h, w, h, w, media._crop_y(still["crop"], h), out))
    return ("color=c=black:s=%dx%d:r=1:d=1[%s_k];[%s]%s,scale=%d:%d:force_original_aspect_ratio=decrease:flags=lanczos[%s_f];"
            "[%s_k][%s_f]overlay=x=(main_w-overlay_w)/2:y=(main_h-overlay_h)/2:shortest=1,trim=end_frame=1,setsar=1,"
            "format=rgb24[%s]" % (w, h, out, inp, media.sprite_prefix(still), w, h, out, out, out, out))


def _tint_filter(spec):
    """ffmpeg filter chain that multiply-tints one rgba sprite frame by a colour spec, keeping its alpha:
    each channel x (1 - TINT_MIX + TINT_MIX * colour), the colour being one value (#RRGGBB) or a gradient
    across the sprite box (stops / rainbow) at t = GRAD_Y * y/h + GRAD_X * x/w."""
    stops = color_stops(spec)
    if not stops:
        return ""
    fac = [[1 - TINT_MIX + TINT_MIX * c for c in s] for s in stops]
    if len(fac) == 1:
        f = fac[0]
        return "colorchannelmixer=rr=%s:gg=%s:bb=%s" % (num(f[0]), num(f[1]), num(f[2]))
    # Gradient: geq on planar gbrap (alpha copied through untouched), piecewise-linear between the stops.
    t = "clip(%s*Y/max(1,H-1)+%s*X/max(1,W-1),0,1)" % (num(GRAD_Y), num(GRAD_X))
    expr = []
    for ch, fn in ((0, "r"), (1, "g"), (2, "b")):
        vals = [f[ch] for f in fac]
        expr.append("%s='%s(X,Y)*%s'" % (fn, fn, kf_expr(vals, 0.0, 1.0, tv=t)))
    return "format=gbrap,geq=%s:a='alpha(X,Y)',format=rgba" % ":".join(expr)


def graph(c, stills, media):
    """(inputs, filter_complex, frames). media = the co_media module (helpers + key detection)."""
    p = c["params"]
    res = resolve(c, stills)
    if res is None:
        raise media.MediaError("a required still is missing")
    fps, n, secs = c["fps"], c["frames"], p["seconds"]
    tb = fps_timebase(fps)
    reps = n + 2                              # frames per stream (2 spare; -frames:v cuts at n)
    retime = "settb=expr=%s,setpts=N" % tb
    inputs, parts = [], []

    def add_input(path):
        inputs.append(["-i", path])
        return "%d:v" % (len(inputs) - 1)

    # ---- background -> [m0] (gbrp) ----
    bgp = p.get("bg") or {}
    if res["bg"]:
        st = stills[res["bg"]]
        rot = bgp.get("rotate")
        k = _rotate_cover(max(abs(v) for v in rot)) if rot else 1.0
        Wc, Hc = _even(W * k), _even(H * k)
        src = add_input(st["src"])
        if bgp.get("zoom"):
            z0, z1 = bgp["zoom"]
            parts.append(_static_full(media, st, src, "bgs", Wc * KB_SCALE, Hc * KB_SCALE))
            parts.append("[bgs]zoompan=z='%s+%s*on/%d':x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)':d=%d:s=%dx%d:fps=%s,%s[bgz]"
                         % (num(z0), _p(z1 - z0), max(1, n - 1), reps, Wc, Hc, num(fps), retime))
        else:
            parts.append(_static_full(media, st, src, "bgs", Wc, Hc))
            parts.append("[bgs]loop=loop=%d:size=1:start=0,%s[bgz]" % (reps - 1, retime))
        if rot:
            parts.append("[bgz]rotate=a='%s*PI/180':ow=%d:oh=%d:c=black,format=gbrp[m0]" % (kf_expr(rot, 0, secs), W, H))
        else:
            parts.append("[bgz]format=gbrp[m0]")
    else:
        parts.append("color=c=black:s=%dx%d:r=%s:d=%s,%s,format=gbrp[m0]" % (W, H, num(fps), num(reps / fps), retime))

    # ---- layers ----
    main = "m0"
    for j, (k, lay, sid) in enumerate(res["layers"]):
        st = stills[sid]
        t0, t1 = lay["t"]
        src = add_input(st["src"])
        lab = "L%d" % j
        fades = ""
        if lay["fade_in"] > 0:
            fades += ",fade=t=in:st=%s:d=%s:alpha=1" % (num(t0), num(lay["fade_in"]))
        if lay["fade_out"] > 0:
            fades += ",fade=t=out:st=%s:d=%s:alpha=1" % (num(t1 - lay["fade_out"]), num(lay["fade_out"]))
        enable = "between(t,%s,%s)" % (num(t0), num(t1))
        if lay["kind"] == "image":
            parts.append(_static_full(media, st, src, lab + "s", W, H))
            parts.append("[%ss]format=rgba,loop=loop=%d:size=1:start=0,%s%s[%s]" % (lab, reps - 1, retime, fades, lab))
            cx, cy = str(W // 2), str(H // 2)
        else:
            hv = lay["height"]
            hpre = int(math.ceil(max(hv)))
            animated_h = len(hv) > 1 and max(hv) - min(hv) > 1e-6
            chain = media.sprite_prefix(st) + ",scale=-1:%d:flags=lanczos" % hpre
            tf = _tint_filter(lay["tint"]) if lay.get("tint") else ""
            if tf:                                # tinted once, at sprite size (a gradient spans the sprite box)
                chain += "," + tf
            chain += ",loop=loop=%d:size=1:start=0,%s%s" % (reps - 1, retime, fades)
            if lay["rotate"]:
                chain += (",rotate=a='%s*PI/180':ow='ceil(hypot(iw,ih)/2)*2':oh=ow:c=none"
                          % kf_expr(lay["rotate"], t0, t1))
            sq = []
            if lay["flutter"]:
                sq.append("(1+%s*sin(2*PI*%s*t))" % (num(FLUTTER_AMP), num(FLUTTER_HZ)))
            if lay["spin"]:
                sq.append("max(%s,abs(cos(2*PI*%s*clip((t-%s)/%s,0,1))))"
                          % (num(SPIN_MIN), num(lay["spin"]), _p(t0), num(max(1e-6, t1 - t0))))
            if animated_h or sq:
                kexp = "(%s/%d)" % (kf_expr(hv, t0, t1), hpre) if animated_h else "1"
                wexp = "max(2,iw*%s%s)" % (kexp, "".join("*" + s for s in sq))
                hexp = "max(2,ih*%s)" % kexp
                chain += ",scale=w='%s':h='%s':eval=frame:flags=bicubic" % (wexp, hexp)
            parts.append("[%s]%s[%s]" % (src, chain, lab))
            cx = str(W // 2) if lay["x"] == "center" else kf_expr(lay["x"], t0, t1)
            cy = "%s+%s/2" % (kf_expr(lay["y"], t0, t1), kf_expr(hv, t0, t1))
            if lay["bob"]:
                cy += "+%s*sin(2*PI*t/%s)" % (num(lay["bob"]["amp"]), num(lay["bob"]["period"]))
        xexp = "%s-overlay_w/2" % cx
        yexp = "%s-overlay_h/2" % cy
        nxt = "m%d" % (j + 1)
        if lay["blend"] == "screen":
            parts.append("color=c=black:s=%dx%d:r=%s:d=%s,%s,format=gbrp[%sc]" % (W, H, num(fps), num(reps / fps), retime, lab))
            parts.append("[%sc][%s]overlay=x='%s':y='%s':eval=frame:enable='%s':format=gbrp[%sf]"
                         % (lab, lab, xexp, yexp, enable, lab))
            parts.append("[%s][%sf]blend=all_mode=screen[%s]" % (main, lab, nxt))
        else:
            parts.append("[%s][%s]overlay=x='%s':y='%s':eval=frame:enable='%s':format=gbrp[%s]"
                         % (main, lab, xexp, yexp, enable, nxt))
        main = nxt
    parts.append("[%s]setsar=1,format=yuvj420p[v]" % main)
    return inputs, ";".join(parts), n
