# contact sheet: sheet.py <frames dir> <clip prefix e.g. c001> <from> <to> <n> <out.png> [cols] [scale]
import sys, os
from PIL import Image, ImageDraw
d, pre, a, b, n, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), sys.argv[6]
cols = int(sys.argv[7]) if len(sys.argv) > 7 else 4
sc = float(sys.argv[8]) if len(sys.argv) > 8 else 0.5
idx = [a + round(i * (b - a) / max(1, n - 1)) for i in range(n)]
ims = []
for i in idx:
    p = os.path.join(d, "%s_%05d.png" % (pre, i))
    if not os.path.exists(p): continue
    im = Image.open(p).convert("RGB")
    im = im.resize((int(im.width * sc), int(im.height * sc)), Image.LANCZOS)
    ImageDraw.Draw(im).text((4, 4), str(i), fill=(255, 255, 0))
    ims.append(im)
if not ims: sys.exit("no frames")
w, h = ims[0].size
rows = (len(ims) + cols - 1) // cols
S = Image.new("RGB", (w * cols, h * rows))
for k, im in enumerate(ims): S.paste(im, ((k % cols) * w, (k // cols) * h))
S.save(out)
print(out, S.size)
