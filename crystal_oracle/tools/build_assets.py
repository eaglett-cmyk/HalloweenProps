#!/usr/bin/env python3
"""
============================================================================
 build_assets.py  -  Crystal Oracle TF-card builder
----------------------------------------------------------------------------
 assets/manifest.json + assets/source/*  ->  sd_card/oracle/...  (ffmpeg)

   python -I tools\\build_assets.py                    build everything that changed
   python -I tools\\build_assets.py --check            validate + write assets/ASSET-STATUS.md
   python -I tools\\build_assets.py --placeholders --test-clips
                                                     ...and fill the gaps with TTS voice,
                                                     synth sounds and labelled test clips
   python -I tools\\build_assets.py --gen-defaults     regenerate firmware story_defaults.h
   python -I tools\\build_assets.py --previews         (re)write every clip's contact sheet in
                                                     assets/previews (a normal build writes
                                                     the changed ones)

 Output (copy sd_card/oracle to the TF card root):
   oracle/clips/<id>.omj | .jpg   OMJ1 clips / 800x480 stills   (media.h)
   oracle/voice/<id>.wav          WAV s16le mono 48 kHz          (audio.h)
   oracle/sfx/<id>.wav, oracle/music/<id>.wav
   oracle/captions.ini            from the manifest "words" + [anchor] (lantern glow per clip)
   oracle/config.ini              template from settings.h - ONLY if missing
   oracle/README.txt
 Incremental: every output's inputs are hashed into assets/.build_cache.json;
 an output is rebuilt only when its manifest entry, its source files or this
 tool's TOOL_VERSION change. Contact sheets of the built clips go to
 assets/previews/<id>.jpg (+ index.html) - git-ignored output.

 Helper modules (same folder): omj.py (OMJ1 + JPEG checks), co_manifest.py
 (validation), co_media.py (ffmpeg recipes), co_layers.py (the "layers"
 timeline recipe + anchors), co_preview.py (contact sheets), co_synth.py
 (placeholder sounds + Windows TTS), co_firmware.py (config/captions/
 story_defaults), co_status.py (ASSET-STATUS.md). Python 3 standard library only; ffmpeg + ffprobe on PATH.
 docs/DESIGN.md section 7 and tools/README.md describe the pipeline.
============================================================================
"""
import argparse
import concurrent.futures
import datetime
import fnmatch
import hashlib
import json
import os
import shutil
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)            # "python -I" does not put the script's folder on sys.path

import omj                    # noqa: E402
import co_manifest as cm      # noqa: E402
import co_media as media      # noqa: E402
import co_synth as synth      # noqa: E402
import co_firmware as fw      # noqa: E402
import co_status as status    # noqa: E402
import co_preview as preview  # noqa: E402

TOOL_VERSION = "1.1.0"        # part of every cache key: bump it when a recipe changes so outputs rebuild
PROJECT = os.path.dirname(HERE)
OUT_DIRS = ("clips", "voice", "sfx", "music")

_print_lock = threading.Lock()


def log(msg=""):
    with _print_lock:
        print(msg, flush=True)


def rel(path, base=None):
    try:
        r = os.path.relpath(path, base or os.getcwd())
        return r if not r.startswith("..\\..") and not r.startswith("../..") else path
    except ValueError:
        return path


def key_of(*parts):
    return hashlib.sha1(json.dumps(parts, sort_keys=True, default=str).encode("utf-8")).hexdigest()


def write_atomic(path, data, text=False):
    tmp = path + ".part"
    if text:
        with open(tmp, "w", encoding="utf-8", newline="") as f:
            f.write(data)
    else:
        with open(tmp, "wb") as f:
            f.write(data)
    os.replace(tmp, path)


