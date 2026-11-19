# The Superstar and X-Factor ability icons (Sim/Abilities.cpp): one picture per ability, drawn here from shapes so the
# set reads as one family and needs no outside art.
#   Superstar: a hexagon in its role's colour (QB blue, ball carrier green, receiver teal, blocker steel, front seven
#              crimson, coverage violet, kicker bronze) with a gold rim and a white mark.
#   X-Factor:  a spiked badge in the flame orange the game paints X-Factors in, a bright rim, a white mark.
# The game shows them at 18-40 px, so every mark is one bold shape (a football, a hand, an anchor, a snowflake...).
#   python gen_ability_icons.py          -> Content/Slate/AbilityIcons/<key>.png (128 x 128, sRGB, straight alpha)
#   python gen_ability_icons.py --sheet=out.png  -> also a contact sheet (every icon at 128 and at 24 px)
import math
import os
import re
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
CATALOG = os.path.join(ROOT, "Source", "TAGGridiron", "Sim", "Abilities.cpp")
OUT = os.path.join(ROOT, "Content", "Slate", "AbilityIcons")
S = 512          # drawn at 4x, then scaled down (the anti-aliasing)
FINAL = 128
GLYPH0, GLYPH = 0.22, 0.56   # the mark's box (fraction of the icon): marks are drawn in 0..100 inside it


def hx(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


ROLE = {  # Superstar fill (top, bottom)
    "qb": ("#2C5BA8", "#132A55"),
    "carry": ("#2E8A57", "#123D27"),
    "recv": ("#1C8C93", "#0C3E44"),
    "block": ("#68707F", "#2A2E38"),
    "front": ("#A3303F", "#4A1119"),
    "cover": ("#6E44B0", "#2C1652"),
    "kick": ("#9A7A2E", "#43330E"),
}
GOLD = hx("#FFD21F")
FLAME = ("#FF9A2E", "#D2340C")


# ---------------------------------------------------------------------------------------------------------------- pen
class Pen:
    """Draws into the mark's mask (255 = mark, 0 = cut back out) in 0..100 units, optionally moved / turned / scaled."""

    def __init__(self, d):
        self.d = d
        self.ox, self.oy, self.a, self.k, self.fx = 0.0, 0.0, 0.0, 1.0, 1.0

    def at(self, ox=0.0, oy=0.0, a=0.0, k=1.0, fx=1.0):
        p = Pen(self.d)
        p.ox, p.oy, p.a, p.k, p.fx = ox, oy, math.radians(a), k, fx
        return p

    def P(self, x, y):
        x *= self.fx
        c, s = math.cos(self.a), math.sin(self.a)
        u, v = self.ox + (x * c - y * s) * self.k, self.oy + (x * s + y * c) * self.k
        return (S * GLYPH0 + u * S * GLYPH / 100.0, S * GLYPH0 + v * S * GLYPH / 100.0)

    def W(self, w):
        return max(1, int(round(w * self.k * S * GLYPH / 100.0)))

    def poly(self, pts, v=255):
        self.d.polygon([self.P(x, y) for x, y in pts], fill=v)

    def circ(self, x, y, r, v=255):
        cx, cy = self.P(x, y)
        rr = r * self.k * S * GLYPH / 100.0
        self.d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=v)

    def ring(self, x, y, r, w, v=255):
        cx, cy = self.P(x, y)
        rr = r * self.k * S * GLYPH / 100.0
        self.d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], outline=v, width=self.W(w))

    def line(self, pts, w, v=255, caps=True):
        q = [self.P(x, y) for x, y in pts]
        self.d.line(q, fill=v, width=self.W(w), joint="curve")
        if caps:
            for x, y in (pts[0], pts[-1]):
                self.circ(x, y, w / 2.0, v)

    def arcpts(self, x, y, r, a0, a1, n=48, ry=None):
        ry = r if ry is None else ry
        return [(x + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)), y + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
                for i in range(n + 1)]

    def arc(self, x, y, r, a0, a1, w, v=255, caps=True, ry=None):
        self.line(self.arcpts(x, y, r, a0, a1, ry=ry), w, v, caps)

    def head(self, tip, frm, size, v=255, wide=0.62):
        """An arrowhead with its tip at tip, pointing away from frm."""
        dx, dy = tip[0] - frm[0], tip[1] - frm[1]
        L = math.hypot(dx, dy) or 1.0
        ux, uy = dx / L, dy / L
        bx, by = tip[0] - ux * size, tip[1] - uy * size
        self.poly([tip, (bx - uy * size * wide, by + ux * size * wide), (bx + uy * size * wide, by - ux * size * wide)], v)

    def arrow(self, pts, w, size=None, v=255):
        size = size or w * 2.4
        tip, frm = pts[-1], pts[-2]
        dx, dy = tip[0] - frm[0], tip[1] - frm[1]
        L = math.hypot(dx, dy) or 1.0
        stop = (tip[0] - dx / L * size * 0.8, tip[1] - dy / L * size * 0.8)
        self.line(pts[:-1] + [stop], w, v)
        self.head(tip, frm, size, v)

    def dashed(self, pts, w, on=7.0, off=6.0, v=255):
        seg, acc, draw = [pts[0]], 0.0, True
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            L = math.hypot(x1 - x0, y1 - y0)
            t = 0.0
            while t < L:
                left = (on if draw else off) - acc
                step = min(left, L - t)
                t += step
                acc += step
                p = (x0 + (x1 - x0) * t / L, y0 + (y1 - y0) * t / L)
                if draw:
                    seg.append(p)
                if acc >= (on if draw else off) - 1e-6:
                    if draw and len(seg) > 1:
                        self.line(seg, w, v)
                    draw, acc, seg = not draw, 0.0, [p]
        if draw and len(seg) > 1:
            self.line(seg, w, v)

    def rrect(self, x0, y0, x1, y1, r, v=255):
        self.poly([(x0 + r, y0), (x1 - r, y0), (x1, y0 + r), (x1, y1 - r), (x1 - r, y1), (x0 + r, y1), (x0, y1 - r), (x0, y0 + r)], v)
        for cx, cy in ((x0 + r, y0 + r), (x1 - r, y0 + r), (x1 - r, y1 - r), (x0 + r, y1 - r)):
            self.circ(cx, cy, r, v)
        self.poly([(x0 + r, y0), (x1 - r, y0), (x1 - r, y1), (x0 + r, y1)], v)
        self.poly([(x0, y0 + r), (x1, y0 + r), (x1, y1 - r), (x0, y1 - r)], v)


