"""Flags every catalog item whose materials still sample an engine placeholder (py -3.13 -I Tools/Art/hero_placeholder_check.py).

Reads Saved/hero_mat_sources.json (Tools/Art/hero_mat_dump.py, run in the editor first). A placeholder is Madden's
missing-texture rainbow (hue bands, the hero_full.py test), its default_runtime_color checker or its
debug_texture_norm. Prints a count per kind and every hit; exit code 1 when anything is found.
"""
import colorsys
import json
import os
import sys

from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
src = json.load(open(os.path.join(ROOT, "Saved", "hero_mat_sources.json")))
_cache = {}


def placeholder(path):
    if path in _cache:
        return _cache[path]
    low = path.replace("\\", "/").lower()
    why = None
    if "default_runtime" in low or "debug_texture" in low:
        why = os.path.basename(low)
    elif os.path.exists(path):
        try:
            px = list(Image.open(path).convert("RGB").resize((32, 8)).getdata())
            bins = {int(colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)[0] * 12) for r, g, b in px
                    if colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)[1] > 0.6 and max(r, g, b) > 127}
            if len(bins) >= 7:
                why = "rainbow " + os.path.basename(low)
        except Exception:
            pass
    _cache[path] = why
    return why


hits, kinds, missing = [], {}, []
for name, it in src.items():
    if it.get("_missing"):
        missing.append(name)
        continue
    kinds[it["kind"]] = kinds.get(it["kind"], 0) + 1
    for slot, s in it["slots"].items():
        for param, f in s["tex"]:
            w = placeholder(f) if f else None
            if w:
                hits.append("%s %s %s: %s" % (name, slot, param, w))
print("items %d by kind %s, missing assets %d, textures looked at %d" % (sum(kinds.values()), kinds, len(missing), len(_cache)))
print("placeholder hits: %d" % len(hits))
for h in hits[:80]:
    print("  " + h)
for m in missing[:20]:
    print("  missing " + m)
sys.exit(1 if hits or missing else 0)
