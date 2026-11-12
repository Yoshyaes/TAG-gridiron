# Full-game telemetry analysis (-TagHighlights -TagHlFull): movement, animation, interactions, AI, physics.
# usage: python analyze.py <telemetry dir> <game log> <out json>
import glob, json, math, os, re, sys
import numpy as np
import pandas as pd

TD, LOG, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
DT = 1 / 30.0
YD = 0.9144          # m per yard
MPH = 2.23694        # mph per m/s

OL = {"LT", "LG", "C", "RG", "RT"}
def group(pos):
    return {"QB": "QB", "HB": "RB", "RB": "RB", "FB": "RB", "WR": "WR", "TE": "TE", "LT": "OL", "LG": "OL", "C": "OL", "RG": "OL",
            "RT": "OL", "OT": "OL", "OG": "OL", "OL": "OL", "DE": "DL", "DT": "DL", "DL": "DL", "NT": "DL", "LB": "LB", "OLB": "LB",
            "MLB": "LB", "ILB": "LB", "CB": "DB", "S": "DB", "FS": "DB", "SS": "DB", "K": "K", "P": "K", "LS": "OL"}.get(pos, pos)

files = sorted(glob.glob(os.path.join(TD, "c[0-9][0-9][0-9].csv")))
frames = []
for f in files:
    c = int(os.path.basename(f)[1:4])
    d = pd.read_csv(f, keep_default_na=True)
    d["clip"] = c
    frames.append(d)
D = pd.concat(frames, ignore_index=True)
D["grp"] = D["pos"].map(group)
D["mode"] = D["anim"].fillna("").str.split(":").str[0]
D["asset"] = D["anim"].fillna("").str.split(":").str[1].fillna("").str.split("@").str[0]
D = D.sort_values(["clip", "pid", "frame"]).reset_index(drop=True)
R = {"clips": len(files), "rows": int(len(D)), "frames": int(D.groupby("clip").frame.nunique().sum())}
R["state_frames"] = D.drop_duplicates(["clip", "frame"]).state.value_counts().to_dict()

# per-player frame-to-frame deltas (same clip, consecutive frames)
g = D.groupby(["clip", "pid"])
for col in ["wx", "wy", "flx", "fly", "frx", "fry", "pz", "sx", "sy", "svx", "svy"]:
    D["d_" + col] = g[col].diff()
D["df"] = g["frame"].diff()
ok = (D.df == 1) & (D.hidden == 0)
D["body_v"] = np.where(ok, np.hypot(D.d_wx, D.d_wy) / DT / 100.0, np.nan)        # m/s, shown actor
D["fl_v"] = np.where(ok, np.hypot(D.d_flx, D.d_fly) / DT / 100.0, np.nan)
D["fr_v"] = np.where(ok, np.hypot(D.d_frx, D.d_fry) / DT / 100.0, np.nan)
D["sim_v"] = np.hypot(D.svx, D.svy) * YD                                          # m/s
D["sim_a"] = np.where(ok, np.hypot(D.d_svx, D.d_svy) * YD / DT, np.nan)

live = D[(D.state == "live") & (D.hidden == 0)]
upright = (D.down == 0) & ~D["mode"].isin(["fall", "ragdoll", "replay", "getup"])

# ---------------------------------------------------------------- movement
# top speed per player per play (sim) and per position group over the game
tops = live.groupby(["clip", "pid", "grp"]).sim_v.max().reset_index()
R["top_speed_mph"] = {k: {"game_max": round(v.max() * MPH, 1), "p90_of_play_max": round(v.quantile(0.9) * MPH, 1),
                          "median_play_max": round(v.median() * MPH, 1)} for k, v in tops.groupby("grp").sim_v}