# ------------------------------------------------------------------------------------------------------ shared shapes
def football(p, x, y, L, a=0.0, laces=True, v=255):
    c, s = math.cos(math.radians(a)), math.sin(math.radians(a))
    H = L * 0.6
    pts = []
    for i in range(41):
        t = -1 + 2 * i / 40
        pts.append((t * L / 2, -(H / 2) * (1 - t * t) ** 0.75))
    for i in range(41):
        t = 1 - 2 * i / 40
        pts.append((t * L / 2, (H / 2) * (1 - t * t) ** 0.75))
    R = lambda u, w: (x + u * c - w * s, y + u * s + w * c)
    p.poly([R(u, w) for u, w in pts], v)
    if laces:
        cut = 0 if v else 255
        lw = L * 0.06
        p.line([R(-L * 0.2, 0), R(L * 0.2, 0)], lw, cut)
        for u in (-0.12, -0.04, 0.04, 0.12):
            p.line([R(u * L, -L * 0.075), R(u * L, L * 0.075)], lw * 0.8, cut)


def hand(p, x, y, k=1.0, a=0.0, fx=1.0, v=255, spread=1.0):
    """An open hand, palm toward us, fingers up (local: palm 0..36 wide, -54..18 tall)."""
    q = p.at(x, y, a, k, fx)
    q.rrect(-18, -14, 18, 18, 9, v)
    for i, (dx, top) in enumerate(((-13.5, -44), (-4.5, -52), (4.5, -50), (13.0, -40))):
        sx = dx * (1 + (spread - 1) * 0.6)
        q.line([(dx, -10), (sx + (sx - dx) * 0.6, top)], 8.2, v)
    q.line([(-14, 8), (-30, -10)], 9, v)


def shieldpts(x, y, w, h):
    pts = [(x - w / 2, y - h / 2), (x + w / 2, y - h / 2), (x + w / 2, y - h * 0.05)]
    for i in range(1, 16):
        t = i / 16
        pts.append((x + w / 2 * (1 - t) ** 1.2, y - h * 0.05 + h * 0.55 * math.sin(t * math.pi / 2)))
    pts.append((x, y + h / 2))
    for i in range(15, 0, -1):
        t = i / 16
        pts.append((x - w / 2 * (1 - t) ** 1.2, y - h * 0.05 + h * 0.55 * math.sin(t * math.pi / 2)))
    pts.append((x - w / 2, y - h * 0.05))
    return pts


