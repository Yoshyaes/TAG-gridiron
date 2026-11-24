"""Build the menus' sounds: everything the player hears before the game, from the title to the kickoff.

One family: a soft tactile click with a low knock under it for moving, a struck chime over a thump for choosing, air
for a screen coming or going, and real drums, a real crowd and a real cymbal for the two big moments (the title
arriving, the game starting). Nothing is a bare beep: every sound has a transient, a body and a little room.

  moving       UiMove (the cursor), UiValue (an option stepped: the game pitches it up or down), UiTab (a page, a team)
  choosing     UiSelect, UiBack, UiError (refused)
  screens      UiOpen / UiClose (a screen comes forward / goes back), UiTitle (the main menu arrives),
               UiStart (kick off: the hit, and the crowd coming up as the teams take the field)
  team select  UiTeam (another team), UiSwapSides (home and away trade), UiRandom (a random team)
  roster       UiPick / UiDrop / UiSwap (a man picked up, put down, carried a place), UiEquip / UiUnequip (an ability),
               UiType (a key, typing a name), UiSave (signed / saved), UiDelete (released / deleted)
  lobby        UiJoin / UiLeave (a player in or out: the game pitches it by player), UiCaller (he calls the plays)

  <booth venv python> build_ui.py <src dir> <out dir> [name ...]
"""
import glob, os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audiolib as al  # noqa: E402
import musiclib as ml  # noqa: E402
from build_field import norm, decay, crack, thump, scuff, blip, swoosh  # noqa: E402

SR = al.SR
_room = {}


def room(x, wet=0.12, rt=0.22, seed=1):
    """Mono or stereo in, stereo out, in a small dry room: enough air that it is a sound, not a test tone."""
    if seed not in _room:
        rng = np.random.default_rng(seed)
        n = int(rt * 1.5 * SR)
        t = np.arange(n) / SR
        ir = np.zeros((n, 2), np.float32)
        for c in range(2):
            w = al.lp(al.hp(al.noise(n, rng), 350), 7500) * np.exp(-6.9 * t / rt)
            w[: int(0.004 * SR)] = 0
            ir[:, c] = w / (np.sqrt(np.sum(w ** 2)) + 1e-9)
        _room[seed] = ir
    ir = _room[seed]
    x = al.stereo(x)
    out = np.zeros((len(x) + len(ir), 2), np.float32)
    out[: len(x)] = x
    from scipy.signal import fftconvolve
    for c in range(2):
        w = fftconvolve(x[:, c], ir[:, c])[: len(out)]
        out[: len(w), c] += w * wet
    return out


def done(x, peak, wet=0.12, tail=0.02):
    x = room(x, wet)
    e = np.abs(x).max(axis=1)
    live = np.nonzero(e > e.max() * al.db(-55))[0]
    x = x[: min(len(x), live[-1] + int(tail * SR))] if len(live) else x
    return norm(al.fade(x, 0.0005, min(0.03, len(x) / SR / 3)), peak)


def click(rng, f=3000.0, body=1100.0, low=0.0, dur=0.05, snap=0.007, ring=0.02):
    """A tactile click: a snap of band-limited noise, a short pitched body, and (low > 0) a knock under it."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    c = al.bp(al.noise(n, rng), f * 0.6, min(f * 1.7, 16000), 2) * decay(n, snap)
    c = c / (np.max(np.abs(c)) + 1e-9)
    c += np.sin(2 * np.pi * body * t) * decay(n, ring) * 0.55
    c += np.sin(2 * np.pi * body * 2.76 * t) * decay(n, ring * 0.5) * 0.15   # (a struck bar's second partial: not a pure tone)
    if low > 0:
        c += np.sin(2 * np.pi * 170 * t * (1 + 0.4 * np.exp(-t / 0.01))) * decay(n, 0.03) * low
    k = int(0.0006 * SR)
    c[:k] *= np.linspace(0, 1, k)
    return c.astype(np.float32)


def chime(notes, t60=0.3, dur=0.6, gap=0.0, rng=None, bright=1.0):
    """Struck notes (Hz), each with a bar's inharmonic partials; gap seconds apart."""
    n = int((dur + gap * len(notes)) * SR)
    out = np.zeros(n, np.float32)
    for i, f in enumerate(notes):
        m = int(dur * SR)
        t = np.arange(m) / SR
        y = np.sin(2 * np.pi * f * t) * decay(m, t60)
        y += np.sin(2 * np.pi * f * 2.0 * t) * decay(m, t60 * 0.5) * 0.28 * bright
        y += np.sin(2 * np.pi * f * 2.76 * t) * decay(m, t60 * 0.3) * 0.2 * bright
        y += np.sin(2 * np.pi * f * 5.4 * t) * decay(m, t60 * 0.12) * 0.1 * bright
        y[: int(0.002 * SR)] *= np.linspace(0, 1, int(0.002 * SR))
        al.place(out, y.astype(np.float32), i * gap, 1.0)
    return out


