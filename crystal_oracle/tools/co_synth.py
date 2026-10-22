"""
============================================================================
 co_synth.py  -  placeholder sounds + Windows TTS voice lines (Crystal Oracle)
----------------------------------------------------------------------------
 --placeholders fills the gaps so the station is testable before real audio
 exists:
   * sfx / music: an ffmpeg lavfi graph per named recipe (the manifest's
     "synth" field):  rise  sparkle  chime  fanfare  wahwah  blip  bloop
     whoosh  drone(music bed, loop-exact). The graphs only need to sound
     roughly right - build_audio() loudness-normalises them afterwards.
   * voice: ONE powershell.exe call drives Windows' built-in System.Speech
     synthesizer for every missing line (a deeper installed voice such as
     "Microsoft David" is preferred, Rate -1). Results are cached in
     assets/.cache/tts/ by text, so re-runs don't re-speak.

 Python 3 standard library only (plus powershell.exe on Windows).
============================================================================
"""
import hashlib
import json
import os
import random
import subprocess
import sys

SR = 48000

# Default recipe + length per firmware sfx id (cues.h) when the manifest names no synth.
SFX_DEFAULTS = {
    "absorb": "rise", "awaken": "sparkle", "reveal_t0": "wahwah", "reveal_t1": "chime",
    "reveal_t2": "sparkle", "reveal_t3": "fanfare", "tap": "blip", "charge_full": "chime",
    "sleepy": "bloop", "button": "blip", "whoosh": "whoosh",
}
SECONDS = {"rise": 1.2, "sparkle": 1.2, "chime": 1.6, "fanfare": 2.2, "wahwah": 2.3,
           "blip": 0.12, "bloop": 0.35, "whoosh": 0.9, "drone": 24.0}


def default_synth(category, sid):
    if category == "music":
        return "drone"
    if sid in SFX_DEFAULTS:
        return SFX_DEFAULTS[sid]
    for word, syn in (("whoosh", "whoosh"), ("tap", "blip"), ("button", "blip"), ("sparkle", "sparkle"),
                      ("rise", "rise"), ("fail", "wahwah"), ("trick", "wahwah"), ("win", "fanfare")):
        if word in sid:
            return syn
    return "chime"


def _f(x):
    return ("%.6f" % x).rstrip("0").rstrip(".")


def _aeval(expr, d):
    return "aevalsrc=exprs='%s':s=%d:d=%s" % (expr, SR, _f(d))


def _note(f, t0, t1, decay, harmonics, wah=False):
    """One gated note (start t0, end t1) with a few harmonics and an exponential decay."""
    gate = "between(t,%s,%s)" % (_f(t0), _f(t1))
    tt = "(t-%s)" % _f(t0)
    parts = []
    for k, a in enumerate(harmonics, 1):
        amp = _f(a)
        if wah and k > 1:              # upper harmonics swell and fall inside the note = a "wah"
            amp = "%s*sin(PI*min(%s/%s,1))" % (_f(a), tt, _f(max(0.05, t1 - t0)))
        parts.append("%s*sin(2*PI*%s*%s)" % (amp, _f(f * k), tt))
    return "%s*(%s)*exp(-%s*%s)*(1-exp(-%s*300))" % (gate, "+".join(parts), _f(decay), tt, tt)


