"""Small DSP kit for the game's sound build (numpy / scipy only). Audio is float32, shape (n, ch), at SR."""
import os, subprocess
import numpy as np
from scipy import signal

SR = 48000


# ---------------------------------------------------------------- files
def load(path, sr=SR, mono=False):
    """Decode anything ffmpeg reads to float32 (n, ch)."""
    ch = 1 if mono else 2
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 'f32le', '-ac', str(ch), '-ar', str(sr), '-'],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.float32).reshape(-1, ch).copy()


def save(path, x, sr=SR):
    import soundfile as sf
    os.makedirs(os.path.dirname(path), exist_ok=True)
    x = np.asarray(x, np.float32)
    peak = float(np.max(np.abs(x))) if len(x) else 0.0
    if peak > 0.999:
        x = x * (0.999 / peak)
    sf.write(path, x, sr, subtype='PCM_16')


def stereo(x):
    x = np.asarray(x, np.float32)
    if x.ndim == 1:
        x = x[:, None]
    return np.repeat(x, 2, axis=1) if x.shape[1] == 1 else x


def mono(x):
    x = np.asarray(x, np.float32)
    return x if x.ndim == 1 else x.mean(axis=1)


def secs(x):
    return len(x) / SR


def cut(x, a, b=None):
    return x[int(a * SR): None if b is None else int(b * SR)].copy()


def silence(t, ch=2):
    return np.zeros((int(t * SR), ch), np.float32) if ch > 1 else np.zeros(int(t * SR), np.float32)


# ---------------------------------------------------------------- level
def db(v):
    return 10.0 ** (v / 20.0)


def rms(x):
    return float(np.sqrt(np.mean(np.square(x)) + 1e-12))


def rms_db(x):
    return 20 * np.log10(rms(x) + 1e-9)


def peak_db(x):
    return 20 * np.log10(float(np.max(np.abs(x))) + 1e-9)


def _kweight(x):
    # ITU-R BS.1770 K-weighting at 48 kHz
    b1 = [1.53512485958697, -2.69169618940638, 1.19839281085285]
    a1 = [1.0, -1.69065929318241, 0.73248077421585]
    b2 = [1.0, -2.0, 1.0]
    a2 = [1.0, -1.99004745483398, 0.99007225036621]
    return signal.lfilter(b2, a2, signal.lfilter(b1, a1, x, axis=0), axis=0)


def lufs(x):
    """Integrated loudness (BS.1770 gating)."""
    x = stereo(x)
    k = _kweight(x)
    blk, hop = int(0.4 * SR), int(0.1 * SR)
    if len(k) < blk:
        return -0.691 + 10 * np.log10(np.sum(np.mean(k ** 2, axis=0)) + 1e-12)
    pw = np.array([np.sum(np.mean(k[i:i + blk] ** 2, axis=0)) for i in range(0, len(k) - blk + 1, hop)])
    l = -0.691 + 10 * np.log10(pw + 1e-12)
    keep = pw[l > -70]
    if not len(keep):
        return -70.0
    rel = -0.691 + 10 * np.log10(np.mean(keep)) - 10
    keep = pw[(l > -70) & (l > rel)]
    return float(-0.691 + 10 * np.log10(np.mean(keep) + 1e-12)) if len(keep) else -70.0


def to_lufs(x, target, ceiling=-1.0):
    g = db(target - lufs(x))
    return limit(x * g, ceiling)


def to_peak(x, target=-1.0):
    return x * db(target - peak_db(x))


def limit(x, ceiling=-1.0, release=0.08):
    """Look-ahead peak limiter."""
    c = db(ceiling)
    env = np.max(np.abs(stereo(x)), axis=1)
    la = int(0.004 * SR)
    need = np.minimum(1.0, c / np.maximum(env, 1e-9))
    need = -signal.lfilter([1], [1], -need)  # copy
    # minimum over the look-ahead window, then smooth the release
    from scipy.ndimage import minimum_filter1d
    g = minimum_filter1d(need, 2 * la + 1)
    a = np.exp(-1.0 / (release * SR))
    out = np.empty_like(g)
    cur = 1.0
    # release smoothing done blockwise for speed
    blk = 64
    for i in range(0, len(g), blk):
        m = float(g[i:i + blk].min())
        cur = m if m < cur else a ** blk * cur + (1 - a ** blk) * m
        out[i:i + blk] = np.minimum(g[i:i + blk], cur)
    return (x.T * out).T.astype(np.float32)


