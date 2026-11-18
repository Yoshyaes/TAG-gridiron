"""Render a headless broadcast (BoothScript --jsonl) to one listenable file: every line from the voice cache, on the
booth's own timeline (a line starts at its time, or after the one before it ends), over a stadium crowd bed that
swells on the big calls. For listening tests without the game.

  python render_broadcast.py <lines.jsonl> <cache dir> <out.wav|.mp3> [--from SEC] [--secs SEC]
"""
import argparse, json, os, subprocess, sys
import numpy as np, soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from booth_tts import line_key, SR  # noqa: E402


def crowd(n, rng):
    """A stadium bed: pink-ish noise, band-limited, slowly breathing."""
    from scipy.signal import butter, lfilter
    w = rng.standard_normal(n).astype(np.float32)
    b, a = butter(2, [180 / (SR / 2), 2400 / (SR / 2)], btype='band')
    x = lfilter(b, a, w)
    b2, a2 = butter(1, 600 / (SR / 2))
    x = 0.6 * x + 0.4 * lfilter(b2, a2, w)
    t = np.arange(n) / SR
    breathe = 0.8 + 0.2 * np.sin(2 * np.pi * t / 23.0) * np.sin(2 * np.pi * t / 7.3)
    x = x * breathe
    return (x / (np.sqrt(np.mean(x ** 2)) + 1e-9)).astype(np.float32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('jsonl')
    ap.add_argument('cache')
    ap.add_argument('out')
    ap.add_argument('--from', dest='start', type=float, default=0)
    ap.add_argument('--secs', type=float, default=1e9)
    a = ap.parse_args()
    lines = [json.loads(l) for l in open(a.jsonl, encoding='utf-8')]
    lines = [l for l in lines if a.start <= l['t'] < a.start + a.secs]
    placed, end, missing = [], 0.0, 0
    for l in lines:
        voice = 'pbp' if l['who'] == 'PBP' else 'color'
        p = os.path.join(a.cache, line_key(voice, l['energy'], l['say']) + '.wav')
        if not os.path.exists(p):
            missing += 1
            continue
        y, sr = sf.read(p, dtype='float32')
        t = max(l['t'] - a.start, end + (0.05 if l['kind'] == 'live' else 0.35))
        placed.append((t, y, l))
        end = t + len(y) / SR
    total = int((end + 2.0) * SR)
    voice = np.zeros(total, np.float32)
    swell = np.zeros(total, np.float32)
    for t, y, l in placed:
        i = int(t * SR)
        voice[i:i + len(y)] += y
        if l['energy'] >= 2:  # the crowd rises with the call and lingers
            k = int((len(y) / SR + (4.0 if l['energy'] == 3 else 1.5)) * SR)
            env = np.minimum(1.0, np.arange(k) / (0.4 * SR)) * np.exp(-np.arange(k) / (2.5 * SR))
            swell[i:i + k] = np.maximum(swell[i:i + k], env[: len(swell[i:i + k])] * (1.0 if l['energy'] == 3 else 0.45))
    bed = crowd(total, np.random.default_rng(7)) * (10 ** (-34 / 20)) * (1.0 + 2.5 * swell)
    mix = voice + bed
    mix /= max(1.0, np.max(np.abs(mix)) / 0.95)
    out = a.out
    wav = out if out.endswith('.wav') else out + '.tmp.wav'
    sf.write(wav, mix, SR, subtype='PCM_16')
    if wav != out:
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', wav, '-b:a', '160k', out], check=True)
        os.remove(wav)
    print(f'{out}: {len(placed)} lines, {missing} without audio, {end/60:.1f} min')


if __name__ == '__main__':
    main()