def synth_graph(name, seconds=None, seed="x"):
    """lavfi input graph for a placeholder sound. Returns (graph, seconds)."""
    d = float(seconds or SECONDS.get(name, 1.0))
    rnd = random.Random(hashlib.sha1(("%s|%s" % (name, seed)).encode()).hexdigest())
    if name == "blip":
        return _aeval("0.6*sin(2*PI*1320*t)*exp(-t*35)*(1-exp(-t*2000))", d), d
    if name == "bloop":
        return _aeval("0.6*sin(2*PI*(220*t+40*(1-exp(-12*t))))*exp(-t*6)*(1-exp(-t*800))", d), d
    if name == "chime":
        e = ("0.4*(sin(2*PI*1046.5*t)+0.5*sin(2*PI*2093*t)*exp(-t*3)+0.35*sin(2*PI*2885*t)*exp(-t*5)"
             "+0.2*sin(2*PI*4100*t)*exp(-t*8))*exp(-t*%s)*(1-exp(-t*600))" % _f(4.0 / d))
        return _aeval(e, d), d
    if name == "rise":
        # linear chirp 300 -> 1500 Hz (phase = 2*pi*(f0 t + (f1-f0) t^2 / 2d)) + a fifth, 14 Hz shimmer, swelling
        k = 1200.0 / (2 * d)
        e = ("0.35*(sin(2*PI*(300*t+%s*t*t))+0.5*sin(2*PI*(450*t+%s*t*t)))*(0.65+0.35*sin(2*PI*14*t))"
             "*pow(t/%s,1.5)*min(1,(%s-t)*25)" % (_f(k), _f(k * 1.5), _f(d), _f(d)))
        return _aeval(e, d), d
    if name == "sparkle":
        pings = []
        for _ in range(8):
            t0 = rnd.uniform(0.0, max(0.05, d - 0.45))
            f = rnd.uniform(2200, 5200)
            pings.append("gte(t,%s)*sin(2*PI*%s*(t-%s))*exp(-(t-%s)*18)" % (_f(t0), _f(f), _f(t0), _f(t0)))
        return _aeval("0.25*(%s)" % "+".join(pings), d), d
    if name == "fanfare":
        h = [1.0, 0.5, 0.33, 0.25, 0.2]
        notes = [_note(523.25, 0.00, 0.16, 6, h), _note(659.25, 0.15, 0.31, 6, h), _note(783.99, 0.30, 0.46, 6, h),
                 _note(1046.5, 0.45, d, 1.6, h), _note(783.99, 0.45, d, 1.8, [0.5, 0.2]), _note(659.25, 0.45, d, 2.0, [0.4, 0.15])]
        return _aeval("0.18*(%s)" % "+".join(notes), d), d
    if name == "wahwah":
        h = [1.0, 0.6, 0.45, 0.3, 0.2]
        last0 = 1.2
        notes = [_note(392.0, 0.0, 0.36, 2, h, wah=True), _note(369.99, 0.4, 0.76, 2, h, wah=True),
                 _note(349.23, 0.8, 1.16, 2, h, wah=True)]
        # the long last note gets a 6 Hz vibrato: f(t) = 329.63 + 6*sin(2*pi*6*t) -> integrate in the phase
        tt = "(t-%s)" % _f(last0)
        vib = "sin(2*PI*(329.63*%s-(1-cos(2*PI*6*%s))/(2*PI)))" % (tt, tt)
        notes.append("between(t,%s,%s)*(%s+0.5*sin(2*PI*659.26*%s)*sin(PI*min(%s/0.6,1)))*exp(-%s*1.2)*(1-exp(-%s*300))"
                     % (_f(last0), _f(d), vib, tt, tt, tt, tt))
        return _aeval("0.2*(%s)" % "+".join(notes), d), d
    if name == "whoosh":
        # pink noise through a band-pass whose centre sweeps 250 Hz -> 5 kHz (asendcmd steps)
        steps = 24
        cmds = ";".join("%s bandpass frequency %d" % (_f(d * i / steps), int(250 * (20 ** (i / steps))))
                        for i in range(steps + 1))
        g = ("anoisesrc=c=pink:r=%d:a=0.9:d=%s:seed=%d,asendcmd=c='%s',bandpass=f=250:w=0.9:t=o,"
             "afade=t=in:st=0:d=%s,afade=t=out:st=%s:d=%s"
             % (SR, _f(d), rnd.randrange(1, 1 << 30), cmds, _f(d * 0.55), _f(d * 0.55), _f(d * 0.45)))
        return g, d
    if name == "drone":
        # Low eerie pad. Every frequency and LFO is a whole number of cycles over d, so the
        # bed loops without a seam. Kept above the 120 Hz high-pass (small speakers).
        def snap(f):
            return round(f * d) / d
        voices = [(130.81, 0.30), (131.06, 0.25), (196.0, 0.22), (196.3, 0.18), (277.18, 0.10),
                  (311.13, 0.08), (392.0, 0.06), (523.25, 0.04)]
        terms = []
        for k, (f, a) in enumerate(voices):
            lfo = snap(rnd.choice([1, 2, 3]) / d)
            ph = rnd.uniform(0, 6.283)
            terms.append("%s*sin(2*PI*%s*t)*(0.65+0.35*sin(2*PI*%s*t+%s))" % (_f(a), _f(snap(f)), _f(lfo), _f(ph)))
        return _aeval("0.5*(%s)" % "+".join(terms), d), d
    raise ValueError("unknown synth %r" % name)


