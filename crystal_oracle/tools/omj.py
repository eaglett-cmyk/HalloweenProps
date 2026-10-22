"""
============================================================================
 omj.py  -  JPEG frame walker + OMJ1 clip packer / reader (Crystal Oracle)
----------------------------------------------------------------------------
 The screen firmware (firmware/crystal_oracle/media.h) plays clips stored as
 OMJ1 = length-prefixed MJPEG, little-endian:

   header 20 B : "OMJ1" u16 width, u16 height, u16 fps*100, u16 reserved(0),
                 u32 frameCount, u32 maxFrameBytes
   frames      : u32 len, len bytes of baseline JPEG (SOI..EOI), zero pad to 4 B

 JPEGDEC decodes every frame straight into an 800x480 RGB565 framebuffer, so
 EVERY frame must be: baseline (SOF0), 8-bit, 3 components, 4:2:0
 (Y 2x2, Cb 1x1, Cr 1x1), one scan, its own DQT + DHT tables, and exactly
 800x480. ffmpeg's mjpeg encoder with yuvj420p produces exactly that; this
 module proves it frame by frame. Stills (/oracle/clips/<id>.jpg) follow the
 same rules (one frame).

 Ported from the research packer (mjpeg2omj.py), plus:
   * strict per-frame validation (sampling factors, progressive, missing
     tables, truncation / a frame that runs into the next one)
   * a reader that re-parses a finished .omj (used by build_assets.py and
     by hand:  python -I tools\\omj.py info sd_card\\oracle\\clips\\x.omj)

 Python 3 standard library only.
============================================================================
"""
import struct
import sys

OMJ_MAGIC = b"OMJ1"
OMJ_HEADER = struct.Struct("<4sHHHHII")     # 20 bytes
assert OMJ_HEADER.size == 20

FRAME_W, FRAME_H = 800, 480
# Largest frame build_assets.py accepts (the firmware sizes its PSRAM frame
# buffer from the header's maxFrameBytes; this cap keeps SD reads per frame sane).
MAX_FRAME_BYTES = 96 * 1024

# Markers that carry no length field.
_STANDALONE = {0x01, 0xD8, 0xD9} | set(range(0xD0, 0xD8))
# SOFn markers (C0..CF except DHT C4, JPG C8, DAC CC).
_SOF = {m for m in range(0xC0, 0xD0) if m not in (0xC4, 0xC8, 0xCC)}


class JpegError(ValueError):
    pass


class JpegInfo:
    """What we learned while walking one JPEG."""
    __slots__ = ("sof", "precision", "width", "height", "comps", "dri", "scans", "dqt", "dht", "app", "com")

    def __init__(self):
        self.sof = None          # SOF marker byte (0xC0 = baseline)
        self.precision = 0
        self.width = 0
        self.height = 0
        self.comps = []          # [(component id, h<<4|v sampling byte), ...]
        self.dri = 0             # restart interval (0 = none)
        self.scans = 0           # number of SOS segments (1 for baseline)
        self.dqt = 0             # DQT segments seen
        self.dht = 0             # DHT segments seen
        self.app = []            # APPn markers seen
        self.com = 0             # COM segments seen

    def sampling(self):
        return tuple(s for _, s in self.comps)

    def describe(self):
        return "SOF%X %dx%d %d-bit comps=%s scans=%d dri=%d" % (
            (self.sof or 0xC0) - 0xC0, self.width, self.height, self.precision,
            "/".join("%02X" % s for s in self.sampling()), self.scans, self.dri)


