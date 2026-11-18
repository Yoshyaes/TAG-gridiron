"""Compose and record the game's music. Every piece is original, written here as notes and played by musiclib's band.

  MusIntro      the broadcast's theme ("Sunday Kickoff"): brass fanfare over driving strings, snare and timpani
  MusBumperOut  the theme's second half, to go to a break       MusBumperIn   its last phrase, to come back
  MusFinal      the theme broad and slow, for the final whistle  MusHalftime   a halftime-show bed on the theme
  MusMenu       the menu's playlist: a trap anthem with real brass, a driving electronic piece, a horn-riff groove

  <booth venv python> build_music.py <src dir> <out dir> [name ...]
"""
import os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audiolib as al  # noqa: E402
import musiclib as ml  # noqa: E402

SR = al.SR
_band = {}


def band(base):
    if _band:
        return _band
    v = os.path.join(base, 'vsco')
    S, H = ml.Sampler, ml.Hits
    _band.update(
        tpt=S(v + '/Brass/Trumpet/sus', 150, 1400, 0.025, 0.12), tpt_s=S(v + '/Brass/Trumpet/stac', 150, 1400, 0.003, 0.09, short=True),
        hn=S(v + '/Brass/F Horn/sus', 50, 900, 0.05, 0.2), hn_s=S(v + '/Brass/F Horn/stac', 50, 900, 0.004, 0.1, short=True),
        tbn=S(v + '/Brass/Tenor Trombone/sus', 40, 700, 0.04, 0.16), tbn_s=S(v + '/Brass/Tenor Trombone/stac', 40, 700, 0.004, 0.1, short=True),
        tuba=S(v + '/Brass/Tuba/sus', 28, 360, 0.04, 0.16), tuba_s=S(v + '/Brass/Tuba/stac', 28, 360, 0.004, 0.1, short=True),
        vln=S(v + '/Strings/Violin Section/susVib', 180, 2600, 0.09, 0.28), vln_s=S(v + '/Strings/Violin Section/Spic', 180, 2600, 0.003, 0.12, short=True),
        vla=S(v + '/Strings/Viola Section/susvib', 120, 1400, 0.09, 0.28), vla_s=S(v + '/Strings/Viola Section/spic', 120, 1400, 0.003, 0.12, short=True),
        vc=S(v + '/Strings/Cello Section/susvib', 60, 700, 0.09, 0.3), vc_s=S(v + '/Strings/Cello Section/spic', 60, 700, 0.003, 0.12, short=True),
        timp=S(v + '/Percussion/Timpani', 45, 280, 0.002, 0.6, short=True, pattern='Timpani*_Hit_*.wav', octave=1),   # (a drum's pitch reads an octave low: the measured notes are lifted)
        snare=H(v + '/Percussion', 'Snare2-HitNS'), bd=H(v + '/Percussion', 'BDrumNewhit'), crash=H(v + '/Percussion', 'cymbal-crash1_ff'),
        swell=H(v + '/Percussion', 'susCymb1-cresc-Median'), roll=H(v + '/Percussion', 'Snare2-rollNS'),
    )
    for k, s in _band.items():
        if isinstance(s, S):
            ms = [i['midi'] for i in s.items]
            print('  %-6s %3d samples, notes %.0f..%.0f' % (k, len(ms), min(ms), max(ms)), flush=True)
    return _band


def section(m, track, inst, midi, when, dur, vel=0.8, players=3, rng=None, spread=0.012):
    """Several players on one note: a hair apart in time and tune."""
    rng = rng or np.random.default_rng(int(midi * 7 + when * 100) % 9973)
    for p in range(players):
        a = inst.note(midi, dur, float(np.clip(vel + rng.uniform(-0.08, 0.08), 0.05, 1)), detune=rng.uniform(-0.07, 0.07) if players > 1 else 0)
        m.add(track, a, max(0.0, when + (rng.uniform(0, spread) if p else 0)), 1.0 / players ** 0.5)


def fit(midi, lo, hi):
    while midi < lo:
        midi += 12
    while midi > hi:
        midi -= 12
    return midi


