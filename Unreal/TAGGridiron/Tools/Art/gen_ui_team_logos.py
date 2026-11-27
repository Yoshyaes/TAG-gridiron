# The team marks for the menus (the team select screen): the helmet atlas (gen_team_logos.py, R = fill, G = ring) cut
# into one RGBA picture per team, painted in the colours the helmet paints them (Athlete.cpp, the LogoOn branch): the
# fill in the team's second colour (its accent when the second colour does not stand off the shell), the ring in the
# accent (or near black / near white when that does not stand off the fill). Transparent around the mark: the screen
# lays it on the team's primary colour, as the helmet shell does.
#   python gen_ui_team_logos.py   -> Content/Slate/TeamLogos/<ABBR>.png (256 x 256, sRGB, straight alpha)
import os
import re

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
ATLAS = os.path.join(ROOT, "RawArt", "Hero", "Textures", "T_TAG_TeamLogos.png")
LEAGUE = os.path.join(ROOT, "Source", "TAGGridiron", "Sim", "League.cpp")
OUT = os.path.join(ROOT, "Content", "Slate", "TeamLogos")
CELL, COLS = 256, 8


def seeds():
    """(abbr, primary, secondary, accent) in league order, from League.cpp kTeamSeeds."""
    pat = re.compile(r'\{"[^"]+",\s*"[^"]+",\s*"([A-Z]{3})",\s*"(#[0-9A-Fa-f]{6})",\s*"(#[0-9A-Fa-f]{6})",\s*"(#[0-9A-Fa-f]{6})"\}')
    with open(LEAGUE, encoding="utf-8") as f:
        return pat.findall(f.read())


def lin(h):
    """sRGB hex -> linear rgb (FLinearColor(FColor) does the same)."""
    c = np.array([int(h[i:i + 2], 16) for i in (1, 3, 5)], np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def lum(c):
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]


def main():
    atlas = np.asarray(Image.open(ATLAS).convert("RGB")).astype(np.float64) / 255.0
    os.makedirs(OUT, exist_ok=True)
    D = lambda a, b: float(np.abs(a - b).sum())
    for i, (abbr, pri, sec, acc) in enumerate(seeds()):
        P, S, A = lin(pri), lin(sec), lin(acc)
        fill = S if D(S, P) > 0.35 else A
        line = A if D(A, fill) > 0.35 else (np.array([0.03, 0.03, 0.035]) if lum(fill) > 0.5 else np.array([0.92, 0.92, 0.92]))
        y0, x0 = (i // COLS) * CELL, (i % COLS) * CELL
        f = atlas[y0:y0 + CELL, x0:x0 + CELL, 0][..., None]
        r = atlas[y0:y0 + CELL, x0:x0 + CELL, 1][..., None]
        a = np.maximum(f, r)
        # the fill lies over the ring (the letter on its badge); blend in linear light, then back to sRGB
        col = line * (1 - f) + fill * f
        rgb = srgb(col) * 255.0
        img = np.concatenate([rgb, a * 255.0], axis=2).round().clip(0, 255).astype(np.uint8)
        Image.fromarray(img, "RGBA").save(os.path.join(OUT, abbr + ".png"))
    print("wrote", len(seeds()), "marks to", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
