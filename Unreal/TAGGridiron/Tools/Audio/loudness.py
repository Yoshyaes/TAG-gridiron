"""Loudness harness for the game's mix (Research/NextLevel/05 A4; targets T21).

Measures a recording of the master mix (default Saved/AudioRec/tag_audio.wav, written by -TagAudioRec=<s>) and prints
PASS/FAIL per target:
  integrated loudness   -24 +-2 LUFS   (ITU-R BS.1770-4 gating; pyloudnorm, MIT)        TARGET
  true peak             <= -1 dBTP     (4x oversampled, scipy resample_poly)             TARGET
  loudness range (LRA)  baseline only  (EBU Tech 3342: 3 s short-term, -70 / -20 LU gates, P95 - P10)
  max momentary / short-term, sample peak, share of digital silence                      info
  per stem (when stem recordings exist) integrated / true peak / LRA                     info
  share of time the booth's voice is on (from a -TagBoothLog log; estimated from word counts)   info
It also prints the project's own audiolib.lufs next to pyloudnorm's figure (05 open question 3).

    Tools/Booth/.venv/Scripts/python.exe Tools/Audio/loudness.py [mix.wav] [--log game.log] [--stems DIR]
        [--skip 5] [--secs 600] [--target -24 --tol 2 --tp -1] [--portable] [--json out.json] [--record baseline.tsv]

Record a mix first (silent, offscreen, nothing reaches the speakers; see memory game-audio):
    bash Tools/Audio/run_test.sh loud_s7 600 -TagWeatherSeed=7 -TagBoothLog
    -> D:/TAGAudio/work/loud_s7.wav + loud_s7.log; then: loudness.py D:/TAGAudio/work/loud_s7.wav --log D:/TAGAudio/work/loud_s7.log
Exit codes: 0 every target met, 1 a target missed (or the file is digital silence), 2 bad input.
"""
import argparse
import datetime as dt
import glob
import json
import math
import os
import re
import sys

import numpy as np
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_WAV = os.path.join(PROJ, "Saved", "AudioRec", "tag_audio.wav")

# T21 (05-audio-commentary.md): console -24 +-2 LUFS, portable -18 +-2, true peak -1 dBTP
TARGET_LUFS, TOL_LU, TARGET_TP = -24.0, 2.0, -1.0
PORTABLE_LUFS = -18.0
WORDS_PER_SEC = 2.6      # booth speech rate for the voice-on estimate (about 156 words a minute)


# ------------------------------------------------------------------ reading
def read_wav(path):
    try:
        import soundfile as sf
        x, sr = sf.read(path, dtype="float64", always_2d=True)
        return x, sr
    except ImportError:
        from scipy.io import wavfile
        sr, x = wavfile.read(path)
        if x.dtype.kind == "i":
            x = x.astype(np.float64) / float(np.iinfo(x.dtype).max)
        elif x.dtype.kind == "u":
            x = (x.astype(np.float64) - 128.0) / 128.0
        x = np.asarray(x, np.float64)
        return (x[:, None] if x.ndim == 1 else x), sr


# ------------------------------------------------------------------ BS.1770 pieces
def _biquad(kind, fs, fc, q, gain_db=0.0):
    """RBJ biquads as pyloudnorm builds the K-weighting (equal to the BS.1770 table at 48 kHz)."""
    A = 10 ** (gain_db / 40.0)
    w0 = 2 * math.pi * fc / fs
    alpha = math.sin(w0) / (2 * q)
    c = math.cos(w0)
    if kind == "high_shelf":
        b = [A * ((A + 1) + (A - 1) * c + 2 * math.sqrt(A) * alpha), -2 * A * ((A - 1) + (A + 1) * c),
             A * ((A + 1) + (A - 1) * c - 2 * math.sqrt(A) * alpha)]
        a = [(A + 1) - (A - 1) * c + 2 * math.sqrt(A) * alpha, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - 2 * math.sqrt(A) * alpha]
    else:  # high pass
        b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]
        a = [1 + alpha, -2 * c, 1 - alpha]
    return np.array(b) / a[0], np.array(a) / a[0]


