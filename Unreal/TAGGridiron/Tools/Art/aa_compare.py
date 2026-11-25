# Anti-aliasing A/B (Research/NextLevel/03 A8): how far two films of the same seeded game differ, frame by frame.
# Films of one condition are the same game from the same camera (fixed step, -TagWeatherSeed / -TagPackSeed), so the
# difference between a TSR run and a no-history run (-TagAA=fxaa), both with -TagMBlur=0, is what TSR's history adds
# or smears: its ghost trails behind legs and the ball, and its softening. Same flags but -TagMBlur=0.15 against 0
# isolates the motion blur instead.
#
#   python Tools\Art\aa_compare.py <run A picks dir> <run B picks dir> [--crop x0,y0,x1,y1] [--out sheet.png] [--scale 4]
#
# For every frame name in both folders: the mean and 99th percentile absolute difference (0..255, luma) over the frame
# or the crop, and the share of pixels that differ by more than 24. --out writes, per frame, A | B | |A-B| x scale.
# Frames of a 120 fps film are numbered 4x those of a 30 fps one: compare a 30 fps pair with a 30 fps pair.
import argparse
import os
import sys

import numpy as np
from PIL import Image


def luma(path, crop):
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.float32)
    if crop:
        x0, y0, x1, y1 = crop
        a = a[y0:y1, x0:x1]
    return a, a @ np.array([0.2126, 0.7152, 0.0722], np.float32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--crop", default="")
    ap.add_argument("--out", default="")
    ap.add_argument("--scale", type=float, default=4.0)
    o = ap.parse_args()
    crop = [int(v) for v in o.crop.split(",")] if o.crop else None
    names = sorted(set(f for f in os.listdir(o.a) if f.endswith(".png")) & set(f for f in os.listdir(o.b) if f.endswith(".png")))
    if not names:
        print("no frame names in common")
        return 1
    rows = []
    print("%-20s %8s %8s %8s" % ("frame", "mean", "p99", ">24 %"))
    for n in names:
        ca, la = luma(os.path.join(o.a, n), crop)
        cb, lb = luma(os.path.join(o.b, n), crop)
        if la.shape != lb.shape:
            print("%-20s size differs" % n)
            continue
        d = np.abs(la - lb)
        print("%-20s %8.2f %8.1f %8.2f" % (n, d.mean(), np.percentile(d, 99), 100.0 * (d > 24).mean()))
        if o.out:
            dv = np.clip(np.abs(ca - cb) * o.scale, 0, 255)
            rows.append(np.concatenate([ca, cb, dv], axis=1).astype(np.uint8))
    if o.out and rows:
        w = max(r.shape[1] for r in rows)
        sheet = np.concatenate([np.pad(r, ((0, 0), (0, w - r.shape[1]), (0, 0))) for r in rows], axis=0)
        Image.fromarray(sheet).save(o.out)
        print("sheet:", o.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
