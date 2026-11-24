# Contact sheets around every turnover moment of a -TagHlFull film: fumbles, loose-ball dives, recoveries, interceptions,
# breakups and drops (from the sim events the film writes per clip, telemetry/evNNN.csv), plus a short text report of
# the ball's path after a fumble (ball columns in cNNN.csv when present).
#   python turnover_sheets.py <film dir (holding frames/ and telemetry/)> <out dir> [frames before] [frames after]
import csv
import glob
import os
import sys

from PIL import Image, ImageDraw

FILM, OUT = sys.argv[1], sys.argv[2]
BEFORE = int(sys.argv[3]) if len(sys.argv) > 3 else 15
AFTER = int(sys.argv[4]) if len(sys.argv) > 4 else 75
KINDS = {"fumble", "dive", "recover", "squirt", "int", "pbu", "drop", "batted"}
os.makedirs(OUT, exist_ok=True)
frames_dir = os.path.join(FILM, "frames")
tele = os.path.join(FILM, "telemetry")


def clip_frames(c):
    """sim time -> frame for clip c, from the clip's telemetry (first row per frame)."""
    p = os.path.join(tele, "c%03d.csv" % c)
    out = []
    if not os.path.exists(p):
        return out
    seen = set()
    with open(p, newline="") as f:
        for r in csv.DictReader(f):
            fr = int(r["frame"])
            if fr in seen:
                continue
            seen.add(fr)
            try:
                out.append((float(r.get("simt") or r.get("t") or 0), fr))
            except ValueError:
                pass
    return out


def sheet(c, f0, f1, n, out, cols=5, sc=0.4):
    idx = [f0 + round(i * (f1 - f0) / max(1, n - 1)) for i in range(n)]
    ims = []
    for i in idx:
        p = os.path.join(frames_dir, "c%03d_%05d.png" % (c, i))
        if not os.path.exists(p):
            continue
        im = Image.open(p).convert("RGB")
        im = im.resize((int(im.width * sc), int(im.height * sc)), Image.LANCZOS)
        ImageDraw.Draw(im).text((4, 4), str(i), fill=(255, 255, 0))
        ims.append(im)
    if not ims:
        return False
    w, h = ims[0].size
    rows = (len(ims) + cols - 1) // cols
    S = Image.new("RGB", (w * cols, h * rows))
    for k, im in enumerate(ims):
        S.paste(im, ((k % cols) * w, (k // cols) * h))
    S.save(out)
    return True


report = []
for ev in sorted(glob.glob(os.path.join(tele, "ev[0-9][0-9][0-9].csv"))):
    c = int(os.path.basename(ev)[2:5])
    rows = list(csv.DictReader(open(ev, newline="")))
    hits = [r for r in rows if r.get("type") in KINDS]
    if not hits:
        continue
    tf = clip_frames(c)
    for r in hits:
        t = float(r.get("t") or 0)
        fr = min(tf, key=lambda x: abs(x[0] - t))[1] if tf else 0
        line = "c%03d frame %d t %.2f %s %s" % (c, fr, t, r.get("type"), {k: v for k, v in r.items() if k not in ("t", "type")})
        report.append(line)
    first = min(hits, key=lambda r: float(r.get("t") or 0))
    t0 = float(first.get("t") or 0)
    fr0 = min(tf, key=lambda x: abs(x[0] - t0))[1] if tf else 0
    name = os.path.join(OUT, "c%03d_%s.png" % (c, "_".join(sorted({h["type"] for h in hits}))))
    sheet(c, max(0, fr0 - BEFORE), fr0 + AFTER, 20, name)
open(os.path.join(OUT, "report.txt"), "w").write("\n".join(report) + "\n")
print("\n".join(report))
