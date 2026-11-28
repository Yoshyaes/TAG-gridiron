"""Arm / capture audit over -TagHlFull telemetry runs (one or more run folders, each with telemetry/ and run.log).

Per action category (labelled from the sim's columns and the run log's "TAG keytime" lines):
  - share of the man's frames with no capture for the action playing (motion matching locomotion "mm:" with the arms
    left to the procedural layers), and what plays instead;
  - the hand off the capture in the chest's frame (all layers: ahl/ahr; the football layer alone: afl/afr), elbow bend
    off the capture (ael/aer): mean, p95, max;
  - arm pops: the shown arm turning > POP deg/s faster than the capture does (awl/awr vs acl/acr);
  - layer jitter (ajit): share of frames > 1.5 cm/frame^2;
  - throws and catches: the capture's key moment (release / hands on the ball) against the ball's, from TAG keytime.
Each throw / catch event is scored and the worst are listed with what to film.

usage: python arm_audit.py OUT.json RUN_DIR [RUN_DIR ...]
"""
import sys, os, re, json, glob, math
import numpy as np
import pandas as pd

FPS = 30.0
POP = 600.0           # deg/s the shown arm turns faster than the capture's own arm
TOL_HAND = 15.0       # cm: hand off the capture by code (football layer) beyond which a throw / catch is code driven
TOL_ELBOW = 25.0      # deg
COLS = ["frame", "state", "simt", "ballmode", "pid", "num", "pos", "side", "role", "sstate", "sanim", "ball", "hidden", "down",
        "ahl", "ahr", "afl", "afr", "ael", "aer", "awl", "awr", "acl", "acr", "ajit", "lod", "scrx", "scry", "anim", "dev", "dxf",
        "hrx", "hry", "hrz", "hlx", "hly", "hlz", "wx", "wy", "twl", "twr", "tul", "tur", "hws", "hwc", "sls", "plf", "sanim"]
KEY = re.compile(r"TAG keytime (\w+) #(\d+) clip (\d+) frame (\d+): (none|in) (-?[\d.]+) s \((.*?), at (-?[\d.]+), key (-?[\d.]+)\)(?: hands (\d+) cm)?(?:, fc (-?[\d.]+) rev (\d+))?")
OFF_EV = {"throw", "catch", "divecatch", "drop", "contested_inc", "oobcatch", "toss", "pump"}


def mode_of(a):
    return a.split(":")[0] if isinstance(a, str) and ":" in a else (a if isinstance(a, str) else "")


def asset_of(a):
    if not isinstance(a, str) or ":" not in a:
        return ""
    return a.split(":", 1)[1].split("@")[0]


def category(r):
    """The action a man is in on a frame, from the sim's columns (events override this below)."""
    st, ss, sa = r.state, r.sstate, r.sanim
    if st == "presnap":
        return "presnap"
    if st in ("post", "other", "kick"):
        return "after whistle / between"
    if st != "live":
        return "other"
    if r.down == 1 or sa in ("down", "tackle", "fall"):
        return "tackle / fall / down"
    if sa == "block" or ss in ("passblock", "runblock") and sa == "block":
        return "block / rush engaged"
    if r.ball == 1 and r.pos == "QB" and ss == "qb":
        return "QB with ball (drop / pocket)"
    if r.ball == 1:
        return "ball carrier"
    if ss in ("rush",):
        return "pass rush (free)"
    if ss in ("route",):
        return "route running"
    if ss in ("pursue", "zone", "man"):
        return "coverage / pursuit"
    if ss in ("passblock", "runblock"):
        return "blocker (free)"
    return "other live (" + ss + ")"


