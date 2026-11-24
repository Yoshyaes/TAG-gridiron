"""Who has the ball in the pack's two- and three-man tackle captures, from the source mocap (no Unreal needed).

The tables the game places tackles from label each capture's men: pack_pairs.json (C carrier / T tackler),
pack_gangs.json (C / T / A) and pack_turnovers.json strips (C / T). pack_pairs.py and pack_gangs.py took __P01 as the
carrier, but Madden's participant order is not fixed: in 59 of 381 pairs and 28 of 42 gangs the man with the ball is
__P02 / __P03. The game plays the C capture on the sim's carrier, so in those tackles the carrier's body moved like the
tackler and the tackler's like the carrier (tucked arm and all) while the ball stayed in the carrier's hand (Fred,
2026-11-22: "the ball carrier turns into the tackler and the tackler into the ball carrier").

The test: a carried ball is a ball socket (the pack rig's RightFootball / LeftFootball, 14 cm off the wrist) held still
at the chest. For each man and hand, the socket's distance from Spine3 over the frames from the clip's start to the
contact (at least 0.4 s): carry score = min over hands of mean + 1.5 * std (m). Carriers score ~0.30-0.40, a man
pumping his arms or reaching to tackle 0.5-0.8. Also the score over the last 0.67 s, so a capture whose ball ends up
with another man is caught.

  python pack_role_check.py               report (pairs, gangs, strips, interceptions)
  python pack_role_check.py --fix         also rewrite the three tables: a pair whose ball is the tackler's has its
                                          roles swapped (relation inverted exactly, CMinZ from the new carrier's json);
                                          a gang whose carrier is the first tackler is re-expressed around him; what
                                          cannot be re-roled from the table alone (a gang whose ball man is the second
                                          tackler, a reversed strip, a pair or gang with no ball - the RunBlock_Reach
                                          captures are two linemen - or no clear carrier) gets "RoleBad", which
                                          FAthleteAssets skips. The first --fix keeps <table>.roles_prev.json.
                                          An interception whose receiver (R) clearly holds the ball after the catch and
                                          whose defender (D) does not gets "RoleBad" too (the ball goes to the defender
                                          on the sim's INT; a reversed capture shows it going to the receiver).
  python pack_role_check.py --log <game.log>   count a -TagTackleCheck run's "TAG tcheck" lines: tackles whose
                                          capture puts the tackler's motion on the carrier, frames the tackler shows the
                                          carry, the ball drawn nearer the tackler without a turnover; PASS = none.
                                          Idempotent: a fixed table checks clean.
Sources: D:/MaddenRef/MocapPack/BVH/<Category>/<clip>.bvh (TAG_MOCAP_BVH overrides), the clip sidecars in
RawArt/Anim/PackHero/<Category> (the source clip, its first frame, mirror) and Content/TAG/AnimH/Pack/*.json.
"""
import json
import math
import os
import re
import shutil
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.abspath(os.path.join(HERE, "..", ".."))
PACK = os.path.join(PROJ, "Content", "TAG", "AnimH", "Pack")
RAW = os.path.join(PROJ, "RawArt", "Anim", "PackHero")
BVH = os.environ.get("TAG_MOCAP_BVH", r"D:\MaddenRef\MocapPack\BVH")
JOINTS = ("AITrajectory", "Hips", "Spine3", "LeftFootball", "RightFootball")

# classification (carry scores, m)
SWAP_MARGIN = 0.12   # the other man's score at least this much lower than the labelled carrier's
CARRY_MAX = 0.50     # ... and his own a carry
NOBALL = 0.50        # neither man under this before the contact or within a second after: no ball in the capture
MOVE_TO = 0.38       # the ball ends with another man: his end score under this ...
MOVE_FROM = 0.55     # ... and the labelled carrier's over this


# ------------------------------------------------------------------ BVH forward kinematics
def _rot(axis, deg):
    a = np.radians(deg)
    c, s = np.cos(a), np.sin(a)
    M = np.zeros((len(a), 3, 3))
    if axis == "X":
        M[:, 0, 0] = 1; M[:, 1, 1] = c; M[:, 1, 2] = -s; M[:, 2, 1] = s; M[:, 2, 2] = c
    elif axis == "Y":
        M[:, 1, 1] = 1; M[:, 0, 0] = c; M[:, 0, 2] = s; M[:, 2, 0] = -s; M[:, 2, 2] = c
    else:
        M[:, 2, 2] = 1; M[:, 0, 0] = c; M[:, 0, 1] = -s; M[:, 1, 0] = s; M[:, 1, 1] = c
    return M


