# How close the shown motion is to the captures it plays (-TagHlFull telemetry with the dev* / mm* columns).
#   python capture_fidelity.py <telemetry dir> [out.json]
# dev   = mean distance (cm) over eleven joints between the capture's pose as played and the pose shown, per man per frame
# dfb.. = what each runtime layer moved: football layer (carry / reach / look / stance hand / throw), orientation warp,
#         stride warp, lean, crossfade (the blend from one capture to the next), foot lock
# mmeff = ground speed the playing clip's feet cover (cm/s, after the play rate); mmstr = stride scale; mmang = angle from
#         the clip's travel to the man's (deg, before the clamp); mmor = orientation warp applied (deg)
import glob, json, os, sys
import numpy as np
import pandas as pd

TD = sys.argv[1]
OUT = sys.argv[2] if len(sys.argv) > 2 else None
YD = 0.9144
files = sorted(glob.glob(os.path.join(TD, "c[0-9][0-9][0-9].csv")))
D = pd.concat([pd.read_csv(f, low_memory=False).assign(clip=int(os.path.basename(f)[1:4])) for f in files], ignore_index=True)
if "dev" not in D.columns:
    sys.exit("no dev column: capture made before the fidelity telemetry")
D["anim"] = D["anim"].fillna("")
D["mode"] = D["anim"].str.split(":").str[0]
D["asset"] = D["anim"].str.split(":").str[1].fillna("").str.split("@").str[0]
# the man's speed from where he actually is frame to frame (the sim's velocity field goes stale once the play is dead:
# positions freeze or the director walks the men, and svx / svy keep their last live value)
D = D.sort_values(["clip", "pid", "frame"]).reset_index(drop=True)
g = D.groupby(["clip", "pid"])
step = np.hypot(g.wx.diff(), g.wy.diff()) / 100 * 30
step[g.frame.diff() != 1] = np.nan
D["v"] = step.groupby([D["clip"], D["pid"]]).transform(lambda s: s.rolling(5, center=True, min_periods=2).mean()).fillna(0.0)  # m/s
V = D[(D.hidden == 0) & D.state.isin(["presnap", "live", "post"]) & (D["mode"] != "replay") & (D["mode"] != "")]
LAYERS = [("dfb", "football layer"), ("dor", "orientation warp"), ("dst", "stride warp"), ("dln", "lean"), ("dxf", "crossfade"), ("dft", "foot lock")]
R = {"frames": int(len(V))}


def dist(s):
    return {"mean": round(float(s.mean()), 2), "median": round(float(s.median()), 2), "p90": round(float(s.quantile(0.9)), 2),
            "p99": round(float(s.quantile(0.99)), 2), "over_5cm": round(float((s > 5).mean()), 3), "over_10cm": round(float((s > 10).mean()), 3)}


R["dev_cm"] = dist(V.dev)
R["dev_by_state"] = {k: dist(g.dev) for k, g in V.groupby("state")}
R["dev_by_mode"] = {k: dict(dist(g.dev), share=round(len(g) / len(V), 3)) for k, g in V.groupby("mode")}
R["layer_mean_cm"] = {name: round(float(V[c].mean()), 2) for c, name in LAYERS}
R["layer_active_share"] = {name: round(float((V[c] > 0.5).mean()), 3) for c, name in LAYERS}
R["layer_mean_when_active_cm"] = {name: round(float(V.loc[V[c] > 0.5, c].mean()), 2) if (V[c] > 0.5).any() else 0.0 for c, name in LAYERS}
live = V[V.state == "live"]
R["layer_mean_cm_live"] = {name: round(float(live[c].mean()), 2) for c, name in LAYERS}
# who the football layer bends most
fb = V[V.dfb > 0.5]
R["football_layer_by"] = {
    "ball_carrier": dist(fb[fb.ball == 1].dfb) if (fb.ball == 1).any() else None,
    "others": dist(fb[fb.ball == 0].dfb) if (fb.ball == 0).any() else None,
    "by_sim_anim": {k: [int(len(g)), round(float(g.dfb.mean()), 1)] for k, g in fb.groupby("sanim") if len(g) > 300},
}
wa = V.groupby("asset").agg(n=("dev", "size"), dev=("dev", "mean"), fb=("dfb", "mean"), xf=("dxf", "mean"), ft=("dft", "mean")).query("n >= 150")
R["worst_assets"] = wa.sort_values("dev", ascending=False).head(15).round(2).reset_index().to_dict("records")