# ---------------------------------------------------------------- the broadcast theme
# in B flat: one bar per chord. (root, third, fifth) as pitch classes above the key's tonic
CH = {'I': (0, 4, 7), 'vi': (9, 0, 4), 'IV': (5, 9, 0), 'V': (7, 11, 2), 'bVII': (10, 2, 5), 'IVV': (5, 9, 0)}
THEME_CH = ['I', 'I', 'vi', 'IVV', 'I', 'bVII', 'IV', 'V']
# the tune: (sixteenth, length, note) per bar, notes as written in B flat (70 = the B flat above middle C)
THEME = [
    [(0, 3, 70), (3, 1, 70), (4, 4, 74), (8, 6, 77), (14, 2, 74)],
    [(0, 3, 75), (3, 1, 75), (4, 4, 79), (8, 8, 77)],
    [(0, 3, 74), (3, 1, 74), (4, 4, 77), (8, 6, 82), (14, 2, 81)],
    [(0, 4, 79), (4, 4, 77), (8, 4, 75), (12, 4, 72)],
    [(0, 3, 70), (3, 1, 70), (4, 4, 74), (8, 6, 77), (14, 2, 79)],
    [(0, 3, 75), (3, 1, 75), (4, 4, 80), (8, 6, 84), (14, 2, 80)],
    [(0, 4, 82), (4, 2, 79), (6, 2, 77), (8, 4, 75), (12, 4, 79)],
    [(0, 6, 77), (6, 2, 77), (8, 2, 79), (10, 2, 81), (12, 4, 84)],
]
SNARE = [.5, .2, .3, .2, .85, .2, .3, .25, .5, .2, .3, .3, .9, .3, .55, .55]


