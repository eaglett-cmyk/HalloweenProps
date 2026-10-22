"""
============================================================================
 co_media.py  -  ffmpeg recipes for the Crystal Oracle TF card
----------------------------------------------------------------------------
 Turns manifest recipes into ffmpeg filter graphs and finished card files:

   clips  -> MJPEG (baseline, yuvj420p 4:2:0, -q:v q, +bitexact) piped back
             into Python, validated frame by frame and packed to OMJ1 (omj.py).
             A clip whose biggest frame is over 96 KB is re-encoded at q+2,
             q+4 ... up to q=12 before giving up.
   stills -> one 800x480 baseline JPEG (-q:v 3 by default).
   layers -> the timeline recipe in co_layers.py (one filter graph).
   audio  -> RIFF WAV, PCM s16le, mono, 48 kHz, loudness-normalised
             (voice/sfx -16 LUFS, music -20 LUFS, TP -1.5, LRA 11), 120 Hz
             high-pass, 5 ms fade in/out, +bitexact so the header is the clean
             44-byte canonical one - and that header is verified byte by byte.

 Geometry: backgrounds / full-frame pictures are scaled to COVER 800x480
 (5:3) and cropped (centre, or top/bottom per still). Sprites keep their alpha
 (PNG/WebP) and are composited with ffmpeg's overlay; a "transparent" still
 without an alpha channel gets its flat black background keyed out.

 Every ffmpeg call runs with -nostdin and a timeout. Python 3 stdlib only.
============================================================================
"""
import os
import struct
import subprocess
import sys
import tempfile
import threading

import co_layers
import omj

# Recipe revisions: part of build_assets.py's cache keys. Bump one when its ffmpeg
# recipe changes so exactly those outputs rebuild on the next run.
CLIP_REV = 3                 # 3: layer "tint" takes colour specs (gradient / rainbow), applied after the sprite scale
AUDIO_REV = 4                # 4: voice "fx": "smudge"

