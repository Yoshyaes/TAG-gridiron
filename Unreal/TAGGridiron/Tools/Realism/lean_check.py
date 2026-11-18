# Does the shown body lean into its acceleration the way a body must? (-TagHlFull telemetry with pvx/pvy/hdx/hdy)
#   python lean_check.py <telemetry dir> [label]
# A man accelerating at a keeps his centre of mass ahead of his feet by tan(tilt) = a / g. The captures carry that lean
# for their own accelerations; the game adds a procedural lean on top (AAthlete::Drive, -TagNoLean turns it off). This
# measures the shown tilt (feet midpoint -> head, per metre of height) along the acceleration, binned by acceleration:
# slope 1.0 = physical. Compare a run with the lean against one without.
import glob, os, sys
import numpy as np
import pandas as pd

TD = sys.argv[1]
LABEL = sys.argv[2] if len(sys.argv) > 2 else os.path.basename(os.path.dirname(TD.rstrip("/\\")))
files = sorted(glob.glob(os.path.join(TD, "c[0-9][0-9][0-9].csv")))
D = pd.concat([pd.read_csv(f, low_memory=False).assign(clip=int(os.path.basename(f)[1:4])) for f in files], ignore_index=True)
if "hdx" not in D.columns:
    sys.exit("no hdx column")
D["mode"] = D["anim"].fillna("").str.split(":").str[0]
D = D[(D.hidden == 0) & (D.state == "live") & (D.down == 0) & D["mode"].isin(["mm", "one", "clip"])].copy()
DT = 1 / 30.0
out = []
for (c, p), g in D.groupby(["clip", "pid"]):
    if len(g) < 20:
        continue
    g = g.sort_values("frame")
    fr = g.frame.values
    if (np.diff(fr) != 1).any():
        continue
    x, y = g.wx.values / 100, g.wy.values / 100
    k = 6  # 0.2 s windows
    vx = (x[k:] - x[:-k]) / (k * DT)
    vy = (y[k:] - y[:-k]) / (k * DT)
    ax = (vx[k:] - vx[:-k]) / (k * DT)
    ay = (vy[k:] - vy[:-k]) / (k * DT)
    sl = slice(k, len(x) - k)  # centred on the acceleration sample
    fx = (g.flx.values + g.frx.values) / 2
    fy = (g.fly.values + g.fry.values) / 2
    h = np.maximum(g.hdz.values - np.minimum(g.flz.values, g.frz.values), 50)
    tx = ((g.hdx.values - fx) / h)[sl]
    ty = ((g.hdy.values - fy) / h)[sl]
    # smooth the tilt over the same 0.4 s the acceleration spans (a stride swings the feet midpoint)
    ker = np.ones(2 * k + 1) / (2 * k + 1)
    if len(tx) < 2 * k + 2:
        continue
    tx = np.convolve(tx, ker, mode="same")
    ty = np.convolve(ty, ker, mode="same")
    a = np.hypot(ax, ay)
    vmid = np.hypot(vx[k // 2: k // 2 + len(ax)], vy[k // 2: k // 2 + len(ax)]) if len(vx) >= k // 2 + len(ax) else np.zeros(len(ax))
    ok = a > 0.5
    along = (tx * ax + ty * ay) / np.maximum(a, 1e-6)
    md = g["mode"].values[sl]
    for i in np.nonzero(ok)[0][k:-k]:
        out.append((a[i], along[i], md[i], vmid[i] if i < len(vmid) else 0.0))
T = pd.DataFrame(out, columns=["a", "tilt", "mode", "v"])
print("%s: %d samples" % (LABEL, len(T)))
bins = [0.5, 1.5, 2.5, 3.5, 4.5, 6, 8, 12]
T["bin"] = pd.cut(T.a, bins)
print("accel m/s2    n     tilt along accel (tan)   physical a/g   ratio")
for b, g in T.groupby("bin", observed=True):
    phys = g.a.mean() / 9.81
    print("%-12s %6d   %7.3f                 %6.3f        %5.2f" % (b, len(g), g.tilt.mean(), phys, g.tilt.mean() / phys))
for md, g in T.groupby("mode"):
    if len(g) > 500:
        s = np.polyfit(g.a / 9.81, g.tilt, 1)
        print("mode %-5s n %6d  slope %.2f  intercept %.3f" % (md, len(g), s[0], s[1]))
s = np.polyfit(T.a / 9.81, T.tilt, 1)
print("all        n %6d  slope %.2f  intercept %.3f   (slope 1.0 = the physical lean)" % (len(T), s[0], s[1]))