# acceleration (sim velocity, 3-frame smoothed)
live2 = live.copy()
live2["a_s"] = live2.groupby(["clip", "pid"]).sim_a.transform(lambda s: s.rolling(3, center=True).mean())
R["accel_ms2"] = {k: {"p99": round(v.quantile(0.99), 2), "max": round(v.max(), 2)} for k, v in live2.groupby("grp").a_s}
# shown body vs sim speed
mv = D[ok & upright & (D.sim_v > 1.0) & (D.state == "live")]
R["body_vs_sim_speed_ratio_median"] = round(float((mv.body_v / mv.sim_v).median()), 3)
# planted-foot slide: a foot is in contact when its ball bone is within 6 cm of the turf
for foot, z, v in (("l", "flz", "fl_v"), ("r", "frz", "fr_v")):
    D["c_" + foot] = (D[z] < 6.0)
cont = []
for foot, v in (("l", "fl_v"), ("r", "fr_v")):
    m = D[ok & upright & D["c_" + foot] & (D.body_v > 1.0)]
    cont.append(m[[v, "body_v", "grp", "state", "mode", "asset"]].rename(columns={v: "fv"}))
C = pd.concat(cont)
R["contact_foot"] = {
    "samples": int(len(C)),
    "median_slide_ms": round(float(C.fv.median()), 3),
    "p90_slide_ms": round(float(C.fv.quantile(0.9)), 3),
    "share_over_0.5ms": round(float((C.fv > 0.5).mean()), 3),
    "share_over_1.0ms": round(float((C.fv > 1.0).mean()), 3),
    "slide_over_body": round(float(C.fv.sum() / C.body_v.sum()), 3),
    "by_group_share_over_0.5": {k: round(float((v.fv > 0.5).mean()), 3) for k, v in C.groupby("grp")},
    "by_mode_share_over_0.5": {k: round(float((v.fv > 0.5).mean()), 3) for k, v in C.groupby("mode") if len(v) > 200},
}
worst = C.groupby("asset").agg(n=("fv", "size"), slide=("fv", lambda s: (s > 0.5).mean())).query("n > 300").sort_values("slide", ascending=False)
R["contact_foot"]["worst_assets"] = {a: [int(r.n), round(r.slide, 3)] for a, r in worst.head(12).iterrows()}
# body height while running fast (sprint posture): pelvis, head
fast = D[ok & upright & (D.sim_v > 7.0) & (D.state == "live")]
R["sprint_posture_cm"] = {"pelvis_median": round(float(fast.pz.median()), 1), "head_median": round(float(fast.hdz.median()), 1),
                          "head_under_120_share": round(float((fast.hdz < 120).mean()), 3), "n": int(len(fast))}
low_fast = fast[fast.hdz < 120]
R["sprint_posture_cm"]["low_head_assets"] = low_fast.asset.value_counts().head(8).to_dict()
# feet in the ground, pops / teleports
vis = D[(D.hidden == 0) & upright]
R["ground"] = {"foot_below_-3cm_share": round(float(((vis.flz < -3) | (vis.frz < -3)).mean()), 4),
               "foot_below_-8cm_share": round(float(((vis.flz < -8) | (vis.frz < -8)).mean()), 4),
               "hand_below_-5cm_share": round(float(((vis.hrz < -5) | (vis.hlz < -5)).mean()), 4)}
jumps = D[ok & (np.hypot(D.d_wx, D.d_wy) > 60)]   # > 18 m/s in one frame
R["actor_teleports"] = {"count": int(len(jumps)), "by_state": jumps.state.value_counts().to_dict(),
                        "plays_with_one": int(jumps["clip"].nunique())}
# what the viewer sees: the visible body (midpoint of the feet) jumping > 60 cm in one frame. The actor root can jump
# while the body stays put (a get-up re-seats the root at the pelvis), so the root count above over-reports.
bj = np.hypot((D.d_flx + D.d_frx) / 2, (D.d_fly + D.d_fry) / 2)
# a swinging foot carries the midpoint ~1.5x the body's speed: a pop is a jump the motion cannot explain
# (> 60 cm and > 2.5 x what the sim speed covers in a frame), outside replays (which cut by design)
expect = 2.5 * D.sim_v * 100 * DT
bjumps = D[ok & (bj > 60) & (bj > expect) & D.state.isin(["presnap", "live", "post"]) & (D.frame > 4)]
# a scene reset (the next play set up, everyone re-placed) moves many men in the same frame: not a glitch on one body
mass = bjumps.groupby(["clip", "frame"]).pid.transform("size") >= 5
bjumps = bjumps[~mass]
R["body_pops"] = {"count": int(len(bjumps)), "by_state": bjumps.state.value_counts().to_dict(),
                  "plays_with_one": int(bjumps["clip"].nunique()),
                  "examples": bjumps[["clip", "frame", "pos", "anim"]].head(10).astype(str).values.tolist()}
