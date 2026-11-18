"""Generate original source textures for TAG Gridiron (turf, field markings, crowd).

Run with the system Python (needs Pillow + NumPy):  python gen_textures.py <outdir>
Everything here is procedurally generated; no third-party art.
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "RawArt")
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(2027)


def tileable_noise(n, scale, octaves=5, seed=0):
    """Periodic value noise in [0,1] (tiles seamlessly)."""
    r = np.random.default_rng(seed)
    acc = np.zeros((n, n), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        g = int(scale * (2 ** o))
        grid = r.random((g, g)).astype(np.float32)
        # bilinear upsample with wrap
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
        v = (a * (1 - sx) + b * sx) * (1 - sy) + (c * (1 - sx) + d * sx) * sy
        acc += v * amp
        tot += amp
        amp *= 0.55
    return acc / tot


def grass(n=2048):
    """Synthetic turf: many short directional blades + clumps + dirt/rubber specks."""
    h = tileable_noise(n, 8, 6, seed=1) * 0.35
    img = np.zeros((n, n), np.float32)
    blades = 260000
    xs = rng.integers(0, n, blades)
    ys = rng.integers(0, n, blades)
    ln = rng.integers(4, 11, blades)
    ang = rng.normal(-1.45, 0.35, blades)  # mostly "up" the image
    br = rng.random(blades).astype(np.float32)
    for k in range(10):
        m = ln > k
        px = (xs[m] + np.round(np.cos(ang[m]) * k).astype(int)) % n
        py = (ys[m] + np.round(np.sin(ang[m]) * k).astype(int)) % n
        np.maximum.at(img, (py, px), br[m] * (1 - k / 12.0))
    img = 0.55 * img + h + 0.15 * tileable_noise(n, 32, 3, seed=2)
    img = (img - img.min()) / (img.max() - img.min())
    clump = tileable_noise(n, 4, 4, seed=3)
    # albedo: deep green with yellow-green tips, darker clumps
    base = np.stack([0.10 + 0.10 * img, 0.30 + 0.28 * img, 0.06 + 0.06 * img], -1)
    base *= (0.85 + 0.3 * clump)[..., None]
    specks = rng.random((n, n)) < 0.0035
    base[specks] = [0.06, 0.05, 0.045]  # rubber infill
    alb = Image.fromarray(np.clip(base * 255, 0, 255).astype(np.uint8), "RGB")
    alb.save(os.path.join(OUT, "T_TurfGrass_D.png"))
    # normal map from height
    hgt = img * 0.8 + clump * 0.2
    dx = (np.roll(hgt, -1, 1) - np.roll(hgt, 1, 1)) * 6
    dy = (np.roll(hgt, -1, 0) - np.roll(hgt, 1, 0)) * 6
    nz = np.ones_like(dx)
    l = np.sqrt(dx * dx + dy * dy + nz * nz)
    nrm = np.stack([(-dx / l) * 0.5 + 0.5, (-dy / l) * 0.5 + 0.5, (nz / l) * 0.5 + 0.5], -1)
    Image.fromarray((nrm * 255).astype(np.uint8), "RGB").save(os.path.join(OUT, "T_TurfGrass_N.png"))
    # roughness/variation mask
    Image.fromarray((clump * 255).astype(np.uint8), "L").save(os.path.join(OUT, "T_TurfVariation.png"))


def field_lines(px_per_yd=48):
    """White paint mask for the full 120 x 53.33 yd field (x = length, y = width)."""
    W, H = int(120 * px_per_yd), int(160 / 3 * px_per_yd)
    im = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(im)
    P = px_per_yd
    line = lambda x0, y0, x1, y1, w: d.rectangle([x0, y0, x1, y1], fill=255) if w else None
    four_in = max(2, int(round(P * 4 / 36)))
    # sidelines/end lines (6 ft wide border reads as 2 yd in some stadiums; use 6 in here)
    b = int(P * 0.33)
    d.rectangle([0, 0, W, b], fill=255)
    d.rectangle([0, H - b, W, H], fill=255)
    d.rectangle([0, 0, b, H], fill=255)
    d.rectangle([W - b, 0, W, H], fill=255)
    hash_off = (70.75 / 3)  # 70'9" from sideline, yards
    for yd in range(0, 101):
        x = int((yd + 10) * P)
        if yd % 5 == 0:
            w = int(P * (8 / 36)) if yd in (0, 100) else four_in
            d.rectangle([x - w // 2, b, x + w // 2, H - b], fill=255)
        else:
            for y0 in (b, H - b - int(P * 0.67), int(hash_off * P) - int(P * 0.67) // 2, H - int(hash_off * P) - int(P * 0.67) // 2):
                d.rectangle([x - four_in // 2, y0, x + four_in // 2, y0 + int(P * 0.67)], fill=255)
    for yd in (2, 98):
        x = int((yd + 10) * P)
        d.rectangle([x - four_in // 2, H // 2 - P // 2, x + four_in // 2, H // 2 + P // 2], fill=255)
    # numbers: 6 ft tall, tops 9 yd from sideline, with direction arrows
    try:
        font = ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", int(P * 2.0))
    except OSError:
        font = ImageFont.load_default()
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
                ax = txt.width / 2 + (-1 if yd < 50 else 1) * (tw / 2 + P * 0.5)
                s = 1 if yd > 50 else -1
                td.polygon([(ax, P * 0.8), (ax + s * -P * 0.5, P * 0.55), (ax + s * -P * 0.5, P * 1.05)], fill=255)
            if not bottom:
                txt = txt.rotate(180)
            cy = (H - 11 * P) if bottom else (11 * P - txt.height)
            im.paste(255, (int(x - txt.width / 2), int(cy)), txt)
    im = im.filter(ImageFilter.GaussianBlur(0.6))
    im.save(os.path.join(OUT, "T_FieldLines.png"))


def crowd(w=2048, h=1024):
    """Rows of seated/standing fans. RGB = people, A = team-shirt mask (tinted in-engine).

    Drawn as a crowd reads from across a stadium: mostly clothing (team shirts, jackets, hoodies), small heads with
    hair or caps over them, shoulders and arms, dark seat gaps between people, standing fans overlapping the row
    behind. (The first version gave every fan a big bare skin-tone head a quarter of a seat wide: from the field the
    stands read as rows of orange discs behind every touchdown close-up, 16 Nov.)"""
    img = Image.new("RGBA", (w, h), (14, 14, 17, 0))
    d = ImageDraw.Draw(img)
    skins = [(92, 60, 42), (120, 80, 56), (150, 104, 76), (176, 128, 98), (198, 152, 120), (70, 46, 34), (134, 92, 66)]
    hair = [(18, 14, 12), (34, 24, 18), (52, 38, 26), (90, 70, 48), (24, 22, 22), (120, 100, 70)]
    neutral = [(28, 28, 32), (196, 196, 198), (54, 62, 82), (110, 34, 36), (44, 44, 46), (138, 138, 142), (70, 84, 60),
               (22, 30, 52), (160, 150, 130), (90, 60, 40)]
    caps = [(20, 20, 24), (200, 200, 200), (30, 40, 80), (120, 30, 30)]
    rows = 16
    rh = h / rows
    for r in range(rows):
        y = (r + 1) * rh
        # the seat row: a dark band the people sit in front of
        d.rectangle([0, y - rh * 0.35, w, y + 2], fill=(20, 20, 24, 0))
        x = rng.uniform(0, 8)
        while x < w:
            sw = rng.uniform(26, 34)
            if rng.random() > 0.05:
                team = rng.random() < 0.55
                shirt = (255, 255, 255) if team else tuple(int(c) for c in neutral[rng.integers(0, len(neutral))])
                a = 255 if team else 0
                stand = rng.random() < 0.3
                top = y - rh * (1.25 if stand else 0.92)   # head top
                cx = x + sw / 2 + rng.uniform(-2, 2)
                hr = sw * rng.uniform(0.15, 0.18)          # head radius: a sixth of the seat, not a quarter
                sh = top + hr * 2.3                        # shoulders
                skin = tuple(int(c) for c in skins[rng.integers(0, len(skins))])
                # torso and shoulders (team shirt / own clothes), a touch darker at the bottom
                d.rounded_rectangle([x + 2, sh, x + sw - 2, y + 2], radius=int(sw * 0.3), fill=shirt + (a,))
                shade = tuple(int(c * 0.72) for c in shirt)
                d.rectangle([x + 2, sh + (y - sh) * 0.6, x + sw - 2, y + 2], fill=shade + (a,))
                # arms: down at the side, a few up (cheering) or across
                if rng.random() < 0.1:
                    side = -1 if rng.random() < 0.5 else 1
                    ax = cx + side * sw * 0.42
                    d.line([ax, sh + 3, ax + side * 3, top - hr * 1.2], fill=shirt + (a,), width=5)
                    d.ellipse([ax + side * 3 - 3, top - hr * 1.2 - 3, ax + side * 3 + 3, top - hr * 1.2 + 3], fill=skin + (0,))
                # neck and head
                d.rectangle([cx - hr * 0.45, top + hr * 1.6, cx + hr * 0.45, sh + 2], fill=skin + (0,))
                d.ellipse([cx - hr, top, cx + hr, top + hr * 2.1], fill=skin + (0,))
                # hair or a cap over most heads
                u = rng.random()
                if u < 0.6:
                    hc = tuple(int(c) for c in hair[rng.integers(0, len(hair))])
                    d.chord([cx - hr * 1.05, top - hr * 0.1, cx + hr * 1.05, top + hr * 1.5], 180, 360, fill=hc + (0,))
                elif u < 0.8:
                    cc = (255, 255, 255) if rng.random() < 0.4 else tuple(int(c) for c in caps[rng.integers(0, len(caps))])
                    ca = 255 if cc == (255, 255, 255) else 0
                    d.chord([cx - hr * 1.1, top - hr * 0.15, cx + hr * 1.1, top + hr * 1.3], 180, 360, fill=cc + (ca,))
                    d.rectangle([cx - hr * 0.2, top + hr * 0.55, cx + hr * 1.5, top + hr * 0.75], fill=cc + (ca,))
            x += sw + rng.uniform(-1, 3)
    img = img.filter(ImageFilter.GaussianBlur(0.6))
    # rows run along V so they follow the length of the stand mesh's top face
    img = img.transpose(Image.Transpose.ROTATE_90)
    img.save(os.path.join(OUT, "T_Crowd.png"))


def turf_wear(px_per_yd=16):
    """Wear mask over the full 120 x 53.33 yd field, same layout as T_FieldLines (x = length, y = width).
    Natural grass in season: a beaten strip between the hashes where the lines set up (heaviest between the 20s),
    scuffed patches at the snap spots, the goal-line stands inside the 5s and the kickoff spots, broken up by noise."""
    W, H = int(120 * px_per_yd), int(160 / 3 * px_per_yd)
    xs = (np.arange(W) + 0.5) / px_per_yd - 10.0          # field yards, 0 = goal line
    ys = (np.arange(H) + 0.5) / px_per_yd                  # yards from one sideline
    X, Y = np.meshgrid(xs, ys)
    mid = 80.0 / 3                                          # center of the field's width
    smooth = lambda a, b, v: np.clip((v - a) / (b - a), 0, 1) ** 2 * (3 - 2 * np.clip((v - a) / (b - a), 0, 1))
    along = smooth(-2, 12, X) * smooth(-2, 12, 100 - X)
    heavy = smooth(12, 30, X) * smooth(12, 30, 100 - X)
    band = np.exp(-((Y - mid) / 4.2) ** 2)
    m = band * (0.45 * along + 0.35 * heavy)
    r = np.random.default_rng(77)
    for _ in range(46):                                     # snap spots: the line of scrimmage moves up and down the field
        cx, cy = r.uniform(4, 96), mid + r.normal(0, 1.3)
        m += 0.55 * np.exp(-(((X - cx) / r.uniform(1.6, 3.2)) ** 2 + ((Y - cy) / r.uniform(1.8, 3.0)) ** 2))
    for gx in (2.5, 97.5):                                  # goal-line stands
        m += 0.6 * np.exp(-(((X - gx) / 3.0) ** 2 + ((Y - mid) / 5.0) ** 2))
    for kx in (35.0, 65.0):                                 # kickoff tees
        m += 0.5 * np.exp(-(((X - kx) / 0.9) ** 2 + ((Y - mid) / 0.9) ** 2))
    # break it into turf and bare patches
    n = tileable_noise(1024, 18, octaves=4, seed=5)
    n = np.array(Image.fromarray((n * 255).astype(np.uint8)).resize((W, H), Image.BILINEAR), np.float32) / 255.0
    n2 = tileable_noise(1024, 40, octaves=3, seed=9)
    n2 = np.array(Image.fromarray((n2 * 255).astype(np.uint8)).resize((W, H), Image.BILINEAR), np.float32) / 255.0
    w = np.clip(m * (0.15 + 1.2 * n ** 1.5) - 0.12 + (n2 - 0.5) * 0.45 * np.clip(m * 2, 0, 1), 0, 0.85) ** 1.3
    Image.fromarray((w * 255).astype(np.uint8), "L").filter(ImageFilter.GaussianBlur(0.8)).save(os.path.join(OUT, "T_TurfWear.png"))


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[2] == "wear":
        turf_wear()
        sys.exit(0)
    turf_wear()
    grass()
    print("grass done")
    field_lines()
    print("lines done")
    crowd()
    print("crowd done ->", os.path.abspath(OUT))