def load_bvh(path, want=JOINTS, hz=30.0):
    """World positions (m, BVH axes) of the wanted joints, resampled to hz."""
    head, motion = open(path).read().split("MOTION", 1)
    joints, stack, col, pending = [], [], 0, None
    tok = head.split()
    i = 0
    while i < len(tok):
        t = tok[i]
        if t in ("ROOT", "JOINT"):
            pending = tok[i + 1]; i += 2; continue
        if t == "End":
            pending = "__end"; i += 2; continue
        if t == "{":
            joints.append([pending, stack[-1] if stack else None, np.zeros(3), [], col]); stack.append(len(joints) - 1); i += 1; continue
        if t == "}":
            stack.pop(); i += 1; continue
        if t == "OFFSET":
            joints[stack[-1]][2] = np.array([float(tok[i + 1]), float(tok[i + 2]), float(tok[i + 3])]); i += 4; continue
        if t == "CHANNELS":
            k = int(tok[i + 1]); joints[stack[-1]][3] = tok[i + 2:i + 2 + k]; joints[stack[-1]][4] = col; col += k; i += 2 + k; continue
        i += 1
    lines = motion.strip().splitlines()
    nf = int(lines[0].split(":")[1])
    ft = float(lines[1].split(":")[1])
    data = np.array([[float(v) for v in l.split()] for l in lines[2:2 + nf]])
    idx = {j[0]: k for k, j in enumerate(joints)}
    need = set()
    for w in want:
        k = idx.get(w)
        while k is not None and k not in need:
            need.add(k)
            k = joints[k][1]
    W, out = {}, {}
    for k in sorted(need):
        name, parent, off, ch, c0 = joints[k]
        T = np.tile(off, (nf, 1))
        R = np.tile(np.eye(3), (nf, 1, 1))
        for ci, cn in enumerate(ch):
            if cn.endswith("position"):
                T[:, "XYZ".index(cn[0])] = data[:, c0 + ci]
        for ci, cn in enumerate(ch):
            if cn.endswith("rotation"):
                R = R @ _rot(cn[0], data[:, c0 + ci])
        if parent is None:
            W[k] = (R, T)
        else:
            PR, PT = W[parent]
            W[k] = (PR @ R, PT + np.einsum("nij,nj->ni", PR, T))
        if name in want:
            out[name] = W[k][1]
    dur = (nf - 1) * ft
    src_t = np.arange(nf) * ft
    dst_t = np.minimum(np.arange(int(round(dur * hz)) + 1) / hz, dur)
    return {n: np.stack([np.interp(dst_t, src_t, P[:, a]) for a in range(3)], axis=1) / 100.0 for n, P in out.items()}


_cache = {}


def man(asset, cat):
    """A clip of the hero library (A_TAG_Pack_<clip>) as the source capture's joints, from the clip's first frame."""
    key = (asset, cat)
    if key not in _cache:
        sj = os.path.join(RAW, cat, asset + ".json")
        meta = json.load(open(sj)) if os.path.exists(sj) else None
        src = meta["source"]["src"] if meta else asset.replace("A_TAG_Pack_", "")
        path = os.path.join(BVH, cat, src + ".bvh")
        M = None
        if os.path.exists(path):
            M = load_bvh(path)
            if meta:
                off = int(round(meta["source"]["src_frames"][0] / meta["source"]["src_fps"] * 30))
                M = {k: v[off:] for k, v in M.items()}
        _cache[key] = (M, meta)
    return _cache[key]


def carry(M, i0, i1):
    """Carry score over frames [i0, i1): min over hands of mean + 1.5 std of the ball socket's distance from Spine3."""
    n = len(M["Spine3"])
    i1 = min(i1, n)
    i0 = max(0, min(i0, i1 - 4))
    best = 9.0
    for h in ("RightFootball", "LeftFootball"):
        d = np.linalg.norm(M[h][i0:i1] - M["Spine3"][i0:i1], axis=1)
        best = min(best, float(d.mean() + 1.5 * d.std()))
    return best


