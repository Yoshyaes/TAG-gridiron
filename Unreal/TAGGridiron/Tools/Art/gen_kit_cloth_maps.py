# In-house uniform cloth maps (realism plan, phase 7: no team's licensed textures on the kits). Writes, into
# RawArt/Hero/Textures, the tileable surface maps hero_import.py puts on every kit's jersey and pants:
#   T_TAG_Hero_JerseyKnitN.png  mesh knit (OpenGL normal): rows of small pores in a knit, the weave of a game jersey
#   T_TAG_Hero_JerseyRSM.png    R roughness ~0.8 (cloth, no gloss), G specular 0.35, B metallic 0
#   T_TAG_Hero_PantsTwillN.png  tackle twill (OpenGL normal): fine diagonal ribs
#   T_TAG_Hero_PantsRSM.png     R roughness ~0.66 (a dull sheen), G specular 0.4, B 0
# The maps tile, so they read the same across every UV island of the pack's kit meshes. Their own density (2048 px
# over the kits' 0-1 UVs, ~8 mm between pores): the kit material has no tiling input, unlike the MetaHuman uniforms
# that take gen_uniform_textures.py's 512 px tiles.
#   python gen_kit_cloth_maps.py [size=2048]
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "RawArt", "Hero", "Textures")
N = int(sys.argv[1]) if len(sys.argv) > 1 else 2048
rng = np.random.default_rng(7)


def tile_noise(n, period, octaves=3):
    """Smooth tileable value noise in [-1, 1] (bilinear lattice, wraps at the edges)."""
    out = np.zeros((n, n))
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        p = period * 2 ** o
        g = rng.uniform(-1, 1, (p, p))
        x = np.arange(n) * p / n
        i0 = np.floor(x).astype(int) % p
        i1 = (i0 + 1) % p
        f = x - np.floor(x)
        f = f * f * (3 - 2 * f)
        a = g[i0][:, i0] * (1 - f)[None, :] + g[i0][:, i1] * f[None, :]
        b = g[i1][:, i0] * (1 - f)[None, :] + g[i1][:, i1] * f[None, :]
        out += amp * (a * (1 - f)[:, None] + b * f[:, None])
        tot += amp
        amp *= 0.5
    return out / tot


def normal_from_height(h, strength):
    """OpenGL tangent normal (green up) from a tileable height field."""
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5 * strength
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5 * strength
    nz = np.ones_like(h)
    v = np.stack([-dx, dy, nz], -1)
    v /= np.linalg.norm(v, axis=-1, keepdims=True)
    return (np.clip(v * 0.5 + 0.5, 0, 1) * 255).astype(np.uint8)


def save(name, arr):
    Image.fromarray(arr).save(os.path.join(OUT, name + ".png"))
    print("wrote", name, arr.shape)


yy, xx = np.mgrid[0:N, 0:N].astype(float)

# ---- jersey: mesh knit. A brick lattice of pores (period P px) on a knit of fine vertical loops.
P = 8.0
row = np.floor(yy / P)
cx = (xx + (row % 2) * P * 0.5) % P - P / 2
cy = yy % P - P / 2
pore = np.exp(-(cx ** 2 + cy ** 2) / (2 * (P * 0.16) ** 2))              # the holes
loops = 0.35 * np.cos(2 * np.pi * xx / (P / 3)) * (0.6 + 0.4 * np.cos(2 * np.pi * yy / (P / 2)))
h = -pore + loops + 0.25 * tile_noise(N, 16)
save("T_TAG_Hero_JerseyKnitN", normal_from_height(h, 1.1))
rough = np.clip(0.80 + 0.06 * tile_noise(N, 8) + 0.06 * pore, 0, 1)
rsm = np.stack([rough, np.full_like(rough, 0.35), np.zeros_like(rough), np.ones_like(rough)], -1)
save("T_TAG_Hero_JerseyRSM", (rsm * 255).astype(np.uint8))

# ---- pants: tackle twill. Diagonal ribs (2/1 twill) with a soft fibre noise.
Q = 6.0
twill = np.cos(2 * np.pi * (xx + yy) / Q)
twill = np.sign(twill) * np.abs(twill) ** 0.6
h = 0.8 * twill + 0.3 * tile_noise(N, 32) + 0.15 * tile_noise(N, 128, 1)
save("T_TAG_Hero_PantsTwillN", normal_from_height(h, 0.9))
rough = np.clip(0.66 + 0.05 * tile_noise(N, 16) - 0.03 * twill, 0, 1)
rsm = np.stack([rough, np.full_like(rough, 0.40), np.zeros_like(rough), np.ones_like(rough)], -1)
save("T_TAG_Hero_PantsRSM", (rsm * 255).astype(np.uint8))