# ---------------------------------------------------------------------------
# Incremental-build cache (assets/.build_cache.json)
# ---------------------------------------------------------------------------
class Cache:
    def __init__(self, path):
        self.path = path
        self.lock = threading.Lock()
        self.data = {"version": 1, "files": {}, "outputs": {}}
        try:
            with open(path, "r", encoding="utf-8") as f:
                d = json.load(f)
            if isinstance(d, dict) and d.get("version") == 1:
                self.data["files"] = d.get("files", {})
                self.data["outputs"] = d.get("outputs", {})
        except (OSError, ValueError):
            pass

    @staticmethod
    def norm(path):
        return os.path.normcase(os.path.abspath(path))

    def file_hash(self, path):
        st = os.stat(path)
        k = self.norm(path)
        with self.lock:
            e = self.data["files"].get(k)
            if e and e.get("size") == st.st_size and e.get("mtime") == st.st_mtime_ns:
                return e["sha1"]
        h = hashlib.sha1()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        with self.lock:
            self.data["files"][k] = {"size": st.st_size, "mtime": st.st_mtime_ns, "sha1": h.hexdigest()}
        return h.hexdigest()

    def get(self, out):
        with self.lock:
            return self.data["outputs"].get(self.norm(out))

    def put(self, out, entry):
        with self.lock:
            self.data["outputs"][self.norm(out)] = entry

    def drop(self, out):
        with self.lock:
            self.data["outputs"].pop(self.norm(out), None)

    def save(self):
        with self.lock:
            self.data["outputs"] = {k: v for k, v in self.data["outputs"].items() if os.path.exists(k)}
            self.data["files"] = {k: v for k, v in self.data["files"].items() if os.path.exists(k)}
            os.makedirs(os.path.dirname(self.path), exist_ok=True)
            write_atomic(self.path, json.dumps(self.data, indent=1, sort_keys=True), text=True)


# ---------------------------------------------------------------------------
# Jobs
# ---------------------------------------------------------------------------
class Job:
    def __init__(self, cat, ident, mode, out, key, entry, label=""):
        self.cat, self.id, self.mode, self.out, self.key, self.entry = cat, ident, mode, out, key, entry
        self.label = label or mode
        self.reason = ""
        self.tts_wav = None


