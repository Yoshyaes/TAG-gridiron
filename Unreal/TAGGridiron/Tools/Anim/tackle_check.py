"""Tackle realism check for a -TagTackleCam capture.

Reads <capture>/tcdata/t###.csv (per frame, per man near the ball: sim state, shown clip, bone world positions) and
tev###.csv (the sim's events), models each body as capsules (pads, helmet, limbs) and reports per clip and overall:

  core_pen    deepest torso/helmet overlap between two of the tackle's men (carrier, tackler, assists) from the hit on
              (cm; real pads compress a few cm)
  limb_pen    deepest limb-into-torso overlap among them (an arm through a chest)
  gap_hit     surface gap tackler <-> carrier at the sim's tackle (cm; ~0 = they touch)
  gap_fall    mean surface gap while both are falling together (first 1 s)
  hands       share of tackle-window frames with a tackler hand within 12 cm of the carrier's body
  turf        deepest body part under the turf (cm)
  pop         largest one-frame spike of a core bone's world motion over the frames either side (cm; a snap)
  lie_slide   mean speed of a lying man's pelvis over the turf (pelvis under 35 cm; cm/s; ~0 once he has landed)
  carrier_lo  the carrier's lowest pelvis after the tackle (cm; over ~60 he stayed on his feet: tackled but standing)
  hit_jump    the largest jump in a tackle man's pelvis velocity across the switch into his fall (cm/s; a man stopped
              dead in a frame at the contact reads as hitting a wall: thousands)
  pile_float  an assist tackler's lowest core part while lying on the pile, highest above the carrier's (cm)

usage: python tackle_check.py <capture dir> [--clip N] [--json out.json]
"""
import csv, glob, json, math, os, sys
from collections import defaultdict

SEGS = [  # (bone a, bone b, radius cm, kind)
    ("pelvis", "spine_03", 17.0, "core"),
    ("spine_03", "neck_01", 21.0, "core"),   # shoulder pads
    ("head", "head", 13.5, "core"),          # helmet
    ("upperarm_l", "lowerarm_l", 6.5, "limb"), ("lowerarm_l", "hand_l", 5.0, "limb"),
    ("upperarm_r", "lowerarm_r", 6.5, "limb"), ("lowerarm_r", "hand_r", 5.0, "limb"),
    ("thigh_l", "calf_l", 9.0, "limb"), ("calf_l", "foot_l", 6.0, "limb"), ("foot_l", "ball_l", 4.5, "limb"),
    ("thigh_r", "calf_r", 9.0, "limb"), ("calf_r", "foot_r", 6.0, "limb"), ("foot_r", "ball_r", 4.5, "limb"),
]
CORE_BONES = ["pelvis", "spine_03", "neck_01", "head"]


def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def norm(a): return math.sqrt(dot(a, a))


def seg_dist(p1, q1, p2, q2):
    """closest distance between segments p1q1 and p2q2"""
    d1, d2, r = sub(q1, p1), sub(q2, p2), sub(p1, p2)
    a, e, f = dot(d1, d1), dot(d2, d2), dot(d2, r)
    if a < 1e-6 and e < 1e-6: return norm(r)
    if a < 1e-6:
        s, t = 0.0, min(max(f / e, 0.0), 1.0)
    else:
        c = dot(d1, r)
        if e < 1e-6:
            t, s = 0.0, min(max(-c / a, 0.0), 1.0)
        else:
            b = dot(d1, d2)
            den = a * e - b * b
            s = min(max((b * f - c * e) / den, 0.0), 1.0) if den > 1e-9 else 0.0
            t = (b * s + f) / e
            if t < 0: t, s = 0.0, min(max(-c / a, 0.0), 1.0)
            elif t > 1: t, s = 1.0, min(max((b - c) / a, 0.0), 1.0)
    c1 = (p1[0] + d1[0] * s, p1[1] + d1[1] * s, p1[2] + d1[2] * s)
    c2 = (p2[0] + d2[0] * t, p2[1] + d2[1] * t, p2[2] + d2[2] * t)
    return norm(sub(c1, c2))


def capsules(m):
    out = []
    for a, b, r, k in SEGS:
        if a in m["b"] and b in m["b"]: out.append((m["b"][a], m["b"][b], r, k, a))
    return out


