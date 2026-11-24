"""Contact sheets for a look-matrix run (Tools/QA/look_matrix.ps1, Research/NextLevel/03 A0).

    python Tools/QA/look_sheet.py <run>                         sheets of one run (into <run>/sheets)
    python Tools/QA/look_sheet.py --compare <before> <after>    before/after sheets (into <after>/compare_<before name>)

A run folder holds one sub-folder per condition and resolution, <cond>_<res>/picks/<frame>.png (plus lineup/*.png when
the run filmed the lineup). Sheets per resolution:
  matrix_<res>.png   rows = conditions, columns = the picked frames (whole frame)
  detail.png         the centre quarter of every pick at both resolutions, same field area, real pixels (nearest
                     neighbour when enlarged): flakes, lines and jersey folds that vanish at 720p show here
  lineup_<res>.png   the -TagLineup close-up set per condition (when filmed)
Compare: rows alternate before / after per condition, and the mean absolute difference (0-255) of each pair is printed
and written to diff.tsv, so an unchanged look reads ~0 and a moved one stands out.
"""
import argparse
import glob
import json
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageStat

TILE_W = 384
LABEL_H = 18
BG = (18, 18, 20)
FG = (235, 235, 235)
DIM = (150, 150, 150)


def cells(run):
    """{(cond, res): folder} for every condition folder in a run, in the manifest's order when there is one."""
    found = {}
    for d in sorted(glob.glob(os.path.join(run, "*_*"))):
        name = os.path.basename(d)
        if not os.path.isdir(os.path.join(d, "picks")) and not os.path.isdir(os.path.join(d, "lineup")):
            continue
        cond, _, res = name.rpartition("_")
        found[(cond, res)] = d
    order_c, order_r = [], []
    man = os.path.join(run, "manifest.json")
    if os.path.isfile(man):
        try:
            with open(man, encoding="utf-8-sig") as fh:
                m = json.load(fh)
            order_c = list(m.get("conditions", []))
            order_r = [str(r) for r in m.get("resolutions", [])]
        except (OSError, ValueError):
            pass
    conds = order_c + sorted({c for c, _ in found} - set(order_c))
    ress = order_r + sorted({r for _, r in found} - set(order_r), key=lambda r: int(r) if r.isdigit() else 0)
    conds = [c for c in conds if any((c, r) in found for r in ress)]
    ress = [r for r in ress if any((c, r) in found for c in conds)]
    return found, conds, ress


def picks_of(run):
    man = os.path.join(run, "manifest.json")
    if os.path.isfile(man):
        try:
            with open(man, encoding="utf-8-sig") as fh:
                p = json.load(fh).get("picks")
            if p:
                return list(p)
        except (OSError, ValueError):
            pass
    names = set()
    for f in glob.glob(os.path.join(run, "*_*", "picks", "*.png")):
        names.add(os.path.splitext(os.path.basename(f))[0])
    return sorted(names)


def pick_file(folder, pick):
    """The pick's PNG; a clip shorter than the pick's frame number was saved under the clip's last frame (see the
    picks.tsv the capture writes)."""
    p = os.path.join(folder, "picks", pick + ".png")
    if os.path.isfile(p):
        return p, pick
    tsv = os.path.join(folder, "picks.tsv")
    if os.path.isfile(tsv):
        with open(tsv, encoding="utf-8-sig") as fh:
            for line in fh:
                parts = line.rstrip("\n").split("\t")
                if len(parts) >= 2 and parts[0] == pick and parts[1]:
                    q = os.path.join(folder, "picks", parts[1] + ".png")
                    if os.path.isfile(q):
                        return q, parts[1]
    return None, pick


def tile(path, w=TILE_W, crop=None, nearest=False):
    if not path:
        return None
    im = Image.open(path).convert("RGB")
    if crop:
        x0, y0, x1, y1 = crop
        W, H = im.size
        im = im.crop((int(x0 * W), int(y0 * H), int(x1 * W), int(y1 * H)))
    h = max(1, int(round(im.height * w / im.width)))
    return im.resize((w, h), Image.NEAREST if nearest and im.width < w else Image.LANCZOS)