def goalpost(p, x, y, k=1.0, w=6.0):
    q = p.at(x, y, 0, k)
    q.line([(0, 40), (0, 4)], w)
    q.line([(-26, 4), (26, 4)], w)
    q.line([(-26, 4), (-26, -40)], w)
    q.line([(26, 4), (26, -40)], w)


def star(p, x, y, r0, r1, n=5, a=-90.0, v=255):
    pts = []
    for i in range(n * 2):
        r = r0 if i % 2 == 0 else r1
        t = math.radians(a + i * 180.0 / n)
        pts.append((x + r * math.cos(t), y + r * math.sin(t)))
    p.poly(pts, v)


def burst(p, x, y, r0, r1, n=10, a=-90.0, v=255):
    star(p, x, y, r0, r1, n, a, v)


def bolt(p, x, y, k=1.0, a=0.0, v=255):
    p.at(x, y, a, k).poly([(6, -48), (-22, 6), (-2, 6), (-10, 48), (22, -10), (2, -10), (14, -48)], v)


def cog(p, x, y, r, teeth=8, v=255):
    pts = []
    for i in range(teeth * 4):
        t = math.radians(i * 360.0 / (teeth * 4) + 360.0 / (teeth * 8))
        rr = r if (i % 4) in (0, 1) else r * 0.78
        pts.append((x + rr * math.cos(t), y + rr * math.sin(t)))
    p.poly(pts, v)


def snowflake(p, x, y, r, w):
    for i in range(6):
        t = math.radians(i * 60 - 90)
        ex, ey = x + r * math.cos(t), y + r * math.sin(t)
        p.line([(x, y), (ex, ey)], w)
        for f in (0.55, 0.8):
            bx, by = x + r * f * math.cos(t), y + r * f * math.sin(t)
            L = r * (0.32 if f < 0.7 else 0.2)
            for side in (-1, 1):
                tt = t + side * math.radians(45)
                p.line([(bx, by), (bx + L * math.cos(tt), by + L * math.sin(tt))], w * 0.8)


def padlock(p, x, y, k=1.0, v=255):
    q = p.at(x, y, 0, k)
    q.arc(0, -10, 20, 180, 360, 9, v)
    q.line([(-20, -10), (-20, 4)], 9, v)
    q.line([(20, -10), (20, 4)], 9, v)
    q.rrect(-32, 0, 32, 46, 7, v)
    q.circ(0, 18, 6, 0)
    q.poly([(-3.5, 18), (3.5, 18), (5, 34), (-5, 34)], 0)


# --------------------------------------------------------------------------------------------------------- the marks
def m_pocket_poise(p):
    football(p, 50, 50, 46, -25)
    for fx in (1, -1):
        q = p.at(50, 50, 0, 1, fx)
        q.line([(-48, -30), (-34, -16)], 6)
        q.head((-30, -12), (-48, -30), 12)
        q.line([(-48, 30), (-34, 16)], 6)
        q.head((-30, 12), (-48, 30), 12)

def m_laser_line(p):
    football(p, 30, 50, 38, 0)
    for yy, x0 in ((40, -2), (50, -6), (60, -2)):
        p.line([(x0, yy), (x0 + 10, yy)], 4.5)
    p.line([(54, 50), (82, 50)], 6)
    p.ring(90, 50, 9, 4.5)
    p.circ(90, 50, 3.5)


def m_moonball_touch(p):
    p.dashed(p.arcpts(48, 84, 40, 196, 330, 40), 5)
    football(p, 46, 30, 32, 10)
    p.line([(70, 74), (74, 92), (92, 92), (96, 74)], 6)

def m_off_script(p):
    p.arrow([(12, 88), (14, 70), (28, 58), (44, 64), (56, 72), (70, 64), (78, 46), (82, 24)], 7, 18)
    football(p, 30, 26, 32, -35)


def m_film_junkie(p):
    p.rrect(4, 18, 96, 82, 8)
    for xx in range(12, 96, 14):
        p.rrect(xx - 4, 23, xx + 4, 31, 1.5, 0)
        p.rrect(xx - 4, 69, xx + 4, 77, 1.5, 0)
    p.poly([(38, 36), (38, 64), (64, 50)], 0)


def m_two_minute_maestro(p):
    p.ring(50, 56, 36, 8)
    p.rrect(42, 4, 58, 14, 3)
    p.line([(50, 12), (50, 20)], 7)
    p.line([(78, 22), (84, 16)], 7)
    p.line([(50, 56), (50, 32)], 7)
    p.line([(50, 56), (66, 66)], 7)
    p.circ(50, 56, 6)
    p.poly([(50, 56)] + p.arcpts(50, 56, 30, -90, -30, 12), 255)