def pair_metrics(ma, mb):
    """(min surface gap, deepest core-core overlap, deepest limb-into-core overlap)"""
    ca, cb = capsules(ma), capsules(mb)
    gap, core, limb = 1e9, 0.0, 0.0
    for p1, q1, r1, k1, _ in ca:
        for p2, q2, r2, k2, _ in cb:
            d = seg_dist(p1, q1, p2, q2) - r1 - r2
            gap = min(gap, d)
            if k1 == "core" and k2 == "core": core = max(core, -d)
            elif k1 != k2: limb = max(limb, -d)
    return gap, core, limb


def hand_gap(mt, mc):
    best = 1e9
    for h in ("hand_l", "hand_r"):
        if h not in mt["b"]: continue
        for p2, q2, r2, k2, _ in capsules(mc):
            if k2 != "core" and "thigh" not in _: continue
            best = min(best, seg_dist(mt["b"][h], mt["b"][h], p2, q2) - r2 - 4.0)
    return best


def lowest(m):
    lo = 1e9
    for p, q, r, k, _ in capsules(m): lo = min(lo, p[2] - r, q[2] - r)
    return lo


def load_clip(path):
    frames = defaultdict(dict)
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            fr = int(row["frame"])
            m = {k: row[k] for k in ("state", "num", "side", "carrier", "sanim", "sstate", "anim", "down")}
            m["simt"] = float(row["simt"])
            m["w"] = (float(row["wx"]), float(row["wy"]), float(row["wz"]))
            m["b"] = {}
            for k in row:
                if k.endswith("_x") and row[k] != "":
                    bn = k[:-2]
                    m["b"][bn] = (float(row[bn + "_x"]), float(row[bn + "_y"]), float(row[bn + "_z"]))
            m["pid"] = row["pid"]
            frames[fr][(row["side"], row["pid"])] = m  # by player id: numbers repeat within a team
    return frames


def load_events(path):
    ev = []
    if os.path.exists(path):
        with open(path, newline="") as f:
            for row in csv.DictReader(f):
                ev.append(row)
    return ev


