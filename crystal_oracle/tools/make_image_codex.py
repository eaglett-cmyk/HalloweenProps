"""make_image_codex.py -- generate a still with ChatGPT's image model through the Codex CLI.

Uses the owner's ChatGPT plan (the Codex desktop app's sign-in), not API credits. Python standard
library only. Every job gets its own folder under assets/.codex_jobs/<job id>/ (git-ignored):
    prompt.txt   the full prompt sent to Codex
    codex.log    Codex's stdout/stderr
    last.txt     Codex's final message
    job.json     state (queued/running/done/failed), pid, Codex session id, collected images
    <files>      copies of this job's generated image(s)
Only images in this job's own Codex session folder (~/.codex/generated_images/<session id>/) are
collected, each is checked (PNG/JPEG/WebP header, width/height, minimum size), and with --id the
first one is copied to assets/source/images/<id>.png for build_assets.py.

    python -I tools/make_image_codex.py run --id prop_lantern "a glowing candy lantern ..."      # wait for it
    python -I tools/make_image_codex.py run --id bg_cave --ref bg_cave_lit --force "..."         # re-take, style ref
    python -I tools/make_image_codex.py start --id prop_lantern "..."                            # detached; prints job id
    python -I tools/make_image_codex.py status [job id]                                          # all jobs / one job
    python -I tools/make_image_codex.py wait <job id>                                            # block until it ends

Notes:
  * One image takes ~3-8 minutes. Images count against the ChatGPT plan's Codex limits.
  * Codex runs with the settings that were proven to work: --skip-git-repo-check --ephemeral
    -s workspace-write -C <job dir>. Codex's own file tools time out in that sandbox, so Codex is
    told NOT to save files; this script copies the image out of Codex's folder itself.
  * The Codex CLI is codex.exe inside the Microsoft Store app (not on PATH); it is found through
    Get-AppxPackage OpenAI.Codex each run (the folder name changes on every app update), or set CODEX_EXE.
  * Prompts get STYLE prepended (the screen is 800x480 landscape); --raw sends the prompt as-is.
"""
import argparse
import ctypes
import json
import os
import re
import shutil
import subprocess
import sys
import time
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
IMAGES_DIR = os.path.join(ROOT, "assets", "source", "images")
JOBS_DIR = os.path.join(ROOT, "assets", ".codex_jobs")
GEN_DIR = os.path.join(os.path.expanduser("~"), ".codex", "generated_images")
IMAGE_EXTS = (".png", ".jpg", ".jpeg", ".webp")

DEFAULT_TIMEOUT = 20 * 60
MIN_SIDE = 512               # anything smaller is not a real generation
STYLE = ("Landscape orientation (wide, about 5:3), for an 800x480 screen. Storybook illustration, "
         "warm and cozy-spooky Halloween mood, rich glowing colour, no text or lettering in the image.")


# ---------------------------------------------------------------- Codex executable

def codex_exe():
    exe = os.environ.get("CODEX_EXE", "").strip()
    if exe:
        if not os.path.isfile(exe):
            sys.exit(f"CODEX_EXE points at a missing file: {exe}")
        return exe
    try:
        loc = subprocess.run(
            ["powershell", "-NoProfile", "-Command",
             "(Get-AppxPackage OpenAI.Codex | Sort-Object Version -Descending | Select-Object -First 1).InstallLocation"],
            capture_output=True, text=True, timeout=60).stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        loc = ""
    exe = os.path.join(loc, "app", "resources", "codex.exe") if loc else ""
    if not exe or not os.path.isfile(exe):
        sys.exit("Codex CLI not found: install the Codex app from the Microsoft Store and sign in "
                 "with ChatGPT, or set CODEX_EXE to codex.exe.")
    return exe


def check_login(exe):
    r = subprocess.run([exe, "login", "status"], capture_output=True, text=True, timeout=60,
                       stdin=subprocess.DEVNULL)
    out = (r.stdout + r.stderr).strip()
    if "ChatGPT" not in out:
        sys.exit(f"Codex is not signed in with ChatGPT ({out or 'no output'}); open the Codex app and sign in.")


