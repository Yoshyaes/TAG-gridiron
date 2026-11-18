"""Build the stadium crowd from the scanned field recordings (crowd_scan.py).

Layers (seamless loops): CrowdCalm (the murmur that never stops), CrowdBuzz (the building leaning in), CrowdRoar (a
sustained roar), CrowdNoise (the home crowd making noise before the other offense's snap). They are woven from many
short clean windows of several real stadium recordings, several streams deep, so no single voice, clap or moment
survives to be recognised on the loop, and the result is as dense as a full bowl.

Reactions (one-shots): real eruptions, groans, boos and gasps cut out of the recordings, each laid over the woven
crowd shaped to the same swell, so a recording of a few thousand people has the weight of seventy thousand.

  python build_crowd.py <src dir> <out dir> [name ...]
"""
import os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audiolib as al  # noqa: E402
from crowd_scan import WIN, DIRTY0  # noqa: E402

SR = al.SR
BED = ['829454', '829456', '528799', '328623', '648448', '324756', '342378', '691975', '612131', '424354']
_cache = {}


def src(i, base):
    if i not in _cache:
        _cache[i] = al.load(os.path.join(base, 'fs', i + '.mp3'))
    return _cache[i]


def pool(base, tier):
    """(file, start seconds) of every clean window in the tier."""
    out = []
    for i in BED:
        f = os.path.join(base, 'scan', i + '.npz')
        if not os.path.exists(f):
            continue
        d = np.load(f)
        dirty = d['p'][:, DIRTY0:].sum(axis=1)
        lvl, crest = d['lvl'], d['crest']
        ok = (dirty < 0.2) & (crest < 16.5) & (lvl > -50)
        if ok.sum() < 6:
            continue
        lo, mid, hi = np.percentile(lvl[ok], [40, 80, 88])
        for k in np.nonzero(ok)[0]:
            if k + 2 >= len(lvl) or not (ok[k + 1]):
                continue
            v = lvl[k]
            if tier == 'calm' and v <= lo:
                out.append((i, k * WIN))
            elif tier == 'buzz' and lo < v <= mid:
                out.append((i, k * WIN))
            elif tier == 'roar' and v >= hi and v > -26:
                out.append((i, k * WIN))
    return out


def stream(base, windows, n, rng, grain=3.0, xf=1.0):
    """One mono stream: grains from random windows, each levelled, crossfaded into the next."""
    out = np.zeros(n + int(grain * SR), np.float32)
    g, x = int(grain * SR), int(xf * SR)
    ramp = np.sin(np.linspace(0, np.pi / 2, x)) ** 2
    pos = -rng.integers(0, g - x)
    while pos < n:
        i, t = windows[rng.integers(len(windows))]
        s = src(i, base)
        a = int(t * SR)
        seg = s[a:a + g, rng.integers(2)].copy()
        if len(seg) < g:
            continue
        seg = al.resample(seg, 1.0 + rng.uniform(-0.025, 0.025))
        seg = np.pad(seg, (0, max(0, g - len(seg))))[:g]
        seg /= al.rms(seg) + 1e-6
        seg[:x] *= ramp
        seg[-x:] *= ramp[::-1]
        a0 = max(pos, 0)
        seg2 = seg[a0 - pos:]
        out[a0:a0 + len(seg2)] += seg2[: len(out) - a0]
        pos += g - x
    return out[:n]


def weave(base, tier, secs, seed, depth=4):
    rng = np.random.default_rng(seed)
    w = pool(base, tier)
    assert len(w) > 20, (tier, len(w))
    n = int(secs * SR)
    L = sum(stream(base, w, n, rng) for _ in range(depth))
    R = sum(stream(base, w, n, rng) for _ in range(depth))
    C = sum(stream(base, w, n, rng) for _ in range(depth))
    x = np.stack([L + 0.6 * C, R + 0.6 * C], axis=1) / np.sqrt(depth * 1.36)
    return al.hp(x.astype(np.float32), 70)