class Builder:
    def __init__(self, a):
        self.a = a
        self.out_root = os.path.abspath(a.out)
        self.oracle = os.path.join(self.out_root, "oracle")
        self.assets = os.path.abspath(a.assets)
        self.manifest = os.path.abspath(a.manifest or os.path.join(self.assets, "manifest.json"))
        fwroot = os.path.abspath(a.firmware)
        self.fwdir = os.path.join(fwroot, "crystal_oracle")
        self.plinth = os.path.join(fwroot, "crystal_plinth")
        self.cache_dir = os.path.join(self.assets, ".cache")
        self.status_path = os.path.join(self.assets, "ASSET-STATUS.md")
        self.cache = Cache(os.path.join(self.assets, ".build_cache.json"))
        self.rep = cm.Report()
        self.model = None
        self.failed = set()
        self.built = []
        self.tts_prefer = list(synth.DEFAULT_TTS_PREFER)
        self.link_state = "not checked"
        self.fields, self.enums, self.notes, self.table = [], {}, {}, None
        self.story_header, self.story_pairs = None, []

    # ---- paths ----
    def clip_out(self, c):
        p = os.path.join(self.oracle, "clips", c["id"] + c["ext"])
        alt = os.path.join(self.oracle, "clips", c["id"] + (".omj" if c["ext"] == ".jpg" else ".jpg"))
        return p, alt

    def audio_out(self, cat, ident):
        return os.path.join(self.oracle, cat, ident + ".wav")

    def out_rel(self, p):
        return os.path.relpath(p, self.out_root).replace("\\", "/")

    # ---- load + validate ----
    def load(self):
        rep = self.rep
        data = cm.load_json(self.manifest, rep)
        if data is None and not os.path.exists(self.manifest):
            ex = os.path.join(self.assets, "manifest.example.json")
            if os.path.exists(ex):
                rep.info("manifest", "start from the example: copy %s to %s" % (rel(ex), rel(self.manifest)))
        self.model = cm.validate(data if isinstance(data, dict) else {"schema": cm.SCHEMA}, self.manifest,
                                 cm.parse_cues_h(os.path.join(self.fwdir, "cues.h")), rep)
        cm.resolve_sources(self.model, os.path.join(self.assets, "source"), rep)
        # placeholder TTS voice: --tts-voice, else voice_style.tts_voice, then the built-in preference list
        want = self.a.tts_voice or self.model.voice_style.get("tts_voice")
        if isinstance(want, str) and want.strip():
            self.tts_prefer = [want.strip()] + [v for v in synth.DEFAULT_TTS_PREFER if v != want.strip()]
        self.check_link()
        sh = os.path.join(self.fwdir, "settings.h")
        try:
            self.fields, self.enums, self.notes = fw.parse_settings_h(sh)
            if len(self.fields) < 10:
                rep.warn("settings.h", "only %d settings found - keep the '// [section]' lines and one "
                                       "'type name = default; // comment' per line so config.ini can be generated" % len(self.fields))
        except OSError:
            rep.warn("settings.h", "not found - no config.ini template can be written")
        self.table = fw.parse_settings_table(os.path.join(self.fwdir, "settings.cpp"))
        if self.table is None:
            rep.warn("settings.cpp", "missing or no FIELDS table found - the config.ini template guesses the keys "
                                     "(snake_case settings.h names)")
        else:
            for w in fw.check_settings_table(self.fields, self.table):
                rep.warn("settings.cpp", w)
        self.story_header, self.story_pairs = fw.parse_story_defaults(os.path.join(self.fwdir, "story_defaults.h"))
        if data is not None and self.story_pairs:
            dk = [k for k, _ in self.story_pairs]
            missing = [k for k in dk if k not in self.model.words]
            extra = [k for k in self.model.words if k not in dk]

            def short(keys):
                return ", ".join(keys[:12]) + (" ... (+%d more)" % (len(keys) - 12) if len(keys) > 12 else "")
            if missing:
                rep.info("words", "%d key(s) of story_defaults.h are not in the manifest (the firmware's built-in "
                                  "words are used for them): %s" % (len(missing), short(missing)))
            if extra:
                rep.info("words", "%d new key(s) that story_defaults.h doesn't have (run --gen-defaults if the "
                                  "firmware should know them): %s" % (len(extra), short(extra)))

    def check_link(self):
        a = os.path.join(self.fwdir, "crystal_link.h")
        b = os.path.join(self.plinth, "crystal_link.h")
        if not os.path.exists(a):
            self.rep.warn("crystal_link.h", "screen copy %s is missing" % rel(a))
            self.link_state = "screen copy missing"
            return
        if not os.path.exists(b):
            self.rep.warn("crystal_link.h", "plinth copy %s does not exist yet - cannot compare the two copies" % rel(b))
            self.link_state = "plinth copy missing (not compared)"
            return
        with open(a, "rb") as f:
            da = f.read()
        with open(b, "rb") as f:
            db = f.read()

        def ver(d):
            import re
            m = re.search(rb"#define\s+LINK_VERSION\s+(\d+)", d)
            return m.group(1).decode() if m else "?"
        if da == db:
            self.link_state = "both copies identical (LINK_VERSION %s)" % ver(da)
        elif da.replace(b"\r\n", b"\n") == db.replace(b"\r\n", b"\n"):
            self.rep.error("crystal_link.h", "the screen and plinth copies differ only in line endings - make them byte-identical")
            self.link_state = "DIFFER (line endings)"
        else:
            self.rep.error("crystal_link.h", "the screen and plinth copies differ (LINK_VERSION %s vs %s) - change both "
                                             "together and bump LINK_VERSION" % (ver(da), ver(db)))
            self.link_state = "DIFFER"

    # ---- reporting ----
    def print_report(self, show_info=False):
        for e in self.rep.errors:
            log("  ERROR    " + e)
        for w in self.rep.warnings:
            log("  warning  " + w)
        if show_info:
            for i in self.rep.infos:
                log("  note     " + i)

    def coverage_line(self):
        m = self.model

        def frac(items, has):
            mv = [x for x in items if x["priority"] == "MVP"]
            return "%d/%d" % (sum(1 for x in mv if has(x)), len(mv))
        return ("MVP sources: stills %s, clips %s buildable, voice %s, sfx %s, music %s" % (
            frac(list(m.stills.values()), lambda s: s["src"]), frac(m.clips, lambda c: cm.clip_buildable(c, m)),
            frac(m.voice, lambda v: v["src"]), frac(m.sfx, lambda s: s["src"]), frac(m.music, lambda s: s["src"])))

    # ---- planning ----
    def _only(self, cat, ident, refs=()):
        if not self.a.only:
            return True
        pats = [p.strip() for p in self.a.only.split(",") if p.strip()]
        for p in pats:
            pc = None
            if ":" in p:
                pc, p = p.split(":", 1)
                pc = {"clips": "clip", "clip": "clip", "voice": "voice", "sfx": "sfx", "music": "music",
                      "still": "still", "stills": "still"}.get(pc, pc)
            if pc in (None, cat) and fnmatch.fnmatchcase(ident, p):
                return True
            if cat == "clip" and pc in (None, "still") and any(fnmatch.fnmatchcase(r, p) for r in refs):
                return True
        return False

    def _ours_or_absent(self, *paths):
        """True if none of the paths exists, or every existing one is a placeholder we made."""
        for p in paths:
            if p and os.path.exists(p):
                ent = self.cache.get(p)
                if not ent or ent.get("mode") == "real":
                    return False
        return True

    def plan(self):
        m, a, jobs = self.model, self.a, []
        for c in m.clips:
            p, alt = self.clip_out(c)
            if not self._only("clip", c["id"], c["refs"]):
                continue
            if cm.clip_buildable(c, m):
                srcs = []
                for sid in cm.clip_used_stills(c, m):
                    st = m.stills[sid]
                    srcs.append([sid, st["transparent"], st["crop"], st["key"],
                                 bool(st["probe"] and st["probe"][2]), self.cache.file_hash(st["src"])])
                if c.get("video_src"):
                    srcs.append(["video", self.cache.file_hash(c["video_src"])])
                canon = {k: c.get(k) for k in ("id", "recipe", "fps", "q", "params", "ext", "frames")}
                jobs.append(Job("clip", c["id"], "real", p, key_of(TOOL_VERSION, media.CLIP_REV, media.PROFILE, "clip", canon, srcs), c, c["recipe"]))
            elif a.test_clips:
                tp = os.path.join(self.oracle, "clips", c["id"] + ".omj")
                if self._ours_or_absent(p, alt):
                    jobs.append(Job("clip", c["id"], "test", tp, key_of(TOOL_VERSION, media.CLIP_REV, "testclip", c["id"], c["fps"]), c, "test card"))
        for cat, entries in (("voice", m.voice), ("sfx", m.sfx), ("music", m.music)):
            for e in entries:
                if not self._only(cat, e["id"]):
                    continue
                p = self.audio_out(cat, e["id"])
                fx = e.get("fx") or None
                fxk = [fx] if fx else []            # only lines with an fx carry it in their key
                if e["src"]:
                    jobs.append(Job(cat, e["id"], "real", p, key_of(TOOL_VERSION, media.AUDIO_REV, "audio", cat, self.cache.file_hash(e["src"]), *fxk),
                                    e, "real " + os.path.splitext(e["src"])[1].lstrip(".") + (" +" + fx if fx else "")))
                elif a.placeholders and self._ours_or_absent(p):
                    if cat == "voice":
                        if e["text"]:
                            jobs.append(Job(cat, e["id"], "tts", p, key_of(TOOL_VERSION, media.AUDIO_REV, "tts", e["text"], self.tts_prefer,
                                                                          a.tts_rate, *fxk), e, "tts" + (" +" + fx if fx else "")))
                    else:
                        name = e["synth"] or synth.default_synth(cat, e["id"])
                        secs = e["seconds"] or synth.SECONDS.get(name, 1.0)
                        jobs.append(Job(cat, e["id"], "synth", p, key_of(TOOL_VERSION, media.AUDIO_REV, "synth", cat, e["id"], name, secs),
                                        dict(e, synth_name=name, synth_secs=secs), "synth " + name))
        todo = []
        force = a.force or (a.clean and a.dry_run)     # a real --clean has already emptied the cache
        for j in jobs:
            ent = self.cache.get(j.out)
            same = bool(ent and ent.get("key") == j.key and os.path.exists(j.out))
            if same and not force:
                continue
            j.reason = ("forced" if same else "new" if not os.path.exists(j.out) else
                        "inputs changed" if ent else "not built by this tool before")
            if same and a.clean:
                j.reason = "clean"
            todo.append(j)
        return jobs, todo

    # ---- execution ----
    def run_job(self, j, tmpdir):
        t = time.time()
        try:
            os.makedirs(os.path.dirname(j.out), exist_ok=True)
            if j.cat == "clip":
                c = j.entry
                if j.mode == "real":
                    blob, info = media.build_clip(c, self.model.stills, tmpdir, self.a.verbose, log)
                else:
                    blob, info = media.build_test_clip(c["id"], c["fps"], verbose=self.a.verbose, log=log)
                info["fps"] = c["fps"]
                write_atomic(j.out, blob)
                other = os.path.splitext(j.out)[0] + (".jpg" if j.out.endswith(".omj") else ".omj")
                if os.path.exists(other):          # findVisual() prefers .omj: never leave a shadowing twin
                    os.remove(other)
                    self.cache.drop(other)
                    log("  removed stale %s" % self.out_rel(other))
                if c["ext"] == ".jpg" and j.mode == "real":
                    desc = "still JPEG %s, q%d" % (status._kb(info["bytes"]), info["q"])
                    if info["bytes"] > 300 * 1024:
                        desc += "  (big: slow to decode - use calmer art or a higher q)"
                else:
                    desc = "%3d fr  max %5.1f KB  avg %5.1f KB  %s  q%d" % (info["frames"], info["max"] / 1024.0,
                                                                         info["avg"] / 1024.0, status._kb(info["bytes"]), info["q"])
                if info.get("note"):
                    desc += "  (%s)" % info["note"]
            else:
                if j.mode == "real":
                    src = ["-i", j.entry["src"]]
                elif j.mode == "tts":
                    if not j.tts_wav:
                        raise media.MediaError("TTS produced nothing for this line")
                    src = ["-i", j.tts_wav]
                else:
                    g, _ = synth.synth_graph(j.entry["synth_name"], j.entry["synth_secs"], seed=j.id)
                    src = ["-f", "lavfi", "-i", g]
                trim = ("tts" if j.mode == "tts" else "edges") if j.cat == "voice" else None
                fx = j.entry.get("fx") if j.cat == "voice" else None
                info = media.build_audio(src, j.cat, j.out, trim=trim, verbose=self.a.verbose, log=log, fx=fx)
                desc = "%6.2f s  %s" % (info["seconds"], status._kb(info["bytes"]))
                if j.cat == "voice" and info["seconds"] > 8.0:
                    desc += "  (long: steps are capped, see DESIGN.md 5.1)"
            self.cache.put(j.out, {"key": j.key, "mode": j.mode, "cat": j.cat, "id": j.id, "info": info,
                                   "built": datetime.datetime.now().isoformat(timespec="seconds")})
            self.built.append(j)
            log("[%-5s] %-22s %-14s %s  (%.1f s)" % (j.cat, j.id, j.label[:14], desc, time.time() - t))
            return True
        except (media.MediaError, omj.JpegError, OSError, ValueError) as e:
            self.failed.add((j.cat, j.id))
            log("[%-5s] %-22s FAILED: %s" % (j.cat, j.id, e))
            return False

    def run_tts(self, todo):
        tts = [j for j in todo if j.mode == "tts"]
        if not tts:
            return
        t = time.time()
        log("Speaking %d placeholder voice line(s) with Windows TTS ..." % len(tts))
        paths, voice, err = synth.run_tts([(j.id, j.entry["text"]) for j in tts], os.path.join(self.cache_dir, "tts"),
                                          self.tts_prefer, self.a.tts_rate, log=log)
        if err:
            log("  TTS problem: %s" % err)
        log("  voice: %s  (%.1f s)" % (voice or "?", time.time() - t))
        for j in tts:
            j.tts_wav = paths.get(j.id)

    # ---- built clips, anchors, previews ----
    def real_clips(self):
        """[(clip, output path)] for every clip on the card that was built from real art."""
        out = []
        for c in self.model.clips:
            p, alt = self.clip_out(c)
            path = p if os.path.exists(p) else alt if os.path.exists(alt) else None
            ent = self.cache.get(path) if path else None
            if ent and ent.get("mode") == "real":
                out.append((c, path))
        return out

    def anchors(self):
        """[(clip id, (x, y, r))] for the built clips that have an anchor_layer."""
        res = []
        for c, _ in self.real_clips():
            a = cm.clip_anchor(c, self.model)
            if a:
                res.append((c["id"], a))
        return res

    def write_previews(self, force=False):
        items = [(c, path, cm.clip_anchor(c, self.model)) for c, path in self.real_clips()]
        return preview.update(os.path.join(self.assets, "previews"), items, self.model.title, force=force,
                              jobs=self.a.jobs, verbose=self.a.verbose, log=log)

    # ---- text outputs ----
    def write_text_outputs(self):
        m = self.model
        wrote = []
        os.makedirs(self.oracle, exist_ok=True)
        anchors = self.anchors()
        if m.words or anchors:
            p = os.path.join(self.oracle, "captions.ini")
            txt = fw.captions_ini(m.words, m.title, rel(self.manifest, PROJECT).replace("\\", "/"), anchors)
            old = None
            if os.path.exists(p):
                with open(p, "r", encoding="utf-8", newline="") as f:
                    old = f.read()
            if old != txt:
                write_atomic(p, txt, text=True)
                wrote.append("captions.ini")
        p = os.path.join(self.oracle, "config.ini")
        card_cfg = os.path.join(PROJECT, "sd_card", "oracle", "config.ini")
        if not os.path.exists(p) and media.PROFILE == "ipad" and os.path.exists(card_cfg):
            os.makedirs(self.oracle, exist_ok=True)
            shutil.copyfile(card_cfg, p)       # the iPad build starts from the card's settings
            wrote.append("config.ini (copied from sd_card)")
        if not os.path.exists(p):
            if self.fields:
                txt = fw.config_template(self.fields, self.enums, self.table,
                                         ("firmware/crystal_oracle/settings.cpp", "firmware/crystal_oracle/settings.h"))
                write_atomic(p, _ascii_fold(txt), text=True)
                wrote.append("config.ini (new template)")
        return wrote

    def write_readme(self):
        counts = {"clips": 0, "voice": 0, "sfx": 0, "music": 0, "placeholders": 0}
        for d in OUT_DIRS:
            dp = os.path.join(self.oracle, d)
            if os.path.isdir(dp):
                for n in os.listdir(dp):
                    full = os.path.join(dp, n)
                    if os.path.isfile(full) and not n.endswith(".part"):
                        counts[d] += 1
                        ent = self.cache.get(full)
                        if ent and ent.get("mode") in ("test", "tts", "synth"):
                            counts["placeholders"] += 1
        txt = fw.card_readme(self.model.title, datetime.datetime.now().strftime("%Y-%m-%d %H:%M"), counts)
        write_atomic(os.path.join(self.oracle, "README.txt"), txt, text=True)

    def orphans(self):
        return [self.out_rel(full) for full in self._orphan_paths()]

    def prune_placeholder_orphans(self):
        """Delete card files that THIS tool made as placeholders (TTS voice, synth sound, test card) but the
        manifest no longer lists - e.g. after a story switch an old extra voice variant (milestone_2) would
        still be counted and played by the firmware. Real files are never deleted (only reported)."""
        removed = []
        for full in self._orphan_paths():
            ent = self.cache.get(full)
            if ent and ent.get("mode") in ("tts", "synth", "test"):
                try:
                    os.remove(full)
                except OSError as e:
                    log("  could not remove stale placeholder %s: %s" % (self.out_rel(full), e))
                    continue
                self.cache.drop(full)
                removed.append(self.out_rel(full))
        return removed

    def _orphan_paths(self):
        expected = set()
        for c in self.model.clips:
            expected.add(Cache.norm(self.clip_out(c)[0]))
            if not os.path.exists(self.clip_out(c)[0]):
                expected.add(Cache.norm(os.path.join(self.oracle, "clips", c["id"] + ".omj")))   # test card for a .jpg clip
        for cat in ("voice", "sfx", "music"):
            for e in getattr(self.model, cat):
                expected.add(Cache.norm(self.audio_out(cat, e["id"])))
        out = []
        for d in OUT_DIRS:
            dp = os.path.join(self.oracle, d)
            if os.path.isdir(dp):
                for n in sorted(os.listdir(dp)):
                    full = os.path.join(dp, n)
                    if os.path.isfile(full) and Cache.norm(full) not in expected:
                        out.append(full)
        return out

    def write_status(self):
        ctx = {"clip_out": self.clip_out, "audio_out": self.audio_out, "rel": self.out_rel,
               "cache_outputs": self.cache.data["outputs"], "failed": self.failed,
               "manifest_rel": rel(self.manifest, PROJECT).replace("\\", "/"),
               "out_rel": rel(self.out_root, PROJECT).replace("\\", "/"),
               "link_state": self.link_state, "orphans": self.orphans() if os.path.isdir(self.oracle) else []}
        os.makedirs(os.path.dirname(self.status_path), exist_ok=True)
        status.write_status(self.status_path, self.model, ctx, self.rep,
                            {"when": datetime.datetime.now().strftime("%Y-%m-%d %H:%M")})

    def clean(self, dry):
        n = 0
        for d in OUT_DIRS:
            dp = os.path.join(self.oracle, d)
            if os.path.isdir(dp):
                for name in os.listdir(dp):
                    full = os.path.join(dp, name)
                    if os.path.isfile(full):
                        n += 1
                        if not dry:
                            os.remove(full)
        for name in ("captions.ini", "README.txt"):
            full = os.path.join(self.oracle, name)
            if os.path.exists(full):
                n += 1
                if not dry:
                    os.remove(full)
        log("--clean: %s %d generated file(s) under %s (config.ini, state/ and logs/ are never touched)"
            % ("would remove" if dry else "removed", n, rel(self.oracle)))
        if not dry:
            self.cache.data = {"version": 1, "files": {}, "outputs": {}}
            for p in (self.cache.path,):
                if os.path.exists(p):
                    os.remove(p)
            if os.path.isdir(self.cache_dir):
                shutil.rmtree(self.cache_dir, ignore_errors=True)

    def gen_defaults(self):
        target = os.path.abspath(self.a.defaults_out) if self.a.defaults_out else os.path.join(self.fwdir, "story_defaults.h")
        if not self.model.words:
            log("--gen-defaults: the manifest has no words - nothing written")
            return False
        txt = fw.gen_story_defaults(self.model.words, self.story_header)
        old = None
        if os.path.exists(target):
            with open(target, "r", encoding="utf-8", newline="") as f:
                old = f.read()
        if old == txt:
            log("--gen-defaults: %s is already up to date (%d words)" % (rel(target), len(self.model.words)))
            return True
        if self.a.dry_run:
            log("--gen-defaults: would rewrite %s (%d words)" % (rel(target), len(self.model.words)))
            return True
        os.makedirs(os.path.dirname(target), exist_ok=True)
        write_atomic(target, txt, text=True)
        log("--gen-defaults: wrote %s (%d words) - rebuild the firmware" % (rel(target), len(self.model.words)))
        return True