pp = D[ok & (D.d_pz.abs() > 25) & upright]
R["pelvis_vertical_pops"] = {"count": int(len(pp)), "assets": pp.asset.value_counts().head(6).to_dict()}
# facing vs travel (running fast: yaw vs velocity direction)
fv = D[ok & upright & (D.body_v > 3.0)].copy()
trav = np.degrees(np.arctan2(fv.d_wy, fv.d_wx))
fv["off"] = ((fv.yaw - trav + 180) % 360 - 180).abs()
R["facing_vs_travel"] = {"median_deg": round(float(fv.off.median()), 1), "share_over_45": round(float((fv.off > 45).mean()), 3),
                         "share_backward_over_120": round(float((fv.off > 120).mean()), 3),
                         "backward_by_group": {k: round(float((v.off > 120).mean()), 3) for k, v in fv.groupby("grp")}}

# ---------------------------------------------------------------- animation coverage
A = D[(D.hidden == 0) & (D.asset != "") & (D.asset != "None")]
R["anim"] = {"distinct_assets": int(A.asset.nunique()),
             "mode_share": {k: round(v, 3) for k, v in A["mode"].value_counts(normalize=True).items()},
             "distinct_by_mode": A.groupby("mode").asset.nunique().to_dict(),
             "pack_share": round(float(A.asset.str.contains("Pack").mean()), 3),
             "top_assets": A.asset.value_counts().head(15).to_dict()}
# one-shot / fall variety: how many distinct clips per kind of action
kinds = {"tackle_fall": A[A["mode"] == "fall"], "getup": A[A["mode"] == "getup"], "oneshot": A[A["mode"] == "one"]}
starts = {}
for k, v in kinds.items():
    s = v[v.groupby(["clip", "pid"]).asset.shift() != v.asset]
    starts[k] = {"uses": int(len(s)), "distinct": int(s.asset.nunique()),
                 "most_repeated": s.asset.value_counts().head(5).to_dict()}
R["anim"]["action_variety"] = starts
# same clip on many men at once (clones): frames where >= 4 visible men play the same mm loop
same = A[A["mode"] == "mm"].groupby(["clip", "frame", "asset"]).size()
R["anim"]["frames_with_4plus_same_loop_share"] = round(float((same >= 4).groupby(level=[0, 1]).any().mean()), 3)

# ---------------------------------------------------------------- interactions
# ball in the carrier's hands
held = D[(D.ball == 1) & (D.ballmode == "held") & (D.hidden == 0) & D.bwx.notna() & (D.state == "live")].copy()
dh = np.minimum(np.sqrt((held.bwx - held.hrx) ** 2 + (held.bwy - held.hry) ** 2 + (held.bwz - held.hrz) ** 2),
                np.sqrt((held.bwx - held.hlx) ** 2 + (held.bwy - held.hly) ** 2 + (held.bwz - held.hlz) ** 2))
R["ball_in_hand_cm"] = {"median": round(float(dh.median()), 1), "p95": round(float(dh.quantile(0.95)), 1),
                        "share_over_25cm": round(float((dh > 25).mean()), 3), "n": int(len(dh))}
# body overlap: two upright men's pelvises closer than 30 cm (bodies through each other)
ov_frames = ov_pairs = tot_frames = 0
eng_d = []
for (c, f), grp_ in D[(D.state == "live") & (D.hidden == 0)].groupby(["clip", "frame"]):
    u = grp_[(grp_.down == 0)]
    tot_frames += 1
    if len(u) < 2:
        continue
    p = np.c_[u.wx.values, u.wy.values]
    dd = np.sqrt(((p[:, None, :] - p[None, :, :]) ** 2).sum(-1))
    iu = np.triu_indices(len(u), 1)
    n = int((dd[iu] < 30).sum())
    ov_pairs += n
    ov_frames += n > 0