def bowl(x, rng, wet=0.14):
    return al.convolve(x, al.stadium_ir(rng, rt60=2.4), wet=wet)[: len(x) + int(2.0 * SR)]


def loop(base, tier, secs, seed, tone=None):
    x = weave(base, tier, secs + 3.0, seed)
    if tone:
        x = tone(x)
    x = bowl(x, np.random.default_rng(seed + 1))[: len(x)]
    x = al.loopable(x, 3.0)
    return al.to_lufs(x, -20.0, -1.5)


def shape(n, attack, hold, decay, floor_db=-40.0):
    """A swell: up in `attack`, held, then dying away (dB-linear) over `decay`."""
    t = np.arange(n) / SR
    e = np.ones(n)
    e[t < attack] = (t[t < attack] / attack) ** 1.5
    d = t > attack + hold
    e[d] = al.db(floor_db * np.clip((t[d] - attack - hold) / decay, 0, 1))
    return e.astype(np.float32)


def take(base, i, a, b, fin=0.05, fout=1.0):
    return al.fade(al.cut(src(i, base), a, b), fin, fout)


def follow(x, smooth=0.25):
    e = np.abs(al.mono(x))
    k = int(smooth * SR)
    e = np.convolve(e, np.ones(k) / k, mode='same')
    return (e / (e.max() + 1e-9)).astype(np.float32)


def many(x, rng, copies=6, spread=0.05, late=0.14):
    """A few dozen people become a section: copies a little apart in pitch, time and place."""
    m = al.mono(x)
    out = np.zeros((len(m) + int((late + 0.3) * SR), 2), np.float32)
    for _ in range(copies):
        c = al.resample(m, 1.0 + rng.uniform(-spread, spread))
        al.place(out, al.pan(c, rng.uniform(-0.8, 0.8)), rng.uniform(0, late), 1.0 / np.sqrt(copies))
    al.place(out, al.stereo(x), 0.02, 0.7)
    return out


def level(x, target=-16.0):
    """Loudest second at `target` dBFS RMS."""
    m = al.mono(x)
    k = SR
    if len(m) <= k:
        r = al.rms(m)
    else:
        c = np.cumsum(np.square(m, dtype=np.float64))
        r = float(np.sqrt(np.max(c[k:] - c[:-k]) / k))
    return al.limit(x * (al.db(target) / (r + 1e-9)), -1.0)


def cheer(base, seed, real, attack, hold, decay, bright=0.0, tier='roar', real_gain=1.0):
    """A real eruption over the woven roar shaped to the same swell."""
    rng = np.random.default_rng(seed)
    total = attack + hold + decay
    n = int(total * SR)
    bed = weave(base, tier, total + 0.2, seed + 7, depth=3)[:n]
    env = shape(n, attack, hold, decay)
    # the roar opens up as it grows: darker at the edges of the swell
    dark = al.lp(bed, 1800)
    bed = (bed.T * env ** 1.3 + dark.T * (env * (1 - env) * 1.5)).T
    out = bed * 0.8
    for (i, a, b) in real:
        r = take(base, i, a, min(b, a + total), 0.03, min(2.5, decay * 0.5))
        r = r / (al.rms(r[: int(3 * SR)]) + 1e-6) * al.rms(bed[int(attack * SR): int((attack + 2) * SR)])
        al.place(out, r, 0.0, real_gain)
    if bright:
        out = al.shelf(out, 3000, bright)
    out = bowl(out, rng, 0.12)
    return al.fade(level(out), 0.0, 1.0)


def groan(base, seed, real, big=False):
    rng = np.random.default_rng(seed)
    parts = [take(base, i, a, b, 0.03, 0.5) for (i, a, b) in real]
    n = int(max(len(p) for p in parts) + 1.5 * SR)
    out = np.zeros((n, 2), np.float32)
    for p in parts:
        p = p / (al.rms(p) + 1e-6)
        m = many(p, rng, copies=8 if big else 6)
        al.place(out, m, rng.uniform(0, 0.08), 1.0)
    # the bowl under it: the woven crowd, darkened (an "ohh" has no top), following the groan's own swell
    e = follow(out, 0.35)
    bed = al.bp(weave(base, 'buzz', n / SR + 0.2, seed + 3, depth=3)[:n], 160, 900)
    out = out / (al.rms(out) + 1e-6) + (bed.T * e).T / (al.rms(bed) + 1e-6) * (0.32 if big else 0.22)
    out = al.lp(out, 6500)
    out = bowl(out, rng, 0.18)
    return al.fade(level(out, -17.0), 0.02, 1.2)