def sweep(x, a, b):
    """Mono to stereo, moving from pan a to pan b (-1 left .. 1 right)."""
    p = np.linspace(a, b, len(x))
    return np.stack([x * np.sqrt(0.5 * (1 - p)), x * np.sqrt(0.5 * (1 + p))], axis=1).astype(np.float32)


def buf(secs, ch=1):
    return np.zeros(int(secs * SR), np.float32) if ch == 1 else np.zeros((int(secs * SR), 2), np.float32)


def recipes(base, out_dir):
    R = {}
    v = os.path.join(base, 'vsco', 'Percussion')

    def ui(name, fn, takes=1):
        for k in range(takes):
            R['Ui/%s/%s_%02d' % (name, name, k + 1)] = (lambda k=k: fn(np.random.default_rng(sum(map(ord, name)) * 31 + k), k))

    # ---- moving
    ui('UiMove', lambda r, k: done(click(r, 2600 + 300 * k, 1000 + 70 * k, low=0.5), -7, 0.08), 4)
    ui('UiValue', lambda r, k: done(click(r, 4200 + 300 * k, 1650 + 60 * k, low=0.15, snap=0.005, ring=0.03), -8, 0.1), 3)

    def tab(r, k):
        o = buf(0.3)
        al.place(o, swoosh(r, 0.11, 700, 5200, 0.6), 0, 0.5)
        al.place(o, click(r, 3000, 1300 + 80 * k, low=0.6, ring=0.03), 0.07, 1.0)
        al.place(o, click(r, 3600, 1950 + 120 * k, ring=0.035), 0.115, 0.55)
        return done(o, -7)
    ui('UiTab', tab, 2)

    # ---- choosing
    def select(r, k):
        o = buf(0.7)
        al.place(o, thump(r, 72, 0.14, 0.6), 0, 0.75)
        al.place(o, click(r, 3200, 1500, ring=0.02), 0, 0.5)
        al.place(o, chime([784.0, 1174.7][:: 1], 0.26 + 0.03 * k, 0.5, 0.038, r), 0.004, 0.55)
        al.place(o, swoosh(r, 0.16, 1500, 9000, 0.3), 0.0, 0.1)
        return done(al.saturate(o, 1.2), -7, 0.14)
    ui('UiSelect', select, 2)

    def back(r, k):
        o = buf(0.5)
        al.place(o, swoosh(r, 0.16, 500, 4200, 0.5)[::-1].copy(), 0, 0.35)
        al.place(o, blip(640 - 30 * k, 0.16, 0.09, glide=-0.32, harm=(1, 0.3, 0.12)), 0.03, 0.8)
        al.place(o, thump(r, 88, 0.1, 0.3), 0.03, 0.45)
        al.place(o, click(r, 2200, 800, ring=0.015), 0.03, 0.35)
        return done(o, -8)
    ui('UiBack', back, 2)

    def error(r, k):
        o = buf(0.5)
        for at, g in ((0.0, 1.0), (0.13, 0.85)):
            n = int(0.11 * SR)
            t = np.arange(n) / SR
            bz = al.lp(np.sign(np.sin(2 * np.pi * 147 * t)) * 0.6 + np.sin(2 * np.pi * 98 * t), 1100) * decay(n, 0.07)
            bz[: int(0.002 * SR)] *= np.linspace(0, 1, int(0.002 * SR))
            al.place(o, bz.astype(np.float32), at, g)
            al.place(o, click(r, 1500, 420, ring=0.02), at, 0.5)
        return done(o, -8, 0.06)
    ui('UiError', error)

    # ---- screens
    def open_(r, k):
        n = 0.62
        o = buf(n, 2)
        al.place(o, sweep(swoosh(r, 0.42, 280, 7500 + 800 * k, 0.72), -0.6, 0.5), 0, 0.9)
        al.place(o, al.stereo(thump(r, 56, 0.26, 0.9)), 0.29, 0.8)
        al.place(o, al.stereo(click(r, 2600, 1200, ring=0.03)), 0.3, 0.3)
        al.place(o, al.stereo(chime([1568.0], 0.18, 0.3, 0, r, 0.6)), 0.3, 0.07)
        return done(o, -7, 0.16, 0.05)
    ui('UiOpen', open_, 2)

    def close(r, k):
        o = buf(0.55, 2)
        al.place(o, sweep(swoosh(r, 0.36, 260, 6000 - 500 * k, 0.72)[::-1].copy(), 0.5, -0.6), 0.02, 0.8)
        al.place(o, al.stereo(thump(r, 66, 0.16, 0.4)), 0.0, 0.55)
        al.place(o, al.stereo(click(r, 2000, 760, ring=0.02)), 0.0, 0.3)
        return done(o, -8, 0.14, 0.05)
    ui('UiClose', close, 2)

    def drums():
        H = ml.Hits
        return H(v, 'BDrumNewhit'), H(v, 'cymbal-crash1_ff'), H(v, 'Snare2-HitNS')

    def title(r, k):
        """The main menu arrives: a cymbal drawn in backwards, then the big drum, the crash and a long low tail. No
        pitch to it, so it sits over whichever piece the playlist starts with."""
        bd, crash, _ = drums()
        pre = 1.35
        o = buf(pre + 5.2, 2)
        rev = al.fade(al.stereo(crash.hit(1.0))[: int(2.4 * SR)], 0.0, 1.2)[::-1].copy()
        rev = al.hp(rev[-int(pre * SR):], 500)
        al.place(o, al.fade(rev, 0.5, 0.005), 0.0, 0.5)
        al.place(o, ml.riser(r, pre, 200, 5000), 0.0, 0.3)
        al.place(o, al.stereo(bd.hit(1.0)), pre, 1.0)
        al.place(o, al.stereo(bd.hit(0.9, 0.84)), pre + 0.012, 0.6)
        al.place(o, ml.impact(r, 4.5), pre, 0.85)
        al.place(o, al.fade(al.stereo(crash.hit(0.9)), 0.0, 2.5), pre, 0.42)
        al.place(o, al.stereo(crack(r, 0.9)), pre, 0.35)
        o = al.convolve(o, ml.hall_ir(r, 2.4), wet=0.5, dry=1.0)[: len(o)]
        o = al.compress(al.saturate(norm(o, -2), 1.3), -14, 2.5, 0.004, 0.2)
        return norm(al.fade(o, 0.01, 2.2), -3.5)
    R['Ui/UiTitle/UiTitle_01'] = lambda: title(np.random.default_rng(71), 0)

    def start(r, k):
        """Kick off: pads and the big drum together, and the building coming up as the teams take the field."""
        bd, crash, snare = drums()
        o = buf(4.2, 2)
        al.place(o, sweep(swoosh(r, 0.3, 300, 8000, 0.8), -0.4, 0.4), 0.0, 0.5)
        at = 0.24
        al.place(o, al.stereo(crack(r, 1.0)), at, 0.7)
        al.place(o, al.stereo(crack(r, 0.8)), at + 0.018, 0.4)
        al.place(o, al.stereo(bd.hit(1.0)), at, 1.0)
        al.place(o, al.stereo(snare.hit(1.0, max_len=0.3)), at, 0.35)
        al.place(o, ml.impact(r, 2.6), at, 0.7)
        al.place(o, al.fade(al.stereo(crash.hit(0.8)), 0.0, 1.8)[: int(2.5 * SR)], at, 0.22)
        cheers = sorted(glob.glob(os.path.join(out_dir, 'Crowd', 'CheerMed', '*.wav'))) or sorted(glob.glob(os.path.join(out_dir, 'Crowd', 'CheerBig', '*.wav')))
        if cheers:
            c = al.load(cheers[0])
            c = al.hp(c[: int(3.8 * SR)], 180)
            c = c / (al.rms(c) + 1e-9)
            t = np.arange(len(c)) / SR
            env = np.interp(t, [0, 0.25, 1.3, 3.8], [0.0, 0.75, 1.0, 0.0])
            al.place(o, (c.T * env).T.astype(np.float32), at - 0.05, 0.16)
        o = al.compress(al.saturate(norm(o, -2), 1.25), -13, 2.5, 0.003, 0.18)
        return norm(al.fade(o, 0.002, 1.2), -3.0)
    R['Ui/UiStart/UiStart_01'] = lambda: start(np.random.default_rng(72), 0)

    # ---- team select
    def team(r, k):
        o = buf(0.36, 2)
        d = 1 if k % 2 else -1
        al.place(o, sweep(swoosh(r, 0.15, 450, 6200 + 500 * k, 0.55), -0.5 * d, 0.5 * d), 0, 0.7)
        al.place(o, al.stereo(thump(r, 118 - 8 * k, 0.1, 0.5)), 0.085, 0.75)
        al.place(o, al.stereo(click(r, 2800, 1150 + 60 * k, ring=0.025)), 0.085, 0.5)
        return done(o, -7, 0.12)
    ui('UiTeam', team, 4)

    def swap_sides(r, k):
        o = buf(0.55, 2)
        al.place(o, sweep(swoosh(r, 0.26, 400, 6500, 0.5), -0.9, 0.9), 0, 0.7)
        al.place(o, sweep(swoosh(r, 0.26, 350, 5200, 0.5), 0.9, -0.9), 0.03, 0.7)
        for at, f in ((0.2, 105), (0.27, 82)):
            al.place(o, al.stereo(thump(r, f, 0.12, 0.5)), at, 0.75)
            al.place(o, al.stereo(click(r, 2800, 1100, ring=0.02)), at, 0.4)
        return done(o, -7, 0.12)
    ui('UiSwapSides', swap_sides)

    def random_(r, k):
        o = buf(0.75)
        ts = np.cumsum([0.0, 0.07, 0.062, 0.054, 0.046, 0.04, 0.036])
        for i, at in enumerate(ts):
            al.place(o, click(r, 3000 + 200 * i, 900 + 150 * i, low=0.2, ring=0.018), at, 0.5 + 0.06 * i)
        al.place(o, thump(r, 80, 0.12, 0.5), ts[-1] + 0.05, 0.7)
        al.place(o, chime([1174.7, 1568.0], 0.22, 0.4, 0.03, r), ts[-1] + 0.05, 0.45)
        return done(o, -7, 0.12)
    ui('UiRandom', random_)

    # ---- roster screens
    def pick(r, k):
        o = buf(0.35)
        al.place(o, scuff(r, 0.12, 1200, 6000), 0, 0.25)
        al.place(o, blip(540, 0.14, 0.08, glide=0.5, harm=(1, 0.3, 0.1)), 0.01, 0.8)
        al.place(o, click(r, 3400, 1400, ring=0.02), 0.01, 0.5)
        return done(o, -8)
    ui('UiPick', pick)

    def drop(r, k):
        o = buf(0.4)
        al.place(o, thump(r, 86, 0.15, 0.6), 0, 0.9)
        al.place(o, click(r, 2400, 720, ring=0.03), 0, 0.6)
        al.place(o, chime([880.0], 0.16, 0.3, 0, r, 0.7), 0.004, 0.3)
        al.place(o, scuff(r, 0.1, 900, 4000), 0.02, 0.12)
        return done(o, -7)
    ui('UiDrop', drop)

    def swap(r, k):
        o = buf(0.2)
        al.place(o, scuff(r, 0.07, 1500, 7000), 0, 0.3)
        al.place(o, click(r, 3000, 1050 + 140 * k, low=0.5, ring=0.022), 0.045, 1.0)
        return done(o, -7, 0.08)
    ui('UiSwap', swap, 3)

    def equip(r, k):
        o = buf(1.2)
        al.place(o, swoosh(r, 0.26, 600, 9000, 0.85), 0, 0.45)
        at = 0.22
        n = int(0.9 * SR)
        t = np.arange(n) / SR
        m = sum(np.sin(2 * np.pi * f * t + r.uniform(0, 6.28)) * decay(n, d) * g for f, d, g in
                ((1318.5, 0.7, 1.0), (1975.5, 0.55, 0.6), (2637.0, 0.45, 0.4), (3520.0, 0.3, 0.3), (4698.0, 0.22, 0.22), (6272.0, 0.15, 0.15), (7902.0, 0.1, 0.1)))
        m[: int(0.002 * SR)] *= np.linspace(0, 1, int(0.002 * SR))
        al.place(o, m.astype(np.float32), at, 0.5)
        al.place(o, thump(r, 64, 0.2, 0.8), at, 0.8)
        al.place(o, crack(r, 0.5), at, 0.3)
        return done(al.saturate(o, 1.15), -6, 0.2, 0.05)
    ui('UiEquip', equip)

    def unequip(r, k):
        o = buf(0.5)
        al.place(o, blip(988, 0.2, 0.1, glide=-0.4, harm=(1, 0.35, 0.2)), 0, 0.7)
        al.place(o, swoosh(r, 0.2, 500, 5000, 0.3)[::-1].copy(), 0.0, 0.3)
        al.place(o, thump(r, 78, 0.1, 0.3), 0.05, 0.5)
        return done(o, -8)
    ui('UiUnequip', unequip)

    def key(r, k):
        o = buf(0.16)
        n = int(0.05 * SR)
        t = np.arange(n) / SR
        down = al.bp(al.noise(n, r), 1600, 6500, 2) * decay(n, 0.005)
        down = down / (np.max(np.abs(down)) + 1e-9) + np.sin(2 * np.pi * (520 + 60 * k) * t) * decay(n, 0.012) * 0.7
        al.place(o, down.astype(np.float32), 0, 1.0)
        al.place(o, click(r, 4200, 1500 + 100 * k, ring=0.006, snap=0.003), 0.06 + 0.008 * k, 0.25)   # (the key coming back up)
        return done(o, -9, 0.06)
    ui('UiType', key, 5)

    def save(r, k):
        o = buf(1.3)
        al.place(o, thump(r, 70, 0.16, 0.6), 0, 0.7)
        al.place(o, click(r, 3200, 1500), 0, 0.4)
        al.place(o, chime([784.0, 987.8, 1174.7, 1568.0], 0.5, 0.9, 0.075, r), 0.0, 0.55)
        al.place(o, swoosh(r, 0.5, 2500, 11000, 0.6), 0.1, 0.06)
        return done(o, -6, 0.22, 0.08)
    ui('UiSave', save)

    def delete(r, k):
        o = buf(0.6)
        al.place(o, swoosh(r, 0.24, 300, 4500, 0.4)[::-1].copy(), 0, 0.5)
        for at in (0.0, 0.035, 0.08, 0.11, 0.16):
            m = int(0.04 * SR)
            al.place(o, (al.lp(al.noise(m, r), 3200) * decay(m, 0.012)).astype(np.float32), at + r.uniform(0, 0.01), r.uniform(0.3, 0.6))
        al.place(o, thump(r, 60, 0.2, 0.4), 0.2, 0.9)
        al.place(o, blip(300, 0.16, 0.08, glide=-0.4, harm=(1, 0.4, 0.2)), 0.2, 0.35)
        return done(o, -7, 0.1)
    ui('UiDelete', delete)

    # ---- the lobby
    def join(r, k):
        o = buf(0.6)
        al.place(o, click(r, 3200, 1400, low=0.6), 0, 0.6)
        al.place(o, chime([587.3, 880.0], 0.24, 0.42, 0.085, r), 0.0, 0.7)
        al.place(o, thump(r, 84, 0.12, 0.5), 0.085, 0.5)
        return done(o, -7, 0.16)
    ui('UiJoin', join)

    def leave(r, k):
        o = buf(0.5)
        al.place(o, chime([880.0, 587.3], 0.16, 0.3, 0.08, r, 0.6), 0.0, 0.6)
        al.place(o, swoosh(r, 0.2, 500, 4000, 0.3)[::-1].copy(), 0.05, 0.2)
        return done(o, -9, 0.14)
    ui('UiLeave', leave)

    def caller(r, k):
        o = buf(0.45)
        al.place(o, click(r, 3600, 1700, ring=0.012), 0.0, 0.7)
        al.place(o, click(r, 3600, 1700, ring=0.012), 0.065, 0.8)
        al.place(o, chime([1318.5], 0.2, 0.3, 0, r), 0.13, 0.6)
        return done(o, -8, 0.14)
    ui('UiCaller', caller)
    return R


def main():
    base, out = sys.argv[1], sys.argv[2]
    R = recipes(base, out)
    for n, fn in R.items():
        if sys.argv[3:] and not any(a in n for a in sys.argv[3:]):
            continue
        x = fn()
        al.save(os.path.join(out, n + '.wav'), x)
        print('%-34s %5.2fs  peak %5.1f  rms %5.1f' % (n, len(x) / SR, al.peak_db(x), al.rms_db(x)), flush=True)


if __name__ == '__main__':
    main()
