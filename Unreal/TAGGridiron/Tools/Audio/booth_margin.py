"""How far the booth sits over the rest of the mix while it talks (05 A3, T18: the A1 keeps the announcers ~6 dB over the
field, and lets the crowd come over them on the big play), from one recorded run - no stems needed.

Every voiced line is in the log with its cache key ("TAG booth voice gain ... key pbp1_<hash>", -TagBoothLog), so its own
wav is known: it is found in the recording (cross-correlation near the logged moment), its gain there fitted, and taken
out; what is left is everything else under the voice. Margin = K-weighted loudness of the voice minus that of the rest,
over the line, in LU; grouped by the line's energy.

  Tools/Booth/.venv/Scripts/python.exe Tools/Audio/booth_margin.py D:/TAGAudio/work/<name> [--cache Saved/BoothVoice]
"""
import argparse
import datetime as dt
import math
import os
import re
import sys

import numpy as np
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import loudness as ld  # noqa: E402

GAIN = re.compile(r"TAG booth voice gain ([\d.]+) x ([\d.]+) for e(\d) .* key (\w+)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("base")
    ap.add_argument("--cache", default=os.path.join(HERE, "..", "..", "Saved", "BoothVoice"))
    a = ap.parse_args()
    x, fs = ld.read_wav(a.base + ".wav")
    mono = x.mean(axis=1)
    k = ld.k_weight(mono[:, None], fs)[:, 0]
    rec0, lines = None, []
    for ln in open(a.base + ".log", encoding="utf-8", errors="replace"):
        t = ld.log_time(ln)
        if t is None:
            continue
        if "TAG audio: recording the output" in ln:
            rec0 = t
        m = GAIN.search(ln)
        if m and rec0 is not None:
            lines.append(((t - rec0).total_seconds(), int(m.group(3)), m.group(4)))
    if not lines:
        print("no 'TAG booth voice gain ... key' lines (a -TagBoothLog run of a build that logs the key)")
        return 2
    clips = {}

    def clip(key):
        if key not in clips:
            p = os.path.join(a.cache, key + ".wav")
            if not os.path.exists(p):
                clips[key] = None
            else:
                c, cfs = ld.read_wav(p)
                clips[key] = signal.resample_poly(c[:, 0], fs, cfs)
        return clips[key]

    def find(t, c, before, after):
        n = len(c)
        lo, hi = int((t - before) * fs), int((t + after) * fs) + n
        if lo < 0 or hi > len(mono) or n < fs * 0.3:
            return None
        win = mono[lo:hi]
        cc = signal.fftconvolve(win, c[::-1], mode="valid")
        at = int(np.argmax(cc))
        seg = win[at:at + n]
        return lo + at, seg, float(np.corrcoef(seg, c)[0, 1])

    # the recording's clock against the log's (the recording may start a moment after its log line): the median lag of
    # the lines found well in a wide search
    lags = []
    for t, energy, key in lines[:40]:
        c = clip(key)
        if c is None or t < 4:
            continue
        f = find(t, c, 4.0, 4.0)
        if f and f[2] > 0.6:
            lags.append(f[0] / fs - t)
    lag = float(np.median(lags)) if lags else 0.0
    print("recording vs log: %+.2f s (from %d lines)" % (lag, len(lags)))
    rows = []
    for t, energy, key in lines:
        c = clip(key)
        if c is None or t < 0:
            continue
        n = len(c)
        f = find(t + lag, c, 3.0, 3.0)  # (the recording drifts against the log where the game hitched)
        if f is None or f[2] < 0.5:
            continue  # (not found: a line cut short, or buried)
        lo, seg, _ = f
        at = 0
        g = float(np.dot(seg, c) / (np.dot(c, c) + 1e-12))
        if g <= 0:
            continue
        r = float(np.corrcoef(seg, c)[0, 1])
        kv = ld.k_weight((g * c)[:, None], fs)[:, 0]
        kr = k[lo + at:lo + at + n] - kv
        pv, pr = float(np.mean(kv ** 2)), float(np.mean(kr ** 2))
        if pv <= 0 or pr <= 0:
            continue
        rows.append((energy, 10 * math.log10(pv / pr), r, g))
    if not rows:
        print("no line could be found in the recording")
        return 1
    print("%s: %d voiced lines found (of %d logged)" % (a.base, len(rows), len(lines)))
    print("  %-8s %5s %12s %12s %10s" % ("energy", "lines", "median LU", "p25..p75", "fit corr"))
    for name, sel in (("e0-1", lambda e: e <= 1), ("e2", lambda e: e == 2), ("e3", lambda e: e == 3), ("all", lambda e: True)):
        v = np.array([m for e, m, _, _ in rows if sel(e)])
        cr = np.array([c for e, _, c, _ in rows if sel(e)])
        if len(v):
            print("  %-8s %5d %12.1f %6.1f..%-5.1f %10.2f" % (name, len(v), np.median(v), np.percentile(v, 25), np.percentile(v, 75), np.median(cr)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