def boo(base, seed, real):
    rng = np.random.default_rng(seed)
    parts = [take(base, i, a, b, 0.4, 2.0) for (i, a, b) in real]
    n = max(len(p) for p in parts)
    out = np.zeros((n, 2), np.float32)
    for p in parts:
        al.place(out, many(p / (al.rms(p) + 1e-6), rng, copies=4, spread=0.04, late=0.3), 0, 1.0)
    e = follow(out, 0.6)
    bed = al.bp(weave(base, 'buzz', n / SR + 0.2, seed + 5, depth=3)[:n], 150, 1200)
    out = out / (al.rms(out) + 1e-6) + (bed.T * e).T / (al.rms(bed) + 1e-6) * 0.7
    out = bowl(out, rng, 0.25)
    return al.fade(level(out, -17.0), 0.3, 2.0)


def recipes(base):
    R = {}
    R['Crowd/CrowdCalm/CrowdCalm_01'] = lambda: loop(base, 'calm', 64, 11, lambda x: al.shelf(x, 3500, -2.5))
    R['Crowd/CrowdBuzz/CrowdBuzz_01'] = lambda: loop(base, 'buzz', 60, 12)
    R['Crowd/CrowdRoar/CrowdRoar_01'] = lambda: loop(base, 'roar', 48, 13, lambda x: al.shelf(x, 2500, 1.5))
    R['Crowd/CrowdNoise/CrowdNoise_01'] = lambda: loop(base, 'roar', 44, 14, lambda x: al.shelf(al.peq(x, 2800, 4, 0.8), 5000, 3.0))
    # the big ones: a touchdown, a takeaway
    big = [[('528799', 1.2, 14)], [('691975', 13.6, 27)], [('328623', 50.3, 64)], [('528799', 1.2, 14), ('691975', 13.6, 27)]]
    for k, r in enumerate(big):
        R['Crowd/CheerBig/CheerBig_%02d' % (k + 1)] = (lambda r=r, k=k: cheer(base, 100 + k, r, 0.45, 4.0, 8.5, bright=1.5))
    med = [[('649156', 7.6, 15)], [('629884', 15.6, 22)], [('639283', 5.2, 12)], [('829453', 6.6, 13)], [('130568', 10.6, 17)]]
    for k, r in enumerate(med):
        R['Crowd/CheerMed/CheerMed_%02d' % (k + 1)] = (lambda r=r, k=k: cheer(base, 200 + k, r, 0.4, 1.6, 4.5))
    small = [[('397434', 0.3, 7)], [('397435', 0.3, 7)], [('829453', 14.6, 20)], [('649156', 13.0, 19)]]
    for k, r in enumerate(small):
        R['Crowd/CheerSmall/CheerSmall_%02d' % (k + 1)] = (lambda r=r, k=k: cheer(base, 300 + k, r, 0.5, 0.8, 3.2, tier='buzz', real_gain=0.8))
    gr = [[('264499', 0.0, 2.3)], [('264499', 4.9, 7.6)], [('494362', 2.6, 6.5)], [('264499', 8.2, 10.8)], [('264499', 14.0, 16.8), ('124996', 0, 2.0)],
          [('264499', 20.6, 24.5)]]
    for k, r in enumerate(gr):
        R['Crowd/Groan/Groan_%02d' % (k + 1)] = (lambda r=r, k=k: groan(base, 400 + k, r))
    grb = [[('264499', 0.0, 2.3), ('494362', 2.6, 6.5)], [('264499', 4.9, 7.6), ('264499', 10.9, 13.9)],
           [('264499', 14.0, 16.8), ('494362', 2.6, 6.5), ('124996', 0, 2.0)]]
    for k, r in enumerate(grb):
        R['Crowd/GroanBig/GroanBig_%02d' % (k + 1)] = (lambda r=r, k=k: groan(base, 450 + k, r, big=True))
    bo = [[('764251', 0.4, 12.5)], [('557189', 1.0, 14.0)], [('25270', 0.5, 12.0)], [('577098', 15.5, 23.5), ('764251', 0.4, 8.0)]]
    for k, r in enumerate(bo):
        R['Crowd/Boo/Boo_%02d' % (k + 1)] = (lambda r=r, k=k: boo(base, 500 + k, r))
    ga = [[('635110', 0.0, 1.5)], [('324898', 0.3, 3.6)], [('264499', 2.3, 4.6)], [('264499', 16.9, 19.5), ('635110', 0.0, 1.5)], [('494362', 2.6, 5.5), ('324898', 0.3, 3.0)]]
    for k, r in enumerate(ga):
        R['Crowd/Gasp/Gasp_%02d' % (k + 1)] = (lambda r=r, k=k: groan(base, 600 + k, r, big=True))
    # the visitors' corner: a few thousand, far away
    aw = [[('629884', 15.6, 22)], [('649156', 7.6, 14)], [('397434', 0.3, 7)]]
    for k, r in enumerate(aw):
        R['Crowd/AwayCheer/AwayCheer_%02d' % (k + 1)] = (lambda r=r, k=k: al.lp(cheer(base, 700 + k, r, 0.3, 1.0, 4.0, tier='calm', real_gain=1.4), 2600) * 0.5)

    def rise(seed, secs=7.0):
        # the kickoff: the whole building on one rising "ohhhhh" until the foot meets the ball
        rng = np.random.default_rng(seed)
        n = int(secs * SR)
        real = take(base, '612131', 27.5, 27.5 + secs, 0.5, 0.2)
        bed = weave(base, 'buzz', secs + 0.2, seed, depth=4)[:n]
        t = np.arange(n) / SR
        e = (0.12 + 0.88 * (t / secs) ** 1.6).astype(np.float32)
        # brighter as it climbs
        low, full = al.bp(bed, 200, 900), al.bp(bed, 200, 3200)
        bed = (low.T * (1 - t / secs) + full.T * (t / secs)).T
        out = (bed.T * e).T / (al.rms(bed) + 1e-6)
        real = real[:n] / (al.rms(real) + 1e-6)
        out[: len(real)] += (real.T * e[: len(real)]).T * 0.8
        out = bowl(out.astype(np.float32), rng, 0.2)[:n]
        return al.fade(level(out, -15.0), 0.4, 0.08)

    # (no DE-FENSE chant: there is no clean public-domain recording of one, and two attempts to build it - layered speech
    # voices, then the real roar shaped into the vowels - both came out sounding like a machine. It needs a real recording:
    # drop takes in Crowd/ChantDefense and the game plays them.)
    R['Crowd/KickRise/KickRise_01'] = lambda: rise(801)
    R['Crowd/KickRise/KickRise_02'] = lambda: rise(802)
    R['Crowd/KickShout/KickShout_01'] = lambda: cheer(base, 810, [('629884', 16.3, 20)], 0.08, 0.5, 2.6)
    R['Crowd/KickShout/KickShout_02'] = lambda: cheer(base, 811, [('639283', 5.6, 9)], 0.08, 0.5, 2.6)
    return R


def main():
    base, out = sys.argv[1], sys.argv[2]
    R = recipes(base)
    names = [n for n in R if not sys.argv[3:] or any(a in n for a in sys.argv[3:])]
    for n in names:
        x = R[n]()
        al.save(os.path.join(out, n + '.wav'), x)
        print('%-40s %5.1fs  %6.1f LUFS  peak %5.1f' % (n, len(x) / SR, al.lufs(x), al.peak_db(x)), flush=True)


if __name__ == '__main__':
    main()
