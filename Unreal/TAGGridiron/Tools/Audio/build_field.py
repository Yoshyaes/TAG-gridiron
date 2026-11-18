"""Build the field's sounds: pads, bodies, the ball, feet, whistles, effort, the stadium's horn and cannon, weather.

Pad hits are designed, not found (nobody records a clean tackle): a hard plastic crack (the shoulder pads' shells), a
helmet's clack on the hard ones, a real body impact under it, a thump you feel, and the scuff after. Everything else
is cut from the public-domain recordings: each file is split into its separate sounds, the tagging model says what
each is, and the ones that are what we want are trimmed, cleaned and levelled.

  python build_field.py <src dir> <out dir> [name ...]
"""
import os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audiolib as al  # noqa: E402
from events import events  # noqa: E402

SR = al.SR
_cache = {}


def src(base, i):
    if i not in _cache:
        _cache[i] = al.load(os.path.join(base, 'fs', i + '.mp3'))
    return _cache[i]


def cuts(base, i, tail=0.25, gate=-30.0, gap=0.12, keep=None, tags=None, min_p=0.25):
    """The separate sounds of a recording as mono clips [(clip, start, len, peak)], optionally only those the tagging
    model hears as one of `tags`."""
    x = src(base, i)
    m = al.mono(x)
    ev = events(x, gate, gap)
    out = []
    for (s, e, p) in ev:
        if keep and not keep(s, e - s, p):
            continue
        out.append((m[int(s * SR): int((e + tail) * SR)].copy(), s, e - s, p))
    if tags and out:
        import listen
        labels = list(tags) + ['a man talking', 'music', 'crowd', 'a click', 'wind noise', 'silence', 'a thud', 'a whoosh', 'a whistle', 'a man grunting']
        labels = list(dict.fromkeys(labels))
        sc = listen.score([np.pad(c, (0, SR // 2)) for c, *_ in out], labels)
        out = [o for o, row in zip(out, sc) if row[: len(tags)].sum() >= min_p]
    return out


def trim(c, fin=0.002, fout=0.06, pre=0.004, floor=-45.0):
    """Start just before the sound, end where it has died away."""
    e = np.abs(c)
    pk = e.max() + 1e-9
    on = np.nonzero(e > pk * al.db(-24))[0]
    a = max(0, on[0] - int(pre * SR)) if len(on) else 0
    k = int(0.01 * SR)
    env = np.convolve(e, np.ones(k) / k, mode='same')
    live = np.nonzero(env > pk * al.db(floor))[0]
    b = min(len(c), live[-1] + int(0.03 * SR)) if len(live) else len(c)
    return al.fade(c[a:b], fin, min(fout, (b - a) / SR / 2))


def norm(c, peak=-3.0):
    return (c * (al.db(peak) / (np.max(np.abs(c)) + 1e-9))).astype(np.float32)


# ---------------------------------------------------------------- designed pieces
def decay(n, t60):
    return np.exp(-6.9 * np.arange(n) / SR / t60).astype(np.float32)


def crack(rng, hard=0.5, dur=0.16):
    """Hard plastic shells meeting: a band of noise that is gone in a few hundredths, and the shells' own knock."""
    n = int(dur * SR)
    burst = al.bp(al.noise(n, rng), 700 + 500 * rng.random(), 3800 + 2500 * hard, 2) * decay(n, 0.035 + 0.03 * rng.random())
    out = burst * 0.9
    t = np.arange(n) / SR
    for _ in range(4 + int(3 * hard)):
        f = rng.uniform(260, 1500)
        out += np.sin(2 * np.pi * f * t + rng.uniform(0, 6.28)) * decay(n, rng.uniform(0.03, 0.09)) * rng.uniform(0.25, 0.6) * (500 / f) ** 0.3
    out[: int(0.0015 * SR)] *= np.linspace(0, 1, int(0.0015 * SR))
    return out.astype(np.float32)


def helmet(rng, dur=0.1):
    """A helmet's shell: higher, tighter, a hint of the facemask's ring."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    out = al.bp(al.noise(n, rng), 1500, 7000, 2) * decay(n, 0.018) * 0.8
    for _ in range(4):
        f = rng.uniform(1300, 3400)
        out += np.sin(2 * np.pi * f * t) * decay(n, rng.uniform(0.012, 0.035)) * 0.4
    f = rng.uniform(4200, 5600)
    out += np.sin(2 * np.pi * f * t) * decay(n, 0.05) * 0.06
    return out.astype(np.float32)


def thump(rng, f=80.0, dur=0.22, drop=0.5):
    """The weight of it: a low note falling away."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    fr = f * (1 + drop * np.exp(-t / 0.03))
    ph = 2 * np.pi * np.cumsum(fr) / SR
    out = np.sin(ph) * decay(n, dur * 0.6)
    out += al.lp(al.noise(n, rng), 220) * decay(n, 0.05) * 0.5
    return out.astype(np.float32)


def scuff(rng, dur=0.35, lo=900, hi=5000):
    """Cloth, pads and turf sliding after the hit."""
    n = int(dur * SR)
    e = np.interp(np.arange(n) / n, [0, 0.12, 0.4, 1], [0, 1, 0.45, 0]) ** 1.5
    w = al.bp(al.noise(n, rng), lo, hi) * e
    # not a steady hiss: a few uneven rubs
    rub = np.interp(np.arange(n), np.linspace(0, n, 9), rng.uniform(0.3, 1, 9))
    return (w * rub).astype(np.float32)


def bodies(base):
    """Real body impacts (tackles on grass, falls on dirt), mono."""
    out = []
    for i, kw in (('662107', dict(keep=lambda s, d, p: d > 0.3, tail=0.2)), ('675923', dict(tail=0.15)), ('504626', dict(tail=0.3)), ('346695', dict(tail=0.3))):
        if os.path.exists(os.path.join(base, 'fs', i + '.mp3')):
            out += [trim(c, fout=0.15) for c, *_ in cuts(base, i, **kw)]
    return out


def hit(base, rng, power, body_pool):
    """power 0 (a light bump) .. 1 (the hit of the game)."""
    n = int(0.9 * SR)
    out = np.zeros(n, np.float32)
    al.place(out, crack(rng, power), 0.0, 0.55 + 0.35 * power)
    if power > 0.3:
        al.place(out, crack(rng, power * 0.8), rng.uniform(0.012, 0.03), 0.3 + 0.2 * power)  # the second shoulder
    if power > 0.55:
        al.place(out, helmet(rng), rng.uniform(0.004, 0.02), 0.25 + 0.4 * (power - 0.55))
    b = body_pool[rng.integers(len(body_pool))]
    b = al.lp(al.resample(b, rng.uniform(0.85, 1.1) - 0.1 * power), 900 + 1400 * power)
    al.place(out, norm(b[: int(0.6 * SR)], -6), 0.004, 0.35 + 0.5 * power)
    al.place(out, thump(rng, 95 - 30 * power, 0.16 + 0.12 * power), 0.0, 0.15 + 0.75 * power)
    al.place(out, scuff(rng, 0.25 + 0.2 * power), 0.05, 0.05 + 0.05 * power)
    out = al.saturate(norm(out, -1.0), 1.2 + 1.2 * power)
    out = al.compress(out, -14, 3.0, 0.002, 0.08)
    if power > 0.8:
        # the big ones come back off the stands
        wet = al.mono(al.convolve(out, al.stadium_ir(rng, rt60=1.6), wet=0.07, dry=1.0))
        out = wet[: int(1.6 * SR)]
    return norm(trim(out, floor=-50), -1.5 - 5.0 * (1 - power))


def line_crash(base, rng, body_pool):
    """The snap: seven or eight collisions inside a fifth of a second, low and heavy."""
    n = int(1.4 * SR)
    out = np.zeros(n, np.float32)
    for _ in range(rng.integers(6, 9)):
        h = hit(base, rng, rng.uniform(0.35, 0.8), body_pool)
        al.place(out, h, abs(rng.normal(0.0, 0.07)), rng.uniform(0.4, 0.9))
    for _ in range(3):
        al.place(out, helmet(rng), rng.uniform(0.0, 0.12), 0.25)
    al.place(out, scuff(rng, 0.8, 500, 4000), 0.08, 0.12)
    out = al.compress(al.saturate(norm(out, -1), 1.6), -16, 4.0, 0.003, 0.12)
    return norm(trim(out, floor=-48), -1.5)


def blip(f, dur, t60, rng=None, glide=0.0, harm=(1.0, 0.3, 0.1)):
    n = int(dur * SR)
    t = np.arange(n) / SR
    fr = f * (1 + glide * (t / dur))
    ph = 2 * np.pi * np.cumsum(fr) / SR
    out = sum(a * np.sin(ph * (k + 1)) for k, a in enumerate(harm))
    out *= decay(n, t60)
    out[: int(0.003 * SR)] *= np.linspace(0, 1, int(0.003 * SR))
    return out.astype(np.float32)


def swoosh(rng, dur=0.6, lo=300, hi=6000, peak=0.45):
    """Air moving past: a band of noise that sweeps up and through."""
    n = int(dur * SR)
    w = al.noise(n + 4096, rng)
    out = np.zeros(n, np.float32)
    k = 16
    for j in range(k):
        a, b = j * n // k, (j + 1) * n // k
        pos = (j + 0.5) / k
        fc = lo * (hi / lo) ** (1 - abs(pos - peak) / max(peak, 1 - peak))
        seg = al.bp(w[max(0, a - 2048): b + 2048], fc * 0.6, min(fc * 1.6, 20000))
        seg = seg[a - max(0, a - 2048): a - max(0, a - 2048) + (b - a)]
        out[a:b] = seg
    e = np.interp(np.arange(n) / n, [0, peak, 1], [0, 1, 0]) ** 2
    return (out * e).astype(np.float32)


# ---------------------------------------------------------------- the list
def recipes(base):
    R = {}
    rng0 = np.random.default_rng(5)
    pool = {}

    def body_pool():
        if 'b' not in pool:
            pool['b'] = bodies(base)
            assert len(pool['b']) >= 6, len(pool['b'])
        return pool['b']

    def add(name, n, fn):
        for k in range(n):
            R['%s_%02d' % (name, k + 1)] = (lambda k=k: fn(k))

    add('Field/HitLight/HitLight', 6, lambda k: hit(base, np.random.default_rng(1000 + k), 0.12 + 0.05 * (k % 3), body_pool()))
    add('Field/HitMed/HitMed', 6, lambda k: hit(base, np.random.default_rng(1100 + k), 0.42 + 0.05 * (k % 3), body_pool()))
    add('Field/HitHeavy/HitHeavy', 6, lambda k: hit(base, np.random.default_rng(1200 + k), 0.7 + 0.05 * (k % 3), body_pool()))
    add('Field/HitHuge/HitHuge', 4, lambda k: hit(base, np.random.default_rng(1300 + k), 0.92 + 0.02 * k, body_pool()))
    add('Field/LineCrash/LineCrash', 5, lambda k: line_crash(base, np.random.default_rng(1400 + k), body_pool()))

    def fall(k):
        b = body_pool()
        rng = np.random.default_rng(1500 + k)
        c = al.lp(al.resample(b[(k * 3 + 1) % len(b)], rng.uniform(0.9, 1.05)), 2600)
        out = np.zeros(int(0.9 * SR), np.float32)
        al.place(out, norm(c[: int(0.7 * SR)], -3), 0.0, 1.0)
        al.place(out, thump(rng, 70, 0.2, 0.3), 0.005, 0.45)
        al.place(out, scuff(rng, 0.4, 700, 4500), 0.03, 0.1)
        return norm(trim(out), -4.0)
    add('Field/BodyFall/BodyFall', 8, fall)

    def from_cuts(name, picks, n, post=lambda c, k: c, peak=-3.0):
        """picks: () -> list of mono clips, strongest first."""
        def one(k):
            if name not in pool:
                pool[name] = picks()
                assert len(pool[name]) >= 1, name
            cl = pool[name]
            return norm(post(trim(cl[k % len(cl)]), k), peak)
        add(name, n, one)

    def loudest(lst, n):
        return [c for c, *_ in sorted(lst, key=lambda r: -r[3])[:n]]

    # the ball
    from_cuts('Field/Catch/Catch', lambda: loudest(cuts(base, '94647', tail=0.12, keep=lambda s, d, p: p > -27 and d < 0.3), 8), 8,
              lambda c, k: al.peq(al.hp(c, 190, 4), 320, 3, 1.0))
    from_cuts('Field/Slap/Slap', lambda: loudest(cuts(base, '445673', tail=0.1, keep=lambda s, d, p: d < 0.2), 5), 5, lambda c, k: al.hp(c, 300))
    from_cuts('Field/Snap/Snap', lambda: loudest(cuts(base, '94647', tail=0.12, keep=lambda s, d, p: p > -31 and d < 0.3), 12)[6:], 5,
              lambda c, k: al.hp(al.lp(al.resample(c, 0.9), 5000), 150, 4), peak=-6.0)

    def throw(k):
        rng = np.random.default_rng(1600 + k)
        w = swoosh(rng, rng.uniform(0.28, 0.4), 500, 5000, 0.3)
        # the spiral: a faint flutter in the air
        t = np.arange(len(w)) / SR
        w *= 1 + 0.25 * np.sin(2 * np.pi * rng.uniform(22, 30) * t)
        return norm(w, -9.0)
    add('Field/Throw/Throw', 5, throw)

    def bounce(k):
        rng = np.random.default_rng(1700 + k)
        out = np.zeros(int(0.5 * SR), np.float32)
        al.place(out, thump(rng, rng.uniform(105, 140), 0.12, 0.35), 0, 0.9)
        al.place(out, al.bp(al.noise(int(0.05 * SR), rng), 400, 2600) * decay(int(0.05 * SR), 0.02), 0, 0.5)   # leather on turf
        al.place(out, scuff(rng, 0.18, 1500, 7000), 0.01, 0.12)                                                # the blades
        return norm(trim(out), -6.0)
    add('Field/Bounce/Bounce', 5, bounce)

    def kick(k, punt):
        rng = np.random.default_rng(1800 + k + (50 if punt else 0))
        if 'k' not in pool:
            pool['k'] = loudest(cuts(base, '515908', tail=0.2, keep=lambda s, d, p: p > -12), 8)
            pool['p'] = loudest(cuts(base, '795715', tail=0.25), 8) + loudest(cuts(base, '521825', tail=0.2), 4)
        real = trim((pool['p'] if punt else pool['k'])[k % len(pool['p'] if punt else pool['k'])], fout=0.12)
        real = al.resample(real, 0.8 if punt else 0.86)   # a football is bigger and softer than a soccer ball: lower
        out = np.zeros(int(0.7 * SR), np.float32)
        al.place(out, norm(al.hp(real[: int(0.5 * SR)], 110, 4), -3), 0.0, 1.0)
        al.place(out, thump(rng, 78 if punt else 92, 0.22 if punt else 0.16, 0.6), 0.0, 0.85)
        al.place(out, al.bp(al.noise(int(0.03 * SR), rng), 1200, 5000) * decay(int(0.03 * SR), 0.012), 0.0, 0.35 if not punt else 0.2)  # the foot's slap
        if punt:
            al.place(out, swoosh(rng, 0.3, 200, 1800, 0.2), 0.02, 0.18)   # the leg through the air
        out = al.compress(al.saturate(norm(al.hp(out, 38, 4), -1), 1.5), -14, 3, 0.002, 0.1)
        return norm(trim(out), -1.5)
    add('Field/KickPlace/KickPlace', 5, lambda k: kick(k, False))
    add('Field/KickPunt/KickPunt', 5, lambda k: kick(k, True))

    def post(k):
        # the upright: a big hollow steel pipe, struck by leather: a low clang with a long hum
        rng = np.random.default_rng(1900 + k)
        n = int(2.2 * SR)
        t = np.arange(n) / SR
        f0 = rng.uniform(150, 185)
        out = np.zeros(n, np.float32)
        for mult, amp, t60 in ((1.0, 1.0, 1.6), (2.76, 0.6, 0.9), (5.4, 0.35, 0.5), (8.93, 0.2, 0.3), (13.3, 0.1, 0.2)):
            out += amp * np.sin(2 * np.pi * f0 * mult * t * (1 + rng.uniform(-0.004, 0.004))) * decay(n, t60)
        al.place(out, thump(rng, 110, 0.1, 0.3), 0, 0.8)
        al.place(out, al.bp(al.noise(int(0.04 * SR), rng), 800, 4000) * decay(int(0.04 * SR), 0.015), 0, 0.6)
        # a real pipe's strike on top, tuned down to the upright's size
        if 'pipe' not in pool:
            pool['pipe'] = [c for c, *_ in sorted(cuts(base, '271170', tail=0.8, gate=-22, gap=0.3) + cuts(base, '57918', tail=0.6) + cuts(base, '336000', tail=0.4),
                                                  key=lambda r: -r[3])[:5]]
        pipe = al.resample(trim(pool['pipe'][k % len(pool['pipe'])], fout=0.3, floor=-50), 0.6)
        out = norm(out, -6)
        al.place(out, norm(pipe[: int(2.0 * SR)], -6), 0, 1.3)
        wet = al.mono(al.convolve(norm(out, -3), al.stadium_ir(rng, rt60=1.8), wet=0.12))
        return norm(trim(wet[: int(2.6 * SR)], fout=0.4, floor=-55), -2.0)
    add('Field/Post/Post', 3, post)

    # feet
    def steps():
        out = []
        for i in ('411148', '127955', '635052', '395564'):
            if os.path.exists(os.path.join(base, 'fs', i + '.mp3')):
                out += cuts(base, i, tail=0.08, gate=-22, gap=0.06, keep=lambda s, d, p: 0.02 < d < 0.3)[:10]
        return loudest(out, 14)
    from_cuts('Field/Step/Step', steps, 10, lambda c, k: al.lp(al.hp(c[: int(0.22 * SR)], 150), 6000), peak=-8.0)

    def cut_move(k):
        rng = np.random.default_rng(2000 + k)
        out = np.zeros(int(0.45 * SR), np.float32)
        al.place(out, scuff(rng, 0.3, 800, 6000), 0, 1.0)
        al.place(out, thump(rng, 120, 0.08, 0.2), 0.01, 0.35)
        return norm(trim(out), -8.0)
    add('Field/Cut/Cut', 4, cut_move)

    # effort
    def grunts():
        out = []
        for i in ('256603', '536751', '464485', '803168', '558489', '558544', '558618', '558656', '841313'):
            if os.path.exists(os.path.join(base, 'fs', i + '.mp3')):
                out += cuts(base, i, tail=0.1, gate=-26, gap=0.1, keep=lambda s, d, p: 0.12 < d < 0.7, tags=['a man grunting', 'a man groaning with effort'], min_p=0.45)[:4]
        return [c for c, *_ in out]
    from_cuts('Field/Grunt/Grunt', grunts, 10, lambda c, k: al.hp(c, 110), peak=-6.0)

    # officials
    def whistles(long):
        out = []
        for i in ('538422', '817398', '631849', '568995', '418564', '499386'):
            if os.path.exists(os.path.join(base, 'fs', i + '.mp3')):
                out += cuts(base, i, tail=0.2, gate=-24, gap=0.15, keep=(lambda s, d, p: d > 0.7) if long else (lambda s, d, p: 0.12 < d < 0.7),
                            tags=['a referee whistle'], min_p=0.5)[:3]
        return [c for c, *_ in out]

    def whistle_post(c, k, long=False):
        rng = np.random.default_rng(2100 + k)
        c = al.hp(c[: int((2.2 if long else 0.75) * SR)], 900)
        c = al.fade(c, 0.003, 0.06)
        wet = al.mono(al.convolve(norm(c, -3), al.stadium_ir(rng, rt60=1.5), wet=0.1))
        return trim(wet[: len(c) + int(0.9 * SR)], fout=0.3, floor=-50)
    from_cuts('Field/Whistle/Whistle', lambda: whistles(False), 5, whistle_post, peak=-4.0)
    from_cuts('Field/WhistleLong/WhistleLong', lambda: whistles(True), 3, lambda c, k: whistle_post(c, k, True), peak=-4.0)

    # the stadium
    def horn(k):
        rng = np.random.default_rng(2200 + k)
        x = al.mono(src(base, '64476' if k == 0 else '68999'))
        ev = events(src(base, '64476' if k == 0 else '68999'), -20, 0.3)
        s, e, _ = max(ev, key=lambda r: r[1] - r[0])
        c = al.fade(x[int(s * SR): int(min(e, s + 3.2) * SR)], 0.01, 0.25)
        c = al.resample(c, 0.72)   # a ship's horn in the rafters, not a hand-held can
        wet = al.convolve(al.lp(norm(c, -3), 5000), al.stadium_ir(rng, rt60=2.8), wet=0.3, dry=0.7)
        return norm(al.fade(wet, 0, 1.0), -3.0)
    add('Stadium/Horn/Horn', 2, horn)

    def fireworks(k):
        rng = np.random.default_rng(2300 + k)
        boom = [trim(c, fout=0.5, floor=-50) for c, *_ in cuts(base, '187767', tail=1.2, gate=-20, gap=0.4)][:3]
        n = int(6.0 * SR)
        out = np.zeros(n, np.float32)
        tt = 0.0
        for j in range(rng.integers(4, 7)):
            b = al.resample(boom[rng.integers(len(boom))], rng.uniform(0.8, 1.15))
            al.place(out, al.lp(norm(b[: int(2.0 * SR)], -3), 3500), tt, rng.uniform(0.5, 1.0))
            # the crackle after the burst
            cr = al.hp(al.noise(int(0.8 * SR), rng), 3000) * (rng.random(int(0.8 * SR)) > 0.985) * decay(int(0.8 * SR), 0.5)
            al.place(out, cr.astype(np.float32), tt + 0.25, 0.25)
            tt += rng.uniform(0.35, 1.0)
        wet = al.convolve(out, al.stadium_ir(rng, rt60=3.0), wet=0.3, dry=0.75)
        return norm(al.fade(wet[: int(8 * SR)], 0, 1.5), -3.0)
    add('Stadium/Fireworks/Fireworks', 3, fireworks)

    def house(k):
        # the stadium's drumline between plays: a phrase of a real cadence, heard through the bowl
        rng = np.random.default_rng(2400 + k)
        x = src(base, '457210')
        starts = [12, 55, 98, 151, 203, 262, 318, 371, 430, 481]
        a = starts[k % len(starts)]
        c = al.fade(x[int(a * SR): int((a + 9.0) * SR)], 0.05, 1.5)
        c = al.shelf(al.hp(c, 90), 4500, -4)
        wet = al.convolve(norm(c, -3), al.stadium_ir(rng, rt60=2.6), wet=0.35, dry=0.6)
        return al.to_lufs(al.fade(wet[: int(10.5 * SR)], 0, 1.5), -18.0)
    add('Stadium/HouseMusic/HouseMusic', 8, house)

    def weather(i, a, secs, tone):
        def f():
            x = src(base, i)
            c = tone(al.cut(x, a, a + secs + 4.0))
            return al.to_lufs(al.loopable(c, 4.0), -22.0, -2.0)
        return f
    R['Weather/WxRain/WxRain_01'] = weather('393560', 20, 40, lambda c: al.hp(c, 150))
    R['Weather/WxWind/WxWind_01'] = weather('117504', 60, 36, lambda c: al.lp(al.hp(c, 50), 5000))

    # the menus and the broadcast's graphics: designed, quiet, dry
    def ui(name, fn):
        R['Ui/%s/%s_01' % (name, name)] = fn
    rng = rng0
    ui('UiMove', lambda: norm(al.hp(blip(1250, 0.05, 0.02, harm=(1, 0.2)), 300), -14))
    ui('UiTab', lambda: norm(np.concatenate([blip(900, 0.035, 0.02), blip(1350, 0.06, 0.03)]), -13))

    def select():
        out = np.zeros(int(0.35 * SR), np.float32)
        al.place(out, blip(660, 0.2, 0.09, harm=(1, 0.4, 0.15)), 0, 0.8)
        al.place(out, blip(990, 0.25, 0.12, harm=(1, 0.3)), 0.045, 0.7)
        al.place(out, thump(np.random.default_rng(3), 70, 0.1, 0.3), 0, 0.5)
        return norm(out, -9)
    ui('UiSelect', select)
    ui('UiBack', lambda: norm(np.concatenate([blip(700, 0.05, 0.03), blip(470, 0.12, 0.06)]), -12))
    ui('UiError', lambda: norm(np.concatenate([blip(220, 0.09, 0.06, harm=(1, 0.6, 0.4)), np.zeros(int(0.03 * SR), np.float32), blip(200, 0.14, 0.08, harm=(1, 0.6, 0.4))]), -11))
    ui('UiTick', lambda: norm(al.hp(blip(1900, 0.025, 0.01, harm=(1, 0.1)), 500), -16))
    ui('UiLock', lambda: norm(blip(1320, 0.3, 0.14, harm=(1, 0.5, 0.2)) + np.pad(thump(np.random.default_rng(4), 90, 0.1, 0.3), (0, int(0.3 * SR) - int(0.1 * SR))) * 0.5, -9))

    def toast():
        out = np.zeros(int(0.4 * SR), np.float32)
        al.place(out, blip(880, 0.12, 0.06), 0, 0.7)
        al.place(out, blip(1175, 0.2, 0.1), 0.09, 0.7)
        return norm(out, -13)
    ui('UiToast', toast)

    def whoosh():
        r = np.random.default_rng(7)
        w = swoosh(r, 0.75, 250, 9000, 0.55)
        out = np.zeros(int(1.1 * SR), np.float32)
        al.place(out, w, 0, 1.0)
        al.place(out, thump(r, 60, 0.3, 0.8), 0.38, 0.9)   # the graphic lands
        return norm(al.stereo(out) if False else out, -7)
    ui('UiWhoosh', whoosh)

    def pause():
        out = np.zeros(int(0.4 * SR), np.float32)
        al.place(out, blip(520, 0.3, 0.14, glide=-0.25, harm=(1, 0.4, 0.2)), 0, 0.9)
        al.place(out, thump(np.random.default_rng(8), 65, 0.15, 0.4), 0, 0.6)
        return norm(out, -10)
    ui('UiPause', pause)
    return R


def main():
    base, out = sys.argv[1], sys.argv[2]
    R = recipes(base)
    names = [n for n in R if not sys.argv[3:] or any(a in n for a in sys.argv[3:])]
    for n in names:
        try:
            x = R[n]()
        except Exception as e:  # noqa: BLE001
            print('!! %-36s %s: %s' % (n, type(e).__name__, e), flush=True)
            continue
        al.save(os.path.join(out, n + '.wav'), x)
        print('%-40s %5.2fs  peak %5.1f  rms %5.1f' % (n, len(x) / SR, al.peak_db(x), al.rms_db(x)), flush=True)


if __name__ == '__main__':
    main()