W, H = omj.FRAME_W, omj.FRAME_H
KB_SCALE = 3                 # Ken Burns works on a 3x canvas so slow moves don't step a whole pixel
Q_RETRY_MAX = 12             # give up re-encoding above this -q:v
FF_TIMEOUT = 900             # seconds per ffmpeg call
MAX_INLINE_GRAPH = 24000     # longer filter graphs go through a file (-/filter_complex), Windows caps a command line at 32767
TEST_FONT_CANDIDATES = [r"C:\Windows\Fonts\arialbd.ttf", r"C:\Windows\Fonts\arial.ttf",
                        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/Library/Fonts/Arial Bold.ttf"]

FFMPEG = "ffmpeg"
FFPROBE = "ffprobe"
BASE = ["-hide_banner", "-nostdin", "-loglevel", "error", "-y"]


class MediaError(Exception):
    pass


def num(x):
    """Compact number for filter strings: 13.0 -> '13', 0.4615384615 -> '0.4615384615'."""
    s = ("%.10f" % float(x)).rstrip("0").rstrip(".")
    return s if s not in ("-0", "") else "0"


def run(args, want_stdout=False, timeout=FF_TIMEOUT, verbose=False, log=None):
    """Run ffmpeg/ffprobe (args[0]). Returns stdout bytes. Raises MediaError."""
    if verbose and log:
        log("    $ " + " ".join(_q(a) for a in args))
    try:
        r = subprocess.run(args, stdout=subprocess.PIPE if want_stdout else subprocess.DEVNULL,
                           stderr=subprocess.PIPE, timeout=timeout)
    except FileNotFoundError:
        raise MediaError("%s not found - install ffmpeg or put it on PATH" % args[0])
    except subprocess.TimeoutExpired:
        raise MediaError("%s timed out after %d s" % (os.path.basename(args[0]), timeout))
    if r.returncode != 0:
        err = r.stderr.decode("utf-8", "replace").strip().splitlines()
        tail = " | ".join(err[-4:]) if err else "exit code %d" % r.returncode
        raise MediaError("ffmpeg failed: " + tail)
    return r.stdout


def _q(a):
    return '"%s"' % a if (" " in a or ";" in a or "'" in a) else a


def tool_version(exe):
    try:
        out = subprocess.run([exe, "-version"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=20).stdout
        return out.decode("utf-8", "replace").splitlines()[0]
    except (OSError, subprocess.TimeoutExpired, IndexError):
        return None


def probe_duration(path):
    """Container duration in seconds (None if unknown)."""
    try:
        out = run([FFPROBE, "-v", "error", "-show_entries", "format=duration", "-of", "default=nw=1:nk=1", path],
                  want_stdout=True, timeout=60)
        return float(out.decode().strip())
    except (MediaError, ValueError):
        return None


# ---------------------------------------------------------------------------
# Picture chains
# ---------------------------------------------------------------------------
def _crop_y(crop, h):
    return {"center": "(ih-%d)/2" % h, "top": "0", "bottom": "ih-%d" % h}[crop]


def full_frame(still, inp, out, w=W, h=H, fps=13, frames=None):
    """Filter text taking [inp] (a looped still) to an opaque w x h picture [out].
    Opaque art: cover + crop. Transparent art: fit inside, flattened onto black."""
    if not still["transparent"]:
        return ("[%s]scale=%d:%d:force_original_aspect_ratio=increase:flags=lanczos,"
                "crop=%d:%d:(iw-%d)/2:%s,setsar=1,format=rgb24[%s]" % (inp, w, h, w, h, w, _crop_y(still["crop"], h), out))
    d = ":d=%s" % num((frames + 2) / fps) if frames else ""
    return ("color=c=black:s=%dx%d:r=%s%s[%s_k];[%s]%s,scale=%d:%d:force_original_aspect_ratio=decrease:flags=lanczos[%s_f];"
            "[%s_k][%s_f]overlay=x=(main_w-overlay_w)/2:y=(main_h-overlay_h)/2:shortest=1,setsar=1,format=rgb24[%s]"
            % (w, h, num(fps), d, out, inp, sprite_prefix(still), w, h, out, out, out, out))


_backdrop_cache = {}
_backdrop_lock = threading.Lock()


def backdrop_color(path):
    """'auto' keying of a transparent still WITHOUT an alpha channel: the colour of its four
    corners (32x32 area-averaged thumbnail). Returns (hex 'RRGGBB', 'green' | 'black' | 'other')."""
    try:
        st = os.stat(path)
    except OSError:
        return "000000", "black"
    k = (os.path.normcase(os.path.abspath(path)), st.st_size, st.st_mtime_ns)
    with _backdrop_lock:
        if k in _backdrop_cache:
            return _backdrop_cache[k]
    res = ("000000", "black")
    try:
        raw = run([FFMPEG] + BASE + ["-i", path, "-frames:v", "1", "-vf", "scale=32:32:flags=area,format=rgb24",
                                     "-f", "rawvideo", "pipe:1"], want_stdout=True, timeout=60)
        if len(raw) >= 32 * 32 * 3:
            px = [raw[(y * 32 + x) * 3:(y * 32 + x) * 3 + 3] for x, y in ((0, 0), (31, 0), (0, 31), (31, 31))]
            r, g, b = (sum(p[i] for p in px) // 4 for i in range(3))
            spread = max(max(p[i] for p in px) - min(p[i] for p in px) for i in range(3))
            if spread <= 40 and g >= 150 and g - max(r, b) >= 90:
                res = ("%02X%02X%02X" % (r, g, b), "green")
            elif spread <= 40 and max(r, g, b) <= 48:
                res = ("000000", "black")
            else:
                res = ("000000", "other")
    except MediaError:
        pass
    with _backdrop_lock:
        _backdrop_cache[k] = res
    return res


def sprite_prefix(still):
    """rgba conversion (+ colour key for 'transparent' art that has no alpha channel).
    key "auto": a green-screen backdrop (ChatGPT's fallback for "transparent") is keyed out,
    otherwise black; an explicit "#RRGGBB" keys that colour; false = no keying."""
    chain = "format=rgba"
    key = still.get("key", "auto")
    probe = still.get("probe")
    if still["transparent"] and key is not False:
        if key == "auto":
            if probe is not None and not probe[2]:
                hexv, kind = backdrop_color(still["src"]) if still.get("src") else ("000000", "black")
                if kind == "green":
                    chain += ",colorkey=color=0x%s:similarity=0.16:blend=0.08" % hexv
                else:
                    chain += ",colorkey=color=0x000000:similarity=0.10:blend=0.08"
        else:
            chain += ",colorkey=color=0x%s:similarity=0.10:blend=0.08" % key[1:]
    return chain


def sprite(still, inp, out, height):
    return "[%s]%s,scale=-1:%d:flags=lanczos[%s]" % (inp, sprite_prefix(still), height, out)


def _xexpr(x):
    return "(main_w-overlay_w)/2" if x == "center" else str(int(x))


def _yexpr(y, amp, period, fps, offset):
    if not amp:
        return str(int(y))
    return "'%d+%s*sin(2*PI*(n+%d)/%s)'" % (int(y), num(amp), int(offset), num(period * fps))


def _loop_in(path, fps, frames=None):
    a = ["-loop", "1", "-framerate", num(fps)]
    if frames:
        a += ["-t", num((frames + 2) / fps)]
    return a + ["-i", path]


# ---------------------------------------------------------------------------
# Recipe -> (inputs, filter_complex, expected frame count)
# ---------------------------------------------------------------------------
def graph_kenburns(c, stills):
    p = c["params"]
    st = stills[p["image"]]
    n, fps = c["frames"], c["fps"]
    z0, z1 = p["zoom"]
    span = max(1, n - 1)
    prog = "on/%d" % span
    z = "%s+%s*%s" % (num(z0), num(z1 - z0), prog)
    cx, cy = "iw/2-(iw/zoom/2)", "ih/2-(ih/zoom/2)"
    x, y = {"center": (cx, cy),
            "up": (cx, "(ih-ih/zoom)*(1-%s)" % prog),
            "down": (cx, "(ih-ih/zoom)*%s" % prog),
            "left": ("(iw-iw/zoom)*(1-%s)" % prog, cy),
            "right": ("(iw-iw/zoom)*%s" % prog, cy)}[p["pan"]]
    g = full_frame(st, "0:v", "big", W * KB_SCALE, H * KB_SCALE, fps, n)
    g += (";[big]zoompan=z='%s':x='%s':y='%s':d=1:s=%dx%d:fps=%s,setsar=1,format=yuvj420p[v]"
          % (z, x, y, W, H, num(fps)))
    return [_loop_in(st["src"], fps)], g, n


def graph_float(c, stills):
    p = c["params"]
    n, fps = c["frames"], c["fps"]
    g = full_frame(stills[p["bg"]], "0:v", "bg", fps=fps, frames=n)
    g += ";" + sprite(stills[p["sprite"]], "1:v", "sp", p["sprite_height"])
    g += (";[bg][sp]overlay=x=%s:y=%s:eval=frame:format=auto,setsar=1,format=yuvj420p[v]"
          % (_xexpr(p["x"]), _yexpr(p["y"], p["amp"], p["period"], fps, 0)))
    return [_loop_in(stills[p["bg"]]["src"], fps), _loop_in(stills[p["sprite"]]["src"], fps)], g, n


def graph_crossfade(c, stills):
    """Exact-frame crossfade chain. Timeline (frames): item0 holds H0, fade F into item1,
    item1 holds H1, ... With loop the last item fades back into a copy of item0, so the
    last frame leads straight into frame 0. Each item is its own finite stream that
    xfade picks up at its start frame T_k (offsets are set half a frame early so
    timestamp rounding can never shift a transition by a frame)."""
    p = c["params"]
    fps = c["fps"]
    items = list(p["items"])
    H_, F = p["hold_frames"], p["fade_frames"]
    K = len(items)
    loop = p["loop"] and K > 1 and F > 0
    seq = list(range(K)) + ([0] if loop else [])
    starts, lens = [], []
    t = 0
    for j, k in enumerate(seq):
        first, last = j == 0, j == len(seq) - 1
        if loop and last:                    # the copy of item 0: only the closing fade
            starts.append(t)
            lens.append(F)
            break
        fade_in = 0 if first else F
        fade_out = 0 if (last and not loop) or F == 0 else F
        starts.append(t)
        lens.append(fade_in + H_[k] + fade_out)
        t += fade_in + H_[k]                 # the next item's stream starts where this one's fade-out starts
    total = c["frames"]
    inputs, parts = [], []
    fl = p.get("float") or {}
    amp, period = fl.get("amp", 0), fl.get("period", 0)
    for j, k in enumerate(seq):
        it = items[k]
        L = lens[j]
        lab = "v%d" % j
        if it["kind"] == "image":
            idx = len(inputs)
            inputs.append(_loop_in(stills[it["still"]]["src"], fps, L))
            parts.append(full_frame(stills[it["still"]], "%d:v" % idx, "i%d" % j, fps=fps, frames=L))
            parts.append("[i%d]trim=end_frame=%d,settb=AVTB,setpts=N/(%s*TB),format=yuv444p[%s]" % (j, L, num(fps), lab))
        else:
            if p["bg"]:
                bi = len(inputs)
                inputs.append(_loop_in(stills[p["bg"]]["src"], fps, L))
                parts.append(full_frame(stills[p["bg"]], "%d:v" % bi, "b%d" % j, fps=fps, frames=L))
            else:
                parts.append("color=c=black:s=%dx%d:r=%s:d=%s,format=rgb24[b%d]" % (W, H, num(fps), num((L + 2) / fps), j))
            si = len(inputs)
            inputs.append(_loop_in(stills[it["still"]]["src"], fps, L))
            parts.append(sprite(stills[it["still"]], "%d:v" % si, "s%d" % j, it["sprite_height"]))
            parts.append("[b%d][s%d]overlay=x=%s:y=%s:eval=frame:format=auto,trim=end_frame=%d,settb=AVTB,"
                         "setpts=N/(%s*TB),format=yuv444p[%s]"
                         % (j, j, _xexpr(it["x"]), _yexpr(it["y"], amp, period, fps, starts[j]), L, num(fps), lab))
    if len(seq) == 1:
        parts.append("[v0]setsar=1,format=yuvj420p[v]")
    elif F == 0:
        parts.append("%sconcat=n=%d:v=1:a=0,setsar=1,format=yuvj420p[v]" % ("".join("[v%d]" % j for j in range(len(seq))), len(seq)))
    else:
        prev = "v0"
        for j in range(1, len(seq)):
            out = "x%d" % j if j < len(seq) - 1 else "xf"
            parts.append("[%s][v%d]xfade=transition=fade:duration=%s:offset=%s[%s]"
                         % (prev, j, num(F / fps), num((starts[j] - 0.5) / fps), out))
            prev = out
        parts.append("[xf]setsar=1,format=yuvj420p[v]")
    return inputs, ";".join(parts), total


def _video_chain(c, inp, out, pix):
    p = c["params"]
    dn = (",hqdn3d=%s" % p["denoise"]) if p["denoise"] else ""
    return ("[%s]fps=%s,scale=%d:%d:force_original_aspect_ratio=increase:flags=lanczos,crop=%d:%d:(iw-%d)/2:%s,"
            "setsar=1%s,format=%s[%s]" % (inp, num(c["fps"]), W, H, W, H, W, _crop_y(p["crop"], H), dn, pix, out))


def _video_in(c):
    p = c["params"]
    a = []
    if p["start"]:
        a += ["-ss", num(p["start"])]
    if p["seconds"]:
        a += ["-t", num(p["seconds"])]
    return a + ["-i", c["video_src"]]


# ---------------------------------------------------------------------------
# Encoding
# ---------------------------------------------------------------------------
def _encode(inputs, graph, frames, q, verbose=False, log=None):
    args = [FFMPEG] + BASE
    for i in inputs:
        args += i
    gfile = None
    if len(graph) > MAX_INLINE_GRAPH:
        fd, gfile = tempfile.mkstemp(prefix="co-graph-", suffix=".txt")
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(graph)
        args += ["-/filter_complex", gfile]
    else:
        args += ["-filter_complex", graph]
    args += ["-map", "[v]", "-an"]
    if frames:
        args += ["-frames:v", str(frames)]
    args += ["-fps_mode", "passthrough", "-c:v", "mjpeg", "-q:v", str(q),
             "-fflags", "+bitexact", "-flags:v", "+bitexact", "-f", "mjpeg", "pipe:1"]
    try:
        return run(args, want_stdout=True, verbose=verbose, log=log)
    finally:
        if gfile:
            try:
                os.remove(gfile)
            except OSError:
                pass


def _qs(q):
    out = [q]
    while out[-1] + 2 <= Q_RETRY_MAX:
        out.append(out[-1] + 2)
    return out


def build_clip(c, stills, tmpdir, verbose=False, log=None):
    """Build one clip. Returns (file bytes, info dict). Raises MediaError / omj.JpegError."""
    r = c["recipe"]
    fps = c["fps"]
    if r == "still":
        st = stills[c["params"]["image"]]
        g = full_frame(st, "0:v", "f", fps=fps, frames=1) + ";[f]format=yuvj420p[v]"
        data = _encode([["-i", st["src"]]], g, 1, c["q"], verbose=verbose, log=log)
        probs, _ = omj.still_problems(data)
        if probs:
            raise MediaError("bad still JPEG: " + "; ".join(probs))
        return data, {"frames": 1, "max": len(data), "avg": len(data), "q": c["q"], "bytes": len(data)}
    if r == "video":
        return _build_video(c, tmpdir, verbose, log)
    if r == "kenburns":
        inputs, g, n = graph_kenburns(c, stills)
    elif r == "float":
        inputs, g, n = graph_float(c, stills)
    elif r == "crossfade":
        inputs, g, n = graph_crossfade(c, stills)
    elif r == "layers":
        inputs, g, n = co_layers.graph(c, stills, sys.modules[__name__])
    else:
        raise MediaError("unknown recipe %r" % r)
    if n < 1:
        raise MediaError("recipe gives 0 frames")
    return _pack_with_retry(lambda q: _encode(inputs, g, n, q, verbose=verbose, log=log), c, n)


def _pack_with_retry(encode, c, expect):
    last = None
    for q in _qs(c["q"]):
        data = encode(q)
        blob, st = omj.pack_mjpeg(data, c["fps"])
        if expect and st["frames"] != expect:
            raise MediaError("ffmpeg produced %d frames, expected %d" % (st["frames"], expect))
        st["q"] = q
        st["bytes"] = len(blob)
        if st["max"] <= omj.MAX_FRAME_BYTES:
            if q != c["q"]:
                st["note"] = "q raised %d -> %d to fit 96 KB frames" % (c["q"], q)
            return blob, st
        last = st
    raise MediaError("biggest frame is %.1f KB even at q=%d (cap %d KB) - simplify the art (less grain/texture) "
                     "or shorten the clip" % (last["max"] / 1024.0, last["q"], omj.MAX_FRAME_BYTES // 1024))


def _build_video(c, tmpdir, verbose, log):
    p = c["params"]
    K = p["loop_fade_frames"]
    if not K:
        g = _video_chain(c, "0:v", "v", "yuvj420p")
        return _pack_with_retry(lambda q: _encode([_video_in(c)], g, None, q, verbose=verbose, log=log), c, None)
    # Pass 1: trimmed, scaled, denoised frames at near-lossless quality, so the frame
    # count N is known exactly before the loop crossfade is laid out.
    g1 = _video_chain(c, "0:v", "v", "yuvj444p")
    mid = _encode([_video_in(c)], g1, None, 2, verbose=verbose, log=log)
    n = len(omj.split_jpegs(mid))
    if n < 2 * K + 2:
        raise MediaError("only %d frames - too short for loop_fade_frames=%d" % (n, K))
    tmp = os.path.join(tmpdir, "%s_pass1.mjpeg" % c["id"])
    with open(tmp, "wb") as f:
        f.write(mid)
    fps = num(c["fps"])
    # main = frames K..N-1, head = frames 0..K-1; the last K frames of main fade into the
    # head, so the clip (N-K frames) ends right before its own frame 0 (= source frame K).
    g2 = ("[0:v]settb=AVTB,setpts=N/(%s*TB),split[a][b];[a]trim=start_frame=%d,setpts=PTS-STARTPTS[m];"
          "[b]trim=end_frame=%d,setpts=PTS-STARTPTS[h];[m][h]xfade=transition=fade:duration=%s:offset=%s,"
          "setsar=1,format=yuvj420p[v]" % (fps, K, K, num(K / c["fps"]), num((n - 2 * K - 0.5) / c["fps"])))
    inp = ["-f", "mjpeg", "-framerate", fps, "-i", tmp]
    try:
        return _pack_with_retry(lambda q: _encode([inp], g2, n - K, q, verbose=verbose, log=log), c, n - K)
    finally:
        try:
            os.remove(tmp)
        except OSError:
            pass


# ---------------------------------------------------------------------------
# Test card (--test-clips): labelled, loop-exact, 3 s
# ---------------------------------------------------------------------------
def test_font():
    for f in TEST_FONT_CANDIDATES:
        if os.path.exists(f):
            return f
    return None


def _ff_path(path):
    """A path for a filter option inside single quotes: forward slashes, ':' escaped."""
    return path.replace("\\", "/").replace(":", "\\:")


def build_test_clip(clip_id, fps, seconds=3.0, verbose=False, log=None):
    nf = max(2, int(round(seconds * fps)))
    dur = num((nf + 2) / fps)
    font = test_font()
    g = ("color=c=0x1A0B2E:s=%dx%d:r=%s:d=%s[bg];"
         "color=c=0xB07CFF:s=260x260:r=%s:d=%s,format=rgba,"
         "geq=r='176':g='124':b='255':a='255*clip((80+40*sin(2*PI*N/%d)-hypot(X-130,Y-130))/3,0,1)'[c];"
         "[bg][c]overlay=x=(main_w-overlay_w)/2:y=40:format=auto" % (W, H, num(fps), dur, num(fps), dur, nf))
    if font:
        ff = "fontfile='%s'" % _ff_path(font)
        safe = "".join(ch for ch in clip_id if ch.isalnum() or ch == "_")
        g += (",drawtext=%s:text='%s':fontsize=60:fontcolor=white:borderw=3:bordercolor=0x000000:x=(w-text_w)/2:y=300"
              ",drawtext=%s:text='TEST CLIP':fontsize=24:fontcolor=0xD8C8FF:x=(w-text_w)/2:y=12"
              ",drawtext=%s:text='frame %%{n} of %d   %s fps':fontsize=18:fontcolor=0xA898D0:x=(w-text_w)/2:y=372"
              % (ff, safe, ff, ff, nf, num(fps)))
    g += ",setsar=1,format=yuvj420p[v]"
    c = {"fps": float(fps), "q": 5, "id": clip_id}
    blob, st = _pack_with_retry(lambda q: _encode([], g, nf, q, verbose=verbose, log=log), c, nf)
    if not font:
        st["note"] = "no font found - test card has no label"
    return blob, st


# ---------------------------------------------------------------------------
# Audio
# ---------------------------------------------------------------------------
# Voice lines: silence at the edges only stretches the show step (a step lasts the
# voice length + 250 ms), so leading/trailing silence below -50 dB is trimmed (40 ms
# lead-in / 80 ms tail kept). TTS placeholders also get their long sentence pauses
# (System.Speech inserts ~1 s at '?' and '.') squeezed to 0.2 s.
TRIM_EDGES = ("silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.04,areverse,"
              "silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.08,areverse,")
TRIM_TTS = ("silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.04:stop_periods=-1:"
            "stop_duration=0.25:stop_threshold=-50dB:stop_silence=0.2,areverse,"
            "silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.08,areverse,")


# Voice only: a gentle 3:1 compressor above -20 dBFS before loudnorm. Short, peaky
# speech otherwise hits the -1.5 dBTP ceiling first and lands 1-5 LU under -16 LUFS
# (measured on System.Speech lines); with it lines measure about -16 +/- 1 LUFS.
VOICE_COMP = "acompressor=threshold=0.1:ratio=3:attack=5:release=80:makeup=2,"


def _pitch_speed(semitones, speed):
    """Pitch up by `semitones` at a net `speed` x tempo: asetrate raises pitch AND speed by
    2^(semitones/12), atempo then brings the speed back to `speed`."""
    ratio = 2.0 ** (semitones / 12.0)
    return ("aresample=48000,asetrate=%d,aresample=48000,atempo=%s,"
            % (int(round(48000 * ratio)), ("%.5f" % (speed / ratio)).rstrip("0")))


# Voice "fx" (manifest voice[].fx): applied before the trim / compressor / loudnorm.
VOICE_FX = {"smudge": _pitch_speed(5, 1.15)}      # Smudge = Oona's recording, +5 semitones, 1.15x


def audio_filter(category, trim=None, fx=None):
    lufs = -20 if category == "music" else -16
    pre = TRIM_TTS if trim == "tts" else TRIM_EDGES if trim == "edges" else ""
    if category == "voice":
        pre += VOICE_COMP
    return ("aformat=channel_layouts=mono,%shighpass=f=120,%sloudnorm=I=%d:TP=-1.5:LRA=11,aresample=48000,"
            "afade=t=in:d=0.005,areverse,afade=t=in:d=0.005,areverse" % (VOICE_FX.get(fx, "") if fx else "", pre, lufs))


def build_audio(src_args, category, out_path, trim=None, verbose=False, log=None, fx=None):
    """src_args: ['-i', path] or ['-f', 'lavfi', '-i', graph]. trim: None | 'edges' | 'tts'.
    fx: a VOICE_FX name or None. Writes out_path atomically. Returns info dict (seconds, bytes)."""
    tmp = out_path + ".part"
    args = [FFMPEG] + BASE + src_args + ["-vn", "-sn", "-dn", "-af", audio_filter(category, trim, fx), "-ar", "48000", "-ac", "1",
                                         "-c:a", "pcm_s16le", "-map_metadata", "-1", "-fflags", "+bitexact",
                                         "-flags:a", "+bitexact", "-f", "wav", tmp]
    try:
        run(args, verbose=verbose, log=log)
        probs, secs = verify_wav(tmp)
        if probs:
            raise MediaError("bad WAV header: " + "; ".join(probs))
        os.replace(tmp, out_path)
    finally:
        if os.path.exists(tmp):
            try:
                os.remove(tmp)
            except OSError:
                pass
    return {"seconds": secs, "bytes": os.path.getsize(out_path)}


def verify_wav(path):
    """Check the canonical 44-byte header audio.h expects. Returns (problems, seconds)."""
    probs = []
    try:
        size = os.path.getsize(path)
        with open(path, "rb") as f:
            h = f.read(44)
    except OSError as e:
        return ["cannot read: %s" % e], 0.0
    if len(h) < 44:
        return ["file shorter than 44 bytes"], 0.0
    riff, riff_size, wave, fmt_id, fmt_len, tag, ch, sr, byte_rate, align, bits, data_id, data_len = \
        struct.unpack("<4sI4s4sIHHIIHH4sI", h)
    if riff != b"RIFF" or wave != b"WAVE":
        probs.append("not RIFF/WAVE")
    if riff_size != size - 8:
        probs.append("RIFF size %d != file size - 8 (%d)" % (riff_size, size - 8))
    if fmt_id != b"fmt " or fmt_len != 16:
        probs.append("fmt chunk is %r len %d (need 'fmt ' len 16)" % (fmt_id, fmt_len))
    if tag != 1:
        probs.append("format tag %d (need 1 = PCM)" % tag)
    if ch != 1:
        probs.append("%d channels (need mono)" % ch)
    if sr != 48000:
        probs.append("%d Hz (need 48000)" % sr)
    if bits != 16 or align != 2 or byte_rate != 96000:
        probs.append("bits %d / align %d / byte rate %d (need 16 / 2 / 96000)" % (bits, align, byte_rate))
    if data_id != b"data":
        probs.append("'data' chunk is not at offset 36 (found %r) - extra chunks in the header" % data_id)
    elif data_len != size - 44:
        probs.append("data length %d != file size - 44 (%d)" % (data_len, size - 44))
    if data_len == 0 or data_len % 2:
        probs.append("data length %d (empty or odd)" % data_len)
    return probs, data_len / 96000.0