def theme(base, bars, bpm=132, intro=2, key=-2, broad=False, lufs=-15.0, seed=1):
    """bars: which of the theme's eight bars to play, in order. The piece ends on the home chord, held."""
    B = band(base)
    rng = np.random.default_rng(seed)
    total_bars = intro + len(bars) + 2
    m = ml.Mix(total_bars * 4 * 60.0 / bpm + 3.5, bpm)
    tonic = 58 + key
    for name, gain, pan, send in (('tpt', 1.0, 0.25, 0.3), ('hn', 0.8, -0.3, 0.38), ('tbn', 0.85, 0.35, 0.3), ('tuba', 0.8, 0.15, 0.25), ('vln', 0.75, -0.45, 0.35),
                                  ('vla', 0.6, -0.1, 0.35), ('vc', 0.75, 0.4, 0.3), ('timp', 0.9, -0.15, 0.35), ('snare', 0.55, 0.05, 0.22), ('bd', 0.8, 0.0, 0.25),
                                  ('cym', 0.5, 0.0, 0.4), ('fx', 0.6, 0.0, 0.3)):
        m.track(name, gain, pan, send)

    def chord(name, second_half=False):
        r, t3, f5 = CH['V' if (name == 'IVV' and second_half) else name]
        return tonic + r, tonic + t3, tonic + f5

    def bar_of(b, name, tune, last_of_phrase=False, soft=False):
        T = lambda s: m.t(b, s)  # noqa: E731
        D = m.dur
        dyn = 0.75 if soft else 1.0
        # the tune: trumpets, the horns an octave under them
        for (s, l, n) in tune:
            n += key
            long_note = l >= 4
            section(m, 'tpt', B['tpt'] if long_note else B['tpt_s'], n, T(s), D(l) * (0.95 if long_note else 1.0), 0.92 * dyn, 3, rng)
            section(m, 'hn', B['hn'] if long_note else B['hn_s'], n - 12, T(s), D(l) * 0.95, 0.8 * dyn, 2, rng)
        for half in (0, 1):
            r, t3, f5 = chord(name, half == 1)
            s0 = half * 8
            # horns hold the harmony in the middle
            for n in (fit(t3, 55, 67), fit(f5, 55, 67)):
                section(m, 'hn', B['hn'], n, T(s0), D(7.5), 0.62 * dyn, 2, rng)
            # trombones: the rhythm of the thing
            for s in ((0, 3, 6) if half == 0 else (0, 4, 6)):
                for n in (fit(r, 43, 54), fit(f5, 46, 58)):
                    section(m, 'tbn', B['tbn_s'], n, T(s0 + s), D(1.6), (0.95 if s == 0 else 0.8) * dyn, 2, rng)
            section(m, 'tuba', B['tuba'], fit(r, 29, 40), T(s0), D(6), 0.85 * dyn, 1, rng)
            # strings: sixteenths that never stop
            tones = sorted([fit(r, 67, 79), fit(t3, 67, 79), fit(f5, 67, 79)])
            for s in range(8):
                n = tones[(0, 2, 1, 2, 0, 2, 1, 2)[s]]
                acc = 0.85 if s % 4 == 0 else 0.6
                section(m, 'vln', B['vln_s'], n, T(s0 + s), D(0.9), acc * dyn, 2, rng, 0.006)
                if s % 2 == 0:
                    section(m, 'vc', B['vc_s'], fit(r, 41, 52) + (12 if s % 4 == 2 else 0), T(s0 + s), D(1.5), 0.8 * dyn, 2, rng, 0.006)
            for n in (fit(t3, 53, 64), fit(f5, 53, 64)):
                section(m, 'vla', B['vla'], n, T(s0), D(8), 0.6 * dyn, 1, rng)
        # drums
        r0 = chord(name)[0]
        m.add('timp', B['timp'].note(fit(r0, 41, 52), 1.2, 0.9 * dyn), T(0))
        m.add('timp', B['timp'].note(fit(chord(name, True)[0], 41, 52), 0.8, 0.7 * dyn), T(8))
        for s in (12, 14):
            m.add('timp', B['timp'].note(fit(r0 + 7, 41, 52), 0.4, 0.55 + 0.1 * (s == 14)), T(s))
        for s in range(16):
            m.add('snare', B['snare'].hit(SNARE[s] * dyn, max_len=0.25), T(s) + rng.uniform(0, 0.004))
            if last_of_phrase and s >= 12:
                m.add('snare', B['snare'].hit(0.5 + 0.1 * (s - 12), max_len=0.2), T(s + 0.5))
        for s in (0, 8, 11) if not soft else (0, 8):
            m.add('bd', B['bd'].hit(0.9 if s == 0 else 0.7, max_len=0.8), T(s))

    # the way in: a roll and a swell into the downbeat
    if intro:
        t0 = m.t(0)
        steps = intro * 16
        for s in range(steps):
            v = 0.2 + 0.75 * (s / steps) ** 1.5
            m.add('timp', B['timp'].note(fit(tonic + 7, 41, 52), 0.3, v), m.t(0, s))
            if s >= steps // 2:
                m.add('snare', B['snare'].hit(v * 0.8, max_len=0.2), m.t(0, s))
                m.add('snare', B['snare'].hit(v * 0.6, max_len=0.2), m.t(0, s + 0.5))
        sw = B['swell'].hit(0.9)
        m.add('cym', sw, max(0.0, m.t(intro) - len(sw) / SR * 0.92), 0.9)
        # the brass gathers on the dominant
        for n in (fit(tonic + 7, 41, 52), fit(tonic + 0, 53, 60), fit(tonic + 2, 55, 64), fit(tonic + 7, 60, 67)):
            x = B['tbn' if n < 56 else 'hn'].note(n, m.dur(steps), 0.75)
            k = len(x)
            x = (x.T * np.linspace(0.15, 1.0, k) ** 1.6).T
            m.add('tbn' if n < 56 else 'hn', x, t0, 1.2)
        m.add('fx', ml.riser(rng, m.dur(steps) * 0.8), t0 + m.dur(steps) * 0.2, 0.35)
    for k, bi in enumerate(bars):
        b = intro + k
        if k % 4 == 0:
            m.add('cym', B['crash'].hit(0.95), m.t(b), 0.9)
            m.add('fx', ml.impact(rng, 2.0), m.t(b), 0.5 if k == 0 else 0.3)
        bar_of(b, THEME_CH[bi], THEME[bi], last_of_phrase=(k % 4 == 3), soft=False)
    # home: the whole band on the chord, timpani rolling under it, and let the hall have it
    b = intro + len(bars)
    T = m.t(b)
    hold = m.dur(22 if not broad else 26)
    r, t3, f5 = tonic, tonic + 4, tonic + 7
    for n, who, pl in ((fit(r, 77, 84), 'tpt', 3), (fit(f5, 70, 78), 'tpt', 2), (fit(t3, 67, 76), 'tpt', 2), (fit(r, 58, 66), 'hn', 3), (fit(t3, 58, 68), 'hn', 2),
                       (fit(f5, 48, 56), 'tbn', 3), (fit(r, 43, 50), 'tbn', 2), (fit(r, 29, 40), 'tuba', 1)):
        section(m, who, B[who], n, T, hold, 0.95, pl, rng)
    for n, who in ((fit(r, 70, 80), 'vln'), (fit(f5, 67, 77), 'vln'), (fit(t3, 58, 66), 'vla'), (fit(r, 41, 52), 'vc')):
        section(m, who, B[who], n, T, hold, 0.9, 2, rng)
    m.add('cym', B['crash'].hit(1.0), T, 1.0)
    m.add('bd', B['bd'].hit(1.0), T)
    m.add('fx', ml.impact(rng, 3.0), T, 0.55)
    steps = int(hold / m.dur(1))
    for s in range(steps):
        v = 0.85 - 0.35 * s / steps if s < steps - 4 else 0.6 + 0.1 * (s - steps + 4)
        m.add('timp', B['timp'].note(fit(r, 41, 52), 0.3, v), T + m.dur(s))
    m.add('timp', B['timp'].note(fit(r, 41, 52), 1.5, 1.0), T + hold)
    m.add('bd', B['bd'].hit(1.0), T + hold)
    m.add('snare', B['snare'].hit(1.0, max_len=0.3), T + hold)
    out = m.render(rng, hall=2.3, lufs=lufs, tone=lambda x: al.shelf(al.peq(x, 2600, 1.5, 0.8), 9000, 1.5))
    end = int((T + hold + 3.2) * SR)
    return al.fade(out[:end], 0.0, 1.2)