def _ascii_fold(s):
    rep = {"—": "-", "–": "-", "…": "...", "‘": "'", "’": "'", "“": '"', "”": '"',
           "→": "->", "×": "x"}
    return "".join(rep.get(ch, ch if ord(ch) < 127 else "?") for ch in s)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def parse_args(argv):
    ap = argparse.ArgumentParser(
        prog="build_assets.py",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="Build the Crystal Oracle TF-card assets (sd_card/oracle) from assets/manifest.json.",
        epilog="examples (run from the crystal_oracle folder):\n"
               "  python -I tools\\build_assets.py --check\n"
               "  python -I tools\\build_assets.py\n"
               "  python -I tools\\build_assets.py --placeholders --test-clips\n"
               "  python -I tools\\build_assets.py --only attract,reveal_t*,voice:greet_r2_1\n"
               "  python -I tools\\build_assets.py --gen-defaults\n"
               "  python -I tools\\build_assets.py --previews\n")
    ap.add_argument("--manifest", help="manifest file (default: <assets>/manifest.json)")
    ap.add_argument("--assets", default=os.path.join(PROJECT, "assets"),
                    help="assets folder with source/, the cache and ASSET-STATUS.md (default: %(default)s)")
    ap.add_argument("--out", default=None,
                    help="output folder; the card files go to <out>/oracle (default: %(default)s)")
    ap.add_argument("--firmware", default=os.path.join(PROJECT, "firmware"),
                    help="firmware folder holding crystal_oracle/ and crystal_plinth/ (default: %(default)s)")
    ap.add_argument("--ipad", action="store_true",
                    help="build for the station web app (iPad): full-colour 4:4:4 clips at high quality, no 96 KB "
                         "frame cap, into sd_card_ipad/ (unless --out); config.ini starts as a copy of "
                         "sd_card/oracle/config.ini. NOT for the Waveshare TF card")
    ap.add_argument("--check", action="store_true", help="validate only and write assets/ASSET-STATUS.md (nothing is built)")
    ap.add_argument("--placeholders", action="store_true",
                    help="missing voice lines -> Windows TTS, missing sfx/music -> synthesized sounds")
    ap.add_argument("--test-clips", action="store_true", help="missing clips -> labelled animated test cards")
    ap.add_argument("--gen-defaults", action="store_true",
                    help="rewrite firmware/crystal_oracle/story_defaults.h from the manifest words (nothing is built)")
    ap.add_argument("--defaults-out", help="with --gen-defaults: write here instead of the firmware folder")
    ap.add_argument("--previews", action="store_true",
                    help="rewrite EVERY clip's contact sheet in <assets>/previews (a normal build already writes "
                         "the ones whose clip changed)")
    ap.add_argument("--no-previews", action="store_true", help="don't write contact sheets")
    ap.add_argument("--only", help="comma list of ids or globs; prefixes clip:/voice:/sfx:/music:/still: narrow it "
                                   "(a still id selects every clip that uses it)")
    ap.add_argument("--clean", action="store_true", help="delete everything generated under <out>/oracle (not config.ini) "
                                                         "and the build cache, then build")
    ap.add_argument("-j", "--jobs", type=int, default=4, help="parallel ffmpeg jobs (default: %(default)s)")
    ap.add_argument("--dry-run", action="store_true", help="show what would be built; write nothing")
    ap.add_argument("--force", action="store_true", help="rebuild even what is up to date")
    ap.add_argument("--tts-voice", help="placeholder TTS voice (substring of an installed voice name, e.g. Zira)")
    ap.add_argument("--tts-rate", type=int, default=-1, help="placeholder TTS rate -10..10 (default: %(default)s)")
    ap.add_argument("--ffmpeg", default="ffmpeg", help="ffmpeg executable (default: from PATH)")
    ap.add_argument("--ffprobe", default="ffprobe", help="ffprobe executable (default: from PATH)")
    ap.add_argument("-v", "--verbose", action="store_true", help="print notes and every ffmpeg command")
    a = ap.parse_args(argv)
    if a.out is None:
        a.out = os.path.join(PROJECT, "sd_card_ipad" if a.ipad else "sd_card")
    if a.jobs < 1:
        ap.error("-j must be at least 1")
    if not -10 <= a.tts_rate <= 10:
        ap.error("--tts-rate must be -10..10")
    return a


