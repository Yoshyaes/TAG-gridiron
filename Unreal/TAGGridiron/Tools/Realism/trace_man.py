import sys, numpy as np, pandas as pd
cap, clip, pid, a, b = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
D = pd.read_csv("D:/TAGFullGame/%s/telemetry/c%03d.csv" % (cap, clip), low_memory=False)
g = D[D.pid == pid].sort_values("frame")
g = g.assign(bs=np.hypot(g.wx.diff(), g.wy.diff()), ls=np.hypot(g.flx.diff(), g.fly.diff()), rs=np.hypot(g.frx.diff(), g.fry.diff()))
g = g[(g.frame >= a) & (g.frame <= b)]
pd.set_option("display.width", 250)
cols = ["frame", "state", "bs", "ls", "flz", "rs", "frz", "slr", "sls", "mmeff", "mmstr", "mmang", "mmor", "rro", "voff", "dev", "anim"]
cols = [c for c in cols if c in g.columns]
o = g[cols].copy(); o["anim"] = o["anim"].str[-38:]
print(o.round(1).to_string(index=False))