def analyze_clip(cap, n):
    frames = load_clip(os.path.join(cap, "tcdata", "t%03d.csv" % n))
    ev = load_events(os.path.join(cap, "tcdata", "tev%03d.csv" % n))
    if not frames: return None
    fr_sorted = sorted(frames)
    carrier = None
    for fr in fr_sorted:
        for key, m in frames[fr].items():
            if m["carrier"] == "1": carrier = key
        if carrier: break
    tk = next((e for e in ev if e["type"] == "tackle"), None)
    wrap = next((e for e in ev if e["type"] == "wrap"), None)
    assists = [e for e in ev if e["type"] == "assist"]
    res = {"clip": n, "frames": len(fr_sorted), "tackle": bool(tk), "wrap": bool(wrap), "assists": len(assists),
           "note": tk["note"] if tk else "", "events": [e["type"] for e in ev]}
    # the man actually tackled (the clip can start on the quarterback before a handoff): from the tackle event
    if tk and tk.get("b_pid") not in (None, "", "-1"): carrier = (tk["b_side"], tk["b_pid"])
    if not carrier: return res
    opp = "def" if carrier[0] == "off" else "off"
    # events name men by player id (a_pid) in newer captures, by number before: map numbers to the ids seen near the ball
    num2pid = {}
    for fr in fr_sorted:
        for (sd, pid), m in frames[fr].items(): num2pid.setdefault((sd, m["num"]), pid)
    def who(e):
        if e.get("a_pid") not in (None, "", "-1"): return (opp, e["a_pid"])
        return (opp, num2pid.get((opp, e["a_num"]), e["a_num"]))
    tackler = who(tk) if tk else None
    helpers = [who(e) for e in assists]
    t_tackle = float(tk["t"]) if tk else None
    t_wrap = float(wrap["t"]) if wrap else None
    core_pen = limb_pen = 0.0
    core_where = None
    pops = []
    turf = 0.0
    gap_hit = None
    fall_gaps = []
    hand_frames = hand_ok = 0
    pile_float = 0.0
    prev = {}
    wrap_gaps = []
    track = {}
    lie = []
    part_all = set()
    for fr in fr_sorted:
        men = frames[fr]
        simt = next(iter(men.values()))["simt"]
        keys = list(men)
        # overlap between the tackle's own men once it has started
        t_from = t_wrap if t_wrap is not None else t_tackle
        part = [k for k in keys if k == carrier or k == tackler or k in helpers]
        part_all.update(part)
        if t_from is not None and simt >= t_from - 1e-6:
            for i in range(len(part)):
                for j in range(i + 1, len(part)):
                    g, c, l = pair_metrics(men[part[i]], men[part[j]])
                    if c > core_pen: core_pen, core_where = c, (fr, part[i], part[j], men[part[i]]["anim"], men[part[j]]["anim"])
                    limb_pen = max(limb_pen, l)
        for k, m in men.items():
            if k in part: turf = min(turf, lowest(m))
            # world motion of the core bones per frame (spikes found below)
            for bn in CORE_BONES:
                if bn in m["b"]: track.setdefault((k, bn), {})[fr] = (m["b"][bn], m["anim"])
            if k in part and "pelvis" in m["b"] and k in prev and "pelvis" in prev[k]["b"] and m["b"]["pelvis"][2] < 35 and prev[k]["b"]["pelvis"][2] < 35:
                a_, b_ = m["b"]["pelvis"], prev[k]["b"]["pelvis"]
                lie.append(math.hypot(a_[0] - b_[0], a_[1] - b_[1]) * 30.0)
            prev[k] = m
        if tackler and tackler in men and carrier in men:
            g, _, _ = pair_metrics(men[tackler], men[carrier])
            if t_tackle is not None and gap_hit is None and simt >= t_tackle - 1e-6:
                gap_hit = g
            if t_wrap is not None and t_wrap <= simt < (t_tackle or 1e9):
                wrap_gaps.append(g)
            if t_tackle is not None and t_tackle <= simt and men[tackler]["anim"].startswith("fall") and men[carrier]["anim"].startswith("fall"):
                if len(fall_gaps) < 30: fall_gaps.append(g)
            if men[tackler]["sanim"] == "tackle" and (t_tackle is None or simt <= t_tackle + 0.5):
                hand_frames += 1
                if hand_gap(men[tackler], men[carrier]) < 12.0: hand_ok += 1
        for h in helpers:
            if h in men and carrier in men and men[h]["anim"].startswith("fall") and t_tackle is not None and men[h]["simt"] > t_tackle + 0.9:
                # lying on the pile: his lowest core point vs the top of the carrier's body under him
                hc = [p for p in capsules(men[h]) if p[3] == "core"]
                cc = [p for p in capsules(men[carrier]) if p[3] == "core"]
                if hc and cc:
                    hx = min(min(p[0][2], p[1][2]) - p[2] for p in hc)
                    ctop = max(max(p[0][2], p[1][2]) + p[2] for p in cc)
                    hp = men[h]["b"].get("spine_03"); cp = men[carrier]["b"].get("spine_03")
                    if hp and cp and math.hypot(hp[0] - cp[0], hp[1] - cp[1]) < 60:
                        pile_float = max(pile_float, hx - ctop)
    for (k, bn), ser in track.items():
        fs = sorted(ser)
        d = {}
        for a_, b_ in zip(fs, fs[1:]):
            if b_ == a_ + 1: d[b_] = norm(sub(ser[b_][0], ser[a_][0]))
        for f in d:
            if f - 1 in d and f + 1 in d:
                spike = d[f] - max(d[f - 1], d[f + 1])
                if spike > 0: pops.append((spike, f, k, bn, ser[f][1]))
    # did the carrier go down? (his lowest pelvis from the tackle on)
    carrier_lo = None
    if t_tackle is not None:
        for fr in fr_sorted:
            m = frames[fr].get(carrier)
            if m and m["simt"] >= t_tackle - 1e-6 and "pelvis" in m["b"]:
                carrier_lo = m["b"]["pelvis"][2] if carrier_lo is None else min(carrier_lo, m["b"]["pelvis"][2])
    # velocity across the switch into the fall (3-frame windows either side)
    hit_jump = 0.0
    for k in set(part_all):
        ser = [(f, frames[f][k]) for f in fr_sorted if k in frames[f] and "pelvis" in frames[f][k]["b"]]
        for i in range(3, len(ser) - 3):
            if ser[i][1]["anim"].startswith("fall:") and not ser[i - 1][1]["anim"].startswith("fall:") and ser[i + 3][0] - ser[i - 3][0] == 6:
                p = [x[1]["b"]["pelvis"] for x in ser[i - 3:i + 3]]
                v0 = ((p[2][0] - p[0][0]) * 15, (p[2][1] - p[0][1]) * 15)
                v1 = ((p[5][0] - p[3][0]) * 15, (p[5][1] - p[3][1]) * 15)
                hit_jump = max(hit_jump, math.hypot(v1[0] - v0[0], v1[1] - v0[1]))
    pops.sort(reverse=True)
    res.update({
        "core_pen": round(core_pen, 1), "core_where": core_where, "limb_pen": round(limb_pen, 1),
        "gap_hit": None if gap_hit is None else round(gap_hit, 1),
        "gap_fall": round(sum(fall_gaps) / len(fall_gaps), 1) if fall_gaps else None,
        "gap_wrap": round(sum(wrap_gaps) / len(wrap_gaps), 1) if wrap_gaps else None,
        "hands": round(hand_ok / hand_frames, 2) if hand_frames else None,
        "turf": round(turf, 1), "pop": round(pops[0][0], 1) if pops else 0.0,
        "pop_where": pops[0][1:] if pops else None, "pops_over_25": sum(1 for p in pops if p[0] > 15), "lie_slide": round(sum(lie) / len(lie), 1) if lie else None, "hit_jump": round(hit_jump), "carrier_lo": None if carrier_lo is None else round(carrier_lo),
        "pile_float": round(pile_float, 1),
    })
    return res