R["body_overlap"] = {"live_frames": tot_frames, "frames_with_overlap_share": round(ov_frames / max(tot_frames, 1), 3),
                     "pair_frames": ov_pairs}
# engaged blocks: distance between the two men (sim and shown)
eng = D[(D.state == "live") & D.eng.notna() & (D.eng != "") & (D.hidden == 0)]
key = D[(D.state == "live")][["clip", "frame", "role", "side", "wx", "wy"]]
key = key[~key.duplicated(["clip", "frame", "role", "side"], keep=False)]   # kick units repeat role names (KB1, KC)
m = eng.merge(key, left_on=["clip", "frame", "eng"], right_on=["clip", "frame", "role"], suffixes=("", "_o"))
m = m[m.side != m.side_o]
dist = np.hypot(m.wx - m.wx_o, m.wy - m.wy_o)
R["engaged_spacing_cm"] = {"median": round(float(dist.median()), 1), "p5": round(float(dist.quantile(0.05)), 1),
                           "p95": round(float(dist.quantile(0.95)), 1), "n": int(len(dist))}

# ---------------------------------------------------------------- events: AI and physics
EV = []
for f in sorted(glob.glob(os.path.join(TD, "ev[0-9][0-9][0-9].csv"))):
    c = int(os.path.basename(f)[2:5])
    lines = open(f, encoding="utf-8").read().splitlines()
    end = next((l[7:-1] for l in lines if l.startswith("# end")), "")
    e = pd.read_csv(f, comment="#")
    e["clip"] = c
    e["end"] = end
    EV.append(e)
E = pd.concat(EV, ignore_index=True)
R["event_counts"] = E.type.value_counts().to_dict()
plays = []
for c, e in E.groupby("clip"):
    t = lambda k: e[e.type == k].t.min() if (e.type == k).any() else np.nan
    plays.append(dict(clip=c, snap=t("snap"), throw=t("throw"), catch=t("catch"), tackle=t("tackle"), handoff=t("handoff"),
                      kick=t("kick"), end=e.end.iloc[0], last=e.t.max(), n_shed=int((e.type == "shed").sum()),
                      assist=int((e.type == "assist").sum()), pancake=int((e.type == "pancake").sum()),
                      scramble=bool((e.type == "scramble").any()), types=",".join(e.type)))
P = pd.DataFrame(plays)
sc = P[P.snap.notna()]
R["time_to_throw_s"] = {"median": round(float((sc.throw - sc.snap).median()), 2), "mean": round(float((sc.throw - sc.snap).mean()), 2),
                        "n": int((sc.throw.notna()).sum())}
R["snap_to_whistle_s"] = {"median": round(float((sc["last"] - sc.snap).median()), 2), "mean": round(float((sc["last"] - sc.snap).mean()), 2)}
R["tacklers_per_tackle"] = round(float(1 + P[P.tackle.notna()].assist.mean()), 2)
R["end_reasons"] = P.end.value_counts().to_dict()

# separation at the catch / at the throw: target to nearest defender
sep_catch, sep_throw, qb_press = [], [], []
for _, p in sc.iterrows():
    d = D[(D["clip"] == p["clip"]) & (D.state == "live")]
    e = E[E["clip"] == p["clip"]]
    for kind, store in (("catch", sep_catch), ("throw", sep_throw)):
        r = e[e.type == kind]
        if not len(r):
            continue
        r = r.iloc[0]
        role = r.a_role if kind == "catch" else r.b_role
        if not isinstance(role, str):
            continue
        fr = d.iloc[(d.simt - r.t).abs().argsort()[:1]].frame.values[0]
        s = d[d.frame == fr]
        tgt = s[(s.role == role) & (s.side == "off")]
        dfn = s[s.side == "def"]
        if len(tgt) and len(dfn):
            store.append(float(np.hypot(dfn.sx - tgt.sx.values[0], dfn.sy - tgt.sy.values[0]).min()))
    # pressure: nearest defender to the QB in the 0.5 s before the throw
    if not math.isnan(p.throw):
        w = d[(d.simt > p.throw - 0.5) & (d.simt <= p.throw)]
        qb = w[w.role == "QB"][["frame", "sx", "sy"]]
        dfn = w[w.side == "def"][["frame", "sx", "sy"]]
        mm_ = dfn.merge(qb, on="frame", suffixes=("", "_q"))
        if len(mm_):
            qb_press.append(float(np.hypot(mm_.sx - mm_.sx_q, mm_.sy - mm_.sy_q).min()))
