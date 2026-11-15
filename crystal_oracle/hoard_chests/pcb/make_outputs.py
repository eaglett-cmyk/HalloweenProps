#!/usr/bin/env python3
"""
make_outputs.py - DRC + fabrication outputs for both boards (run after gen_boards.py).

    "C:\\Program Files\\KiCad\\9.0\\bin\\python.exe" make_outputs.py [hub|node] [--drc-only]

Per board folder: <name>-drc.rpt / -drc.json (errors only, the acceptance test) and
<name>-drc-all.rpt (warnings too), gerbers/ (F/B Cu, F/B Mask, F/B Silk, Edge.Cuts + PTH/NPTH
Excellon), <name>_gerbers.zip, <name>-top.png / <name>-bottom.png renders.
"""
import os, sys, json, subprocess, zipfile, shutil

CLI = r"C:\Program Files\KiCad\9.0\bin\kicad-cli.exe"
HERE = os.path.dirname(os.path.abspath(__file__))
BOARDS = {"hub": ("hub", "hoard_hub"), "node": ("node", "hoard_node")}
LAYERS = "F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,Edge.Cuts"


def run(args):
    r = subprocess.run([CLI] + args, capture_output=True, text=True)
    if r.returncode not in (0, 5):
        print(r.stdout, r.stderr)
    return r


def drc(folder, name):
    pcb = os.path.join(folder, name + ".kicad_pcb")
    rpt = os.path.join(folder, name + "-drc.rpt")
    js = os.path.join(folder, name + "-drc.json")
    run(["pcb", "drc", "--severity-error", "--units", "mm", "-o", rpt, pcb])
    run(["pcb", "drc", "--severity-error", "--units", "mm", "--format", "json", "-o", js, pcb])
    run(["pcb", "drc", "--severity-all", "--units", "mm", "-o", os.path.join(folder, name + "-drc-all.rpt"), pcb])
    d = json.load(open(js, encoding="utf-8"))
    nv = len(d.get("violations", []))
    nu = len(d.get("unconnected_items", []))
    ncp = len(d.get("schematic_parity", []))
    print("%s DRC (errors): %d violations, %d unconnected items" % (name, nv, nu))
    kinds = {}
    for v in d.get("violations", []):
        kinds[v["type"]] = kinds.get(v["type"], 0) + 1
    if kinds:
        print("   ", kinds)
    return nv, nu


def fab(folder, name):
    pcb = os.path.join(folder, name + ".kicad_pcb")
    gdir = os.path.join(folder, "gerbers")
    if os.path.isdir(gdir):
        shutil.rmtree(gdir)
    os.makedirs(gdir)
    run(["pcb", "export", "gerbers", "--layers", LAYERS, "--subtract-soldermask", "-o", gdir + os.sep, pcb])
    run(["pcb", "export", "drill", "--format", "excellon", "--excellon-separate-th", "--excellon-units", "mm",
         "--generate-map", "--map-format", "gerberx2", "-o", gdir + os.sep, pcb])
    z = os.path.join(folder, name + "_gerbers.zip")
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
        for f in sorted(os.listdir(gdir)):
            zf.write(os.path.join(gdir, f), f)
    print("  zip:", z, sorted(os.listdir(gdir)))
    for side in ("top", "bottom"):
        out = os.path.join(folder, "%s-%s.png" % (name, side))
        run(["pcb", "render", "--side", side, "--quality", "high", "--width", "2000", "--height", "1800",
             "--background", "opaque", "-o", out, pcb])
        print("  render:", out, os.path.exists(out))
    for side, lay in (("top", "F.Cu,F.Silkscreen,F.Mask,Edge.Cuts,F.Fab"), ("bottom", "B.Cu,B.Silkscreen,Edge.Cuts")):
        run(["pcb", "export", "svg", "--layers", lay, "--mode-single", "--page-size-mode", "2", "--exclude-drawing-sheet",
             "-o", os.path.join(folder, "%s-%s-layers.svg" % (name, side))] + (["--mirror"] if side == "bottom" else []) + [pcb])


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    which = args or ["hub", "node"]
    for w in which:
        sub, name = BOARDS[w]
        folder = os.path.join(HERE, sub)
        drc(folder, name)
        if "--drc-only" not in sys.argv:
            fab(folder, name)
