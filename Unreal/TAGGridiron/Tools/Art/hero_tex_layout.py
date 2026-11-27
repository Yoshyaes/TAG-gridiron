"""Channel layout of every colour texture the model pack characters use (py -3.13 -I Tools/Art/hero_tex_layout.py).

Many of the pack's "_col" textures are not colour: the extraction wrote Madden's single-channel gear maps (BC4) into
the red channel, some with a region mask in blue (cleats: sole, heel, tongue, the brand mark) or green (windbreakers,
a few coach tops). Shown as colour they render red (tape, belts, wristbands, coach trousers) or magenta / purple
(cleats, jackets). Madden shades them as a grey overlay (mid grey = the gear colour) with the mask picking a second
colour. hero_import.py reads this table and gives those slots that shading (COLOR_HLSL Mode 5, "Packed").

Output RawArt/Hero/tex_layout.json: {texture path: {"layout": "RGB" | "R" | "RB" | "RG", "r": mean red (sRGB 0-1)}},
plus "skin_tint" (linear RGB) on arm / leg textures the pack leaves as the missing-texture rainbow.
"""
import glob
import json
import os
import re
import sys

from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
HERO = os.path.join(ROOT, "RawArt", "Hero")


def face_tone(path):
    """Median skin colour (linear RGB) of a face texture's middle: texels that read as skin (red over green over
    blue, neither near black nor near white). None when the face is missing or is itself the rainbow."""
    if not os.path.exists(path):
        return None
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = list(im.crop((int(w * 0.3), int(h * 0.3), int(w * 0.7), int(h * 0.7))).resize((64, 64)).getdata())
    skin = [p for p in px if p[0] > p[1] > p[2] and p[0] - p[2] > 12 and 40 < p[0] < 250]
    if len(skin) < len(px) * 0.2:
        return None
    med = [sorted(p[i] for p in skin)[len(skin) // 2] / 255.0 for i in range(3)]
    return [round(c ** 2.2, 4) for c in med]
OUT = os.path.join(HERO, "tex_layout.json")


def layout(path):
    im = Image.open(path).convert("RGBA").resize((64, 64))
    px = list(im.getdata())
    n = float(len(px))
    r = sum(p[0] for p in px) / n / 255.0
    # the packed signature: most texels carry red alone (grey detail with no colour). Unused UV padding can be an even
    # grey in all three channels (handwarmer_col), so the test is the red-only share, not "G and B empty everywhere";
    # an evenly grey colour texture (white socks, towels) has no red-only texels and stays RGB
    # Either test marks it packed: green or blue empty almost everywhere (the mask lives in the other one and may
    # cover most of the map: cleats), or most texels red alone (a mask plus even-grey padding: handwarmer_col)
    red_only = sum(1 for p in px if p[0] > 20 and p[1] < 4 and p[2] < 4) / n
    g0 = sum(1 for p in px if p[1] < 3) / n
    b0 = sum(1 for p in px if p[2] < 3) / n
    if g0 > 0.97 and b0 > 0.97:
        return {"layout": "R", "r": round(r, 4)}
    if g0 > 0.97:
        return {"layout": "RB", "r": round(r, 4)}
    if b0 > 0.97 and r > 0.02:
        return {"layout": "RG", "r": round(r, 4)}
    if red_only < 0.35:
        return {"layout": "RGB", "r": round(r, 4)}
    # the region mask: the channel whose texels go bright where red has detail (ignoring even-grey padding)
    def masked(i):
        return sum(1 for p in px if p[i] > 128 and abs(p[i] - p[0]) > 24) / n
    mg, mb = masked(1), masked(2)
    k = "RB" if mb > 0.01 and mb >= mg else "RG" if mg > 0.01 else "R"
    return {"layout": k, "r": round(r, 4)}


def main():
    paths = set()
    for f in glob.glob(os.path.join(HERO, "*", "*.json")):
        try:
            d = json.load(open(f))
        except Exception:
            continue
        if not isinstance(d, dict):
            continue
        for info in d.get("slots", {}).values():
            c = (info.get("textures") or {}).get("colorTexture") or (info.get("textures") or {}).get("swarmColor")
            if c:
                paths.add(c)
    out = {}
    for p in sorted(paths):
        try:
            out[p] = layout(p)
        except Exception as e:  # a missing file stays RGB (the import reports it)
            print("skip", p, e, file=sys.stderr)
    # arms / legs the pack leaves as the missing-texture rainbow (7 story characters): the skin tone of the
    # character's own face, so hero_import.py paints them flat in that tone instead of the rainbow
    tints = 0
    for f in glob.glob(os.path.join(HERO, "*", "*.json")):
        try:
            d = json.load(open(f))
        except Exception:
            continue
        if not isinstance(d, dict):
            continue
        head = ((d.get("slots", {}).get("Head") or {}).get("textures") or {})
        for info in d.get("slots", {}).values():
            t = info.get("textures") or {}
            c = t.get("colorTexture")
            if not c or t.get("_placeholder") != "rainbow" or not re.search(r"_(arms|legs)_", c) or c not in out:
                continue
            # the character's own face; a donor face when the pack has none (hero_full.py donor_face)
            tone = face_tone(head["colorTexture"] if head.get("_donor") and head.get("colorTexture") else re.sub(r"_(arms|legs)_", "_", c))
            if tone:
                out[c]["skin_tint"] = tone
                tints += 1
    print("skin tints for rainbow arms / legs: %d" % tints)
    json.dump(out, open(OUT, "w"), indent=0)
    counts = {}
    for v in out.values():
        counts[v["layout"]] = counts.get(v["layout"], 0) + 1
    print("tex_layout: %d textures %s -> %s" % (len(out), counts, OUT))


main()
