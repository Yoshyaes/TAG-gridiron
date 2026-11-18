r"""Team trim masks for the model pack uniforms (jersey + sleeves, pants, helmet shell).

  py -3.13 -I Tools/Art/hero_masks.py            -> RawArt/Hero/Textures/T_TAG_Hero_<part>Mask.png

The pack's uniform meshes point at flat grey textures; the game colours them per team. The only dressed set in the
pack (one white 2018 uniform) shows where a uniform's stripes go in each part's UV layout, so its stripes become a
mask: R = main trim (the set's red), G = second trim (its black / the helmet's centre band). Logos and wordmarks are
dropped (small or round connected pieces), so only stripes are left. The material mixes team colours with it.
"""
import os
import sys

from PIL import Image

PACK = r"D:\MaddenRef\ModelPack"
UNI = os.path.join(PACK, "Textures", "characters", "player", "parts", "uniforms", "49ers", "2018")
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(os.path.join(HERE, "..", "..", "RawArt", "Hero", "Textures"))


def components(mask, w, h):
    """4-connected pieces of a 0/1 list: [(pixels, (x0, y0, x1, y1))]."""
    seen = bytearray(w * h)
    out = []
    for start in range(w * h):
        if not mask[start] or seen[start]:
            continue
        stack = [start]
        seen[start] = 1
        px = []
        x0 = y0 = 1 << 30
        x1 = y1 = -1
        while stack:
            i = stack.pop()
            px.append(i)
            x, y = i % w, i // w
            x0, x1, y0, y1 = min(x0, x), max(x1, x), min(y0, y), max(y1, y)
            for j in (i - 1 if x > 0 else -1, i + 1 if x < w - 1 else -1, i - w, i + w):
                if 0 <= j < w * h and mask[j] and not seen[j]:
                    seen[j] = 1
                    stack.append(j)
        out.append((px, (x0, y0, x1, y1)))
    return out