def scores(roles, cat, start):
    """{role: (before the contact, through a second after it, at the end)} or None when a capture is missing."""
    out = {}
    i = int(round(start * 30))
    for r, a in roles.items():
        M, _ = man(a, cat)
        if M is None:
            return None
        n = len(M["Spine3"])
        out[r] = (carry(M, 0, max(i, 12)), carry(M, 0, i + 30), carry(M, n - 20, n))
    return out


def verdict(sc, carrier="C"):
    """'ok', 'swap:<role>' (another man is the one with the ball), 'noball' or 'moves:<role>' (the ball ends with him)."""
    others = [r for r in sc if r != carrier]
    c = sc[carrier]
    o = min(others, key=lambda r: sc[r][0])
    if sc[o][0] < c[0] - SWAP_MARGIN and sc[o][0] < CARRY_MAX and sc[o][1] <= c[1]:
        return "swap:" + o
    if min(s[0] for s in sc.values()) > NOBALL and min(s[1] for s in sc.values()) > NOBALL:
        return "noball"
    if c[0] > sc[o][0] + 0.06 and c[0] > 0.42:
        return "unsure:" + o  # (the other man a little steadier at the chest: neither clearly the carrier)
    oe = min(others, key=lambda r: sc[r][2])
    if c[0] < 0.42 and sc[oe][2] < MOVE_TO and c[2] > MOVE_FROM:
        return "moves:" + oe
    return "ok"


# ------------------------------------------------------------------ the capture frame (x left, y back, m; + yaw left)
def world_of(anchor, heading_deg, x_left, y_back):
    h = math.radians(heading_deg)
    fwd = (math.sin(h), -math.cos(h))
    left = (math.cos(h), math.sin(h))
    return (anchor[0] + left[0] * x_left - fwd[0] * y_back, anchor[1] + left[1] * x_left - fwd[1] * y_back)


def rel(p_from, h_from, p_to):
    h = math.radians(h_from)
    fwd = (math.sin(h), -math.cos(h))
    left = (math.cos(h), math.sin(h))
    dx, dy = p_to[0] - p_from[0], p_to[1] - p_from[1]
    return dx * left[0] + dy * left[1], -(dx * fwd[0] + dy * fwd[1])


def wrap180(a):
    return math.degrees(math.atan2(math.sin(math.radians(a)), math.cos(math.radians(a))))


def cminz(asset, cat="Tackles"):
    _, meta = man(asset, cat)
    return round(meta["pelvis_z"]["min"], 3) if meta else None


def swap_pair(p):
    """The same capture with the tackler as the carrier: relation inverted, per-man fields exchanged."""
    q = dict(p)
    cpos = world_of((0.0, 0.0), 0.0, p["Ox"], p["Oy"])
    ox, oy = rel(cpos, p["Yaw"], (0.0, 0.0))
    q.update({"T": p["C"], "C": p["T"], "Ox": round(ox, 2), "Oy": round(oy, 2), "Yaw": round(wrap180(-p["Yaw"]), 1)})
    for a, b in (("LenT", "LenC"), ("TravelT", "TravelC")):
        if a in p and b in p:
            q[a], q[b] = p[b], p[a]
    # (the new carrier's pelvis end is not in the table: his root's travel stands in, the game reads it only in a log)
    if "TravelT" in p:
        q["EndX"], q["EndY"] = p["TravelT"][0], p["TravelT"][1]
    m = re.search(r"_D(\d)", p["T"])
    code = int(m.group(1)) if m else 1
    if p["Kind"] in ("Drive", "Over") and code in (1, 2, 8):
        q["Kind"] = "Over" if q["EndY"] < -0.3 else "Drive"
    q["TLie"], q["CLie"] = (0.70, 0.90) if q["Kind"] == "Dive" else (1.05, 0.45) if q["Kind"] == "Over" else (0.45, 1.05)
    z = cminz(q["C"])
    if z is not None:
        q["CMinZ"] = z
    q["Roles"] = "swapped"
    return q


