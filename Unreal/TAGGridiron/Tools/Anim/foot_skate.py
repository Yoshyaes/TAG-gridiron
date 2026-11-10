# Foot skating from a -TagMoveTest -TagMoveFrames=<route>,<from>,<to> log (run with -UseFixedTimeStep -FPS=30):
# per frame the world speed of each foot (ball bones) against the body's; a gait has one foot near rest most of the
# time (the plant), a skating body moves both feet with it. Prints, per 0.5 s: the slower foot's mean speed (the
# planted foot's skate), the body's speed, the clip playing; then the route's totals.
#   python Tools/Anim/foot_skate.py Saved/Logs/mt_frames.log
import re
import sys

rows = []
for l in open(sys.argv[1], encoding="utf-8", errors="replace"):
    m = re.search(r"TAG frame r(\d) (\d+) t=([\d.]+) v=V\(([^)]*)\) footL=V\(([^)]*)\) footR=V\(([^)]*)\).*?mm=(\S+)", l)
    if not m:
        continue
    vec = lambda s: [float((re.search(r"%s=(-?[\d.]+)" % c, s) or [None, "0"])[1]) for c in "XYZ"]  # compact: zeros omitted
    rows.append((float(m.group(3)), vec(m.group(4))[:3], vec(m.group(5))[:3], vec(m.group(6))[:3], m.group(7)))
if not rows:
    sys.exit("no TAG frame rows")
dist = lambda a, b: ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5
bins = {}
tot_plant = tot_body = 0.0
n = 0
for (t0, v0, l0, r0, m0), (t1, v1, l1, r1, m1) in zip(rows, rows[1:]):
    dt = t1 - t0
    if dt <= 0:
        continue
    sl, sr = dist(l1, l0) / dt, dist(r1, r0) / dt
    body = (v1[0] ** 2 + v1[1] ** 2) ** 0.5
    plant = min(sl, sr)
    key = int(t1 * 2) / 2.0
    b = bins.setdefault(key, [0.0, 0.0, 0, {}])
    b[0] += plant
    b[1] += body
    b[2] += 1
    b[3][m1.replace("A_TAG_Loco_", "").replace("M_Neutral_", "N_")] = b[3].get(m1, 0) + 1
    if body > 30:
        tot_plant += plant * dt
        tot_body += body * dt
        n += 1
print("%6s %10s %10s  %s" % ("t", "plant cm/s", "body cm/s", "clip"))
for k in sorted(bins):
    b = bins[k]
    print("%6.1f %10.0f %10.0f  %s" % (k, b[0] / b[2], b[1] / b[2], max(b[3], key=b[3].get)))
print("moving: planted foot travelled %.0f cm while the body travelled %.0f cm (%.0f%%)" % (tot_plant, tot_body, 100 * tot_plant / max(tot_body, 1)))