# ---------------------------------------------------------------- image checks

def image_info(path):
    """(format, width, height) from the file header, or None if it isn't a PNG/JPEG/WebP."""
    with open(path, "rb") as f:
        head = f.read(64)
        if head[:8] == b"\x89PNG\r\n\x1a\n" and head[12:16] == b"IHDR":
            return "png", int.from_bytes(head[16:20], "big"), int.from_bytes(head[20:24], "big")
        if head[:4] == b"RIFF" and head[8:12] == b"WEBP":
            kind = head[12:16]
            if kind == b"VP8X":
                return "webp", 1 + int.from_bytes(head[24:27], "little"), 1 + int.from_bytes(head[27:30], "little")
            if kind == b"VP8 ":
                return "webp", int.from_bytes(head[26:28], "little") & 0x3FFF, int.from_bytes(head[28:30], "little") & 0x3FFF
            if kind == b"VP8L":
                b = int.from_bytes(head[21:25], "little")
                return "webp", (b & 0x3FFF) + 1, ((b >> 14) & 0x3FFF) + 1
            return None
        if head[:2] == b"\xff\xd8":
            f.seek(2)
            while True:
                m = f.read(2)
                if len(m) < 2 or m[0] != 0xFF:
                    return None
                if m[1] in (0xD8, 0x01) or 0xD0 <= m[1] <= 0xD7:
                    continue
                seg = int.from_bytes(f.read(2), "big")
                if m[1] in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
                    d = f.read(5)
                    return "jpeg", int.from_bytes(d[3:5], "big"), int.from_bytes(d[1:3], "big")
                f.seek(seg - 2, 1)
    return None


def verify(path, landscape):
    info = image_info(path)
    if not info:
        return None, "not a PNG/JPEG/WebP file"
    fmt, w, h = info
    if min(w, h) < MIN_SIDE:
        return info, f"too small ({w}x{h})"
    if landscape and w <= h:
        return info, f"not landscape ({w}x{h})"
    return info, None


# ---------------------------------------------------------------- job bookkeeping

def job_path(job_id, name="job.json"):
    return os.path.join(JOBS_DIR, job_id, name)


def load_job(job_id):
    p = job_path(job_id)
    if not os.path.exists(p):
        sys.exit(f"No such job: {job_id} (see: status)")
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def save_job(job):
    tmp = job_path(job["id"], "job.json.tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(job, f, indent=1)
    os.replace(tmp, job_path(job["id"]))


def pid_alive(pid):
    if not pid:
        return False
    if os.name == "nt":
        h = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)   # PROCESS_QUERY_LIMITED_INFORMATION
        if not h:
            return False
        code = ctypes.c_ulong()
        ctypes.windll.kernel32.GetExitCodeProcess(h, ctypes.byref(code))
        ctypes.windll.kernel32.CloseHandle(h)
        return code.value == 259                                     # STILL_ACTIVE
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def effective_state(job):
    """A job left 'running' whose worker died (PC restart, killed window) is reported as 'died'."""
    if job["state"] in ("queued", "running") and not pid_alive(job.get("worker_pid")):
        return "died"
    return job["state"]


def new_job(a):
    if a.id and not re.fullmatch(r"[a-z0-9_]+", a.id):
        sys.exit("--id must be lowercase letters, digits and _ (like the manifest's still ids)")
    if a.id and not a.force and existing_image(a.id):
        sys.exit(f"assets/source/images already has {a.id}; use --force to replace it (the old file is kept as .old)")
    refs = [resolve_ref(r) for r in (a.ref or [])]
    prompt = a.prompt if a.raw else f"{STYLE}\n\n{a.prompt}"
    job_id = time.strftime("%Y%m%d-%H%M%S-") + (a.id or "img") + "-" + uuid.uuid4().hex[:4]
    os.makedirs(os.path.join(JOBS_DIR, job_id))
    with open(job_path(job_id, "prompt.txt"), "w", encoding="utf-8") as f:
        f.write(prompt)
    job = {"id": job_id, "state": "queued", "still_id": a.id, "force": a.force, "refs": refs,
           "timeout": a.timeout, "landscape": not a.any_shape, "created": time.time()}
    save_job(job)
    return job