def rerole_gang(g):
    """A gang whose ball man is the first tackler (T): him as the carrier, the old carrier (a tackler) as T."""
    q = dict(g)
    tpos = world_of((0.0, 0.0), 0.0, g["Tx"], g["Ty"])
    apos = world_of((0.0, 0.0), 0.0, g["Ax"], g["Ay"])
    tx, ty = rel(tpos, g["TYaw"], (0.0, 0.0))
    ax, ay = rel(tpos, g["TYaw"], apos)
    q.update({"C": g["T"], "T": g["C"], "Tx": round(tx, 2), "Ty": round(ty, 2), "TYaw": round(wrap180(-g["TYaw"]), 1),
              "Ax": round(ax, 2), "Ay": round(ay, 2), "AYaw": round(wrap180(g["AYaw"] - g["TYaw"]), 1), "EndX": 0.0, "EndY": 0.0})
    z = cminz(q["C"])
    if z is not None:
        q["CMinZ"] = z
    q["Roles"] = "swapped"
    return q


def log_report(path, swap_frames=6, off_frames=3):
    """Count a -TagTackleCheck game log's tackles: the start verdicts (SIM MISMATCH / CAPTURE ROLE SWAP / ok), the
    tackles where the tackler showed the carry and the carrier did not for >= swap_frames frames of the first second,
    and those whose drawn ball was nearer the tackler for >= off_frames frames while the sim's carrier had it."""
    starts, done = {}, []
    rs = re.compile(r"TAG tcheck start (\d+) (\w+): .* -> (.+)$")
    rd = re.compile(r"TAG tcheck done (\d+) (\w+) (\S+): carrier #(-?\d+), (\d+) frames, tackler shows the carry (\d+), ball nearer the tackler (\d+), sim turnover (\d), sim mismatch (\d)")
    loose = re.compile(r"TAG: (tackle pair|gang tackle|strip) (\S+) left out: (the ball.*)$")
    left = []
    for line in open(path, errors="replace"):
        m = rs.search(line)
        if m:
            starts[m.group(1)] = (m.group(2), m.group(3).strip())
            continue
        m = rd.search(line)
        if m:
            done.append(dict(id=m.group(1), kind=m.group(2), clip=m.group(3), frames=int(m.group(5)), swap=int(m.group(6)), off=int(m.group(7)),
                             turnover=int(m.group(8)), mismatch=int(m.group(9))))
            continue
        m = loose.search(line)
        if m:
            left.append((m.group(1), m.group(2), m.group(3)))
    verd = {}
    for k, (kind, v) in starts.items():
        verd[v] = verd.get(v, 0) + 1
    swaps = [d for d in done if d["swap"] >= swap_frames]
    offs = [d for d in done if d["off"] >= off_frames and not d["turnover"]]
    print("tackles started %d %s, finished %d" % (len(starts), json.dumps(verd), len(done)))
    print("role swaps on screen (tackler carries >= %d frames): %d" % (swap_frames, len(swaps)))
    for d in swaps:
        print("   %s %s %s: %d of %d frames" % (d["id"], d["kind"], d["clip"], d["swap"], d["frames"]))
    print("ball nearer the tackler without a turnover (>= %d frames): %d" % (off_frames, len(offs)))
    for d in offs:
        print("   %s %s %s: %d of %d frames" % (d["id"], d["kind"], d["clip"], d["off"], d["frames"]))
    print("captures the loader's role guard left out: %d" % len(left))
    for k, n, why in left[:40]:
        print("   %s %s: %s" % (k, n, why))
    ok = verd.get("ok", 0) == len(starts) and not swaps and not offs
    print("PASS" if ok else "FAIL")
    return ok