def m_escape_hatch(p):
    p.line([(10, 92), (10, 8), (56, 8), (56, 92)], 7, caps=False)
    p.poly([(10, 8), (40, 18), (40, 96), (10, 92)])
    p.circ(33, 54, 3.5, 0)
    p.arrow([(52, 54), (98, 54)], 8, 20)


def m_battering_ram(p):
    for (x0, y0) in ((60, 16), (60, 52), (78, 34), (78, 70)):
        p.rrect(x0, y0, x0 + 16, y0 + 32, 2)
    p.poly([(56, 36), (66, 44), (58, 50), (68, 58), (56, 66)], 0)
    p.poly([(2, 38), (36, 38), (36, 24), (62, 50), (36, 76), (36, 62), (2, 62)])


def m_ankle_taker(p):
    p.arrow([(8, 90), (36, 56), (20, 44), (60, 20), (48, 6), (92, 6)], 7, 18)
    p.line([(54, 84), (96, 84)], 6)
    p.line([(60, 72), (66, 78)], 5)
    p.line([(78, 66), (78, 76)], 5)
    p.line([(94, 72), (88, 78)], 5)


def m_vise_grip(p):
    football(p, 50, 56, 50, -90)
    p.rrect(6, 28, 22, 86, 3)
    p.rrect(78, 28, 94, 86, 3)
    p.line([(22, 46), (34, 46)], 5, caps=False)
    p.line([(66, 46), (78, 46)], 5, caps=False)
    p.line([(14, 28), (14, 12), (86, 12), (86, 28)], 6)


def m_home_run_hitter(p):
    for x in (8, 38, 68):
        p.line([(x, 18), (x + 26, 50), (x, 82)], 11)


def m_second_effort(p):
    for x, y in ((22, 80), (46, 80), (70, 80), (34, 60), (58, 60)):
        p.circ(x, y, 11)
    p.arrow([(14, 44), (40, 28), (72, 22), (96, 26)], 8, 18)


def m_receiving_back(p):
    p.arrow([(14, 92), (16, 66), (28, 46), (52, 34), (62, 22), (66, 6)], 7, 18)
    football(p, 70, 64, 34, -35)


def m_glue_hands(p):
    football(p, 50, 30, 40, 0)
    hand(p, 30, 72, 0.62, 25)
    hand(p, 70, 72, 0.62, -25, fx=-1)


def m_jump_ball_ace(p):
    football(p, 50, 16, 34, 0)
    hand(p, 50, 72, 0.78, 0, spread=1.25)
    p.arrow([(12, 96), (12, 46)], 6, 14)
    p.arrow([(88, 96), (88, 46)], 6, 14)


def m_route_artist(p):
    p.dashed([(26, 50), (26, 4)], 5)
    p.line([(26, 96), (26, 50)], 8)
    p.arrow([(26, 50), (52, 32), (92, 32)], 8, 18)
    p.circ(26, 50, 7)


def m_jam_breaker(p):
    # a chain link snapped in two
    for fx in (1, -1):
        q = p.at(50, 50, 0, 1, fx)
        q.arc(-26, 0, 18, 70, 290, 9)
        q.line([(-20, -17), (-8, -17)], 9)
        q.line([(-20, 17), (-8, 17)], 9)
    for a in (-90, -55, 55, 90):
        t = math.radians(a)
        p.line([(50 + 10 * math.cos(t), 50 + 10 * math.sin(t) * 2.2), (50 + 16 * math.cos(t), 50 + 16 * math.sin(t) * 2.4)], 4.5)

def m_catch_and_go(p):
    football(p, 34, 50, 42, -20)
    p.arrow([(62, 50), (98, 50)], 8, 18)
    p.line([(60, 30), (82, 30)], 5)
    p.line([(60, 70), (82, 70)], 5)


def m_red_zone_threat(p):
    p.ring(50, 50, 40, 6)
    p.ring(50, 50, 22, 6)
    p.circ(50, 50, 8)
    for a in (0, 90, 180, 270):
        t = math.radians(a)
        p.line([(50 + 32 * math.cos(t), 50 + 32 * math.sin(t)), (50 + 50 * math.cos(t), 50 + 50 * math.sin(t))], 6, caps=False)