def existing_image(still_id):
    for ext in IMAGE_EXTS:
        p = os.path.join(IMAGES_DIR, still_id + ext)
        if os.path.exists(p):
            return p
    return None


def resolve_ref(ref):
    if os.path.isfile(ref):
        return os.path.abspath(ref)
    p = existing_image(ref)
    if not p:
        sys.exit(f"--ref {ref}: not a file and no assets/source/images/{ref}.*")
    return p


# ---------------------------------------------------------------- the worker

INSTRUCTIONS = ("\n\nGenerate exactly ONE image for the description above with your image generation tool"
                "{refs}. Do not create, copy or save any files yourself and do not run shell commands. "
                "When the image is done, reply with only the full path of the generated image file.")


def run_worker(job_id):
    job = load_job(job_id)
    jdir = os.path.join(JOBS_DIR, job_id)
    job.update(state="running", worker_pid=os.getpid(), started=time.time())
    save_job(job)
    try:
        exe = codex_exe()
        check_login(exe)
        with open(os.path.join(jdir, "prompt.txt"), encoding="utf-8") as f:
            prompt = f.read()
        refs_note = " (use the attached image(s) as the style and character reference)" if job["refs"] else ""
        cmd = [exe, "exec", "--skip-git-repo-check", "--ephemeral", "-s", "workspace-write",
               "-C", jdir, "-o", os.path.join(jdir, "last.txt")]
        for r in job["refs"]:
            cmd += ["-i", r]
        cmd.append("-")                     # prompt on stdin: -i takes several files and would eat a positional prompt
        sent = os.path.join(jdir, "prompt_sent.txt")
        with open(sent, "w", encoding="utf-8") as f:
            f.write(prompt + INSTRUCTIONS.format(refs=refs_note))

        log_path = os.path.join(jdir, "codex.log")
        with open(log_path, "w", encoding="utf-8", errors="replace") as log, open(sent, "rb") as stdin:
            proc = subprocess.Popen(cmd, stdin=stdin, stdout=log, stderr=subprocess.STDOUT)
            job["codex_pid"] = proc.pid
            save_job(job)
            try:
                rc = proc.wait(timeout=job["timeout"])
            except subprocess.TimeoutExpired:
                subprocess.run(["taskkill", "/T", "/F", "/PID", str(proc.pid)], capture_output=True)
                raise RuntimeError(f"Codex timed out after {job['timeout']} s (killed)")
        job["codex_exit"] = rc

        with open(log_path, encoding="utf-8", errors="replace") as f:
            m = re.search(r"session id:\s*([0-9a-fA-F-]{36})", f.read())
        if not m:
            raise RuntimeError(f"no Codex session id in codex.log (exit {rc})")
        job["session_id"] = m.group(1)
        sdir = os.path.join(GEN_DIR, job["session_id"])
        found = sorted(os.path.join(sdir, n) for n in os.listdir(sdir)
                       if n.lower().endswith(IMAGE_EXTS)) if os.path.isdir(sdir) else []
        if not found:
            raise RuntimeError(f"Codex finished (exit {rc}) but made no image; see codex.log / last.txt")

        images = []
        for i, src in enumerate(found, 1):
            info, problem = verify(src, job["landscape"])
            dest = os.path.join(jdir, f"image_{i}" + os.path.splitext(src)[1].lower())
            shutil.copy2(src, dest)
            images.append({"file": dest, "codex_file": src, "format": info and info[0],
                           "width": info and info[1], "height": info and info[2], "problem": problem})
        job["images"] = images
        good = [im for im in images if not im["problem"]]
        if not good:
            raise RuntimeError("image(s) failed the check: " + "; ".join(im["problem"] for im in images))

        if job["still_id"]:
            pick = good[0]
            old = existing_image(job["still_id"])
            if old:
                os.replace(old, old + ".old")
            out = os.path.join(IMAGES_DIR, job["still_id"] + ".png")
            if pick["format"] == "png":
                shutil.copy2(pick["file"], out)
            else:                                           # build_assets reads any format, but keep the
                out = os.path.join(IMAGES_DIR, job["still_id"] + "." + pick["format"].replace("jpeg", "jpg"))
                shutil.copy2(pick["file"], out)            # real extension rather than mislabel it
            job["installed"] = out
        job.update(state="done", finished=time.time())
    except Exception as e:                                  # noqa: BLE001 -- recorded in job.json for status/wait
        job.update(state="failed", error=str(e), finished=time.time())
    save_job(job)
    return job


