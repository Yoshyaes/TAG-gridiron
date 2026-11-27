# In-house team marks (realism plan, phase 7: the fictional teams had no logos). One mark per team of the league
# (Source/TAGGridiron/Sim/League.cpp kTeamSeeds order = team id), drawn into an 8 x 4 atlas of 256 px cells that the
# helmet shell material projects onto both sides of the shell (hero_import.py, COLOR_HLSL LogoOn branch):
#   R = the mark's fill (painted in the team's second colour), G = its outline ring and badge (the third colour).
# A mark is the team name's initial in the collegiate face the numbers use, alone or on a badge (circle, shield,
# diamond, hexagon); the badge is picked from the team id so neighbours differ. Upright, so it reads the same on both
# sides of the helmet (the material never mirrors a letter).
#   python gen_team_logos.py            -> RawArt/Hero/Textures/T_TAG_TeamLogos.png (2048 x 1024, linear)
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
FONT = os.path.join(ROOT, "RawArt", "Fonts", "Graduate-Regular.ttf")
OUT = os.path.join(ROOT, "RawArt", "Hero", "Textures", "T_TAG_TeamLogos.png")
CELL = 256
COLS, ROWS = 8, 4

# team names in League.cpp order (id 0..31)
TEAMS = ["Outlaws", "Lumberjacks", "Vaqueros", "Twisters", "Summit", "Ironclads", "Goldrush", "Kings", "Thoroughbreds",
         "Forge", "Roadrunners", "Stampede", "Waves", "Kestrels", "Oaks", "Ironhorses", "Bison", "Aurora", "Scorpions",
         "Sundogs", "Timberwolves", "Brewmasters", "Archers", "Tritons", "Comets", "Bridges", "Huskies", "Aztecas",
         "Mariners", "Cyclones FC", "Palmettos", "Raptors"]
BADGES = ["none", "circle", "shield", "diamond", "hex"]


def badge_poly(kind, s):
    """The badge outline, kept to the middle 86 % of the cell (the forward lean must not push it off the cell)."""
    pts = _badge_poly(kind, s)
    c = s / 2
    return pts if not pts else [(c + (x - c) * 0.86, c + (y - c) * 0.86) for x, y in pts]


def _badge_poly(kind, s):
    c = s / 2
    if kind == "circle":
        return None
    if kind == "shield":
        return [(c - 0.40 * s, 0.10 * s), (c + 0.40 * s, 0.10 * s), (c + 0.40 * s, 0.52 * s), (c, 0.92 * s), (c - 0.40 * s, 0.52 * s)]
    if kind == "diamond":
        return [(c, 0.05 * s), (0.95 * s, c), (c, 0.95 * s), (0.05 * s, c)]
    if kind == "hex":
        return [(c + 0.44 * s * np.cos(a), c + 0.44 * s * np.sin(a)) for a in np.radians(np.arange(30, 390, 60))]
    return []


def mark(name, idx):
    s = CELL * 4  # drawn large, then downsampled (clean edges)
    kind = BADGES[(idx * 3 + len(name)) % len(BADGES)]
    fill = Image.new("L", (s, s), 0)
    ring = Image.new("L", (s, s), 0)
    df, dr = ImageDraw.Draw(fill), ImageDraw.Draw(ring)
    if kind == "circle":
        dr.ellipse((0.11 * s, 0.11 * s, 0.89 * s, 0.89 * s), fill=255)
    elif kind != "none":
        dr.polygon(badge_poly(kind, s), fill=255)
    letter = name[0]
    box = 0.62 if kind != "none" else 0.86
    size = int(s * box)
    font = ImageFont.truetype(FONT, size)
    while True:
        b = df.textbbox((0, 0), letter, font=font)
        if b[3] - b[1] <= s * box * 0.95 and b[2] - b[0] <= s * box * 0.95:
            break
        size -= 8
        font = ImageFont.truetype(FONT, size)
    b = df.textbbox((0, 0), letter, font=font)
    x = (s - (b[2] - b[0])) / 2 - b[0]
    y = (s - (b[3] - b[1])) / 2 - b[1] + (0.04 * s if kind == "shield" else 0)
    df.text((x, y), letter, fill=255, font=font)
    # the letter's own outline ring (G), widened; inside a badge the badge is the ring colour already
    # (a blur and a threshold: a dilation by ~2 % of the cell, fast at this size)
    edge = np.asarray(fill.filter(ImageFilter.GaussianBlur(s * 0.012))) > 8
    ring = Image.fromarray(np.maximum(np.asarray(ring), edge.astype(np.uint8) * 255))
    fill = fill.resize((CELL, CELL), Image.LANCZOS)
    ring = ring.resize((CELL, CELL), Image.LANCZOS)
    return np.asarray(fill), np.asarray(ring)


def main():
    atlas = np.zeros((ROWS * CELL, COLS * CELL, 3), np.uint8)
    for i, name in enumerate(TEAMS):
        f, r = mark(name, i)
        y0, x0 = (i // COLS) * CELL, (i % COLS) * CELL
        atlas[y0:y0 + CELL, x0:x0 + CELL, 0] = f
        atlas[y0:y0 + CELL, x0:x0 + CELL, 1] = r
    Image.fromarray(atlas).save(OUT)
    print("wrote", OUT, atlas.shape)


if __name__ == "__main__":
    main()
