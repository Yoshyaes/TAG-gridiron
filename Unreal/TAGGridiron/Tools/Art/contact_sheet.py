# Tiles a series of screenshots into one contact sheet (look-dev review of motion: -TagMoveTest writes tag_move_r<N>_<k>.png).
# usage: python contact_sheet.py <out.png> <cols> <crop: x0,y0,x1,y1 in 0..1 or "full"> <png> [<png> ...]
import sys

from PIL import Image, ImageDraw

out, cols, crop, files = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4:]
tiles = []
for f in files:
    im = Image.open(f).convert("RGB")
    if crop != "full":
        x0, y0, x1, y1 = (float(v) for v in crop.split(","))
        w, h = im.size
        im = im.crop((int(x0 * w), int(y0 * h), int(x1 * w), int(y1 * h)))
    im.thumbnail((360, 360))
    d = ImageDraw.Draw(im)
    d.text((6, 4), f.replace("\\", "/").split("/")[-1].rsplit(".", 1)[0][-2:], fill=(255, 255, 0))
    tiles.append(im)
if not tiles:
    sys.exit("no images")
tw, th = tiles[0].size
rows = (len(tiles) + cols - 1) // cols
sheet = Image.new("RGB", (cols * tw, rows * th), (20, 20, 20))
for i, t in enumerate(tiles):
    sheet.paste(t, ((i % cols) * tw, (i // cols) * th))
sheet.save(out)
print(out, sheet.size)
