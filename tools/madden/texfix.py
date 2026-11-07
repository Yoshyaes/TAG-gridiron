"""The one place where Madden 19 texture conventions are converted for Unreal.

Rules (evidence in FINDINGS.md, Stage 5)
  NORMAL_FLIP_GREEN  Madden normal maps test as OpenGL style (green up) with high confidence on clean
                     maps (jersey 0.95, pants 0.81). Unreal expects DirectX style, so green is inverted.
  NORMAL_REBUILD_Z   Some maps keep other data in blue (the head stores a mask there, socks store 0).
                     Z is always rebuilt from X and Y as sqrt(1 - x^2 - y^2), which also matches the
                     maps that do carry a real Z.
  Alpha is set to 1 in the converted normal. The head normal's blue mask is written next to it as
  <name>_bmask.png so nothing is lost.
Colour and RSM maps are not changed.

Outputs go to <dir>/../textures_ue/<name>_ue.png with manifest entries (op "texfix").
Idempotent. A file is rewritten only when its source hash changed.

Usage
  py -3.13 -I texfix.py <textures dir>
"""
import hashlib
import json
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import madden  # noqa: E402  (stdlib only, shares manifest helpers)

NORMAL_FLIP_GREEN = True
NORMAL_REBUILD_Z = True
VERSION = "texfix-1 flip_green=%s rebuild_z=%s" % (NORMAL_FLIP_GREEN, NORMAL_REBUILD_Z)


def is_normal(name):
    n = name.lower()
    return n.endswith(("_norm.png", "_normal.png", "_nrm.png")) or "normal" in n


def convert_normal(src, dst, bmask=None):
    a = np.asarray(Image.open(src).convert("RGBA")).astype(np.float64) / 255.0
    x = a[..., 0] * 2 - 1
    y = a[..., 1] * 2 - 1
    if NORMAL_FLIP_GREEN:
        y = -y
    if NORMAL_REBUILD_Z:
        z = np.sqrt(np.clip(1 - x * x - y * y, 0, 1))
    else:
        z = a[..., 2] * 2 - 1
    out = np.stack([(x + 1) / 2, (y + 1) / 2, (z + 1) / 2, np.ones_like(x)], axis=-1)
    Image.fromarray(np.clip(out * 255 + 0.5, 0, 255).astype(np.uint8)).save(dst)
    if bmask:
        Image.fromarray((a[..., 2] * 255 + 0.5).astype(np.uint8)).save(bmask)


def main(tex_dir):
    tex_dir = os.path.abspath(tex_dir)
    out_dir = os.path.join(os.path.dirname(tex_dir), "textures_ue")
    os.makedirs(out_dir, exist_ok=True)
    m = madden.load_manifest()
    done = skipped = 0
    for f in sorted(os.listdir(tex_dir)):
        if not f.endswith(".png") or not is_normal(f):
            continue
        src = os.path.join(tex_dir, f)
        dst = os.path.join(out_dir, f[:-4] + "_ue.png")
        src_hash = madden.sha256(src)
        prev = m["files"].get(madden.rel(dst), {})
        if os.path.exists(dst) and prev.get("source_sha256") == src_hash and prev.get("sha256") == madden.sha256(dst) and prev.get("params", {}).get("version") == VERSION:
            skipped += 1
            continue
        a = np.asarray(Image.open(src).convert("RGBA"))
        blue_is_data = float((a[..., 2].astype(np.float64) / 255 * 2 - 1).mean()) < 0.5
        bmask = os.path.join(out_dir, f[:-4] + "_bmask.png") if blue_is_data and a[..., 2].std() > 2 else None
        convert_normal(src, dst, bmask)
        for path, kind in ((dst, "normal_ue"), (bmask, "blue_mask")):
            if not path:
                continue
            m["files"][madden.rel(path)] = {"op": "texfix", "stage": "stage5", "kind": kind, "source_file": madden.rel(src),
                                            "source_sha256": src_hash, "size": os.path.getsize(path), "sha256": madden.sha256(path),
                                            "params": {"version": VERSION}}
        done += 1
    madden.save_manifest(m)
    print("texfix converted", done, "skipped", skipped, "->", out_dir)


if __name__ == "__main__":
    main(sys.argv[1])
