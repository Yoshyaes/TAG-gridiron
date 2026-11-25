# Unreal Editor Python (headless). The pack library's three-man pass-block double teams as groups the game can play:
# the defender (the man whose heading at the start is opposite the other two) and his two blockers, followed through
# the capture's shared world the way pack_gangs.py follows a gang tackle (each man's anchor = his sidecar's anchor,
# plus his root motion).
# Per group, the stretch where both blockers are on him (pelvises within 1.3 m of his) and the pile is steady (his
# root under 0.9 m/s), its longest run of at least 0.8 s:
#   D, A, B        the defender's part, the blocker on his left at the stretch's start, the one on his right
#   T0, T1         seconds into the captures
#   Ax, Ay, AYaw   blocker A's root in the defender's frame at T0 (m; x left, y back; yaw relative, + turns left)
#   Bx, By, BYaw   blocker B's
#   Drift          how far the defender's root goes over the stretch (m)
#   Kind           "hold" (a double team that holds), "shed" (the defender beats it: DefWin / Spits / Splits / Beats / Shed),
#                  "run" (a run block's double team, the combo: RunBlock_DbleTm_* / RunBlock_DoubleTeam_*)
#   Out            (run) how the capture ends: "drive" (the two move him off the ball: OffWin, Push, Power, Zone_Fwd),
#                  "hold" (Stalemate, Neutral), "climb" (one comes off: Takeover, Disengage, Chip, Swap, ShoveExit, NonEng),
#                  "defwin" (he beats it: DefWin, Shed, Split, Leave)
# A run double's pile travels (the two drive him back): its steady stretch allows the defender 2.5 m/s, and 0.6 s.
# Output: Content/TAG/AnimH/Pack/pack_dbl.json for FAthleteAssets (double teams), and Saved/pack_dbl_report.json.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import glob
import json
import math
import os
import re

import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJ, "RawArt", "Anim", "PackHero", "Blocking")
OUT = os.path.join(PROJ, "Content", "TAG", "AnimH", "Pack", "pack_dbl.json")
REPORT = os.path.join(PROJ, "Saved", "pack_dbl_report.json")
LIB = "/Game/TAG/AnimH/Pack/Blocking/"
opts = unreal.AnimPoseEvaluationOptions()
_skel = unreal.load_asset("/Game/TAG/Hero/Rig/SKM_TAG_HeroRig_Skeleton")
ROOT_REST_YAW = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_reference_pose(_skel), "root",
                                                        unreal.AnimPoseSpaces.LOCAL).rotation.rotator().yaw
WANT = re.compile(r"(PassBlock_D(ou)?ble?T|PassBlock_DbleTm|DoubleTeam_PassBlock|Passblock_3man)")
RUN = re.compile(r"RunBlock_D(bleTm|bletm|bleTeam|oubleTeam)")  # (the run doubles: 100 groups in the pack)
SHED = ("DefWin", "Spits", "Splits", "Beats", "Shed", "Fail_Inside")
RUN_OUT = (("defwin", ("DefWin", "Shed", "Split", "Leave")), ("climb", ("Takeover", "Disengage", "DisEng", "Chip", "Swap", "ShoveExit", "NonEng")),
           ("hold", ("Stalemate", "Neutral", "_Neu")))


def run_out(g):
    gl = g.lower()
    for out, keys in RUN_OUT:
        if any(k.lower() in gl for k in keys):
            return out
    return "drive"


def bone(anim, t, b):
    return unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts), b, unreal.AnimPoseSpaces.WORLD)


def clip_xy(tr):
    return tr.translation.x / 100.0, -tr.translation.y / 100.0


def world_of(anchor, heading_deg, x_left, y_back):
    h = math.radians(heading_deg)
    fwd = (math.sin(h), -math.cos(h))
    left = (math.cos(h), math.sin(h))
    return (anchor[0] + left[0] * x_left - fwd[0] * y_back, anchor[1] + left[1] * x_left - fwd[1] * y_back)


def man(anim, j):
    anc, h0 = j["source"]["anchor"], j["source"]["anchor_heading_deg"]
    L = anim.get_play_length()
    n = int(round(L * 30)) + 1
    rows = []
    for i in range(n):
        t = min(L, i / 30.0)
        r = bone(anim, t, "root")
        p = bone(anim, t, "pelvis")
        rx, ry = clip_xy(r)
        px, py = clip_xy(p)
        yaw = -(r.rotation.rotator().yaw - ROOT_REST_YAW)
        rows.append((world_of(anc, h0, px, py), world_of(anc, h0, rx, ry), h0 + yaw))
    return rows, L