FAMILIES = [
    ("carrier move (juke/spin/truck/stiff arm/hurdle)", ("Juke", "Spin_", "_Spin", "Truck", "StiffArm", "Stiff_Arm", "Hurdle")),
    ("rush move / win / beaten", ("Swim", "Rip", "Club", "LongArm", "Long_Arm", "DipRip", "RushSpin", "Bull", "Win_v", "Win_Aggressive", "Beaten", "Loss_v", "DL_Move")),
    ("block rep / engage", ("PassBlock", "RunBlock", "Block_Impact", "GetEng", "HeadsUp", "Block_Engaged", "Block_Walk", "DbleTm", "Double")),
    ("get-up / help-up", ("GetUp", "Getup", "Get_Up", "HelpUp")),
    ("tackle / tackled / pile", ("Tackle", "Tackled", "PileOn", "Pile_On", "Gang", "_PT_", "Whiff")),
    ("kick / punt / hold / snap", ("Kick", "Punt", "FG_", "Holder", "Snap", "Field_Goal")),
    ("celebration", ("Celeb", "Celebrate", "Spike")),
    ("stumble / bump", ("Stumble", "Bump", "Jam")),
    ("handoff / play fake / pitch", ("Handoff", "PA_", "Fake", "Pitch", "Toss")),
]


def family(mode, asset):
    if mode == "mm":
        return "locomotion (motion matching)"
    for name, keys in FAMILIES:
        if any(k in asset for k in keys):
            return name
    return mode + " other"


def load(run):
    T = []
    for p in sorted(glob.glob(os.path.join(run, "telemetry", "c*.csv"))):
        clip = int(os.path.basename(p)[1:4])
        d = pd.read_csv(p, usecols=lambda c: c in COLS, low_memory=False)
        d["clip"] = clip
        T.append(d)
    D = pd.concat(T, ignore_index=True)
    D["run"] = os.path.basename(run.rstrip("/\\"))
    keys = []
    log = os.path.join(run, "run.log")
    if os.path.exists(log):
        for line in open(log, encoding="utf-8", errors="replace"):
            m = KEY.search(line)
            if m:
                keys.append(dict(run=D.run.iloc[0], ev=m.group(1), num=int(m.group(2)), clip=int(m.group(3)), frame=int(m.group(4)),
                                 has=m.group(5) == "in", keyin=float(m.group(6)), anim=m.group(7), at=float(m.group(8)), key=float(m.group(9)),
                                 hands=float(m.group(10)) if m.group(10) else None, fc=float(m.group(11)) if m.group(11) else None, fcrev=int(m.group(12)) if m.group(12) else None))
    return D, keys


def stats(v):
    v = np.asarray(v, dtype=float)
    v = v[~np.isnan(v)]
    if not len(v):
        return [0, 0, 0]
    return [round(float(v.mean()), 1), round(float(np.percentile(v, 95)), 1), round(float(v.max()), 1)]