# ---------------------------------------------------------------- commands

def describe(job):
    st = effective_state(job)
    took = ""
    if job.get("started"):
        took = " %d s" % ((job.get("finished") or time.time()) - job["started"])
    line = f"{job['id']:<44} {st:<8}{took}"
    if job.get("still_id"):
        line += f"  -> {job['still_id']}"
    if st == "done":
        for im in job.get("images", []):
            line += f"\n    {im['file']}  {im['format']} {im['width']}x{im['height']}" + \
                    (f"  [{im['problem']}]" if im["problem"] else "")
        if job.get("installed"):
            line += f"\n    installed: {job['installed']}"
    elif st == "failed":
        line += f"\n    error: {job.get('error')}"
    elif st == "died":
        line += "\n    the worker process is gone (restart / killed); start the job again"
    return line


def cmd_start(a):
    job = new_job(a)
    flags = 0x00000008 | 0x00000200 if os.name == "nt" else 0       # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
    p = subprocess.Popen([sys.executable, "-I", os.path.abspath(__file__), "_worker", job["id"]],
                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         creationflags=flags, close_fds=True)
    job["worker_pid"] = p.pid
    save_job(job)
    print(job["id"])


def cmd_wait(job_id, poll=10):
    while True:
        job = load_job(job_id)
        if effective_state(job) not in ("queued", "running"):
            print(describe(job))
            return 0 if job["state"] == "done" else 1
        time.sleep(poll)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("run", "start"):
        s = sub.add_parser(name, help="generate an image (run = wait for it, start = detached)")
        s.add_argument("prompt", help="what to draw")
        s.add_argument("--id", help="still id: install the result as assets/source/images/<id>.png")
        s.add_argument("--force", action="store_true", help="replace an existing still (old kept as .old)")
        s.add_argument("--ref", action="append", help="reference image: a still id or a file path (repeatable)")
        s.add_argument("--raw", action="store_true", help="send the prompt without the house STYLE line")
        s.add_argument("--any-shape", action="store_true", help="allow portrait/square results")
        s.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT, help="seconds (default %(default)s)")
    s = sub.add_parser("status", help="list jobs, or show one")
    s.add_argument("job", nargs="?")
    s = sub.add_parser("wait", help="block until a job finishes; exit 0 = done")
    s.add_argument("job")
    s = sub.add_parser("_worker")
    s.add_argument("job")
    a = ap.parse_args()

    if a.cmd == "run":
        job = run_worker(new_job(a)["id"])
        print(describe(job))
        sys.exit(0 if job["state"] == "done" else 1)
    if a.cmd == "start":
        cmd_start(a)
    elif a.cmd == "_worker":
        run_worker(a.job)
    elif a.cmd == "wait":
        sys.exit(cmd_wait(a.job))
    elif a.cmd == "status":
        if a.job:
            print(describe(load_job(a.job)))
        else:
            ids = sorted(os.listdir(JOBS_DIR)) if os.path.isdir(JOBS_DIR) else []
            for j in ids:
                if os.path.exists(job_path(j)):
                    print(describe(load_job(j)))
            if not ids:
                print("no jobs yet")


if __name__ == "__main__":
    main()