def compress(x, thresh=-18.0, ratio=3.0, attack=0.01, release=0.15, makeup=0.0):
    e = np.max(np.abs(stereo(x)), axis=1)
    aa, ar = np.exp(-1 / (attack * SR)), np.exp(-1 / (release * SR))
    # envelope follower (blockwise)
    blk = 32
    n = len(e) // blk + 1
    eb = np.pad(e, (0, n * blk - len(e))).reshape(n, blk).max(axis=1)
    env = np.empty(n)
    cur = 0.0
    for i in range(n):
        v = eb[i]
        k = aa ** blk if v > cur else ar ** blk
        cur = k * cur + (1 - k) * v
        env[i] = cur
    env = np.repeat(env, blk)[: len(e)]
    lvl = 20 * np.log10(env + 1e-9)
    over = np.maximum(0.0, lvl - thresh)
    g = db(-over * (1 - 1 / ratio) + makeup)
    return (x.T * g).T.astype(np.float32)


# ---------------------------------------------------------------- shape
def fade(x, fin=0.0, fout=0.0, curve=1.0):
    x = x.copy()
    n1, n2 = int(fin * SR), int(fout * SR)
    if n1 > 0:
        r = (np.arange(n1) / n1) ** curve
        x[:n1] = (x[:n1].T * r).T
    if n2 > 0:
        r = (np.arange(n2, 0, -1) / n2) ** curve
        x[-n2:] = (x[-n2:].T * r).T
    return x


def envelope(x, points):
    """Gain envelope from [(t, gain), ...] (linear between points)."""
    t = np.arange(len(x)) / SR
    g = np.interp(t, [p[0] for p in points], [p[1] for p in points])
    return (x.T * g).T.astype(np.float32)


def loopable(x, xf=1.5):
    """Make a seamless loop: the tail is equal-power crossfaded into the head. Result is len - xf seconds."""
    n = int(xf * SR)
    body = x[n:-n] if len(x) > 3 * n else x[n:len(x) - n]
    head, tail = x[:n], x[-n:]
    t = np.linspace(0, np.pi / 2, n)
    mixed = (tail.T * np.cos(t)).T + (head.T * np.sin(t)).T
    # loop = [head-after-fade ... body ... tail blended into head]; start at body so the join is inside the blend
    return np.concatenate([body, mixed]).astype(np.float32)


def resample(x, ratio):
    """Play faster (ratio > 1: higher and shorter) or slower."""
    if abs(ratio - 1.0) < 1e-4:
        return x.copy()
    n = int(round(len(x) / ratio))
    return signal.resample_poly(x, 1000, int(round(1000 * ratio)), axis=0).astype(np.float32)[:n]


def place(dst, src, t, gain=1.0):
    i = int(t * SR)
    if i >= len(dst) or i + len(src) <= 0:
        return
    if i < 0:
        src, i = src[-i:], 0
    k = min(len(src), len(dst) - i)
    if dst.ndim == 2 and src.ndim == 1:
        src = src[:, None]
    dst[i:i + k] += src[:k] * gain


def pan(x, p):
    """Mono -> stereo, constant power, p in [-1, 1]."""
    a = (p + 1) * np.pi / 4
    m = mono(x)
    return np.stack([m * np.cos(a), m * np.sin(a)], axis=1).astype(np.float32)


# ---------------------------------------------------------------- tone
def _sos(kind, f, order=2):
    f = np.asarray(f, float) / (SR / 2)
    return signal.butter(order, np.clip(f, 1e-5, 0.999), btype=kind, output='sos')


def hp(x, f, order=2):
    return signal.sosfilt(_sos('high', f, order), x, axis=0).astype(np.float32)


def lp(x, f, order=2):
    return signal.sosfilt(_sos('low', f, order), x, axis=0).astype(np.float32)


def bp(x, f1, f2, order=2):
    return signal.sosfilt(_sos('band', [f1, f2], order), x, axis=0).astype(np.float32)