def m_chain_mover(p):
    for x in (20, 50, 80):
        q = p.at(x, 66, 0)
        q.rrect(-18, -10, 18, 10, 10)
        q.rrect(-11, -3.5, 11, 3.5, 3.5, 0)
    p.line([(10, 8), (10, 50)], 6)
    p.arrow([(24, 26), (90, 26)], 8, 18)


def m_brick_wall(p):
    rows = [(10, 28), (32, 50), (54, 72), (76, 94)]
    for i, (y0, y1) in enumerate(rows):
        xs = [4, 36, 68, 100] if i % 2 == 0 else [4, 20, 52, 84, 100]
        for a, b in zip(xs, xs[1:]):
            p.rrect(a + 1.5, y0, b - 1.5, y1, 2)


def m_road_grader(p):
    # a bulldozer, blade first
    p.rrect(16, 66, 82, 90, 12)
    for x in (28, 44, 60, 72):
        p.circ(x, 78, 5, 0)
    p.rrect(24, 40, 70, 64, 3)
    p.rrect(30, 18, 54, 42, 3)
    p.rrect(35, 23, 49, 33, 1.5, 0)
    p.line([(70, 52), (84, 52)], 6)
    p.poly([(84, 26), (96, 30), (100, 92), (84, 92)])

def m_anchor(p):
    p.ring(50, 14, 9, 6)
    p.line([(50, 23), (50, 88)], 8)
    p.line([(30, 34), (70, 34)], 7)
    p.arc(50, 52, 38, 20, 160, 8)
    p.head((14, 54), (22, 74), 13)
    p.head((86, 54), (78, 74), 13)


def m_quick_feet(p):
    # two footprints mid-shuffle
    for (x, y, a) in ((38, 64, -10), (66, 30, 10)):
        q = p.at(x, y, a)
        q.poly(q.arcpts(0, -8, 11, 0, 360, 30, ry=17))
        q.poly(q.arcpts(0, 20, 8, 0, 360, 24, ry=9))
    p.line([(4, 60), (20, 60)], 4.5)
    p.line([(8, 76), (22, 76)], 4.5)
    p.line([(32, 26), (48, 26)], 4.5)
    p.line([(36, 42), (50, 42)], 4.5)

def m_climber(p):
    steps = [(6, 94), (6, 72), (30, 72), (30, 50), (54, 50), (54, 28), (78, 28), (78, 94)]
    p.poly(steps)
    p.arrow([(14, 56), (40, 32), (66, 10)], 6, 14)


def m_safety_valve(p):
    p.ring(50, 50, 36, 9)
    p.circ(50, 50, 10)
    for a in (0, 120, 240):
        t = math.radians(a - 90)
        p.line([(50, 50), (50 + 34 * math.cos(t), 50 + 34 * math.sin(t))], 7)
    for a in range(0, 360, 45):
        t = math.radians(a)
        p.circ(50 + 40 * math.cos(t), 50 + 40 * math.sin(t), 4.5)


def m_edge_bender(p):
    p.rrect(38, 44, 62, 96, 4)
    p.arrow([(10, 96), (10, 54)] + p.arcpts(44, 44, 34, 180, 330, 30)[1:], 8, 18)


def m_bull_charge(p):
    p.poly(p.arcpts(50, 56, 22, 0, 180, 20, ry=28) + [(28, 56), (72, 56)])
    p.circ(50, 50, 22)
    for fx in (1, -1):
        q = p.at(50, 0, 0, 1, fx)
        q.poly([(18, 40), (36, 30), (44, 14), (42, 2), (50, 14), (50, 30), (38, 46), (22, 52)])
    p.circ(42, 66, 3, 0)
    p.circ(58, 66, 3, 0)


def m_gap_plugger(p):
    p.rrect(4, 10, 30, 90, 3)
    p.rrect(70, 10, 96, 90, 3)
    p.poly([(36, 36), (64, 36), (60, 90), (40, 90)])
    p.arrow([(50, 2), (50, 28)], 6, 12)


def m_motor(p):
    cog(p, 50, 50, 42, 9)
    p.circ(50, 50, 16, 0)
    p.circ(50, 50, 7)


def m_finisher(p):
    p.arrow([(50, 2), (50, 62)], 12, 26)
    p.line([(8, 82), (92, 82)], 8)
    for a, L in ((200, 14), (230, 12), (310, 12), (340, 14)):
        t = math.radians(a)
        p.line([(50 + 26 * math.cos(t), 76 + 26 * math.sin(t)), (50 + (26 + L) * math.cos(t), 76 + (26 + L) * math.sin(t))], 5)


