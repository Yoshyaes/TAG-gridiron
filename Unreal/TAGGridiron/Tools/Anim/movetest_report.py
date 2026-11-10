# Summarize a -TagMoveTest log: slide and gait per route, how often the pick changed, which clips were picked,
# and (with -TagMMTrace) the traced player's picks with cost, clip speed and rate multiplier.
#   python Tools/Anim/movetest_report.py Saved/Logs/movetest.log
import re
import sys
from collections import Counter

log = open(sys.argv[1], encoding="utf-8", errors="replace").read().splitlines()
strip = lambda s: re.sub(r"^\[[^\]]*\]\[ *\d+\]LogTemp: Display: ", "", s)
for l in log:
    if "TAG slide r" in l or "TAG gait r" in l or "TAG perf" in l:
        print(strip(l))
field = lambda s, k, cast=str: (lambda m: cast(m.group(1)) if m else None)(re.search(r"\b%s=([^\s|]+)" % k, s))
for r in range(4):
    rows = [strip(l) for l in log if "TAG move r%d " % r in l]
    picks = [field(s, "mm") for s in rows]
    switches = sum(1 for a, b in zip(picks, picks[1:]) if a != b)
    muls = [field(s, "mul", float) for s in rows if field(s, "mul")]
    clip = [(field(s, "v", float), field(s, "clip", float)) for s in rows if field(s, "clip")]
    off = [abs(v - c) for v, c in clip if v and c and v > 60 and c > 60]
    print("r%d: %d samples, %d pick changes, |v - clip speed| mean %.0f cm/s over %d, mul %.2f-%.2f" % (
        r, len(rows), switches, sum(off) / max(len(off), 1), len(off), min(muls or [1]), max(muls or [1])))
    sl = [field(s, "slide") for s in rows]
    raw = [float(x.split("/")[0]) if x else None for x in sl]
    d = [("+%.0f" % (b - a)) if a is not None and b is not None else "" for a, b in zip([raw[0]] + raw, raw)]
    print("    " + " ".join("%s:%s%s" % (field(s, "t"), (field(s, "mm") or "").replace("A_TAG_Loco_", "").replace("M_Neutral_", "N_"), d[i]) for i, s in enumerate(rows)))
tr = [strip(l) for l in log if "TAG mm trace" in l]
if tr:
    print("trace: %d rows" % len(tr))
    prev = None
    for s in tr:
        mm = field(s, "mm")
        if mm != prev or float(field(s, "t")) % 1.0 < 0.11:
            print("  t=%s |v|=%s set=%s mm=%s at=%s cost=%s cont=%s want=%s clip=%s mul=%s" % tuple(field(s, k) for k in ("t", "|v|", "set", "mm", "at", "cost", "cont", "want", "clip", "mul")))
        prev = mm
    costs = Counter()
    for s in tr:
        c = field(s, "cost", float)
        if c is not None and c < 1e6:
            costs[field(s, "mm")] += 1