def main(argv=None):
    try:
        sys.stdout.reconfigure(errors="replace")
    except (AttributeError, ValueError):
        pass
    a = parse_args(sys.argv[1:] if argv is None else argv)
    t0 = time.time()
    if a.ipad:
        media.use_ipad_profile()
    b = Builder(a)
    media.FFMPEG, media.FFPROBE = a.ffmpeg, a.ffprobe
    log("Crystal Oracle assets %s - %s" % (TOOL_VERSION, rel(b.manifest)))
    b.load()
    m = b.model
    log("  %d stills, %d clips, %d voice lines, %d sfx, %d music, %d words  |  %s"
        % (len(m.stills), len(m.clips), len(m.voice), len(m.sfx), len(m.music), len(m.words), b.coverage_line()))

    if a.check or a.gen_defaults:
        b.print_report(show_info=a.verbose or a.check)
        rc = 1 if b.rep.errors else 0
        if a.gen_defaults:
            if b.rep.errors:
                log("--gen-defaults: fix the manifest errors first - nothing written")
            elif not b.gen_defaults():
                rc = 1
        if a.check:
            if not a.dry_run:
                b.write_status()
            log("check: %d error(s), %d warning(s), %d note(s)  ->  %s   [crystal_link.h: %s]"
                % (len(b.rep.errors), len(b.rep.warnings), len(b.rep.infos),
                   rel(b.status_path) if not a.dry_run else "(dry run, no status file)", b.link_state))
        return rc

    if b.rep.errors:
        b.print_report()
        log("%d manifest error(s) - nothing built (fix them; --check lists everything)" % len(b.rep.errors))
        return 1
    if b.rep.warnings:
        if a.verbose:
            b.print_report()
        else:
            log("  %d warning(s) - run --check or -v to see them" % len(b.rep.warnings))
    ver = media.tool_version(a.ffmpeg)
    if not ver:
        log("ffmpeg not found (%s) - install it (winget install Gyan.FFmpeg) or pass --ffmpeg" % a.ffmpeg)
        return 1
    if a.verbose:
        log("  " + ver)

    if a.clean:
        b.clean(a.dry_run)
    jobs, todo = b.plan()
    log("  %d output(s) planned, %d up to date, %d to build%s"
        % (len(jobs), len(jobs) - len(todo), len(todo), " (dry run)" if a.dry_run else ""))
    if a.dry_run:
        for j in todo:
            log("  would build %-5s %-22s %-14s -> %s  [%s]" % (j.cat, j.id, j.label[:14], b.out_rel(j.out), j.reason))
        return 0

    tmpdir = tempfile.mkdtemp(prefix="co-build-")
    try:
        for d in OUT_DIRS:
            os.makedirs(os.path.join(b.oracle, d), exist_ok=True)
        b.run_tts(todo)
        # heavy clips first so the pool stays busy
        weight = {"video": 0, "layers": 0, "crossfade": 1, "kenburns": 2, "float": 3, "still": 5}
        todo.sort(key=lambda j: (0 if j.cat == "clip" else 1, weight.get(j.entry.get("recipe"), 4) if j.cat == "clip" else 0))
        with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
            list(pool.map(lambda j: b.run_job(j, tmpdir), todo))
        stale = b.prune_placeholder_orphans()
        if stale:
            log("  removed %d stale placeholder(s) the manifest no longer lists: %s" % (len(stale), ", ".join(stale)))
        wrote = b.write_text_outputs()
        b.write_readme()
        if wrote:
            log("  wrote " + ", ".join(wrote))
        if not a.no_previews:
            pv = b.write_previews(force=a.previews)
            if pv:
                log("  previews: %s" % pv)
    except KeyboardInterrupt:
        log("interrupted - finished outputs are kept")
        raise
    finally:
        b.cache.save()
        shutil.rmtree(tmpdir, ignore_errors=True)
    if not a.ipad:                         # ASSET-STATUS.md describes the TF card build
        b.write_status()
    orph = b.orphans()
    if orph:
        log("  %d file(s) on the card are not in the manifest (see ASSET-STATUS.md; --clean removes them)" % len(orph))
    log("done in %.1f s: %d built, %d failed  ->  %s   status: %s"
        % (time.time() - t0, len(b.built), len(b.failed), rel(b.oracle), rel(b.status_path)))
    return 1 if b.failed else 0


if __name__ == "__main__":
    sys.exit(main())
