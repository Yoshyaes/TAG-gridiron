"""The band the game's music is played by: a sampler over real orchestral recordings (VSCO-2-CE, CC0) and a small
kit of synthesized drums, basses and pads, with a mixer, a hall and a mastering chain.

The sampler does not trust file names for pitch: every sample's note is measured once (YIN) and cached, so a note is
played from the nearest recording, re-pitched by exactly the right ratio: the sections are in tune with each other.
"""
import glob, json, os, re
import numpy as np
from scipy import signal

import audiolib as al

SR = al.SR


def hz(m):
    return 440.0 * 2 ** ((m - 69) / 12.0)


# ---------------------------------------------------------------- sampler
class Sampler:
    def __init__(self, folder, fmin=40, fmax=1600, attack=0.03, release=0.16, short=False, gain=1.0, pattern='*.wav', octave=0):
        self.folder, self.attack, self.release, self.short, self.gain = folder, attack, release, short, gain
        files = sorted(glob.glob(os.path.join(folder, pattern)))
        assert files, folder
        cache = os.path.join(folder, '_pitch.json')
        known = json.load(open(cache)) if os.path.exists(cache) else {}
        self.items = []
        dirty = False
        for f in files:
            k = os.path.basename(f)
            if k not in known:
                known[k] = self._measure(f, fmin, fmax)
                dirty = True
            if known[k] is None:
                continue
            m = re.search(r'_v(\d+)', k)
            self.items.append(dict(file=f, midi=known[k]['midi'] + 12 * octave, rms=known[k]['rms'], layer=int(m.group(1)) if m else 1, y=None))
        if dirty:
            json.dump(known, open(cache, 'w'))
        layers = sorted({i['layer'] for i in self.items})
        for i in self.items:
            i['dyn'] = layers.index(i['layer']) / max(1, len(layers) - 1)
        self.rng = np.random.default_rng(len(files))

    @staticmethod
    def _measure(f, fmin, fmax):
        import librosa
        y = al.mono(al.load(f, mono=True))
        if len(y) < SR // 10:
            return None
        seg = y[int(0.08 * SR): int(min(len(y), 1.6 * SR))]
        try:
            f0 = librosa.yin(seg.astype(np.float64), fmin=fmin, fmax=fmax, sr=SR, frame_length=4096)
        except Exception:  # noqa: BLE001
            return None
        f0 = float(np.median(f0[: max(1, len(f0) * 2 // 3)]))
        return dict(midi=69 + 12 * np.log2(f0 / 440.0), rms=float(al.rms(y[: int(min(len(y), 1.0 * SR))])))

    def _load(self, it):
        if it['y'] is None:
            y = al.load(it['file'])
            # start at the note, not at the silence before it
            e = np.abs(al.mono(y))
            on = np.nonzero(e > e.max() * 0.03)[0]
            a = max(0, on[0] - int(0.004 * SR)) if len(on) else 0
            it['y'] = y[a:] / (it['rms'] + 1e-6) * 0.1
        return it['y']

    def note(self, midi, dur, vel=0.8, detune=0.0):
        """Stereo audio of one note (its release included)."""
        best = min(abs(i['midi'] - midi) for i in self.items)
        near = [i for i in self.items if abs(i['midi'] - midi) <= best + 0.6]
        want = min(near, key=lambda i: abs(i['dyn'] - vel))['dyn']
        pick = [i for i in near if i['dyn'] == want]
        it = pick[self.rng.integers(len(pick))]
        y = self._load(it)
        ratio = 2 ** ((midi + detune - it['midi']) / 12.0)
        need = int((dur + self.release) * SR * ratio) + 64
        if self.short:
            y = y[: min(len(y), need)]
        elif len(y) < need:
            # hold the note: loop its steady middle
            a, b = int(len(y) * 0.35), int(len(y) * 0.85)
            x = int(0.06 * SR)
            body = y[a:b]
            ramp = np.linspace(0, 1, x)[:, None]
            reps = [y[:b]]
            while sum(len(r) for r in reps) < need + x:
                nxt = body.copy()
                reps[-1] = reps[-1].copy()
                reps[-1][-x:] = reps[-1][-x:] * (1 - ramp) + nxt[:x] * ramp
                reps.append(nxt[x:])
            y = np.concatenate(reps)[:need]
        else:
            y = y[:need]
        y = al.resample(y, ratio)
        n = min(len(y), int((dur + self.release) * SR))
        y = y[:n].copy()
        a = min(n, int(self.attack * SR))
        if a > 1:
            y[:a] *= np.linspace(0, 1, a)[:, None] ** 0.7
        r = min(n, int(self.release * SR))
        if r > 1:
            y[-r:] *= np.linspace(1, 0, r)[:, None] ** 1.5
        return y * (self.gain * (0.35 + 0.65 * vel) ** 1.6)


class Hits:
    """Unpitched percussion: recordings picked by name, by how hard."""
    def __init__(self, folder, prefix, gain=1.0):
        self.files = sorted(glob.glob(os.path.join(folder, prefix + '*.wav')))
        assert self.files, (folder, prefix)
        self.data = {}
        self.gain = gain
        self.rng = np.random.default_rng(len(self.files))

    def hit(self, vel=0.8, pitch=1.0, max_len=None):
        k = int(round(vel * (len(self.files) - 1) + self.rng.uniform(-0.6, 0.6)))
        f = self.files[int(np.clip(k, 0, len(self.files) - 1))]
        if f not in self.data:
            y = al.load(f)
            e = np.abs(al.mono(y))
            on = np.nonzero(e > e.max() * 0.03)[0]
            y = y[max(0, on[0] - int(0.003 * SR)) if len(on) else 0:]
            self.data[f] = y / (np.max(np.abs(y)) + 1e-9)
        y = self.data[f]
        if pitch != 1.0:
            y = al.resample(y, pitch)
        if max_len:
            y = al.fade(y[: int(max_len * SR)], 0, min(0.08, max_len / 2))
        return y * (self.gain * (0.25 + 0.75 * vel))


# ---------------------------------------------------------------- synths
def env_adsr(n, a, d, s, r):
    e = np.full(n, s, np.float32)
    na, nd, nr = int(a * SR), int(d * SR), int(r * SR)
    na, nd, nr = min(na, n), min(nd, max(0, n - na)), min(nr, n)
    if na:
        e[:na] = np.linspace(0, 1, na)
    if nd:
        e[na:na + nd] = np.linspace(1, s, nd)
    if nr:
        e[-nr:] *= np.linspace(1, 0, nr)
    return e


def saw(f, n, detune=0.0, phase=0.0):
    t = np.arange(n) / SR
    return signal.sawtooth(2 * np.pi * f * (1 + detune) * t + phase).astype(np.float32)


def sub808(midi, dur, drive=2.2, slide=0.0):
    """The long low note of a trap record: a sine that starts a little high, pushed until it growls."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    f = hz(midi) * (1 + 1.5 * np.exp(-t / 0.012)) * (1 + slide * t / max(dur, 1e-3))
    y = np.sin(2 * np.pi * np.cumsum(f) / SR)
    y = np.tanh(y * drive) / np.tanh(drive)
    y *= np.exp(-t / max(0.25, dur * 0.9)) * env_adsr(n, 0.002, 0.0, 1.0, 0.04)
    return al.stereo(y.astype(np.float32))


def kick(punch=1.0, length=0.32, tone=52.0):
    n = int(length * SR)
    t = np.arange(n) / SR
    f = tone * (1 + 3.2 * np.exp(-t / 0.018))
    y = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / (length * 0.35))
    click = al.hp(np.random.default_rng(1).standard_normal(n).astype(np.float32), 2500) * np.exp(-t / 0.003) * 0.25 * punch
    return al.stereo((np.tanh((y + click) * 1.8) * 0.9).astype(np.float32))


def snare(rng, tone=190.0, length=0.22, bright=1.0):
    n = int(length * SR)
    t = np.arange(n) / SR
    body = np.sin(2 * np.pi * tone * (1 + 0.4 * np.exp(-t / 0.01)) * t) * np.exp(-t / 0.05)
    wires = al.bp(rng.standard_normal(n).astype(np.float32), 1200, 9000 * bright) * np.exp(-t / (length * 0.4))
    return al.stereo((np.tanh((0.7 * body + 0.9 * wires) * 1.6) * 0.8).astype(np.float32))


def clap(rng, length=0.25):
    n = int(length * SR)
    t = np.arange(n) / SR
    w = al.bp(rng.standard_normal(n).astype(np.float32), 900, 4200)
    e = np.zeros(n, np.float32)
    for d in (0.0, 0.011, 0.023, 0.034):
        k = int(d * SR)
        e[k:] += np.exp(-(t[: n - k]) / (0.012 if d < 0.03 else 0.09))
    out = np.stack([w * e, np.roll(w, 37) * e], axis=1) * 0.5
    return out.astype(np.float32)


def hat(rng, length=0.05, open_=False):
    n = int((0.3 if open_ else length) * SR)
    t = np.arange(n) / SR
    w = al.hp(rng.standard_normal(n).astype(np.float32), 7000, 4)
    # a little metal: a few inharmonic squares under the noise
    m = sum(np.sign(np.sin(2 * np.pi * f * t)) for f in (3520, 4170, 5410, 6230, 8010)) * 0.08
    y = (w + al.hp(m.astype(np.float32), 6000)) * np.exp(-t / (0.09 if open_ else 0.012 + length * 0.2))
    return al.stereo((y * 0.4).astype(np.float32))


def bass_synth(midi, dur, cutoff=420.0, growl=1.5):
    n = int((dur + 0.03) * SR)
    y = saw(hz(midi), n) * 0.6 + saw(hz(midi), n, 0.006) * 0.4 + np.sin(2 * np.pi * hz(midi - 12) * np.arange(n) / SR) * 0.7
    e = env_adsr(n, 0.004, 0.08, 0.75, 0.03)
    y = al.lp((y * e).astype(np.float32), cutoff, 4)
    return al.stereo((np.tanh(y * growl) / np.tanh(growl) * 0.8).astype(np.float32))


def pad(midis, dur, cutoff=1800.0, attack=0.4, release=0.8, voices=5, rng=None):
    """Warm and wide: detuned saws per note, slow in, slow out."""
    rng = rng or np.random.default_rng(3)
    n = int((dur + release) * SR)
    out = np.zeros((n, 2), np.float32)
    for m in midis:
        for v in range(voices):
            d = rng.uniform(-0.007, 0.007)
            y = saw(hz(m), n, d, rng.uniform(0, 6.28))
            p = rng.uniform(-0.9, 0.9)
            out += al.pan(y, p) / (voices * len(midis)) ** 0.5
    e = env_adsr(n, attack, 0.2, 0.85, release)
    out = (out.T * e).T
    return al.lp(out, cutoff, 2) * 0.5


def pluck(midi, dur, cutoff=3000.0, shape='saw'):
    n = int((dur + 0.15) * SR)
    t = np.arange(n) / SR
    y = saw(hz(midi), n) if shape == 'saw' else np.sign(np.sin(2 * np.pi * hz(midi) * t)).astype(np.float32)
    y = y * 0.6 + saw(hz(midi), n, 0.005) * 0.4
    e = np.exp(-t / max(0.06, dur * 0.5)) * env_adsr(n, 0.002, 0, 1, 0.05)
    # the filter closes with the note
    lo = al.lp(y, cutoff * 0.3, 2)
    hi = al.lp(y, cutoff, 2)
    k = np.exp(-t / max(0.04, dur * 0.3))
    return al.stereo(((hi * k + lo * (1 - k)) * e * 0.6).astype(np.float32))


def epiano(midi, dur, vel=0.8):
    """An electric piano by FM: a bell on the attack, a round body."""
    n = int((dur + 0.4) * SR)
    t = np.arange(n) / SR
    f = hz(midi)
    mod = np.sin(2 * np.pi * f * 14 * t) * np.exp(-t / 0.05) * 0.6 * vel + np.sin(2 * np.pi * f * t) * np.exp(-t / 0.9) * 1.1 * vel
    y = np.sin(2 * np.pi * f * t + mod) * np.exp(-t / max(0.6, dur * 1.2)) * env_adsr(n, 0.003, 0, 1, 0.3)
    trem = 1 + 0.12 * np.sin(2 * np.pi * 5.2 * t)
    return np.stack([y * trem, y * (2 - trem)], axis=1).astype(np.float32) * 0.35 * (0.4 + 0.6 * vel)


def riser(rng, dur, lo=300.0, hi=9000.0):
    n = int(dur * SR)
    w = rng.standard_normal(n).astype(np.float32)
    out = np.zeros(n, np.float32)
    k = 24
    for j in range(k):
        a, b = j * n // k, (j + 1) * n // k
        fc = lo * (hi / lo) ** ((j + 0.5) / k)
        out[a:b] = al.bp(w[a:b], fc * 0.7, min(fc * 1.4, 20000))
    e = (np.arange(n) / n) ** 2.2
    return al.widen(al.stereo(out * e), 0.5) * 0.5


def impact(rng, length=2.5):
    """A cinematic hit: a sub drop, a slam of noise, a long dark tail."""
    n = int(length * SR)
    t = np.arange(n) / SR
    sub = np.sin(2 * np.pi * np.cumsum(38 * (1 + 2.5 * np.exp(-t / 0.05))) / SR) * np.exp(-t / 0.6)
    slam = al.lp(rng.standard_normal(n).astype(np.float32), 2500) * np.exp(-t / 0.08)
    tail = al.bp(rng.standard_normal(n).astype(np.float32), 120, 900) * np.exp(-t / 0.7) * 0.4
    y = np.tanh((sub * 1.2 + slam * 0.9 + tail) * 1.5)
    return al.widen(al.stereo(y.astype(np.float32)), 0.3) * 0.8


# ---------------------------------------------------------------- the mix
def hall_ir(rng, rt60=2.1, bright=5200.0, predelay=0.018):
    n = int((rt60 * 1.4) * SR)
    t = np.arange(n) / SR
    out = np.zeros((n, 2), np.float32)
    for c in range(2):
        w = al.noise(n, rng) * np.exp(-6.9 * t / rt60)
        w = al.lp(al.hp(w, 160), bright)
        # the top dies first
        w = w * 0.6 + al.lp(w, 1800) * 0.8 * (t / rt60).clip(0, 1)
        w[: int(predelay * SR)] = 0
        for d, g in ((0.011, 0.5), (0.019, 0.4), (0.027, 0.3), (0.041, 0.25)):
            i = int((predelay + d + (0.003 if c else 0)) * SR)
            w[i] += g * 8
        out[:, c] = w * 0.04
    return out


class Mix:
    def __init__(self, secs, bpm, swing=0.0):
        self.n = int(secs * SR)
        self.bpm, self.swing = bpm, swing
        self.tracks = {}

    def t(self, bar, step=0.0, steps_per_bar=16):
        """Seconds at (bar, sixteenth step) - bars and steps from 0. Swing delays the off sixteenths."""
        s = step
        if self.swing and steps_per_bar == 16:
            whole = int(np.floor(step))
            if whole % 2 == 1:
                s = step + self.swing
        return (bar + s / steps_per_bar) * 4 * 60.0 / self.bpm

    def dur(self, steps, steps_per_bar=16):
        return steps / steps_per_bar * 4 * 60.0 / self.bpm

    def track(self, name, gain=1.0, pan=0.0, send=0.2, width=1.0):
        if name not in self.tracks:
            self.tracks[name] = dict(buf=np.zeros((self.n, 2), np.float32), gain=gain, pan=pan, send=send, width=width)
        return self.tracks[name]

    def add(self, name, audio, when, gain=1.0):
        tr = self.tracks[name]
        a = al.stereo(audio)
        al.place(tr['buf'], a, when, gain)

    def render(self, rng, hall=2.1, wet=1.0, lufs=-15.0, tone=None):
        dry = np.zeros((self.n, 2), np.float32)
        send = np.zeros((self.n, 2), np.float32)
        for tr in self.tracks.values():
            b = tr['buf'] * tr['gain']
            if tr['width'] != 1.0:
                mid, side = b.mean(axis=1, keepdims=True), b - b.mean(axis=1, keepdims=True)
                b = mid + side * tr['width']
            if tr['pan']:
                l, r = np.cos((tr['pan'] + 1) * np.pi / 4) * 1.414, np.sin((tr['pan'] + 1) * np.pi / 4) * 1.414
                b = b * np.array([l, r], np.float32)
            dry += b
            send += b * tr['send']
        out = dry + al.convolve(send, hall_ir(rng, hall), wet=wet, dry=0.0)[: self.n]
        if tone:
            out = tone(out)
        out = al.hp(out, 28, 2)
        out = al.compress(out, -20, 2.2, 0.02, 0.25)
        out = al.to_lufs(out, lufs, -1.0)
        return out
