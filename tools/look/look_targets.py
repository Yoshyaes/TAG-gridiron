"""Phase 1 of LOOK-AND-FEEL-PLAN.md: reference targets from MaddenRef, as aggregates only.

Reads the decoded clip metadata (D:\\MaddenRef\\Extracted\\animation\\full\\*\\*.json) and, for small samples, the
decoded poses through tools/madden/antbank. Writes numbers only (counts, medians, percentiles) to
docs/look_targets.json and docs/LOOK_TARGETS.md. No clip, curve, pose or mesh data is written, and nothing here
may be used to drive TAG Gridiron animation. Targets are for comparison, next to real world references.

    python -I tools/look/look_targets.py [--no-poses]
"""
import glob
import json
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
FULL = os.path.join(REPO, "extracted", "animation", "full")
OUT_JSON = os.path.join(REPO, "docs", "look_targets.json")
OUT_MD = os.path.join(REPO, "docs", "LOOK_TARGETS.md")

# Ordered: first match wins. Categories follow the sim's action states and the Phase 3 priority list.
CATS = [
    ("injury", r"injur"),
    ("celebration", r"celeb|taunt|dance|spikeball|pointto|fist_?pump"),
    ("qb_dropback", r"drop_?back|\d_?step_?drop|dropstep"),
    ("qb_throw", r"qb.*(pass|throw|release)|(pass|throw).*qb|_pass_d\d|deeppass|shortpass"),
    ("handoff_pitch", r"handoff|hand_off|pitch"),
    ("snap", r"_snap|snap_|center_?exchange"),
    ("stance_3pt", r"3pt|three_?point"),
    ("stance_2pt", r"2pt|two_?point"),
    ("stance_qb", r"shotgun|under_?center"),
    ("stance_other", r"stance|preplay"),
    ("catch_high", r"catch.*(high|abovehead|overthrow|jump)|(high|jump).*catch"),
    ("catch_low", r"catch.*(low|knee|shoe|scoop)|low.*catch"),
    ("catch_over_shoulder", r"over_?shoulder|basket"),
    ("catch_dive", r"(dive|diving).*catch|catch.*(dive|diving)"),
    ("catch_sideline", r"toe_?tap|sideline_?catch|catch.*sideline"),
    ("catch_other", r"catch|reception"),
    ("tackle_multi", r"^\d?man_.*tackl|gang|^\dman_.*(hit|wrap)"),
    ("tackle_dive", r"dive.*tackl|tackl.*dive"),
    ("tackle", r"tackl|wrap|hitstick|big_?hit|takedown"),
    ("block_pass", r"pass_?pro|passblock|pass_?block|kick_?slide"),
    ("block_run", r"run_?block|drive_?block|down_?block|block"),
    ("rush_move", r"swim|bull_?rush|rip_|spin_?move|pass_?rush|shed"),
    ("bc_juke", r"juke"),
    ("bc_spin", r"spin"),
    ("bc_stiff_arm", r"stiff"),
    ("bc_truck_hurdle", r"truck|hurdle"),
    ("bc_cut", r"_cut|cut_"),
    ("fall", r"fall|knock_?down|stumble"),
    ("get_up", r"get_?up|jump_?up|getsup"),
    ("kicking", r"kick|punt|_fg_|holder|long_?snap"),
    ("loco_walk", r"walk"),
    ("loco_jog", r"jog"),
    ("loco_run", r"loco.*run|run_loop|_run_"),
    ("loco_sprint", r"sprint"),
    ("loco_backpedal", r"backpedal|back_?pedal"),
    ("loco_shuffle", r"shuffle|strafe"),
    ("idle", r"idle"),
]
CRX = [(c, re.compile(r, re.I)) for c, r in CATS]


def category(name):
    for c, rx in CRX:
        if rx.search(name):
            return c
    return "other"


def group(name):
    return re.sub(r"__[PC]\d+$", "", name)


def pct(a, q):
    return round(float(np.percentile(a, q)), 3)


def load_meta():
    rows = []
    for f in glob.glob(os.path.join(FULL, "*", "*.json")):
        m = json.load(open(f))
        rows.append(m)
    return rows


def timing(rows):
    by = {}
    groups = {}
    for m in rows:
        c = category(m["name"])
        by.setdefault(c, []).append(float(m["duration_s"]))
        groups.setdefault(group(m["name"]), set()).add(m["name"])
    out = {}
    for c, d in sorted(by.items(), key=lambda kv: -len(kv[1])):
        a = np.array(d)
        out[c] = {"clips": len(a), "median_s": pct(a, 50), "p10_s": pct(a, 10), "p90_s": pct(a, 90)}
    sizes = np.array([len(v) for v in groups.values()])
    inter = {"groups": int(len(sizes)), "multi_participant_groups": int((sizes > 1).sum()),
             "max_participants": int(sizes.max()),
             "participants_histogram": {str(k): int((sizes == k).sum()) for k in range(1, int(sizes.max()) + 1)}}
    return out, inter