# ---------------------------------------------------------------------------
# Windows TTS (System.Speech) - one powershell.exe call for all lines
# ---------------------------------------------------------------------------
TTS_PS1 = r"""
param([string]$Jobs, [string]$Prefer, [int]$Rate)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$s = New-Object System.Speech.Synthesis.SpeechSynthesizer
$names = @($s.GetInstalledVoices() | Where-Object { $_.Enabled } | ForEach-Object { $_.VoiceInfo.Name })
$pick = $null
foreach ($want in $Prefer.Split('|')) {
  if (-not $pick -and $want) { $pick = $names | Where-Object { $_ -like ('*' + $want + '*') } | Select-Object -First 1 }
}
if ($pick) { $s.SelectVoice($pick) }
Write-Output ('VOICE ' + $s.Voice.Name)
Write-Output ('ALL ' + ($names -join ', '))
$s.Rate = $Rate
$list = [System.IO.File]::ReadAllText($Jobs, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
foreach ($j in $list) {
  try {
    $s.SetOutputToWaveFile($j.out)
    $s.Speak([string]$j.text)
    $s.SetOutputToNull()
    Write-Output ('OK ' + $j.id)
  } catch {
    try { $s.SetOutputToNull() } catch {}
    Write-Output ('FAIL ' + $j.id + ' ' + $_.Exception.Message)
  }
}
$s.Dispose()
"""

DEFAULT_TTS_PREFER = ["David", "Mark", "George", "James", "Guy", "Zira"]


def tts_cache_name(text, prefer, rate):
    h = hashlib.sha1(("%s|%s|%d" % (text, "|".join(prefer), rate)).encode("utf-8")).hexdigest()[:20]
    return "tts_%s.wav" % h


def run_tts(lines, cache_dir, prefer, rate=-1, timeout=600, log=print):
    """lines: [(voice_id, text)]. Speaks every line not yet cached. Returns
    ({voice_id: cached wav path or None}, voice name used or None, error text or None)."""
    os.makedirs(cache_dir, exist_ok=True)
    result, todo = {}, []
    for vid, text in lines:
        path = os.path.join(cache_dir, tts_cache_name(text, prefer, rate))
        result[vid] = path
        if not (os.path.exists(path) and os.path.getsize(path) > 1000):
            todo.append({"id": vid, "text": text, "out": path + ".part.wav"})
    if not todo:
        return result, "(cached)", None
    if sys.platform != "win32":
        return {k: (v if os.path.exists(v) else None) for k, v in result.items()}, None, "Windows TTS needs Windows"
    jobs = os.path.join(cache_dir, "tts_jobs.json")
    ps1 = os.path.join(cache_dir, "tts_speak.ps1")
    with open(jobs, "w", encoding="utf-8") as f:
        json.dump(todo, f, ensure_ascii=True)
    with open(ps1, "w", encoding="utf-8-sig") as f:      # BOM: PowerShell 5.1 reads BOM-less scripts as ANSI
        f.write(TTS_PS1)
    voice, err = None, None
    try:
        r = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                            "-File", ps1, "-Jobs", jobs, "-Prefer", "|".join(prefer), "-Rate", str(rate)],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, stdin=subprocess.DEVNULL, timeout=timeout)
        out = r.stdout.decode("utf-8", "replace").splitlines()
        for line in out:
            if line.startswith("VOICE "):
                voice = line[6:].strip()
            elif line.startswith("FAIL "):
                log("  tts: " + line)
        if r.returncode != 0:
            err = "powershell exit %d: %s" % (r.returncode, r.stderr.decode("utf-8", "replace").strip()[-300:])
    except FileNotFoundError:
        err = "powershell.exe not found"
    except subprocess.TimeoutExpired:
        err = "TTS timed out after %d s" % timeout
    for j in todo:
        part, final = j["out"], j["out"][:-len(".part.wav")]
        if os.path.exists(part) and os.path.getsize(part) > 1000:
            os.replace(part, final)
        elif os.path.exists(part):
            os.remove(part)
    for name in (jobs, ps1):
        try:
            os.remove(name)
        except OSError:
            pass
    return {k: (v if os.path.exists(v) else None) for k, v in result.items()}, voice, err