R["separation_yd"] = {"at_catch_median": round(float(np.median(sep_catch)), 2) if sep_catch else None, "n_catch": len(sep_catch),
                      "at_throw_median": round(float(np.median(sep_throw)), 2) if sep_throw else None, "n_throw": len(sep_throw)}
R["pressure"] = {"defender_within_2yd_at_throw_share": round(float(np.mean(np.array(qb_press) < 2.0)), 3) if qb_press else None,
                 "n": len(qb_press)}

# ball flight: gravity, release speed, apex, hang time
air = D[(D.ballmode == "air") & (D.state == "live")].drop_duplicates(["clip", "frame"]).sort_values(["clip", "frame"])
gs, rel, hang = [], [], []
for c, a in air.groupby("clip"):
    a = a[a.frame.diff().fillna(1) == 1]
    if len(a) < 12:
        continue
    t = a.simt.values - a.simt.values[0]
    z = a.bz.values
    k = np.polyfit(t, z, 2)
    gs.append(-2 * k[0] * YD)
    vx = np.gradient(a.bx.values, t)
    vy = np.gradient(a.by.values, t)
    vz = np.gradient(z, t)
    rel.append(float(np.sqrt(vx[1] ** 2 + vy[1] ** 2 + vz[1] ** 2) * YD * MPH))
    hang.append((c, float(t[-1]), float(z.max() * YD), float(np.hypot(a.bx.values[-1] - a.bx.values[0], a.by.values[-1] - a.by.values[0]))))
R["ball_flight"] = {"gravity_ms2_median": round(float(np.median(gs)), 2) if gs else None, "gravity_spread": [round(float(np.min(gs)), 2), round(float(np.max(gs)), 2)] if gs else None,
                    "release_mph_median": round(float(np.median(rel)), 1) if rel else None, "n": len(gs),
                    "flights": [[c, round(h, 2), round(ap, 1), round(dd, 1)] for c, h, ap, dd in hang]}

# ---------------------------------------------------------------- game log
desc = {}
for l in open(LOG, encoding="utf-8", errors="replace"):
    mm = re.search(r'TAG hl clip (\d+) end frames (\d+) big (\d+) desc "(.*)"', l)
    if mm:
        desc[int(mm.group(1))] = dict(frames=int(mm.group(2)), big=int(mm.group(3)), desc=mm.group(4))
    mm = re.search(r"TAG hl clip (\d+) game q(\d) clock (\d+) score (\d+)-(\d+)", l)
    if mm and int(mm.group(1)) in desc:
        desc[int(mm.group(1))].update(q=int(mm.group(2)), clock=int(mm.group(3)), away=int(mm.group(4)), home=int(mm.group(5)))
R["log_plays"] = len(desc)
json.dump({"R": R, "plays": P.to_dict("records"), "desc": desc}, open(OUT, "w"), indent=1, default=float)
print(json.dumps(R, indent=1, default=float)[:20000])

# ---------------------------------------------------------------- shown vs scored yards
def scored(desc):
    m = re.search(r"for (?:a )?loss of (\d+)", desc)
    if m: return -int(m.group(1))
    if re.search(r"no gain", desc): return 0
    m = re.search(r"for (-?\d+) yards?", desc)
    return int(m.group(1)) if m else None