def m_get_off(p):
    bolt(p, 56, 50, 1.0)
    p.line([(6, 30), (26, 30)], 5)
    p.line([(2, 50), (24, 50)], 5)
    p.line([(6, 70), (22, 70)], 5)


def m_sure_tackler(p):
    p.ring(50, 50, 40, 8)
    p.line([(28, 52), (44, 68), (74, 32)], 11)


def m_ball_punch(p):
    burst(p, 62, 52, 44, 26, 9)
    football(p, 62, 52, 40, -30, v=0)
    football(p, 62, 52, 30, -30)
    p.rrect(2, 36, 30, 66, 8)
    for y in (40, 48, 56):
        p.line([(26, y), (34, y)], 6)


def m_thumper(p):
    q = p.at(50, 50, -35)
    q.rrect(-36, -46, 36, -14, 5)
    q.rrect(-6, -16, 6, 46, 3)
    p.line([(78, 82), (96, 82)], 5)


def m_sideline_to_sideline(p):
    p.line([(6, 8), (6, 92)], 6)
    p.line([(94, 8), (94, 92)], 6)
    p.line([(30, 50), (70, 50)], 8)
    p.head((18, 50), (40, 50), 18)
    p.head((82, 50), (60, 50), 18)


def m_diagnoser(p):
    p.ring(40, 40, 28, 9)
    p.line([(60, 60), (90, 90)], 13)
    p.arc(40, 40, 16, 200, 260, 5)


def m_blanket(p):
    for dy in (-16, 16):
        pts = [(6, 50 + dy), (26, 34 + dy), (46, 50 + dy), (66, 34 + dy), (86, 50 + dy)]
        p.line(pts, 7)
    p.head((100, 34 - 16), (86, 50 - 16), 15)
    p.head((100, 34 + 16), (86, 50 + 16), 15)


def m_zone_reader(p):
    eye = p.arcpts(50, 82, 60, 222, 318, 30) + p.arcpts(50, 18, 60, 42, 138, 30)
    p.poly(eye)
    p.circ(50, 50, 18, 0)
    p.circ(50, 50, 11)
    p.circ(55, 45, 3.5, 0)


def m_pick_artist(p):
    football(p, 34, 34, 40, 30)
    p.arrow([(90, 20), (90, 62)] + p.arcpts(60, 62, 30, 0, 180, 24)[1:] + [(30, 40)], 7, 15)


def m_swat_team(p):
    hand(p, 40, 68, 0.78, -30)
    football(p, 82, 16, 26, 30)
    for x0, y0 in ((60, 52), (68, 66), (72, 82)):
        p.line([(x0, y0), (x0 + 16, y0 - 4)], 4.5)


def m_press_master(p):
    hand(p, 36, 62, 0.86, 90)
    for y in (30, 50, 70):
        p.line([(80, y), (98, y)], 5)


def m_deep_range(p):
    p.arc(50, 82, 44, 180, 360, 6)
    p.arc(50, 82, 26, 180, 360, 6)
    p.poly([(50, 82)] + p.arcpts(50, 82, 44, 220, 260, 12))
    p.circ(50, 82, 7)
    p.line([(4, 92), (96, 92)], 5)


def m_closing_speed(p):
    p.arrow([(4, 18), (38, 44)], 7, 16)
    p.arrow([(4, 82), (38, 56)], 7, 16)
    p.circ(62, 50, 13)
    p.line([(82, 38), (96, 38)], 5)
    p.line([(84, 50), (100, 50)], 5)
    p.line([(82, 62), (96, 62)], 5)


def m_big_leg(p):
    p.poly([(20, 4), (40, 4), (40, 58), (64, 66), (68, 80), (18, 80)])
    p.line([(16, 88), (70, 88)], 5)
    p.dashed(p.arcpts(100, 70, 34, 200, 260, 16), 5)
    football(p, 84, 22, 26, -40)


def m_steady_foot(p):
    p.line([(18, 4), (18, 96)], 5)
    p.line([(82, 4), (82, 96)], 5)
    football(p, 50, 64, 34, -90)
    p.arrow([(50, 40), (50, 2)], 7, 16)


def m_ice_water(p):
    p.poly([(50, 2)] + p.arcpts(50, 62, 34, -30, 210, 40))
    for i in range(3):
        t = math.radians(i * 60)
        p.line([(50 - 16 * math.cos(t), 62 - 16 * math.sin(t)), (50 + 16 * math.cos(t), 62 + 16 * math.sin(t))], 5, 0)