def main():
    if "--log" in sys.argv:
        sys.exit(0 if log_report(sys.argv[sys.argv.index("--log") + 1]) else 1)
    fix = "--fix" in sys.argv
    verbose = "-v" in sys.argv
    files = {k: os.path.join(PACK, k) for k in ("pack_pairs.json", "pack_gangs.json", "pack_turnovers.json")}
    pairs = json.load(open(files["pack_pairs.json"]))
    gangs = json.load(open(files["pack_gangs.json"]))
    turn = json.load(open(files["pack_turnovers.json"]))
    tally = {}

    def note(kind, v, name, sc):
        tally.setdefault(kind, {}).setdefault(v.split(":")[0], 0)
        tally[kind][v.split(":")[0]] += 1
        if v != "ok" or verbose:
            print("%-5s %-10s %-72s %s" % (kind, v, name.replace("A_TAG_Pack_", "")[:72],
                                           "  ".join("%s %.2f/%.2f/%.2f" % (r, s[0], s[1], s[2]) for r, s in sc.items())))

    new_pairs = []
    for p in pairs:
        sc = scores({"C": p["C"], "T": p["T"]}, "Tackles", p["Start"])
        if sc is None:
            new_pairs.append(p)
            continue
        v = verdict(sc)
        note("pair", v, p["T"], sc)
        if v == "swap:T":
            new_pairs.append(swap_pair(p))
        elif v == "ok":
            q = dict(p)
            q.pop("RoleBad", None)
            new_pairs.append(q)
        else:
            q = dict(p)
            q["RoleBad"] = v
            new_pairs.append(q)
    new_gangs = []
    for g in gangs:
        sc = scores({"C": g["C"], "T": g["T"], "A": g["A"]}, "Tackles", g["Start"])
        if sc is None:
            new_gangs.append(g)
            continue
        v = verdict(sc)
        note("gang", v, g["C"], sc)
        if v == "swap:T":
            new_gangs.append(rerole_gang(g))
        elif v == "ok":
            q = dict(g)
            q.pop("RoleBad", None)
            new_gangs.append(q)
        else:
            q = dict(g)
            q["RoleBad"] = v
            new_gangs.append(q)
    new_strips = []
    for s in turn.get("strips", []):
        sc = scores({"C": s["C"], "T": s["T"]}, s["Cat"], s["Start"])
        q = dict(s)
        if sc is not None:
            v = verdict(sc)
            # (a strip's ball does leave the carrier: only who has it at the start counts; and a sacked quarterback holds
            # it in both hands in front, not tucked - "no ball" there is the test's blind spot, not a missing carrier)
            if v.startswith("moves") or (s.get("Sack") and not v.startswith("swap")):
                v = "ok"
            note("strip", v, s["Name"], sc)
            if v == "ok":
                q.pop("RoleBad", None)
            else:
                q["RoleBad"] = v
        new_strips.append(q)
    # interceptions: the defender (D) is the one holding the ball after the catch (T: his hands' catch), the receiver
    # the one who does not - over 0.2-1.2 s after it. A catch high in both hands or a pile on the turf often shows
    # neither man tucked ("unsure": kept); only a receiver clearly holding it and the defender not is reversed.
    new_ints = []
    for s in turn.get("ints", []):
        q = dict(s)
        Md, _ = man(s["D"], "Catching")
        Mr, _ = man(s["R"], "Catching")
        if Md is not None and Mr is not None:
            n = min(len(Md["Spine3"]), len(Mr["Spine3"]))
            i = int(round(s["T"] * 30))
            a, b = i + 6, min(n, i + 36)
            if b - a < 8:
                v = "short"
                sc = {"D": (9.0,) * 3, "R": (9.0,) * 3}
            else:
                sd, sr = carry(Md, a, b), carry(Mr, a, b)
                sc = {"D": (sd,) * 3, "R": (sr,) * 3}
                v = "swap:R" if sr < sd - SWAP_MARGIN and sr < CARRY_MAX else "ok" if sd < sr - SWAP_MARGIN and sd < CARRY_MAX else "unsure"
            note("int", v, s["Name"], sc)
            if v.startswith("swap"):
                q["RoleBad"] = v
            else:
                q.pop("RoleBad", None)
        new_ints.append(q)
    print("verdicts:", json.dumps(tally))
    if fix:
        turn["strips"] = new_strips
        turn["ints"] = new_ints
        for k, rows in (("pack_pairs.json", new_pairs), ("pack_gangs.json", new_gangs), ("pack_turnovers.json", turn)):
            prev = files[k].replace(".json", ".roles_prev.json")
            if not os.path.exists(prev):
                shutil.copyfile(files[k], prev)
            json.dump(rows, open(files[k], "w"), indent=0)
        print("written: pairs swapped %d / left out %d, gangs re-roled %d / left out %d, strips left out %d" % (
            sum(1 for p in new_pairs if p.get("Roles") == "swapped" and p not in pairs), sum(1 for p in new_pairs if "RoleBad" in p),
            sum(1 for g in new_gangs if g.get("Roles") == "swapped" and g not in gangs), sum(1 for g in new_gangs if "RoleBad" in g),
            sum(1 for s in new_strips if "RoleBad" in s)))


if __name__ == "__main__":
    main()