cmp_ = []
for _, p in P[P.snap.notna()].iterrows():
    c = int(p["clip"])
    dsc = desc.get(c, {}).get("desc", "")
    y = scored(dsc)
    if y is None or "PENALTY" in dsc or "kick" in dsc or "punt" in dsc:
        continue
    d = D[(D["clip"] == c) & (D.state == "live")]
    if not len(d):
        continue
    f0 = d[d.frame == d.frame.min()]
    last = d[d.frame == d.frame.max()]
    los = f0.bx.iloc[0]   # the ball on the line when the play goes live
    end_x = last.bx.iloc[0]
    sim_shown = end_x - los  # the sim's ball at the whistle (before the carrier's forward fall is added)
    # what the viewer sees: the visible ball once it has settled after the whistle (the fall played out), in yards along
    # the offense's direction (world X sign from the ball's sim / world travel over the live frames)
    shown = sim_shown
    bw = D[(D["clip"] == c)].drop_duplicates("frame").set_index("frame")[["state", "bx", "bwx"]]
    lv = bw[bw.state == "live"]
    if len(lv) > 5 and lv.bwx.notna().all():
        sgn = 1.0 if np.corrcoef(lv.bx, lv.bwx)[0, 1] >= 0 else -1.0
        if np.isnan(sgn) or lv.bx.std() < 0.2:
            sgn = 1.0 if (lv.bwx.iloc[-1] - lv.bwx.iloc[0]) * (lv.bx.iloc[-1] - lv.bx.iloc[0]) >= 0 else -1.0
        after = bw[(bw.index > lv.index.max()) & (bw.index <= lv.index.max() + 60)].bwx.dropna()
        if len(after) > 3:
            v = after.diff().abs()
            still = after[v < 1.0]  # the first settled frame (the ball moving < 1 cm a frame)
            end_w = (still.iloc[0] if len(still) else after.iloc[-1])
            shown = sgn * (end_w - lv.bwx.iloc[0]) / 91.44
    cmp_.append(dict(clip=c, scored=y, shown=round(float(shown), 1), sim=round(float(sim_shown), 1), diff=round(float(shown - y), 1), desc=dsc[:70]))
CM = pd.DataFrame(cmp_)
if len(CM):
    R["shown_vs_scored"] = {"plays": int(len(CM)), "median_abs_diff_yd": round(float(CM["diff"].abs().median()), 1),
                            "share_within_1yd": round(float((CM["diff"].abs() <= 1.0).mean()), 3),
                            "share_off_by_3plus": round(float((CM["diff"].abs() >= 3).mean()), 3),
                            "worst": CM.reindex(CM["diff"].abs().sort_values(ascending=False).index).head(8).to_dict("records")}
json.dump({"R": R, "plays": P.to_dict("records"), "desc": desc, "cmp": CM.to_dict("records")}, open(OUT, "w"), indent=1, default=float)
print(json.dumps(R.get("shown_vs_scored"), indent=1, default=float))

# ---------------------------------------------------------------- refined: free-running acceleration (0.2 s windows)
K = 3
acc_rows = []
LV = D[(D.state == "live") & (D.hidden == 0)]
for (c, p), g in LV.groupby(["clip", "pid"]):
    if len(g) < 20 or (g.frame.diff().fillna(1) != 1).any():
        continue
    x = g.sx.values * YD; y = g.sy.values * YD
    vx = (x[2 * K:] - x[:-2 * K]) / (2 * K * DT); vy = (y[2 * K:] - y[:-2 * K]) / (2 * K * DT)
    ax = (vx[2 * K:] - vx[:-2 * K]) / (2 * K * DT); ay = (vy[2 * K:] - vy[:-2 * K]) / (2 * K * DT)
    v = np.c_[vx, vy][K:-K]; sp = np.linalg.norm(v, axis=1); u = v / np.maximum(sp[:, None], 1e-6); a = np.c_[ax, ay]
    gg = g.iloc[2 * K:-2 * K]
    bad = np.convolve(g.eng.notna().astype(int).values + (g.stun.values > 0) + g.down.values, np.ones(4 * K + 1), "valid") > 0
    acc_rows.append(pd.DataFrame(dict(st=gg.sstate.values, grp=gg.grp.values, free=~bad, along=(a * u).sum(1),
                                      lat=np.abs(a[:, 0] * u[:, 1] - a[:, 1] * u[:, 0]), sp=sp, a=np.hypot(ax, ay))))