# ---------------------------------------------------------------- halftime bed
def halftime(base, seed=5):
    """The theme's chords at a walk, a soft horn on the tune, a slow beat: something to read a box score to."""
    B = band(base)
    rng = np.random.default_rng(seed)
    bpm, key = 92, -2
    bars = 16
    m = ml.Mix(bars * 4 * 60.0 / bpm + 3.0, bpm)
    tonic = 58 + key
    for name, gain, pan, send in (('str', 0.8, 0.0, 0.4), ('hn', 0.7, -0.2, 0.45), ('ep', 0.8, 0.2, 0.25), ('bass', 0.9, 0, 0.05), ('kick', 0.9, 0, 0.05),
                                  ('snare', 0.6, 0, 0.2), ('hat', 0.35, 0.2, 0.1)):
        m.track(name, gain, pan, send)
    for b in range(bars):
        name = THEME_CH[b % 8]
        r, t3, f5 = (tonic + x for x in CH[name])
        for n, who in ((fit(r, 67, 78), 'vln'), (fit(f5, 62, 72), 'vln'), (fit(t3, 55, 65), 'vla'), (fit(r, 43, 52), 'vc')):
            section(m, 'str', B[who], n, m.t(b), m.dur(16), 0.5, 2, rng)
        for s in (0, 6):
            for n in (fit(r, 55, 66), fit(t3, 55, 66), fit(f5, 55, 66), fit(r + 14, 60, 72)):
                m.add('ep', ml.epiano(n, m.dur(5), 0.6), m.t(b, s))
        if b >= 4:
            for (s, l, n) in THEME[b % 8]:
                if l >= 3:
                    section(m, 'hn', B['hn'], n + key - 12, m.t(b, s), m.dur(l) * 0.95, 0.5, 2, rng)
        for s, l in ((0, 6), (10, 4)):
            m.add('bass', ml.sub808(fit(r, 29, 40), m.dur(l), 1.6), m.t(b, s), 0.8)
            m.add('kick', ml.kick(0.8), m.t(b, s))
        for s in (4, 12):
            m.add('snare', ml.snare(rng, 180, 0.2, 0.8), m.t(b, s), 0.8)
            m.add('snare', ml.clap(rng), m.t(b, s), 0.5)
        for s in range(0, 16, 2):
            m.add('hat', ml.hat(rng, 0.04), m.t(b, s), 0.9 if s % 4 == 0 else 0.6)
    out = m.render(rng, hall=2.0, lufs=-17.0)
    return al.fade(out[: int((m.t(bars) + 2.5) * SR)], 0.6, 2.4)


# ---------------------------------------------------------------- the menu's playlist
def drums_trap(m, rng, b, fill=False, hats=True, kick_on=True):
    for s in (0, 10) + ((14,) if b % 2 else ()):
        if kick_on:
            m.add('kick', ml.kick(1.0, 0.3, 50), m.t(b, s))
    m.add('snare', ml.clap(rng), m.t(b, 8), 0.9)
    m.add('snare', ml.snare(rng, 200, 0.2), m.t(b, 8), 0.7)
    if hats:
        for s in range(0, 16, 2):
            m.add('hat', ml.hat(rng, 0.03), m.t(b, s), 0.8 if s % 4 == 0 else 0.55)
        if b % 2 == 1:
            for k in range(8):   # the roll into the next bar
                m.add('hat', ml.hat(rng, 0.02), m.t(b, 12 + k * 0.5), 0.35 + 0.06 * k)
        if b % 4 == 2:
            for k in range(3):
                m.add('hat', ml.hat(rng, 0.02), m.t(b, 6 + k / 3 * 2 / 2), 0.5)
        if b % 4 == 3:
            m.add('hat', ml.hat(rng, 0.2, True), m.t(b, 14), 0.6)
    if fill:
        for k in range(8):
            m.add('snare', ml.snare(rng, 210, 0.12), m.t(b, 12 + k * 0.5), 0.3 + 0.08 * k)


