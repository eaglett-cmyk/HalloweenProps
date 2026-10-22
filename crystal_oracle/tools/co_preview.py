"""
============================================================================
 co_preview.py  -  contact sheets of the built clips (assets/previews)
----------------------------------------------------------------------------
 For every clip built from real art: assets/previews/<id>.jpg = a 2x2 sheet
 (800x480) of the frames at 10 / 35 / 60 / 85 % of the clip, each at half
 size (400x240), with the clip's lantern anchor marked by a small cyan ring
 (radius = half the glow radius r, like everything else on the sheet), plus
 assets/previews/index.html listing them all. Rebuilt only when the clip file
 or its anchor changed (assets/previews/.previews.json); --previews redoes
 all. Output only - keep assets/previews out of git.
 Python 3 standard library only (+ ffmpeg).
============================================================================
"""
import concurrent.futures
import datetime
import html
import json
import os
import shutil
import struct
import tempfile

import co_media as media
import omj

FRACS = (0.10, 0.35, 0.60, 0.85)
TW, TH = omj.FRAME_W // 2, omj.FRAME_H // 2
REV = 1
STATE = ".previews.json"


def clip_frames(path):
    """All frames of an .omj (list of JPEG bytes), or [the jpeg] for a still .jpg."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != omj.OMJ_MAGIC:
        return [data], None
    _, _, _, fps100, _, count, _ = omj.OMJ_HEADER.unpack_from(data, 0)
    p, out = omj.OMJ_HEADER.size, []
    for _ in range(count):
        ln = struct.unpack_from("<I", data, p)[0]
        out.append(data[p + 4:p + 4 + ln])
        p += 4 + ln + ((-ln) % 4)
    return out, fps100 / 100.0


def pick(n):
    return [min(n - 1, int(f * n)) for f in FRACS]


def _ring(anchor):
    """lavfi source of the anchor ring sprite (half-size coordinates) -> (graph, x, y)."""
    x, y, r = anchor
    rr = max(4.0, r / 2.0)
    d = int(2 * rr + 8)
    c = d / 2.0
    g = ("color=c=black@0:s=%dx%d:r=1:d=1,format=rgba,geq=r='0':g='255':b='255':"
         "a='255*max(between(hypot(X-%s,Y-%s),%s,%s),lte(hypot(X-%s,Y-%s),1.6))'"
         % (d, d, media.num(c), media.num(c), media.num(rr - 1.6), media.num(rr + 0.6), media.num(c), media.num(c)))
    return g, int(round(x / 2.0 - c)), int(round(y / 2.0 - c))


def make_sheet(clip_path, out_jpg, anchor, label, verbose=False, log=None):
    frames, fps = clip_frames(clip_path)
    idx = pick(len(frames))
    tmp = tempfile.mkdtemp(prefix="co-prev-")
    try:
        inputs, parts = [], []
        for j, k in enumerate(idx):
            fp = os.path.join(tmp, "f%d.jpg" % j)
            with open(fp, "wb") as f:
                f.write(frames[k])
            inputs += ["-i", fp]
        font = media.test_font()
        ff = "fontfile='%s'" % media._ff_path(font) if font else None
        ring = None
        if anchor:
            g, rx, ry = _ring(anchor)
            parts.append(g + ",split=4[r0][r1][r2][r3]")
            ring = (rx, ry)
        for j, k in enumerate(idx):
            ch = "[%d:v]scale=%d:%d:flags=area,format=rgb24" % (j, TW, TH)
            if ring:
                parts.append(ch + "[b%d]" % j)
                ch = "[b%d][r%d]overlay=x=%d:y=%d:format=auto" % (j, j, ring[0], ring[1])
            if ff:
                t = ("frame %d  t=%.2fs" % (k, k / fps)) if fps else "still"
                if j == 0:
                    t = "%s   %s" % (label, t)
                safe = "".join(ch_ for ch_ in t if ch_.isalnum() or ch_ in " _.=")
                ch += (",drawtext=%s:text='%s':fontsize=13:fontcolor=white:borderw=2:bordercolor=black:x=6:y=h-th-6"
                       % (ff, safe))
            parts.append(ch + "[t%d]" % j)
        parts.append("[t0][t1][t2][t3]xstack=inputs=4:layout=0_0|w0_0|0_h0|w0_h0,format=yuvj420p[v]")
        out_tmp = out_jpg + ".part.jpg"
        media.run([media.FFMPEG] + media.BASE + inputs + ["-filter_complex", ";".join(parts), "-map", "[v]",
                                                          "-frames:v", "1", "-q:v", "3", "-update", "1", out_tmp],
                  verbose=verbose, log=log, timeout=120)
        os.replace(out_tmp, out_jpg)
        return len(frames), fps
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def update(prev_dir, items, title, force=False, jobs=4, verbose=False, log=print):
    """items: [(clip, clip path, anchor or None)]. Writes the changed sheets + index.html.
    Returns a short summary string ('' if nothing to do)."""
    os.makedirs(prev_dir, exist_ok=True)
    state_path = os.path.join(prev_dir, STATE)
    try:
        with open(state_path, "r", encoding="utf-8") as f:
            state = json.load(f)
        if not isinstance(state, dict):
            state = {}
    except (OSError, ValueError):
        state = {}
    todo, info = [], {}
    for c, path, anc in items:
        st = os.stat(path)
        sig = [REV, st.st_size, st.st_mtime_ns, list(anc) if anc else None]
        jpg = os.path.join(prev_dir, c["id"] + ".jpg")
        old = state.get(c["id"], {})
        info[c["id"]] = old.get("info")
        if force or old.get("sig") != sig or not os.path.exists(jpg):
            todo.append((c, path, anc, sig, jpg))
    failed = []

    def one(t):
        c, path, anc, sig, jpg = t
        try:
            n, fps = make_sheet(path, jpg, anc, c["id"], verbose, log)
            return c["id"], {"sig": sig, "info": {"frames": n, "fps": fps, "bytes": os.path.getsize(path),
                                                   "anchor": list(anc) if anc else None}}
        except (media.MediaError, OSError, ValueError, struct.error) as e:
            failed.append("%s (%s)" % (c["id"], e))
            return c["id"], None

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        for cid, ent in pool.map(one, todo):
            if ent:
                state[cid] = ent
                info[cid] = ent["info"]
    # sheets of clips that are no longer built from real art: drop them (only files we made)
    keep = {c["id"] for c, _, _ in items}
    for cid in list(state):
        if cid not in keep:
            try:
                os.remove(os.path.join(prev_dir, cid + ".jpg"))
            except OSError:
                pass
            state.pop(cid, None)
    with open(state_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(state, f, indent=1, sort_keys=True)
    write_index(prev_dir, items, state, title)
    for fl in failed:
        log("  preview FAILED: " + fl)
    if not todo and not failed:
        return ""
    return "%d sheet(s) written%s -> %s" % (len(todo) - len(failed), ", %d failed" % len(failed) if failed else "",
                                           os.path.join(prev_dir, "index.html"))


def write_index(prev_dir, items, state, title):
    rows = []
    for c, path, anc in items:
        ent = state.get(c["id"])
        if not ent:
            continue
        i = ent.get("info") or {}
        fps = i.get("fps")
        n = i.get("frames", 0)
        what = ("%d frames, %.2f s at %g fps" % (n, n / fps, fps)) if fps else "still"
        a = ("anchor %d,%d r%d" % tuple(anc)) if anc else "no anchor"
        var = (" (colour variant of %s)" % c["variant_of"]) if c.get("variant_of") else ""
        rows.append('<figure><a href="%s.jpg"><img src="%s.jpg?%d" width="800" height="480" alt="%s"></a>'
                    '<figcaption><b>%s</b>%s &middot; %s &middot; %s &middot; %.0f KB &middot; %s<br>'
                    'frames at %s of the clip; cyan ring = lantern anchor (half size)</figcaption></figure>'
                    % (html.escape(c["id"]), html.escape(c["id"]), ent["sig"][2] // 1000000 % 100000000,
                       html.escape(c["id"]), html.escape(c["id"]), html.escape(var), html.escape(c["recipe"]),
                       what, i.get("bytes", 0) / 1024.0, a, " / ".join("%d%%" % int(f * 100) for f in FRACS)))
    doc = ("<!doctype html><meta charset=\"utf-8\"><title>Clip previews - %s</title>\n"
           "<style>body{background:#111;color:#ddd;font:14px sans-serif;margin:16px}figure{display:inline-block;"
           "margin:0 16px 24px 0;vertical-align:top}img{display:block;border:1px solid #444;max-width:100%%;height:auto}"
           "figcaption{margin-top:4px}</style>\n<h1>Clip previews - %s</h1>\n<p>Generated %s by tools/build_assets.py "
           "(contact sheets of the clips built from real art; regenerated on every build).</p>\n%s\n"
           % (html.escape(title or "Crystal Oracle"), html.escape(title or "Crystal Oracle"),
              datetime.datetime.now().strftime("%Y-%m-%d %H:%M"), "\n".join(rows) or "<p>No clips built from real art yet.</p>"))
    with open(os.path.join(prev_dir, "index.html"), "w", encoding="utf-8", newline="\n") as f:
        f.write(doc)