def m_pin_point(p):
    p.poly([(50, 96)] + p.arcpts(50, 36, 32, 155, 385, 40))
    p.circ(50, 36, 12, 0)


# ---- X-Factors
def m_ice_veins(p):
    snowflake(p, 50, 50, 44, 7)
    p.circ(50, 50, 9)


def m_rocket_arm(p):
    q = p.at(50, 50, 45, 1.15)
    q.poly([(0, -46), (14, -24), (14, 18), (-14, 18), (-14, -24)])
    q.poly([(-14, 0), (-28, 24), (-14, 18)])
    q.poly([(14, 0), (28, 24), (14, 18)])
    q.circ(0, -12, 6, 0)
    q.poly([(-9, 22), (9, 22), (0, 44)])

def m_juggernaut(p):
    p.poly([(4, 24), (58, 24), (58, 6), (98, 50), (58, 94), (58, 76), (4, 76)])
    for y in (40, 60):
        p.line([(4, y), (40, y)], 5, 0, caps=False)


def m_phantom(p):
    pts = p.arcpts(50, 40, 34, 180, 360, 30)
    pts += [(84, 92), (73, 82), (62, 92), (50, 82), (38, 92), (27, 82), (16, 92)]
    p.poly(pts)
    p.circ(38, 40, 7, 0)
    p.circ(62, 40, 7, 0)


def m_spotlight(p):
    p.poly([(34, 6), (66, 6), (98, 92), (2, 92)], 110)
    p.rrect(32, 0, 68, 14, 3)
    football(p, 50, 64, 40, 0)


def m_slipstream(p):
    for i, y in enumerate((24, 50, 76)):
        x0 = 4 + (i % 2) * 14
        p.line([(x0, y), (x0 + 60, y)], 7)
        p.arc(x0 + 60, y - 10, 10, 90, 330, 7)


def m_fortress(p):
    p.poly([(10, 96), (10, 24), (24, 24), (24, 10), (38, 10), (38, 24), (62, 24), (62, 10), (76, 10), (76, 24), (90, 24), (90, 96)])
    p.poly([(40, 96), (40, 70)] + p.arcpts(50, 70, 10, 180, 360, 10) + [(60, 96)], 0)


def m_havoc(p):
    burst(p, 50, 50, 48, 22, 8, -90)
    burst(p, 50, 50, 22, 10, 8, -67.5, 0)


def m_black_hole(p):
    for k in range(3):
        pts = []
        for i in range(60):
            t = i / 59
            r = 6 + 40 * t
            a = math.radians(k * 120 + t * 300)
            pts.append((50 + r * math.cos(a), 50 + r * math.sin(a)))
        p.line(pts, 7)
    p.circ(50, 50, 10)


def m_enforcer(p):
    # a helmet, side view, facing right
    p.poly(p.arcpts(44, 52, 38, 170, 360, 40) + [(82, 66), (60, 70), (52, 88), (18, 88), (8, 64)])
    p.circ(30, 56, 7, 0)
    p.line([(70, 52), (94, 52), (94, 82), (70, 82)], 5, caps=True)
    p.line([(82, 52), (82, 82)], 4)


def m_lockdown(p):
    padlock(p, 50, 50, 1.0)


def m_last_line(p):
    p.poly(shieldpts(50, 50, 76, 92))
    p.poly(shieldpts(50, 50, 56, 72), 0)
    p.line([(50, 20), (50, 76)], 8)
    p.line([(26, 40), (74, 40)], 8)


def m_automatic(p):
    goalpost(p, 50, 52, 1.05, 7)
    football(p, 50, 18, 26, -90)


MARKS = {k[2:]: v for k, v in globals().items() if k.startswith("m_")}

GROUP = {}
for g, keys in {
    "qb": "pocket_poise laser_line moonball_touch off_script film_junkie two_minute_maestro escape_hatch",
    "carry": "battering_ram ankle_taker vise_grip home_run_hitter second_effort receiving_back",
    "recv": "glue_hands jump_ball_ace route_artist jam_breaker catch_and_go red_zone_threat chain_mover",
    "block": "brick_wall road_grader anchor quick_feet climber safety_valve",
    "front": "edge_bender bull_charge gap_plugger motor finisher get_off",
    "cover": "sure_tackler ball_punch thumper sideline_to_sideline diagnoser blanket zone_reader pick_artist swat_team "
             "press_master deep_range closing_speed",
    "kick": "big_leg steady_foot ice_water pin_point",
}.items():
    for k in keys.split():
        GROUP[k] = g