def menu_trap(base, seed=11):
    """F minor, 140, half-time: 808s under a brass hook played by real trombones and trumpets."""
    B = band(base)
    rng = np.random.default_rng(seed)
    bpm = 140
    prog = [(41, (0, 3, 7)), (37, (0, 4, 7)), (44, (0, 4, 7)), (39, (0, 4, 7))]   # Fm  Db  Ab  Eb
    hook = [[(0, 3, 72), (3, 3, 72), (6, 2, 75), (8, 6, 72)], [(0, 3, 73), (3, 3, 73), (6, 2, 77), (8, 6, 73)],
            [(0, 3, 75), (3, 3, 75), (6, 2, 80), (8, 6, 75)], [(0, 3, 75), (3, 3, 79), (6, 2, 82), (8, 8, 79)]]
    plan = ['intro'] * 4 + ['a'] * 8 + ['b'] * 8 + ['break'] * 4 + ['b'] * 8 + ['out'] * 2
    m = ml.Mix(len(plan) * 4 * 60.0 / bpm + 3.0, bpm)
    for name, gain, pan, send in (('kick', 1.0, 0, 0.02), ('sub', 1.0, 0, 0.0), ('snare', 0.7, 0, 0.2), ('hat', 0.3, 0.15, 0.08), ('str', 0.6, -0.3, 0.35),
                                  ('pad', 0.5, 0, 0.4), ('brass', 0.95, 0.1, 0.3), ('fx', 0.6, 0, 0.3)):
        m.track(name, gain, pan, send)
    for b, sec in enumerate(plan):
        root, tri = prog[b % 4]
        tones = [root + 12 + t for t in tri]
        if sec != 'out':
            m.add('pad', ml.pad([fit(t, 53, 65) for t in tones] + [fit(root, 41, 52)], m.dur(16), 900 if sec == 'intro' else 1500, 0.3, 0.5, 4, rng), m.t(b),
                  0.9 if sec in ('intro', 'break') else 0.6)
        if sec in ('intro', 'a', 'break', 'b'):
            for s in range(16):
                n = [fit(tones[0], 60, 72), fit(tones[2], 60, 72), fit(tones[1], 60, 72), fit(tones[2], 60, 72)][s % 4]
                v = (0.75 if s % 4 == 0 else 0.5) * (0.7 if sec == 'intro' else 1.0)
                section(m, 'str', B['vla_s'] if n < 65 else B['vln_s'], n, m.t(b, s), m.dur(0.9), v, 2, rng, 0.005)
        if sec in ('a', 'b'):
            drums_trap(m, rng, b, fill=(b % 8 == 7))
            for s, l in ((0, 6), (10, 4)) + (((14, 2),) if b % 2 else ()):
                m.add('sub', ml.sub808(fit(root, 29, 40), m.dur(l) * 0.98, 2.4, slide=(-0.25 if s == 14 else 0.0)), m.t(b, s))
        if sec == 'break':
            drums_trap(m, rng, b, hats=True, kick_on=False, fill=(b % 4 == 3))
            if b % 4 == 2:
                m.add('fx', ml.riser(rng, m.dur(32)), m.t(b), 0.6)
        if sec == 'b':
            if b % 8 == 0:
                m.add('fx', ml.impact(rng, 2.5), m.t(b), 0.7)
                m.add('fx', B['crash'].hit(0.9), m.t(b), 0.5)
            for (s, l, n) in hook[b % 4]:
                long_note = l >= 6
                section(m, 'brass', B['tpt'] if long_note else B['tpt_s'], n, m.t(b, s), m.dur(l) * 0.9, 0.9, 2, rng)
                section(m, 'brass', B['tbn'] if long_note else B['tbn_s'], n - 12, m.t(b, s), m.dur(l) * 0.9, 0.9, 2, rng)
                section(m, 'brass', B['hn'] if long_note else B['hn_s'], n - 12 + (3 if tri[1] == 3 else 4) - 12 + 12, m.t(b, s), m.dur(l) * 0.9, 0.7, 2, rng)
            for s in (0, 3):
                section(m, 'brass', B['tuba_s'], fit(root, 29, 40), m.t(b, s), m.dur(2), 0.85, 1, rng)
        if sec == 'intro' and b == 3:
            m.add('fx', ml.riser(rng, m.dur(16)), m.t(b), 0.5)
            for k in range(8):
                m.add('snare', ml.snare(rng, 210, 0.12), m.t(b, 12 + k * 0.5), 0.25 + 0.08 * k)
        if sec == 'out' and b == len(plan) - 2:
            m.add('fx', ml.impact(rng, 3.0), m.t(b), 0.9)
            m.add('sub', ml.sub808(fit(prog[0][0], 29, 40), m.dur(24), 2.4), m.t(b))
            for n in (53, 60, 65, 68, 72):
                section(m, 'brass', B['tbn'] if n < 62 else B['tpt'], n, m.t(b), m.dur(20), 0.85, 2, rng)
    out = m.render(rng, hall=1.6, wet=0.8, lufs=-14.5, tone=lambda x: al.shelf(x, 8000, 1.5))
    return al.fade(out[: int((m.t(len(plan)) + 1.5) * SR)], 0.01, 1.8)