def peq(x, f, gain_db, q=1.0):
    """Peaking EQ (RBJ)."""
    a = 10 ** (gain_db / 40)
    w = 2 * np.pi * f / SR
    al = np.sin(w) / (2 * q)
    b = [1 + al * a, -2 * np.cos(w), 1 - al * a]
    d = [1 + al / a, -2 * np.cos(w), 1 - al / a]
    return signal.lfilter(np.array(b) / d[0], np.array(d) / d[0], x, axis=0).astype(np.float32)


def shelf(x, f, gain_db, high=True):
    a = 10 ** (gain_db / 40)
    w = 2 * np.pi * f / SR
    al = np.sin(w) / 2 * np.sqrt(2)
    c = np.cos(w)
    s = 2 * np.sqrt(a) * al
    if high:
        b = [a * ((a + 1) + (a - 1) * c + s), -2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - s)]
        d = [(a + 1) - (a - 1) * c + s, 2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - s]
    else:
        b = [a * ((a + 1) - (a - 1) * c + s), 2 * a * ((a - 1) - (a + 1) * c), a * ((a + 1) - (a - 1) * c - s)]
        d = [(a + 1) + (a - 1) * c + s, -2 * ((a - 1) + (a + 1) * c), (a + 1) + (a - 1) * c - s]
    return signal.lfilter(np.array(b) / d[0], np.array(d) / d[0], x, axis=0).astype(np.float32)


def saturate(x, drive=1.5):
    return (np.tanh(x * drive) / np.tanh(drive)).astype(np.float32)


# ---------------------------------------------------------------- space
def noise(n, rng, color=0.0):
    """White (0), pink (1) or brown (2) noise, unit RMS."""
    w = rng.standard_normal(n)
    if color:
        f = np.fft.rfftfreq(n, 1 / SR)
        f[0] = f[1]
        w = np.fft.irfft(np.fft.rfft(w) / f ** (color / 2), n)
    return (w / (np.std(w) + 1e-9)).astype(np.float32)


def stadium_ir(rng, rt60=2.6, predelay=0.03, bright=4500.0, slaps=((0.21, 0.35), (0.38, 0.22), (0.61, 0.12))):
    """An open bowl: a long diffuse tail plus the slap-backs off the far stands. Stereo."""
    n = int((rt60 * 1.3 + 1.0) * SR)
    t = np.arange(n) / SR
    out = np.zeros((n, 2), np.float32)
    for c in range(2):
        tail = noise(n, rng) * np.exp(-6.9 * t / rt60)
        tail = lp(hp(tail, 180), bright)
        tail[: int(predelay * SR)] = 0
        out[:, c] = tail * 0.05
        for d, g in slaps:
            i = int((d + (0.007 if c else 0.0)) * SR)
            k = int(0.02 * SR)
            out[i:i + k, c] += lp(noise(k, rng), 3000) * np.hanning(k) * g * 0.6
    return out


def convolve(x, ir, wet=0.3, dry=1.0):
    x = stereo(x)
    w = np.stack([signal.fftconvolve(x[:, c], ir[:, c]) for c in range(2)], axis=1)
    out = np.zeros_like(w)
    out[: len(x)] += x * dry
    out += w * wet
    return out.astype(np.float32)


def pa_voice(x, rng, wet=0.5):
    """A voice through the stadium horns: band-limited, a touch of grit, the bowl's slap and tail."""
    v = mono(x)
    v = bp(v, 320, 3600, 3)
    v = peq(v, 1800, 5, 1.2)
    v = saturate(v, 2.2)
    v = compress(v, -20, 4)
    return convolve(v, stadium_ir(rng, rt60=2.2, slaps=((0.19, 0.5), (0.34, 0.33), (0.52, 0.2), (0.80, 0.1))), wet=wet, dry=0.8)


def widen(x, amount=0.3, rng=None):
    """Decorrelate a mono-ish source into stereo with short all-pass style delays."""
    x = stereo(x)
    d = int(0.011 * SR)
    side = np.zeros(len(x), np.float32)
    side[d:] = mono(x)[:-d]
    side = hp(side, 300)
    out = x.copy()
    out[:, 0] += side * amount
    out[:, 1] -= side * amount
    return out