def walk_jpeg(data, start=0):
    """Walk the JPEG whose SOI is at data[start]. Returns (end, JpegInfo) where
    data[start:end] is the complete image (SOI..EOI). Raises JpegError."""
    n = len(data)
    if data[start:start + 2] != b"\xff\xd8":
        raise JpegError("no SOI at offset %d" % start)
    info = JpegInfo()
    p = start + 2
    while True:
        if p + 2 > n:
            raise JpegError("truncated JPEG (no EOI) starting at %d" % start)
        if data[p] != 0xFF:
            raise JpegError("bad marker byte 0x%02X at %d" % (data[p], p))
        m = data[p + 1]
        if m == 0xFF:                       # fill byte before a marker
            p += 1
            continue
        if m == 0xD9:                       # EOI
            return p + 2, info
        if m == 0xD8:                       # a new SOI before our EOI: this frame is cut short
            raise JpegError("JPEG starting at %d is truncated (next SOI at %d)" % (start, p))
        if m in _STANDALONE:
            p += 2
            continue
        if p + 4 > n:
            raise JpegError("truncated segment header at %d" % p)
        ln = struct.unpack_from(">H", data, p + 2)[0]
        if ln < 2 or p + 2 + ln > n:
            raise JpegError("bad segment length %d at %d" % (ln, p))
        seg = data[p + 4:p + 2 + ln]
        if m in _SOF:
            if len(seg) < 6:
                raise JpegError("short SOF segment")
            prec, h, w, nc = struct.unpack_from(">BHHB", seg, 0)
            if len(seg) < 6 + 3 * nc:
                raise JpegError("short SOF component list")
            info.sof, info.precision, info.width, info.height = m, prec, w, h
            info.comps = [(seg[6 + 3 * k], seg[6 + 3 * k + 1]) for k in range(nc)]
        elif m == 0xC4:
            info.dht += 1
        elif m == 0xDB:
            info.dqt += 1
        elif m == 0xDD and len(seg) >= 2:   # DRI
            info.dri = struct.unpack_from(">H", seg, 0)[0]
        elif 0xE0 <= m <= 0xEF:
            info.app.append(m)
        elif m == 0xFE:
            info.com += 1
        if m == 0xDA:
            # SOS: skip its header, then the entropy-coded data. Inside it 0xFF is
            # followed by 0x00 (a stuffed byte) or RST0..7; anything else is the
            # next real marker (usually EOI), which the main loop handles.
            info.scans += 1
            p += 2 + ln
            while True:
                q = data.find(b"\xff", p)
                if q < 0 or q + 1 >= n:
                    raise JpegError("truncated scan data in JPEG starting at %d" % start)
                nxt = data[q + 1]
                if nxt == 0x00 or 0xD0 <= nxt <= 0xD7:
                    p = q + 2
                    continue
                p = q                       # a marker (or 0xFF fill bytes before one)
                break
            continue
        p += 2 + ln


def split_jpegs(data):
    """Split a concatenated MJPEG stream (ffmpeg -f mjpeg) into frames.
    Returns [(start, end, JpegInfo), ...]. Raises JpegError on garbage."""
    frames = []
    i = 0
    n = len(data)
    while i < n:
        s = data.find(b"\xff\xd8", i)
        if s < 0:
            if data[i:].strip(b"\x00"):
                raise JpegError("%d stray bytes after frame %d" % (n - i, len(frames)))
            break
        if s != i and data[i:s].strip(b"\x00"):
            raise JpegError("%d stray bytes before frame %d" % (s - i, len(frames)))
        e, info = walk_jpeg(data, s)
        frames.append((s, e, info))
        i = e
    return frames


def frame_problems(info, width=FRAME_W, height=FRAME_H):
    """Everything that would stop the firmware decoding this frame (empty list = OK)."""
    probs = []
    if info.sof is None:
        probs.append("no SOF segment")
    elif info.sof != 0xC0:
        probs.append("not baseline (SOF%X; need SOF0)" % (info.sof - 0xC0))
    if info.sof is not None and info.precision != 8:
        probs.append("%d-bit samples (need 8)" % info.precision)
    if (info.width, info.height) != (width, height):
        probs.append("%dx%d (need %dx%d)" % (info.width, info.height, width, height))
    if len(info.comps) != 3:
        probs.append("%d components (need 3 = YCbCr)" % len(info.comps))
    elif info.sampling() != (0x22, 0x11, 0x11):
        probs.append("chroma sampling %s (need 4:2:0 = 22/11/11)" % "/".join("%02X" % s for s in info.sampling()))
    if info.scans != 1:
        probs.append("%d scans (need exactly 1)" % info.scans)
    if not info.dqt:
        probs.append("no quantisation tables (DQT)")
    if not info.dht:
        probs.append("no Huffman tables (DHT)")
    return probs


