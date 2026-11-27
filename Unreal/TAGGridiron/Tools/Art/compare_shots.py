"""Look comparison shots (LOOK-AND-FEEL-PLAN.md Phase 1). Plain Python, runs the editor in -game mode.

Renders TAG Gridiron's own automated shots (-TagAutoWatch: broadcast, sideline, skycam, stands, athlete
close ups) and timed shots of the MaddenRef preview levels (-TagShotAt), then gathers both sets into
Saved/Compare/<stamp>/ with a side by side sheet when Pillow is available.

The output is for looking only. It is local (Saved/ is not in git) and is never a source for game content.
MaddenRef levels never cook (DirectoriesToNeverCook) and game content may not reference them (maddenref_audit.py).

    py -3.13 -I Tools/Art/compare_shots.py [--tag-only | --ref-only] [--night]
"""
import glob
import os
import shutil
import subprocess
import sys
import time

UE = os.environ.get("UE_ROOT", r"C:\Program Files\Epic Games\UE_5.8")
EDITOR = os.path.join(UE, "Engine", "Binaries", "Win64", "UnrealEditor.exe")
PROJ_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
UPROJECT = os.path.join(PROJ_DIR, "TAGGridiron.uproject")
SHOTS = os.path.join(PROJ_DIR, "Saved", "Screenshots")
REF_MAPS = ["L_MaddenRef_Prototype", "L_MaddenRef_Stage2Player", "L_MaddenRef_AnimPrototype"]
RES = ["-windowed", "-ResX=1600", "-ResY=900", "-unattended", "-nosplash", "-nosound"]
TIMEOUT = 540


def run(args, label):
    t0 = time.time()
    try:
        rc = subprocess.run([EDITOR, UPROJECT] + args, timeout=TIMEOUT).returncode
    except subprocess.TimeoutExpired:
        rc = "timeout"
    print("%-28s rc=%s %.0fs" % (label, rc, time.time() - t0))


def newest(name, since):
    hits = [p for p in glob.glob(os.path.join(SHOTS, "**", name), recursive=True) if os.path.getmtime(p) >= since]
    return max(hits, key=os.path.getmtime) if hits else None


def sheet(pairs, out):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("Pillow not installed, no sheet")
        return
    ims = [(lbl, Image.open(p).convert("RGB")) for lbl, p in pairs if p]
    if not ims:
        return
    w = 800
    rows = [(lbl, im.resize((w, int(im.height * w / im.width)))) for lbl, im in ims]
    cols = 2
    h = max(im.height for _, im in rows) + 24
    S = Image.new("RGB", (w * cols, h * ((len(rows) + cols - 1) // cols)), (18, 18, 20))
    d = ImageDraw.Draw(S)
    for i, (lbl, im) in enumerate(rows):
        x, y = (i % cols) * w, (i // cols) * h
        S.paste(im, (x, y + 24))
        d.text((x + 6, y + 5), lbl, fill=(230, 230, 230))
    S.save(out)
    print("sheet", out)


def main():
    stamp = time.strftime("%Y%m%d_%H%M%S")
    out = os.path.join(PROJ_DIR, "Saved", "Compare", stamp)
    os.makedirs(out, exist_ok=True)
    since = time.time()
    pairs = []
    if "--ref-only" not in sys.argv:
        run(["-game", "-TagAutoWatch"] + ([] if "--night" in sys.argv else ["-TagDay"]) + RES, "TAG auto watch")
        for n in ["tag_auto_00.png", "tag_auto_04.png", "tag_auto_06.png", "tag_auto_stands.png",
                  "tag_auto_closeup.png", "tag_auto_closeup_full.png", "tag_auto_closeup_back.png"]:
            p = newest(n, since)
            if p:
                shutil.copy2(p, os.path.join(out, n))
            pairs.append(("TAG " + n[:-4], p))
    if "--tag-only" not in sys.argv:
        for m in REF_MAPS:
            name = "ref_%s.png" % m
            run(["/Game/MaddenRef/Maps/" + m, "-game", "-TagShotAt=6", "-TagShotName=" + name] + RES, "ref " + m)
            p = newest(name, since)
            if p:
                shutil.copy2(p, os.path.join(out, name))
            pairs.append(("MaddenRef " + m, p))
    for lbl, p in pairs:
        print("%-40s %s" % (lbl, "ok" if p else "MISSING"))
    sheet(pairs, os.path.join(out, "sheet.png"))
    print("->", out)


if __name__ == "__main__":
    main()
