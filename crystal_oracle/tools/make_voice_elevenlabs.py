"""make_voice_elevenlabs.py -- record every voice line in assets/manifest.json with ElevenLabs.

Writes one MP3 per line to assets/source/voice/<line id>.mp3; then run build_assets.py, which
converts them to the 48 kHz mono WAVs the screen plays (and pitches up the Snuffwick lines).
Python standard library only. Lines that already have a source file are skipped (use --force / --only).

API key (never commit it, never paste it into a chat):
    set ELEVENLABS_API_KEY in your environment, or put the key alone in tools/.elevenlabs_key
    (that file is git-ignored).

    python -I tools/make_voice_elevenlabs.py --list-voices             # pick Butterscotch's voice
    python -I tools/make_voice_elevenlabs.py --voice "<name or id>" --only greet_r2_1 --force   # try one line
    python -I tools/make_voice_elevenlabs.py --voice "<name or id>" --priority MVP             # the night-one set
    python -I tools/make_voice_elevenlabs.py --voice "<name or id>"                            # everything missing
    python -I tools/build_assets.py --placeholders                                             # convert + build the card

The chosen voice/model/settings are remembered in tools/.elevenlabs_voice.json so later runs
(re-takes of single lines) sound the same.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
MANIFEST = os.path.join(ROOT, "assets", "manifest.json")
OUT_DIR = os.path.join(ROOT, "assets", "source", "voice")
KEY_FILE = os.path.join(HERE, ".elevenlabs_key")
STATE_FILE = os.path.join(HERE, ".elevenlabs_voice.json")
API = "https://api.elevenlabs.io/v1"
AUDIO_EXTS = (".wav", ".mp3", ".m4a", ".ogg", ".flac", ".aac")

# Defaults follow STORY.md's voice direction (gentle, older storyteller; quick ~2 s lines).
DEFAULT_MODEL = "eleven_multilingual_v2"
DEFAULT_SETTINGS = {"stability": 0.40, "similarity_boost": 0.75, "style": 0.30,
                    "use_speaker_boost": True, "speed": 1.07}


def api_key():
    k = os.environ.get("ELEVENLABS_API_KEY", "").strip()
    if not k and os.path.exists(KEY_FILE):
        with open(KEY_FILE, encoding="utf-8") as f:
            k = f.read().strip()
    if not k:
        sys.exit("No API key: set ELEVENLABS_API_KEY or put the key in tools/.elevenlabs_key (git-ignored).")
    return k


def request(method, path, key, body=None, accept="application/json", retries=5):
    data = json.dumps(body).encode("utf-8") if body is not None else None
    for attempt in range(retries):
        req = urllib.request.Request(API + path, data=data, method=method)
        req.add_header("xi-api-key", key)
        req.add_header("Accept", accept)
        if data is not None:
            req.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            msg = e.read().decode("utf-8", "replace")[:400]
            if e.code in (429, 500, 502, 503, 504) and attempt + 1 < retries:
                wait = 2 ** attempt * 2
                print(f"  HTTP {e.code}, retrying in {wait} s ...")
                time.sleep(wait)
                continue
            sys.exit(f"ElevenLabs HTTP {e.code} on {path}: {msg}")
        except urllib.error.URLError as e:
            if attempt + 1 < retries:
                time.sleep(2 ** attempt * 2)
                continue
            sys.exit(f"Network error talking to ElevenLabs: {e}")


def list_voices(key):
    voices = json.loads(request("GET", "/voices", key))["voices"]
    for v in sorted(voices, key=lambda v: (v.get("category", ""), v["name"].lower())):
        labels = ", ".join(f"{k}={val}" for k, val in (v.get("labels") or {}).items())
        print(f"{v['voice_id']}  {v['name']:<28} [{v.get('category', '')}] {labels}")
    return voices


def resolve_voice(key, wanted):
    voices = json.loads(request("GET", "/voices", key))["voices"]
    for v in voices:
        if v["voice_id"] == wanted or v["name"].lower() == wanted.lower():
            return v["voice_id"], v["name"]
    matches = [v for v in voices if wanted.lower() in v["name"].lower()]
    if len(matches) == 1:
        return matches[0]["voice_id"], matches[0]["name"]
    if matches:
        sys.exit("Several voices match '%s': %s" % (wanted, ", ".join(v["name"] for v in matches)))
    sys.exit(f"No voice called '{wanted}' in your ElevenLabs account (try --list-voices; add library voices to 'My Voices' first).")


def existing_source(line_id):
    for ext in AUDIO_EXTS:
        p = os.path.join(OUT_DIR, line_id + ext)
        if os.path.exists(p):
            return p
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list-voices", action="store_true", help="list the voices in your account and exit")
    ap.add_argument("--voice", help="voice name or id (remembered for later runs)")
    ap.add_argument("--model", help=f"model id (default {DEFAULT_MODEL}, remembered)")
    ap.add_argument("--stability", type=float)
    ap.add_argument("--similarity", type=float)
    ap.add_argument("--style", type=float)
    ap.add_argument("--speed", type=float, help="0.7-1.2; default 1.07 (lines must fit ~2 s)")
    ap.add_argument("--priority", choices=["MVP", "NICE", "all"], default="all")
    ap.add_argument("--only", help="comma-separated line ids (e.g. greet_r2_1,attract_call_3)")
    ap.add_argument("--force", action="store_true", help="re-record lines that already have a source file")
    ap.add_argument("--dry-run", action="store_true", help="show what would be recorded and the character count")
    a = ap.parse_args()

    key = None if a.dry_run else api_key()
    if a.list_voices:
        list_voices(key or api_key())
        return

    with open(MANIFEST, encoding="utf-8") as f:
        man = json.load(f)
    lines = man.get("voice", [])
    if a.only:
        want = {s.strip() for s in a.only.split(",") if s.strip()}
        unknown = want - {l["id"] for l in lines}
        if unknown:
            sys.exit("Unknown line ids: " + ", ".join(sorted(unknown)))
        lines = [l for l in lines if l["id"] in want]
    elif a.priority != "all":
        lines = [l for l in lines if l.get("priority") == a.priority]
    todo = [l for l in lines if a.force or not existing_source(l["id"])]

    state = {}
    if os.path.exists(STATE_FILE):
        with open(STATE_FILE, encoding="utf-8") as f:
            state = json.load(f)
    settings = dict(DEFAULT_SETTINGS)
    settings.update(state.get("settings", {}))
    for name, val in (("stability", a.stability), ("similarity_boost", a.similarity), ("style", a.style), ("speed", a.speed)):
        if val is not None:
            settings[name] = val
    model = a.model or state.get("model") or DEFAULT_MODEL

    chars = sum(len(l["text"]) for l in todo)
    print(f"{len(todo)} line(s) to record ({chars} characters ~ {chars} credits on most models); "
          f"{len(lines) - len(todo)} already have a source file.")
    if a.dry_run or not todo:
        for l in todo:
            print(f"  {l['id']:<16} {l['text']}")
        return

    if a.voice:
        voice_id, voice_name = resolve_voice(key, a.voice)
    elif state.get("voice_id"):
        voice_id, voice_name = state["voice_id"], state.get("voice_name", state["voice_id"])
    else:
        sys.exit("Pick a voice first: --list-voices, then --voice \"<name>\". Direction: "
                 + man.get("voice_style", {}).get("elevenlabs", "a gentle, older storyteller voice"))
    with open(STATE_FILE, "w", encoding="utf-8") as f:
        json.dump({"voice_id": voice_id, "voice_name": voice_name, "model": model, "settings": settings}, f, indent=1)
    print(f"Voice: {voice_name} ({voice_id})   model: {model}   settings: {settings}")

    os.makedirs(OUT_DIR, exist_ok=True)
    for i, l in enumerate(todo, 1):
        body = {"text": l["text"], "model_id": model, "voice_settings": settings}
        audio = request("POST", f"/text-to-speech/{voice_id}?output_format=mp3_44100_128", key, body, accept="audio/mpeg")
        old = existing_source(l["id"])
        if old and old.endswith(".mp3") is False:
            os.replace(old, old + ".old")          # keep a hand-made file instead of silently shadowing it
        out = os.path.join(OUT_DIR, l["id"] + ".mp3")
        with open(out, "wb") as f:
            f.write(audio)
        print(f"  [{i}/{len(todo)}] {l['id']:<16} {len(audio) // 1024:>4} KB  {l['text']}")
        time.sleep(0.3)
    print("Done. Now run:  python -I tools/build_assets.py --placeholders")


if __name__ == "__main__":
    main()