def still_problems(data, width=FRAME_W, height=FRAME_H):
    """Check a single still JPEG file (bytes). Returns (problems, JpegInfo|None)."""
    try:
        frames = split_jpegs(data)
    except JpegError as e:
        return [str(e)], None
    if len(frames) != 1:
        return ["%d JPEG images in the file (need exactly 1)" % len(frames)], (frames[0][2] if frames else None)
    s, e, info = frames[0]
    probs = frame_problems(info, width, height)
    if s != 0:
        probs.append("%d bytes before SOI" % s)
    return probs, info


def pack(jpeg_frames, fps, width=FRAME_W, height=FRAME_H):
    """jpeg_frames: list of bytes (one complete JPEG each). Returns the OMJ1 file bytes."""
    if not jpeg_frames:
        raise ValueError("no frames to pack")
    fps100 = int(round(fps * 100))
    if not 1 <= fps100 <= 0xFFFF:
        raise ValueError("fps %r out of range" % fps)
    out = bytearray(OMJ_HEADER.pack(OMJ_MAGIC, width, height, fps100, 0, len(jpeg_frames), 0))
    mx = 0
    for f in jpeg_frames:
        ln = len(f)
        mx = max(mx, ln)
        out += struct.pack("<I", ln)
        out += f
        out += b"\0" * ((-ln) % 4)
    struct.pack_into("<I", out, 16, mx)
    return bytes(out)


def pack_mjpeg(mjpeg, fps, width=FRAME_W, height=FRAME_H):
    """Validate + pack an ffmpeg MJPEG stream. Returns (omj_bytes, stats dict).
    Raises JpegError listing the first bad frames."""
    frames = split_jpegs(mjpeg)
    if not frames:
        raise JpegError("no JPEG frames in the encoder output")
    bad = []
    for k, (_, _, info) in enumerate(frames):
        p = frame_problems(info, width, height)
        if p:
            bad.append("frame %d: %s" % (k, "; ".join(p)))
    if bad:
        raise JpegError("; ".join(bad[:3]) + (" (+%d more)" % (len(bad) - 3) if len(bad) > 3 else ""))
    blobs = [mjpeg[s:e] for s, e, _ in frames]
    sizes = [len(b) for b in blobs]
    stats = {
        "frames": len(blobs),
        "fps": fps,
        "max": max(sizes),
        "avg": sum(sizes) / len(sizes),
        "kbps": sum(sizes) / len(sizes) * fps / 1024.0,
        "dri": max(i.dri for _, _, i in frames),
    }
    return pack(blobs, fps, width, height), stats