def grid(rows, col_labels, title, out):
    """rows: [(row label, [PIL image or None, ...], [tile caption, ...])]"""
    ncol = max(len(r[1]) for r in rows) if rows else 0
    if not ncol:
        return None
    th = max((im.height for _, ims, _ in rows for im in ims if im is not None), default=216)
    lw = 150
    W = lw + ncol * TILE_W
    H = 2 * LABEL_H + len(rows) * (th + LABEL_H)
    S = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(S)
    d.text((6, 3), title, fill=FG)
    for j, c in enumerate(col_labels):
        d.text((lw + j * TILE_W + 6, LABEL_H + 3), c, fill=DIM)
    y = 2 * LABEL_H
    for label, ims, caps in rows:
        d.text((6, y + th // 2), label, fill=FG)
        for j, im in enumerate(ims):
            x = lw + j * TILE_W
            if im is None:
                d.rectangle((x + 2, y + 2, x + TILE_W - 3, y + th - 3), outline=(90, 40, 40))
                d.text((x + 10, y + th // 2), "missing", fill=(200, 90, 90))
            else:
                S.paste(im, (x, y))
            if j < len(caps) and caps[j]:
                d.text((x + 6, y + th + 2), caps[j], fill=DIM)
        y += th + LABEL_H
    os.makedirs(os.path.dirname(out), exist_ok=True)
    S.save(out)
    print("sheet", out, S.size)
    return out


def run_sheets(run):
    found, conds, ress = cells(run)
    picks = picks_of(run)
    if not found:
        sys.exit("no <cond>_<res>/picks folders in %s" % run)
    out = os.path.join(run, "sheets")
    for res in ress:
        rows = []
        for c in conds:
            f = found.get((c, res))
            ims, caps = [], []
            for p in picks:
                path, got = pick_file(f, p) if f else (None, p)
                ims.append(tile(path))
                caps.append(got if got != p else "")
            rows.append((c, ims, caps))
        grid(rows, picks, "%s  %sp  (conditions x picked frames)" % (os.path.basename(run), res), os.path.join(out, "matrix_%s.png" % res))
    # the same field area at every resolution: centre quarter, real pixels
    rows = []
    for c in conds:
        for res in ress:
            f = found.get((c, res))
            ims = [tile(pick_file(f, p)[0] if f else None, crop=(0.375, 0.375, 0.625, 0.625), nearest=True) for p in picks]
            rows.append(("%s %sp" % (c, res), ims, []))
    if rows:
        grid(rows, picks, "%s  centre quarter of each pick, every resolution (nearest-neighbour)" % os.path.basename(run), os.path.join(out, "detail.png"))
    for res in ress:
        rows = []
        names = []
        for c in conds:
            f = found.get((c, res))
            files = sorted(glob.glob(os.path.join(f, "lineup", "*.png"))) if f else []
            for x in files:
                n = os.path.splitext(os.path.basename(x))[0].replace("tag_lineup_", "")
                if n not in names:
                    names.append(n)
        if not names:
            continue
        for c in conds:
            f = found.get((c, res))
            ims = [tile(os.path.join(f, "lineup", "tag_lineup_%s.png" % n) if f and os.path.isfile(os.path.join(f, "lineup", "tag_lineup_%s.png" % n)) else None) for n in names]
            rows.append((c, ims, []))
        grid(rows, names, "%s  %sp  -TagLineup close-ups" % (os.path.basename(run), res), os.path.join(out, "lineup_%s.png" % res))


def mad(a, b):
    """(mean absolute difference 0-255 over RGB, % of pixels that moved by more than 16 levels in any channel)."""
    A = Image.open(a).convert("RGB")
    B = Image.open(b).convert("RGB")
    if A.size != B.size:
        B = B.resize(A.size, Image.LANCZOS)
    D = ImageChops.difference(A, B)
    st = ImageStat.Stat(D)
    r, g, bl = D.split()
    m = ImageChops.lighter(ImageChops.lighter(r, g), bl).point(lambda v: 255 if v > 16 else 0)
    changed = 100.0 * ImageStat.Stat(m).mean[0] / 255.0
    return sum(st.mean) / 3.0, changed


def compare(before, after):
    fb, cb, rb = cells(before)
    fa, ca, ra = cells(after)
    picks = [p for p in picks_of(after) if p in picks_of(before)] or picks_of(after)
    nb, na = os.path.basename(os.path.normpath(before)), os.path.basename(os.path.normpath(after))
    out = os.path.join(after, "compare_" + nb)
    conds = [c for c in ca if c in cb]
    ress = [r for r in ra if r in rb]
    if not conds or not ress:
        sys.exit("the two runs share no condition / resolution")
    diffs = []
    for res in ress:
        rows = []
        for c in conds:
            for tag, found, name in (("before", fb, nb), ("after", fa, na)):
                f = found.get((c, res))
                ims = [tile(pick_file(f, p)[0] if f else None) for p in picks]
                caps = []
                if tag == "after":
                    for p in picks:
                        pa_ = pick_file(fa.get((c, res)), p)[0] if fa.get((c, res)) else None
                        pb_ = pick_file(fb.get((c, res)), p)[0] if fb.get((c, res)) else None
                        if pa_ and pb_:
                            v, ch = mad(pb_, pa_)
                            diffs.append((c, res, p, v, ch))
                            caps.append("diff %.1f  changed %.1f%%" % (v, ch))
                        else:
                            caps.append("")
                rows.append(("%s %s" % (c, tag), ims, caps))
        grid(rows, picks, "before %s / after %s  %sp" % (nb, na, res), os.path.join(out, "compare_%s.png" % res))
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "diff.tsv"), "w", encoding="utf-8") as fh:
        fh.write("condition\tres\tpick\tmean_abs_diff\tchanged_pct\n")
        for c, res, p, v, ch in diffs:
            fh.write("%s\t%s\t%s\t%.2f\t%.2f\n" % (c, res, p, v, ch))
    print()
    print("%-12s %5s  %s" % ("condition", "res", "per pick: mean |before-after| (0-255) / % of pixels changed > 16 levels"))
    for c in conds:
        for res in ress:
            vs = [(v, ch) for cc, rr, _, v, ch in diffs if cc == c and rr == res]
            if vs:
                print("%-12s %5s  %s   mean %.1f / %.1f%%" % (c, res, "  ".join("%.1f/%.0f%%" % x for x in vs),
                                                            sum(x[0] for x in vs) / len(vs), sum(x[1] for x in vs) / len(vs)))
    print("->", out)


def main():
    ap = argparse.ArgumentParser(description="Look-matrix contact sheets (one run, or before/after).")
    ap.add_argument("run", nargs="?", help="a look-matrix run folder")
    ap.add_argument("--compare", nargs=2, metavar=("BEFORE", "AFTER"), help="two run folders")
    a = ap.parse_args()
    if a.compare:
        for d in a.compare:
            if not os.path.isdir(d):
                sys.exit("no folder %s" % d)
        compare(*a.compare)
    elif a.run:
        if not os.path.isdir(a.run):
            sys.exit("no folder %s" % a.run)
        run_sheets(a.run)
    else:
        ap.print_help()
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
