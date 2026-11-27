# Foot skate on the feet the captures themselves hold planted (-TagHlFull telemetry with the plf column).
#   python plant_truth.py <telemetry dir> [out.json]
# plf = feet the capture holds on the turf this frame (FTagRetargetProxy::MeasurePlants: on the turf, and the foot's own
#       motion in the clip plus the clip's root travel under 45 cm/s); slr / sls = how far those feet travelled over the
#       ground this frame as the clip was driven (before the foot lock) and as shown (cm).
# A height-only test ("ball of the foot under 5 cm") counts a walker's swing foot as planted (it clears the turf by 4-6 cm),
# so the older foot_plants numbers of analyze.py overstate the skating; this is the measure to steer by.
import glob, json, os, sys
import numpy as np
import pandas as pd

TD = sys.argv[1]
OUT = sys.argv[2] if len(sys.argv) > 2 else None
files = sorted(glob.glob(os.path.join(TD, "c[0-9][0-9][0-9].csv")))
D = pd.concat([pd.read_csv(f, low_memory=False).assign(clip=int(os.path.basename(f)[1:4])) for f in files], ignore_index=True)
if "plf" not in D.columns:
    sys.exit("no plf column: capture made before the planted-foot verdict")
D["anim"] = D["anim"].fillna("")
D["mode"] = D["anim"].str.split(":").str[0]
D["asset"] = D["anim"].str.split(":").str[1].fillna("").str.split("@").str[0]
D = D.sort_values(["clip", "pid", "frame"]).reset_index(drop=True)
g = D.groupby(["clip", "pid"])
step = np.hypot(g.wx.diff(), g.wy.diff())
step[g.frame.diff() != 1] = np.nan
D["bstep"] = step.fillna(0.0)  # cm the body moved this frame
V = D[(D.hidden == 0) & (D.down == 0) & D.state.isin(["presnap", "live", "post"]) & D["mode"].isin(["mm", "one", "clip"])]
P = V[V.plf > 0]


def block(p):
    n = float(p.plf.sum())
    if n <= 0:
        return None
    per = p.sls / p.plf  # cm per planted foot this frame
    return {"planted_foot_frames": int(n), "raw_cm_s": round(float(p.slr.sum() / n * 30), 1), "shown_cm_s": round(float(p.sls.sum() / n * 30), 1),
            "shown_over_raw": round(float(p.sls.sum() / max(p.slr.sum(), 1e-6)), 3),
            "frames_over_1cm": round(float((per > 1.0).mean()), 3), "frames_over_3cm": round(float((per > 3.0).mean()), 3),
            "body_cm_s": round(float(p.bstep.mean() * 30), 1)}


R = {"frames": int(len(V)), "frames_with_a_planted_foot": round(float((V.plf > 0).mean()), 3), "all": block(P)}
R["moving"] = block(P[P.bstep * 30 > 100])  # the body covering over 1 m/s
R["by_state"] = {k: block(p) for k, p in P.groupby("state")}
R["by_mode"] = {k: block(p) for k, p in P.groupby("mode")}
R["moving_by_mode"] = {k: block(p) for k, p in P[P.bstep * 30 > 100].groupby("mode")}
t = P[P.bstep * 30 > 100].groupby("asset").agg(n=("plf", "sum"), shown=("sls", "sum"), raw=("slr", "sum")).query("n >= 60")
t["shown_cm_s"] = (t.shown / t.n * 30).round(1)
t["raw_cm_s"] = (t.raw / t.n * 30).round(1)
t["cost"] = t.shown  # cm of visible skate in all
R["worst_assets_by_total_skate"] = t.sort_values("cost", ascending=False).head(20)[["n", "raw_cm_s", "shown_cm_s"]].reset_index().to_dict("records")
if "voff" in D.columns:
    R["body_offset_cm"] = {"median": round(float(V.voff.median()), 1), "p90": round(float(V.voff.quantile(0.9)), 1), "p99": round(float(V.voff.quantile(0.99)), 1),
                           "at_55_plus": round(float((V.voff >= 55).mean()), 4),
                           "live_median": round(float(V[V.state == "live"].voff.median()), 1), "live_p90": round(float(V[V.state == "live"].voff.quantile(0.9)), 1)}
print(json.dumps(R, indent=1))
if OUT:
    json.dump(R, open(OUT, "w"), indent=1)