def main():
    out = sys.argv[1]
    runs = sys.argv[2:]
    frames, keys = [], []
    for r in runs:
        d, k = load(r)
        frames.append(d)
        keys += k
    D = pd.concat(frames, ignore_index=True)
    D = D[(D.hidden != 1) & D.state.isin(["presnap", "live", "post", "other", "kick"])].copy()
    for c in ("ahl", "ahr", "afl", "afr", "ael", "aer", "awl", "awr", "acl", "acr", "ajit", "down", "ball"):
        D[c] = pd.to_numeric(D[c], errors="coerce").fillna(0)
    for c in ("twl", "twr", "tul", "tur", "hws", "hwc", "sls", "plf"):
        D[c] = pd.to_numeric(D[c], errors="coerce").fillna(0) if c in D.columns else 0.0
    # twist flips: the hand's or upper arm's twist jumping > 90 deg in a frame (a candy-wrap pop), per man
    D = D.sort_values(["run", "clip", "pid", "frame"])
    same = (D.run.values[1:] == D.run.values[:-1]) & (D["clip"].values[1:] == D["clip"].values[:-1]) & (D.pid.values[1:] == D.pid.values[:-1]) & (np.diff(D.frame.values) == 1)
    flip = np.zeros(len(D), dtype=bool)
    for c in ("twl", "twr", "tul", "tur"):
        v = D[c].values.astype(float)
        dv = np.abs(np.diff(v))
        dv = np.minimum(dv, 360.0 - dv)
        flip[1:] |= same & (dv > 90.0)
    D["twflip"] = flip
    D["twwrap"] = (np.abs(D.twl) > 120) | (np.abs(D.twr) > 120) | (np.abs(D.tul) > 120) | (np.abs(D.tur) > 120)
    D["headpop"] = (D.hws - D.hwc) > 400
    D["mode"] = D.anim.map(mode_of)
    D["asset"] = D.anim.map(asset_of)
    D["cat"] = [category(r) for r in D.itertuples()]
    D["fam"] = [family(m, a_) for m, a_ in zip(D["mode"].values, D["asset"].values)]
    # event windows (throw: windup 0.7 s before the release to 0.3 s after; a catch / play on the ball: 0.4 s before to 0.3 s after)
    D["ev"] = ""
    D["evid"] = -1
    side_of = lambda ev: "off" if ev in OFF_EV else "def"
    idx = D.set_index(["run", "clip", "num", "side"]).index
    evs = []
    for i, k in enumerate(keys):
        pre, post = (0.7, 0.3) if k["ev"] in ("throw", "toss", "pump") else (0.4, 0.3)
        m = (D.run == k["run"]) & (D["clip"] == k["clip"]) & (D.num == k["num"]) & (D.side == side_of(k["ev"])) & \
            (D.frame >= k["frame"] - pre * FPS) & (D.frame <= k["frame"] + post * FPS)
        if not m.any():
            continue
        D.loc[m, "ev"] = k["ev"]
        D.loc[m, "evid"] = i
        w = D[m]
        role = w.role.iloc[0]
        on_cap = (w["mode"] != "mm").mean()
        code_hand = float(np.nanmax(np.fmax(w.afl.values, w.afr.values)))
        code_elbow = float(np.nanmax(np.fmax(w.ael.values, w.aer.values)))
        all_hand = float(np.nanmax(np.fmax(w.ahl.values, w.ahr.values)))
        pops = int(((w.awl - w.acl) > POP).sum() + ((w.awr - w.acr) > POP).sum())
        key_err = k["keyin"] if k["has"] else None
        # a throw's release as the body shows it: the frame the throwing hand moves fastest (relative to his body), against
        # the ball's release (the event frame). Independent of any bookkeeping; + = the arm comes through after the ball left
        rel_err = None
        if k["ev"] in ("throw",) and len(w) > 8:
            ww = w.sort_values("frame")
            best = None
            for hx, hy, hz in (("hrx", "hry", "hrz"), ("hlx", "hly", "hlz")):
                x = ww[hx].astype(float).values - ww.wx.astype(float).values
                y = ww[hy].astype(float).values - ww.wy.astype(float).values
                z = ww[hz].astype(float).values
                sp = np.r_[0.0, np.sqrt(np.diff(x) ** 2 + np.diff(y) ** 2 + np.diff(z) ** 2)]
                sp[np.r_[True, np.diff(ww.frame.values) != 1]] = 0.0
                if len(sp) and (best is None or sp.max() > best[0]):
                    best = (sp.max(), int(ww.frame.values[int(np.argmax(sp))]))
            if best and best[0] > 8.0:
                rel_err = (best[1] - k["frame"]) / FPS
        if rel_err is not None:
            key_err = rel_err if k["ev"] == "throw" else key_err
        score = (1 - on_cap) * 40 + max(0.0, code_hand - TOL_HAND) + max(0.0, code_elbow - TOL_ELBOW) * 0.5 + pops * 3 + (abs(key_err) * 60 if key_err is not None else 15)
        evs.append(dict(i=i, run=k["run"], clip=k["clip"], frame=k["frame"], ev=k["ev"], num=k["num"], role=role, anim=k["anim"],
                        capture_share=round(float(on_cap), 2), code_hand_max=round(code_hand, 1), code_elbow_max=round(code_elbow, 1),
                        hand_all_max=round(all_hand, 1), pops=pops, key_in=key_err, score=round(score, 1), hands_cm=k.get("hands"),
                        assets=sorted(set(a for a in w.asset if a))[:4],
                        flag=bool(code_hand > TOL_HAND or code_elbow > TOL_ELBOW)))
    D.loc[D.ev != "", "cat"] = "EV " + D.loc[D.ev != "", "ev"]
    # per category
    res = {}
    for cat, g in D.groupby("cat"):
        if len(g) < 30:
            continue
        hand = np.fmax(g.ahl.values, g.ahr.values)
        code = np.fmax(g.afl.values, g.afr.values)
        elb = np.fmax(g.ael.values, g.aer.values)
        pop = (((g.awl - g.acl) > POP) | ((g.awr - g.acr) > POP)).mean()
        modes = g["mode"].value_counts(normalize=True).round(3).to_dict()
        top = g[g["mode"] == "mm"].asset.value_counts().head(3).to_dict()
        res[cat] = dict(frames=int(len(g)), no_capture_share=round(float((g["mode"] == "mm").mean()), 3), modes=modes,
                        hand_cm=stats(hand), code_hand_cm=stats(code), elbow_deg=stats(elb),
                        code_over_tol=round(float(((code > TOL_HAND) | (elb > TOL_ELBOW)).mean()), 3),
                        pop_share=round(float(pop), 4), jitter_share=round(float((g.ajit > 1.5).mean()), 4), jitter=stats(g.ajit.values),
                        mm_assets=top, twist_wrap=round(float(g.twwrap.mean()), 4), twist_flip=round(float(g.twflip.mean()), 4),
                        head_pop=round(float(g.headpop.mean()), 4), slide_cm_s=round(float(g.sls.sum() / max(1.0, len(g) / FPS)), 2))
    fams = {}
    for fam, g in D[D.state.isin(["live", "post"])].groupby("fam"):
        if len(g) < 30:
            continue
        code = np.fmax(g.afl.values, g.afr.values)
        elb = np.fmax(g.ael.values, g.aer.values)
        fams[fam] = dict(frames=int(len(g)), code_hand_cm=stats(code), elbow_deg=stats(elb),
                         code_over_tol=round(float(((code > TOL_HAND) | (elb > TOL_ELBOW)).mean()), 4),
                         pop_share=round(float((((g.awl - g.acl) > POP) | ((g.awr - g.acr) > POP)).mean()), 4),
                         twist_flip=round(float(g.twflip.mean()), 4), head_pop=round(float(g.headpop.mean()), 4),
                         slide_cm_s=round(float(g.sls.sum() / max(1.0, len(g) / FPS)), 2))
    E = pd.DataFrame(evs)
    summ = {}
    if len(E):
        for ev, g in E.groupby("ev"):
            ke = g.key_in.dropna().astype(float)
            summ[ev] = dict(n=int(len(g)), capture_share=round(float(g.capture_share.mean()), 3),
                            no_key_capture=round(float(g.key_in.isna().mean()), 3),
                            key_err_ms=[round(float(ke.abs().mean() * 1000), 0), round(float(np.percentile(ke.abs(), 95) * 1000), 0) if len(ke) else 0,
                                        round(float(ke.abs().max() * 1000), 0) if len(ke) else 0],
                            key_err_signed_ms=round(float(ke.mean() * 1000), 0) if len(ke) else 0,
                            flagged=int(g.flag.sum()), code_hand=stats(g.code_hand_max), code_elbow=stats(g.code_elbow_max), pops=int(g.pops.sum()),
                            hands_to_ball_cm=stats(g.hands_cm.dropna().astype(float)) if "hands_cm" in g and g.hands_cm.notna().any() else None)
    worst = {}
    if len(E):
        for kind, evset in (("throws", {"throw", "toss", "pump"}), ("catches", {"catch", "divecatch", "int", "pbu", "drop", "contested_inc", "oobcatch"})):
            w = E[E.ev.isin(evset)].sort_values("score", ascending=False).head(10)
            worst[kind] = w.to_dict("records")
    # the models: LOD changes of the shown body (per man per minute on screen) and what share of on-screen frames each LOD
    lodinfo = {}
    if "lod" in D.columns:
        D["lod"] = pd.to_numeric(D["lod"], errors="coerce").fillna(-1)
        on = D[(D.scrx.astype(float) > 0) & (D.scrx.astype(float) < 1) & (D.scry.astype(float) > 0) & (D.scry.astype(float) < 1) & (D.lod >= 0)]
        if len(on):
            ch = 0
            for _, g in on.sort_values("frame").groupby(["run", "clip", "pid"]):
                l = g.lod.values
                f = g.frame.values
                ch += int(((l[1:] != l[:-1]) & (np.diff(f) == 1)).sum())
            lodinfo = dict(onscreen_frames=int(len(on)), changes=ch, changes_per_man_min=round(ch / (len(on) / FPS / 60.0), 2),
                           share=on.lod.value_counts(normalize=True).round(3).to_dict())
        print("\nLOD on screen:", lodinfo)
    print("\n%-48s %7s %16s %16s %6s %6s %7s %7s %8s" % ("capture family (live/post)", "frames", "code hand cm", "elbow deg", ">tol%", "pop%", "twFlip%", "head%", "slide"))
    for fam in sorted(fams, key=lambda f: -fams[f]["frames"]):
        r = fams[fam]
        print("%-48s %7d %16s %16s %6.2f %6.2f %7.3f %7.2f %8.2f" % (fam[:48], r["frames"], "/".join(map(str, r["code_hand_cm"])), "/".join(map(str, r["elbow_deg"])),
              100 * r["code_over_tol"], 100 * r["pop_share"], 100 * r["twist_flip"], 100 * r["head_pop"], r["slide_cm_s"]))
    json.dump(dict(categories=res, families=fams, events=summ, worst=worst, lod=lodinfo, all_events=E.to_dict("records") if len(E) else []), open(out, "w"), indent=1, default=str)
    # print
    print("%-34s %7s %6s %16s %16s %16s %6s %6s %6s" % ("category", "frames", "noCap", "hand cm m/p95/x", "code cm", "elbow deg", ">tol", "pop%", "jit%"))
    for cat in sorted(res, key=lambda c: -res[c]["frames"]):
        r = res[cat]
        print("%-34s %7d %6.3f %16s %16s %16s %6.3f %6.2f %6.2f" % (cat[:34], r["frames"], r["no_capture_share"], "/".join(map(str, r["hand_cm"])),
              "/".join(map(str, r["code_hand_cm"])), "/".join(map(str, r["elbow_deg"])), r["code_over_tol"], 100 * r["pop_share"], 100 * r["jitter_share"]))
    print()
    print("%-34s %8s %8s %8s %10s" % ("category", "twWrap%", "twFlip%", "headPop%", "slide cm/s"))
    for cat in sorted(res, key=lambda c: -res[c]["frames"]):
        r = res[cat]
        print("%-34s %8.2f %8.3f %8.2f %10.2f" % (cat[:34], 100 * r["twist_wrap"], 100 * r["twist_flip"], 100 * r["head_pop"], r["slide_cm_s"]))
    print()
    for ev, s in sorted(summ.items()):
        print("%-14s n %4d  capture %.2f  no key capture %.2f  key err |ms| mean/p95/max %s  signed %+.0f  flagged %d  code hand %s  elbow %s  pops %d" % (
            ev, s["n"], s["capture_share"], s["no_key_capture"], s["key_err_ms"], s["key_err_signed_ms"], s["flagged"], s["code_hand"], s["code_elbow"], s["pops"]))
    for kind, w in worst.items():
        print("\nworst", kind)
        for e in w:
            print("  %s c%03d f%d %-8s #%d %-4s score %5.1f cap %.2f code hand %.0f elbow %.0f pops %d key %s | %s" % (
                e["run"], e["clip"], e["frame"], e["ev"], e["num"], e["role"], e["score"], e["capture_share"], e["code_hand_max"], e["code_elbow_max"], e["pops"],
                "none" if e["key_in"] is None or (isinstance(e["key_in"], float) and math.isnan(e["key_in"])) else "%+.3f" % e["key_in"], e["anim"][:60]))


if __name__ == "__main__":
    main()
