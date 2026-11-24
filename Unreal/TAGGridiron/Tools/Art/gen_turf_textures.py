"""In-house source textures for the realistic turf (M_TagTurf2 / M_TagGrassBlade, Tools/Art/ue_build_turf.py).

Run with the system Python (Pillow + NumPy):  python gen_turf_textures.py [what ...]
  what = blades | macro | logos | words   (default: all)
Writes into RawArt/:
  T_TagTurfBlades_D.png  2048 RGBA, tileable, one tile = 1.5 m of turf seen from above. RGB = the blades' colour as a
                         multiplier around 0.5 (the material multiplies the grass colour by rgb * 2), A = height
                         (0 thatch between the blades .. 1 blade tips). Linear.
  T_TagTurfBlades_N.png  the normal of that height field.
  T_TagTurfMacro.png     1024 RGB, tileable: three independent cloud noises (R broad, G medium, B fine), stretched to
                         use the whole 0..1 range. The material reads it at two unrelated scales and rotations.
  T_TagFieldLogos.png    4096 x 2048: the team marks of gen_team_logos.py at 512 px a cell (8 x 4 cells, R fill, G ring
                         and badge) for the midfield logo.
  T_TagFieldWords.png    4096 x 4096 L: the end-zone wordmarks, 2 x 16 cells of 2048 x 256 (team id = row * 2 + col).
Everything is drawn here; no third-party art (the face is RawArt/Fonts/Graduate-Regular.ttf, OFL).
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
OUT = os.path.join(ROOT, "RawArt")
FONT = os.path.join(OUT, "Fonts", "Graduate-Regular.ttf")


def tileable_noise(n, scale, octaves=5, seed=0):
    """Periodic value noise in [0,1] (tiles seamlessly)."""
    r = np.random.default_rng(seed)
    acc = np.zeros((n, n), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        g = int(scale * (2 ** o))
        grid = r.random((g, g)).astype(np.float32)
        xs = np.linspace(0, g, n, endpoint=False)
        x0 = np.floor(xs).astype(int)
        fx = (xs - x0)[None, :]
        fy = (xs - x0)[:, None]
        x1 = (x0 + 1) % g
        a = grid[np.ix_(x0, x0)]
        b = grid[np.ix_(x0, x1)]
        c = grid[np.ix_(x1, x0)]
        d = grid[np.ix_(x1, x1)]
        sx = fx * fx * (3 - 2 * fx)
        sy = fy * fy * (3 - 2 * fy)
        acc += ((a * (1 - sx) + b * sx) * (1 - sy) + (c * (1 - sx) + d * sx) * sy) * amp
        tot += amp
        amp *= 0.55
    return acc / tot


def stretch(a, lo=1.0, hi=99.0):
    p0, p1 = np.percentile(a, [lo, hi])
    return np.clip((a - p0) / max(p1 - p0, 1e-6), 0, 1)


def blades(n=2048, count=150000, seed=41):
    """Mown grass from straight above: short tapered blades in every direction, the tall ones over the low ones."""
    ss = 2                                   # drawn at twice the size, then downsampled
    S = n * ss
    r = np.random.default_rng(seed)
    soil = tileable_noise(n, 48, 3, seed=seed + 1)
    soil = np.array(Image.fromarray((soil * 255).astype(np.uint8)).resize((S, S), Image.BILINEAR), np.float32) / 255.0
    base = 0.13 + 0.10 * soil
    col = Image.fromarray((np.stack([base * 1.05, base, base * 0.8], -1) * 255).astype(np.uint8), "RGB")
    hgt = Image.fromarray((soil * 28).astype(np.uint8), "L")
    dc, dh = ImageDraw.Draw(col), ImageDraw.Draw(hgt)
    layer = np.sort(r.random(count))         # low blades first
    px = r.random(count) * S
    py = r.random(count) * S
    ang = r.random(count) * 2 * np.pi
    # px at ss: 1 px = 150 cm / 4096 = 0.37 mm. A blade seen from above: 0.6 .. 2.6 cm long, 3.5 .. 6 mm wide
    ln = (0.6 + 2.0 * r.random(count) ** 1.5) / 0.0366
    wd = (0.35 + 0.25 * r.random(count)) / 0.0366
    bend = r.normal(0, 0.35, count)
    tone = r.random(count)
    hue = r.random(count)
    dead = r.random(count) < 0.035
    SEG = 4
    for i in range(count):
        c, s = np.cos(ang[i]), np.sin(ang[i])
        pts = []
        for k in range(SEG + 1):
            t = k / SEG
            a = ang[i] + bend[i] * t * t
            x = px[i] + np.cos(ang[i] + bend[i] * t * 0.5) * ln[i] * t
            y = py[i] + np.sin(ang[i] + bend[i] * t * 0.5) * ln[i] * t
            w = wd[i] * (1.0 - 0.85 * t ** 1.6) * 0.5
            pts.append((x, y, -np.sin(a) * w, np.cos(a) * w, t))
        offs = [(0, 0)]
        m = ln[i] + wd[i]
        ox = [0] + ([S] if px[i] < m else []) + ([-S] if px[i] > S - m else [])
        oy = [0] + ([S] if py[i] < m else []) + ([-S] if py[i] > S - m else [])
        offs = [(a_, b_) for a_ in ox for b_ in oy]
        lift = 0.55 + 0.45 * layer[i]
        for k in range(SEG):
            x0, y0, nx0, ny0, t0 = pts[k]
            x1, y1, nx1, ny1, t1 = pts[k + 1]
            t = (t0 + t1) * 0.5
            v = (0.26 + 0.50 * t) * (0.78 + 0.44 * tone[i]) * (0.75 + 0.25 * layer[i])
            if dead[i]:
                rgb = (v * 1.45, v * 1.12, v * 0.55)
            else:
                rgb = (v * (0.86 + 0.38 * hue[i]), v, v * (0.78 - 0.2 * hue[i]))
            fill = tuple(int(np.clip(ch * 255, 0, 255)) for ch in rgb)
            hv = int(np.clip((0.12 + 0.88 * t * lift) * 255, 0, 255))
            for dx, dy in offs:
                poly = [(x0 + nx0 + dx, y0 + ny0 + dy), (x1 + nx1 + dx, y1 + ny1 + dy), (x1 - nx1 + dx, y1 - ny1 + dy), (x0 - nx0 + dx, y0 - ny0 + dy)]
                dc.polygon(poly, fill=fill)
                dh.polygon(poly, fill=hv)
    col = col.resize((n, n), Image.LANCZOS)
    hgt = hgt.resize((n, n), Image.LANCZOS)
    c = np.asarray(col, np.float32) / 255.0
    h = np.asarray(hgt, np.float32) / 255.0
    c *= 0.5 / np.maximum(c.reshape(-1, 3).mean(0), 1e-4)  # the material doubles it: each channel averages 1, so the
    #                                                        grass colour parameter is the turf's mean colour
    h = stretch(h, 0.5, 99.5)
    rgba = np.dstack([np.clip(c, 0, 1), h])
    Image.fromarray((rgba * 255).astype(np.uint8), "RGBA").save(os.path.join(OUT, "T_TagTurfBlades_D.png"))
    hb = np.asarray(Image.fromarray((h * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.7)), np.float32) / 255.0
    dx = (np.roll(hb, -1, 1) - np.roll(hb, 1, 1)) * 5.0
    dy = (np.roll(hb, -1, 0) - np.roll(hb, 1, 0)) * 5.0
    l = np.sqrt(dx * dx + dy * dy + 1.0)
    nrm = np.stack([(-dx / l) * 0.5 + 0.5, (-dy / l) * 0.5 + 0.5, (1.0 / l) * 0.5 + 0.5], -1)
    Image.fromarray((nrm * 255).astype(np.uint8), "RGB").save(os.path.join(OUT, "T_TagTurfBlades_N.png"))
    print("blades: colour mean", c.reshape(-1, 3).mean(0).round(3), "height mean %.3f" % h.mean())


def macro(n=1024):
    a = stretch(tileable_noise(n, 3, 5, seed=101))
    b = stretch(tileable_noise(n, 7, 5, seed=202))
    c = stretch(tileable_noise(n, 22, 4, seed=303))
    Image.fromarray((np.dstack([a, b, c]) * 255).astype(np.uint8), "RGB").save(os.path.join(OUT, "T_TagTurfMacro.png"))
    print("macro: mean", [round(float(x.mean()), 3) for x in (a, b, c)], "std", [round(float(x.std()), 3) for x in (a, b, c)])


def logos(cell=512):
    sys.path.insert(0, HERE)
    import gen_team_logos as g
    g.CELL = cell
    atlas = np.zeros((g.ROWS * cell, g.COLS * cell, 3), np.uint8)
    for i, name in enumerate(g.TEAMS):
        f, rr = g.mark(name, i)
        y0, x0 = (i // g.COLS) * cell, (i % g.COLS) * cell
        atlas[y0:y0 + cell, x0:x0 + cell, 0] = f
        atlas[y0:y0 + cell, x0:x0 + cell, 1] = rr
    Image.fromarray(atlas).save(os.path.join(OUT, "T_TagFieldLogos.png"))
    print("logos:", atlas.shape)


def words(cw=2048, ch=256):
    sys.path.insert(0, HERE)
    import gen_team_logos as g
    cols, rows = 2, 16
    atlas = Image.new("L", (cols * cw, rows * ch), 0)
    for i, name in enumerate(g.TEAMS):
        label = name.upper()
        # letter-spaced capitals as tall as the cell allows; a long name is squeezed sideways before it is shrunk
        size = int(ch * 1.05)
        while True:
            font = ImageFont.truetype(FONT, size)
            tmp = Image.new("L", (cw * 3, ch * 2), 0)
            d = ImageDraw.Draw(tmp)
            x = 20.0
            gap = size * 0.16
            for chx in label:
                d.text((x, ch * 0.4), chx, fill=255, font=font)
                x += d.textlength(chx, font=font) + gap
            tmp = tmp.filter(ImageFilter.MaxFilter(9))   # a heavier stroke: field paint is laid on broad
            box = tmp.getbbox()
            if box[3] - box[1] <= ch * 0.84 or size < 40:
                break
            size -= 6
        word = tmp.crop(box)
        maxw = int(cw * 0.92)
        if word.width > maxw:
            squeeze = max(maxw / word.width, 0.62)
            word = word.resize((int(word.width * squeeze), word.height), Image.LANCZOS)
        if word.width > maxw:
            k = maxw / word.width
            word = word.resize((maxw, int(word.height * k)), Image.LANCZOS)
        cx, cy = (i % cols) * cw, (i // cols) * ch
        atlas.paste(word, (cx + (cw - word.width) // 2, cy + (ch - word.height) // 2))
    atlas.save(os.path.join(OUT, "T_TagFieldWords.png"))
    print("words:", atlas.size)


def lines(P=72):
    """White paint mask of the whole 120 x 53.33 yd field, the layout of gen_textures.py field_lines (x = length,
    y = width, same marks), drawn at 72 px a yard and saved as a power of two (8192 x 4096) so it gets mip maps and
    anisotropic filtering: the 5760 x 2560 original has neither, and the lines crawl from the sideline camera."""
    W, H = int(120 * P), int(160 / 3 * P)
    im = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(im)
    four_in = max(2, int(round(P * 4 / 36)))
    b = int(P * 0.33)
    d.rectangle([0, 0, W, b], fill=255)
    d.rectangle([0, H - b, W, H], fill=255)
    d.rectangle([0, 0, b, H], fill=255)
    d.rectangle([W - b, 0, W, H], fill=255)
    hash_off = 70.75 / 3   # 70 ft 9 in from the sideline, in yards
    hl = int(P * 0.67)
    for yd in range(0, 101):
        x = int((yd + 10) * P)
        if yd % 5 == 0:
            w = int(P * (8 / 36)) if yd in (0, 100) else four_in
            d.rectangle([x - w // 2, b, x + w // 2, H - b], fill=255)
        else:
            for y0 in (b, H - b - hl, int(hash_off * P) - hl // 2, H - int(hash_off * P) - hl // 2):
                d.rectangle([x - four_in // 2, y0, x + four_in // 2, y0 + hl], fill=255)
    for yd in (2, 98):
        x = int((yd + 10) * P)
        d.rectangle([x - four_in // 2, H // 2 - P // 2, x + four_in // 2, H // 2 + P // 2], fill=255)
    font = ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", int(P * 2.0))
    for yd in range(10, 100, 10):
        n = str(yd if yd <= 50 else 100 - yd)
        x = (yd + 10) * P
        for bottom in (True, False):
            txt = Image.new("L", (int(P * 6), int(P * 2.6)), 0)
            td = ImageDraw.Draw(txt)
            label = " ".join(n)
            tw = td.textlength(label, font=font)
            td.text(((txt.width - tw) / 2, 0), label, fill=255, font=font)
            if yd != 50:
                # the arrow points at the nearer goal line on both sides of the field: the copy turned half round
                # below (the -Y side) has its arrow drawn on the other side first
                toward = (-1 if yd < 50 else 1) * (1 if bottom else -1)
                ax = txt.width / 2 + toward * (tw / 2 + P * 0.5)
                s = toward
                td.polygon([(ax, P * 0.8), (ax + s * -P * 0.5, P * 0.55), (ax + s * -P * 0.5, P * 1.05)], fill=255)
            if not bottom:
                txt = txt.rotate(180)
            cy = (H - 11 * P) if bottom else (11 * P - txt.height)
            im.paste(255, (int(x - txt.width / 2), int(cy)), txt)
    im = im.resize((8192, 4096), Image.LANCZOS)
    im.save(os.path.join(OUT, "T_TagFieldLines.png"))
    print("lines:", im.size)


if __name__ == "__main__":
    what = sys.argv[1:] or ["macro", "logos", "words", "lines", "blades"]
    for w in what:
        {"blades": blades, "macro": macro, "logos": logos, "words": words, "lines": lines}[w]()
