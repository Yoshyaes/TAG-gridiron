# The animation A/B numbers of a -TagHlFull capture (telemetry c###.csv) and its log, for the four changes of the
# 2026-11-24 batch (Research/NextLevel/01-animation-motion.md actions 1, 2, 3, 6, 7; 06 action 6):
#   breaks  - the sim's heading turning > 45 deg within 0.3 s (svx, svy) at > 2 m/s: from the onset (its first 8 deg) the
#             lag to the first matched pick change (anim column), and over +-0.5 s the mean |mmang| (clip travel vs his
#             travel), |mmor| (orientation warp) and the planted feet's travel as shown (sls / plf). A/B: -TagNoSimFuture.
#   style   - the share of a class's moving matched frames on each class's captures (OL/DL on agile captures, WR/CB/S
#             on big ones: both should be < 5 % with the body-class sets; A/B -TagNoStyleSets); tired captures shown.
#   stumble - frames on a contact-reaction capture (the bumps, jams, upright stumbles): how far the body is from its sim
#             spot there (voff p95 <= 60 cm, the follower's limit). A/B: -TagNoStumble.
#   log     - the game's own cumulative lines: "TAG break lag" (with the user's man and the pelvis's turn), "TAG xfj"
#             (transition jerk, A/B -TagFrozenFade / -TagMMDeadBlend), "TAG style share" (with the tired gait by
#             fatigue), "TAG stumbles", the last of each.
#   python mm_ab.py <telemetry dir> [--log <game log>] [--fps 30] [--b <dir> [--blog <log>]] [--json out.json]
# With --b the same numbers for a second capture side by side (the deterministic sim plays the same plays: compare like
# for like on one seed, -TagWeatherSeed=<n>).
import argparse
import glob
import json
import os
import re
import sys

import numpy as np
import pandas as pd

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mm_classes import body_class, capture_class  # noqa: E402

# the contact reactions without the ball (FAthleteAssets::PackBumps, new) and the carrier's stumbles (PackStumbles: the
# broken tackle the sim calls, and now a carrier's contact reaction too)
BUMP_RE = re.compile(r"Avoidance_Bump|TwoMan_Neg_Bump|NonBC_Stumble_Hit|DB_Jam_Loss|DB_WR_Jam", re.I)
STUMBLE_RE = re.compile(r"Stumble|Brkn|Broken", re.I)


def load(td):
    files = sorted(glob.glob(os.path.join(td, "c[0-9][0-9][0-9].csv")))
    if not files:
        sys.exit("no c###.csv in " + td)
    D = pd.concat([pd.read_csv(f, low_memory=False).assign(clip=int(os.path.basename(f)[1:4])) for f in files], ignore_index=True)
    D["anim"] = D["anim"].fillna("")
    D["mode"] = D["anim"].str.split(":").str[0]
    D["asset"] = D["anim"].str.split(":").str[1].fillna("").str.split("@").str[0]
    return D.sort_values(["clip", "pid", "frame"]).reset_index(drop=True)


def breaks(D, fps):
    L = D[(D.state == "live") & (D.hidden == 0) & (D.down == 0)]
    out = []
    n30 = max(1, int(round(0.3 * fps)))
    for (clip, pid), g in L.groupby(["clip", "pid"]):
        if len(g) < n30 + 3:
            continue
        f = g.frame.to_numpy()
        sp = np.hypot(g.svx.to_numpy(), g.svy.to_numpy())
        hd = np.degrees(np.arctan2(g.svy.to_numpy(), g.svx.to_numpy()))
        asset = g.asset.to_numpy()
        mode = g["mode"].to_numpy()
        # (mmang is measured only while a matched clip's feet cover ground, mmeff > 80 cm/s: 0 otherwise, left out)
        valid = (g.mmeff.to_numpy() > 80) & (g["mode"].to_numpy() == "mm")
        ang = np.where(valid, np.abs(g.mmang.to_numpy()), np.nan)
        orient = np.where(valid, np.abs(g.mmor.to_numpy()), np.nan)
        sls = g.sls.to_numpy()
        plf = g.plf.to_numpy()
        pick = np.zeros(len(g), bool)
        pick[1:] = (mode[1:] == "mm") & (mode[:-1] == "mm") & (asset[1:] != asset[:-1])
        cool = -1
        for i in range(n30, len(g)):
            if i < cool or f[i] - f[i - n30] != n30:
                continue
            if sp[i] < 2.2 or sp[i - n30] < 2.2:  # yd/s (2 m/s)
                continue
            d = (hd[i] - hd[i - n30] + 180) % 360 - 180
            if abs(d) < 45:
                continue
            h0 = hd[i - n30]
            onset = i
            for j in range(i - n30, i + 1):
                if abs((hd[j] - h0 + 180) % 360 - 180) > 8:
                    onset = j
                    break
            lo, hi = max(0, onset - int(0.5 * fps)), min(len(g), onset + int(0.5 * fps) + 1)
            # (picks change every few tenths anyway: the first change from just before the onset, not any before it)
            pk = [j for j in range(max(0, onset - int(0.1 * fps)), hi) if pick[j]]
            # realign: from the sim's 45 deg (i) until the playing clip travels within 15 deg of where he travels again
            # (|mmang|), whatever picked it - the time the body runs on a capture going the old way
            ra = next((j for j in range(i, min(len(g), i + int(1.0 * fps))) if valid[j] and ang[j] < 15), None)
            w = slice(lo, hi)
            out.append({"clip": clip, "pid": pid, "role": g.role.iloc[i], "sstate": g.sstate.iloc[onset],
                        "pick_lag_ms": (pk[0] - onset) / fps * 1000 if pk else np.nan,
                        "realign_ms": (ra - i) / fps * 1000 if ra is not None else np.nan,
                        "mmang": float(np.nanmean(ang[w])), "mmor": float(np.nanmean(orient[w])),
                        "slide_cm_s": float(sls[w].sum() / plf[w].sum() * fps) if plf[w].sum() > 0 else np.nan,
                        "mm_share": float((mode[w] == "mm").mean())})
            cool = i + int(1.0 * fps)
    return pd.DataFrame(out)