def k_weight(x, fs):
    b1, a1 = _biquad("high_shelf", fs, 1681.974450955533, 0.7071752369554196, 3.999843853973347)
    b2, a2 = _biquad("high_pass", fs, 38.13547087602444, 0.5003270373238773)
    return signal.lfilter(b2, a2, signal.lfilter(b1, a1, x, axis=0), axis=0)


def block_power(k, fs, win, hop):
    """Channel-summed mean square of K-weighted audio in windows of win s every hop s (L/R/C weight 1)."""
    sub = int(round(fs * hop))
    n = k.shape[0] // sub
    if n == 0:
        return np.zeros(0)
    sq = np.square(k[: n * sub]).reshape(n, sub, k.shape[1]).mean(axis=1).sum(axis=1)  # per hop, summed over channels
    m = int(round(win / hop))
    if n < m:
        return np.zeros(0)
    c = np.concatenate([[0.0], np.cumsum(sq)])
    return (c[m:] - c[:-m]) / m


def lufs_of(p):
    return -0.691 + 10 * np.log10(np.maximum(p, 1e-20))


def integrated_own(k, fs):
    p = block_power(k, fs, 0.4, 0.1)
    if not len(p):
        return float("-inf")
    p = p[lufs_of(p) > -70]
    if not len(p):
        return float("-inf")
    rel = lufs_of(p.mean()) - 10
    p = p[lufs_of(p) > rel]
    return float(lufs_of(p.mean())) if len(p) else float("-inf")


def lra(k, fs):
    """EBU Tech 3342: short-term (3 s) loudness every 0.1 s, absolute gate -70 LUFS, relative gate -20 LU below the
    power mean of what passed, LRA = P95 - P10. Returns (lra, p10, p95)."""
    p = block_power(k, fs, 3.0, 0.1)
    if not len(p):
        return float("nan"), float("nan"), float("nan")
    l = lufs_of(p)
    p = p[l > -70]
    if not len(p):
        return float("nan"), float("nan"), float("nan")
    rel = lufs_of(p.mean()) - 20
    l = lufs_of(p)
    l = l[l > rel]
    if not len(l):
        return float("nan"), float("nan"), float("nan")
    lo, hi = np.percentile(l, 10), np.percentile(l, 95)
    return float(hi - lo), float(lo), float(hi)


def true_peak_db(x, os_factor=4, chunk_s=10.0, fs=48000):
    """Max |sample| after 4x oversampling (resample_poly, chunked with overlap so a long recording fits in memory)."""
    n = x.shape[0]
    step = int(chunk_s * fs)
    pad = 2048
    peak = 0.0
    for s in range(0, n, step):
        a, b = max(0, s - pad), min(n, s + step + pad)
        y = signal.resample_poly(x[a:b], os_factor, 1, axis=0)
        lo = (s - a) * os_factor
        hi = lo + (min(n, s + step) - s) * os_factor
        if hi > lo:
            peak = max(peak, float(np.max(np.abs(y[lo:hi]))))
    return 20 * math.log10(peak) if peak > 0 else float("-inf")


