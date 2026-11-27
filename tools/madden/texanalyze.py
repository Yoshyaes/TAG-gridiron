"""Texture convention analysis for the Madden 19 reference pipeline. Read only on extracted PNGs.

For normal maps it reports
  z_stats      blue channel decoded to [-1, 1]. A real Z channel sits near +1. A two channel map has
               a constant or zero blue channel and needs Z rebuilt.
  unit_error   mean | |n| - 1 | with the stored Z. Small means XYZ is a proper unit normal.
  convention   curl test. A normal map derived from a height field is curl free under the right green
               convention. With rows increasing downward and n = (-dh/dcol, ny, 1):
                 OpenGL (green up)   ny = +dh/drow, so d(nx)/drow + d(ny)/dcol = 0
                 DirectX (green down) ny = -dh/drow, so d(nx)/drow - d(ny)/dcol = 0
               The smaller residual wins. Unreal expects DirectX.
For RSM style maps it reports per channel mean, standard deviation and range.

Usage
  py -3.13 -I texanalyze.py <png> [<png> ...] [--json out.json]
"""
import json
import sys

import numpy as np
from PIL import Image


def load(path):
    im = Image.open(path).convert("RGBA")
    return np.asarray(im).astype(np.float64) / 255.0


def normal_report(a):
    x = a[..., 0] * 2 - 1
    y = a[..., 1] * 2 - 1
    z = a[..., 2] * 2 - 1
    length = np.sqrt(x * x + y * y + z * z)
    r = {
        "z_mean": round(float(z.mean()), 4), "z_min": round(float(z.min()), 4),
        "z_std": round(float(z.std()), 4),
        "unit_error": round(float(np.abs(length - 1).mean()), 4),
        "alpha_mean": round(float(a[..., 3].mean()), 4),
    }
    nx = x / np.maximum(z, 0.05)
    ny = y / np.maximum(z, 0.05)
    dnx_drow = np.diff(nx, axis=0)[:, :-1]
    dny_dcol = np.diff(ny, axis=1)[:-1, :]
    gl = float(np.mean((dnx_drow + dny_dcol) ** 2))
    dx = float(np.mean((dnx_drow - dny_dcol) ** 2))
    r["curl_residual_opengl"] = round(gl, 6)
    r["curl_residual_directx"] = round(dx, 6)
    r["convention"] = "DirectX" if dx < gl else "OpenGL"
    r["confidence"] = round(abs(gl - dx) / max(gl, dx, 1e-12), 3)
    two_channel = r["z_std"] < 0.01 and (r["z_mean"] < 0.1 or r["unit_error"] > 0.2)
    r["needs_z_rebuild"] = bool(two_channel or r["unit_error"] > 0.15)
    return r


def channel_report(a):
    out = {}
    for i, c in enumerate("RGBA"):
        ch = a[..., i]
        out[c] = {"mean": round(float(ch.mean()), 3), "std": round(float(ch.std()), 3),
                  "min": round(float(ch.min()), 3), "max": round(float(ch.max()), 3)}
    return out


def main(argv):
    paths = [p for p in argv[1:] if not p.startswith("--")]
    out_json = argv[argv.index("--json") + 1] if "--json" in argv else None
    if out_json in paths:
        paths.remove(out_json)
    results = {}
    for p in paths:
        a = load(p)
        low = p.lower()
        kind = "normal" if ("norm" in low or "normal" in low) else ("rsm" if "rsm" in low else "color")
        r = {"kind": kind, "size": [a.shape[1], a.shape[0]], "channels": channel_report(a)}
        if kind == "normal":
            r["normal"] = normal_report(a)
        results[p] = r
        brief = r.get("normal", {})
        print(p.split("/")[-1].split("\\")[-1], kind, json.dumps(brief) if brief else json.dumps({c: r["channels"][c]["mean"] for c in "RGBA"}))
    if out_json:
        json.dump(results, open(out_json, "w"), indent=1)


if __name__ == "__main__":
    main(sys.argv)