def menu_drive(base, seed=12):
    """E minor, 126: the orchestra over a four-on-the-floor beat: strings that never stop, low-brass power chords, the tune on trumpets."""
    rng = np.random.default_rng(seed)
    B = band(base)
    bpm = 126
    prog = [(40, (0, 3, 7)), (36, (0, 4, 7)), (43, (0, 4, 7)), (38, (0, 4, 7))]   # Em  C  G  D
    lead = [[(0, 4, 76), (4, 2, 79), (6, 6, 83), (12, 4, 79)], [(0, 4, 76), (4, 2, 79), (6, 6, 84), (12, 4, 79)],
            [(0, 4, 74), (4, 2, 79), (6, 6, 83), (12, 4, 79)], [(0, 4, 74), (4, 2, 78), (6, 6, 81), (12, 4, 78)]]
    plan = ['intro'] * 4 + ['a'] * 8 + ['b'] * 8 + ['break'] * 4 + ['b'] * 8 + ['out'] * 2
    m = ml.Mix(len(plan) * 4 * 60.0 / bpm + 3.0, bpm)
    for name, gain, pan, send in (('kick', 1.0, 0, 0.02), ('bass', 0.85, 0, 0.0), ('snare', 0.75, 0, 0.25), ('hat', 0.3, -0.15, 0.08), ('stab', 0.85, 0.2, 0.3),
                                  ('arp', 0.7, -0.3, 0.35), ('lead', 0.95, 0.1, 0.35), ('str', 0.65, -0.2, 0.4), ('fx', 0.6, 0, 0.3)):
        m.track(name, gain, pan, send)
    for b, sec in enumerate(plan):
        root, tri = prog[b % 4]
        tones = [root + 12 + t for t in tri]
        full = sec in ('a', 'b')
        if full or sec == 'out':
            if sec != 'out':
                for s in (0, 4, 8, 12):
                    m.add('kick', ml.kick(1.0, 0.26, 54), m.t(b, s))
                for s in (4, 12):
                    m.add('snare', ml.snare(rng, 185, 0.24), m.t(b, s))
                    m.add('snare', ml.clap(rng), m.t(b, s), 0.5)
                for s in range(16):
                    if s % 4 == 2:
                        m.add('hat', ml.hat(rng, 0.2, True), m.t(b, s), 0.7)
                    else:
                        m.add('hat', ml.hat(rng, 0.02), m.t(b, s), 0.45 if s % 2 else 0.6)
                if b % 8 == 7:
                    for k in range(8):
                        m.add('snare', ml.snare(rng, 200, 0.12), m.t(b, 12 + k * 0.5), 0.3 + 0.08 * k)
                for s in range(0, 16, 2):
                    n = fit(root, 28, 39) + (12 if s in (6, 14) else 0)
                    m.add('bass', ml.bass_synth(n, m.dur(1.7), 520 if sec == 'b' else 380), m.t(b, s), 1.0 if s % 4 == 0 else 0.8)
        if sec in ('a', 'b', 'break'):
            for s in (0, 3, 6, 10, 13):
                # power chords from the low brass (the trumpets join for the tune's sections)
                v = 0.55 if sec == 'break' else 0.9
                section(m, 'stab', B['tbn_s'], fit(root, 41, 52), m.t(b, s), m.dur(1.6), v, 2, rng)
                section(m, 'stab', B['tbn_s'], fit(tones[2], 46, 57), m.t(b, s), m.dur(1.6), v, 2, rng)
                section(m, 'stab', B['hn_s'], fit(tones[1], 52, 63), m.t(b, s), m.dur(1.6), v * 0.9, 2, rng)
                if sec == 'b':
                    section(m, 'stab', B['tpt_s'], fit(tones[0], 60, 71), m.t(b, s), m.dur(1.6), v * 0.85, 2, rng)
            if sec != 'break':
                section(m, 'stab', B['tuba'], fit(root, 28, 39), m.t(b), m.dur(7), 0.8, 1, rng)
                section(m, 'stab', B['tuba'], fit(root, 28, 39), m.t(b, 8), m.dur(7), 0.8, 1, rng)
        if sec in ('intro', 'a', 'b', 'break'):
            order = [0, 1, 2, 1, 0, 2, 1, 2]
            for s in range(16):
                n = fit(tones[order[s % 8]], 64, 76) + (12 if s % 8 >= 4 else 0)
                section(m, 'arp', B['vln_s'] if n >= 67 else B['vla_s'], n, m.t(b, s), m.dur(0.9), (0.85 if s % 4 == 0 else 0.6) * (0.5 + 0.5 * min(1, b / 3) if sec == 'intro' else 1), 2, rng, 0.005)
                if s % 2 == 0:
                    section(m, 'arp', B['vc_s'], fit(root, 40, 51) + (12 if s % 4 == 2 else 0), m.t(b, s), m.dur(1.5), 0.75, 2, rng, 0.005)
        if sec == 'b':
            if b % 8 == 0:
                m.add('fx', ml.impact(rng, 2.5), m.t(b), 0.6)
            for (s, l, n) in lead[b % 4]:
                section(m, 'lead', B['tpt'] if l >= 4 else B['tpt_s'], n - 12, m.t(b, s), m.dur(l) * 0.92, 0.9, 3, rng)
                section(m, 'lead', B['hn'] if l >= 4 else B['hn_s'], n - 24, m.t(b, s), m.dur(l) * 0.92, 0.8, 2, rng)
                section(m, 'str', B['vln'], n, m.t(b, s), m.dur(l) * 0.95, 0.75, 2, rng)
                m.add('lead', ml.pluck(n, m.dur(l), 3600), m.t(b, s), 0.25)
        if sec in ('intro', 'break'):
            for n, who in ((fit(tones[0], 64, 75), 'vln'), (fit(tones[2], 59, 70), 'vln'), (fit(tones[1], 52, 63), 'vla'), (fit(root, 40, 51), 'vc')):
                section(m, 'str', B[who], n, m.t(b), m.dur(16), 0.6, 2, rng)
            if b % 4 == 2:
                m.add('fx', ml.riser(rng, m.dur(32)), m.t(b), 0.6)
            if b % 4 == 3:
                for k in range(16):
                    m.add('snare', ml.snare(rng, 200, 0.1), m.t(b, k), 0.15 + 0.045 * k)
        if sec == 'out' and b == len(plan) - 2:
            m.add('fx', ml.impact(rng, 3.0), m.t(b), 0.9)
            for n, who in ((76, 'tpt'), (71, 'tpt'), (64, 'hn'), (59, 'hn'), (52, 'tbn'), (40, 'tuba'), (76, 'vln'), (67, 'vln'), (59, 'vla'), (40, 'vc')):
                section(m, 'str' if who in ('vln', 'vla', 'vc') else 'stab', B[who], n, m.t(b), m.dur(22), 0.9, 2, rng)
            m.add('fx', B['crash'].hit(1.0), m.t(b), 0.6)
            m.add('bass', ml.bass_synth(28, m.dur(20), 300), m.t(b))
    out = m.render(rng, hall=1.5, wet=0.8, lufs=-14.5, tone=lambda x: al.shelf(x, 8500, 2.0))
    return al.fade(out[: int((m.t(len(plan)) + 1.5) * SR)], 0.01, 1.8)


