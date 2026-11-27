"""A listening file from one recorded run (run_test.sh): the moments worth hearing, cut out by the run's log and laid end
to end with a short gap - the pregame open, the first replay package, the first penalty (with -TagBoothRef: the booth's
referee), the first touchdown call. Prints where each one is in the file.

  Tools/Booth/.venv/Scripts/python.exe Tools/Audio/listen_cut.py D:/TAGAudio/work/<name> [<name2> ...] D:/TAGAudio/listen/<out>.mp3
      (reads <name>.wav and <name>.log; each moment from the first run that has it)
"""
import datetime as dt
import os
import re
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audiolib as al  # noqa: E402

STAMP = re.compile(r"^\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):(\d{3})\]")


def stamp(line):
    m = STAMP.match(line)
    if not m:
        return None
    y, mo, d, h, mi, s, ms = (int(v) for v in m.groups())
    return dt.datetime(y, mo, d, h, mi, s, ms * 1000)


def scan(base):
    """The moments of one run: name -> (start, end) seconds into its recording."""
    rec0, marks = None, {}
    seen_start = open_on = False  # (the recording starts on the match's first frame on screen: the open is its start)
    whoosh, wipes = [], []  # (the replay in and out; the wipe between two angles of a package)
    for ln in open(base + ".log", encoding="utf-8", errors="replace"):
        t = stamp(ln)
        if t is None:
            continue
        if "TAG audio: recording the output" in ln:
            rec0 = t
            open_on = open_on or seen_start
        if "TAG audio: match start" in ln:
            seen_start = True
        if rec0 is None:
            continue
        s = (t - rec0).total_seconds()
        if "TAG audio: match start" in ln:
            marks.setdefault("open", (s - 2.0, s + 30.0))
        elif " ui UiWhoosh 0.70" in ln:
            whoosh.append(s)
        elif " ui UiWhoosh 0.35" in ln:
            wipes.append(s)
        elif "TAG booth " in ln and " REF " in ln:
            marks.setdefault("penalty (referee)", (s - 9.0, s + 9.0))
        elif "PENALTY" in ln or ("TAG audio" in ln and " Ref_" in ln):
            marks.setdefault("penalty", (s - 6.0, s + 10.0))
        elif "TAG booth " in ln and "[voice]" in ln and "Touchdown" in ln:
            marks.setdefault("touchdown", (s - 10.0, s + 14.0))
    if open_on:
        marks.setdefault("open", (0.0, 32.0))
    # a replay package: the whoosh in and the whoosh out (the stage change both ways); one with a wipe (two angles) first
    pairs = [(a, b) for a, b in zip(whoosh[::2], whoosh[1::2]) if 3.0 < b - a < 40.0]
    pairs.sort(key=lambda p: not any(p[0] < w < p[1] for w in wipes))
    if pairs:
        marks["replay package"] = (pairs[0][0] - 3.0, pairs[0][1] + 2.0)
    return marks


def main():
    bases, out = sys.argv[1:-1], sys.argv[-1]
    found = [(b, scan(b)) for b in bases]
    parts, at, audio = [], 0.0, {}
    order = ["open", "replay package", "penalty (referee)", "penalty", "touchdown"]
    if any("penalty (referee)" in m for _, m in found):
        order.remove("penalty")
    for name in order:
        src = next(((b, m[name]) for b, m in found if name in m), None)
        if src is None:
            print("  (no %s in these runs)" % name)
            continue
        base, (a, b) = src
        if base not in audio:
            audio[base] = al.load(base + ".wav")
        seg = al.cut(audio[base], max(0.0, a), max(0.0, b))
        if len(seg) < al.SR:
            continue
        print("%6.1f s  %-18s (%s %.1f-%.1f s)" % (at, name, os.path.basename(base), a, b))
        parts += [al.fade(seg, 0.2, 0.4), al.silence(1.2)]
        at += len(seg) / al.SR + 1.2
    if not parts:
        print("nothing to cut")
        return 1
    tmp = out + ".tmp.wav"
    al.save(tmp, np.concatenate(parts))
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", tmp, "-b:a", "192k", out], check=True)
    os.remove(tmp)
    print("-> %s (%.0f s)" % (out, at))
    return 0


if __name__ == "__main__":
    sys.exit(main())
