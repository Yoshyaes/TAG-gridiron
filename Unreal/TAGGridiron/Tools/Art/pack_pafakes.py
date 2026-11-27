# Unreal Editor Python (headless). The pack library's two-man play-action fakes (QB and back) as pairs the game can time:
# each capture's two parts followed through their shared world (the sidecar's anchor + root motion, as pack_handoffs.py
# does), and the fake found where the ball is shown - the quarterback's hands nearest the back's chest - and then pulled
# (the quarterback's part goes on into his drop or his boot).
#   Q, B          the quarterback's part and the back's
#   T             seconds into the captures of the fake (the mesh)
#   Bx, By, BYaw  the back's root in the quarterback's frame at T (m; x left, y back; yaw relative)
#   Gun           shotgun / pistol (else under center)
#   Boot          the quarterback boots out after the fake (else he drops)
#   D             the closest the hands come to the chest (m)
# Output: Content/TAG/AnimH/Pack/pack_pafakes.json for FAthleteAssets (play action).
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import glob
import json
import math
import os
import re

import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJ, "Content", "TAG", "AnimH", "Pack", "pack_pafakes.json")
opts = unreal.AnimPoseEvaluationOptions()
_skel = unreal.load_asset("/Game/TAG/Hero/Rig/SKM_TAG_HeroRig_Skeleton")
ROOT_REST_YAW = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_reference_pose(_skel), "root",
                                                        unreal.AnimPoseSpaces.LOCAL).rotation.rotator().yaw
SKIP = ("Pumpfake", "FakeSnap", "FakeFG", "FakePunt", "Fake_FG", "Kicker", "Upback", "Spike", "3Player", "Wildcat", "Reverse")
WANT = ("_PA_", "Fake_Handoff", "_Boot")


def world_of(anchor, heading_deg, x_left, y_back):
    h = math.radians(heading_deg)
    fwd = (math.sin(h), -math.cos(h))
    left = (math.cos(h), math.sin(h))
    return (anchor[0] + left[0] * x_left - fwd[0] * y_back, anchor[1] + left[1] * x_left - fwd[1] * y_back)


def clip_xy(tr):
    return tr.translation.x / 100.0, -tr.translation.y / 100.0


def man(anim, j):
    anc, h0 = j["source"]["anchor"], j["source"]["anchor_heading_deg"]
    L = anim.get_play_length()
    n = int(round(L * 30)) + 1
    rows = []
    for i in range(n):
        t = min(L, i / 30.0)
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts)
        get = lambda b: unreal.AnimPoseExtensions.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD)
        r = get("root")
        hl, hr, ch = get("hand_l"), get("hand_r"), get("spine_05")
        hx = (hl.translation.x + hr.translation.x) / 200.0
        hy = -(hl.translation.y + hr.translation.y) / 200.0
        cx, cy = clip_xy(ch)
        rx, ry = clip_xy(r)
        yaw = -(r.rotation.rotator().yaw - ROOT_REST_YAW)
        rows.append({"hands": world_of(anc, h0, hx, hy), "chest": world_of(anc, h0, cx, cy), "root": world_of(anc, h0, rx, ry), "yaw": h0 + yaw})
    return rows, L


def rel(p_from, h_from, p_to):
    h = math.radians(h_from)
    fwd = (math.sin(h), -math.cos(h))
    left = (math.cos(h), math.sin(h))
    dx, dy = p_to[0] - p_from[0], p_to[1] - p_from[1]
    return dx * left[0] + dy * left[1], -(dx * fwd[0] + dy * fwd[1])


def wrap180(a):
    return math.degrees(math.atan2(math.sin(math.radians(a)), math.cos(math.radians(a))))


groups = {}
for cat in ("Quarterback", "Catching", "BallCarrier"):
    for jpath in glob.glob(os.path.join(PROJ, "RawArt", "Anim", "PackHero", cat, "A_TAG_Pack_*.json")):
        name = os.path.splitext(os.path.basename(jpath))[0]
        m = re.match(r"^(.*)__P(\d+)$", name)
        if not m or not any(w in name for w in WANT) or any(s in name for s in SKIP):
            continue
        groups.setdefault((cat, m.group(1)), {})[m.group(2)] = jpath
rows = []
for (cat, g), parts in sorted(groups.items()):
    if sorted(parts) != ["01", "02"]:
        continue
    try:
        js = {k: json.load(open(parts[k])) for k in parts}
        ns = {k: os.path.splitext(os.path.basename(parts[k]))[0] for k in parts}
        an = {k: unreal.load_asset("/Game/TAG/AnimH/Pack/%s/%s" % (cat, ns[k])) for k in ns}
        if not all(an.values()):
            continue
        A, LA = man(an["01"], js["01"])
        B, LB = man(an["02"], js["02"])
        n = min(len(A), len(B))
        best = (9.0, 0, "01")
        for i in range(n):
            dab = math.dist(A[i]["hands"], B[i]["chest"])
            dba = math.dist(B[i]["hands"], A[i]["chest"])
            if dab < best[0]:
                best = (dab, i, "01")
            if dba < best[0]:
                best = (dba, i, "02")
        d, i, qk = best
        if d > 0.6:
            continue
        Q, R = (A, B) if qk == "01" else (B, A)
        bk = "02" if qk == "01" else "01"
        bx, by = rel(Q[i]["root"], Q[i]["yaw"], R[i]["root"])
        rows.append({"Name": g.replace("A_TAG_Pack_", ""), "Cat": cat, "Q": ns[qk], "B": ns[bk], "T": round(i / 30.0, 2), "D": round(d, 2),
                     "Bx": round(bx, 2), "By": round(by, 2), "BYaw": round(wrap180(R[i]["yaw"] - Q[i]["yaw"]), 1),
                     "Gun": ("Shotgun" in g or "Pistol" in g or "SG" in g), "Boot": "Boot" in g, "Len": round(min(LA, LB), 2)})
    except Exception as e:
        unreal.log_warning("TAG_PACK_PAFAKES %s: %s" % (g, str(e)[:160]))
json.dump(rows, open(OUT, "w"), indent=0)
unreal.log("TAG_PACK_PAFAKES %d fakes" % len(rows))