def rel(p_from, h_from, p_to):
    h = math.radians(h_from)
    fwd = (math.sin(h), -math.cos(h))
    left = (math.cos(h), math.sin(h))
    dx, dy = p_to[0] - p_from[0], p_to[1] - p_from[1]
    return dx * left[0] + dy * left[1], -(dx * fwd[0] + dy * fwd[1])


def wrap180(a):
    return math.degrees(math.atan2(math.sin(math.radians(a)), math.cos(math.radians(a))))


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


groups = {}
for jpath in glob.glob(os.path.join(SRC, "A_TAG_Pack_*.json")):
    name = os.path.splitext(os.path.basename(jpath))[0]
    m = re.match(r"^(.*)__P(\d+)$", name)
    if not m or not (WANT.search(name) or RUN.search(name)):
        continue
    groups.setdefault(m.group(1), {})[m.group(2)] = jpath
rows, report = [], []
for g, parts in sorted(groups.items()):
    if sorted(parts) != ["01", "02", "03"]:
        continue
    try:
        js = {k: json.load(open(parts[k])) for k in parts}
        ns = {k: os.path.splitext(os.path.basename(parts[k]))[0] for k in parts}
        # the defender: the one heading against the other two
        hs = {k: js[k]["source"]["anchor_heading_deg"] for k in js}
        def odd(k):
            others = [o for o in hs if o != k]
            return sum(abs(wrap180(hs[k] - hs[o])) for o in others) - abs(wrap180(hs[others[0]] - hs[others[1]]))
        dk = max(hs, key=odd)
        bk = [k for k in hs if k != dk]
        an = {k: unreal.load_asset(LIB + ns[k]) for k in ns}
        if not all(an.values()):
            report.append({"group": g, "skip": "assets"})
            continue
        D, LD = man(an[dk], js[dk])
        B1, L1 = man(an[bk[0]], js[bk[0]])
        B2, L2 = man(an[bk[1]], js[bk[1]])
        n = min(len(D), len(B1), len(B2))
        bRun = bool(RUN.search(g))
        ok = []
        for i in range(n):
            on = dist(D[i][0], B1[i][0]) < 1.3 and dist(D[i][0], B2[i][0]) < 1.3
            sp = dist(D[i][1], D[max(0, i - 3)][1]) / max(1, i - max(0, i - 3)) * 30.0 if i else 0.0
            ok.append(on and sp < (2.5 if bRun else 0.9))
        best, cur, s0 = (0, 0), 0, 0
        for i in range(n + 1):
            if i < n and ok[i]:
                if cur == 0:
                    s0 = i
                cur += 1
            else:
                if cur > best[1] - best[0]:
                    best = (s0, s0 + cur)
                cur = 0
        i0, i1 = best
        entry = {"group": g, "D": ns[dk], "frames_on": sum(ok), "best": [round(i0 / 30, 2), round(i1 / 30, 2)], "run": bRun}
        report.append(entry)
        if (i1 - i0) / 30.0 < (0.6 if bRun else 0.8):
            continue
        dp, dr, dh = D[i0]
        a = rel(dr, dh, B1[i0][1])
        b = rel(dr, dh, B2[i0][1])
        (ka, A, kb, B) = (bk[0], B1, bk[1], B2) if a[0] >= b[0] else (bk[1], B2, bk[0], B1)
        ax, ay = rel(dr, dh, A[i0][1])
        bx, by = rel(dr, dh, B[i0][1])
        rows.append({"Name": g.replace("A_TAG_Pack_", ""), "D": ns[dk], "A": ns[ka], "B": ns[kb], "T0": round(i0 / 30.0, 2), "T1": round(i1 / 30.0, 2),
                     "Ax": round(ax, 2), "Ay": round(ay, 2), "AYaw": round(wrap180(A[i0][2] - dh), 1),
                     "Bx": round(bx, 2), "By": round(by, 2), "BYaw": round(wrap180(B[i0][2] - dh), 1),
                     "Drift": round(dist(D[i0][1], D[i1 - 1][1]), 2),
                     "Kind": "run" if bRun else "shed" if any(s in g for s in SHED) else "hold", "Out": run_out(g) if bRun else ""})
    except Exception as e:
        report.append({"group": g, "error": str(e)[:160]})
json.dump(rows, open(OUT, "w"), indent=0)
json.dump(report, open(REPORT, "w"), indent=0)