def pose_measures(rows, per_set=12):
    """Aggregate gait speed, cadence and stance heights from small samples. Y is up, meters."""
    sys.path.insert(0, os.path.join(REPO, "tools", "madden", "antbank"))
    import pose  # noqa: E402
    import fbxanim  # noqa: E402
    ev = pose.Evaluator()
    h = ev.hero
    need = ["Hips", "Head", "LeftFoot", "RightFoot"]
    if not all(b in h.index for b in need):
        return {"error": "hero skeleton lacks " + ",".join(b for b in need if b not in h.index)}
    idx = [h.index[b] for b in need]

    sets = {
        "jog_loop": lambda n: re.search(r"jog", n, re.I) and re.search(r"loop", n, re.I),
        "run_loop": lambda n: re.search(r"loco.*run|run.*loop", n, re.I) and not re.search(r"jog|sprint|walk", n, re.I),
        "sprint_loop": lambda n: re.search(r"sprint", n, re.I) and re.search(r"loco|loop", n, re.I),
        "stance_3pt": lambda n: re.search(r"3pt", n, re.I),
        "stance_2pt": lambda n: re.search(r"2pt", n, re.I),
        "stance_qb_shotgun": lambda n: re.search(r"shotgun", n, re.I) and re.search(r"stance|idle|loop", n, re.I),
    }
    out = {}
    rng = np.random.default_rng(19)
    for sname, pred in sets.items():
        cands = sorted([m for m in rows if pred(m["name"])], key=lambda m: m["name"])
        if len(cands) > per_set:
            cands = [cands[i] for i in sorted(rng.choice(len(cands), per_set, replace=False))]
        speeds, cadences, head, hips = [], [], [], []
        used = 0
        for m in cands:
            try:
                key = int(m["clip_key"], 16)
                _, _, poses = ev.sample(key)
                fr = fbxanim.local_frames(ev, poses)
                P = np.array([ev.fk(*f)[1][idx] for f in fr])   # frames x 4 x 3
            except Exception:
                continue
            fps = float(m.get("source_fps") or m.get("fps") or 30.0)
            dur = (len(P) - 1) / fps
            if dur <= 0.2:
                continue
            used += 1
            hipxz = P[:, 0, [0, 2]]
            if sname.endswith("_loop"):
                speeds.append(float(np.linalg.norm(hipxz[-1] - hipxz[0]) / dur))
                # steps: local minima of each foot's height below its 20th percentile
                steps = 0
                for k in (2, 3):
                    y = P[:, k, 1]
                    lo = np.percentile(y, 20)
                    steps += int(np.sum((y[1:-1] < y[:-2]) & (y[1:-1] <= y[2:]) & (y[1:-1] < lo)))
                cadences.append(steps / dur)
            head.append(float(np.median(P[:, 1, 1])))
            hips.append(float(np.median(P[:, 0, 1])))
        r = {"clips_measured": used}
        for k, v in (("hips_speed_mps", speeds), ("steps_per_s", cadences), ("head_height_m", head), ("hips_height_m", hips)):
            if v:
                a = np.array(v)
                r[k] = {"median": pct(a, 50), "p10": pct(a, 10), "p90": pct(a, 90)}
        out[sname] = r
    return out


def write_md(t, inter, poses, n):
    L = ["# Look targets (Phase 1)", "",
         "Generated by `tools/look/look_targets.py` from MaddenRef clip metadata. Aggregates only, for comparison.",
         "Nothing here drives TAG Gridiron animation. Real world references (combine measurables, broadcast",
         "footage, equipment specs) come first. These numbers are the game feel comparison.", "",
         "## Motion timing by category", "",
         "%d clips. Duration of whole clips, so setup and recovery are included." % n, "",
         "| Category | Clips | Median s | p10 s | p90 s |", "| --- | ---: | ---: | ---: | ---: |"]
    for c, r in t.items():
        L.append("| %s | %d | %.2f | %.2f | %.2f |" % (c, r["clips"], r["median_s"], r["p10_s"], r["p90_s"]))
    L += ["", "## Interactions", "",
          "%d clip groups, %d with two or more participants, up to %d." % (inter["groups"], inter["multi_participant_groups"], inter["max_participants"]),
          "", "| Participants | Groups |", "| ---: | ---: |"]
    for k, v in inter["participants_histogram"].items():
        L.append("| %s | %d |" % (k, v))
    if poses:
        L += ["", "## Gait and stance (small samples)", "",
              "Median (p10 to p90). Meters, meters per second, steps per second. Caveats: samples of up to 12",
              "clips. Some loops are partly in place, so low hips speeds (run_loop p10) are root handling, not",
              "gait. Cadence counts foot height minima and is a heuristic. For top speed, combine 40 yard times",
              "and the sim's own model (99 speed is about a 4.28 forty) take precedence.", "",
              "| Set | Clips | Hips speed | Cadence | Head height | Hips height |", "| --- | ---: | --- | --- | --- | --- |"]
        f = lambda r, k: ("%.2f (%.2f to %.2f)" % (r[k]["median"], r[k]["p10"], r[k]["p90"])) if k in r else "-"
        for s, r in poses.items():
            if "error" in r if isinstance(r, dict) else False:
                continue
            L.append("| %s | %d | %s | %s | %s | %s |" % (s, r.get("clips_measured", 0), f(r, "hips_speed_mps"), f(r, "steps_per_s"),
                                                        f(r, "head_height_m"), f(r, "hips_height_m")))
    L += ["", "## How these are used", "",
          "- Phase 3 gates: our clip and procedural timings fall inside each category's p10 to p90.",
          "- Animation budget: categories and interaction counts set the priority and variety targets.",
          "- Gait and stance numbers sanity check speed matching (`Athlete::Drive`) and stance poses.", ""]
    open(OUT_MD, "w", encoding="utf-8").write("\n".join(L))


def main():
    rows = load_meta()
    if not rows:
        sys.exit("no clip metadata under " + FULL)
    t, inter = timing(rows)
    poses = None if "--no-poses" in sys.argv else pose_measures(rows)
    os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
    json.dump({"source": "MaddenRef clip metadata, aggregates only", "clips": len(rows), "timing": t,
               "interactions": inter, "gait_stance": poses}, open(OUT_JSON, "w"), indent=1)
    write_md(t, inter, poses, len(rows))
    print("clips", len(rows), "categories", len(t), "->", OUT_JSON)
    if poses:
        print(json.dumps(poses, indent=1))


if __name__ == "__main__":
    main()