# ----------------------------------------------------------------------------------------------------------- frames
def gradient(top, bottom):
    t, b = hx(top), hx(bottom)
    g = Image.linear_gradient("L").resize((S, S))
    return Image.merge("RGB", [Image.eval(g, lambda v, i=i: int(t[i] + (b[i] - t[i]) * v / 255)) for i in range(3)])


def hexpts(r, cx=S / 2, cy=S / 2):
    return [(cx + r * math.cos(math.radians(60 * i - 90)), cy + r * math.sin(math.radians(60 * i - 90))) for i in range(6)]


def spikepts(r):
    """The X-Factor's badge: an octagon with a point at each corner."""
    pts = []
    for i in range(16):
        a = math.radians(i * 22.5 - 90)
        rr = r if i % 2 == 0 else r * 0.86
        pts.append((S / 2 + rr * math.cos(a), S / 2 + rr * math.sin(a)))
    return pts


def mask_of(pts):
    m = Image.new("L", (S, S), 0)
    ImageDraw.Draw(m).polygon(pts, fill=255)
    return m


def icon(key, xfactor):
    glyph = Image.new("L", (S, S), 0)
    MARKS[key](Pen(ImageDraw.Draw(glyph)))
    if xfactor:
        outer = mask_of(spikepts(S * 0.49))
        inner = mask_of(spikepts(S * 0.43))
        fill = gradient(*FLAME)
        rim = (255, 226, 120)
        dark = (90, 18, 0)
    else:
        outer = mask_of(hexpts(S * 0.49))
        inner = mask_of(hexpts(S * 0.42))
        fill = gradient(*ROLE[GROUP[key]])
        rim = GOLD
        dark = (6, 10, 20)
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    img.paste(Image.new("RGB", (S, S), rim), (0, 0), outer)
    img.paste(fill, (0, 0), inner)
    # a sheen over the top half of the face
    sheen = Image.new("L", (S, S), 0)
    ImageDraw.Draw(sheen).ellipse([-S * 0.3, -S * 0.75, S * 1.3, S * 0.48], fill=34)
    img.paste(Image.new("RGB", (S, S), (255, 255, 255)), (0, 0), ImageChops.multiply(sheen, inner))
    # the mark: a soft dark shadow, then white
    glyph = ImageChops.multiply(glyph, inner)
    shadow = glyph.filter(ImageFilter.GaussianBlur(S * 0.012))
    shadow = ImageChops.offset(shadow, int(S * 0.006), int(S * 0.012))
    shadow = ImageChops.multiply(shadow, inner).point(lambda v: v * 0.75)
    img.paste(Image.new("RGB", (S, S), dark), (0, 0), shadow)
    img.paste(Image.new("RGB", (S, S), (255, 255, 255)), (0, 0), glyph)
    img.putalpha(outer)
    return img.resize((FINAL, FINAL), Image.LANCZOS)


def catalog():
    """(key, X-Factor?) in catalog order, from Abilities.cpp."""
    with open(CATALOG, encoding="utf-8") as f:
        src = f.read()
    return [(k, kind == "xf") for kind, k in re.findall(r'v\.push_back\((sup|xf)\(A::\w+,\s*"([a-z_]+)"', src)]


def main():
    os.makedirs(OUT, exist_ok=True)
    cat = catalog()
    missing = [k for k, xf in cat if k not in MARKS or (not xf and k not in GROUP)]
    if missing:
        sys.exit("no mark (or no role colour) for: " + ", ".join(missing))
    icons = []
    for k, xf in cat:
        im = icon(k, xf)
        im.save(os.path.join(OUT, k + ".png"))
        icons.append((k, im))
    print("wrote", len(icons), "icons to", os.path.normpath(OUT))
    sheet_path = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--sheet=")), None)
    if sheet_path:
        cols = 8
        rows = (len(icons) + cols - 1) // cols
        cell = FINAL + 16
        sheet = Image.new("RGB", (cols * cell, rows * (cell + 34)), (14, 17, 24))
        for i, (k, im) in enumerate(icons):
            x, y = (i % cols) * cell + 8, (i // cols) * (cell + 34) + 8
            sheet.paste(im, (x, y), im)
            small = im.resize((24, 24), Image.LANCZOS)
            sheet.paste(small, (x, y + FINAL + 4), small)
            ImageDraw.Draw(sheet).text((x + 30, y + FINAL + 10), k[:17], fill=(200, 205, 215))
        sheet.save(sheet_path)
        print("sheet", os.path.normpath(sheet_path))


if __name__ == "__main__":
    main()