# motion matching: how well the playing clip's travel matches the man's
M = V[(V["mode"] == "mm") & (V.mmeff > 60) & (V.v > 0.6)].copy()
if len(M):
    M["ratio"] = M.v * 100 / M.mmeff               # sim speed over what the clip's feet cover
    M["resid"] = (M.ratio / M.mmstr - 1).abs()      # what the stride warp leaves
    M["ang_left"] = (M.mmang - M.mmor).abs()        # what the orientation warp leaves
    # ground the planted foot must slide per second: speed error + direction error
    M["skate"] = np.hypot(M.v * (1 - 1 / (M.ratio / M.mmstr)).abs().clip(0, 1), 2 * M.v * np.sin(np.radians(M.ang_left.clip(0, 180)) / 2))
    R["mm"] = {
        "frames": int(len(M)),
        "speed_ratio": {"median": round(float(M.ratio.median()), 3), "p10": round(float(M.ratio.quantile(0.1)), 3), "p90": round(float(M.ratio.quantile(0.9)), 3),
                        "outside_0.7_1.35": round(float(((M.ratio < 0.7) | (M.ratio > 1.35)).mean()), 3)},
        "stride_scale": {"median": round(float(M.mmstr.median()), 3), "at_clamp": round(float(((M.mmstr <= 0.701) | (M.mmstr >= 1.299)).mean()), 3)},
        "angle_deg": {"abs_median": round(float(M.mmang.abs().median()), 1), "abs_p90": round(float(M.mmang.abs().quantile(0.9)), 1),
                      "over_40": round(float((M.mmang.abs() > 40).mean()), 3)},
        "angle_left_deg": {"median": round(float(M.ang_left.median()), 1), "p90": round(float(M.ang_left.quantile(0.9)), 1), "over_15": round(float((M.ang_left > 15).mean()), 3)},
        "skate_m_s": {"median": round(float(M.skate.median()), 2), "p90": round(float(M.skate.quantile(0.9)), 2), "over_1": round(float((M.skate > 1.0).mean()), 3)},
    }
    R["mm"]["by_state"] = {k: {"frames": int(len(g)), "ratio_outside": round(float(((g.ratio < 0.7) | (g.ratio > 1.35)).mean()), 3),
                               "angle_over_40": round(float((g.mmang.abs() > 40).mean()), 3), "skate_over_1": round(float((g.skate > 1.0).mean()), 3),
                               "skate_median": round(float(g.skate.median()), 2)} for k, g in M.groupby("state")}
    wm = M.groupby("asset").agg(n=("skate", "size"), skate=("skate", "mean"), ang=("ang_left", "mean"), ratio=("ratio", "median")).query("n >= 150")
    R["mm"]["worst_assets"] = wm.sort_values("skate", ascending=False).head(15).round(2).reset_index().to_dict("records")
# moving while a clip that is not motion matched plays (one-shots, loops, stances): the sim carries the man, the clip stays put
NM = V[(V["mode"].isin(["one", "clip"])) & (V.down == 0)]
R["non_mm_moving"] = {"share_of_frames": round(len(NM) / len(V), 3), "moving_over_1ms": round(float((NM.v > 1.0).mean()), 3),
                      "moving_over_2.5ms": round(float((NM.v > 2.5).mean()), 3)}
nm = NM[NM.v > 1.0].groupby("asset").agg(n=("v", "size"), v=("v", "mean")).query("n >= 100").sort_values("n", ascending=False).head(20)
R["non_mm_moving"]["top_assets"] = nm.round(2).reset_index().to_dict("records")
print(json.dumps(R, indent=1))
if OUT:
    json.dump(R, open(OUT, "w"), indent=1)