def generic(src, w, h, region, base_region=None):
    """(classify, base) for any uniform texture: the cloth's base colour is the commonest colour inside region(x, y); a
    pixel more than a threshold from it is trim, split into two trims by a 2-means on the trim colours (R = the larger
    group, G = the other)."""
    im = Image.open(os.path.join(UNI if "/" not in src else os.path.dirname(os.path.dirname(UNI)), src)).convert("RGB").resize((w, h), Image.BILINEAR)
    px = list(im.getdata())
    q = lambda p: (p[0] // 24, p[1] // 24, p[2] // 24)
    hist = {}
    br = base_region or region
    for i, p in enumerate(px):
        if br(i % w, i // w, w, h) and max(p) > 30:  # the black around the UV islands is not cloth
            hist[q(p)] = hist.get(q(p), 0) + 1
    bq = max(hist, key=hist.get)
    base = tuple(c * 24 + 12 for c in bq)
    d2 = lambda a, b: sum((x - y) ** 2 for x, y in zip(a, b))
    trim = [p for i, p in enumerate(px) if region(i % w, i // w, w, h) and d2(p, base) > 70 ** 2 and max(p) > 30]
    if not trim:
        return (lambda p: 0), base
    c = [trim[0], max(trim, key=lambda p: d2(p, trim[0]))]
    for _ in range(8):
        g = [[], []]
        for p in trim:
            g[0 if d2(p, c[0]) <= d2(p, c[1]) else 1].append(p)
        c = [tuple(sum(v[k] for v in gi) / max(1, len(gi)) for k in range(3)) if gi else c[n] for n, gi in enumerate(g)]
    if len(g[1]) > len(g[0]):
        c = [c[1], c[0]]

    def classify(p):
        if d2(p, base) <= 70 ** 2 or max(p) <= 30:
            return 0
        return 1 if d2(p, c[0]) <= d2(p, c[1]) else 2
    return classify, base


def build(src, out, w, h, classify, keep, region=None):
    im = Image.open(os.path.join(UNI if "/" not in src else os.path.dirname(os.path.dirname(UNI)), src)).convert("RGB").resize((w, h), Image.BILINEAR)
    if region:
        px0 = list(im.getdata())
        im.putdata([p if region(i % w, i // w, w, h) else (0, 0, 0) for i, p in enumerate(px0)])
    px = list(im.getdata())
    chans = []
    for ch in (0, 1):
        m = [1 if classify(p) == ch + 1 else 0 for p in px]
        kept = bytearray(w * h)
        for comp, box in components(m, w, h):
            if keep(ch, len(comp), box, w, h):
                for i in comp:
                    kept[i] = 255
        chans.append(Image.frombytes("L", (w, h), bytes(kept)))
    z = Image.new("L", (w, h), 0)
    # grow by a pixel so the bilinear lookup does not leave a seam of base colour at the stripe edges
    from PIL import ImageFilter
    chans = [c.filter(ImageFilter.MaxFilter(3)) for c in chans]
    Image.merge("RGB", (chans[0], chans[1], z)).save(os.path.join(OUT, out))
    print(out, [sum(1 for v in c.getdata() if v) for c in chans])


def red(p):
    r, g, b = p
    return r > 120 and g < 90 and b < 90


def dark(p):
    r, g, b = p
    return r < 60 and g < 60 and b < 60


def defaults():
    """Neutral inputs for the hero masters' texture parameters."""
    for name, rgb in (("White", (255, 255, 255)), ("Black", (0, 0, 0)), ("FlatN", (128, 128, 255)), ("RSM", (140, 128, 0))):
        Image.new("RGB", (8, 8), rgb).save(os.path.join(OUT, "T_TAG_Hero_%s.png" % name))


def main():
    os.makedirs(OUT, exist_ok=True)
    defaults()
    # jersey + sleeves (2048 x 4096): red sleeve and shoulder arcs; black collar ring (the unused black around the
    # UV islands touches the border and is left out)
    build("nin_jersey_2018_whialt_jersey_col.png", "T_TAG_Hero_JerseyMask.png", 512, 1024,
          lambda p: 1 if red(p) else 2 if dark(p) else 0,
          lambda ch, n, box, w, h: n > 600 and (ch == 0 or (box[0] > 0 and box[1] > 0.5 * h and box[2] < w - 1 and box[3] < h - 1)))
    # pants: red / black / red side stripes (long and thin); the shield and swoosh are small
    build("nin_pants_2018_whialt_pants_col.png", "T_TAG_Hero_PantsMask.png", 512, 512,
          lambda p: 1 if red(p) else 2 if dark(p) else 0,
          lambda ch, n, box, w, h: n > 800 and (box[3] - box[1]) > 4 * max(1, box[2] - box[0]))
    # the pack's other sets (same UV layouts): Bullfrogs jersey / pants / helmet, Cowboys, Dolphins, Longshot helmets
    jersey_region = lambda x, y, w, h: True
    stripe = lambda ch, n, box, w, h: n > 300 and ((box[2] - box[0]) > 4 * max(1, box[3] - box[1]) or (box[3] - box[1]) > 4 * max(1, box[2] - box[0]) or n > 2500)
    helmet_band = lambda x, y, w, h: y < 0.3 * h and x < 0.83 * w
    sets = [("bullfrogs/2017/mat_jersey_2017_yel_jersey_col.png", "T_TAG_Hero_JerseyMask_bullfrogs", 512, 1024,
             lambda x, y, w, h: 0.05 * w < x < 0.95 * w, lambda ch, n, box, w, h: n > 400 and box[1] < 0.62 * h),
            ("bullfrogs/2017/mat_pants_2017_yelgrn_pants_col.png", "T_TAG_Hero_PantsMask_bullfrogs", 512, 512, None,
             lambda ch, n, box, w, h: n > 800 and (box[3] - box[1]) > 4 * max(1, box[2] - box[0]))]
    for f in ("bullfrogs/2017/mat_helmet_2017_yelgrn_helmet_col.png", "cowboys/2010/cow_helmet_2010_sil_helmet_col.png",
              "cowboys/2018/practice/cow_helmet_2018_whi_helmet_col.png", "dolphins/2013/dol_helmet_2013_whi_helmet_col.png",
              "dolphins/2015/dol_helmet_2015_white_helmet_col.png", "teamlongshot/2017/tls_helmet_2017_blu_helmet_col.png"):
        tag = f.split("/")[0] + f.split("/")[1]
        sets.append((f, "T_TAG_Hero_HelmetMask_" + tag, 1024, 512, helmet_band,
                     lambda ch, n, box, w, h: (box[2] - box[0]) > 6 * max(1, box[3] - box[1]) and box[1] < 0.3 * h and box[3] < 0.3 * h))
    shell = lambda x, y, w, h: 0.32 * h < y < 0.9 * h and x < 0.8 * w  # the shell's own colour (the stripes are on it)
    for f, name, w, h, region, keep in sets:
        cl, base = generic(f, w, h, region or (lambda x, y, w, h: True), shell if "Helmet" in name else None)
        build(f, name + ".png", w, h, cl, keep, region)
    # helmet shell (2048 x 1024): red edge bands and the white centre band of the stripe; the round logos go
    build("nin_helmet_2018_golalt_helmet_col.png", "T_TAG_Hero_HelmetMask.png", 1024, 512,
          lambda p: 1 if red(p) else 2 if (p[0] > 200 and p[1] > 200 and p[2] > 200) else 0,
          lambda ch, n, box, w, h: (box[2] - box[0]) > 6 * max(1, box[3] - box[1]) and box[1] < 0.3 * h and box[2] < 0.86 * w)


if __name__ == "__main__":
    main()