def measure(x, fs):
    k = k_weight(x, fs)
    out = {"seconds": x.shape[0] / fs, "rate": fs, "channels": x.shape[1]}
    try:
        import pyloudnorm as pyln
        out["integrated"] = float(pyln.Meter(fs).integrated_loudness(x))
        try:
            from importlib.metadata import version
            out["meter"] = "pyloudnorm %s" % version("pyloudnorm")
        except Exception:
            out["meter"] = "pyloudnorm"
    except ImportError:
        out["integrated"] = integrated_own(k, fs)
        out["meter"] = "own BS.1770 (pyloudnorm not installed: use Tools/Booth/.venv/Scripts/python.exe)"
    out["integrated_own"] = integrated_own(k, fs)
    out["lra"], out["st_p10"], out["st_p95"] = lra(k, fs)
    mom = block_power(k, fs, 0.4, 0.1)
    st = block_power(k, fs, 3.0, 0.1)
    out["max_momentary"] = float(lufs_of(mom.max())) if len(mom) else float("-inf")
    out["max_short_term"] = float(lufs_of(st.max())) if len(st) else float("-inf")
    sp = float(np.max(np.abs(x))) if x.size else 0.0
    out["sample_peak"] = 20 * math.log10(sp) if sp > 0 else float("-inf")
    out["true_peak"] = true_peak_db(x, fs=fs)
    frame = int(fs * 0.1)
    nf = x.shape[0] // frame
    if nf:
        blocks = np.abs(x[: nf * frame]).reshape(nf, frame, -1).max(axis=(1, 2))
        out["silent_share"] = float(np.mean(blocks < 1e-6))
    else:
        out["silent_share"] = 1.0
    out["_st"] = st  # (for the voice windows; not printed)
    return out


def audiolib_lufs(x, fs):
    """The pipeline's own meter (Tools/Audio/audiolib.lufs, 48 kHz coefficients) for the cross-check."""
    if fs != 48000:
        return None
    sys.path.insert(0, HERE)
    try:
        import audiolib
        return float(audiolib.lufs(x.astype(np.float32)))
    except Exception:
        return None
    finally:
        sys.path.pop(0)


# ------------------------------------------------------------------ the booth's voice from the log
STAMP = re.compile(r"^\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):(\d{3})\]")


def log_time(line):
    m = STAMP.match(line)
    if not m:
        return None
    y, mo, d, h, mi, s, ms = (int(v) for v in m.groups())
    return dt.datetime(y, mo, d, h, mi, s, ms * 1000)


def voice_windows(log_path, rec_secs):
    """[(start, end)] seconds into the recording when a booth line was voiced (end estimated from its word count)."""
    rec0 = None
    lines = []
    with open(log_path, encoding="utf-8", errors="replace") as fh:
        for ln in fh:
            if "TAG audio: recording the output" in ln:
                rec0 = log_time(ln)
            elif "TAG booth " in ln and "[voice]" in ln:
                t = log_time(ln)
                text = ln.split("|", 1)[1].strip() if "|" in ln else ""
                if t:
                    lines.append((t, len(text.split())))
    if rec0 is None:
        return None, len(lines)
    win = []
    for t, words in lines:
        a = (t - rec0).total_seconds()
        b = a + 0.3 + words / WORDS_PER_SEC
        if b > 0 and a < rec_secs:
            win.append((max(0.0, a), min(rec_secs, b)))
    return win, len(lines)


# ------------------------------------------------------------------ report
def fmt(v, unit=""):
    if v is None or (isinstance(v, float) and (math.isnan(v) or math.isinf(v))):
        return "n/a"
    return "%.1f%s" % (v, unit)


def find_stems(wav, stems_dir):
    if stems_dir:
        return sorted(glob.glob(os.path.join(stems_dir, "*.wav")))
    d = os.path.dirname(os.path.abspath(wav))
    base = os.path.splitext(os.path.basename(wav))[0]
    found = sorted(glob.glob(os.path.join(d, "stems", "*.wav"))) + sorted(glob.glob(os.path.join(d, base + "_*.wav")))
    return [f for f in found if os.path.abspath(f) != os.path.abspath(wav)]


