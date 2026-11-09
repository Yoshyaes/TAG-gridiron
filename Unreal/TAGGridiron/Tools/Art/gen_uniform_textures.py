"""TAG uniform texture pack (LOOK-AND-FEEL-PLAN Phase 2.3). Original, procedural, Pillow + NumPy.

    py -3.13 -I Tools/Art/gen_uniform_textures.py [<out dir, default RawArt>]

Writes
  T_TagNumbers.png       digit atlas 0-9 in 10 cells (2560 x 384). R = digit fill, G = outline ring. Linear.
  T_TagJerseyMesh_N.png  tileable athletic mesh knit (staggered round perforations over a fine rib), normal map.
  T_TagTwill_N.png       tileable 2/1 twill for pants, normal map.
Font: Graduate (SIL Open Font License 1.1, RawArt/Fonts/OFL-Graduate.txt).
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "RawArt")
FONT = os.path.join(HERE, "..", "..", "RawArt", "Fonts", "Graduate-Regular.ttf")


def numbers(cell_w=256, cell_h=384, outline_px=14):
    W = cell_w * 10
    fill = Image.new("L", (W, cell_h), 0)
    d = ImageDraw.Draw(fill)
    size = cell_h
    font = ImageFont.truetype(FONT, size)
    # fit the tallest digit to 86 percent of the cell height and 88 percent of the cell width
    while True:
        boxes = [d.textbbox((0, 0), str(k), font=font) for k in range(10)]
        h = max(b[3] - b[1] for b in boxes)
        w = max(b[2] - b[0] for b in boxes)
        if h <= cell_h * 0.86 - 2 * outline_px and w <= cell_w * 0.88 - 2 * outline_px:
            break
        size -= 4
        font = ImageFont.truetype(FONT, size)
    for k in range(10):
        b = d.textbbox((0, 0), str(k), font=font)
        x = k * cell_w + (cell_w - (b[2] - b[0])) / 2 - b[0]
        y = (cell_h - (b[3] - b[1])) / 2 - b[1]
        d.text((x, y), str(k), fill=255, font=font)
    ring = fill.filter(ImageFilter.MaxFilter(2 * outline_px + 1))
    ring = ring.filter(ImageFilter.GaussianBlur(1.0))
    fill_b = fill.filter(ImageFilter.GaussianBlur(0.8))
    rgb = Image.merge("RGB", (fill_b, ring, Image.new("L", fill.size, 0)))
    rgb.save(os.path.join(RAW, "T_TagNumbers.png"))


def height_to_normal(h, strength):
    """h: float array in [0, 1], tileable. Returns uint8 RGB normal map (DirectX style green down is not needed:
    Unreal expects +Y up in tangent space for its default import, which this writes)."""
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * strength
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5 * strength
    n = np.stack([-dx, dy, np.ones_like(h)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return ((n * 0.5 + 0.5) * 255).round().astype(np.uint8)


def jersey_mesh(S=512, holes=16):
    y, x = np.mgrid[0:S, 0:S].astype(np.float32)
    pitch = S / holes
    h = np.zeros((S, S), np.float32)
    # fine knit rib running vertically
    h += 0.25 * (0.5 + 0.5 * np.cos(2 * np.pi * x / (pitch / 4)))
    # staggered perforations: periodic distance to the nearest hole centre on a staggered lattice
    gx = x / pitch
    gy = y / pitch
    row = np.floor(gy)
    off = np.where(row % 2 == 1, 0.5, 0.0)
    fx = (gx - off) - np.floor(gx - off) - 0.5
    fy = gy - row - 0.5
    d = np.sqrt(fx * fx + fy * fy)
    hole = np.clip((0.30 - d) / 0.06, 0.0, 1.0)
    h = h * (1 - hole) - 0.6 * hole + 0.6
    h = (h - h.min()) / (h.max() - h.min())
    return height_to_normal(h, strength=3.0)


def twill(S=512, period=8):
    y, x = np.mgrid[0:S, 0:S].astype(np.float32)
    diag = (x + y * 0.5) / period            # 2/1 twill: diagonal wales
    h = 0.5 + 0.5 * np.cos(2 * np.pi * diag)
    h += 0.15 * (0.5 + 0.5 * np.cos(2 * np.pi * x / (period / 2)))
    h = (h - h.min()) / (h.max() - h.min())
    return height_to_normal(h, strength=1.6)


def main():
    os.makedirs(RAW, exist_ok=True)
    numbers()
    Image.fromarray(jersey_mesh()).save(os.path.join(RAW, "T_TagJerseyMesh_N.png"))
    Image.fromarray(twill()).save(os.path.join(RAW, "T_TagTwill_N.png"))
    print("uniform textures ->", os.path.abspath(RAW))


if __name__ == "__main__":
    main()