def main():
    cap = sys.argv[1]
    only = None
    out = None
    a = sys.argv[2:]
    if "--clip" in a: only = int(a[a.index("--clip") + 1])
    if "--json" in a: out = a[a.index("--json") + 1]
    clips = sorted(int(os.path.basename(p)[1:4]) for p in glob.glob(os.path.join(cap, "tcdata", "t[0-9][0-9][0-9].csv")))
    rows = []
    for n in clips:
        if only and n != only: continue
        r = analyze_clip(cap, n)
        if r: rows.append(r)
    keys = ["clip", "frames", "tackle", "wrap", "assists", "note", "core_pen", "limb_pen", "gap_hit", "gap_wrap", "gap_fall", "hands", "turf", "pop", "pops_over_25", "lie_slide"]
    print(" ".join("%9s" % k[:9] for k in keys))
    for r in rows:
        print(" ".join("%9s" % (str(r.get(k))[:9]) for k in keys))
    T = [r for r in rows if r.get("tackle")]
    def mean(k, rs):
        v = [r[k] for r in rs if r.get(k) is not None]
        return round(sum(v) / len(v), 1) if v else None
    def mx(k, rs):
        v = [r[k] for r in rs if r.get(k) is not None]
        return round(max(v), 1) if v else None
    summ = {"clips": len(rows), "tackles": len(T), "core_pen_mean": mean("core_pen", T), "core_pen_max": mx("core_pen", T),
            "limb_pen_mean": mean("limb_pen", T), "gap_hit_mean": mean("gap_hit", T), "gap_hit_max": mx("gap_hit", T),
            "gap_fall_mean": mean("gap_fall", T), "gap_wrap_mean": mean("gap_wrap", T), "hands_mean": mean("hands", T),
            "turf_min": min((r["turf"] for r in T), default=None), "pop_mean": mean("pop", T), "pop_max": mx("pop", T),
            "pops_over_15": sum(r.get("pops_over_25", 0) for r in T), "lie_slide_mean": mean("lie_slide", T), "hit_jump_mean": mean("hit_jump", T), "carrier_up": sum(1 for r in T if r.get("carrier_lo") is not None and r["carrier_lo"] > 60),
            "carrier_unseen": sum(1 for r in T if r.get("carrier_lo") is None), "hit_jump_max": mx("hit_jump", T), "pile_float_max": mx("pile_float", T)}
    print("\nsummary:", json.dumps(summ))
    for r in rows:
        if r.get("core_where"): print("clip %d deepest core overlap at frame %s: %s vs %s (%s | %s)" % ((r["clip"],) + tuple(r["core_where"])))
        if r.get("pop_where"): print("clip %d biggest pop %.0f cm at frame %s %s %s (%s)" % ((r["clip"], r["pop"]) + tuple(r["pop_where"])))
    if out:
        with open(out, "w") as f: json.dump({"summary": summ, "clips": rows}, f, indent=1, default=str)


if __name__ == "__main__":
    main()