def summarize_breaks(B):
    if B.empty:
        return {"n": 0}
    def blk(x):
        return {"n": int(len(x)), "pick_lag_ms_p50": round(float(x.pick_lag_ms.median()), 0) if x.pick_lag_ms.notna().any() else None,
                "pick_lag_ms_p90": round(float(x.pick_lag_ms.quantile(0.9)), 0) if x.pick_lag_ms.notna().any() else None,
                "no_pick": int(x.pick_lag_ms.isna().sum()),
                "realign_ms_p50": round(float(x.realign_ms.median()), 0) if x.realign_ms.notna().any() else None,
                "realign_ms_p90": round(float(x.realign_ms.quantile(0.9)), 0) if x.realign_ms.notna().any() else None,
                "no_realign": int(x.realign_ms.isna().sum()),
                "mmang": round(float(x.mmang.mean()), 1), "mmor": round(float(x.mmor.mean()), 1),
                "slide_cm_s": round(float(x.slide_cm_s.mean()), 1) if x.slide_cm_s.notna().any() else None}
    R = {"all": blk(B)}
    R["route"] = blk(B[B.sstate == "route"]) if (B.sstate == "route").any() else {"n": 0}
    R["other"] = blk(B[B.sstate != "route"]) if (B.sstate != "route").any() else {"n": 0}
    return R


def style(D, fps):
    M = D[(D["mode"] == "mm") & (D.hidden == 0) & (D.down == 0) & D.state.isin(["presnap", "live", "post"])].copy()
    g = M.groupby(["clip", "pid"])
    step = np.hypot(g.wx.diff(), g.wy.diff()) * fps
    M = M[step > 150].copy()
    M["man"] = [body_class(p) for p in M["pos"]]
    M["cap"] = [capture_class(a) for a in M["asset"]]
    R = {}
    for man in ("Big", "Med", "Agile"):
        x = M[M.man == man]
        if len(x):
            R[man] = {"frames": int(len(x)), **{"on_" + c: round(float((x.cap == c).mean()) * 100, 1) for c in ("Big", "Med", "Agile", "General", "Fatigued")}}
    R["fatigued_frames_all"] = int((M.cap == "Fatigued").sum())
    return R


def stumble(D):
    def blk(S):
        if S.empty:
            return {"frames": 0}
        starts = S[S.groupby(["clip", "pid"]).frame.diff().fillna(99) != 1]
        return {"frames": int(len(S)), "reactions": int(len(starts)), "voff_p50": round(float(S.voff.median()), 1), "voff_p95": round(float(S.voff.quantile(0.95)), 1),
                "voff_max": round(float(S.voff.max()), 1), "dev_mean": round(float(S.dev.mean()), 1),
                "by_asset": S.asset.str.replace("A_TAG_Pack_", "").value_counts().head(8).to_dict()}
    one = D[D["mode"] == "one"]
    return {"bumps": blk(one[one.asset.str.contains(BUMP_RE)]), "carrier_stumbles": blk(one[one.asset.str.contains(STUMBLE_RE) & ~one.asset.str.contains(BUMP_RE)])}


def log_lines(path):
    if not path or not os.path.exists(path):
        return {}
    keys = {"break_lag": "TAG break lag", "xfj": "TAG xfj", "style_share": "TAG style share", "stumbles": "TAG stumbles:", "mm_enabled": "TAG: motion matching enabled"}
    R, n = {}, 0
    for line in open(path, encoding="utf-8", errors="replace"):
        if "TAG stumble #" in line:
            n += 1
        for k, t in keys.items():
            if t in line:
                R[k] = line.split(t, 1)[1].strip() if k != "mm_enabled" else line.strip()
    R["stumble_lines"] = n
    return R


def report(td, log, fps):
    D = load(td)
    return {"dir": td, "breaks": summarize_breaks(breaks(D, fps)), "style": style(D, fps), "stumble": stumble(D), "log": log_lines(log)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--log")
    ap.add_argument("--b")
    ap.add_argument("--blog")
    ap.add_argument("--fps", type=float, default=30.0)
    ap.add_argument("--json")
    a = ap.parse_args()
    R = {"a": report(a.dir, a.log, a.fps)}
    if a.b:
        R["b"] = report(a.b, a.blog, a.fps)
    print(json.dumps(R, indent=1, default=str))
    if a.json:
        json.dump(R, open(a.json, "w"), indent=1, default=str)


if __name__ == "__main__":
    main()
