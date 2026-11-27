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
# World NSM maps (RG normal XY, B smoothness, A metal). Across 53 stadium maps the strongest structured
# signals test OpenGL style (chainlink fence 0.75, stand cloth 0.61) like the character maps. The DirectX
# votes come from isotropic noise surfaces (concrete, galvanized metal) where the curl test is weak. One
# rule for the whole game, so green is inverted here too. Each conversion logs its own curl measurement.
NSM_FLIP_GREEN = True
VERSION = "texfix-1 flip_green=%s rebuild_z=%s" % (NORMAL_FLIP_GREEN, NORMAL_REBUILD_Z)
NSM_VERSION = "texfix-nsm-1 flip_green=%s" % NSM_FLIP_GREEN


def is_nsm(name):
    return name.lower().endswith("_nsm.png")


def curl(x, y):
    z = np.sqrt(np.clip(1 - x * x - y * y, 0, 1))
    nx, ny = x / np.maximum(z, 0.05), y / np.maximum(z, 0.05)
    d1, d2 = np.diff(nx, axis=0)[:, :-1], np.diff(ny, axis=1)[:-1, :]
    gl, dx = float(np.mean((d1 + d2) ** 2)), float(np.mean((d1 - d2) ** 2))
    return {"opengl": round(gl, 6), "directx": round(dx, 6), "convention": "DirectX" if dx < gl else "OpenGL",
            "confidence": round(abs(gl - dx) / max(gl, dx, 1e-12), 3)}


def convert_nsm(src, dst_n, dst_rsm):
    """NSM -> DirectX normal (Z rebuilt) and an RSM style map (R roughness = 1 - smoothness, G 0.5, B metal)."""
    a = np.asarray(Image.open(src).convert("RGBA")).astype(np.float64) / 255.0
    x = a[..., 0] * 2 - 1
    y = a[..., 1] * 2 - 1
    measured = curl(x, y)
    if NSM_FLIP_GREEN:
        y = -y
    z = np.sqrt(np.clip(1 - x * x - y * y, 0, 1))
    n = np.stack([(x + 1) / 2, (y + 1) / 2, (z + 1) / 2, np.ones_like(x)], axis=-1)
    Image.fromarray(np.clip(n * 255 + 0.5, 0, 255).astype(np.uint8)).save(dst_n)
    rsm = np.stack([1 - a[..., 2], np.full_like(x, 0.5), a[..., 3], np.ones_like(x)], axis=-1)
    Image.fromarray(np.clip(rsm * 255 + 0.5, 0, 255).astype(np.uint8)).save(dst_rsm)
    return measured


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
        if f.endswith(".png") and is_nsm(f):
            src = os.path.join(tex_dir, f)
            dst_n = os.path.join(out_dir, f[:-4] + "_n_ue.png")
            dst_r = os.path.join(out_dir, f[:-4] + "_rsm_ue.png")
            src_hash = madden.sha256(src)
            prev = m["files"].get(madden.rel(dst_n), {})
            if all(os.path.exists(d) for d in (dst_n, dst_r)) and prev.get("source_sha256") == src_hash and prev.get("params", {}).get("version") == NSM_VERSION:
                skipped += 1
                continue
            measured = convert_nsm(src, dst_n, dst_r)
            for path, kind in ((dst_n, "nsm_normal_ue"), (dst_r, "nsm_rsm_ue")):
                m["files"][madden.rel(path)] = {"op": "texfix", "stage": "stage7", "kind": kind, "source_file": madden.rel(src),
                                                "source_sha256": src_hash, "size": os.path.getsize(path), "sha256": madden.sha256(path),
                                                "params": {"version": NSM_VERSION, "measured_curl": measured}}
            done += 1
            continue
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