def read_omj(data, validate=True):
    """Parse an OMJ1 file (bytes). Returns a dict with the header fields, the
    per-frame sizes and a list of problems (empty = the firmware will accept it)."""
    res = {"problems": [], "sizes": []}
    if len(data) < OMJ_HEADER.size:
        res["problems"].append("file shorter than the 20-byte header")
        return res
    magic, w, h, fps100, rsv, count, mx = OMJ_HEADER.unpack_from(data, 0)
    res.update(magic=magic, width=w, height=h, fps=fps100 / 100.0, reserved=rsv, frames=count, maxFrameBytes=mx)
    if magic != OMJ_MAGIC:
        res["problems"].append("bad magic %r" % magic)
        return res
    if (w, h) != (FRAME_W, FRAME_H):
        res["problems"].append("header says %dx%d (the panel is %dx%d)" % (w, h, FRAME_W, FRAME_H))
    if rsv != 0:
        res["problems"].append("reserved field is %d (expected 0)" % rsv)
    if fps100 == 0:
        res["problems"].append("fps is 0")
    if count == 0:
        res["problems"].append("0 frames")
    p = OMJ_HEADER.size
    real_max = 0
    for k in range(count):
        if p + 4 > len(data):
            res["problems"].append("file ends inside frame %d's length field" % k)
            break
        ln = struct.unpack_from("<I", data, p)[0]
        p += 4
        if ln == 0 or p + ln > len(data):
            res["problems"].append("frame %d: bad length %d" % (k, ln))
            break
        frame = data[p:p + ln]
        res["sizes"].append(ln)
        real_max = max(real_max, ln)
        if validate:
            try:
                end, info = walk_jpeg(frame, 0)
                if end != ln:
                    res["problems"].append("frame %d: %d bytes after EOI" % (k, ln - end))
                fp = frame_problems(info, w, h)
                if fp:
                    res["problems"].append("frame %d: %s" % (k, "; ".join(fp)))
            except JpegError as e:
                res["problems"].append("frame %d: %s" % (k, e))
        pad = (-ln) % 4
        if data[p + ln:p + ln + pad].strip(b"\0"):
            res["problems"].append("frame %d: non-zero padding" % k)
        p += ln + pad
    if len(res["sizes"]) == count and p != len(data):
        res["problems"].append("%d trailing bytes after the last frame" % (len(data) - p))
    if res["sizes"] and real_max != mx:
        res["problems"].append("header maxFrameBytes %d but the largest frame is %d" % (mx, real_max))
    if res["sizes"]:
        res["avg"] = sum(res["sizes"]) / len(res["sizes"])
    return res


def _main(argv):
    import argparse
    ap = argparse.ArgumentParser(prog="omj.py", description="Inspect or pack OMJ1 clips / check JPEG stills.")
    sub = ap.add_subparsers(dest="cmd", required=True)
    pi = sub.add_parser("info", help="print header + frame stats of .omj / .jpg / .mjpeg files")
    pi.add_argument("files", nargs="+")
    pp = sub.add_parser("pack", help="pack an ffmpeg MJPEG stream into OMJ1")
    pp.add_argument("src")
    pp.add_argument("dst")
    pp.add_argument("--fps", type=float, required=True)
    a = ap.parse_args(argv)
    rc = 0
    if a.cmd == "pack":
        with open(a.src, "rb") as f:
            omj, st = pack_mjpeg(f.read(), a.fps)
        with open(a.dst, "wb") as f:
            f.write(omj)
        print("%s: %d frames @ %.2f fps, avg %.1f KB, max %.1f KB" % (a.dst, st["frames"], a.fps, st["avg"] / 1024, st["max"] / 1024))
        if st["max"] > MAX_FRAME_BYTES:
            print("   WARNING: max frame is over the %d KB cap" % (MAX_FRAME_BYTES // 1024))
        return 0
    for path in a.files:
        with open(path, "rb") as f:
            data = f.read()
        if data[:4] == OMJ_MAGIC:
            r = read_omj(data)
            print("%s: OMJ1 %dx%d %.2f fps, %d frames (%.1f s), max %.1f KB, avg %.1f KB" % (
                path, r["width"], r["height"], r["fps"], r["frames"], r["frames"] / (r["fps"] or 1),
                r["maxFrameBytes"] / 1024, r.get("avg", 0) / 1024))
            if r.get("maxFrameBytes", 0) > MAX_FRAME_BYTES:
                r["problems"].append("max frame %d B is over the %d KB cap" % (r["maxFrameBytes"], MAX_FRAME_BYTES // 1024))
        else:
            try:
                fr = split_jpegs(data)
                probs = [("frame %d: " % k) + "; ".join(frame_problems(i)) for k, (_, _, i) in enumerate(fr) if frame_problems(i)]
                print("%s: %d JPEG frame(s), %.1f KB, first: %s" % (path, len(fr), len(data) / 1024, fr[0][2].describe() if fr else "-"))
                r = {"problems": probs}
            except JpegError as e:
                r = {"problems": [str(e)]}
        for p in r["problems"]:
            print("   PROBLEM: " + p)
        if r["problems"]:
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