AC = pd.concat(acc_rows); FR = AC[AC.free]
R["accel_free"] = {"frames": int(len(FR)), "over_1g_share": round(float((FR.a > 9.81).mean()), 3),
                   "speedup_p99": round(float(FR.along.quantile(.99)), 2), "braking_p1": round(float(FR.along.quantile(.01)), 2),
                   "lateral_p95_at_6ms": round(float(FR[FR.sp > 6].lat.quantile(.95)), 2),
                   "by_state_over_1g": {k: round(float((v.a > 9.81).mean()), 3) for k, v in FR.groupby("st") if len(v) > 300}}

# ---------------------------------------------------------------- refined: foot plants (ball bone < 5 cm for >= 3 frames)
UP = D[(D.hidden == 0) & (D.down == 0) & ~D["mode"].isin(["fall", "ragdoll", "replay", "getup"])]
segs = []
for (c, p), g in UP.groupby(["clip", "pid"]):
    fr = g.frame.values; st = g.state.values; an = g.anim.fillna("").values; bx = g.wx.values; by = g.wy.values; gp = g.grp.values
    for fx, fy, fz in (("flx", "fly", "flz"), ("frx", "fry", "frz")):
        z = g[fz].values; X = g[fx].values; Y = g[fy].values
        on = z < 5; i = 0; n = len(z)
        while i < n:
            if on[i]:
                j = i
                while j + 1 < n and on[j + 1] and fr[j + 1] == fr[j] + 1:
                    j += 1
                if j - i + 1 >= 3:
                    path = np.hypot(np.diff(X[i:j + 1]), np.diff(Y[i:j + 1])).sum()
                    segs.append((st[i], gp[i], an[i].split(":")[0], an[i].split(":")[1] if ":" in an[i] else "", (j - i + 1) / 30, path,
                                 np.hypot(bx[j] - bx[i], by[j] - by[i])))
                i = j + 1
            else:
                i += 1
SG = pd.DataFrame(segs, columns=["state", "grp", "mode", "asset", "dur", "path", "body"])
MVP = SG[SG.body / SG.dur / 100 > 1.0]
R["foot_plants"] = {"plants_moving": int(len(MVP)), "median_slide_cm": round(float(MVP.path.median()), 1),
                    "share_over_10cm": round(float((MVP.path > 10).mean()), 3), "share_under_3cm": round(float((MVP.path < 3).mean()), 3),
                    "foot_over_body": round(float(MVP.path.sum() / MVP.body.sum()), 3),
                    "by_mode_over_10cm": {k: round(float((v.path > 10).mean()), 3) for k, v in MVP.groupby("mode")},
                    "by_group_over_10cm": {k: round(float((v.path > 10).mean()), 3) for k, v in MVP.groupby("grp")}}
wa = MVP.groupby("asset").agg(n=("path", "size"), med=("path", "median"), gt10=("path", lambda s: (s > 10).mean())).query("n >= 40")
R["foot_plants"]["worst_assets"] = wa.sort_values("gt10", ascending=False).head(10).round(3).reset_index().to_dict("records")
R["foot_plants"]["best_assets"] = wa.sort_values("gt10").head(8).round(3).reset_index().to_dict("records")
# ---------------------------------------------------------------- foot lock: planted-foot travel as the clip has it vs as shown
# (slr / sls columns, the retarget layer's own per-update measure: a lock that works shows much less than the clip)
if "slr" in D.columns and "sls" in D.columns:
    FL = D[(D.hidden == 0) & (D.down == 0) & (np.hypot(D.svx, D.svy) * YD > 1.0) & ~D["mode"].isin(["fall", "ragdoll", "replay", "getup"])]
    R["foot_lock"] = {"frames": int(len(FL)), "raw_cm_per_s": round(float(FL.slr.sum() / max(1, len(FL)) * 30), 1),
                      "shown_cm_per_s": round(float(FL.sls.sum() / max(1, len(FL)) * 30), 1),
                      "shown_over_raw": round(float(FL.sls.sum() / max(1e-6, FL.slr.sum())), 3),
                      "by_mode_shown_over_raw": {k: round(float(v.sls.sum() / max(1e-6, v.slr.sum())), 3) for k, v in FL.groupby("mode") if len(v) > 300}}