def main():
    ap = argparse.ArgumentParser(description="Measure a recorded TAG mix against the loudness targets (05 A4 / T21).")
    ap.add_argument("wav", nargs="?", default=DEFAULT_WAV, help="the master-mix recording (default Saved/AudioRec/tag_audio.wav)")
    ap.add_argument("--log", help="the run's log (-abslog): booth voice share from its -TagBoothLog lines")
    ap.add_argument("--stems", help="folder of per-stem wavs (default: <wav dir>/stems/*.wav or <name>_*.wav)")
    ap.add_argument("--skip", type=float, default=0.0, help="seconds to leave out at the start")
    ap.add_argument("--secs", type=float, default=0.0, help="measure at most this many seconds (after --skip)")
    ap.add_argument("--target", type=float, default=TARGET_LUFS, help="integrated target LUFS (default -24)")
    ap.add_argument("--tol", type=float, default=TOL_LU, help="tolerance LU (default 2)")
    ap.add_argument("--tp", type=float, default=TARGET_TP, help="true peak ceiling dBTP (default -1)")
    ap.add_argument("--portable", action="store_true", help="portable target (-18 LUFS)")
    ap.add_argument("--json", help="write the numbers here")
    ap.add_argument("--record", help="append one TSV row (date, file, numbers) to this baseline file")
    a = ap.parse_args()
    if a.portable:
        a.target = PORTABLE_LUFS

    if not os.path.isfile(a.wav):
        print("no recording at %s" % a.wav)
        print("record one: bash Tools/Audio/run_test.sh <name> 600 -TagWeatherSeed=7 -TagBoothLog  (-> D:/TAGAudio/work/<name>.wav)")
        return 2
    try:
        x, fs = read_wav(a.wav)
    except Exception as e:
        print("cannot read %s: %s" % (a.wav, e))
        return 2
    s0 = int(a.skip * fs)
    s1 = x.shape[0] if a.secs <= 0 else min(x.shape[0], s0 + int(a.secs * fs))
    x = x[s0:s1]
    if x.shape[0] < int(fs * 3):
        print("%s: under 3 s of audio after --skip/--secs" % a.wav)
        return 2

    m = measure(x, fs)
    al = audiolib_lufs(x, fs)
    print("%s" % a.wav)
    print("  %.1f s, %d Hz, %d ch, meter: %s" % (m["seconds"], fs, m["channels"], m["meter"]))
    rows = []
    silent = m["silent_share"] > 0.98 or math.isinf(m["integrated"])
    ok_i = (not silent) and abs(m["integrated"] - a.target) <= a.tol
    ok_tp = (not silent) and m["true_peak"] <= a.tp
    rows.append(("integrated loudness", fmt(m["integrated"], " LUFS"), "%.0f +-%.0f LUFS" % (a.target, a.tol), "PASS" if ok_i else "FAIL"))
    rows.append(("true peak (4x)", ("%.2f dBTP" % m["true_peak"]) if not math.isinf(m["true_peak"]) else "n/a", "<= %.1f dBTP" % a.tp, "PASS" if ok_tp else "FAIL"))
    rows.append(("loudness range", fmt(m["lra"], " LU"), "baseline (P10 %s, P95 %s)" % (fmt(m["st_p10"]), fmt(m["st_p95"])), "INFO"))
    rows.append(("max momentary", fmt(m["max_momentary"], " LUFS"), "", "INFO"))
    rows.append(("max short-term", fmt(m["max_short_term"], " LUFS"), "", "INFO"))
    rows.append(("sample peak", fmt(m["sample_peak"], " dBFS"), "", "INFO"))
    rows.append(("digital silence", "%.0f %%" % (100 * m["silent_share"]), "", "INFO"))
    rows.append(("own BS.1770 / audiolib", "%s / %s" % (fmt(m["integrated_own"], " LUFS"), fmt(al, " LUFS") if al is not None else "n/a"),
                 "cross-check vs the meter above", "INFO"))

    voice = None
    if a.log:
        if not os.path.isfile(a.log):
            print("  (no log at %s)" % a.log)
        else:
            win, nlines = voice_windows(a.log, m["seconds"] + a.skip)
            if win is None:
                rows.append(("voice on", "n/a", "log has no 'TAG audio: recording' line", "INFO"))
            elif not nlines:
                rows.append(("voice on", "0 lines", "no '[voice]' booth lines (run with -TagBoothLog and the speech service)", "INFO"))
            else:
                mask = np.zeros(int(math.ceil(m["seconds"] * 10)) + 1, bool)
                for s, e in win:
                    s, e = s - a.skip, e - a.skip
                    if e > 0:
                        mask[int(max(0, s) * 10): int(e * 10) + 1] = True
                share = float(mask[: int(m["seconds"] * 10)].mean())
                # short-term loudness while the booth talks vs the rest (3 s windows that end inside / outside)
                st = m["_st"]
                ends = (np.arange(len(st)) + 30)  # (window i covers hop i .. i+29: it ends at hop i + 30)
                inside = mask[np.minimum(ends, len(mask) - 1)]
                on = float(lufs_of(st[inside].mean())) if inside.any() else float("nan")
                off = float(lufs_of(st[~inside].mean())) if (~inside).any() else float("nan")
                voice = {"lines": nlines, "share": share, "st_voice": on, "st_rest": off}
                rows.append(("voice on (estimated)", "%.0f %%" % (100 * share), "%d voiced lines, %.1f words/s" % (nlines, WORDS_PER_SEC), "INFO"))
                rows.append(("short-term voice / rest", "%s / %s" % (fmt(on, " LUFS"), fmt(off, " LUFS")), "power mean of 3 s windows", "INFO"))

    stems = []
    for f in find_stems(a.wav, a.stems):
        try:
            sx, sfs = read_wav(f)
        except Exception as e:
            print("  (stem %s unreadable: %s)" % (f, e))
            continue
        sx = sx[int(a.skip * sfs): (None if a.secs <= 0 else int((a.skip + a.secs) * sfs))]
        if sx.shape[0] < int(sfs * 3):
            continue
        sm = measure(sx, sfs)
        name = os.path.splitext(os.path.basename(f))[0]
        stems.append({"stem": name, "integrated": sm["integrated"], "true_peak": sm["true_peak"], "lra": sm["lra"]})
        rows.append(("stem " + name, "%s, TP %s" % (fmt(sm["integrated"], " LUFS"), fmt(sm["true_peak"], " dBTP")), "LRA %s" % fmt(sm["lra"], " LU"), "INFO"))

    print()
    print("  %-26s %-22s %-36s %s" % ("MEASURE", "VALUE", "TARGET", "RESULT"))
    for r in rows:
        print("  %-26s %-22s %-36s %s" % r)
    print()
    if silent:
        print("  the recording is (almost) digital silence: an offscreen / unfocused game is muted unless it runs with")
        print("  \"-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0\" (run_test.sh passes it)")
    verdict = "PASS" if ok_i and ok_tp else "FAIL"
    print("LOUDNESS %s: integrated %s (target %.0f +-%.0f), true peak %.2f dBTP (ceiling %.1f)" % (
        verdict, fmt(m["integrated"], " LUFS"), a.target, a.tol, m["true_peak"], a.tp))

    result = {k: v for k, v in m.items() if not k.startswith("_")}
    result.update({"file": os.path.abspath(a.wav), "skip": a.skip, "audiolib_lufs": al, "target": a.target, "tol": a.tol, "tp_ceiling": a.tp,
                   "verdict": verdict, "voice": voice, "stems": stems})
    if a.json:
        with open(a.json, "w", encoding="utf-8") as fh:
            json.dump(result, fh, indent=1, default=lambda o: None)
    if a.record:
        new = not os.path.isfile(a.record)
        with open(a.record, "a", encoding="utf-8") as fh:
            if new:
                fh.write("date\tfile\tseconds\tintegrated_lufs\ttrue_peak_dbtp\tlra_lu\tmax_short_term\tverdict\n")
            fh.write("%s\t%s\t%.0f\t%.2f\t%.2f\t%.2f\t%.2f\t%s\n" % (dt.datetime.now().strftime("%Y-%m-%d %H:%M"), os.path.basename(a.wav), m["seconds"],
                                                                 m["integrated"], m["true_peak"], m["lra"], m["max_short_term"], verdict))
    return 0 if verdict == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