def menu_groove(base, seed=13):
    """G minor, 92, swung: a head-nod beat, an electric piano, and a horn section answering it."""
    rng = np.random.default_rng(seed)
    B = band(base)
    bpm = 92
    prog = [(43, (55, 58, 62, 65)), (36, (55, 58, 60, 63)), (39, (55, 58, 62, 63)), (38, (54, 57, 60, 62))]   # Gm7 Cm7 Ebmaj7 D7
    riff = [[(8, 2, 67), (10, 2, 70), (12, 1, 72), (13, 3, 74)], [(8, 2, 67), (10, 2, 70), (12, 1, 72), (13, 3, 75)],
            [(8, 2, 70), (10, 2, 74), (12, 1, 75), (13, 3, 79)], [(8, 2, 69), (10, 2, 72), (12, 1, 74), (13, 3, 78)]]
    plan = ['intro'] * 2 + ['a'] * 8 + ['b'] * 8 + ['break'] * 4 + ['b'] * 8 + ['out'] * 2
    m = ml.Mix(len(plan) * 4 * 60.0 / bpm + 3.0, bpm, swing=0.3)
    for name, gain, pan, send in (('kick', 1.0, 0, 0.02), ('sub', 0.9, 0, 0.0), ('snare', 0.8, 0, 0.22), ('hat', 0.3, 0.2, 0.08), ('ep', 0.75, -0.15, 0.3),
                                  ('horns', 0.9, 0.15, 0.32), ('str', 0.5, 0, 0.4), ('fx', 0.5, 0, 0.3)):
        m.track(name, gain, pan, send)
    for b, sec in enumerate(plan):
        root, chord = prog[(b // 2) % 4]
        drums = sec in ('a', 'b') or (sec == 'break' and b % 4 >= 2)
        if sec != 'out':
            for s in (0, 6) + ((11,) if b % 2 else ()):
                for n in chord:
                    m.add('ep', ml.epiano(n, m.dur(4 if s == 0 else 2.5), 0.75 if s == 0 else 0.55), m.t(b, s) + rng.uniform(0, 0.012))
        if drums:
            for s in (0, 7, 10):
                m.add('kick', ml.kick(0.9, 0.28, 56), m.t(b, s), 1.0 if s == 0 else 0.8)
                m.add('sub', ml.sub808(fit(root, 31, 43), m.dur(3 if s == 0 else 2), 1.5), m.t(b, s), 0.9)
            for s in (4, 12):
                m.add('snare', ml.snare(rng, 175, 0.24, 0.8), m.t(b, s))
                m.add('snare', B['snare'].hit(0.8, max_len=0.25), m.t(b, s), 0.5)
            for s in range(16):
                if s % 2 == 0:
                    m.add('hat', ml.hat(rng, 0.03), m.t(b, s), 0.85 if s % 4 == 0 else 0.6)
                elif s % 4 == 3:
                    m.add('hat', ml.hat(rng, 0.02), m.t(b, s), 0.35)
            if b % 4 == 3:
                m.add('hat', ml.hat(rng, 0.2, True), m.t(b, 14), 0.6)
                m.add('snare', ml.snare(rng, 175, 0.15, 0.8), m.t(b, 15), 0.5)
        if sec == 'a' and b % 2 == 1:
            for (s, l, n) in riff[(b // 2) % 4]:   # the verse: the trombones alone, answering every other bar
                section(m, 'horns', B['tbn'] if l >= 3 else B['tbn_s'], n - 12, m.t(b, s), m.dur(l) * 0.85, 0.65, 2, rng)
        if sec == 'b':
            if b % 8 == 0:
                m.add('fx', B['crash'].hit(0.8), m.t(b), 0.5)
            for (s, l, n) in riff[(b // 2) % 4]:
                last = l >= 3
                section(m, 'horns', B['tpt'] if last else B['tpt_s'], n, m.t(b, s), m.dur(l) * 0.85, 0.9, 2, rng)
                section(m, 'horns', B['tbn'] if last else B['tbn_s'], n - 12, m.t(b, s), m.dur(l) * 0.85, 0.9, 2, rng)
                if last:
                    section(m, 'horns', B['hn'], n - 12 + 3, m.t(b, s), m.dur(l) * 0.85, 0.7, 2, rng)
            if b % 2 == 0:
                for n in (fit(chord[2], 62, 72), fit(chord[1], 67, 79)):
                    section(m, 'str', B['vln'], n, m.t(b), m.dur(32), 0.5, 2, rng)
        if sec in ('intro', 'break', 'a'):
            for n, who in ((fit(chord[3], 62, 73), 'vln'), (fit(chord[1], 55, 66), 'vla'), (fit(root, 36, 47), 'vc')):
                section(m, 'str', B[who], n, m.t(b), m.dur(16), 0.5 if sec == 'a' else 0.65, 2, rng)
        if sec == 'out' and b == len(plan) - 2:
            for n in prog[0][1]:
                m.add('ep', ml.epiano(n, m.dur(24), 0.8), m.t(b))
            m.add('sub', ml.sub808(31, m.dur(16), 1.5), m.t(b))
            for n in (67, 62, 55):
                section(m, 'horns', B['tpt'] if n > 60 else B['tbn'], n, m.t(b), m.dur(14), 0.75, 2, rng)
    m.tracks['ep']['buf'] = al.lp(m.tracks['ep']['buf'], 2600, 2)   # a rounder, older keyboard
    out = m.render(rng, hall=1.4, wet=0.8, lufs=-14.5)
    return al.fade(out[: int((m.t(len(plan)) + 1.5) * SR)], 0.01, 1.8)


def main():
    base, out = sys.argv[1], sys.argv[2]
    R = {
        'Music/MusIntro/MusIntro_01': lambda: theme(base, [0, 1, 2, 3, 4, 5, 6, 7], 132, 2),
        'Music/MusBumperOut/MusBumperOut_01': lambda: theme(base, [4, 5, 6, 7], 132, 1, seed=2),
        'Music/MusBumperIn/MusBumperIn_01': lambda: theme(base, [6, 7], 132, 0, seed=3),
        'Music/MusFinal/MusFinal_01': lambda: theme(base, [0, 1, 2, 3, 4, 5, 6, 7], 116, 2, broad=True, seed=4),
        'Music/MusHalftime/MusHalftime_01': lambda: halftime(base),
        'Music/MusMenu/MusMenu_01': lambda: menu_trap(base),
        'Music/MusMenu/MusMenu_02': lambda: menu_drive(base),
        'Music/MusMenu/MusMenu_03': lambda: menu_groove(base),
    }
    for n, fn in R.items():
        if sys.argv[3:] and not any(a in n for a in sys.argv[3:]):
            continue
        x = fn()
        al.save(os.path.join(out, n + '.wav'), x)
        print('%-36s %5.1fs  %6.1f LUFS  peak %5.1f' % (n, len(x) / SR, al.lufs(x), al.peak_db(x)), flush=True)


if __name__ == '__main__':
    main()