# ---------------------------------------------------------------- realism-plan measures (rubric.json)
# the ball in the air never past the end line (110) or far outside the sidelines (sim yards)
air = D[D.ballmode == "air"].drop_duplicates(["clip", "frame"])
R["ball_air_max_x_yd"] = round(float(air.bx.max()), 1) if len(air) else None
# touchdowns followed by a celebration clip (after the whistle or in the replay)
td_clips = set(P[P["end"] == "td"]["clip"])
celeb = D[D["clip"].isin(td_clips) & D.asset.str.contains("Celeb", case=False, na=False)]["clip"].unique()
R["celebrations"] = {"td_plays": len(td_clips), "with_celebration": int(len(celeb)),
                     "share": round(len(celeb) / len(td_clips), 3) if td_clips else None}
# a clip made for the other side of the ball or for a pass play on a run (wrong job): share of live animated frames
LA = D[(D.state == "live") & (D.hidden == 0) & D["mode"].isin(["mm", "one", "clip"]) & (D.asset != "")].copy()
run_clips = set(P[P["throw"].isna()]["clip"])
# get-off clips are one-man and side-specific (paired pack captures like Dline_PassBlock_*__P01/P02 hold both men, so they
# are not judged by name)
getoff = LA.asset.str.contains(r"InitMove|InitialMove|Get_?Off", case=False, regex=True)
off_bad = (LA.side == "off") & getoff & LA.asset.str.contains(r"DL|Dline|Def", case=True, regex=True)
def_bad = (LA.side == "def") & getoff & LA.asset.str.contains(r"OL|Oline|RunBlock|PassBlock", case=True, regex=True)
arms_bad = LA.asset.str.contains("ArmsUp", case=False) & LA["clip"].isin(run_clips) & (LA.ball == 0)
R["role_mismatch"] = {"share": round(float((off_bad | def_bad | arms_bad).mean()), 4),
                      "by_kind": {"off_dl_clip": int(off_bad.sum()), "def_ol_clip": int(def_bad.sum()), "armsup_on_run": int(arms_bad.sum())},
                      "examples": LA[off_bad | def_bad | arms_bad].asset.value_counts().head(8).to_dict()}
# the most-repeated one-shot's share of all one-shot starts
os_ = R["anim"]["action_variety"]["oneshot"]
R["anim"]["top_oneshot_share"] = round(max(os_["most_repeated"].values()) / max(os_["uses"], 1), 3) if os_["most_repeated"] else None

# framing: the ball carrier inside the broadcast safe frame (10% margins) during live play (captures with scrx/scry)
if "scrx" in D.columns:
    car = D[(D.state == "live") & (D.ball == 1) & D.scrx.notna()]
    inside = (car.scrx.between(0.1, 0.9)) & (car.scry.between(0.1, 0.9))
    R["framing"] = {"carrier_in_safe_frame_share": round(float(inside.mean()), 4) if len(car) else None, "n": int(len(car))}
# the referee and the players: frames (any state but replay) where he is within 1 m of someone (walking through them)
if "refd" in D.columns:
    rf = D[(D.state != "replay")].drop_duplicates(["clip", "frame"])
    rf = rf[rf.refd.notna()]
    R["referee"] = {"within_1m_share": round(float((rf.refd < 100).mean()), 4) if len(rf) else None,
                    "min_cm": round(float(rf.refd.min()), 1) if len(rf) else None, "n": int(len(rf))}

json.dump({"R": R, "plays": P.to_dict("records"), "desc": desc, "cmp": CM.to_dict("records")}, open(OUT, "w"), indent=1, default=float)
print(json.dumps({k: R.get(k) for k in ("accel_free", "foot_plants", "foot_lock", "ball_air_max_x_yd", "celebrations", "role_mismatch", "framing", "referee")}, indent=1, default=float)[:4000])
